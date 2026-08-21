// Snapshot-level tests for Blockworlds cosmetics.
//
// These check what the server actually puts on the wire for a given viewer,
// which is the only way to tell "the cosmetic is applied" from "the cosmetic is
// stored but never sent". The fixture mirrors src/test/gameworld_test.cpp.

#include <gtest/gtest.h>

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

#include <blockworlds/bw_context.h>
#include <blockworlds/bw_player.h>
#include <blockworlds/cosmetics/cosmetics.h>

#include "../../test/test.h"

#include <memory>

class BwSnap : public ::testing::Test // NOLINT(readability-identifier-naming)
{
public:
	IGameServer *m_pGameServer = nullptr;
	CServer *m_pServer = nullptr;
	std::unique_ptr<IKernel> m_pKernel;
	CTestInfo m_TestInfo;
	std::unique_ptr<IStorage> m_pStorage;

	CGameContext *GameServer() { return (CGameContext *)m_pGameServer; }

	BwSnap()
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

	~BwSnap() override
	{
		m_pGameServer->OnShutdown(nullptr);
		m_pServer->DbPool()->OnShutdown();
	}

	/// Puts a client ingame with a player, without going through the network.
	CPlayer *AddPlayer(int ClientId, const char *pName, const char *pSkin = "default")
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
		pPlayer->Bw().Init(pPlayer);
		pPlayer->SetTeeInfos(CTeeInfo(pSkin, false, 0, 0));
		return pPlayer;
	}

	/// Snaps `pPlayer` as `SnappingClient` sees it and returns the client info.
	CNetObj_ClientInfo SnapClientInfoFor(CPlayer *pPlayer, int SnappingClient)
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

TEST_F(BwSnap, PlainPlayerKeepsItsOwnSkin)
{
	CPlayer *pPlayer = AddPlayer(0, "one", "cammo");
	AddPlayer(1, "two");

	CNetObj_ClientInfo Info = SnapClientInfoFor(pPlayer, 1);
	char aSkin[64];
	IntsToStr(Info.m_aSkin, std::size(Info.m_aSkin), aSkin, sizeof(aSkin));
	EXPECT_STREQ(aSkin, "cammo");
}

TEST_F(BwSnap, SkinmaniChangesWhatOtherPlayersSee)
{
	CPlayer *pPlayer = AddPlayer(0, "one", "cammo");
	AddPlayer(1, "two");

	CNetObj_ClientInfo Before = SnapClientInfoFor(pPlayer, 1);

	// pick the first skinmani effect; the handler decides what it looks like
	pPlayer->Bw().SetSkinMani(0);
	CNetObj_ClientInfo After = SnapClientInfoFor(pPlayer, 1);

	EXPECT_NE(mem_comp(&Before, &After, sizeof(Before)), 0)
		<< "a skinmani did not change the client info that other players receive";
}

TEST_F(BwSnap, ViewersWhoHideCosmeticsSeeThePlainSkin)
{
	CPlayer *pPlayer = AddPlayer(0, "one", "cammo");
	CPlayer *pViewer = AddPlayer(1, "two");
	pPlayer->Bw().SetSkinMani(0);

	pViewer->Bw().m_HideCosmetics = true;
	CNetObj_ClientInfo Hidden = SnapClientInfoFor(pPlayer, 1);

	pViewer->Bw().m_HideCosmetics = false;
	CNetObj_ClientInfo Shown = SnapClientInfoFor(pPlayer, 1);

	EXPECT_NE(mem_comp(&Hidden, &Shown, sizeof(Hidden)), 0)
		<< "hiding cosmetics made no difference to what is snapped";
}

TEST_F(BwSnap, ClanTagIsEmptyWithoutAnAccount)
{
	CPlayer *pPlayer = AddPlayer(0, "one");
	AddPlayer(1, "two");

	CNetObj_ClientInfo Info = SnapClientInfoFor(pPlayer, 1);
	char aClan[64];
	IntsToStr(Info.m_aClan, std::size(Info.m_aClan), aClan, sizeof(aClan));
	EXPECT_STREQ(aClan, "") << "a player without a Blockworlds clan must not carry a clan tag";
}

// --- gundesign: the cosmetic is snapped instead of the bullet ---

/// Snaps a WEAPON_GUN projectile owned by `Owner` as `SnappingClient` sees it.
/// Returns true when a gundesign took the bullet's place.
static bool SnapGunProjectile(CGameContext *pGameServer, CServer *pServer, int Owner, int SnappingClient)
{
	pServer->m_SnapshotBuilder.Init();
	const bool Handled = pGameServer->Bw().OnSnapProjectile(WEAPON_GUN, Owner, vec2(100, 100), vec2(1, 0), 0, SnappingClient);
	CSnapshotBuffer Scratch;
	pServer->m_SnapshotBuilder.Finish(&Scratch);
	return Handled;
}

