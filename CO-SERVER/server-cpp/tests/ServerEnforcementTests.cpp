#include "ServerEnforcement.h"

#include "BanStore.h"
#include "F4TGnsSnapshotEnvelope.h"
#include "ServerProtocol.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace CommonwealthOnline::Server;

namespace
{
	using F4T::GnsSnapshotEnvelope::Family;
	using Json = nlohmann::json;

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	std::vector<std::uint8_t> EncodeSnapshot(const Json& a_packet, Family a_family, std::uint32_t a_sequence)
	{
		const auto json = a_packet.dump();
		std::vector<std::uint8_t> output(F4T::GnsSnapshotEnvelope::HeaderSize + json.size());
		assert(F4T::GnsSnapshotEnvelope::WriteHeader(std::span<std::uint8_t>{ output }, a_family, a_sequence));
		std::copy(json.begin(), json.end(), output.begin() + static_cast<std::ptrdiff_t>(F4T::GnsSnapshotEnvelope::HeaderSize));
		return output;
	}

	Json DecodePacket(const OutboundFrame& a_frame)
	{
		F4T::GnsSnapshotEnvelope::Header header{};
		const auto decode = F4T::GnsSnapshotEnvelope::Decode(a_frame.payload, header);
		std::span<const std::uint8_t> payload{ a_frame.payload };
		if (decode == F4T::GnsSnapshotEnvelope::DecodeResult::Valid) {
			payload = payload.subspan(F4T::GnsSnapshotEnvelope::HeaderSize);
		} else {
			assert(decode == F4T::GnsSnapshotEnvelope::DecodeResult::NotEnvelope);
		}
		return Json::parse(std::string{
			reinterpret_cast<const char*>(payload.data()),
			payload.size()
		});
	}

