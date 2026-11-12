#include "BanStore.h"
#include "AtomicFileWrite.h"

#include "F4TSteamId.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace CommonwealthOnline::Server
{
namespace
{
	using Json = nlohmann::json;
}

void BanStore::Load()
{
	_entries.clear();
	_steamEntries.clear();
	const auto expiresAtOf = [](const Json& a_item) {
		if (const auto node = a_item.find("expiresAt"); node != a_item.end() && !node->is_boolean() && node->is_number()) {
			const auto value = node->get<double>();
			if (!std::isfinite(value)) throw std::runtime_error("Ban file has a non-finite expiresAt.");
			return value;
		}
		return 0.0;
	};
	if (!std::filesystem::exists(_path)) return;
	std::ifstream input(_path, std::ios::binary);
	if (!input) throw std::runtime_error("Could not open ban file '" + _path.string() + "'.");
	Json root;
	try { input >> root; } catch (const Json::exception& exception) {
		throw std::runtime_error("Ban file is not valid JSON: " + std::string{ exception.what() });
	}
	if (!root.is_object()) throw std::runtime_error("Ban file has an invalid schema.");
	const auto banned = root.find("banned_ips");
	if (banned == root.end() || !banned->is_array()) throw std::runtime_error("Ban file has an invalid schema.");
	const auto bannedProfiles = root.find("banned_profiles");
	if (bannedProfiles != root.end() && !bannedProfiles->is_array()) throw std::runtime_error("Ban file has an invalid schema.");
	const auto bannedSteamIds = root.find("banned_steam_ids");
	if (bannedSteamIds != root.end() && !bannedSteamIds->is_array()) throw std::runtime_error("Ban file has an invalid schema.");
	std::size_t invalid = 0;
	for (const auto& item : *banned) {
		if (!item.is_object()) { ++invalid; continue; }
		const auto ipIt = item.find("ip");
		if (ipIt == item.end() || !ipIt->is_string()) { ++invalid; continue; }
		const auto normalized = BanStore::NormalizeAddress(ipIt->get_ref<const std::string&>());
		if (!normalized) { ++invalid; continue; }
		std::string reason;
		if (const auto node = item.find("reason"); node != item.end() && node->is_string()) {
			reason = node->get<std::string>();
			if (reason.size() > 1024) reason.resize(1024);
		}
		double bannedAt = 0.0;
		if (const auto node = item.find("bannedAt"); node != item.end() && !node->is_boolean() && node->is_number()) {
			try { const auto value = node->get<double>(); if (std::isfinite(value)) bannedAt = value; } catch (...) {}
		}
		BanEntry entry{ .ip = *normalized, .profileId = {}, .reason = std::move(reason), .bannedAt = bannedAt, .expiresAt = expiresAtOf(item) };
		const auto existing = std::find_if(_entries.begin(), _entries.end(), [&](const BanEntry& candidate) {
			return candidate.profileId.empty() && candidate.ip == entry.ip;
		});
		if (existing == _entries.end()) _entries.push_back(std::move(entry)); else *existing = std::move(entry);
	}
	if (bannedProfiles != root.end()) {
		for (const auto& item : *bannedProfiles) {
			if (!item.is_object()) { ++invalid; continue; }
			const auto profileIt = item.find("profileId");
			if (profileIt == item.end() || !profileIt->is_string()) { ++invalid; continue; }
			const auto normalized = BanStore::NormalizeProfileId(profileIt->get_ref<const std::string&>());
			if (!normalized) { ++invalid; continue; }
			std::string reason;
			if (const auto node = item.find("reason"); node != item.end() && node->is_string()) {
				reason = node->get<std::string>();
				if (reason.size() > 1024) reason.resize(1024);
			}
			double bannedAt = 0.0;
			if (const auto node = item.find("bannedAt"); node != item.end() && !node->is_boolean() && node->is_number()) {
				try { const auto value = node->get<double>(); if (std::isfinite(value)) bannedAt = value; } catch (...) {}
			}
			BanEntry entry{ .ip = {}, .profileId = *normalized, .reason = std::move(reason), .bannedAt = bannedAt, .expiresAt = expiresAtOf(item) };
			const auto existing = std::find_if(_entries.begin(), _entries.end(), [&](const BanEntry& candidate) {
				return !candidate.profileId.empty() && candidate.profileId == entry.profileId;
			});
			if (existing == _entries.end()) _entries.push_back(std::move(entry)); else *existing = std::move(entry);
		}
	}
	if (bannedSteamIds != root.end()) {
		for (const auto& item : *bannedSteamIds) {
			if (!item.is_object()) { ++invalid; continue; }
			const auto steamIt = item.find("steamId");
			if (steamIt == item.end() || !steamIt->is_string() || !F4T::SteamId::ParseIndividual(steamIt->get_ref<const std::string&>())) { ++invalid; continue; }
			std::string reason;
			if (const auto node = item.find("reason"); node != item.end() && node->is_string()) {
				reason = node->get<std::string>();
				if (reason.size() > 1024) reason.resize(1024);
			}
			double bannedAt = 0.0;
			if (const auto node = item.find("bannedAt"); node != item.end() && !node->is_boolean() && node->is_number()) {
				try { const auto value = node->get<double>(); if (std::isfinite(value)) bannedAt = value; } catch (...) {}
			}
			SteamBanEntry entry{ .steamId = steamIt->get<std::string>(), .reason = std::move(reason), .bannedAt = bannedAt, .expiresAt = expiresAtOf(item) };
			const auto existing = std::find_if(_steamEntries.begin(), _steamEntries.end(), [&](const SteamBanEntry& candidate) { return candidate.steamId == entry.steamId; });
			if (existing == _steamEntries.end()) _steamEntries.push_back(std::move(entry)); else *existing = std::move(entry);
		}
	}
	if (invalid != 0) throw std::runtime_error("Ban file contains " + std::to_string(invalid) + " invalid entries.");
}

void BanStore::Save(bool& a_committed)
{
	std::filesystem::create_directories(_path.parent_path());
	Json banned = Json::array();
	Json bannedProfiles = Json::array();
	Json bannedSteamIds = Json::array();
	for (const auto& entry : ListSteamIds()) bannedSteamIds.push_back(Json{{ "steamId", entry.steamId }, { "reason", entry.reason }, { "bannedAt", entry.bannedAt }, { "expiresAt", entry.expiresAt }});
	for (const auto& entry : List()) {
		if (!entry.profileId.empty()) bannedProfiles.push_back(Json{{ "profileId", entry.profileId }, { "reason", entry.reason }, { "bannedAt", entry.bannedAt }, { "expiresAt", entry.expiresAt }});
		else banned.push_back(Json{{ "ip", entry.ip }, { "reason", entry.reason }, { "bannedAt", entry.bannedAt }, { "expiresAt", entry.expiresAt }});
	}
	try {
		WriteFileAtomically(_path, Json{ { "banned_ips", std::move(banned) }, { "banned_profiles", std::move(bannedProfiles) }, { "banned_steam_ids", std::move(bannedSteamIds) } }.dump(2) + "\n", &a_committed);
		if (a_committed && _postCommitHook) {
			_postCommitHook();
		}
	} catch (...) {
		if (a_committed) {
			try {
				throw;
			} catch (const std::exception& exception) {
				_persistenceWarning = exception.what();
			} catch (...) {
				_persistenceWarning = "Ban state committed with an unspecified post-commit warning.";
			}
			return;
		}
		throw;
	}
}
}
