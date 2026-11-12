#include "AdminRoster.h"

#include "BanStore.h"
#include "AtomicFileWrite.h"

#include <algorithm>
#include <fstream>
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

AdminRoster::AdminRoster(std::vector<std::string> a_configured, std::filesystem::path a_path)
	: _configured(std::move(a_configured)), _path(std::move(a_path))
{
	std::error_code ec;
	if (_path.empty() || !std::filesystem::exists(_path, ec)) return;
	try {
		std::ifstream in(_path, std::ios::binary);
		const auto root = nlohmann::json::parse(in);
		for (const auto& node : root.at("admins")) {
			if (const auto id = BanStore::NormalizeProfileId(node.get<std::string>()); id && !Has(_granted, *id)) _granted.push_back(*id);
		}
	} catch (const std::exception& error) {
		_granted.clear();
		_loadWarning = "Could not read " + _path.string() + " (" + error.what() + "); no granted admins were loaded.";
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

AdminChange AdminRoster::Grant(std::string_view a_profileId)
{
	const auto id = BanStore::NormalizeProfileId(a_profileId);
	if (!id) return AdminChange::Invalid;
	std::scoped_lock lock(_mutex);
	if ((!_host.empty() && SameProfile(_host, *id)) || Has(_configured, *id) || Has(_granted, *id)) return AdminChange::Unchanged;
	if (_granted.size() >= kMaxGranted) throw std::length_error("The granted admin list is full.");
	_granted.push_back(*id);
	try {
		Save();
	} catch (...) {
		_granted.pop_back();
		throw;
	}
	return AdminChange::Changed;
}

AdminChange AdminRoster::Revoke(std::string_view a_profileId)
{
	const auto id = BanStore::NormalizeProfileId(a_profileId);
	if (!id) return AdminChange::Invalid;
	std::scoped_lock lock(_mutex);
	if ((!_host.empty() && SameProfile(_host, *id)) || Has(_configured, *id)) return AdminChange::Fixed;
	const auto found = std::find_if(_granted.begin(), _granted.end(), [&](const std::string& entry) { return SameProfile(entry, *id); });
	if (found == _granted.end()) return AdminChange::Unchanged;
	const auto removed = *found;
	_granted.erase(found);
	try {
		Save();
	} catch (...) {
		_granted.push_back(removed);
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
	WriteFileAtomically(_path, nlohmann::json{ { "version", 1 }, { "admins", _granted } }.dump(2));
}

void InstallAdminRosterHooks(AdminCommandHooks& a_hooks, AdminRoster& a_roster, ProfileOfPlayer a_profileOfPlayer, AdminAnnounce a_announce)
{
	a_hooks.admins = [&a_roster] { return a_roster.List(); };
	a_hooks.grantAdmin = [&a_roster, profileOfPlayer = std::move(a_profileOfPlayer), announce = a_announce](std::optional<std::uint32_t> a_playerId, std::string_view a_profileId) {
		std::string profileId{ a_profileId };
		if (profileId.empty() && a_playerId) {
			const auto found = profileOfPlayer(*a_playerId);
			if (!found) return AdminActionResult{ false, "Player " + std::to_string(*a_playerId) + " is not connected.", nullptr };
			profileId = *found;
		}
		try {
			const auto change = a_roster.Grant(profileId);
			if (change == AdminChange::Invalid) return AdminActionResult{ false, "Not a valid profile id.", nullptr };
			if (change == AdminChange::Unchanged) return AdminActionResult{ false, "Profile " + profileId + " is already an admin.", nullptr };
			announce(profileId);
			return AdminActionResult{ true, "Granted admin to " + profileId + ". A player who is online may need to rejoin.", nlohmann::json{ { "profileId", profileId } } };
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
