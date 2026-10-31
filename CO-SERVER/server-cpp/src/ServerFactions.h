#pragma once

#include "ServerProtocol.h"
#include "ServerOutboundFrames.h"
#include "ServerSocialGraph.h"

#include "F4TPacketRateLimit.h"
#include "F4TServerConfiguration.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
	class PersistenceGate;

	struct FactionSessionState
	{
		std::optional<double> lastRosterRequestMonotonic;
		std::optional<std::string> lastRosterName;
		std::optional<double> lastRosterPushMonotonic;
		std::optional<double> lastFactionListRequestMonotonic;
		F4T::PacketRateLimit::TokenBucket mutations;
	};

	struct FactionVitals
	{
		std::uint32_t healthPercent{};
		bool          downed{ false };
		bool          dead{ false };
		bool          known{ false };
	};

	struct FactionPeer
	{
		std::uint32_t connectionId{};
		std::uint32_t playerId{};
		std::string   profileId;
		std::string   name;
		bool          socialNames{};
		FactionVitals vitals;
	};

	class FactionService
	{
	public:
		using Json = nlohmann::json;
		using Peers = std::vector<FactionPeer>;

		explicit FactionService(ProtocolStats& a_stats, SocialGraph& a_social) : stats(a_stats), social(a_social)
		{}

		[[nodiscard]] bool Enabled() const noexcept { return enabled; }
		void SetLimits(F4T::ServerConfiguration::FactionLimits a_limits) noexcept { limits = a_limits; }
		bool Enable(PersistenceGate* a_persistence);
		bool Disable();
		bool SaveNow();
		[[nodiscard]] bool SameFaction(const std::string& a_profileId, const std::string& a_otherProfileId) const;
		[[nodiscard]] const std::vector<std::string>* MembersOf(const std::string& a_profileId) const;
		[[nodiscard]] std::string FactionIdOf(const std::string& a_profileId) const;

		std::optional<std::vector<OutboundFrame>> Dispatch(
			std::string_view a_type,
			const FactionPeer& a_self,
			FactionSessionState& a_state,
			const Peers& a_peers,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds);
		[[nodiscard]] std::vector<OutboundFrame> PollInviteExpiry(const Peers& a_peers, double a_monotonicSeconds, double a_unixTimeSeconds);
		std::vector<OutboundFrame> ExistingFrames(const Peers& a_peers, std::uint32_t a_connectionId, const std::string& a_profileId, double a_unixTimeSeconds);
		[[nodiscard]] bool RosterNameChanged(std::string_view a_name, FactionSessionState& a_state, double a_monotonicSeconds);
		[[nodiscard]] std::vector<OutboundFrame> RosterFrames(const Peers& a_peers, double a_unixTimeSeconds) const;
		[[nodiscard]] std::vector<OutboundFrame> VitalsFrames(
			const Peers& a_peers,
			const std::string& a_profileId,
			double a_monotonicSeconds,
			double a_unixTimeSeconds);
		std::vector<OutboundFrame> DisconnectFrames(const Peers& a_peers, const std::string& a_profileId, double a_unixTimeSeconds);
		[[nodiscard]] Json AdminList() const;
		std::vector<OutboundFrame> AdminDisband(const Peers& a_peers, std::string_view a_factionId, double a_unixTimeSeconds, std::string& a_failure);
		std::vector<OutboundFrame> AdminPromote(const Peers& a_peers, std::string_view a_factionId, std::string_view a_targetProfileId, double a_unixTimeSeconds, std::string& a_failure);
		std::vector<OutboundFrame> AdminKick(const Peers& a_peers, std::string_view a_factionId, std::string_view a_targetProfileId, double a_unixTimeSeconds, std::string& a_failure);

	private:
		struct Faction
		{
			std::string factionId;
			std::string name;
			std::string tag;
			std::string leaderProfileId;
			std::vector<std::string> members;
			std::map<std::string, std::string> memberNames;
			double createdAt{};
			std::set<std::string> officers;
			std::string description;
			std::string motd;
			std::string color;
		};

		struct FactionInvite
		{
			std::string inviteId;
			std::string factionId;
			std::string inviterProfileId;
			std::string targetProfileId;
			std::uint32_t targetPlayerId{};
			double createdMonotonicSeconds{};
			double expiresAtUnixSeconds{};
		};

		struct FactionSnapshot
		{
			std::map<std::string, Faction> factions;
			std::map<std::string, std::string> factionByProfile;
			std::uint32_t nextFactionNumber{ 1 };
			std::map<std::string, FactionInvite> factionInvites;
		};

		struct PushedVitals
		{
			std::uint32_t healthPercent{};
			bool          downed{ false };
			bool          dead{ false };
			bool          known{ false };
		};

		static std::string MemberName(const Faction& a_faction, const std::string& a_profileId);
		static bool VitalsChanged(const FactionVitals& a_now, const PushedVitals& a_pushed, bool& a_flagsChanged);
		void ForgetVitals(const std::string& a_profileId);
		static bool RefreshFactionNames(const Peers& a_peers, Faction& a_faction);
		[[nodiscard]] Json FactionJson(const Peers& a_peers, const Faction& a_faction, const FactionPeer& a_viewer) const;
		[[nodiscard]] static std::string SuccessorProfileId(const Peers& a_peers, const Faction& a_faction);
		[[nodiscard]] static Json FactionResultPacket(std::string_view a_op, bool a_ok, std::string_view a_reason, double a_unixTimeSeconds);
		[[nodiscard]] static OutboundFrame ReliableFrame(std::uint32_t a_connectionId, const Json& a_packet)
		{
			return ::CommonwealthOnline::Server::ReliableFrame(a_connectionId, a_packet);
		}
		static void AppendFrames(std::vector<OutboundFrame>& a_destination, std::vector<OutboundFrame> a_source)
		{
			::CommonwealthOnline::Server::AppendFrames(a_destination, std::move(a_source));
		}
		[[nodiscard]] static const FactionPeer* PeerForProfile(const Peers& a_peers, const std::string& a_profileId);
		[[nodiscard]] static const FactionPeer* PeerForPlayer(const Peers& a_peers, std::uint32_t a_playerId);
		[[nodiscard]] static const FactionPeer* PeerForConnection(const Peers& a_peers, std::uint32_t a_connectionId);
		[[nodiscard]] bool FactionMutationAllows(FactionSessionState& a_state, double a_monotonicSeconds);
		Faction* FactionOfProfile(const std::string& a_profileId);
		[[nodiscard]] bool FactionNameTaken(const std::string& a_name) const;
		[[nodiscard]] bool FactionTagTaken(const std::string& a_tag) const;
		void ExpireFactionInvites(double a_monotonicSeconds);
		std::vector<OutboundFrame> InviteExpiryFrames(const Peers& a_peers, double a_unixTimeSeconds);
		[[nodiscard]] FactionSnapshot SnapshotFactions() const;
		void RestoreFactions(FactionSnapshot a_snapshot);
		std::vector<OutboundFrame> RestoreAndFail(FactionSnapshot a_snapshot, std::uint32_t a_connectionId, std::string_view a_op, double a_unixTimeSeconds);
		std::vector<std::string> EraseFaction(std::string a_factionId);
		[[nodiscard]] Json PendingInvitesJson(const Peers& a_peers, const FactionPeer& a_viewer, double a_unixTimeSeconds) const;
		Json FactionStatePacket(const Peers& a_peers, const FactionPeer& a_viewer, double a_unixTimeSeconds);
		[[nodiscard]] Json RosterPacketFor(const Peers& a_peers, const FactionPeer& a_viewer, double a_unixTimeSeconds) const;
		[[nodiscard]] SocialGraph::NameView PeerNameView(
			const std::string& a_viewerProfileId,
			const std::string& a_subjectProfileId,
			std::string_view a_realName) const;
		[[nodiscard]] Json FactionListPacket(const Peers& a_peers, const std::string& a_profileId) const;
		[[nodiscard]] Json FactionsJson() const;
		void LoadFactions(const Json& a_stored);
		bool SaveFactions();
		std::vector<OutboundFrame> RejectMalformed();
		std::vector<OutboundFrame> FactionFailure(std::uint32_t a_connectionId, std::string_view a_op, std::string_view a_reason, double a_unixTimeSeconds);
		std::vector<OutboundFrame> ProfileStateFrames(const Peers& a_peers, const std::vector<std::string>& a_profileIds, double a_unixTimeSeconds);
		std::vector<OutboundFrame> FactionStateFrames(const Peers& a_peers, const std::string& a_factionId, double a_unixTimeSeconds);
		[[nodiscard]] bool FactionInviteRateAllows(const std::string& a_profileId) const;

		std::vector<OutboundFrame> HandleFactionCreate(const FactionPeer& a_self, FactionSessionState& a_state, const Peers& a_peers, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds);
		std::vector<OutboundFrame> HandleFactionInvite(const FactionPeer& a_self, FactionSessionState& a_state, const Peers& a_peers, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds);
		std::vector<OutboundFrame> HandleFactionInviteResponse(const FactionPeer& a_self, FactionSessionState& a_state, const Peers& a_peers, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds);
		std::vector<OutboundFrame> HandleFactionLeave(const FactionPeer& a_self, FactionSessionState& a_state, const Peers& a_peers, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds);
		std::vector<OutboundFrame> HandleFactionKick(const FactionPeer& a_self, FactionSessionState& a_state, const Peers& a_peers, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds);
		std::vector<OutboundFrame> HandleFactionDisband(const FactionPeer& a_self, FactionSessionState& a_state, const Peers& a_peers, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds);
		std::vector<OutboundFrame> HandleFactionPromote(const FactionPeer& a_self, FactionSessionState& a_state, const Peers& a_peers, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds);
		std::vector<OutboundFrame> HandleFactionSetRank(const FactionPeer& a_self, FactionSessionState& a_state, const Peers& a_peers, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds);
		std::vector<OutboundFrame> HandleFactionSetProfile(const FactionPeer& a_self, FactionSessionState& a_state, const Peers& a_peers, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds);
		std::vector<OutboundFrame> HandleFactionRosterRequest(const FactionPeer& a_self, FactionSessionState& a_state, const Peers& a_peers, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds);
		std::vector<OutboundFrame> HandleFactionList(const FactionPeer& a_self, FactionSessionState& a_state, const Peers& a_peers, const Json& a_packet, double a_monotonicSeconds);
		std::optional<std::vector<OutboundFrame>> DispatchType(std::string_view a_type, const FactionPeer& a_self, FactionSessionState& a_state, const Peers& a_peers, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds);

		ProtocolStats& stats;
		SocialGraph& social;
		bool enabled{};
		F4T::ServerConfiguration::FactionLimits limits;
		PersistenceGate* persistence{};
		std::uint32_t nextFactionNumber{ 1 };
		std::uint32_t nextFactionInviteNumber{ 1 };
		std::map<std::string, Faction> factions;
		std::map<std::string, std::string> factionByProfile;
		Json quarantinedFactions = Json::object();
		std::map<std::string, FactionInvite> factionInvites;
		std::map<std::string, std::deque<double>> factionInviteWindows;
		std::set<std::string> expiredInviters;
		std::map<std::string, PushedVitals> pushedVitals;
		std::map<std::string, double> factionVitalsPushMonotonic;
	};
}
