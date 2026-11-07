#include "ServerProtocolImpl.h"
#include "ServerAdminMenu.h"
#include "ServerFurnitureRelay.h"
#include "F4TPlayerKey.h"
#include "F4TSleepBedIdentity.h"
#include "ServerRefusalWindow.h"
#include "F4TServerWorldState.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <utility>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using F4T::PlayerStateValidation::kRelayedPlayerStateFields;

		constexpr double kHoursPerDay = 24.0;
		constexpr double kHalfDayHours = kHoursPerDay / 2.0;
		constexpr double kTeleportDistanceStrikeFactor = 4.0;
		constexpr double kPvpDamageStrikeFactor = 2.0;

		bool IsTeleportStrike(const F4T::NetworkValidation::MovementValidationResult& a_movement)
		{
			if (a_movement.verdict == F4T::NetworkValidation::MovementVerdict::RejectedDestination) {
				return true;
			}
			return a_movement.verdict == F4T::NetworkValidation::MovementVerdict::RejectedDistance &&
			       a_movement.distance > kTeleportDistanceStrikeFactor * a_movement.allowedDistance;
		}

		bool HasDurablePlayerState(const Json& a_packet)
		{
			for (const char* field : kRelayedPlayerStateFields) {
				if (a_packet.contains(field)) {
					return true;
				}
			}
			return false;
		}

		std::uint64_t DurablePlayerStateDigest(const Json& a_cached, std::string_view a_factionTag)
		{
			Json durable = Json::object();
			for (const char* field : kRelayedPlayerStateFields) {
				if (const auto found = a_cached.find(field); found != a_cached.end()) {
					durable[field] = *found;
				}
			}
			return std::hash<std::string>{}(durable.dump() + '|' + std::string{ a_factionTag });
		}

		double ShortGameHourDelta(double a_gameHour, double a_previousGameHour)
		{
			double delta = std::fmod(a_gameHour - a_previousGameHour, kHoursPerDay);
			if (delta > kHalfDayHours) {
				delta -= kHoursPerDay;
			} else if (delta <= -kHalfDayHours) {
				delta += kHoursPerDay;
			}
			return delta;
		}

		bool WorldStateTransitionAllowed(Session& a_session, SessionTypes::WorldDaysBaseline& a_days, const Json& a_packet, double a_unixTimeSeconds)
		{
			const auto hourNode = a_packet.find("gameHour");
			const bool hasGameHour = hourNode != a_packet.end() && hourNode->is_number();
			double gameHour = 0.0;
			if (hasGameHour) {
				gameHour = hourNode->get<double>();
				if (!std::isfinite(gameHour) || gameHour < 0.0 || gameHour > kHoursPerDay) {
					return false;
				}
				if (a_session.lastWorldGameHour) {
					const auto step = std::abs(ShortGameHourDelta(gameHour, *a_session.lastWorldGameHour));
					const bool tooSoon =
						a_unixTimeSeconds - a_session.lastWorldTimeAcceptedUnix < SessionTypes::kWorldGameTimeResyncSeconds;
					if (step > SessionTypes::kMaximumWorldGameHourStepPerPacket && tooSoon) {
						return false;
					}
				}
			}

			const auto daysNode = a_packet.find("gameDaysPassed");
			const bool hasGameDays = daysNode != a_packet.end() && daysNode->is_number() && std::isfinite(daysNode->get<double>());
			double gameDays = 0.0;
			bool resync = false;
			if (hasGameDays) {
				gameDays = daysNode->get<double>();
				if (a_days.gameDays) {
					const auto advance = gameDays - *a_days.gameDays;
					const auto allowance = SessionTypes::kWorldGameDaysAdvanceSlack +
						std::max(0.0, a_unixTimeSeconds - a_days.acceptedUnix) *
							SessionTypes::kMaximumWorldGameDaysPerRealSecond;
					if (advance > allowance) {
						const bool resyncDue = !a_days.lastResyncUnix ||
							a_unixTimeSeconds - *a_days.lastResyncUnix >= SessionTypes::kWorldGameDaysResyncIntervalSeconds;
						if (!resyncDue || !a_packet.value("timeSync", false)) {
							return false;
						}
						resync = true;
					}
				}
			}

			const auto weatherNode = a_packet.find("weatherFormId");
			const bool hasWeatherFormId = weatherNode != a_packet.end() && weatherNode->is_string();
			std::string weatherFormId;
			if (hasWeatherFormId) {
				weatherFormId = weatherNode->get<std::string>();
				if (a_session.lastWorldWeatherFormId && *a_session.lastWorldWeatherFormId != weatherFormId &&
					a_unixTimeSeconds - a_session.lastWorldWeatherChangeUnix <
						SessionTypes::kMinimumWorldWeatherChangeIntervalSeconds) {
					return false;
				}
			}

			if (hasGameHour) {
				a_session.lastWorldGameHour = gameHour;
				a_session.lastWorldTimeAcceptedUnix = a_unixTimeSeconds;
			}
			if (hasGameDays) {
				a_days.gameDays = gameDays;
				a_days.acceptedUnix = a_unixTimeSeconds;
				if (resync) {
					a_days.lastResyncUnix = a_unixTimeSeconds;
				}
			}
			if (hasWeatherFormId && (!a_session.lastWorldWeatherFormId || *a_session.lastWorldWeatherFormId != weatherFormId)) {
				a_session.lastWorldWeatherFormId = std::move(weatherFormId);
				a_session.lastWorldWeatherChangeUnix = a_unixTimeSeconds;
			}
			return true;
		}
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::HandleHello(
		std::uint32_t a_connectionId,
		Session& a_session,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		const auto protocolVersion = ReadUInt32(a_packet, "protocolVersion");
		const auto compatibilityCapabilities = NormalizeCompatibilityCapabilities(a_packet);
		const auto difficulty = a_packet.find("difficulty");
		if (a_session.active || !protocolVersion || *protocolVersion != kProtocolVersion || !compatibilityCapabilities ||
			(difficulty != a_packet.end() && !DifficultyFieldValid(*difficulty))) {
			Reject();
			return {};
		}
		a_session.compatibilityCapabilities = *compatibilityCapabilities;
		a_session.privacy = {};
		if (a_session.compatibilityCapabilities.Has(F4T::Identity::PrivacyCapability)) {
			if (const auto privacy = a_packet.find("privacy"); privacy != a_packet.end()) {
				if (const auto settings = F4T::PrivacyValidation::NormalizePrivacy(*privacy)) a_session.privacy = *settings;
			}
		}
		a_session.compatibilityRevisions.clear();
		hasEverActivatedSession = true;
		a_session.socialKey.clear();
		a_session.questProfile.clear();
		a_session.socialKeyRequired = characterSlots.Enabled();
		Json slotReadyBlock = Json::object();
		std::string persistenceProfile;
		std::vector<OutboundFrame> frames;
		std::vector<std::string> supersededKeys;
		if (characterSlots.Enabled()) {
			std::vector<std::uint32_t> replaced;
			bool replacedActive = false;
			for (const auto& [connectionId, other] : sessions) {
				if (connectionId != a_connectionId && !a_session.profileId.empty() && other.profileId == a_session.profileId) {
					replaced.push_back(connectionId);
					replacedActive = replacedActive || other.active;
				}
			}
			for (const auto connectionId : replaced) {
				frames.push_back(ReliableFrame(connectionId, Json{ { "type", "sessionEnded" }, { "code", "session_superseded" }, { "reason", "This session was replaced by a newer connection for the same character account." }, { "serverTime", a_unixTimeSeconds } }));
				characterSlots.ReleaseConnection(connectionId);
				supersededKeys.push_back(std::string{ SessionSocialKeyView(sessions.at(connectionId)) });
				AppendFrames(frames, HandleDisconnected(connectionId, a_unixTimeSeconds, false, false));
			}
			if (replacedActive) {
				AppendFrames(frames, FactionDisconnectFrames(a_session.profileId, a_unixTimeSeconds));
			}
		}
		ReleaseExpiredHolds(a_unixTimeSeconds);
		std::optional<SessionTypes::HeldPlayer> hold;
		if (const auto held = heldPlayers.find(a_session.profileId); held != heldPlayers.end() && a_session.compatibilityCapabilities.Has(F4T::Identity::ReconnectResumeCapability)) {
			hold = held->second;
			heldPlayers.erase(held);
		}
		if (characterSlots.Enabled()) {
			auto slotHello = characterSlots.OnHello(
				a_connectionId, a_session.profileId, hold ? std::string_view{ hold->persistenceProfile } : std::string_view{}, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
			AppendFrames(frames, ForgetErasedProfiles(a_unixTimeSeconds));
			slotReadyBlock = std::move(slotHello.readyBlock);
			persistenceProfile = std::move(slotHello.persistenceProfile);
			a_session.socialKey = persistenceProfile;
			a_session.questProfile = std::move(slotHello.questProfile);
			if (!a_session.questProfile.empty() && inventoryAuthority != nullptr) {
				try { (void)inventoryAuthority->Store().ClearDeathBagDeparted(a_session.questProfile); } catch (const std::exception&) {}
			}
			for (const auto& oldKey : supersededKeys) {
				if (!oldKey.empty() && oldKey != persistenceProfile) AppendFrames(frames, FriendDisconnectFrames(oldKey, a_unixTimeSeconds));
			}
		}
		const bool resumed = hold.has_value() && hold->persistenceProfile == persistenceProfile;
		if (resumed) a_session.playerId = hold->playerId;
		a_session.active = true;
		a_session.playerIncarnation = ++nextPlayerIncarnation;
		StartDifficultyTracking(a_session, a_packet, a_monotonicSeconds);
		peerVisibility.AddPeer(a_session.playerId);
		peerVisibility.RegisterEntity(a_session.playerId);
		peerSnapshots.insert_or_assign(a_session.playerId, std::array<PeerSnapshotSender, 2>{});
		const bool becameHost = worldStateHostPlayerId == 0;
		if (becameHost) {
			worldStateHostPlayerId = a_session.playerId;
		}
		Json ready{
			{ "type", "sessionReady" },
			{ "startingInventoryRecovery", nullptr },
			{ "profileId", a_session.profileId },
			{ "playerId", a_session.playerId },
			{ "protocolVersion", kProtocolVersion },
			{ "serverProtocolVersion", kProtocolVersion },
			{ "orderedPlayerVisibility", true },
			{ "visibilityPauseSupported", true },
			{ "authoritativeDownedSupported", true },
			{ "worldStateHostPlayerId", worldStateHostPlayerId },
			{ "consoleAllowed", IsLiveAdminSession(a_session) },
			{ "survivalMode", survivalMode },
			{ "survivalNeeds", SurvivalNeedsActive() },
			{ "difficultyReportSupported", true },
			{ "serverTime", a_unixTimeSeconds }
		};
		if (!slotReadyBlock.empty()) {
			ready["characterSlots"] = std::move(slotReadyBlock);
		}
		if (!persistenceProfile.empty()) {
			ready["persistenceProfile"] = std::move(persistenceProfile);
		}
		if (resumed) ready["resumed"] = true;
		AppendFrames(frames, SendLifecycleState(a_connectionId, a_session, F4T::Session::LifecycleState::Connected, a_unixTimeSeconds));
		LogSurvivalWelcome(a_session.playerId);
		frames.push_back(ReliableFrame(a_connectionId, ready));
		if (IsLiveAdminSession(a_session)) frames.push_back(ReliableFrame(a_connectionId, AdminMenuService::MakeState(a_unixTimeSeconds)));
		AppendFrames(frames, SendLifecycleState(a_connectionId, a_session, F4T::Session::LifecycleState::Ready, a_unixTimeSeconds));
		AppendFrames(frames, ExistingWelcomeStateFrames(a_connectionId, a_session, a_unixTimeSeconds));
		AppendFrames(frames, StateBagSnapshotFrames(a_connectionId, a_session, stateBag));
		if (F4T::DeathBagLocal::Enabled && inventoryAuthority != nullptr && InventoryNegotiated(a_session) && !a_session.questProfile.empty()) {
			try {
				const auto profile = inventoryAuthority->ProfileForConnection(a_connectionId);
				const auto grants = profile ? inventoryAuthority->Store().GetDeathBagRecipientGrants(*profile, a_session.questProfile) : std::nullopt;
				if (grants && !grants->empty()) {
					Json offered = *grants;
					for (auto& grant : offered) grant.erase("freshProof");
					frames.push_back(ReliableFrame(a_connectionId, Json{ { "type", "deathBagGrantReconciliation" }, { "grants", std::move(offered) } }));
				}
			} catch (const std::exception&) {
				std::cerr << "Death bag grant reconciliation is unavailable.\n";
			}
		}
		if (inventoryAuthority != nullptr && !a_session.questProfile.empty() && !SessionSocialKeyView(a_session).empty()) {
			try {
				auto& store = inventoryAuthority->Store();
				const auto active = store.GetActiveDeathIntent(a_session.questProfile);
				const auto now = store.CurrentUnixTime();
				if (!store.InventoryAuthorityBlocked() && active && std::isfinite(now) && now >= 0.0 &&
					active->at("profileId") == std::string{ SessionSocialKeyView(a_session) } && active->at("characterId") == a_session.questProfile) {
					const bool downed = active->at("state") == "downed";
					const auto remaining = downed ? std::clamp(active->at("deadlineUnix").get<double>() - now, 0.0,
						static_cast<double>(F4T::DownedState::MaximumDownedSeconds)) : 0.0;
					(void)downedService.RestoreOffline(SessionSocialKeyView(a_session), downed ? F4T::DownedState::State::Downed : F4T::DownedState::State::Dead,
						remaining, a_monotonicSeconds, a_session.questProfile);
				}
			} catch (const std::exception&) {
				std::cerr << "Death state could not be restored; inventory remains unchanged.\n";
			}
		}
		AppendFrames(frames, downedService.Join(DownedPeers(), a_connectionId, a_session.playerId, SessionSocialKeyView(a_session), a_monotonicSeconds, a_session.questProfile));
		AppendFrames(frames, ExistingPluginListFrames(a_connectionId));
		if (becameHost) {
			AppendFrames(frames, BroadcastReliable(WorldStateHostPacket(a_session.playerId, a_unixTimeSeconds)));
		}
		events.Notify(ServerEvent{ ServerEventKind::PlayerReady, a_connectionId, a_session.playerId, a_session.profileId, nullptr });
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::HandleTransform(
		std::uint32_t a_connectionId,
		Session& a_session,
		Json a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		const auto retry = a_packet.find("resumeRetry");
		const bool resumeRetry = retry != a_packet.end() && retry->is_boolean() && retry->get<bool>();
		const auto resumeTransition = a_packet.find("resumeTransitionId");
		const auto resumeTransitionId = resumeTransition != a_packet.end() && resumeTransition->is_number_unsigned() ?
			resumeTransition->get<std::uint64_t>() : 0;
		Json frameProbe;
		if (const auto pa = a_packet.find("powerArmor"); pa != a_packet.end() && pa->is_object()) {
			const auto in = pa->find("inFrame");
			const auto fb = pa->find("frameBaseFormId");
			if (in != pa->end() && in->is_boolean() && in->get<bool>() && fb != pa->end() && fb->is_string() && fb->get<std::string>().size() <= 8) {
				frameProbe = Json{ { "powerArmor", Json{ { "inFrame", true }, { "frameBaseFormId", *fb } } } };
			}
		}
		auto current = NormalizeTransform(a_packet);
		if (!current) {
			Reject();
			stats.movementRejected++;
			return {};
		}
		std::optional<F4T::NetworkValidation::MovementState> previous;
		const auto& previousMovement = a_session.lastMovement ? a_session.lastMovement : a_session.pausedMovement;
		if (previousMovement) {
			previous = F4T::NetworkValidation::MovementState{
				previousMovement->x,
				previousMovement->y,
				previousMovement->z,
				previousMovement->cellId,
				previousMovement->worldspaceId,
				previousMovement->movementType
			};
		}
		if (!a_session.resumeAwaitingTransform && a_session.movementEpoch != 0 && previousMovement &&
			resumeRetry &&
			resumeTransitionId != 0 && resumeTransitionId == a_session.acceptedResumeTransitionId && previousMovement->cellId == current->cellId &&
			previousMovement->worldspaceId == current->worldspaceId) {
			current->movementType = "normal";
			a_packet["movementType"] = "normal";
		}
		if (a_session.resumeAwaitingTransform && previousMovement && current->movementType == "normal" &&
			(previousMovement->cellId != current->cellId || previousMovement->worldspaceId != current->worldspaceId)) {
			current->movementType = previousMovement->worldspaceId != current->worldspaceId ? "worldspace_change" : "cell_change";
			a_packet["movementType"] = current->movementType;
		}
		const F4T::NetworkValidation::MovementState currentView{
			current->x,
			current->y,
			current->z,
			current->cellId,
			current->worldspaceId,
			current->movementType
		};
		const auto movement = F4T::NetworkValidation::ValidateMovement(
			previous,
			a_session.lastMovementMonotonic,
			currentView,
			a_monotonicSeconds,
			a_session.movementTransitions);
		if (!movement.Accepted()) {
			Reject();
			stats.movementRejected++;
			if (IsTeleportStrike(movement) && a_session.teleportRejections.Observe(a_monotonicSeconds)) {
				violations.push_back(CommonwealthOnline::Server::PendingViolation{ a_connectionId, CommonwealthOnline::Server::Violation::TeleportDistance, MovementRejectReason(movement) });
			}
			if (movement.verdict == F4T::NetworkValidation::MovementVerdict::RejectedDestination &&
				RuntimeSupport::NoteRefusal(movementRefusals, "player:" + std::to_string(a_session.playerId), a_monotonicSeconds).writeLine) {
				std::cerr << "Movement rejected for player " << a_session.playerId << ": " << MovementRejectReason(movement) << '\n';
			}
			if (a_session.lastMovement) {
				stats.movementCorrectionsSent++;
				return { PositionCorrectionFrame(
					a_connectionId,
					*a_session.lastMovement,
					MovementRejectReason(movement),
					a_unixTimeSeconds) };
			}
			return {};
		}
		if (!frameProbe.is_null()) ObserveParkedSuitFrame(a_session, frameProbe, a_monotonicSeconds);
		if (movement.verdict == F4T::NetworkValidation::MovementVerdict::AcceptedExplicitTransition) {
			a_session.movementTransitions.Record(a_monotonicSeconds);
		}
		if (movement.verdict == F4T::NetworkValidation::MovementVerdict::AcceptedBoundaryCrossing) {
			a_session.movementTransitions.RecordBoundaryCrossing(a_monotonicSeconds, previousMovement->cellId, current->cellId);
		}
		if (Activity::MovementChanged(*current, a_session.lastMovement)) {
			a_session.playerActivityPending = true;
		}
		std::vector<OutboundFrame> frames;
		if (a_session.lastSleepPacket && previousMovement &&
			(F4T::NetworkValidation::IsMovementTransitionType(current->movementType) ||
			 previousMovement->cellId != current->cellId || previousMovement->worldspaceId != current->worldspaceId)) {
			if (a_session.lastMovement) {
				frames = SleepUseWithNeeds(a_connectionId, a_session, F4T::SleepBedIdentity::MakeSleepExit(), a_monotonicSeconds);
			} else {
				a_session.lastSleepPacket.reset();
			}
		}
		const auto previousScope = ScopeFromMovement(a_session.lastMovement);
		const auto previousObjectInterest = InterestScopeFromMovement(a_session.lastMovement);
		a_packet["playerId"] = a_session.playerId;
		a_packet["playerIncarnation"] = a_session.playerIncarnation;
		a_packet["serverTime"] = a_unixTimeSeconds;
		a_session.lastMovement = std::move(current);
		a_session.pausedMovement.reset();
		a_session.acceptedResumeTransitionId = resumeTransitionId;
		a_session.lastMovementMonotonic = a_monotonicSeconds;
		a_session.lastTransformPacket = a_packet;
		AppendFrames(frames, ReconcilePeerVisibility(a_connectionId, a_session, a_unixTimeSeconds, previousScope.has_value()));
		const bool resumingVisibility = a_session.resumeAwaitingTransform;
		if (a_session.movementEpoch != 0 && resumeTransitionId != 0 && resumeTransitionId > a_session.acknowledgedResumeTransitionId) {
			frames.push_back(ReliableFrame(a_connectionId, Json{
				{ "type", "visibilityResumed" },
				{ "movementEpoch", a_session.movementEpoch },
				{ "resumeTransitionId", resumeTransitionId }
			}));
			a_session.acknowledgedResumeTransitionId = resumeTransitionId;
		}
		a_session.resumeAwaitingTransform = false;
		if (resumingVisibility && a_session.lastDurablePlayerState) {
			Json state = *a_session.lastDurablePlayerState;
			state["serverTime"] = a_unixTimeSeconds;
			state.erase("progression");
			if (state.contains("vitals") && state["vitals"].is_object()) {
				if (const auto downed = downedService.StateOf(a_session.playerId)) {
					state["vitals"]["isDowned"] = *downed == F4T::DownedState::State::Downed || *downed == F4T::DownedState::State::Dead;
					state["vitals"]["isDead"] = *downed == F4T::DownedState::State::Dead;
					(*a_session.lastDurablePlayerState)["vitals"] = state["vitals"];
				}
			}
			for (const auto& [connectionId, recipient] : sessions) {
				if (connectionId != a_connectionId && recipient.active) {
					Json masked = state;
					MaskRelayedName(masked, recipient, a_session);
					frames.push_back(ReliableFrame(connectionId, masked));
				}
			}
			a_session.lastDurableDigest = DurablePlayerStateDigest(state, factionService.TagForProfile(a_session.profileId));
			a_session.lastDurableRelayMonotonic = a_monotonicSeconds;
			a_session.durableRelayPending = false;
			if (F4T::DownedValidation::CarriesVitals(state) && factionService.Enabled()) {
				AppendFrames(frames, factionService.VitalsFrames(FactionPeers(), a_session.profileId, a_monotonicSeconds, a_unixTimeSeconds));
			}
		}
		if (resumingVisibility) AppendFrames(frames, downedService.ResumeFrames(DownedPeers(), a_session.playerId, a_monotonicSeconds));
		AppendFrames(frames, LifecycleFramesForMovement(a_connectionId, a_session, a_unixTimeSeconds));
		if (inventoryAuthority != nullptr) AppendFrames(frames, RefreshInventoryProjection());

		for (auto& [connectionId, recipient] : sessions) {
			if (connectionId == a_connectionId || !recipient.active ||
				!peerVisibility.IsVisible(recipient.playerId, a_session.playerId)) {
				continue;
			}
			auto frame = SnapshotFrame(connectionId, recipient, Family::Transform, a_packet);
			if (!frame.payload.empty()) {
				frames.push_back(std::move(frame));
			}
		}
		AppendFrames(frames, ReconcileNpcAuthority(a_unixTimeSeconds));
		const auto currentScope = ScopeFromMovement(a_session.lastMovement);
		if (currentScope && currentScope != previousScope) {
			if (!previousScope) {
				for (const auto& assignment : npcAuthority.Assignments()) {
					frames.push_back(ReliableFrame(a_connectionId, AuthorityPacket(assignment, a_unixTimeSeconds)));
				}
				AppendFrames(frames, ExistingTransformFrames(a_connectionId, a_session, a_unixTimeSeconds));
			} else if (const auto assignment = npcAuthority.Get(*currentScope)) {
				frames.push_back(ReliableFrame(a_connectionId, AuthorityPacket(*assignment, a_unixTimeSeconds)));
			}
			AppendFrames(frames, ExistingNpcCoopFrames(a_connectionId, a_session, a_unixTimeSeconds));
		}
		AppendFrames(frames, ObjectStateFramesForTransform(a_connectionId, a_session, currentScope != previousScope, previousObjectInterest));
		return frames;
	}

	std::string ProtocolHost::Impl::PlayerKeyOf(const Session& a_session) const
	{
		return F4T::PlayerKey::Derive(playerKeySecret, a_session.steamId, a_session.profileId);
	}

	void ProtocolHost::Impl::MaskRelayedName(Json& relay, const Session& a_viewer, const Session& a_subject)
	{
		if (const auto relayedName = relay.find("characterName"); relayedName != relay.end() && relayedName->is_string()) {
			const std::string viewerKey{ SessionSocialKeyView(a_viewer) };
			const std::string subjectKey{ SessionSocialKeyView(a_subject) };
			const auto view = socialGraph.NameViewFor(viewerKey, a_viewer.profileId, subjectKey, relayedName->get<std::string>(), factionService.SameFaction(a_viewer.profileId, a_subject.profileId));
			relay["displayName"] = view.displayName;
			if (auto tag = factionService.TagForProfile(a_subject.profileId); !tag.empty()) relay["factionTag"] = std::move(tag);
			const auto playerKey = PlayerKeyOf(a_subject);
			if (!playerKey.empty()) relay["playerKey"] = playerKey;
			if (!view.disclosedName) {
				// Constraint: legacy clients get display text in characterName instead of no characterName.
				if (!SessionHasSocialNames(a_viewer)) relay["characterName"] = view.displayName;
				else relay.erase("characterName");
			}
		}
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::HandlePlayerState(
		std::uint32_t a_connectionId,
		Session& a_session,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		auto normalized = NormalizePlayerState(a_packet);
		if (!normalized) {
			Reject();
			return {};
		}
		(*normalized)["playerId"] = a_session.playerId;
		(*normalized)["playerIncarnation"] = a_session.playerIncarnation;
		(*normalized)["serverTime"] = a_unixTimeSeconds;
		if (normalized->contains("vitals") &&
			!F4T::PacketRateLimit::TakeToken(a_session.vitalsUpdates, a_monotonicSeconds, kVitalsUpdatesRefillPerSecond, kVitalsUpdatesBurst)) {
			normalized->erase("vitals");
		}
		const bool hasVitals = normalized->contains("vitals");
		const bool hasDurableState = HasDurablePlayerState(*normalized);
		if (hasDurableState || hasVitals) {
			Json cached = a_session.lastDurablePlayerState.value_or(Json::object());
			cached["type"] = "playerState";
			cached["playerId"] = a_session.playerId;
			cached["playerIncarnation"] = a_session.playerIncarnation;
			cached["serverTime"] = a_unixTimeSeconds;
			for (const char* field : kRelayedPlayerStateFields) {
				if (normalized->contains(field)) {
					cached[field] = (*normalized)[field];
				}
			}
			if (const auto vitals = normalized->find("vitals"); vitals != normalized->end()) {
				cached["vitals"] = *vitals;
			}
			cached.erase("actionEvents");
			a_session.lastDurablePlayerState = std::move(cached);
			if (const auto explosive = SessionTypes::EquippedExplosiveKind(*a_session.lastDurablePlayerState)) {
				a_session.lastEquippedExplosive = explosive;
				a_session.lastEquippedExplosiveMonotonic = a_monotonicSeconds;
			}
		}
		ObserveParkedSuitFrame(a_session, *normalized, a_monotonicSeconds);
		if (a_session.visibilityPaused || a_session.resumeAwaitingTransform) {
			if (hasDurableState) a_session.durableRelayPending = true;
			if (F4T::DownedValidation::CarriesVitals(*normalized)) {
				const auto vitals = F4T::DownedValidation::ReadVitals(*normalized);
				return downedService.ObserveVitals([this]() { return DownedPeers(); }, a_session.playerId, vitals.isDowned, vitals.isDead, a_monotonicSeconds, vitals.hasStateFlags);
			}
			return {};
		}
		RecordCombatActions(a_session, *normalized, a_monotonicSeconds);
		bool relayDurableState = false;
		if (hasDurableState) {
			const auto digest = DurablePlayerStateDigest(a_session.lastDurablePlayerState.value_or(Json::object()), factionService.TagForProfile(a_session.profileId));
			const bool changed = !a_session.lastDurableDigest || *a_session.lastDurableDigest != digest;
			const bool intervalElapsed = !a_session.lastDurableRelayMonotonic ||
				a_monotonicSeconds - *a_session.lastDurableRelayMonotonic >=
					SessionTypes::kDurablePlayerStateRelayIntervalSeconds;
			if (changed && intervalElapsed) {
				relayDurableState = true;
				a_session.lastDurableDigest = digest;
				a_session.lastDurableRelayMonotonic = a_monotonicSeconds;
				a_session.durableRelayPending = false;
				stats.durableStateRelays++;
			} else {
				a_session.durableRelayPending = changed;
				stats.durableStateSuppressed++;
			}
		}
		std::vector<OutboundFrame> frames;
		for (const auto& [connectionId, recipient] : sessions) {
			if (connectionId == a_connectionId || !recipient.active) {
				continue;
			}
			Json relay = *normalized;
			relay.erase("progression");
			if (relay.contains("actionEvents") && !peerVisibility.IsVisible(recipient.playerId, a_session.playerId)) {
				relay.erase("actionEvents");
			}
			if (HasDurablePlayerState(relay) && !relayDurableState) {
				for (const char* field : kRelayedPlayerStateFields) {
					relay.erase(field);
				}
			}
			MaskRelayedName(relay, recipient, a_session);
			if (!relay.contains("actionEvents") && !HasDurablePlayerState(relay) && !relay.contains("displayName") && !relay.contains("vitals")) {
				continue;
			}
			frames.push_back(ReliableFrame(connectionId, relay));
		}
		if (F4T::DownedValidation::CarriesVitals(*normalized)) {
			const auto vitals = F4T::DownedValidation::ReadVitals(*normalized);
			AppendFrames(frames, downedService.ObserveVitals([this]() { return DownedPeers(); }, a_session.playerId, vitals.isDowned, vitals.isDead, a_monotonicSeconds, vitals.hasStateFlags));
		}
		if (hasVitals && factionService.Enabled()) {
			AppendFrames(frames, factionService.VitalsFrames(FactionPeers(), a_session.profileId, a_monotonicSeconds, a_unixTimeSeconds));
		}
		return frames;
	}

	void ProtocolHost::Impl::MarkFactionTagChanges()
	{
		for (auto& [connectionId, session] : sessions) {
			if (!session.active || !session.lastDurablePlayerState) continue;
			const auto digest = DurablePlayerStateDigest(*session.lastDurablePlayerState, factionService.TagForProfile(session.profileId));
			if (!session.lastDurableDigest || *session.lastDurableDigest != digest) session.durableRelayPending = true;
		}
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::PollDurablePlayerState(double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		std::vector<OutboundFrame> frames = SweepParkedSuits(a_monotonicSeconds, a_unixTimeSeconds);
		for (auto& [senderConnectionId, sender] : sessions) {
			if (!sender.active || sender.visibilityPaused || sender.resumeAwaitingTransform || !sender.durableRelayPending || !sender.lastDurablePlayerState) {
				continue;
			}
			if (sender.lastDurableRelayMonotonic &&
				a_monotonicSeconds - *sender.lastDurableRelayMonotonic < SessionTypes::kDurablePlayerStateRelayIntervalSeconds) {
				continue;
			}
			Json relay = Json::object();
			relay["type"] = "playerState";
			relay["playerId"] = sender.playerId;
			relay["playerIncarnation"] = sender.playerIncarnation;
			relay["serverTime"] = a_unixTimeSeconds;
			for (const char* field : kRelayedPlayerStateFields) {
				if (sender.lastDurablePlayerState->contains(field)) {
					relay[field] = (*sender.lastDurablePlayerState)[field];
				}
			}
			for (const auto& [connectionId, recipient] : sessions) {
				if (connectionId != senderConnectionId && recipient.active) {
					Json masked = relay;
					MaskRelayedName(masked, recipient, sender);
					frames.push_back(ReliableFrame(connectionId, masked));
				}
			}
			sender.lastDurableDigest = DurablePlayerStateDigest(*sender.lastDurablePlayerState, factionService.TagForProfile(sender.profileId));
			sender.lastDurableRelayMonotonic = a_monotonicSeconds;
			sender.durableRelayPending = false;
			stats.durableStateRelays++;
		}
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::HandleWorldState(
		std::uint32_t a_connectionId,
		Session& a_session,
		const Json& a_packet,
		double a_unixTimeSeconds)
	{
		if (worldStateHostPlayerId != a_session.playerId) {
			Reject();
			stats.worldStateRejected++;
			return {};
		}
		auto normalized = NormalizeWorldState(a_packet);
		if (!normalized) {
			Reject();
			stats.worldStateRejected++;
			return {};
		}
		std::vector<OutboundFrame> resyncFrames;
		bool hostWeatherStripped = false;
		if (const auto hostWeather = normalized->find("weatherFormId");
			hostWeather != normalized->end() && hostWeather->is_string() &&
			F4T::ServerWorldState::DecideHostWeatherRelay(lockedWeatherFormId, hostWeather->get<std::string>()) ==
				F4T::ServerWorldState::HostWeatherRelay::Strip) {
			normalized->erase("weatherFormId");
			hostWeatherStripped = true;
			resyncFrames.push_back(ReliableFrame(a_connectionId, Json{
				{ "type", "serverWorldState" },
				{ "weatherFormId", *lockedWeatherFormId },
				{ "serverTime", a_unixTimeSeconds } }));
		}
		if (!WorldStateTransitionAllowed(a_session, worldDaysBaseline, *normalized, a_unixTimeSeconds)) {
			a_session.pendingWorldTimeSync = a_session.pendingWorldTimeSync || normalized->value("timeSync", false);
			Reject();
			stats.worldStateRejected++;
			return {};
		}
		if (a_session.pendingWorldTimeSync && normalized->contains("gameHour") && normalized->contains("gameDaysPassed")) {
			(*normalized)["timeSync"] = true;
			a_session.pendingWorldTimeSync = false;
		}
		(*normalized)["playerId"] = a_session.playerId;
		(*normalized)["serverTime"] = a_unixTimeSeconds;
		stats.worldStatePacketsReceived++;
		std::vector<OutboundFrame> frames = std::move(resyncFrames);
		if (hostWeatherStripped && !normalized->contains("gameHour") && !normalized->contains("gameDaysPassed")) {
			return frames;
		}
		for (const auto& [connectionId, recipient] : sessions) {
			if (connectionId != a_connectionId && recipient.active) {
				frames.push_back(ReliableFrame(connectionId, *normalized));
			}
		}
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::HandleCombatHit(
		std::uint32_t a_connectionId,
		Session& a_session,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		auto normalized = NormalizeCombatHit(a_packet);
		if (!normalized) {
			Reject();
			stats.combatHitsRejected++;
			return {};
		}
		if (!friendlyFire) {
			Reject();
			stats.combatHitsRejected++;
			return {};
		}
		const auto sequence = ReadUInt32(*normalized, "sequence").value_or(0);
		if (!F4T::GnsSnapshotEnvelope::IsNewer(sequence, a_session.lastCombatSequence)) {
			Reject();
			stats.combatHitsRejected++;
			return {};
		}
		if (!CombatWindowAllows(a_session, a_monotonicSeconds)) {
			Reject();
			stats.combatHitsRejected++;
			return {};
		}
		std::uint32_t actionType = 0;
		CombatActionState* combatAction = nullptr;
		if (!CombatActionAllows(a_session, *normalized, a_monotonicSeconds, actionType, combatAction) || !combatAction) {
			Reject();
			stats.combatHitsRejected++;
			return {};
		}
		const auto claimedDamage = static_cast<float>(ReadDouble(*normalized, "damage").value_or(0.0));
		const auto boundedDamage = CombatChecks::ClampCombatDamageClaim(claimedDamage, CombatChecks::CapForAction(actionType));
		if (!(boundedDamage > 0.0F)) {
			Reject();
			stats.combatHitsRejected++;
			return {};
		}
		if (boundedDamage < claimedDamage) {
			stats.combatDamageClaimsClamped++;
		}
		(*normalized)["damage"] = boundedDamage;
		const auto targetPlayerId = ReadUInt32(*normalized, "targetPlayerId").value_or(0);
		if (targetPlayerId == a_session.playerId) {
			Reject();
			stats.combatHitsRejected++;
			return {};
		}
		if (!factionFriendlyFire && SameFactionAsPlayer(a_session.profileId, targetPlayerId)) {
			Reject();
			stats.combatHitsRejected++;
			return {};
		}
		std::uint32_t targetConnectionId = 0;
		Session* target = nullptr;
		for (auto& [connectionId, candidate] : sessions) {
			if (candidate.active && candidate.playerId == targetPlayerId) {
				targetConnectionId = connectionId;
				target = &candidate;
				break;
			}
		}
		if (!target) {
			Reject();
			stats.combatHitsRejected++;
			return {};
		}
		if (!CombatGeometryAllows(a_session, *target, actionType, a_monotonicSeconds)) {
			Reject();
			stats.combatHitsRejected++;
			return {};
		}
		if (claimedDamage > kPvpDamageStrikeFactor * CombatChecks::CapForAction(actionType) && a_session.pvpOverCapClaims.Observe(a_monotonicSeconds)) {
			violations.push_back(CommonwealthOnline::Server::PendingViolation{ a_connectionId, CommonwealthOnline::Server::Violation::PvpDamage, "Combat damage claim above the per-action cap." });
		}
		a_session.lastCombatSequence = sequence;
		RecordCombatHit(a_session, *combatAction, a_monotonicSeconds);
		RecordAcceptedHit(
			a_session,
			AcceptedHitRecord{
				0.0,
				sequence,
				ReadUInt32(*normalized, "attackSequence").value_or(0),
				targetPlayerId,
				std::string{},
				ReadString(*normalized, "cellId").value_or(""),
				ReadString(*normalized, "worldspaceId").value_or(""),
				false
			},
			a_monotonicSeconds);
		normalized->erase("attackSequence");
		(*normalized)["playerId"] = a_session.playerId;
		(*normalized)["serverTime"] = a_unixTimeSeconds;
		stats.combatHitsReceived++;
		stats.combatHitsRouted++;
		return { ReliableFrame(targetConnectionId, *normalized) };
	}

	bool ProtocolHost::Impl::TakePlayerActivity(std::uint32_t a_connectionId)
	{
		const auto found = sessions.find(a_connectionId);
		if (found == sessions.end()) {
			return false;
		}
		const bool pending = found->second.playerActivityPending;
		found->second.playerActivityPending = false;
		return pending;
	}

	Afk::Exemption ProtocolHost::Impl::SessionAfkExemption(std::uint32_t a_connectionId) const noexcept
	{
		const auto found = sessions.find(a_connectionId);
		return found != sessions.end() ? found->second.afkExemption : Afk::Exemption{};
	}

	bool ProtocolHost::TakePlayerActivity(std::uint32_t a_connectionId)
	{
		return _impl->TakePlayerActivity(a_connectionId);
	}

	Afk::Exemption ProtocolHost::SessionAfkExemption(std::uint32_t a_connectionId) const noexcept
	{
		return _impl->SessionAfkExemption(a_connectionId);
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::ExistingFactionFrames(std::uint32_t a_connectionId, const Session& a_target, double a_unixTimeSeconds)
	{
		if (!factionService.Enabled()) return {};
		return factionService.ExistingFrames(FactionPeers(), a_connectionId, a_target.profileId, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::FactionNameFrames(std::uint32_t a_connectionId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		if (!factionService.Enabled()) return {};
		const auto found = sessions.find(a_connectionId);
		if (found == sessions.end() || !found->second.active) return {};
		if (!factionService.RosterNameChanged(SessionNameView(found->second), found->second.faction, a_monotonicSeconds)) return {};
		return factionService.RosterFrames(FactionPeers(), a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::FactionDisconnectFrames(const std::string& a_profileId, double a_unixTimeSeconds)
	{
		if (!factionService.Enabled()) return {};
		return factionService.DisconnectFrames(FactionPeers(), a_profileId, a_unixTimeSeconds);
	}
}
