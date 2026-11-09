#pragma once

#include "F4TQuestProtocol.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::Fireteam
{
	inline constexpr double kInviteTtlSeconds = 60.0;

	enum class JoinRefusal : std::uint8_t
	{
		None,
		Full,
		AlreadyMember,
		SelfTarget,
		UnknownTarget,
		NoInvite,
		NotLeader,
		NotMember
	};

	enum class LeaveOutcome : std::uint8_t
	{
		NotMember,
		LeftTeamDisbanded,
		LeftTeamRemains
	};

	struct MemberView
	{
		std::uint32_t playerId{};
	};

	class FireteamTable
	{
	public:
		explicit FireteamTable(double a_inviteTtlSeconds = kInviteTtlSeconds)
			: _inviteTtlSeconds(a_inviteTtlSeconds)
		{}

		std::optional<std::string> Invite(std::uint32_t a_from, std::uint32_t a_target, double a_nowSeconds, JoinRefusal& a_refusal)
		{
			a_refusal = JoinRefusal::None;
			ExpireInvites(a_nowSeconds);
			if (a_from == a_target) {
				a_refusal = JoinRefusal::SelfTarget;
				return std::nullopt;
			}
			if (a_from == 0 || a_target == 0) {
				a_refusal = JoinRefusal::UnknownTarget;
				return std::nullopt;
			}
			if (IsMember(a_target)) {
				a_refusal = JoinRefusal::AlreadyMember;
				return std::nullopt;
			}
			if (const Team* team = TeamOf(a_from); team != nullptr && team->members.size() >= QuestProtocol::kMaximumFireteamMembers) {
				a_refusal = JoinRefusal::Full;
				return std::nullopt;
			}
			for (auto existing = _invites.begin(); existing != _invites.end(); ++existing) {
				if (existing->second.fromPlayerId == a_from && existing->second.targetPlayerId == a_target) {
					existing->second.expiresAtSeconds = a_nowSeconds + _inviteTtlSeconds;
					return existing->first;
				}
			}
			const std::string inviteId = "i-" + std::to_string(++_inviteCounter);
			_invites.emplace(inviteId, InviteRecord{ a_from, a_target, a_nowSeconds + _inviteTtlSeconds });
			return inviteId;
		}

		std::optional<std::string> Accept(std::string_view a_inviteId, std::uint32_t a_target, double a_nowSeconds, JoinRefusal& a_refusal)
		{
			a_refusal = JoinRefusal::None;
			const auto found = _invites.find(std::string{ a_inviteId });
			if (found == _invites.end()) {
				a_refusal = JoinRefusal::NoInvite;
				return std::nullopt;
			}
			if (a_nowSeconds >= found->second.expiresAtSeconds) {
				_invites.erase(found);
				a_refusal = JoinRefusal::NoInvite;
				return std::nullopt;
			}
			if (a_target == 0 || found->second.targetPlayerId != a_target) {
				a_refusal = JoinRefusal::NoInvite;
				return std::nullopt;
			}
			if (IsMember(a_target)) {
				a_refusal = JoinRefusal::AlreadyMember;
				return std::nullopt;
			}
			const std::uint32_t from = found->second.fromPlayerId;
			std::string teamId;
			const auto owner = _teamIdOf.find(from);
			if (owner != _teamIdOf.end()) {
				teamId = owner->second;
			} else {
				const auto created = Create(from);
				if (!created.has_value()) {
					a_refusal = JoinRefusal::UnknownTarget;
					return std::nullopt;
				}
				teamId = *created;
			}
			const auto team = _teams.find(teamId);
			if (team == _teams.end()) {
				a_refusal = JoinRefusal::UnknownTarget;
				return std::nullopt;
			}
			if (team->second.members.size() >= QuestProtocol::kMaximumFireteamMembers) {
				a_refusal = JoinRefusal::Full;
				return std::nullopt;
			}
			team->second.members.emplace(a_target, MemberView{ a_target });
			_teamIdOf.emplace(a_target, teamId);
			_invites.erase(found);
			return teamId;
		}

		bool Decline(std::string_view a_inviteId, std::uint32_t a_target)
		{
			const auto found = _invites.find(std::string{ a_inviteId });
			if (found == _invites.end() || found->second.targetPlayerId != a_target) {
				return false;
			}
			_invites.erase(found);
			return true;
		}

		std::optional<std::string> Create(std::uint32_t a_leader)
		{
			if (a_leader == 0 || IsMember(a_leader)) {
				return std::nullopt;
			}
			const std::string teamId = std::string{ QuestProtocol::kFireteamTrackPrefix } + std::to_string(++_teamCounter);
			Team team;
			team.leaderId = a_leader;
			team.members.emplace(a_leader, MemberView{ a_leader });
			_teams.emplace(teamId, std::move(team));
			_teamIdOf.emplace(a_leader, teamId);
			return teamId;
		}

		LeaveOutcome Leave(std::uint32_t a_player, std::string& a_teamId)
		{
			a_teamId.clear();
			const auto index = _teamIdOf.find(a_player);
			if (index == _teamIdOf.end()) {
				return LeaveOutcome::NotMember;
			}
			a_teamId = index->second;
			const auto team = _teams.find(index->second);
			_teamIdOf.erase(index);
			if (team == _teams.end()) {
				return LeaveOutcome::NotMember;
			}
			team->second.members.erase(a_player);
			if (team->second.members.empty()) {
				_teams.erase(team);
				return LeaveOutcome::LeftTeamDisbanded;
			}
			if (team->second.leaderId == a_player) {
				team->second.leaderId = team->second.members.begin()->first;
			}
			return LeaveOutcome::LeftTeamRemains;
		}

		JoinRefusal Kick(std::uint32_t a_leader, std::uint32_t a_target, std::string& a_teamId)
		{
			a_teamId.clear();
			if (a_target == 0) {
				return JoinRefusal::UnknownTarget;
			}
			if (a_target == a_leader) {
				return JoinRefusal::SelfTarget;
			}
			const auto index = _teamIdOf.find(a_leader);
			if (index == _teamIdOf.end()) {
				return JoinRefusal::NotMember;
			}
			const auto team = _teams.find(index->second);
			if (team == _teams.end()) {
				return JoinRefusal::NotMember;
			}
			if (team->second.leaderId != a_leader) {
				return JoinRefusal::NotLeader;
			}
			if (team->second.members.erase(a_target) == 0) {
				return JoinRefusal::NotMember;
			}
			a_teamId = index->second;
			_teamIdOf.erase(a_target);
			if (team->second.members.empty()) {
				_teams.erase(team);
			} else if (team->second.leaderId == a_target) {
				team->second.leaderId = team->second.members.begin()->first;
			}
			return JoinRefusal::None;
		}

		void ExpireInvites(double a_nowSeconds)
		{
			for (auto invite = _invites.begin(); invite != _invites.end();) {
				if (a_nowSeconds >= invite->second.expiresAtSeconds) {
					invite = _invites.erase(invite);
				} else {
					++invite;
				}
			}
		}

		[[nodiscard]] std::string TeamIdOf(std::uint32_t a_player) const
		{
			const auto index = _teamIdOf.find(a_player);
			return index == _teamIdOf.end() ? std::string{} : index->second;
		}

		[[nodiscard]] std::optional<std::uint32_t> LeaderOf(std::uint32_t a_player) const
		{
			const Team* team = TeamOf(a_player);
			return team == nullptr ? std::nullopt : std::optional<std::uint32_t>{ team->leaderId };
		}

		[[nodiscard]] std::vector<std::uint32_t> MembersOf(std::uint32_t a_player) const
		{
			const Team* team = TeamOf(a_player);
			if (team == nullptr) {
				return {};
			}
			std::vector<std::uint32_t> members;
			members.reserve(team->members.size());
			for (const auto& entry : team->members) {
				members.push_back(entry.first);
			}
			return members;
		}

		[[nodiscard]] bool IsLeader(std::uint32_t a_player) const
		{
			const Team* team = TeamOf(a_player);
			return team != nullptr && team->leaderId == a_player;
		}

		[[nodiscard]] bool IsMember(std::uint32_t a_player) const noexcept
		{
			return _teamIdOf.find(a_player) != _teamIdOf.end();
		}

		[[nodiscard]] std::size_t TeamCount() const noexcept { return _teams.size(); }
		[[nodiscard]] std::size_t InviteCount() const noexcept { return _invites.size(); }

		[[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> InviteParties(std::string_view a_inviteId) const
		{
			const auto found = _invites.find(std::string{ a_inviteId });
			if (found == _invites.end()) {
				return std::nullopt;
			}
			return std::make_pair(found->second.fromPlayerId, found->second.targetPlayerId);
		}

		void Clear() noexcept
		{
			_teams.clear();
			_teamIdOf.clear();
			_invites.clear();
		}

	private:
		struct InviteRecord
		{
			std::uint32_t fromPlayerId{};
			std::uint32_t targetPlayerId{};
			double expiresAtSeconds{};
		};

		struct Team
		{
			std::uint32_t leaderId{};
			std::map<std::uint32_t, MemberView> members;
		};

		[[nodiscard]] const Team* TeamOf(std::uint32_t a_player) const
		{
			const auto index = _teamIdOf.find(a_player);
			if (index == _teamIdOf.end()) {
				return nullptr;
			}
			const auto team = _teams.find(index->second);
			return team == _teams.end() ? nullptr : &team->second;
		}

		double _inviteTtlSeconds{ kInviteTtlSeconds };
		std::uint64_t _teamCounter{};
		std::uint64_t _inviteCounter{};
		std::map<std::string, Team> _teams;
		std::map<std::uint32_t, std::string> _teamIdOf;
		std::map<std::string, InviteRecord> _invites;
	};
}