	const OutboundFrame* FindFrame(
		const std::vector<OutboundFrame>& a_frames,
		std::uint32_t a_connectionId,
		std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && !frame.payload.empty() &&
				DecodePacket(frame).value("type", std::string{}) == a_type) {
				return &frame;
			}
		}
		return nullptr;
	}

	std::uint32_t ConnectPlayer(
		ProtocolHost& a_host,
		std::uint32_t a_connectionId,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		assert(welcome.size() == 1);
		const auto playerId = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		const auto ready = a_host.HandleMessage(
			a_connectionId,
			EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }),
			a_monotonicSeconds,
			a_unixTimeSeconds + 0.1);
		assert(FindFrame(ready, a_connectionId, "sessionReady") != nullptr);
		return playerId;
	}

	Json MoveTo(double a_x, const std::string& a_movementType)
	{
		return Json{
			{ "type", "transform" },
			{ "cell" "Id", "CAFE" },
			{ "worldspace" "Id", "F00D" },
			{ "x", a_x },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "movementType", a_movementType }
		};
	}

	Json RangedPlayerState(std::uint32_t a_actionSequence)
	{
		return Json{
			{ "type", "playerState" },
			{ "equippedItems", Json::array({ Json{ { "slot", "rightHand" }, { "form" "Id", "1234" } } }) },
			{ "actionEvents", Json::array({ Json{
				{ "sequence", a_actionSequence },
				{ "type", 3 },
				{ "eventName", "fireSingle" }
			} }) }
		};
	}

	Json Hit(std::uint32_t a_target, std::uint32_t a_sequence, double a_damage, std::uint32_t a_attackSequence)
	{
		return Json{
			{ "type", "combatHit" },
			{ "targetPlayerId", a_target },
			{ "sequence", a_sequence },
			{ "attackSequence", a_attackSequence },
			{ "damage", a_damage },
			{ "weaponForm" "Id", "1234" }
		};
	}

	std::uint32_t StageRangedDuel(ProtocolHost& a_host)
	{
		ConnectPlayer(a_host, 30, 1.0, 100.0);
		const auto victimId = ConnectPlayer(a_host, 40, 1.1, 100.2);
		a_host.HandleMessage(30, EncodeSnapshot(MoveTo(0.0, "normal"), Family::Transform, 1), 2.0, 101.0);
		a_host.HandleMessage(40, EncodeSnapshot(MoveTo(100.0, "normal"), Family::Transform, 1), 2.1, 101.1);
		a_host.HandleMessage(30, EncodeJson(RangedPlayerState(10)), 2.2, 101.2);
		return victimId;
	}

	void RunDetectionTests(const std::filesystem::path& a_scratch)
	{
		ViolationWindow window;
		for (const double second : { 0.0, 1.0, 2.0, 3.0 }) {
			assert(!window.Observe(second));
		}
		assert(!window.Observe(40.0));
		for (const double second : { 41.0, 42.0, 43.0 }) {
			assert(!window.Observe(second));
		}
		assert(window.Observe(44.0));
		for (const double second : { 45.0, 46.0, 47.0, 48.0 }) {
			assert(!window.Observe(second));
		}
		assert(window.Observe(49.0));

		{
			ProtocolHost host{ "Teleport Glitch" };
			ConnectPlayer(host, 20, 1.0, 100.0);
			host.HandleMessage(20, EncodeSnapshot(MoveTo(0.0, "normal"), Family::Transform, 1), 2.0, 101.0);
			host.HandleMessage(20, EncodeSnapshot(MoveTo(100000.0, "normal"), Family::Transform, 2), 2.1, 101.1);
			assert(host.Stats().movementRejected == 1);
			assert(host.TakeViolations().empty());
		}
		{
			ProtocolHost host{ "Fast Travel" };
			ConnectPlayer(host, 20, 1.0, 100.0);
			host.HandleMessage(20, EncodeSnapshot(MoveTo(0.0, "normal"), Family::Transform, 1), 2.0, 101.0);
			host.HandleMessage(20, EncodeSnapshot(MoveTo(100000.0, "fast_travel"), Family::Transform, 2), 2.1, 101.1);
			assert(host.Stats().movementRejected == 0);
			assert(host.TakeViolations().empty());
		}

		std::vector<std::pair<std::uint32_t, std::string>> ended;
		BanStore bans(a_scratch / "detection-bans.json");
		ServerEnforcement enforcement(
			&bans,
			[](std::uint32_t a_connectionId) {
				ViolationSubject subject;
				subject.connected = true;
				subject.address = a_connectionId == 30 || a_connectionId == 31 ? "10.0.0.51" : "10.0.0.50";
				subject.steamId = a_connectionId == 20 ? "76561198000000020" : a_connectionId == 30 ? "76561198000000030" : "";
				return subject;
			},
			[&](std::uint32_t a_connectionId, std::string_view a_code, std::string_view a_reason) {
				ended.emplace_back(a_connectionId, std::string{ a_code } + "|" + std::string{ a_reason });
			},
			"");

		{
			ProtocolHost host{ "Teleport Burst" };
			ConnectPlayer(host, 20, 1.0, 100.0);
			host.HandleMessage(20, EncodeSnapshot(MoveTo(0.0, "normal"), Family::Transform, 1), 2.0, 101.0);
			for (std::uint32_t hit = 1; hit <= 15; ++hit) {
				host.HandleMessage(20, EncodeSnapshot(MoveTo(100000.0, "normal"), Family::Transform, hit + 1), 2.0 + 0.1 * hit, 101.0 + 0.1 * hit);
				enforcement.ReportViolations(host.TakeViolations());
				if (hit == 7) {
					assert(ended.empty());
				}
				if (hit == 10) {
					assert(ended.size() == 1);
				}
			}
			assert(ended.size() == 2);
			assert(ended[0].second == "teleport_distance|Kicked: Moved farther than the server allows.");
			assert(ended[1].second == "banned|Banned: Moved farther than the server allows. Temporary ban: 60 minutes.");
			assert(bans.GetSteamId("76561198000000020").has_value());
		}
		ended.clear();
		{
			ProtocolHost host{ "PvP Repeats" };
			const auto victimId = StageRangedDuel(host);
			for (std::uint32_t hit = 1; hit <= 15; ++hit) {
				host.HandleMessage(30, EncodeJson(Hit(victimId, hit, 2500.0, 10)), 2.2 + 0.01 * hit, 101.2 + 0.01 * hit);
				enforcement.ReportViolations(host.TakeViolations());
				if (hit == 7) {
					assert(ended.empty());
				}
				if (hit == 10) {
					assert(ended.size() == 1);
				}
			}
			assert(ended.size() == 2);
			assert(ended[0].second == "pvp_damage|Kicked: Dealt damage that the server does not allow.");
			assert(ended[1].second == "banned|Banned: Dealt damage that the server does not allow. Temporary ban: 60 minutes.");
			assert(bans.GetSteamId("76561198000000030").has_value());
		}
		{
			ProtocolHost host{ "PvP Single Claim" };
			const auto victimId = StageRangedDuel(host);
			host.HandleMessage(30, EncodeJson(Hit(victimId, 1, 2500.0, 10)), 2.21, 101.21);
			assert(host.Stats().combatHitsRouted == 1);
			assert(host.TakeViolations().empty());
		}
		{
			ProtocolHost host{ "PvP Mid Margin" };
			const auto victimId = StageRangedDuel(host);
			for (std::uint32_t hit = 1; hit <= 10; ++hit) {
				host.HandleMessage(30, EncodeJson(Hit(victimId, hit, 1500.0, 10)), 2.2 + 0.01 * hit, 101.2 + 0.01 * hit);
			}
			assert(host.Stats().combatHitsRouted == 10);
			assert(host.Stats().combatDamageClaimsClamped == 10);
			assert(host.TakeViolations().empty());
		}
		ended.clear();
		{
			ProtocolHost host{ "Teleport Shared Address" };
			ConnectPlayer(host, 31, 1.0, 100.0);
			host.HandleMessage(31, EncodeSnapshot(MoveTo(0.0, "normal"), Family::Transform, 1), 2.0, 101.0);
			for (std::uint32_t hit = 1; hit <= 15; ++hit) {
				host.HandleMessage(31, EncodeSnapshot(MoveTo(100000.0, "normal"), Family::Transform, hit + 1), 2.0 + 0.1 * hit, 101.0 + 0.1 * hit);
				enforcement.ReportViolations(host.TakeViolations());
			}
			assert(ended.size() == 2);
			assert(ended[0].second == "teleport_distance|Kicked: Moved farther than the server allows.");
			assert(ended[1].second == "teleport_distance|Kicked: Moved farther than the server allows.");
			assert(!bans.Get("10.0.0.51").has_value());
		}
	}
}

