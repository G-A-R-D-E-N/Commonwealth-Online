#include "F4TParkedSuit.h"

#include "F4TInventoryContract.h"
#include "F4TPlayerKey.h"
#include "F4TPowerArmorValidation.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <set>

namespace F4T::ParkedSuit
{
	namespace
	{
		using Json = nlohmann::json;

		bool HasExactKeys(const Json& a_value, std::initializer_list<const char*> a_keys, std::initializer_list<const char*> a_optionalKeys = {})
		{
			if (!a_value.is_object()) {
				return false;
			}
			const auto optionalCount = static_cast<std::size_t>(std::count_if(a_optionalKeys.begin(), a_optionalKeys.end(), [&](const char* a_key) { return a_value.contains(a_key); }));
			if (a_value.size() != a_keys.size() + optionalCount) {
				return false;
			}
			return std::all_of(a_keys.begin(), a_keys.end(), [&](const char* a_key) { return a_value.contains(a_key); });
		}

		bool IsText(const Json& a_value, std::string_view a_text)
		{
			return a_value.is_string() && a_value.get<std::string>() == a_text;
		}

		std::optional<double> ReadFinite(const Json& a_value, const char* a_key)
		{
			const auto found = a_value.find(a_key);
			if (found == a_value.end() || !found->is_number()) {
				return std::nullopt;
			}
			const auto value = found->get<double>();
			if (!std::isfinite(value)) {
				return std::nullopt;
			}
			return value;
		}

		Json PositionJson(const F4T::ObjectWire::ObjectPosition& a_position)
		{
			return Json{
				{ "cellId", a_position.cellId },
				{ "worldspaceId", a_position.worldspaceId },
				{ "x", a_position.x },
				{ "y", a_position.y },
				{ "z", a_position.z }
			};
		}

		void QueueRelease(std::vector<PendingRelease>& a_releases, PendingRelease a_release)
		{
			const auto duplicate = std::any_of(a_releases.begin(), a_releases.end(), [&](const PendingRelease& a_pending) {
				return a_pending.playerKey == a_release.playerKey && a_pending.suitId == a_release.suitId && a_pending.localId == a_release.localId;
			});
			if (duplicate) {
				return;
			}
			if (a_releases.size() >= kMaximumPendingReleases) {
				a_releases.erase(a_releases.begin());
			}
			a_releases.push_back(std::move(a_release));
		}

		std::optional<Json> PowerArmorInFrame(const Json& a_value)
		{
			const auto normalized = F4T::PowerArmorValidation::NormalizePowerArmor(a_value);
			if (!normalized || !normalized->at("inFrame").get<bool>() || normalized->at("frameBaseFormId").get<std::string>().empty()) {
				return std::nullopt;
			}
			return normalized;
		}

