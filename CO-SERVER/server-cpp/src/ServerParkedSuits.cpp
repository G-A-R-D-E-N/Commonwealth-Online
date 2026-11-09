#include "ServerProtocolImpl.h"

#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <random>
#include <set>

namespace CommonwealthOnline::Server
{
	void ProtocolHost::Impl::EnableParkedSuits(PersistenceGate* a_persistence)
	{
		parkedSuitPersistence = a_persistence;
		if (parkedSuitPersistence == nullptr) return;
		parkedSuits.LoadFromJson(parkedSuitPersistence->ReadStoredParkedSuits());
	}

	bool ProtocolHost::Impl::SaveParkedSuits()
	{
		if (parkedSuitPersistence == nullptr) return true;
		try {
			return parkedSuitPersistence->SaveParkedSuits(parkedSuits.ToJson(), true);
		} catch (const std::exception& error) {
			std::cerr << "Parked suits persistence unavailable: " << error.what() << '\n';
			return false;
		}
	}

	Json ProtocolHost::Impl::ParkPacketFor(const std::string& a_recipientKey, const F4T::ParkedSuit::Record& a_record) const
	{
		auto packet = F4T::ParkedSuit::BuildParkPacket(a_record);
		packet["mine"] = a_recipientKey == a_record.ownerKey;
		if (a_recipientKey == a_record.localOwnerKey) packet["localId"] = a_record.localId;
		return packet;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::ParkedSuitFramesFor(std::uint32_t a_connectionId, const Session& a_target,
		const std::optional<F4T::InterestScope::StateScope>& a_previous, double a_previousMarginUnits) const
	{
		std::vector<OutboundFrame> frames;
		if (!a_target.compatibilityCapabilities.Has(F4T::ParkedSuit::kCapabilityName)) return frames;
		const auto viewer = InterestScopeFromMovement(a_target.lastMovement);
		const auto recipientKey = PlayerKeyOf(a_target);
		for (const auto& record : parkedSuits.Entries()) {
			if (!ObjectStateVisible(viewer, record.position)) continue;
			if (a_previous && ObjectStateCoveredByPrevious(*a_previous, record.position, a_previousMarginUnits)) continue;
			frames.push_back(ReliableFrame(a_connectionId, ParkPacketFor(recipientKey, record)));
		}
		return frames;
	}

	bool ProtocolHost::Impl::ParkedSuitInReach(const Session& a_session, const F4T::ObjectWire::ObjectPosition& a_position, double a_monotonicSeconds) const
	{
		if (!a_session.lastMovement || !a_session.lastMovementMonotonic) return false;
		const auto age = a_monotonicSeconds - *a_session.lastMovementMonotonic;
		if (!(age >= 0.0) || age > kMaximumCombatPositionAgeSeconds) return false;
		if (!ObjectStateVisible(InterestScopeFromMovement(a_session.lastMovement), a_position)) return false;
		const auto& player = *a_session.lastMovement;
		const auto dx = player.x - a_position.x;
		const auto dy = player.y - a_position.y;
		const auto dz = player.z - a_position.z;
		const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
		return std::isfinite(distance) && distance <= kMaximumObjectReach;
	}

	bool ProtocolHost::Impl::ParkedSuitRecipientOnline(const std::string& a_playerKey) const
	{
		for (const auto& [connectionId, recipient] : sessions) {
			(void)connectionId;
			if (recipient.active && recipient.compatibilityCapabilities.Has(F4T::ParkedSuit::kCapabilityName) && PlayerKeyOf(recipient) == a_playerKey) return true;
		}
		return false;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::ParkedSuitReleaseFramesFor(std::uint32_t a_connectionId, const Session& a_target)
	{
		std::vector<OutboundFrame> frames;
		if (!a_target.compatibilityCapabilities.Has(F4T::ParkedSuit::kCapabilityName)) return frames;
		const auto snapshot = parkedSuits;
		const auto releases = parkedSuits.TakeReleases(PlayerKeyOf(a_target));
		if (releases.empty()) return frames;
		if (!SaveParkedSuits()) {
			parkedSuits = snapshot;
			return frames;
		}
		for (const auto& release : releases) {
			frames.push_back(ReliableFrame(a_connectionId, F4T::ParkedSuit::BuildReleasePacket(release.suitId, release.localId)));
		}
		return frames;
	}

	bool ProtocolHost::Impl::ParkedSuitChallengeLive(const std::optional<SessionTypes::ParkedSuitBreachChallenge>& a_challenge, std::string_view a_suitId, double a_monotonicSeconds) const
	{
		if (!a_challenge || a_challenge->suitId != a_suitId) return false;
		const auto age = a_monotonicSeconds - a_challenge->issuedAtMonotonic;
		return age >= 0.0 && age <= kParkedSuitChallengeLifetimeSeconds;
	}

	std::size_t ProtocolHost::Impl::ParkedSuitBreachLength(const std::string& a_ownerKey, const F4T::ObjectWire::ObjectPosition& a_position) const
	{
		for (const auto& [connectionId, owner] : sessions) {
			(void)connectionId;
			if (!owner.active || PlayerKeyOf(owner) != a_ownerKey) continue;
			if (!owner.lastMovement) return 3;
			const auto dx = owner.lastMovement->x - a_position.x;
			const auto dy = owner.lastMovement->y - a_position.y;
			const auto dz = owner.lastMovement->z - a_position.z;
			return std::sqrt(dx * dx + dy * dy + dz * dz) <= kParkedSuitNearbyOwnerUnits ? 4 : 3;
		}
		return kParkedSuitOfflineBreachLength;
	}

	void ProtocolHost::Impl::ArmParkedSuitPieceAudit(std::uint32_t a_connectionId, Session& a_session, const F4T::ParkedSuit::Record& a_record, double a_monotonicSeconds)
	{
		if (a_record.pieceKeys.empty() || inventoryAuthority == nullptr || !InventoryNegotiated(a_session)) return;
		try {
			const auto profile = inventoryAuthority->ProfileForConnection(a_connectionId);
			if (!profile || !inventoryAuthority->Store().GetInventory(*profile)) return;
			a_session.parkedSuitPieceAudit = SessionTypes::ParkedSuitPieceAudit{ *profile, a_record.suitId, a_record.pieceKeys, a_record.hacked, a_monotonicSeconds + kParkedSuitPieceAuditSeconds };
		} catch (const std::exception&) {
		}
	}

	void ProtocolHost::Impl::AuditParkedSuitPiecesOnSnapshot(std::uint32_t a_connectionId, Session& a_session, double a_monotonicSeconds)
	{
		auto& audit = a_session.parkedSuitPieceAudit;
		if (!audit || inventoryAuthority == nullptr) return;
		try {
			const auto profile = inventoryAuthority->ProfileForConnection(a_connectionId);
			if (!profile || *profile != audit->profileId) {
				audit.reset();
				return;
			}
			const auto inventory = inventoryAuthority->Store().GetInventory(*profile);
			if (!inventory) return;
			const auto result = F4T::ParkedSuit::EvaluatePieceAudit(audit->pieceKeys, *inventory, a_monotonicSeconds, audit->deadlineMonotonic);
			if (result.allPresent) {
				audit.reset();
				return;
			}
			if (!result.expired) return;
			++stats.parkedSuitPieceAuditMisses;
			std::cerr << "Parked suit " << audit->suitId << " pieces " << (audit->appearedAfterEntry ? "appeared after entry" : "held before entry") << " missing from the ledger:";
			for (const auto& key : result.missing) std::cerr << ' ' << key;
			std::cerr << '\n';
			audit.reset();
		} catch (const std::exception&) {
		}
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::ParkedSuitWriteRejected(std::uint32_t a_connectionId, const Json& a_packet, std::string_view a_reason)
	{
		Reject();
		return { ReliableFrame(a_connectionId, F4T::ParkedSuit::BuildAckPacket(a_packet, false, a_reason)) };
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::HandleParkedSuitWrite(
		std::uint32_t a_connectionId,
		Session& a_session,
		const Json& a_packet,
		double a_monotonicSeconds)
	{
		const auto ownerKey = PlayerKeyOf(a_session);
		if (const auto localId = F4T::ParkedSuit::NormalizeRemoveRequest(a_packet)) {
			const auto snapshot = parkedSuits;
			const auto removedSuitId = parkedSuits.Remove(ownerKey, *localId);
			if (!removedSuitId) {
				if (parkedSuits.FindByLocal(ownerKey, *localId)) return ParkedSuitWriteRejected(a_connectionId, a_packet);
				if (parkedSuits.ClearRelease(ownerKey, *localId) && !SaveParkedSuits()) {
					parkedSuits = snapshot;
					return ParkedSuitWriteRejected(a_connectionId, a_packet);
				}
				return { ReliableFrame(a_connectionId, F4T::ParkedSuit::BuildAckPacket(a_packet, true)) };
			}
			if (!SaveParkedSuits()) {
				parkedSuits = snapshot;
				return ParkedSuitWriteRejected(a_connectionId, a_packet);
			}
			if (const auto* removed = snapshot.FindByLocal(ownerKey, *localId)) ArmParkedSuitPieceAudit(a_connectionId, a_session, *removed, a_monotonicSeconds);
			const auto removal = F4T::ParkedSuit::BuildRemovePacket(ownerKey, *removedSuitId);
			std::vector<OutboundFrame> frames;
			for (const auto& [connectionId, recipient] : sessions) {
				if (connectionId == a_connectionId || !recipient.active || !recipient.compatibilityCapabilities.Has(F4T::ParkedSuit::kCapabilityName)) continue;
				frames.push_back(ReliableFrame(connectionId, removal));
			}
			frames.push_back(ReliableFrame(a_connectionId, F4T::ParkedSuit::BuildAckPacket(a_packet, true)));
			return frames;
		}

		if (const auto adopt = F4T::ParkedSuit::NormalizeAdoptRequest(a_packet)) {
			const auto snapshot = parkedSuits;
			std::string previousLocalOwnerKey;
			if (parkedSuits.Adopt(adopt->suitId, ownerKey, adopt->localId, &previousLocalOwnerKey) != F4T::ParkedSuit::AdoptOutcome::Adopted) {
				return ParkedSuitWriteRejected(a_connectionId, a_packet);
			}
			const auto releases = ParkedSuitRecipientOnline(previousLocalOwnerKey) ? parkedSuits.TakeReleases(previousLocalOwnerKey) : std::vector<F4T::ParkedSuit::PendingRelease>{};
			if (!SaveParkedSuits()) {
				parkedSuits = snapshot;
				return ParkedSuitWriteRejected(a_connectionId, a_packet);
			}
			const auto* stored = parkedSuits.Find(adopt->suitId);
			if (!stored) return ParkedSuitWriteRejected(a_connectionId, a_packet);
			ArmParkedSuitPieceAudit(a_connectionId, a_session, *stored, a_monotonicSeconds);
			std::vector<OutboundFrame> frames;
			frames.push_back(ReliableFrame(a_connectionId, F4T::ParkedSuit::BuildAckPacket(a_packet, true)));
			frames.push_back(ReliableFrame(a_connectionId, ParkPacketFor(ownerKey, *stored)));
			for (const auto& release : releases) {
				for (const auto& [connectionId, recipient] : sessions) {
					if (!recipient.active || !recipient.compatibilityCapabilities.Has(F4T::ParkedSuit::kCapabilityName)) continue;
					if (PlayerKeyOf(recipient) != release.playerKey) continue;
					frames.push_back(ReliableFrame(connectionId, F4T::ParkedSuit::BuildReleasePacket(release.suitId, release.localId)));
				}
			}
			return frames;
		}

		const auto park = F4T::ParkedSuit::NormalizeParkRequest(a_packet);
		if (!park) {
			return ParkedSuitWriteRejected(a_connectionId, a_packet, "invalidParkRequest");
		}
		if (!ParkedSuitInReach(a_session, park->position, a_monotonicSeconds)) {
			return ParkedSuitWriteRejected(a_connectionId, a_packet, "outOfReach");
		}
		const bool consoleSpawn = park->console && IsLiveAdminSession(a_session);
		if (!consoleSpawn && !ParkedSuitFrameObserved(ownerKey, park->powerArmor, a_monotonicSeconds)) {
			return ParkedSuitWriteRejected(a_connectionId, a_packet, "notInFrame");
		}
		const F4T::ParkedSuit::Record record{ ownerKey, "", park->localId, ownerKey, park->powerArmor, park->position, park->angleZ, park->pieceKeys };
		const auto snapshot = parkedSuits;
		auto outcome = parkedSuits.Park(record);
		std::optional<F4T::ParkedSuit::Record> capEvicted;
		if (outcome == F4T::ParkedSuit::ParkOutcome::TableFull) {
			capEvicted = parkedSuits.EvictOldestOffline(ParkedSuitOnlineOwners());
			if (capEvicted) outcome = parkedSuits.Park(record);
		}
		if (outcome == F4T::ParkedSuit::ParkOutcome::OwnerLimit || outcome == F4T::ParkedSuit::ParkOutcome::TableFull || outcome == F4T::ParkedSuit::ParkOutcome::NotOwner) {
			parkedSuits = snapshot;
			return ParkedSuitWriteRejected(a_connectionId, a_packet, "parkRefused");
		}
		if (!SaveParkedSuits()) {
			parkedSuits = snapshot;
			return ParkedSuitWriteRejected(a_connectionId, a_packet);
		}
		if (capEvicted) LogParkedSuitEviction(*capEvicted, "table full");
		const auto* stored = parkedSuits.FindByLocal(ownerKey, park->localId);
		if (!stored) return ParkedSuitWriteRejected(a_connectionId, a_packet);
		std::vector<OutboundFrame> frames;
		if (capEvicted) frames = ParkedSuitEvictionFrames(*capEvicted);
		frames.push_back(ReliableFrame(a_connectionId, ParkPacketFor(ownerKey, *stored)));
		for (const auto& [connectionId, recipient] : sessions) {
			if (connectionId == a_connectionId || !recipient.active || !recipient.compatibilityCapabilities.Has(F4T::ParkedSuit::kCapabilityName)) continue;
			if (!ObjectStateVisible(InterestScopeFromMovement(recipient.lastMovement), park->position)) continue;
			frames.push_back(ReliableFrame(connectionId, ParkPacketFor(PlayerKeyOf(recipient), *stored)));
		}
		frames.push_back(ReliableFrame(a_connectionId, F4T::ParkedSuit::BuildAckPacket(a_packet, true)));
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::HandleParkedSuitBreach(
		std::uint32_t a_connectionId,
		Session& a_session,
		const Json& a_packet,
		double a_monotonicSeconds)
	{
		const auto ownerKey = PlayerKeyOf(a_session);
		if (const auto breachSuitId = F4T::ParkedSuit::NormalizeBreachStartRequest(a_packet)) {
			const auto* stored = parkedSuits.Find(*breachSuitId);
			if (!stored || stored->ownerKey == ownerKey || !ParkedSuitInReach(a_session, stored->position, a_monotonicSeconds) ||
				ParkedSuitBreachCooling(ownerKey, *breachSuitId, a_monotonicSeconds)) {
				Reject();
				return {};
			}
			const auto length = ParkedSuitBreachLength(stored->ownerKey, stored->position);
			std::random_device entropy;
			std::uniform_int_distribution<std::uint32_t> seeds{ 1U, std::numeric_limits<std::uint32_t>::max() };
			const auto seed = seeds(entropy);
			a_session.parkedSuitChallenge = SessionTypes::ParkedSuitBreachChallenge{ *breachSuitId, seed, length, a_monotonicSeconds };
			return { ReliableFrame(a_connectionId, F4T::ParkedSuit::BuildBreachChallengePacket(*breachSuitId, seed, length)) };
		}

		if (const auto failedHack = F4T::ParkedSuit::NormalizeBreachFailedRequest(a_packet)) {
			const auto challenge = a_session.parkedSuitChallenge;
			a_session.parkedSuitChallenge.reset();
			if (!challenge || challenge->suitId != *failedHack) {
				Reject();
				return {};
			}
			const auto* failedRecord = parkedSuits.Find(*failedHack);
			if (!failedRecord || failedRecord->ownerKey == ownerKey || !ParkedSuitInReach(a_session, failedRecord->position, a_monotonicSeconds)) {
				Reject();
				return {};
			}
			StartParkedSuitBreachCooldown(ownerKey, *failedHack, a_monotonicSeconds);
			const auto failedAlert = F4T::ParkedSuit::BuildHackAlertPacket(*failedHack, true);
			std::vector<OutboundFrame> failedFrames;
			for (const auto& [ownerConnectionId, ownerSession] : sessions) {
				if (!ownerSession.active || !ownerSession.compatibilityCapabilities.Has(F4T::ParkedSuit::kCapabilityName)) continue;
				if (PlayerKeyOf(ownerSession) != failedRecord->ownerKey) continue;
				failedFrames.push_back(ReliableFrame(ownerConnectionId, failedAlert));
			}
			return failedFrames;
		}

		if (const auto hack = F4T::ParkedSuit::NormalizeHackRequest(a_packet)) {
			const auto challenge = a_session.parkedSuitChallenge;
			a_session.parkedSuitChallenge.reset();
			if (!ParkedSuitChallengeLive(challenge, hack->suitId, a_monotonicSeconds)) {
				Reject();
				return {};
			}
			const auto* stored = parkedSuits.Find(hack->suitId);
			if (stored && stored->ownerKey == ownerKey) {
				return { ReliableFrame(a_connectionId, F4T::ParkedSuit::BuildHackResultPacket(hack->suitId, false)) };
			}
			if (!stored || !ParkedSuitInReach(a_session, stored->position, a_monotonicSeconds)) {
				Reject();
				return {};
			}
			const auto grid = F4T::ParkedSuit::Breach::Generate(challenge->seed, challenge->length);
			if (!F4T::ParkedSuit::Breach::Solves(grid, challenge->length, hack->picks)) {
				StartParkedSuitBreachCooldown(ownerKey, hack->suitId, a_monotonicSeconds);
				Reject();
				return {};
			}
			const auto suitPosition = stored->position;
			const auto previousOwnerKey = stored->ownerKey;
			const auto snapshot = parkedSuits;
			if (parkedSuits.Reassign(hack->suitId, ownerKey) != F4T::ParkedSuit::HackOutcome::Hacked) {
				Reject();
				return {};
			}
			if (!SaveParkedSuits()) {
				parkedSuits = snapshot;
				Reject();
				return {};
			}
			std::vector<OutboundFrame> frames;
			frames.push_back(ReliableFrame(a_connectionId, F4T::ParkedSuit::BuildHackResultPacket(hack->suitId, true)));
			const auto* reassignedRecord = parkedSuits.Find(hack->suitId);
			if (!reassignedRecord) return frames;
			for (const auto& [connectionId, recipient] : sessions) {
				if (!recipient.active || !recipient.compatibilityCapabilities.Has(F4T::ParkedSuit::kCapabilityName)) continue;
				if (!ObjectStateVisible(InterestScopeFromMovement(recipient.lastMovement), suitPosition)) continue;
				frames.push_back(ReliableFrame(connectionId, ParkPacketFor(PlayerKeyOf(recipient), *reassignedRecord)));
			}
			const auto alert = F4T::ParkedSuit::BuildHackAlertPacket(hack->suitId, false);
			for (const auto& [ownerConnectionId, ownerSession] : sessions) {
				if (ownerConnectionId == a_connectionId || !ownerSession.active) continue;
				if (!ownerSession.compatibilityCapabilities.Has(F4T::ParkedSuit::kCapabilityName)) continue;
				if (PlayerKeyOf(ownerSession) != previousOwnerKey) continue;
				frames.push_back(ReliableFrame(ownerConnectionId, alert));
			}
			return frames;
		}

		Reject();
		return {};
	}

	void ProtocolHost::Impl::ObserveParkedSuitFrame(Session& a_session, const Json& a_packet, double a_monotonicSeconds)
	{
		const auto found = a_packet.find("powerArmor");
		if (found == a_packet.end() || !found->is_object()) return;
		const auto inFrame = found->find("inFrame");
		const auto frame = found->find("frameBaseFormId");
		if (inFrame == found->end() || !inFrame->is_boolean() || !inFrame->get<bool>()) return;
		if (frame == found->end() || !frame->is_string() || frame->get<std::string>().empty()) return;
		const auto frameBaseFormId = NormalizeFormId(frame->get<std::string>());
		if (frameBaseFormId.empty()) return;
		const auto owner = PlayerKeyOf(a_session);
		const auto existing = parkedSuitFrameObservations.find(owner);
		if (existing != parkedSuitFrameObservations.end()) {
			const auto age = a_monotonicSeconds - existing->second.observedMonotonic;
			if (existing->second.frameBaseFormId == frameBaseFormId && age >= 0.0 && age < kParkedSuitFrameObservationThrottleSeconds) return;
		} else if (a_monotonicSeconds >= nextParkedSuitFrameObservationPurgeMonotonic) {
			std::erase_if(parkedSuitFrameObservations, [&](const auto& a_entry) { return a_monotonicSeconds - a_entry.second.observedMonotonic > kParkedSuitFrameObservationSeconds; });
			nextParkedSuitFrameObservationPurgeMonotonic = a_monotonicSeconds + kParkedSuitFrameObservationPurgeSeconds;
		}
		parkedSuitFrameObservations[owner] = SessionTypes::ParkedSuitFrameObservation{ frameBaseFormId, a_monotonicSeconds };
	}

	bool ProtocolHost::Impl::ParkedSuitFrameObserved(const std::string& a_ownerKey, const Json& a_powerArmor, double a_monotonicSeconds) const
	{
		const auto found = parkedSuitFrameObservations.find(a_ownerKey);
		if (found == parkedSuitFrameObservations.end()) return false;
		const auto age = a_monotonicSeconds - found->second.observedMonotonic;
		return age >= 0.0 && age <= kParkedSuitFrameObservationSeconds &&
			NormalizeFormId(a_powerArmor.value("frameBaseFormId", std::string{})) == found->second.frameBaseFormId;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::ParkedSuitEvictionFrames(const F4T::ParkedSuit::Record& a_record) const
	{
		std::vector<OutboundFrame> frames;
		const auto removal = F4T::ParkedSuit::BuildRemovePacket(a_record.ownerKey, a_record.suitId);
		for (const auto& [connectionId, recipient] : sessions) {
			if (!recipient.active || !recipient.compatibilityCapabilities.Has(F4T::ParkedSuit::kCapabilityName)) continue;
			frames.push_back(ReliableFrame(connectionId, removal));
		}
		return frames;
	}

	std::set<std::string> ProtocolHost::Impl::ParkedSuitOnlineOwners() const
	{
		std::set<std::string> online;
		for (const auto& [connectionId, session] : sessions) {
			(void)connectionId;
			if (session.active) online.insert(PlayerKeyOf(session));
		}
		return online;
	}

	bool ProtocolHost::Impl::ParkedSuitBreachCooling(const std::string& a_hackerKey, const std::string& a_suitId, double a_monotonicSeconds) const
	{
		const auto found = parkedSuitBreachCooldowns.find({ a_hackerKey, a_suitId });
		return found != parkedSuitBreachCooldowns.end() && a_monotonicSeconds < found->second;
	}

	void ProtocolHost::Impl::StartParkedSuitBreachCooldown(const std::string& a_hackerKey, const std::string& a_suitId, double a_monotonicSeconds)
	{
		std::erase_if(parkedSuitBreachCooldowns, [&](const auto& a_entry) { return a_monotonicSeconds >= a_entry.second; });
		if (parkedSuitBreachCooldowns.size() >= kMaximumParkedSuitBreachCooldowns) parkedSuitBreachCooldowns.erase(parkedSuitBreachCooldowns.begin());
		parkedSuitBreachCooldowns[{ a_hackerKey, a_suitId }] = a_monotonicSeconds + kParkedSuitBreachCooldownSeconds;
	}

	void ProtocolHost::Impl::LogParkedSuitEviction(const F4T::ParkedSuit::Record& a_record, std::string_view a_reason) const
	{
		std::cout << "Parked suit " << a_record.suitId << " evicted: " << a_reason << '\n';
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::SweepParkedSuits(double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		if (a_monotonicSeconds < nextParkedSuitSweepMonotonic) return {};
		nextParkedSuitSweepMonotonic = a_monotonicSeconds + kParkedSuitSweepIntervalSeconds;
		const auto snapshot = parkedSuits;
		const auto before = snapshot.ToJson();
		const auto evicted = parkedSuits.SweepOffline(a_unixTimeSeconds, kParkedSuitOfflineRetentionSeconds, ParkedSuitOnlineOwners());
		if (parkedSuits.ToJson() == before) return {};
		if (!SaveParkedSuits()) {
			parkedSuits = snapshot;
			return {};
		}
		std::vector<OutboundFrame> frames;
		for (const auto& record : evicted) {
			LogParkedSuitEviction(record, "owner offline past retention");
			auto removals = ParkedSuitEvictionFrames(record);
			frames.insert(frames.end(), removals.begin(), removals.end());
		}
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::HandleParkedSuitRequest(
		std::uint32_t a_connectionId,
		Session& a_session,
		const Json& a_packet,
		double a_monotonicSeconds)
	{
		const auto write = F4T::ParkedSuit::IsWriteRequest(a_packet);
		if (!a_session.compatibilityCapabilities.Has(F4T::ParkedSuit::kCapabilityName) ||
			!F4T::PacketRateLimit::TakeToken(a_session.parkedSuitRequests, a_monotonicSeconds, kParkedSuitRefillPerSecond, kParkedSuitBurst)) {
			if (write) return ParkedSuitWriteRejected(a_connectionId, a_packet);
			Reject();
			return {};
		}
		if (write) return HandleParkedSuitWrite(a_connectionId, a_session, a_packet, a_monotonicSeconds);
		return HandleParkedSuitBreach(a_connectionId, a_session, a_packet, a_monotonicSeconds);
	}
}
