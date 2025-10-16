#ifndef BLOCKWORLDS_COMPONENTS_CHATFILTER_CHAT_FILTER_H
#define BLOCKWORLDS_COMPONENTS_CHATFILTER_CHAT_FILTER_H

#include <blockworlds/components/core/component.h>
#include <engine/console.h>

#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

// chat filter component: loads a local word list and mutes players using banned words
class CChatFilterComponent final : public CComponent
{
public:
	static const char *GetNameStatic() { return "chatfilter"; }

public:
	explicit CChatFilterComponent(class CGameContext *pGameServer);

public: // CComponent
	const char *GetName() const override { return GetNameStatic(); }
	void OnConsoleInit() override;

public:
	bool CheckAndMaybeMute(int ClientId, const char *pMessage);

private:
	bool Load();
	bool Save();

	static void ConChatFilterAdd(IConsole::IResult *pResult, void *pUserData);
	static void ConChatFilterRemove(IConsole::IResult *pResult, void *pUserData);
	static void ConChatFilterList(IConsole::IResult *pResult, void *pUserData);
	static void ConChatFilterReload(IConsole::IResult *pResult, void *pUserData);
	static void ConChatFilterSave(IConsole::IResult *pResult, void *pUserData);

private:
	std::unordered_set<std::string> m_Words; // stored as-is, case-insensitive search is used at runtime
	std::mutex m_Mutex;
};

#endif // BLOCKWORLDS_COMPONENTS_CHATFILTER_CHAT_FILTER_H
