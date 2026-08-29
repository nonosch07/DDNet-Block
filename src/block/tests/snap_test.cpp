// Snapshot-level tests for Block cosmetics.
//
// These check what the server actually puts on the wire for a given viewer,
// which is the only way to tell "the cosmetic is applied" from "the cosmetic is
// stored but never sent". The fixture mirrors src/test/gameworld_test.cpp.

#include "../../test/test.h"

#include <base/logger.h>

#include <engine/engine.h>
#include <engine/http.h>
#include <engine/kernel.h>
#include <engine/server/databases/connection_pool.h>
#include <engine/server/register.h>
#include <engine/server/server.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/player.h>
#include <game/version.h>

#include <block/context.h>
#include <block/cosmetics/cosmetics.h>
#include <block/player.h>
#include <gtest/gtest.h>

#include <memory>

class BlockSnap : public ::testing::Test // NOLINT(readability-identifier-naming)
{
public:
	IGameServer *m_pGameServer = nullptr;
	CServer *m_pServer = nullptr;
	std::unique_ptr<IKernel> m_pKernel;
	CTestInfo m_TestInfo;
	std::unique_ptr<IStorage> m_pStorage;

	CGameContext *GameServer() const { return (CGameContext *)m_pGameServer; }

	BlockSnap()
	{
		m_pServer = CreateServer();
		m_pKernel = std::unique_ptr<IKernel>(IKernel::Create());
		m_pKernel->RegisterInterface(m_pServer);

		IEngine *pEngine = CreateTestEngine(GAME_NAME);
		m_pKernel->RegisterInterface(pEngine);

		m_TestInfo.m_DeleteTestStorageFilesOnSuccess = true;
		m_pStorage = m_TestInfo.CreateTestStorage();
		m_pKernel->RegisterInterface(m_pStorage.get(), false);

		IConsole *pConsole = CreateConsole(CFGFLAG_SERVER | CFGFLAG_ECON).release();
		m_pKernel->RegisterInterface(pConsole);

		IConfigManager *pConfigManager = CreateConfigManager();
		m_pKernel->RegisterInterface(pConfigManager);

		IEngineHttp *pEngineHttp = CreateEngineHttp();
		m_pKernel->RegisterInterface(pEngineHttp);
		m_pKernel->RegisterInterface(static_cast<IHttp *>(pEngineHttp), false);

		IEngineAntibot *pEngineAntibot = CreateEngineAntibot();
		m_pKernel->RegisterInterface(pEngineAntibot);
		m_pKernel->RegisterInterface(static_cast<IAntibot *>(pEngineAntibot), false);

		m_pGameServer = CreateGameServer();
		m_pKernel->RegisterInterface(m_pGameServer);

		pEngine->Init();
		pConsole->Init();
		pConfigManager->Init();
		m_pServer->RegisterCommands();

		EXPECT_NE(m_pServer->LoadMap("coverage"), 0);
		m_pServer->m_RunServer = CServer::RUNNING;
		m_pServer->m_AuthManager.Init();

		const int Size = GameServer()->PersistentClientDataSize();
		for(auto &Client : m_pServer->m_aClients)
		{
			Client.m_HasPersistentData = false;
			Client.m_pPersistentData = malloc(Size);
		}
		m_pServer->m_pPersistentData = malloc(GameServer()->PersistentDataSize());

		m_pServer->Antibot()->Init();
		GameServer()->OnInit(nullptr);
	}

	~BlockSnap() override
	{
		m_pGameServer->OnShutdown(nullptr);
		m_pServer->DbPool()->OnShutdown();
	}

	/// Puts a client ingame with a player, without going through the network.
	CPlayer *AddPlayer(int ClientId, const char *pName, const char *pSkin = "default") const
	{
		CServer::CClient &Client = m_pServer->m_aClients[ClientId];
		Client.m_State = CServer::CClient::STATE_INGAME;
		Client.m_Sixup = false;
		// modern enough that Translate() is a no-op, so snapped ids match client ids
		Client.m_DDNetVersion = DDNET_VERSION_NUMBER;
		Client.m_GotDDNetVersionPacket = true;
		Client.m_DDNetVersionSettled = true;
		str_copy(Client.m_aName, pName, sizeof(Client.m_aName));
		Client.m_aClan[0] = '\0';
		Client.m_Country = -1;

		delete GameServer()->m_apPlayers[ClientId];
		GameServer()->m_apPlayers[ClientId] = new(ClientId) CPlayer(GameServer(), 1 + ClientId, ClientId, TEAM_RED);
		CPlayer *pPlayer = GameServer()->m_apPlayers[ClientId];
		pPlayer->Block().Init(pPlayer);
		pPlayer->SetTeeInfos(CTeeInfo(pSkin, false, 0, 0));
		return pPlayer;
	}

