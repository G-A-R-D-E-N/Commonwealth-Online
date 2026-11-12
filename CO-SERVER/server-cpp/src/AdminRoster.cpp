#include "AdminRoster.h"

#include "BanStore.h"
#include "AtomicFileWrite.h"
#include "F4TSteamId.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace CommonwealthOnline::Server
{
namespace
{
	bool SameProfile(std::string_view a_left, std::string_view a_right)
	{
		return a_left == a_right;
	}

	bool Has(const std::vector<std::string>& a_list, std::string_view a_profileId)
	{
		return std::any_of(a_list.begin(), a_list.end(), [&](const std::string& entry) { return SameProfile(entry, a_profileId); });
	}
}

AdminRoster::AdminRoster(std::vector<std::string> a_configured, std::filesystem::path a_path, KnownProfile a_known)
	: _configured(std::move(a_configured)), _path(std::move(a_path)), _known(std::move(a_known))
{
	std::error_code ec;
	if (!_path.empty() && std::filesystem::exists(_path, ec)) {
		try {
			std::ifstream in(_path, std::ios::binary);
			const auto root = nlohmann::json::parse(in);
			for (const auto& node : root.at("admins")) {
				if (const auto id = BanStore::NormalizeProfileId(node.get<std::string>()); id && !Has(_granted, *id)) _granted.push_back(*id);
			}
			for (const auto& node : root.value("unbound", nlohmann::json::array())) {
				if (const auto id = BanStore::NormalizeProfileId(node.get<std::string>()); id && !Has(_unbound, *id)) _unbound.push_back(*id);
			}
			const auto steam = root.value("steam", nlohmann::json::object());
			for (const auto& [key, value] : steam.items()) {
				if (const auto id = BanStore::NormalizeProfileId(key); id && F4T::SteamId::ParseIndividual(value.get<std::string>())) _steam[*id] = value.get<std::string>();
			}
		} catch (const std::exception& error) {
			_granted.clear();
			_unbound.clear();
			_steam.clear();
			_loadWarning = "Could not read " + _path.string() + " (" + error.what() + "); no granted admins were loaded.";
			const auto stamp = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
			auto target = std::filesystem::path{ _path.string() + ".quarantine." + stamp };
			for (std::size_t n = 2; std::filesystem::exists(target, ec); ++n) {
				target = std::filesystem::path{ _path.string() + ".quarantine." + stamp + "." + std::to_string(n) };
			}
			std::filesystem::rename(_path, target, ec);
			*_loadWarning += ec ? " The unreadable file could not be moved aside and may be overwritten." : " The unreadable file was moved to " + target.string() + ".";
		}
	}
	if (!_known) return;
	bool changed = false;
	for (const auto* list : { &_configured, &_granted }) {
		for (const auto& id : *list) {
			if (_steam.contains(id) || Has(_unbound, id) || _known(id)) continue;
			_unbound.push_back(id);
			changed = true;
		}
	}
	if (!changed) return;
	try {
		Save();
	} catch (const std::exception& error) {
		_loadWarning = "Could not save " + _path.string() + " (" + error.what() + ").";
	}
}

bool AdminRoster::Contains(std::string_view a_profileId) const
{
	std::scoped_lock lock(_mutex);
	return (!_host.empty() && SameProfile(_host, a_profileId)) || Has(_configured, a_profileId) || Has(_granted, a_profileId);
}

bool AdminRoster::ContainsStored(std::string_view a_profileId) const
{
	std::scoped_lock lock(_mutex);
	return Has(_configured, a_profileId) || Has(_granted, a_profileId);
}

AdminChange AdminRoster::Grant(std::string_view a_profileId, std::string_view a_steamId, bool a_connected)
{
	const auto id = BanStore::NormalizeProfileId(a_profileId);
	if (!id) return AdminChange::Invalid;
	if (!a_steamId.empty() && !F4T::SteamId::ParseIndividual(a_steamId)) return AdminChange::Invalid;
	std::scoped_lock lock(_mutex);
	const bool present = (!_host.empty() && SameProfile(_host, *id)) || Has(_configured, *id) || Has(_granted, *id);
	const bool binds = !a_steamId.empty() || a_connected;
	if (present && !binds) return AdminChange::Unchanged;
	if (!present && _granted.size() >= kMaxGranted) throw std::length_error("The granted admin list is full.");
	const auto granted = _granted;
	const auto unbound = _unbound;
	const auto steam = _steam;
	bool changed = !present;
	if (!present) _granted.push_back(*id);
	if (!a_steamId.empty()) {
		auto& slot = _steam[*id];
		if (slot != a_steamId) {
			slot = std::string{ a_steamId };
			changed = true;
		}
	}
	if (binds) {
		if (const auto found = std::find(_unbound.begin(), _unbound.end(), *id); found != _unbound.end()) {
			_unbound.erase(found);
			changed = true;
		}
	} else if (_known && !_known(*id)) {
		_unbound.push_back(*id);
	}
	if (!changed) return AdminChange::Unchanged;
	try {
		Save();
	} catch (...) {
		_granted = granted;
		_unbound = unbound;
		_steam = steam;
		throw;
	}
	return AdminChange::Changed;
}

bool AdminRoster::Honours(std::string_view a_profileId, std::string_view a_verifiedSteamId) const
{
	std::scoped_lock lock(_mutex);
	if (!_host.empty() && SameProfile(_host, a_profileId)) return true;
	if (const auto bound = _steam.find(std::string{ a_profileId }); bound != _steam.end()) return !a_verifiedSteamId.empty() && bound->second == a_verifiedSteamId;
	return !Has(_unbound, a_profileId);
}

bool AdminRoster::IsUnbound(std::string_view a_profileId) const
{
	std::scoped_lock lock(_mutex);
	return Has(_unbound, a_profileId) && !_steam.contains(std::string{ a_profileId });
}

AdminChange AdminRoster::Revoke(std::string_view a_profileId)
{
	const auto id = BanStore::NormalizeProfileId(a_profileId);
	if (!id) return AdminChange::Invalid;
	std::scoped_lock lock(_mutex);
	if ((!_host.empty() && SameProfile(_host, *id)) || Has(_configured, *id)) return AdminChange::Fixed;
	const auto found = std::find_if(_granted.begin(), _granted.end(), [&](const std::string& entry) { return SameProfile(entry, *id); });
	if (found == _granted.end()) return AdminChange::Unchanged;
	const auto granted = _granted;
	const auto unbound = _unbound;
	const auto steam = _steam;
	_granted.erase(found);
	if (const auto stale = std::find(_unbound.begin(), _unbound.end(), *id); stale != _unbound.end()) _unbound.erase(stale);
	_steam.erase(*id);
	try {
		Save();
	} catch (...) {
		_granted = granted;
		_unbound = unbound;
		_steam = steam;
		throw;
	}
	return AdminChange::Changed;
}

void AdminRoster::SetHost(std::string_view a_profileId)
{
	std::scoped_lock lock(_mutex);
	_host = std::string{ a_profileId };
}

void AdminRoster::NoteHost(bool a_isHost, std::string_view a_profileId)
{
	if (a_isHost) SetHost(a_profileId);
}

nlohmann::json AdminRoster::List() const
{
	std::scoped_lock lock(_mutex);
	auto admins = nlohmann::json::array();
	const auto add = [&](std::string_view a_id, std::string_view a_source) {
		admins.push_back(nlohmann::json{ { "profileId", a_id }, { "source", a_source } });
		if (const auto bound = _steam.find(std::string{ a_id }); bound != _steam.end()) admins.back()["steamId"] = bound->second;
		else if (Has(_unbound, a_id)) admins.back()["unbound"] = true;
	};
	if (!_host.empty()) add(_host, "host");
	for (const auto& id : _configured) add(id, "config");
	for (const auto& id : _granted) add(id, "granted");
	return nlohmann::json{ { "admins", std::move(admins) } };
}

std::optional<std::string> AdminRoster::TakeLoadWarning()
{
	std::scoped_lock lock(_mutex);
	return std::exchange(_loadWarning, std::nullopt);
}

void AdminRoster::Save() const
{
	if (_path.empty()) return;
	std::error_code ec;
	if (_path.has_parent_path()) std::filesystem::create_directories(_path.parent_path(), ec);
	WriteFileAtomically(_path, nlohmann::json{ { "version", 1 }, { "admins", _granted }, { "unbound", _unbound }, { "steam", _steam } }.dump(2));
}

void InstallAdminRosterHooks(AdminCommandHooks& a_hooks, AdminRoster& a_roster, ProfileOfPlayer a_profileOfPlayer, AdminAnnounce a_announce)
{
	a_hooks.admins = [&a_roster] { return a_roster.List(); };
	a_hooks.grantAdmin = [&a_roster, profileOfPlayer = std::move(a_profileOfPlayer), announce = a_announce](std::optional<std::uint32_t> a_playerId, std::string_view a_profileId, std::string_view a_steamId) {
		std::string profileId{ a_profileId };
		const bool connected = profileId.empty() && a_playerId;
		if (connected) {
			const auto found = profileOfPlayer(*a_playerId);
			if (!found) return AdminActionResult{ false, "Player " + std::to_string(*a_playerId) + " is not connected.", nullptr };
			profileId = *found;
		}
		try {
			const auto change = a_roster.Grant(profileId, a_steamId, connected);
			if (change == AdminChange::Invalid) return AdminActionResult{ false, "Not a valid profile id or Steam ID.", nullptr };
			if (change == AdminChange::Unchanged) return AdminActionResult{ false, "Profile " + profileId + " is already an admin.", nullptr };
			announce(profileId);
			std::string message = "Granted admin to " + profileId + ". A player who is online may need to rejoin.";
			if (a_roster.IsUnbound(profileId)) {
				message += " WARNING: this profile has not joined this server yet, so the grant is not active. Re-run admin_grant with the steamId of that player, or grant it by playerId while they are connected.";
				std::cerr << "Admin grant for " << profileId << " is inactive until it is bound to a Steam ID or a connected player.\n";
			}
			return AdminActionResult{ true, std::move(message), nlohmann::json{ { "profileId", profileId } } };
		} catch (const std::exception& error) {
			return AdminActionResult{ false, error.what(), nullptr };
		}
	};
	a_hooks.revokeAdmin = [&a_roster, announce = std::move(a_announce)](std::string_view a_profileId) {
		try {
			const std::string profileId{ a_profileId };
			const auto change = a_roster.Revoke(profileId);
			if (change == AdminChange::Invalid) return AdminActionResult{ false, "Not a valid profile id.", nullptr };
			if (change == AdminChange::Fixed) return AdminActionResult{ false, "Profile " + profileId + " is an admin through the server config or is hosting this session, so it cannot be revoked here.", nullptr };
			if (change == AdminChange::Unchanged) return AdminActionResult{ false, "Profile " + profileId + " was not granted admin.", nullptr };
			announce(profileId);
			return AdminActionResult{ true, "Revoked admin from " + profileId + ".", nlohmann::json{ { "profileId", profileId } } };
		} catch (const std::exception& error) {
			return AdminActionResult{ false, error.what(), nullptr };
		}
	};
}
}
