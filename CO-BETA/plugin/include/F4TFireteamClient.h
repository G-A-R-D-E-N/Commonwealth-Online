#pragma once

#include "F4TQuestWire.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::FireteamClient
{
	inline constexpr std::uint64_t kInviteLifetimeMilliseconds = 60'000;
	inline constexpr std::size_t kMaximumInvites = 16;
	inline constexpr std::string_view kReasonUnsupported = "unsupported";
	inline constexpr std::string_view kReasonNotConnected = "not_connected";

	enum class EventKind : std::uint8_t
	{
		None,
		Refusal,
		Declined,
		Invited,
		Removed
	};

	struct Member
	{
		std::uint32_t playerId{};
		std::string name;
	};

	struct Team
	{
		std::string fireteamId;
		std::uint32_t leaderPlayerId{};
		std::vector<Member> members;
	};

	struct Invite
	{
		std::string inviteId;
		std::uint32_t fromPlayerId{};
		std::string fromName;
		std::uint64_t receivedMilliseconds{};
		bool responding{};
	};

	struct Event
	{
		EventKind kind{ EventKind::None };
		std::string reason;
		std::uint32_t playerId{};
	};

	struct Context
	{
		bool supported{};
		bool connected{};
		std::optional<std::uint32_t> self;
	};

	struct View
	{
		Team team;
		std::vector<Invite> invites;
		Event event;
	};

	void ApplySnapshot(const QuestWire::FireteamSnapshotIn& a_snapshot);
	void ApplyInvite(const QuestWire::FireteamInviteIn& a_invite, std::uint64_t a_nowMilliseconds);
	void ApplyDecline(const QuestWire::FireteamDeclineIn& a_decline);
	void ApplyEvent(const QuestWire::FireteamEventIn& a_event);
	void ClearSession();
	[[nodiscard]] View Current(std::uint64_t a_nowMilliseconds);
	[[nodiscard]] std::optional<QuestWire::Json> TryInvite(std::uint32_t a_target, const Context& a_context);
	[[nodiscard]] std::optional<QuestWire::Json> TryRespond(
		std::string_view a_inviteId,
		bool a_accept,
		const Context& a_context,
		std::uint64_t a_nowMilliseconds);
	void RespondSent(std::string_view a_inviteId);
	void RespondFailed(std::string_view a_inviteId);
	[[nodiscard]] std::optional<QuestWire::Json> TryLeave(const Context& a_context);
	[[nodiscard]] std::optional<QuestWire::Json> TryKick(std::uint32_t a_target, const Context& a_context);
	[[nodiscard]] std::uint64_t MonotonicMilliseconds();
}