	/// Snaps `pPlayer` as `SnappingClient` sees it and returns the client info.
	CNetObj_ClientInfo SnapClientInfoFor(CPlayer *pPlayer, int SnappingClient) const
	{
		m_pServer->m_SnapshotBuilder.Init();
		pPlayer->Snap(SnappingClient);
		const int Key = (NETOBJTYPE_CLIENTINFO << 16) | pPlayer->GetCid();
		std::optional<int> Index = m_pServer->m_SnapshotBuilder.FindItemIndexByKey(Key);
		EXPECT_TRUE(Index.has_value()) << "no CNetObj_ClientInfo was snapped for cid " << pPlayer->GetCid();
		CNetObj_ClientInfo Info = {};
		if(Index.has_value())
			mem_copy(&Info, m_pServer->m_SnapshotBuilder.GetItemData(Index.value()), sizeof(Info));
		// every Init needs its Finish, or the next snap trips the builder assert
		CSnapshotBuffer Scratch;
		m_pServer->m_SnapshotBuilder.Finish(&Scratch);
		return Info;
	}
};

TEST_F(BlockSnap, PlainPlayerKeepsItsOwnSkin)
{
	CPlayer *pPlayer = AddPlayer(0, "one", "cammo");
	AddPlayer(1, "two");

	CNetObj_ClientInfo Info = SnapClientInfoFor(pPlayer, 1);
	char aSkin[64];
	IntsToStr(Info.m_aSkin, std::size(Info.m_aSkin), aSkin, sizeof(aSkin));
	EXPECT_STREQ(aSkin, "cammo");
}

TEST_F(BlockSnap, SkinmaniChangesWhatOtherPlayersSee)
{
	CPlayer *pPlayer = AddPlayer(0, "one", "cammo");
	AddPlayer(1, "two");

	CNetObj_ClientInfo Before = SnapClientInfoFor(pPlayer, 1);

	// pick the first skinmani effect; the handler decides what it looks like
	pPlayer->Block().SetSkinMani(0);
	CNetObj_ClientInfo After = SnapClientInfoFor(pPlayer, 1);

	EXPECT_NE(mem_comp(&Before, &After, sizeof(Before)), 0)
		<< "a skinmani did not change the client info that other players receive";
}

TEST_F(BlockSnap, ViewersWhoHideCosmeticsSeeThePlainSkin)
{
	CPlayer *pPlayer = AddPlayer(0, "one", "cammo");
	CPlayer *pViewer = AddPlayer(1, "two");
	pPlayer->Block().SetSkinMani(0);

	pViewer->Block().m_HideCosmetics = true;
	CNetObj_ClientInfo Hidden = SnapClientInfoFor(pPlayer, 1);

	pViewer->Block().m_HideCosmetics = false;
	CNetObj_ClientInfo Shown = SnapClientInfoFor(pPlayer, 1);

	EXPECT_NE(mem_comp(&Hidden, &Shown, sizeof(Hidden)), 0)
		<< "hiding cosmetics made no difference to what is snapped";
}

TEST_F(BlockSnap, ClanTagIsEmptyWithoutAnAccount)
{
	CPlayer *pPlayer = AddPlayer(0, "one");
	AddPlayer(1, "two");

	CNetObj_ClientInfo Info = SnapClientInfoFor(pPlayer, 1);
	char aClan[64];
	IntsToStr(Info.m_aClan, std::size(Info.m_aClan), aClan, sizeof(aClan));
	EXPECT_STREQ(aClan, "") << "a player without a Block clan must not carry a clan tag";
}

// --- gundesign: the cosmetic is snapped instead of the bullet ---

/// Snaps a WEAPON_GUN projectile owned by `Owner` as `SnappingClient` sees it.
/// Returns true when a gundesign took the bullet's place.
static bool SnapGunProjectile(CGameContext *pGameServer, CServer *pServer, int Owner, int SnappingClient)
{
	pServer->m_SnapshotBuilder.Init();
	const bool Handled = pGameServer->Block().OnSnapProjectile(WEAPON_GUN, Owner, vec2(100, 100), vec2(1, 0), 0, SnappingClient);
	CSnapshotBuffer Scratch;
	pServer->m_SnapshotBuilder.Finish(&Scratch);
	return Handled;
}

TEST_F(BlockSnap, PlainBulletWithoutAGundesign)
{
	AddPlayer(0, "one");
	AddPlayer(1, "two");
	EXPECT_FALSE(SnapGunProjectile(GameServer(), m_pServer, 0, 1));
}

