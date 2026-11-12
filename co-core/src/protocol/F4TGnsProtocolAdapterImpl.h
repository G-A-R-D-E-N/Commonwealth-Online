#pragma once

#include "F4TGnsProtocolAdapterInternal.h"

namespace F4T::GnsProtocolAdapter
{
	using namespace Internal;

	class Session::Impl
	{
	public:
		bool ready{ false };
		bool helloSent{ false };
		bool orderedPlayerVisibility{ false };
		bool visibilityPauseSupported{ false };
		bool authoritativeDownedSupported{ false };
		std::uint64_t movementEpoch{};
		std::uint64_t outboundMovementSequence{};
		bool awaitingVisibilityResume{};
		bool resumeStatePending{};
		std::optional<std::string> resumeMovementType;
		std::uint64_t resumeTransitionId{};
		std::string resumeCellId;
		std::string resumeWorldspaceId;
		std::unordered_map<std::uint32_t, std::uint64_t> visibilitySequences;
		std::unordered_map<std::uint32_t, std::uint64_t> playerIncarnations;
		std::uint32_t localPlayerId{};
		std::optional<ClientIdentity> clientIdentity;
		ReadyIdentityCommitter readyIdentityCommitter;
		FormIdTranslator formIdTranslator;
		Json pendingWelcome;
		std::optional<Json> lastLocalAppearance;
		std::optional<std::string> lastLocalCharacterName;
		std::string lastLocalDurableDump;
		std::chrono::steady_clock::time_point lastStickySendAt{};
		std::unordered_map<std::uint32_t, Json> remoteDurablePlayerState;
		std::unordered_map<std::uint32_t, Json> latestRemoteTransform;
		std::unordered_map<std::uint32_t, Json> pendingRemoteActionEvents;
		std::unordered_map<std::uint32_t, Json> pendingRemoteVitals;
		struct NpcAuthorityEntry
		{
			std::uint32_t playerId{};
			std::uint64_t epoch{};
			ScopeKey serverScope;
		};
		std::unordered_map<ScopeKey, NpcAuthorityEntry, ScopeKeyHash> npcAuthority;
		std::unordered_set<std::uint32_t> trackedRemotePlayers;
		std::unordered_set<std::uint32_t> liveRemotePlayers;
		std::unordered_set<std::uint32_t> rosterAbsence;
		std::optional<F4T::WorldPolicy::Policy> worldPolicy;
		F4T::SurvivalPolicy::State survivalMode;
		F4T::WorldPolicy::Reassembler worldPolicyReassembler;

		void Reset();
		void ForgetRemotePlayer(std::uint32_t a_playerId);
		bool ReserveRemotePlayer(std::uint32_t a_playerId);
		void HandleRemotePlayerDisconnect(const Json& packet);
		void HandlePlayerRoster(const Json& packet);
		std::vector<OutboundMessage> AdaptTransform(Json packet);
		std::vector<OutboundMessage> AdaptNpcState(const Json& packet);
		std::vector<OutboundMessage> AdaptOutbound(std::string_view a_legacyPayload);
		InboundResult HandleWelcome(const Json& packet);
		InboundResult HandleSessionReady(const Json& packet);
		InboundResult HandleSessionEnded(const Json& packet);
		void HandleNpcAuthority(const Json& packet);
		InboundResult HandlePlayerState(const Json& packet);
		InboundResult HandleTransform(const Json& packet);
		InboundResult AdaptInbound(std::string_view a_jsonPayload);
	};
}
