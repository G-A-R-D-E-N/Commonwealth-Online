#include "ServerProtocolImpl.h"

#include "PersistenceGate.h"

#include "F4TFurnitureSync.h"
#include "F4TPacketRateLimit.h"
#include "F4TSurvivalNeeds.h"
#include "ServerLogSanitizer.h"

#include <exception>
#include <format>
#include <iostream>
#include <map>

namespace CommonwealthOnline::Server
{
	namespace
	{
		constexpr std::size_t kMaximumNeedsOperationChars = 64;
		constexpr std::size_t kMaximumNeedsItemKeyChars = 160;
		constexpr double kAwakeAccrualIntervalSeconds = 10.0;
		constexpr double kSurvivalPollIntervalSeconds = 1.0;
		constexpr double kSleepRefusedLogIntervalSeconds = 10.0;
		constexpr std::string_view kNeedsCapability = "survival-needs-v1";
	}

	bool ProtocolHost::Impl::SurvivalNeedsActive() const
	{
		return survivalNeeds && inventoryAuthority != nullptr;
	}

	void ProtocolHost::Impl::LogSurvivalWelcome(std::uint32_t a_playerId) const
	{
		std::cout << std::format("[survival] welcome player={} survivalMode={} survivalNeeds={}", a_playerId,
			survivalMode ? "true" : "false", SurvivalNeedsActive() ? "true" : "false") << std::endl;
	}

