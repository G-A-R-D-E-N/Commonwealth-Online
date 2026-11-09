#include "SteamIdentityRegistry.h"

#include "BanStore.h"
#include "F4TSteamId.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using Json = nlohmann::json;

		std::optional<SteamIdSource> ParseSource(std::string_view a_name)
		{
			for (const auto source : { SteamIdSource::SteamTunnel, SteamIdSource::HostSelf, SteamIdSource::WebApi }) {
				if (SteamIdSourceName(source) == a_name) return source;
			}
			return std::nullopt;
		}

		std::optional<SteamIdentityFlagKind> ParseFlagKind(std::string_view a_name)
		{
			for (const auto kind : { SteamIdentityFlagKind::SharedSteamId, SteamIdentityFlagKind::MultipleSteamIds }) {
				if (SteamIdentityFlagKindName(kind) == a_name) return kind;
			}
			return std::nullopt;
		}

		const std::string& RequireString(const Json& a_object, const char* a_key)
		{
			const auto found = a_object.find(a_key);
			if (found == a_object.end() || !found->is_string()) {
				throw std::invalid_argument(std::string{ "Steam identity entry is missing " } + a_key + ".");
			}
			return found->get_ref<const std::string&>();
		}

		double RequireTime(const Json& a_object, const char* a_key)
		{
			const auto found = a_object.find(a_key);
			if (found == a_object.end() || !found->is_number() || found->is_boolean()) {
				throw std::invalid_argument(std::string{ "Steam identity entry is missing " } + a_key + ".");
			}
			const auto value = found->get<double>();
			if (!std::isfinite(value) || value < 0.0) {
				throw std::invalid_argument(std::string{ "Steam identity entry has an invalid " } + a_key + ".");
			}
			return value;
		}

		std::string RequireProfileId(const Json& a_object, const char* a_key)
		{
			const auto& value = RequireString(a_object, a_key);
			if (BanStore::NormalizeProfileId(value) != value) throw std::invalid_argument("Steam identity entry has an invalid profileId.");
			return value;
		}

		std::string RequireSteamId(const Json& a_object, const char* a_key)
		{
			const auto& value = RequireString(a_object, a_key);
			if (!F4T::SteamId::ParseIndividual(value)) throw std::invalid_argument("Steam identity entry has an invalid Steam ID.");
			return value;
		}

		bool SamePair(const SteamIdentityRecord& a_record, std::string_view a_profileId, std::string_view a_steamId)
		{
			return a_record.profileId == a_profileId && a_record.steamId == a_steamId;
		}
	}

	std::string_view SteamIdentityFlagKindName(SteamIdentityFlagKind a_kind) noexcept
	{
		return a_kind == SteamIdentityFlagKind::SharedSteamId ? "shared_steam_id" : "multiple_steam_ids";
	}

	SteamIdentityRegistry::SteamIdentityRegistry(const nlohmann::json& a_stored)
	{
		if (a_stored.is_null()) return;
		if (!a_stored.is_object()) throw std::invalid_argument("Steam identity section must be an object.");
		const auto records = a_stored.find("records");
		const auto flags = a_stored.find("flags");
		if ((records != a_stored.end() && !records->is_array()) || (flags != a_stored.end() && !flags->is_array())) {
			throw std::invalid_argument("Steam identity records and flags must be arrays.");
		}
		if (records != a_stored.end()) {
			if (records->size() > kMaximumRecords) throw std::invalid_argument("Steam identity section has too many records.");
			for (const auto& item : *records) {
				if (!item.is_object()) throw std::invalid_argument("Steam identity record must be an object.");
				SteamIdentityRecord record{
					.profileId = RequireProfileId(item, "profileId"),
					.steamId = RequireSteamId(item, "steamId"),
					.source = ParseSource(RequireString(item, "source")).value_or(SteamIdSource::None),
					.firstSeen = RequireTime(item, "firstSeen"),
					.lastSeen = RequireTime(item, "lastSeen"),
					.nicknames = {}
				};
				if (record.source == SteamIdSource::None) throw std::invalid_argument("Steam identity record has an unknown source.");
				if (record.firstSeen > record.lastSeen) throw std::invalid_argument("Steam identity record was first seen after it was last seen.");
				if (const auto names = item.find("nicknames"); names != item.end()) {
					if (!names->is_array() || names->size() > kMaximumNicknames) throw std::invalid_argument("Steam identity nicknames must be a short array.");
					for (const auto& name : *names) {
						if (!name.is_string() || name.get_ref<const std::string&>().size() > kMaximumNicknameLength) {
							throw std::invalid_argument("Steam identity nickname is invalid.");
						}
						record.nicknames.push_back(name.get<std::string>());
					}
				}
				if (std::any_of(_records.begin(), _records.end(), [&](const auto& existing) { return SamePair(existing, record.profileId, record.steamId); })) {
					throw std::invalid_argument("Steam identity section repeats a profile and Steam ID pair.");
				}
				_records.push_back(std::move(record));
			}
		}
		if (flags != a_stored.end()) {
			if (flags->size() > kMaximumFlags) throw std::invalid_argument("Steam identity section has too many flags.");
			for (const auto& item : *flags) {
				if (!item.is_object()) throw std::invalid_argument("Steam identity flag must be an object.");
				const auto kind = ParseFlagKind(RequireString(item, "kind"));
				if (!kind) throw std::invalid_argument("Steam identity flag has an unknown kind.");
				_flags.push_back(SteamIdentityFlag{
					.kind = *kind,
					.profileId = RequireProfileId(item, "profileId"),
					.steamId = RequireSteamId(item, "steamId"),
					.otherId = *kind == SteamIdentityFlagKind::SharedSteamId ? RequireProfileId(item, "otherId") : RequireSteamId(item, "otherId"),
					.raisedAt = RequireTime(item, "raisedAt")
				});
			}
		}
	}

	SteamIdentityObservation SteamIdentityRegistry::Observe(
		std::string_view a_profileId,
		std::string_view a_steamId,
		SteamIdSource a_source,
		std::string_view a_nickname,
		double a_now)
	{
		SteamIdentityObservation observation;
		if (BanStore::NormalizeProfileId(a_profileId) != a_profileId || !F4T::SteamId::ParseIndividual(a_steamId) ||
			a_source == SteamIdSource::None || !std::isfinite(a_now) || a_now < 0.0) {
			return observation;
		}
		auto record = std::find_if(_records.begin(), _records.end(), [&](const auto& existing) { return SamePair(existing, a_profileId, a_steamId); });
		if (record == _records.end()) {
			if (_records.size() >= kMaximumRecords) return observation;
			for (const auto& other : _records) {
				if (other.steamId == a_steamId) {
					(void)RaiseFlag({ SteamIdentityFlagKind::SharedSteamId, std::string{ a_profileId }, std::string{ a_steamId }, other.profileId, a_now }, observation);
				} else if (other.profileId == a_profileId) {
					(void)RaiseFlag({ SteamIdentityFlagKind::MultipleSteamIds, std::string{ a_profileId }, std::string{ a_steamId }, other.steamId, a_now }, observation);
				}
			}
			_records.push_back(SteamIdentityRecord{ std::string{ a_profileId }, std::string{ a_steamId }, a_source, a_now, a_now, {} });
			record = std::prev(_records.end());
			observation.newLink = true;
		}
		record->source = a_source;
		record->lastSeen = (std::max)(record->lastSeen, a_now);
		const auto nickname = a_nickname.substr(0, kMaximumNicknameLength);
		if (!nickname.empty() && (record->nicknames.empty() || record->nicknames.front() != nickname)) {
			std::erase(record->nicknames, nickname);
			record->nicknames.insert(record->nicknames.begin(), std::string{ nickname });
			if (record->nicknames.size() > kMaximumNicknames) record->nicknames.resize(kMaximumNicknames);
		}
		observation.stored = true;
		return observation;
	}

	bool SteamIdentityRegistry::RaiseFlag(SteamIdentityFlag a_flag, SteamIdentityObservation& a_observation)
	{
		const bool known = std::any_of(_flags.begin(), _flags.end(), [&](const SteamIdentityFlag& existing) {
			return existing.kind == a_flag.kind && existing.profileId == a_flag.profileId &&
				existing.steamId == a_flag.steamId && existing.otherId == a_flag.otherId;
		});
		if (known || _flags.size() >= kMaximumFlags) return false;
		a_observation.raised.push_back(a_flag);
		_flags.push_back(std::move(a_flag));
		return true;
	}

	std::vector<SteamIdentityRecord> SteamIdentityRegistry::ForSteamId(std::string_view a_steamId) const
	{
		std::vector<SteamIdentityRecord> found;
		std::copy_if(_records.begin(), _records.end(), std::back_inserter(found), [&](const auto& record) { return record.steamId == a_steamId; });
		return found;
	}

	std::vector<SteamIdentityRecord> SteamIdentityRegistry::ForProfile(std::string_view a_profileId) const
	{
		std::vector<SteamIdentityRecord> found;
		std::copy_if(_records.begin(), _records.end(), std::back_inserter(found), [&](const auto& record) { return record.profileId == a_profileId; });
		return found;
	}

	bool SteamIdentityRegistry::IsFlagged(const SteamIdentityRecord& a_record) const
	{
		return std::any_of(_flags.begin(), _flags.end(), [&](const SteamIdentityFlag& flag) {
			const bool otherIsProfile = flag.kind == SteamIdentityFlagKind::SharedSteamId;
			return (flag.profileId == a_record.profileId && flag.steamId == a_record.steamId) ||
				(otherIsProfile ? flag.otherId == a_record.profileId && flag.steamId == a_record.steamId
				                : flag.profileId == a_record.profileId && flag.otherId == a_record.steamId);
		});
	}

	std::size_t SteamIdentityRegistry::ClearFlags(std::string_view a_profileId, std::string_view a_steamId)
	{
		return std::erase_if(_flags, [&](const SteamIdentityFlag& flag) { return flag.profileId == a_profileId && flag.steamId == a_steamId; });
	}

	std::size_t SteamIdentityRegistry::Sweep(double a_now, const std::function<bool(const SteamIdentityRecord&)>& a_keep)
	{
		return std::erase_if(_records, [&](const SteamIdentityRecord& record) {
			return a_now - record.lastSeen > kRetentionSeconds && !IsFlagged(record) && !(a_keep && a_keep(record));
		});
	}

	nlohmann::json SteamIdentityRegistry::RecordJson(const SteamIdentityRecord& a_record)
	{
		return Json{
			{ "profileId", a_record.profileId },
			{ "steamId", a_record.steamId },
			{ "source", std::string{ SteamIdSourceName(a_record.source) } },
			{ "firstSeen", a_record.firstSeen },
			{ "lastSeen", a_record.lastSeen },
			{ "nicknames", a_record.nicknames }
		};
	}

	nlohmann::json SteamIdentityRegistry::FlagJson(const SteamIdentityFlag& a_flag)
	{
		return Json{
			{ "kind", std::string{ SteamIdentityFlagKindName(a_flag.kind) } },
			{ "profileId", a_flag.profileId },
			{ "steamId", a_flag.steamId },
			{ "otherId", a_flag.otherId },
			{ "raisedAt", a_flag.raisedAt }
		};
	}

	nlohmann::json SteamIdentityRegistry::ToJson() const
	{
		Json records = Json::array();
		for (const auto& record : _records) records.push_back(RecordJson(record));
		Json flags = Json::array();
		for (const auto& flag : _flags) flags.push_back(FlagJson(flag));
		return Json{ { "records", std::move(records) }, { "flags", std::move(flags) } };
	}
}