		std::optional<std::uint32_t> ParseHexId(const Json& a_value)
		{
			if (!a_value.is_string()) {
				return std::nullopt;
			}
			const auto text = a_value.get<std::string>();
			if (text.empty() || text.size() > 8) {
				return std::nullopt;
			}
			std::uint32_t parsed = 0;
			const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, 16);
			if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsed == 0) {
				return std::nullopt;
			}
			return parsed;
		}

		std::string HexId(std::uint32_t a_value)
		{
			char buffer[9]{};
			std::snprintf(buffer, sizeof(buffer), "%08X", a_value);
			return std::string{ buffer };
		}

		std::optional<std::string> PlayerKeyField(const Json& a_value)
		{
			if (!a_value.is_string() || !F4T::PlayerKey::IsPlayerKey(a_value.get<std::string>())) {
				return std::nullopt;
			}
			return a_value.get<std::string>();
		}

		std::optional<std::size_t> ReadCell(const Json& a_value)
		{
			if (!a_value.is_number_integer()) {
				return std::nullopt;
			}
			const auto value = a_value.get<std::int64_t>();
			if (value < 0 || value >= static_cast<std::int64_t>(Breach::kGridSize)) {
				return std::nullopt;
			}
			return static_cast<std::size_t>(value);
		}

		std::optional<std::vector<Breach::Pick>> NormalizePicks(const Json& a_value)
		{
			if (!a_value.is_array() || a_value.size() > Breach::kMaximumPicks) {
				return std::nullopt;
			}
			std::vector<Breach::Pick> picks;
			for (const auto& entry : a_value) {
				if (!entry.is_array() || entry.size() != 2) {
					return std::nullopt;
				}
				const auto row = ReadCell(entry[0]);
				const auto col = ReadCell(entry[1]);
				if (!row || !col) {
					return std::nullopt;
				}
				picks.push_back(Breach::Pick{ *row, *col });
			}
			return picks;
		}

		std::optional<std::vector<std::string>> ReadPieceKeys(const Json& a_object)
		{
			const auto found = a_object.find("pieceKeys");
			if (found == a_object.end()) {
				return std::vector<std::string>{};
			}
			if (!found->is_array() || found->size() > kMaximumPieceKeys) {
				return std::nullopt;
			}
			std::vector<std::string> keys;
			for (const auto& entry : *found) {
				if (!entry.is_string()) {
					return std::nullopt;
				}
				const auto key = entry.get<std::string>();
				if (key.size() > F4T::InventoryContract::kMaximumItemKeyChars || !F4T::InventoryContract::ParseItemKey(key)) {
					return std::nullopt;
				}
				if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
					keys.push_back(key);
				}
			}
			return keys;
		}

		std::size_t CountOwned(const std::vector<Record>& a_records, std::string_view a_ownerKey)
		{
			return static_cast<std::size_t>(std::count_if(a_records.begin(), a_records.end(), [&](const Record& a_record) {
				return a_record.ownerKey == a_ownerKey;
			}));
		}
	}

	std::optional<std::string> NormalizeSuitId(const Json& a_value)
	{
		const auto parsed = ParseHexId(a_value);
		if (!parsed) {
			return std::nullopt;
		}
		return HexId(*parsed);
	}

	std::optional<std::uint32_t> NormalizeNonce(const Json& a_value)
	{
		if (!a_value.is_number_integer()) {
			return std::nullopt;
		}
		const auto value = a_value.get<std::int64_t>();
		if (value < 0 || value > std::numeric_limits<std::uint32_t>::max()) {
			return std::nullopt;
		}
		return static_cast<std::uint32_t>(value);
	}

	std::optional<ParkRequest> NormalizeParkRequest(const Json& a_packet)
	{
		if (!HasExactKeys(a_packet, { "type", "action", "localId", "powerArmor", "position", "angleZ", "nonce" }, { "pieceKeys", "console" })) {
			return std::nullopt;
		}
		if (!IsText(a_packet.at("type"), kPacketType) || !IsText(a_packet.at("action"), "park")) {
			return std::nullopt;
		}
		const auto localId = NormalizeSuitId(a_packet.at("localId"));
		const auto powerArmor = PowerArmorInFrame(a_packet.at("powerArmor"));
		const auto position = F4T::ObjectWire::ParsePosition(a_packet.at("position"));
		const auto angleZ = ReadFinite(a_packet, "angleZ");
		const auto nonce = NormalizeNonce(a_packet.at("nonce"));
		const auto pieceKeys = ReadPieceKeys(a_packet);
		bool console = false;
		if (const auto found = a_packet.find("console"); found != a_packet.end()) {
			if (!found->is_boolean()) {
				return std::nullopt;
			}
			console = found->get<bool>();
		}
		if (!localId || !powerArmor || !position || !angleZ || !nonce || !pieceKeys) {
			return std::nullopt;
		}
		return ParkRequest{ *localId, *powerArmor, *position, *angleZ, *pieceKeys, console };
	}

	std::optional<std::string> NormalizeRemoveRequest(const Json& a_packet)
	{
		if (!HasExactKeys(a_packet, { "type", "action", "localId", "nonce" })) {
			return std::nullopt;
		}
		if (!IsText(a_packet.at("type"), kPacketType) || !IsText(a_packet.at("action"), "remove") || !NormalizeNonce(a_packet.at("nonce"))) {
			return std::nullopt;
		}
		return NormalizeSuitId(a_packet.at("localId"));
	}

	std::optional<std::string> NormalizeBreachStartRequest(const Json& a_packet)
	{
		if (!HasExactKeys(a_packet, { "type", "action", "suitId" })) {
			return std::nullopt;
		}
		if (!IsText(a_packet.at("type"), kPacketType) || !IsText(a_packet.at("action"), "breachStart")) {
			return std::nullopt;
		}
		return NormalizeSuitId(a_packet.at("suitId"));
	}

	std::optional<HackRequest> NormalizeHackRequest(const Json& a_packet)
	{
		if (!HasExactKeys(a_packet, { "type", "action", "suitId", "picks" })) {
			return std::nullopt;
		}
		if (!IsText(a_packet.at("type"), kPacketType) || !IsText(a_packet.at("action"), "hack")) {
			return std::nullopt;
		}
		const auto suitId = NormalizeSuitId(a_packet.at("suitId"));
		const auto picks = NormalizePicks(a_packet.at("picks"));
		if (!suitId || !picks) {
			return std::nullopt;
		}
		return HackRequest{ *suitId, *picks };
	}

	std::optional<AdoptRequest> NormalizeAdoptRequest(const Json& a_packet)
	{
		if (!HasExactKeys(a_packet, { "type", "action", "suitId", "localId", "nonce" })) {
			return std::nullopt;
		}
		if (!IsText(a_packet.at("type"), kPacketType) || !IsText(a_packet.at("action"), "adopt")) {
			return std::nullopt;
		}
		const auto suitId = NormalizeSuitId(a_packet.at("suitId"));
		const auto localId = NormalizeSuitId(a_packet.at("localId"));
		if (!suitId || !localId || !NormalizeNonce(a_packet.at("nonce"))) {
			return std::nullopt;
		}
		return AdoptRequest{ *suitId, *localId };
	}

	bool IsWriteRequest(const Json& a_packet)
	{
		if (!a_packet.is_object() || !a_packet.contains("type") || !a_packet.contains("action")) {
			return false;
		}
		return IsText(a_packet.at("type"), kPacketType) && (IsText(a_packet.at("action"), "park") || IsText(a_packet.at("action"), "remove") || IsText(a_packet.at("action"), "adopt"));
	}

	Json BuildParkPacket(const Record& a_record)
	{
		return Json{
			{ "type", std::string{ kPacketType } },
			{ "action", "park" },
			{ "ownerKey", a_record.ownerKey },
			{ "suitId", a_record.suitId },
			{ "powerArmor", a_record.powerArmor },
			{ "position", PositionJson(a_record.position) },
			{ "angleZ", a_record.angleZ }
		};
	}

	Json BuildRemovePacket(std::string_view a_ownerKey, std::string_view a_suitId)
	{
		return Json{
			{ "type", std::string{ kPacketType } },
			{ "action", "remove" },
			{ "ownerKey", std::string{ a_ownerKey } },
			{ "suitId", std::string{ a_suitId } }
		};
	}

	Json BuildReleasePacket(std::string_view a_suitId, std::string_view a_localId)
	{
		return Json{
			{ "type", std::string{ kPacketType } },
			{ "action", "release" },
			{ "suitId", std::string{ a_suitId } },
			{ "localId", std::string{ a_localId } }
		};
	}

	Json BuildBreachChallengePacket(std::string_view a_suitId, std::uint32_t a_seed, std::size_t a_length)
	{
		return Json{
			{ "type", std::string{ kPacketType } },
			{ "action", "breachChallenge" },
			{ "suitId", std::string{ a_suitId } },
			{ "seed", a_seed },
			{ "length", a_length }
		};
	}

	Json BuildAckPacket(const Json& a_request, bool a_ok, std::string_view a_reason)
	{
		const auto localId = a_request.contains("localId") && a_request.at("localId").is_string() ? a_request.at("localId").get<std::string>() : std::string{};
		auto ack = Json{
			{ "type", std::string{ kPacketType } },
			{ "action", "ack" },
			{ "op", a_request.at("action") },
			{ "localId", localId },
			{ "nonce", a_request.value("nonce", Json(nullptr)) },
			{ "ok", a_ok }
		};
		if (!a_reason.empty()) {
			ack["reason"] = std::string{ a_reason };
		}
		return ack;
	}

	Json BuildHackResultPacket(std::string_view a_suitId, bool a_hacked)
	{
		return Json{
			{ "type", std::string{ kPacketType } },
			{ "action", "hackResult" },
			{ "suitId", std::string{ a_suitId } },
			{ "hacked", a_hacked }
		};
	}

	Json BuildHackAlertPacket(std::string_view a_suitId, bool a_failed)
	{
		return Json{
			{ "type", std::string{ kPacketType } },
			{ "action", "hackAlert" },
			{ "suitId", std::string{ a_suitId } },
			{ "failed", a_failed }
		};
	}

	Json BuildBreachFailedPacket(std::string_view a_suitId)
	{
		return Json{
			{ "type", std::string{ kPacketType } },
			{ "action", "breachFailed" },
			{ "suitId", std::string{ a_suitId } }
		};
	}

	std::optional<std::string> NormalizeHackAlert(const Json& a_packet)
	{
		if (!HasExactKeys(a_packet, { "type", "action", "suitId", "failed" })) {
			return std::nullopt;
		}
		if (!IsText(a_packet.at("type"), kPacketType) || !IsText(a_packet.at("action"), "hackAlert")) {
			return std::nullopt;
		}
		if (!a_packet.at("failed").is_boolean()) {
			return std::nullopt;
		}
		return NormalizeSuitId(a_packet.at("suitId"));
	}

	std::optional<std::string> NormalizeBreachFailedRequest(const Json& a_packet)
	{
		if (!HasExactKeys(a_packet, { "type", "action", "suitId" })) {
			return std::nullopt;
		}
		if (!IsText(a_packet.at("type"), kPacketType) || !IsText(a_packet.at("action"), "breachFailed")) {
			return std::nullopt;
		}
		return NormalizeSuitId(a_packet.at("suitId"));
	}

	ParkOutcome Table::Park(Record a_record)
	{
		const auto same = std::find_if(m_records.begin(), m_records.end(), [&](const Record& a_existing) {
			return a_existing.localOwnerKey == a_record.localOwnerKey && a_existing.localId == a_record.localId;
		});
		if (same != m_records.end()) {
			if (same->ownerKey != same->localOwnerKey && a_record.ownerKey == a_record.localOwnerKey) {
				return ParkOutcome::NotOwner;
			}
			if (same->ownerKey != a_record.ownerKey && CountOwned(m_records, a_record.ownerKey) >= kMaximumPerOwner) {
				return ParkOutcome::OwnerLimit;
			}
			a_record.suitId = same->suitId;
			*same = std::move(a_record);
			return ParkOutcome::Replaced;
		}
		if (CountOwned(m_records, a_record.ownerKey) >= kMaximumPerOwner) {
			return ParkOutcome::OwnerLimit;
		}
		if (m_records.size() >= kMaximumTotal) {
			return ParkOutcome::TableFull;
		}
		if (a_record.suitId.empty()) {
			a_record.suitId = NextSuitId();
		}
		m_records.push_back(std::move(a_record));
		return ParkOutcome::Stored;
	}

	std::optional<std::string> Table::Remove(std::string_view a_ownerKey, std::string_view a_localId)
	{
		const auto found = std::find_if(m_records.begin(), m_records.end(), [&](const Record& a_record) {
			return a_record.localOwnerKey == a_ownerKey && a_record.ownerKey == a_ownerKey && a_record.localId == a_localId;
		});
		if (found == m_records.end()) {
			return std::nullopt;
		}
		auto suitId = found->suitId;
		m_records.erase(found);
		return suitId;
	}

	const Record* Table::Find(std::string_view a_suitId) const
	{
		const auto found = std::find_if(m_records.begin(), m_records.end(), [&](const Record& a_record) {
			return a_record.suitId == a_suitId;
		});
		return found == m_records.end() ? nullptr : &*found;
	}

	const Record* Table::FindByLocal(std::string_view a_localOwnerKey, std::string_view a_localId) const
	{
		const auto found = std::find_if(m_records.begin(), m_records.end(), [&](const Record& a_record) {
			return a_record.localOwnerKey == a_localOwnerKey && a_record.localId == a_localId;
		});
		return found == m_records.end() ? nullptr : &*found;
	}

	HackOutcome Table::Reassign(std::string_view a_suitId, std::string a_newOwnerKey)
	{
		const auto found = std::find_if(m_records.begin(), m_records.end(), [&](const Record& a_record) {
			return a_record.suitId == a_suitId;
		});
		if (found == m_records.end()) {
			return HackOutcome::Missing;
		}
		if (found->ownerKey == a_newOwnerKey) {
			return HackOutcome::SameOwner;
		}
		if (CountOwned(m_records, a_newOwnerKey) >= kMaximumPerOwner) {
			return HackOutcome::OwnerLimit;
		}
		found->ownerKey = std::move(a_newOwnerKey);
		found->hacked = true;
		return HackOutcome::Hacked;
	}

	AdoptOutcome Table::Adopt(std::string_view a_suitId, std::string_view a_requesterKey, std::string_view a_localId, std::string* a_previousLocalOwnerKey)
	{
		const auto found = std::find_if(m_records.begin(), m_records.end(), [&](const Record& a_record) {
			return a_record.suitId == a_suitId;
		});
		if (found == m_records.end()) {
			return AdoptOutcome::Missing;
		}
		if (found->ownerKey != a_requesterKey) {
			return AdoptOutcome::NotOwner;
		}
		const auto duplicate = std::any_of(m_records.begin(), m_records.end(), [&](const Record& a_record) {
			return a_record.suitId != a_suitId && a_record.localOwnerKey == a_requesterKey && a_record.localId == a_localId;
		});
		if (duplicate) {
			return AdoptOutcome::Duplicate;
		}
		if (a_previousLocalOwnerKey) {
			*a_previousLocalOwnerKey = found->localOwnerKey;
		}
		if (!found->localOwnerKey.empty() && found->localOwnerKey != a_requesterKey) {
			QueueRelease(m_releases, PendingRelease{ found->localOwnerKey, found->suitId, found->localId });
		}
		found->localOwnerKey = std::string{ a_requesterKey };
		found->localId = std::string{ a_localId };
		return AdoptOutcome::Adopted;
	}

	std::vector<PendingRelease> Table::TakeReleases(std::string_view a_playerKey)
	{
		std::vector<PendingRelease> taken;
		std::erase_if(m_releases, [&](const PendingRelease& a_release) {
			if (a_release.playerKey != a_playerKey) {
				return false;
			}
			taken.push_back(a_release);
			return true;
		});
		return taken;
	}

	bool Table::ClearRelease(std::string_view a_playerKey, std::string_view a_localId)
	{
		return std::erase_if(m_releases, [&](const PendingRelease& a_release) {
			return a_release.playerKey == a_playerKey && a_release.localId == a_localId;
		}) > 0;
	}

	std::vector<Record> Table::SweepOffline(double a_nowUnix, double a_retentionSeconds, const std::set<std::string>& a_onlineOwnerKeys)
	{
		for (auto& record : m_records) {
			if (a_onlineOwnerKeys.contains(record.ownerKey) || record.ownerLastSeenUnix <= 0.0) {
				record.ownerLastSeenUnix = a_nowUnix;
			}
		}
		std::vector<Record> evicted;
		std::erase_if(m_records, [&](const Record& a_record) {
			const bool expired = !a_onlineOwnerKeys.contains(a_record.ownerKey) && a_nowUnix - a_record.ownerLastSeenUnix > a_retentionSeconds;
			if (expired) {
				evicted.push_back(a_record);
			}
			return expired;
		});
		for (const auto& record : evicted) {
			std::erase_if(m_releases, [&](const PendingRelease& a_release) { return a_release.suitId == record.suitId; });
		}
		return evicted;
	}

	std::optional<Record> Table::EvictOldestOffline(const std::set<std::string>& a_onlineOwnerKeys)
	{
		const auto ageOf = [](const Record& a_record) {
			return a_record.ownerLastSeenUnix > 0.0 ? a_record.ownerLastSeenUnix : std::numeric_limits<double>::max();
		};
		auto oldest = m_records.end();
		for (auto found = m_records.begin(); found != m_records.end(); ++found) {
			if (a_onlineOwnerKeys.contains(found->ownerKey)) {
				continue;
			}
			if (oldest == m_records.end() || ageOf(*found) < ageOf(*oldest)) {
				oldest = found;
			}
		}
		if (oldest == m_records.end()) {
			return std::nullopt;
		}
		Record evicted = *oldest;
		m_records.erase(oldest);
		std::erase_if(m_releases, [&](const PendingRelease& a_release) { return a_release.suitId == evicted.suitId; });
		return evicted;
	}

	std::string Table::NextSuitId()
	{
		if (m_nextSuitNumber == 0) {
			m_nextSuitNumber = 1;
		}
		return HexId(m_nextSuitNumber++);
	}

	Json Table::ToJson() const
	{
		Json records = Json::array();
		for (const auto& record : m_records) {
			records.push_back(Json{
				{ "ownerKey", record.ownerKey },
				{ "suitId", record.suitId },
				{ "localId", record.localId },
				{ "localOwnerKey", record.localOwnerKey },
				{ "powerArmor", record.powerArmor },
				{ "position", PositionJson(record.position) },
				{ "angleZ", record.angleZ },
				{ "pieceKeys", record.pieceKeys },
				{ "hacked", record.hacked },
				{ "ownerLastSeenUnix", record.ownerLastSeenUnix } });
		}
		Json releases = Json::array();
		for (const auto& release : m_releases) {
			releases.push_back(Json{
				{ "playerKey", release.playerKey },
				{ "suitId", release.suitId },
				{ "localId", release.localId } });
		}
		return Json{ { "records", records }, { "releases", releases } };
	}

	void Table::LoadFromJson(const Json& a_value)
	{
		m_records.clear();
		m_releases.clear();
		m_nextSuitNumber = 1;
		const auto isObject = a_value.is_object();
		const auto& records = isObject && a_value.contains("records") ? a_value.at("records") : a_value;
		if (isObject && a_value.contains("releases") && a_value.at("releases").is_array()) {
			for (const auto& node : a_value.at("releases")) {
				if (!HasExactKeys(node, { "playerKey", "suitId", "localId" })) {
					continue;
				}
				const auto playerKey = PlayerKeyField(node.at("playerKey"));
				const auto suitId = NormalizeSuitId(node.at("suitId"));
				const auto localId = NormalizeSuitId(node.at("localId"));
				if (playerKey && suitId && localId) {
					QueueRelease(m_releases, PendingRelease{ *playerKey, *suitId, *localId });
				}
			}
		}
		if (!records.is_array()) {
			return;
		}
		std::vector<Record> loaded;
		std::uint32_t highest = 0;
		std::set<std::uint32_t> seenSuitIds;
		for (const auto& node : records) {
			const auto accepted = HasExactKeys(node, { "ownerKey", "suitId", "localId", "localOwnerKey", "powerArmor", "position", "angleZ" }, { "pieceKeys", "hacked", "ownerLastSeenUnix" }) ||
				HasExactKeys(node, { "ownerKey", "localId", "localOwnerKey", "powerArmor", "position", "angleZ" }, { "pieceKeys", "hacked", "ownerLastSeenUnix" }) ||
				HasExactKeys(node, { "ownerKey", "suitId", "powerArmor", "position", "angleZ" }, { "pieceKeys", "hacked", "ownerLastSeenUnix" });
			if (!accepted) {
				continue;
			}
			const bool legacy = !node.contains("localId");
			const auto ownerKey = PlayerKeyField(node.at("ownerKey"));
			const auto localId = NormalizeSuitId(node.at(legacy ? "suitId" : "localId"));
			const auto localOwnerKey = legacy ? ownerKey : PlayerKeyField(node.at("localOwnerKey"));
			const auto powerArmor = PowerArmorInFrame(node.at("powerArmor"));
			const auto position = F4T::ObjectWire::ParsePosition(node.at("position"));
			const auto angleZ = ReadFinite(node, "angleZ");
			if (!ownerKey || !localId || !localOwnerKey || !powerArmor || !position || !angleZ) {
				continue;
			}
			std::optional<std::uint32_t> suitValue;
			if (!legacy && node.contains("suitId")) {
				suitValue = ParseHexId(node.at("suitId"));
			}
			if (suitValue) {
				if (!seenSuitIds.insert(*suitValue).second) {
					continue;
				}
				highest = std::max(highest, *suitValue);
			}
			const auto pieceKeys = ReadPieceKeys(node).value_or(std::vector<std::string>{});
			const auto hacked = node.contains("hacked") && node.at("hacked").is_boolean() && node.at("hacked").get<bool>();
			const auto ownerLastSeen = std::max(0.0, ReadFinite(node, "ownerLastSeenUnix").value_or(0.0));
			loaded.push_back(Record{ *ownerKey, suitValue ? HexId(*suitValue) : std::string{}, *localId, *localOwnerKey, *powerArmor, *position, *angleZ, pieceKeys, hacked, ownerLastSeen });
		}
		m_nextSuitNumber = highest + 1;
		for (auto& record : loaded) {
			(void)Park(std::move(record));
		}
	}

	PieceAuditResult EvaluatePieceAudit(const std::vector<std::string>& a_keys, const Json& a_inventory, double a_now, double a_deadline)
	{
		PieceAuditResult result;
		const auto items = a_inventory.find("items");
		const auto held = [&](const std::string& a_key) {
			if (items == a_inventory.end() || !items->is_object()) {
				return false;
			}
			const auto found = items->find(a_key);
			return found != items->end() && found->contains("count") && found->at("count").is_number_unsigned() && found->at("count").get<std::uint32_t>() > 0;
		};
		for (const auto& key : a_keys) {
			if (!held(key)) {
				result.missing.push_back(key);
			}
		}
		result.allPresent = result.missing.empty();
		result.expired = !result.allPresent && a_now >= a_deadline;
		return result;
	}
}
