#include "F4TGnsProtocolAdapter.h"

#include "F4TInventoryContract.h"
#include "F4TGnsProtocolAdapterJson.h"
#include "F4TGnsProtocolAdapterModpack.h"
#include "F4TGnsProtocolAdapterSlots.h"
#include "F4TNpcAuthority.h"
#include "F4TRemotePlayerCapacity.h"
#include "F4TSecretWipe.h"
#include "F4TSurvivalPolicyState.h"
#include "F4TGnsProtocolAdapterImpl.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace F4T::GnsProtocolAdapter
{
	Session::Impl::~Impl()
	{
		WipePasswords();
	}

	void Session::Impl::WipePasswords()
	{
		if (clientIdentity && clientIdentity->serverPassword) {
			F4T::Secret::Wipe(*clientIdentity->serverPassword);
		}
		if (deferredHello && deferredHello->contains("serverPassword") && (*deferredHello)["serverPassword"].is_string()) {
			F4T::Secret::Wipe((*deferredHello)["serverPassword"].get_ref<std::string&>());
		}
	}

	void Session::Impl::Reset()
	{
		ready = false;
		helloSent = false;
		orderedPlayerVisibility = false;
		visibilityPauseSupported = false;
		g_visibilityPauseSupported.store(false, std::memory_order_release);
		authoritativeDownedSupported = false;
		movementEpoch = 0;
		outboundMovementSequence = 0;
		awaitingVisibilityResume = false;
		resumeStatePending = false;
		resumeMovementType.reset();
		resumeTransitionId = 0;
		visibilitySequences.clear();
		playerIncarnations.clear();
		localPlayerId = 0;
		WipePasswords();
		clientIdentity.reset();
		readyIdentityCommitter = {};
		formIdTranslator = {};
		pendingWelcome = Json{};
		pendingTicket = {};
		deferredHello.reset();
		lastLocalAppearance.reset();
		lastLocalCharacterName.reset();
		lastLocalDurableDump.clear();
		remoteDurablePlayerState.clear();
		latestRemoteTransform.clear();
		pendingRemoteActionEvents.clear();
		pendingRemoteVitals.clear();
		npcAuthority.clear();
		trackedRemotePlayers.clear();
		liveRemotePlayers.clear();
		rosterAbsence.clear();
		worldPolicy.reset();
		survivalMode.Reset();
		F4T::DifficultyReport::SetServerAcceptsReports(false);
		worldPolicyReassembler.Reset();
	}

	void Session::Impl::ForgetRemotePlayer(std::uint32_t a_playerId)
	{
		remoteDurablePlayerState.erase(a_playerId);
		latestRemoteTransform.erase(a_playerId);
		pendingRemoteActionEvents.erase(a_playerId);
		pendingRemoteVitals.erase(a_playerId);
		trackedRemotePlayers.erase(a_playerId);
		liveRemotePlayers.erase(a_playerId);
		rosterAbsence.erase(a_playerId);
	}

	bool Session::Impl::ReserveRemotePlayer(std::uint32_t a_playerId)
	{
		if (trackedRemotePlayers.contains(a_playerId)) {
			return true;
		}
		if (F4T::RemotePlayerCapacity::Decide(trackedRemotePlayers.size(), false) !=
			F4T::RemotePlayerCapacity::Admission::RejectAtCapacity) {
			trackedRemotePlayers.insert(a_playerId);
			return true;
		}
		if (!liveRemotePlayers.contains(a_playerId)) {
			return false;
		}
		const auto evicted = std::find_if(
			trackedRemotePlayers.begin(),
			trackedRemotePlayers.end(),
			[this](std::uint32_t a_tracked) { return !liveRemotePlayers.contains(a_tracked); });
		if (evicted == trackedRemotePlayers.end()) {
			return false;
		}
		ForgetRemotePlayer(*evicted);
		trackedRemotePlayers.insert(a_playerId);
		return true;
	}

	void Session::Impl::HandleRemotePlayerDisconnect(const Json& packet)
	{
		if (const auto playerId = ReadUnsigned32(packet, "playerId"); playerId && *playerId != 0) {
			ForgetRemotePlayer(*playerId);
		}
	}

	void Session::Impl::HandlePlayerRoster(const Json& packet)
	{
		const auto players = packet.find("players");
		if (players == packet.end() || !players->is_array() || players->size() > kMaximumRosterEntries) {
			return;
		}
		std::unordered_set<std::uint32_t> membership;
		for (const auto& entry : *players) {
			if (!entry.is_object()) {
				continue;
			}
			if (const auto playerId = ReadUnsigned32(entry, "playerId"); playerId && *playerId != 0) {
				membership.insert(*playerId);
			}
		}
		liveRemotePlayers = std::move(membership);

		std::vector<std::uint32_t> departed;
		for (const auto playerId : trackedRemotePlayers) {
			if (liveRemotePlayers.contains(playerId)) {
				rosterAbsence.erase(playerId);
				continue;
			}
			if (rosterAbsence.contains(playerId)) {
				departed.push_back(playerId);
			} else {
				rosterAbsence.insert(playerId);
			}
		}
		for (const auto playerId : departed) {
			ForgetRemotePlayer(playerId);
		}
		std::erase_if(rosterAbsence, [this](std::uint32_t a_playerId) { return !trackedRemotePlayers.contains(a_playerId); });
	}


	Session::Session() :
		_impl(std::make_unique<Impl>())
	{}

	Session::~Session() = default;
	Session::Session(Session&&) noexcept = default;
	Session& Session::operator=(Session&&) noexcept = default;

	void Session::Reset()
	{
		_impl->Reset();
	}

	void Session::SetClientIdentity(ClientIdentity a_identity, ReadyIdentityCommitter a_committer)
	{
		_impl->clientIdentity = std::move(a_identity);
		_impl->readyIdentityCommitter = std::move(a_committer);
	}

	void Session::SetFormIdTranslator(FormIdTranslator a_translator)
	{
		_impl->formIdTranslator = std::move(a_translator);
	}

	bool Session::IsReady() const noexcept
	{
		return _impl && _impl->ready;
	}

	std::vector<OutboundMessage> Session::AdaptOutbound(std::string_view a_legacyPayload)
	{
		return _impl->AdaptOutbound(a_legacyPayload);
	}

	InboundResult Session::AdaptInbound(std::string_view a_jsonPayload)
	{
		return _impl->AdaptInbound(a_jsonPayload);
	}

	std::vector<OutboundMessage> Session::PollDeferredHello()
	{
		return _impl->PollDeferredHello();
	}

	std::optional<NpcAuthorityAssignment> Session::GetNpcAuthority(
		std::string_view a_cellId,
		std::string_view a_worldspaceId) const
	{
		if (!_impl || a_cellId.empty()) {
			return std::nullopt;
		}
		const auto found = _impl->npcAuthority.find(ScopeKey{ std::string{ a_cellId }, std::string{ a_worldspaceId } });
		return found == _impl->npcAuthority.end() ? std::nullopt : std::optional{ NpcAuthorityAssignment{ found->second.playerId, found->second.epoch } };
	}

	std::optional<F4T::WorldPolicy::Policy> Session::GetWorldPolicy() const
	{
		return _impl ? _impl->worldPolicy : std::nullopt;
	}

	std::optional<bool> Session::GetSurvivalMode() const noexcept
	{
		return _impl ? _impl->survivalMode.Required() : std::nullopt;
	}
}