TEST_F(BlockSnap, GundesignReplacesTheBullet)
{
	CPlayer *pPlayer = AddPlayer(0, "one");
	AddPlayer(1, "two");
	pPlayer->Block().SetGunDesign(CCosmeticsHandler::GUNDESIGN_HEART);
	EXPECT_TRUE(SnapGunProjectile(GameServer(), m_pServer, 0, 1))
		<< "the gundesign was set but the plain bullet was snapped anyway";
}

TEST_F(BlockSnap, ViewersWhoHideCosmeticsSeeThePlainBullet)
{
	CPlayer *pPlayer = AddPlayer(0, "one");
	CPlayer *pViewer = AddPlayer(1, "two");
	pPlayer->Block().SetGunDesign(CCosmeticsHandler::GUNDESIGN_HEART);
	pViewer->Block().m_HideCosmetics = true;

	EXPECT_FALSE(SnapGunProjectile(GameServer(), m_pServer, 0, 1));
	// but you always see your own
	EXPECT_TRUE(SnapGunProjectile(GameServer(), m_pServer, 0, 0));
}

TEST_F(BlockSnap, AGundesignedBulletOutlivingItsOwnerIsPlain)
{
	CPlayer *pPlayer = AddPlayer(0, "one");
	AddPlayer(1, "two");
	pPlayer->Block().SetGunDesign(CCosmeticsHandler::GUNDESIGN_HEART);

	delete GameServer()->m_apPlayers[0];
	GameServer()->m_apPlayers[0] = nullptr;
	EXPECT_FALSE(SnapGunProjectile(GameServer(), m_pServer, 0, 1));
}

// --- passive/protected players are transparent to bullets and lasers ---

class BlockHit : public BlockSnap // NOLINT(readability-identifier-naming)
{
public:
	CNetObj_PlayerInput m_Input = {};

	CCharacter *AddCharacter(int ClientId, vec2 Pos)
	{
		CCharacter *pChr = new(ClientId) CCharacter(&GameServer()->m_World, m_Input);
		pChr->m_Pos = Pos;
		pChr->Core()->m_Passive = false;
		pChr->Core()->m_Protected = false;
		GameServer()->m_World.InsertEntity(pChr);
		return pChr;
	}
};

TEST_F(BlockHit, LaserStopsOnTheFirstPlayerInLine)
{
	CCharacter *pOwner = AddCharacter(0, vec2(0, 0));
	CCharacter *pNear = AddCharacter(1, vec2(100, 0));
	AddCharacter(2, vec2(200, 0));

	vec2 At;
	EXPECT_EQ(GameServer()->Block().IntersectLaserTarget(vec2(0, 0), vec2(400, 0), At, pOwner, -1, true), pNear);
}

TEST_F(BlockHit, LaserPassesThroughAPassivePlayer)
{
	CCharacter *pOwner = AddCharacter(0, vec2(0, 0));
	CCharacter *pNear = AddCharacter(1, vec2(100, 0));
	CCharacter *pFar = AddCharacter(2, vec2(200, 0));
	pNear->Core()->m_Passive = true;

	vec2 At;
	EXPECT_EQ(GameServer()->Block().IntersectLaserTarget(vec2(0, 0), vec2(400, 0), At, pOwner, -1, true), pFar)
		<< "the laser stopped on a passive player instead of going through";

	// protected behaves the same way
	pNear->Core()->m_Passive = false;
	pNear->Core()->m_Protected = true;
	EXPECT_EQ(GameServer()->Block().IntersectLaserTarget(vec2(0, 0), vec2(400, 0), At, pOwner, -1, true), pFar);
}

TEST_F(BlockHit, APassiveShooterHitsNobodyElse)
{
	CCharacter *pOwner = AddCharacter(0, vec2(0, 0));
	AddCharacter(1, vec2(100, 0));
	pOwner->Core()->m_Passive = true;

	vec2 At;
	EXPECT_EQ(GameServer()->Block().IntersectLaserTarget(vec2(0, 0), vec2(400, 0), At, pOwner, -1, true), nullptr);
	// with the self-hit allowed the passive shooter can still unfreeze itself
	EXPECT_EQ(GameServer()->Block().IntersectLaserTarget(vec2(0, 0), vec2(400, 0), At, pOwner, -1, false), pOwner);
}

TEST_F(BlockHit, BulletsSkipPassiveAndProtected)
{
	CCharacter *pOwner = AddCharacter(0, vec2(0, 0));
	CCharacter *pTarget = AddCharacter(1, vec2(100, 0));

	EXPECT_EQ(GameServer()->Block().FilterHitTarget(pOwner, pTarget), pTarget);

	pTarget->Core()->m_Passive = true;
	EXPECT_EQ(GameServer()->Block().FilterHitTarget(pOwner, pTarget), nullptr);

	pTarget->Core()->m_Passive = false;
	pOwner->Core()->m_Protected = true;
	EXPECT_EQ(GameServer()->Block().FilterHitTarget(pOwner, pTarget), nullptr)
		<< "a protected shooter must not damage anyone";
}