void InboundBudgetWarnsBeforeTheKick()
{
	std::vector<std::pair<std::uint32_t, std::string>> notices;
	std::vector<std::pair<std::uint32_t, std::string>> ended;
	ServerEnforcement enforcement(
		nullptr,
		[](std::uint32_t) {
			ViolationSubject subject;
			subject.connected = true;
			subject.address = "10.0.0.70";
			return subject;
		},
		[&](std::uint32_t a_connectionId, std::string_view a_code, std::string_view a_reason) {
			ended.emplace_back(a_connectionId, std::string{ a_code } + "|" + std::string{ a_reason });
		},
		"",
		[&](std::uint32_t a_connectionId, std::string_view a_text) {
			notices.emplace_back(a_connectionId, std::string{ a_text });
		});

	enforcement.ReportViolation(70, Violation::InboundBudget, "Sustained byte-rate limit exceeded.");
	assert(notices.size() == 1 && notices.front().first == 70);
	assert(notices.front().second.rfind("Warning:", 0) == 0);
	assert(ended.empty());

	enforcement.ReportViolation(70, Violation::InboundBudget, "second");
	assert(ended.size() == 1 && ended.front().second.rfind("inbound_budget|", 0) == 0);
	assert(notices.size() == 1);

	enforcement.ReportViolation(71, Violation::TeleportDistance, "first");
	assert(notices.size() == 1);
}