TEST_F(BwSnap, PlainBulletWithoutAGundesign)
{
	AddPlayer(0, "one");
	AddPlayer(1, "two");
	EXPECT_FALSE(SnapGunProjectile(GameServer(), m_pServer, 0, 1));
}

TEST_F(BwSnap, GundesignReplacesTheBullet)
{
	CPlayer *pPlayer = AddPlayer(0, "one");
	AddPlayer(1, "two");
	pPlayer->Bw().SetGunDesign(CCosmeticsHandler::GUNDESIGN_HEART);
	EXPECT_TRUE(SnapGunProjectile(GameServer(), m_pServer, 0, 1))
		<< "the gundesign was set but the plain bullet was snapped anyway";
}

TEST_F(BwSnap, ViewersWhoHideCosmeticsSeeThePlainBullet)
{
	CPlayer *pPlayer = AddPlayer(0, "one");
	CPlayer *pViewer = AddPlayer(1, "two");
	pPlayer->Bw().SetGunDesign(CCosmeticsHandler::GUNDESIGN_HEART);
	pViewer->Bw().m_HideCosmetics = true;

	EXPECT_FALSE(SnapGunProjectile(GameServer(), m_pServer, 0, 1));
	// but you always see your own
	EXPECT_TRUE(SnapGunProjectile(GameServer(), m_pServer, 0, 0));
}

TEST_F(BwSnap, AGundesignedBulletOutlivingItsOwnerIsPlain)
{
	CPlayer *pPlayer = AddPlayer(0, "one");
	AddPlayer(1, "two");
	pPlayer->Bw().SetGunDesign(CCosmeticsHandler::GUNDESIGN_HEART);

	delete GameServer()->m_apPlayers[0];
	GameServer()->m_apPlayers[0] = nullptr;
	EXPECT_FALSE(SnapGunProjectile(GameServer(), m_pServer, 0, 1));
}

// --- passive/protected players are transparent to bullets and lasers ---

class BwHit : public BwSnap // NOLINT(readability-identifier-naming)
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

TEST_F(BwHit, LaserStopsOnTheFirstPlayerInLine)
{
	CCharacter *pOwner = AddCharacter(0, vec2(0, 0));
	CCharacter *pNear = AddCharacter(1, vec2(100, 0));
	AddCharacter(2, vec2(200, 0));

	vec2 At;
	EXPECT_EQ(GameServer()->Bw().IntersectLaserTarget(vec2(0, 0), vec2(400, 0), At, pOwner, -1, true), pNear);
}

TEST_F(BwHit, LaserPassesThroughAPassivePlayer)
{
	CCharacter *pOwner = AddCharacter(0, vec2(0, 0));
	CCharacter *pNear = AddCharacter(1, vec2(100, 0));
	CCharacter *pFar = AddCharacter(2, vec2(200, 0));
	pNear->Core()->m_Passive = true;

	vec2 At;
	EXPECT_EQ(GameServer()->Bw().IntersectLaserTarget(vec2(0, 0), vec2(400, 0), At, pOwner, -1, true), pFar)
		<< "the laser stopped on a passive player instead of going through";

	// protected behaves the same way
	pNear->Core()->m_Passive = false;
	pNear->Core()->m_Protected = true;
	EXPECT_EQ(GameServer()->Bw().IntersectLaserTarget(vec2(0, 0), vec2(400, 0), At, pOwner, -1, true), pFar);
}

TEST_F(BwHit, APassiveShooterHitsNobodyElse)
{
	CCharacter *pOwner = AddCharacter(0, vec2(0, 0));
	AddCharacter(1, vec2(100, 0));
	pOwner->Core()->m_Passive = true;

	vec2 At;
	EXPECT_EQ(GameServer()->Bw().IntersectLaserTarget(vec2(0, 0), vec2(400, 0), At, pOwner, -1, true), nullptr);
	// with the self-hit allowed the passive shooter can still unfreeze itself
	EXPECT_EQ(GameServer()->Bw().IntersectLaserTarget(vec2(0, 0), vec2(400, 0), At, pOwner, -1, false), pOwner);
}

TEST_F(BwHit, BulletsSkipPassiveAndProtected)
{
	CCharacter *pOwner = AddCharacter(0, vec2(0, 0));
	CCharacter *pTarget = AddCharacter(1, vec2(100, 0));

	EXPECT_EQ(GameServer()->Bw().FilterHitTarget(pOwner, pTarget), pTarget);

	pTarget->Core()->m_Passive = true;
	EXPECT_EQ(GameServer()->Bw().FilterHitTarget(pOwner, pTarget), nullptr);

	pTarget->Core()->m_Passive = false;
	pOwner->Core()->m_Protected = true;
	EXPECT_EQ(GameServer()->Bw().FilterHitTarget(pOwner, pTarget), nullptr)
		<< "a protected shooter must not damage anyone";
}
