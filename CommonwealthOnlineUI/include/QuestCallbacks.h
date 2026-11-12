#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::Quests
{
	enum class Scope : std::uint8_t
	{
		Shared,
		Solo,
		Party,
		Held,
		Fireteam
	};

	enum class ActionKind : std::uint8_t
	{
		SetScope,
		Refresh,
		Close,
		FireteamInvite,
		FireteamKick,
		FireteamRespond,
		FireteamLeave
	};

	struct Action
	{
		ActionKind    kind{ ActionKind::Refresh };
		Scope         scope{ Scope::Shared };
		std::string   quest;
		std::uint32_t target{ 0 };
		std::string   inviteId;
		bool          accept{ false };
	};

	constexpr std::size_t kStateJsonMaxBytes = 256 * 1024;

	[[nodiscard]] bool IsValidQuestKey(std::string_view a_key);
	[[nodiscard]] const char* ScopeName(Scope a_scope);
	[[nodiscard]] std::optional<Scope> ParseScope(std::string_view a_scope);
	[[nodiscard]] std::optional<Action> ParseAction(std::string_view a_json);
	[[nodiscard]] std::string FormatActionJson(const Action& a_action);
	[[nodiscard]] std::string FormatStateScript(std::string_view a_stateJson);
	[[nodiscard]] std::string UnavailableStateJson();

	class IQuestHost
	{
	public:
		virtual ~IQuestHost() = default;

		virtual bool GetStateJson(
			char*       a_jsonOut,
			std::size_t a_jsonOutSize,
			char*       a_errorBuf,
			std::size_t a_errorBufSize) = 0;

		virtual void SendAction(const char* a_actionJson) = 0;
	};

	void SetHost(IQuestHost* a_host);
	[[nodiscard]] IQuestHost* GetHost();
	[[nodiscard]] std::string PullStateJson(std::string& a_error);
	void ForwardAction(const Action& a_action);
}