TEST_F(BlockHit, PassivePlayersCanStillFire)
{
	CCharacter *pShooter = AddCharacter(0, vec2(0, 0));

	EXPECT_FALSE(pShooter->Block().BlocksFire(false)) << "a normal player could not fire";

	pShooter->Core()->m_Passive = true;
	EXPECT_FALSE(pShooter->Block().BlocksFire(false))
		<< "a passive player was stopped from firing; passive governs hits, not weapons";

	pShooter->Core()->m_Passive = false;
	pShooter->Core()->m_Protected = true;
	EXPECT_FALSE(pShooter->Block().BlocksFire(false))
		<< "a protected player was stopped from firing";
}

TEST_F(BlockHit, PassiveHammerHitsNobodyInEitherDirection)
{
	CCharacter *pShooter = AddCharacter(0, vec2(0, 0));
	CCharacter *pTarget = AddCharacter(1, vec2(10, 0));

	// a passive shooter's hammer must not land
	pShooter->Core()->m_Passive = true;
	EXPECT_FALSE(pShooter->Block().OnHammerHit(pTarget)) << "a passive player's hammer still hit";

	// and a passive target must not be hammered
	pShooter->Core()->m_Passive = false;
	pTarget->Core()->m_Passive = true;
	EXPECT_FALSE(pShooter->Block().OnHammerHit(pTarget)) << "a passive player was still hammered";
}

// --- scoreboard: the account level, never a race time ---
//
// Block has no race, but CGameControllerDDNet answers SnapPlayerTime with
// a finish time (NotFinished is still a *set* value) and a client that gets one
// renders the time column and ignores the score. That is why the scoreboard
// showed a race timer on newer clients while older ones, which have no such
// field, correctly showed the level.

/// Reads the CNetObj_PlayerInfo the given viewer receives for pPlayer.
static CNetObj_PlayerInfo SnapPlayerInfoFor(CServer *pServer, CPlayer *pPlayer, int SnappingClient)
{
	pServer->m_SnapshotBuilder.Init();
	pPlayer->Snap(SnappingClient);
	const int Key = (NETOBJTYPE_PLAYERINFO << 16) | pPlayer->GetCid();
	std::optional<int> Index = pServer->m_SnapshotBuilder.FindItemIndexByKey(Key);
	EXPECT_TRUE(Index.has_value()) << "no CNetObj_PlayerInfo was snapped";
	CNetObj_PlayerInfo Info = {};
	if(Index.has_value())
		mem_copy(&Info, pServer->m_SnapshotBuilder.GetItemData(Index.value()), sizeof(Info));
	CSnapshotBuffer Scratch;
	pServer->m_SnapshotBuilder.Finish(&Scratch);
	return Info;
}

TEST_F(BlockSnap, ScoreboardSendsNoRaceTimeSoTheScoreIsUsed)
{
	CPlayer *pPlayer = AddPlayer(0, "one");
	AddPlayer(1, "two");
	IGameController *pController = GameServer()->m_pController;

	EXPECT_EQ(pController->SnapPlayerTime(1, pPlayer).m_Seconds, (int)FinishTime::UNSET)
		<< "a finish time was sent, so the client shows the race timer instead of the level";
	EXPECT_EQ(pController->SnapMapBestTime(1).m_Seconds, (int)FinishTime::UNSET)
		<< "a map best time was sent, so the client shows a record row";
}

TEST_F(BlockSnap, ScoreIsZeroWhenNotLoggedInAndTheLevelWhenLoggedIn)
{
	CPlayer *pPlayer = AddPlayer(0, "one");
	CPlayer *pViewer = AddPlayer(1, "two");

	// a guest scores 0, not -9999 (which the client hides) and not a time
	EXPECT_FALSE(pPlayer->Block().IsLoggedIn());
	EXPECT_EQ(SnapPlayerInfoFor(m_pServer, pPlayer, 1).m_Score, 0);

	// logged in: the score is the account level, for every viewer
	pPlayer->Block().SetPlayerId(1234);
	pPlayer->Block().SetPlayerLevel(42);
	ASSERT_TRUE(pPlayer->Block().IsLoggedIn());
	EXPECT_EQ(SnapPlayerInfoFor(m_pServer, pPlayer, 1).m_Score, 42) << "another player did not see the level";
	EXPECT_EQ(SnapPlayerInfoFor(m_pServer, pPlayer, 0).m_Score, 42) << "the player did not see their own level";
	(void)pViewer;
}
