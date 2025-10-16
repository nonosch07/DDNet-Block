#include "chat_filter.h"

#include <algorithm>

#include <base/logger.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <game/server/gamecontext.h>

static const char *DEFAULT_CHATFILTER_FILENAME = "data/chatfilter_words.txt";

CChatFilterComponent::CChatFilterComponent(CGameContext *pGameServer) :
	CComponent(pGameServer)
{
	Load();
}

void CChatFilterComponent::OnConsoleInit()
{
	Console()->Register("chatfilter_add", "r[word]", CFGFLAG_SERVER, ConChatFilterAdd, this, "Add a word to the chat filter and persist");
	Console()->Register("chatfilter_remove", "r[word]", CFGFLAG_SERVER, ConChatFilterRemove, this, "Remove a word from the chat filter and persist");
	Console()->Register("chatfilter_list", "", CFGFLAG_SERVER, ConChatFilterList, this, "List filtered words");
	Console()->Register("chatfilter_reload", "", CFGFLAG_SERVER, ConChatFilterReload, this, "Reload chat filter words from disk");
	Console()->Register("chatfilter_save", "", CFGFLAG_SERVER, ConChatFilterSave, this, "Force-save chat filter words to disk");
}

bool CChatFilterComponent::Load()
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	m_Words.clear();
	CLineReader LineReader;
	char aPath[IO_MAX_PATH_LENGTH] = {0};
	const char *pConfigured = g_Config.m_SvChatfilterWordsFile[0] ? g_Config.m_SvChatfilterWordsFile : DEFAULT_CHATFILTER_FILENAME;
	GameServer()->Storage()->GetBinaryPath(pConfigured, aPath, sizeof(aPath));
	IOHANDLE h = io_open(aPath, IOFLAG_READ);
	if(!h)
	{
		h = GameServer()->Storage()->OpenFile(pConfigured, IOFLAG_READ, IStorage::TYPE_ALL);
	}
	if(!LineReader.OpenFile(h))
		return false;
	while(const char *pLine = LineReader.Get())
	{
		if(!pLine[0])
			continue;
		std::string s = pLine;
		str_clean_whitespaces(&s[0]);
		if(!s.empty())
			m_Words.insert(s);
	}
	return true;
}

bool CChatFilterComponent::Save()
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	char aPath[IO_MAX_PATH_LENGTH] = {0};
	const char *pConfigured = g_Config.m_SvChatfilterWordsFile[0] ? g_Config.m_SvChatfilterWordsFile : DEFAULT_CHATFILTER_FILENAME;
	GameServer()->Storage()->GetBinaryPath(pConfigured, aPath, sizeof(aPath));
	IOHANDLE h = io_open(aPath, IOFLAG_WRITE);
	if(!h)
	{
		h = GameServer()->Storage()->OpenFile(pConfigured, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!h)
			return false;
	}
	for(const auto &w : m_Words)
	{
		io_write(h, w.c_str(), w.size());
		io_write_newline(h);
	}
	io_close(h);
	return true;
}

bool CChatFilterComponent::CheckAndMaybeMute(int ClientId, const char *pMessage)
{
	if(!pMessage || !*pMessage)
		return false;

	// skip commands
	if(pMessage[0] == '/')
		return false;

	// quick check: case-insensitive substring find for each word
	std::lock_guard<std::mutex> lock(m_Mutex);
	for(const auto &w : m_Words)
	{
		if(!w.empty() && str_utf8_find_nocase(pMessage, w.c_str()))
		{
			// found bad word
			int Hours = clamp(g_Config.m_SvChatfilterMuteHours, 0, 24 * 30);
			if(Hours > 0)
			{
				const int Secs = Hours * 3600;
				NETADDR Addr;
				GameServer()->Server()->GetClientAddr(ClientId, &Addr);

				int Remaining = GameServer()->GetRemainingMuteSecondsPublic(ClientId);

				GameServer()->AddIpMuteSilent(&Addr, Secs, "Chat filter violation");

				if(Remaining > 0)
				{
					GameServer()->SendChatTarget(ClientId, "Your message was blocked by the chat filter. You are already muted.");
				}
				else
				{
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "You have been muted for %d hour%s due to chat filter violation.", Hours, Hours == 1 ? "" : "s");
					GameServer()->SendChatTarget(ClientId, aBuf);
				}
			}
			else
			{
				GameServer()->SendChatTarget(ClientId, "Your message was blocked by the chat filter.");
			}
			return true;
		}
	}
	return false;
}

void CChatFilterComponent::ConChatFilterAdd(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = (CChatFilterComponent *)pUserData;
	std::string Word = pResult->GetString(0);
	if(Word.empty())
		return;
	std::vector<char> Buf(Word.begin(), Word.end());
	Buf.push_back('\0');
	str_clean_whitespaces(Buf.data());
	Word = Buf.data();
	if(Word.empty())
		return;
	{
		std::lock_guard<std::mutex> lock(pSelf->m_Mutex);
		pSelf->m_Words.insert(Word);
	}
	pSelf->Save();
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pSelf->GetName(), "Added word");
}

void CChatFilterComponent::ConChatFilterRemove(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = (CChatFilterComponent *)pUserData;
	std::string Word = pResult->GetString(0);
	if(Word.empty())
		return;
	std::vector<char> Buf(Word.begin(), Word.end());
	Buf.push_back('\0');
	str_clean_whitespaces(Buf.data());
	Word = Buf.data();
	if(Word.empty())
		return;
	{
		std::lock_guard<std::mutex> lock(pSelf->m_Mutex);
		pSelf->m_Words.erase(Word);
	}
	pSelf->Save();
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pSelf->GetName(), "Removed word");
}

void CChatFilterComponent::ConChatFilterList(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = (CChatFilterComponent *)pUserData;
	std::vector<std::string> v;
	{
		std::lock_guard<std::mutex> lock(pSelf->m_Mutex);
		v.reserve(pSelf->m_Words.size());
		for(const auto &w : pSelf->m_Words)
			v.push_back(w);
	}
	std::sort(v.begin(), v.end());
	for(const auto &w : v)
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pSelf->GetName(), w.c_str());
}

void CChatFilterComponent::ConChatFilterReload(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = (CChatFilterComponent *)pUserData;
	if(pSelf->Load())
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pSelf->GetName(), "Reloaded from disk");
	else
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pSelf->GetName(), "No file found; seeded from legacy list if available");
}

void CChatFilterComponent::ConChatFilterSave(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = (CChatFilterComponent *)pUserData;
	if(pSelf->Save())
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pSelf->GetName(), "Saved to disk");
	else
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pSelf->GetName(), "Save failed");
}