int main()
{
	InboundBudgetWarnsBeforeTheKick();
	assert(ConsequenceFor(Violation::RateLimited, 1).action == Action::Kick);
	assert(ConsequenceFor(Violation::RateLimited, 2).action == Action::TempBan);
	assert(ConsequenceFor(Violation::RateLimited, 2).seconds == 600);
	assert(ConsequenceFor(Violation::RateLimited, 9).seconds == 3600);
	assert(ConsequenceFor(Violation::RateLimited, 0).action == Action::Kick);
	assert(ConsequenceFor(Violation::MalformedPayload, 3).action == Action::PermaBan);
	assert(ConsequenceFor(Violation::TeleportDistance, 1).action == Action::Warn);
	assert(ConsequenceFor(Violation::InboundBudget, 3).seconds == 600);
	assert(ConsequenceFor(Violation::DifficultyUnreported, 1).action == Action::Kick);
	assert(ConsequenceFor(Violation::DifficultyUnreported, 3).action == Action::Kick);

	assert(WithAppealLink("Kicked: x", "") == "Kicked: x");
	assert(WithAppealLink("Kicked: x", "https://example.test/appeal") == "Kicked: x Appeal: https://example.test/appeal");
	assert(WithAppealLink("Kicked: x", "https://example.test/appeal", 12) == "Kicked: x");

	StrikeLedger ledger;
	assert(ledger.Record("10.0.0.1", Violation::RateLimited, 0.0) == 1);
	assert(ledger.Record("10.0.0.1", Violation::RateLimited, 10.0) == 2);
	assert(ledger.Record("10.0.0.1", Violation::PvpDamage, 10.0) == 1);
	assert(ledger.Record("10.0.0.1", Violation::RateLimited, 3610.0) == 2);
	assert(ledger.Record("10.0.0.1", Violation::RateLimited, 100000.0) == 1);
	assert(ledger.Record("10.0.0.2", Violation::RateLimited, 0.0) == 1);
	assert(ledger.Record("10.0.0.2", Violation::RateLimited, 3599.0) == 2);
	assert(ledger.Record("10.0.0.2", Violation::RateLimited, 7198.0) == 2);
	assert(ledger.Record("10.0.0.2", Violation::RateLimited, 10797.0) == 2);
	assert(ledger.Record("10.0.0.2", Violation::RateLimited, 14400.0) == 1);
	assert(ledger.Record("10.0.0.3", Violation::RateLimited, 0.0) == 1);
	assert(ledger.Record("10.0.0.3", Violation::RateLimited, 1800.0) == 2);
	assert(ledger.Record("10.0.0.3", Violation::RateLimited, 3600.0) == 2);
	assert(ledger.Record("10.0.0.3", Violation::RateLimited, 5400.0) == 3);

	const auto scratch = std::filesystem::temp_directory_path() / "co-server-enforcement-tests";
	std::filesystem::remove_all(scratch);
	BanStore bans(scratch / "bans.json");
	std::vector<std::pair<std::uint32_t, std::string>> ended;
	ServerEnforcement enforcement(
		&bans,
		[](std::uint32_t a_connectionId) {
			ViolationSubject subject;
			subject.connected = a_connectionId != 9;
			switch (a_connectionId) {
			case 1:
				subject.address = "10.0.0.7";
				break;
			case 2:
				subject.address = "10.0.0.8";
				subject.steamId = "76561198000000888";
				break;
			case 3:
				subject.address = "10.0.0.9";
				subject.hostSelf = true;
				break;
			case 4:
				subject.address = "10.0.0.10";
				subject.adminOnAddress = true;
				break;
			case 6:
				subject.address = "10.0.0.11";
				break;
			case 12:
				subject.address = "10.0.0.60";
				break;
			default:
				break;
			}
			return subject;
		},
		[&](std::uint32_t a_connectionId, std::string_view a_code, std::string_view a_reason) {
			ended.emplace_back(a_connectionId, std::string{ a_code } + "|" + std::string{ a_reason });
		},
		"https://example.test/appeal");
	const std::string appeal = " Appeal: https://example.test/appeal";

	enforcement.ReportViolation(1, Violation::RateLimited, "first");
	assert(ended.back().second == "rate_limited|Kicked: Sent messages too fast." + appeal);
	enforcement.ReportViolation(1, Violation::RateLimited, "second");
	assert(bans.Get("10.0.0.7").has_value());
	assert(ended.back().second == "banned|Banned: Sent messages too fast. Temporary ban: 10 minutes." + appeal);

	enforcement.ReportViolation(2, Violation::MalformedPayload, "first");
	assert(ended.back().second == "malformed_payload|Kicked: Sent a malformed message." + appeal);
	enforcement.ReportViolation(2, Violation::MalformedPayload, "second");
	assert(bans.GetSteamId("76561198000000888").has_value());
	assert(!bans.Get("10.0.0.8").has_value());
	assert(ended.back().second == "banned|Banned: Sent a malformed message. Temporary ban: 60 minutes." + appeal);

	enforcement.ReportViolation(3, Violation::RateLimited, "first");
	enforcement.ReportViolation(3, Violation::RateLimited, "second");
	assert(!bans.Get("10.0.0.9").has_value());
	assert(ended.back().second == "rate_limited|Kicked: Sent messages too fast." + appeal);

	enforcement.ReportViolation(4, Violation::RateLimited, "first");
	enforcement.ReportViolation(4, Violation::RateLimited, "second");
	assert(!bans.Get("10.0.0.10").has_value());
	assert(ended.back().second == "rate_limited|Kicked: Sent messages too fast." + appeal);

	enforcement.ReportViolation(5, Violation::RateLimited, "first");
	enforcement.ReportViolation(5, Violation::RateLimited, "second");
	assert(ended.back().first == 5);
	assert(ended.back().second == "rate_limited|Kicked: Sent messages too fast." + appeal);

	enforcement.ReportViolation(6, Violation::MalformedPayload, "first");
	assert(ended.back().second == "malformed_payload|Kicked: Sent a malformed message." + appeal);
	enforcement.ReportViolation(6, Violation::MalformedPayload, "second");
	assert(ended.back().second == "banned|Banned: Sent a malformed message. Temporary ban: 60 minutes." + appeal);
	enforcement.ReportViolation(6, Violation::MalformedPayload, "third");
	assert(bans.Get("10.0.0.11").has_value());
	assert(bans.Get("10.0.0.11")->expiresAt != 0.0);
	assert(ended.back().second == "banned|Banned: Sent a malformed message. Temporary ban: 1440 minutes." + appeal);

	const auto nowSeconds = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
	const auto seeded = bans.Ban("10.0.0.60", "Earlier ban.", nowSeconds, nowSeconds + 7200.0);
	assert(seeded.expiresAt == nowSeconds + 7200.0);
	enforcement.ReportViolation(12, Violation::RateLimited, "first");
	enforcement.ReportViolation(12, Violation::RateLimited, "second");
	assert(ended.back().second == "banned|Banned: Sent messages too fast. Temporary ban: 120 minutes." + appeal);

	const auto endedBefore = ended.size();
	enforcement.ReportViolation(1, Violation::TeleportDistance, "first");
	assert(ended.size() == endedBefore);
	enforcement.ReportViolation(9, Violation::RateLimited, "unknown");
	enforcement.ReportViolation(9, Violation::RateLimited, "unknown");
	assert(ended.size() == endedBefore);

	enforcement.ReportViolations({ PendingViolation{ 8, Violation::RateLimited, "queued" } });
	assert(ended.back().first == 8);
	assert(ended.back().second == "rate_limited|Kicked: Sent messages too fast." + appeal);

	ProtocolHost protocol;
	protocol.QueueViolation(7, Violation::PvpDamage, "drain");
	const auto queued = protocol.TakeViolations();
	assert(queued.size() == 1 && queued.front().connectionId == 7 && queued.front().violation == Violation::PvpDamage && queued.front().detail == "drain");
	assert(protocol.TakeViolations().empty());

	RunDetectionTests(scratch);
	std::filesystem::remove_all(scratch);
	return 0;
}