	void ProtocolHost::Impl::AccrueAwakeNeeds(const std::vector<std::uint32_t>& a_connectionIds, double a_monotonicSeconds)
	{
		std::map<std::string, double> elapsedByProfile;
		for (const auto connectionId : a_connectionIds) {
			const auto found = sessions.find(connectionId);
			if (found == sessions.end()) continue;
			const auto previous = found->second.needsAccruedMonotonic;
			found->second.needsAccruedMonotonic = a_monotonicSeconds;
			const auto profile = inventoryAuthority->ProfileForConnection(connectionId);
			if (previous && profile && a_monotonicSeconds > *previous) elapsedByProfile[*profile] = a_monotonicSeconds - *previous;
		}
		if (elapsedByProfile.empty()) return;
		std::vector<std::string> profiles;
		profiles.reserve(elapsedByProfile.size());
		for (const auto& entry : elapsedByProfile) profiles.push_back(entry.first);
		bool saved = false;
		try {
			saved = inventoryAuthority->Store().MutateSurvivalNeeds(profiles, [&elapsedByProfile](std::string_view a_profileId, F4T::SurvivalNeeds::Record& a_record) {
				const auto next = F4T::SurvivalNeeds::Advance(a_record.state, elapsedByProfile.at(std::string{ a_profileId }), F4T::SurvivalNeeds::kDefaultRates);
				if (!next) return false;
				a_record.state = *next;
				return true;
			}, false);
		} catch (const std::exception&) {
		}
		if (!saved) std::cout << std::format("[survival] decay not saved profiles={}", profiles.size()) << std::endl;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::SleepUseWithNeeds(std::uint32_t a_connectionId, Session& a_session,
		const Json& a_packet, double a_monotonicSeconds)
	{
		const bool wasSleeping = a_session.lastSleepPacket.has_value();
		const auto requested = F4T::FurnitureSync::ParseClientUse(a_packet);
		auto frames = CommonwealthOnline::Server::FurnitureRelay::HandleSleepUse(sessions, a_connectionId, a_session, a_packet, a_monotonicSeconds, stats);
		const bool sleeping = a_session.lastSleepPacket.has_value();
		if (!wasSleeping && sleeping) {
			std::cout << std::format("[survival] sleep accepted player={} needs={}", a_session.playerId, SurvivalNeedsActive() ? "true" : "false") << std::endl;
		} else if (wasSleeping && !sleeping) {
			std::cout << std::format("[survival] sleep ended player={}", a_session.playerId) << std::endl;
		} else if (requested == F4T::FurnitureSync::Action::Enter && !sleeping) {
			const auto lastLog = sleepRefusedLogMonotonic.find(a_session.playerId);
			if (lastLog == sleepRefusedLogMonotonic.end() || a_monotonicSeconds - lastLog->second >= kSleepRefusedLogIntervalSeconds) {
				sleepRefusedLogMonotonic[a_session.playerId] = a_monotonicSeconds;
				std::cout << std::format("[survival] sleep refused player={}", a_session.playerId) << std::endl;
			}
		}
		if (!SurvivalNeedsActive() || wasSleeping == sleeping) return frames;
		if (!sleeping) {
			const auto started = a_session.sleepStartedMonotonic;
			a_session.sleepStartedMonotonic.reset();
			a_session.needsAccruedMonotonic = a_monotonicSeconds;
			const auto credit = started ? F4T::SurvivalNeeds::SleepCredit(a_monotonicSeconds - *started) : std::nullopt;
			const auto profile = inventoryAuthority->ProfileForConnection(a_connectionId);
			if (!credit || !profile) return frames;
			std::cout << std::format("[survival] sleep credit player={} seconds={:.1f}", a_session.playerId, *credit) << std::endl;
			bool saved = false;
			try {
				saved = inventoryAuthority->Store().MutateSurvivalNeeds({ *profile }, [credit](std::string_view, F4T::SurvivalNeeds::Record& a_record) {
					const auto next = F4T::SurvivalNeeds::Sleep(a_record.state, *credit, F4T::SurvivalNeeds::kDefaultRates);
					if (!next) return false;
					a_record.state = *next;
					return true;
				}, true);
			} catch (const std::exception&) {
			}
			std::cout << std::format("[survival] sleep credit {} player={}", saved ? "saved" : "not saved", a_session.playerId) << std::endl;
			return frames;
		}
		AccrueAwakeNeeds({ a_connectionId }, a_monotonicSeconds);
		a_session.sleepStartedMonotonic = a_monotonicSeconds;
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::HandleNeedsConsume(std::uint32_t a_connectionId, Session& a_session,
		const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		if (!SurvivalNeedsActive()) return {};
		const auto operation = ReadString(a_packet, "operationId");
		const auto item = ReadString(a_packet, "itemFormId");
		const auto kind = ReadString(a_packet, "kind");
		if (a_packet.size() != 4 || !operation || operation->empty() || operation->size() > kMaximumNeedsOperationChars ||
			!item || item->empty() || item->size() > kMaximumNeedsItemKeyChars || !kind || !F4T::SurvivalNeeds::ReliefFor(*kind) ||
			!a_session.compatibilityCapabilities.Has(kNeedsCapability)) {
			Reject();
			return {};
		}
		const auto result = [&](std::string_view a_status) {
			return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, Json{
				{ "type", "needsConsumeResult" }, { "operationId", *operation }, { "status", std::string{ a_status } } }) };
		};
		const auto profile = inventoryAuthority->ProfileForConnection(a_connectionId);
		if (!profile) {
			Reject();
			return {};
		}
		if (!F4T::PacketRateLimit::TakeToken(a_session.needsConsumes, a_monotonicSeconds)) {
			reportService.Record("survivalNeeds", "Consume " + *operation + " refused: rate limited", a_unixTimeSeconds);
			return result("refused");
		}
		try {
			const auto admission = inventoryAuthority->Store().BeginSurvivalClaim(*profile, F4T::SurvivalNeeds::Claim{ *operation, *kind, *item, 0, 0.0 });
			switch (admission) {
			case F4T::SurvivalNeeds::Admission::Started:
			case F4T::SurvivalNeeds::Admission::Pending:
				std::cout << std::format("[survival] consume accepted player={} op={} kind={} admission={}", a_session.playerId,
					RuntimeSupport::SanitizeLogField(*operation), *kind,
					admission == F4T::SurvivalNeeds::Admission::Started ? "started" : "pending") << std::endl;
				return result("pending");
			case F4T::SurvivalNeeds::Admission::Replayed: {
				Json reply{ { "type", "needsConsumeResult" }, { "operationId", *operation }, { "status", "duplicate" } };
				if (const auto recorded = F4T::SurvivalNeeds::FindConsumed(inventoryAuthority->Store().GetSurvivalNeeds(*profile), *operation)) {
					reply["result"] = *recorded == F4T::SurvivalNeeds::Status::Applied ? "applied" : "refused";
				}
				return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, reply) };
			}
			case F4T::SurvivalNeeds::Admission::Busy:
				reportService.Record("survivalNeeds", "Consume " + *operation + " refused: too many consumes are still being settled", a_unixTimeSeconds);
				return result("refused");
			case F4T::SurvivalNeeds::Admission::Missing:
				reportService.Record("survivalNeeds", "Consume " + *operation + " refused: item not in inventory", a_unixTimeSeconds);
				return result("refused");
			case F4T::SurvivalNeeds::Admission::Unavailable:
				reportService.Record("survivalNeeds", "Consume " + *operation + " refused: persistence unavailable", a_unixTimeSeconds);
				return result("refused");
			}
		} catch (const std::exception&) {
			reportService.Record("survivalNeeds", "Consume " + *operation + " refused: persistence unavailable", a_unixTimeSeconds);
			return result("refused");
		}
		return {};
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::PollSurvivalNeeds(double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		if (a_monotonicSeconds < nextSurvivalPollMonotonic) return {};
		nextSurvivalPollMonotonic = a_monotonicSeconds + kSurvivalPollIntervalSeconds;
		std::vector<std::uint32_t> awake;
		bool accrualDue = false;
		for (auto& [connectionId, session] : sessions) {
			const auto profile = SurvivalNeedsActive() && session.active ? inventoryAuthority->ProfileForConnection(connectionId) : std::nullopt;
			if (!profile) {
				session.needsAccruedMonotonic.reset();
				continue;
			}
			if (!session.needsAccruedMonotonic) {
				session.needsAccruedMonotonic = a_monotonicSeconds;
			} else if (!session.sleepStartedMonotonic) {
				awake.push_back(connectionId);
				accrualDue = accrualDue || a_monotonicSeconds - *session.needsAccruedMonotonic >= kAwakeAccrualIntervalSeconds;
			}
		}
		if (accrualDue) AccrueAwakeNeeds(awake, a_monotonicSeconds);
		std::vector<OutboundFrame> frames;
		for (auto& [connectionId, session] : sessions) {
			const auto profile = SurvivalNeedsActive() && session.active ? inventoryAuthority->ProfileForConnection(connectionId) : std::nullopt;
			if (!profile) continue;
			const bool capable = session.compatibilityCapabilities.Has(kNeedsCapability);
			try {
				auto& store = inventoryAuthority->Store();
				if (!store.GetSurvivalNeeds(*profile).claims.empty()) {
					for (const auto& [operationId, status] : store.SettleSurvivalNeeds(*profile)) {
						const bool applied = status == F4T::SurvivalNeeds::Status::Applied;
						if (applied) {
							std::cout << std::format("[survival] consume applied player={} op={}", session.playerId, RuntimeSupport::SanitizeLogField(operationId)) << std::endl;
						} else {
							reportService.Record("survivalNeeds", "Consume " + operationId + " refused: no inventory loss before the deadline", a_unixTimeSeconds);
						}
						if (capable) {
							frames.push_back(ReliableFrame(connectionId, Json{
								{ "type", "needsConsumeResult" }, { "operationId", operationId }, { "status", applied ? "applied" : "refused" } }));
						}
					}
				}
				if (!capable) continue;
				const auto record = store.GetSurvivalNeeds(*profile);
				if (!session.sentNeedsRevision || *session.sentNeedsRevision != record.revision) {
					frames.push_back(ReliableFrame(connectionId, Json{
						{ "type", "needsUpdate" }, { "hunger", record.state.hunger }, { "thirst", record.state.thirst },
						{ "sleep", record.state.sleep }, { "revision", record.revision } }));
					session.sentNeedsRevision = record.revision;
				}
			} catch (const std::exception&) {
			}
		}
		return frames;
	}
}
