#include "ServerProtocolImpl.h"

#include "PersistenceGate.h"

#include "F4TFurnitureSync.h"
#include "F4TPacketRateLimit.h"
#include "F4TSurvivalNeeds.h"
#include "ServerLogSanitizer.h"

#include <exception>
#include <format>
#include <fstream>
#include <iostream>
#include <map>

namespace CommonwealthOnline::Server
{
	namespace
	{
		constexpr std::size_t kMaximumNeedsOperationChars = 64;
		constexpr std::size_t kMaximumNeedsItemKeyChars = 160;
		constexpr std::size_t kMaximumLoggedNeedsItemKeyChars = 64;
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

	void ProtocolHost::Impl::LoadSurvivalItems(const std::filesystem::path& a_path)
	{
		survivalItems = F4T::SurvivalItems::Catalog{};
		std::ifstream input(a_path, std::ios::binary);
		if (!input) {
			std::cerr << "Survival items: " << F4T::Utf8Path::Encode(a_path) << " is missing; every survival consume is refused." << std::endl;
			return;
		}
		const auto parsed = Json::parse(input, nullptr, false);
		if (parsed.is_discarded() || !parsed.is_object() || !parsed.contains("items") || !parsed["items"].is_array()) {
			std::cerr << "Survival items: " << F4T::Utf8Path::Encode(a_path) << " is invalid; every survival consume is refused." << std::endl;
			return;
		}
		F4T::SurvivalItems::Catalog catalog;
		for (const auto& entry : parsed["items"]) {
			const auto key = entry.is_object() ? entry.find("itemKey") : entry.end();
			const auto kind = entry.is_object() ? entry.find("kind") : entry.end();
			if (key == entry.end() || kind == entry.end() || !key->is_string() || !kind->is_string() ||
				!catalog.Add(key->get<std::string>(), kind->get<std::string>())) {
				std::cerr << "Survival items: skipped an invalid entry in " << F4T::Utf8Path::Encode(a_path) << std::endl;
			}
		}
		survivalItems = std::move(catalog);
		std::cout << "Survival items: " << survivalItems.Size() << " loaded from " << F4T::Utf8Path::Encode(a_path) << std::endl;
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
		for (auto& [profile, seconds] : elapsedByProfile) {
			if (const auto retained = awakeRetainedByProfile.find(profile); retained != awakeRetainedByProfile.end()) seconds += retained->second;
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
			}, true);
		} catch (const std::exception&) {
		}
		if (saved) {
			for (const auto& profile : profiles) awakeRetainedByProfile.erase(profile);
			return;
		}
		for (const auto& [profile, seconds] : elapsedByProfile) awakeRetainedByProfile[profile] = seconds;
		std::cout << std::format("[survival] decay not saved profiles={}", profiles.size()) << std::endl;
	}

	bool ProtocolHost::Impl::ApplySleepCredit(const std::string& a_profileId)
	{
		const auto pending = sleepCreditByProfile.find(a_profileId);
		if (pending == sleepCreditByProfile.end()) return true;
		const auto credit = pending->second;
		bool saved = false;
		try {
			saved = inventoryAuthority->Store().MutateSurvivalNeeds({ a_profileId }, [credit](std::string_view, F4T::SurvivalNeeds::Record& a_record) {
				const auto next = F4T::SurvivalNeeds::Sleep(a_record.state, credit, F4T::SurvivalNeeds::kDefaultRates);
				if (!next) return false;
				a_record.state = *next;
				return true;
			}, true);
		} catch (const std::exception&) {
		}
		if (saved) sleepCreditByProfile.erase(pending);
		return saved;
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
		if (wasSleeping == sleeping) return frames;
		if (!sleeping) {
			CloseSleepInterval(a_connectionId, a_session, a_monotonicSeconds);
			return frames;
		}
		if (SurvivalNeedsActive()) {
			AccrueAwakeNeeds({ a_connectionId }, a_monotonicSeconds);
			a_session.sleepStartedMonotonic = a_monotonicSeconds;
		}
		return frames;
	}

	void ProtocolHost::Impl::CloseSleepInterval(std::uint32_t a_connectionId, Session& a_session, double a_monotonicSeconds)
	{
		const auto started = a_session.sleepStartedMonotonic;
		a_session.sleepStartedMonotonic.reset();
		a_session.needsAccruedMonotonic = a_monotonicSeconds;
		if (!started || inventoryAuthority == nullptr) return;
		const auto credit = F4T::SurvivalNeeds::SleepCredit(a_monotonicSeconds - *started);
		const auto profile = inventoryAuthority->ProfileForConnection(a_connectionId);
		if (!credit || !profile) return;
		std::cout << std::format("[survival] sleep credit player={} seconds={:.1f}", a_session.playerId, *credit) << std::endl;
		sleepCreditByProfile[*profile] += *credit;
		const bool saved = ApplySleepCredit(*profile);
		std::cout << std::format("[survival] sleep credit {} player={}", saved ? "saved" : "not saved", a_session.playerId) << std::endl;
	}

	void ProtocolHost::Impl::ObserveNeedsActivity(double a_monotonicSeconds)
	{
		const bool active = SurvivalNeedsActive();
		if (active == needsObservedActive) return;
		needsObservedActive = active;
		std::vector<std::uint32_t> awake;
		for (auto& [connectionId, session] : sessions) {
			if (!session.lastSleepPacket) {
				awake.push_back(connectionId);
			} else if (active) {
				session.sleepStartedMonotonic = a_monotonicSeconds;
			} else {
				CloseSleepInterval(connectionId, session, a_monotonicSeconds);
			}
		}
		if (!active && inventoryAuthority != nullptr) AccrueAwakeNeeds(awake, a_monotonicSeconds);
	}

	void ProtocolHost::Impl::SettleSurvivalNeeds(std::uint32_t a_connectionId, double a_monotonicSeconds)
	{
		const auto session = sessions.find(a_connectionId);
		if (session == sessions.end() || !SurvivalNeedsActive()) return;
		if (session->second.sleepStartedMonotonic) CloseSleepInterval(a_connectionId, session->second, a_monotonicSeconds);
		else AccrueAwakeNeeds({ a_connectionId }, a_monotonicSeconds);
	}

	void ProtocolHost::Impl::RecordSleepDelivery(const OutboundFrame& a_frame, bool a_sent)
	{
		if (a_sent || a_frame.delivery != Delivery::ReliableOrdered) return;
		const std::string_view text{ reinterpret_cast<const char*>(a_frame.payload.data()), a_frame.payload.size() };
		if (text.find("sleepUse") == std::string_view::npos) return;
		const auto session = sessions.find(a_frame.connectionId);
		if (session == sessions.end()) return;
		const auto packet = Json::parse(a_frame.payload.begin(), a_frame.payload.end(), nullptr, false);
		if (!packet.is_object()) return;
		const auto type = packet.find("type");
		const auto player = packet.find("playerId");
		if (type == packet.end() || !type->is_string() || type->get<std::string>() != "sleepUse" || player == packet.end() || !player->is_number_unsigned()) return;
		session->second.sleepResyncPlayers.insert(player->get<std::uint32_t>());
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::ResyncSleepRelays()
	{
		std::vector<OutboundFrame> frames;
		for (auto& [connectionId, recipient] : sessions) {
			if (recipient.sleepResyncPlayers.empty()) continue;
			const auto pending = recipient.sleepResyncPlayers;
			recipient.sleepResyncPlayers.clear();
			for (const auto subjectPlayerId : pending) {
				const Session* subject = nullptr;
				for (const auto& entry : sessions) {
					if (entry.second.playerId == subjectPlayerId) subject = &entry.second;
				}
				if (auto frame = FurnitureRelay::SleepResync(connectionId, recipient, subjectPlayerId, subject)) frames.push_back(std::move(*frame));
			}
		}
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
		if (!survivalItems.Accepts(*item, *kind)) {
			const auto shownItem = RuntimeSupport::SanitizeLogField(item->substr(0, kMaximumLoggedNeedsItemKeyChars));
			std::cout << std::format("[survival] consume refused player={} op={} kind={} item={} reason=item not listed", a_session.playerId,
				RuntimeSupport::SanitizeLogField(*operation), *kind, shownItem) << std::endl;
			reportService.Record("survivalNeeds", "Consume " + *operation + " refused: item " + shownItem + " is not a listed " + *kind, a_unixTimeSeconds);
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

	void ProtocolHost::Impl::RecordNeedsDelivery(const OutboundFrame& a_frame, bool a_sent)
	{
		if (!a_sent || a_frame.delivery != Delivery::ReliableOrdered) return;
		const std::string_view text{ reinterpret_cast<const char*>(a_frame.payload.data()), a_frame.payload.size() };
		if (text.find("needsUpdate") == std::string_view::npos) return;
		const auto session = sessions.find(a_frame.connectionId);
		if (session == sessions.end()) return;
		const auto packet = Json::parse(a_frame.payload.begin(), a_frame.payload.end(), nullptr, false);
		if (!packet.is_object()) return;
		const auto type = packet.find("type");
		if (type == packet.end() || !type->is_string() || type->get<std::string>() != "needsUpdate" || !packet.contains("revision") ||
			!packet["revision"].is_number_unsigned()) return;
		const auto revision = packet["revision"].get<std::uint64_t>();
		auto& sent = session->second.sentNeedsRevision;
		if (!sent || *sent < revision) sent = revision;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::PollSurvivalNeeds(double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		ObserveNeedsActivity(a_monotonicSeconds);
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
		std::vector<OutboundFrame> frames = ResyncSleepRelays();
		for (auto& [connectionId, session] : sessions) {
			const auto profile = SurvivalNeedsActive() && session.active ? inventoryAuthority->ProfileForConnection(connectionId) : std::nullopt;
			if (!profile) continue;
			if (sleepCreditByProfile.contains(*profile)) (void)ApplySleepCredit(*profile);
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
				}
			} catch (const std::exception&) {
			}
		}
		return frames;
	}
}
