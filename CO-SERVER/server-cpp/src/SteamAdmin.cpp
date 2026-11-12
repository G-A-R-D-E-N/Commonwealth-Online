#include "SteamAdmin.h"

#include "F4TConnectionPolicy.h"
#include "ServerTextHelpers.h"

#include <algorithm>
#include <exception>
#include <iostream>
#include <utility>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using TextHelpers::Ipv4Text;
	}

	SteamAdmin::SteamAdmin(SteamAdminDependencies a_dependencies)
		: _dependencies(std::move(a_dependencies))
	{
	}

	std::optional<SteamAdmin> SteamAdmin::Create(const nlohmann::json& a_stored, SteamAdminDependencies a_dependencies)
	{
		try {
			SteamAdmin admin{ std::move(a_dependencies) };
			admin._identities = SteamIdentityRegistry{ a_stored };
			return admin;
		} catch (const std::exception& exception) {
			std::cerr << "Failed to load Steam identity records: " << exception.what() << '\n';
			return std::nullopt;
		}
	}

	double SteamAdmin::Now() const
	{
		return _dependencies.unixTimeSeconds();
	}

	std::string SteamAdmin::WithWarning(std::string a_message) const
	{
		return _dependencies.withPersistenceWarning(std::move(a_message), _dependencies.bans->TakePersistenceWarning());
	}

	void SteamAdmin::Persist()
	{
		if (!_dependencies.saveIdentities) return;
		try {
			_dependencies.saveIdentities(_identities.ToJson());
		} catch (const std::exception& exception) {
			std::cerr << "Failed to save Steam identity records: " << exception.what() << '\n';
		}
	}

	void SteamAdmin::Sweep(double a_now)
	{
		const auto removed = _identities.Sweep(a_now, [this](const SteamIdentityRecord& a_record) {
			return _dependencies.bans->GetSteamId(a_record.steamId).has_value()
				|| _dependencies.bans->GetProfile(a_record.profileId).has_value();
		});
		if (removed != 0) {
			std::cout << "Steam identity retention removed " << removed << " records last seen over 365 days ago." << std::endl;
			Persist();
		}
	}

	void SteamAdmin::SweepIfDue(double a_now)
	{
		if (a_now < _nextSweepUnix) return;
		Sweep(a_now);
		_nextSweepUnix = a_now + 3600.0;
	}

	std::vector<std::string> SteamAdmin::ProfileSteamIds(std::string_view a_profileId) const
	{
		std::vector<std::string> steamIds;
		for (const auto& record : _identities.ForProfile(a_profileId)) {
			if (record.steamId.empty() || (record.source != SteamIdSource::SteamTunnel && record.source != SteamIdSource::WebApi)) continue;
			if (std::find(steamIds.begin(), steamIds.end(), record.steamId) == steamIds.end()) steamIds.push_back(record.steamId);
		}
		return steamIds;
	}

	void SteamAdmin::RecordIdentity(
		std::uint32_t a_connectionId,
		std::string_view a_profileId,
		std::string_view a_nickname,
		std::string_view a_steamId,
		SteamIdSource a_source,
		double a_now)
	{
		const auto seen = _identities.Observe(a_profileId, a_steamId, a_source, a_nickname, a_now);
		if (!seen.stored) {
			std::cerr << "Steam identity for connection " << a_connectionId << " was not recorded (history full or entry invalid).\n";
			return;
		}
		for (const auto& flag : seen.raised) {
			std::cerr << "Steam identity on connection " << a_connectionId << " flagged for review ("
			          << SteamIdentityFlagKindName(flag.kind) << "). See steam_flags on the admin port.\n";
		}
		Persist();
	}

	std::size_t SteamAdmin::EndSessions(
		const std::function<bool(const SteamAdminConnection&)>& a_match,
		std::string_view a_code,
		std::string_view a_reason)
	{
		std::vector<std::uint32_t> targets;
		for (const auto& connection : _dependencies.connections()) {
			if (a_match(connection)) targets.push_back(connection.connectionId);
		}
		for (const auto connectionId : targets) {
			_dependencies.endSession(connectionId, a_code, a_reason);
		}
		return targets.size();
	}

	std::string SteamAdmin::AddressKey(const SteamAdminConnection& a_connection)
	{
		if (a_connection.remoteAddress.empty()) {
			return Ipv4Text(a_connection.remoteIpv4);
		}
		return BanStore::NormalizeAddress(a_connection.remoteAddress).value_or(a_connection.remoteAddress);
	}

	std::size_t SteamAdmin::EndSessionsForIp(std::string_view a_ip, std::string_view a_code, std::string_view a_reason)
	{
		return EndSessions(
			[a_ip](const SteamAdminConnection& a_connection) { return AddressKey(a_connection) == a_ip; },
			a_code,
			a_reason);
	}

	std::size_t SteamAdmin::EndSessionsForProfile(std::string_view a_profileId, std::string_view a_code, std::string_view a_reason)
	{
		return EndSessions(
			[a_profileId](const SteamAdminConnection& a_connection) { return a_connection.profileId == a_profileId; },
			a_code,
			a_reason);
	}

	std::size_t SteamAdmin::EndSessionsForSteamId(std::string_view a_steamId, std::string_view a_code, std::string_view a_reason)
	{
		return EndSessions(
			[a_steamId](const SteamAdminConnection& a_connection) { return a_connection.steamId == a_steamId; },
			a_code,
			a_reason);
	}

	AdminActionResult SteamAdmin::BanSteamIdResult(std::string_view a_steamId, std::string_view a_reason)
	{
		try {
			const auto entry = _dependencies.bans->BanSteamId(a_steamId, a_reason, Now());
			const auto ended = EndSessionsForSteamId(entry.steamId, "steam_banned", entry.reason);
			return AdminActionResult{
				true,
				WithWarning("Banned the Steam ID."),
				nlohmann::json{ { "steamId", entry.steamId }, { "reason", entry.reason }, { "bannedAt", entry.bannedAt }, { "sessionsEnded", ended } }
			};
		} catch (const std::exception& exception) {
			return AdminActionResult{ false, exception.what(), nullptr };
		}
	}

	void SteamAdmin::InstallHooks(AdminCommandHooks& a_hooks)
	{
		a_hooks.bans = [this] {
			nlohmann::json bans = nlohmann::json::array();
			for (const auto& entry : _dependencies.bans->List()) {
				bans.push_back(nlohmann::json{
					{ "ip", entry.ip }, { "profileId", entry.profileId }, { "reason", entry.reason }, { "bannedAt", entry.bannedAt }
				});
			}
			nlohmann::json steamBans = nlohmann::json::array();
			for (const auto& entry : _dependencies.bans->ListSteamIds()) {
				steamBans.push_back(nlohmann::json{ { "steamId", entry.steamId }, { "reason", entry.reason }, { "bannedAt", entry.bannedAt } });
			}
			return nlohmann::json{ { "bans", std::move(bans) }, { "steamBans", std::move(steamBans) } };
		};
		a_hooks.banPlayer = [this](std::uint32_t a_playerId, std::string_view a_reason) {
			std::optional<std::string> address;
			bool hasIp{};
			bool loopback{};
			std::string profileId;
			std::string steamId;
			for (const auto& connection : _dependencies.connections()) {
				if (connection.active && connection.playerId == a_playerId) {
					address = AddressKey(connection);
					hasIp = connection.remoteIpv4 != 0 || !connection.remoteAddress.empty();
					loopback = F4T::ConnectionPolicy::IsLoopbackHost(connection.remoteAddress.empty() ? Ipv4Text(connection.remoteIpv4) : connection.remoteAddress);
					profileId = connection.profileId;
					steamId = connection.steamId;
					break;
				}
			}
			if (!address) {
				return AdminActionResult{ false, "No connected player with id " + std::to_string(a_playerId), nullptr };
			}
			const bool banAddress = hasIp && !loopback;
			if (!banAddress && profileId.empty() && steamId.empty()) {
				return AdminActionResult{ false, "Player " + std::to_string(a_playerId) + " has no profile, Steam ID or bannable IP address.", nullptr };
			}
			std::string message;
			const auto addSentence = [&message](const std::string& a_sentence) { message += (message.empty() ? "" : " ") + a_sentence; };
			nlohmann::json data = nlohmann::json::object();
			std::size_t ended = 0;
			try {
				if (banAddress) {
					const auto entry = _dependencies.bans->Ban(*address, a_reason, Now());
					ended += EndSessionsForIp(entry.ip, "banned", entry.reason);
					addSentence("Banned IP " + entry.ip + ".");
					data["ip"] = entry.ip;
					data["reason"] = entry.reason;
					data["bannedAt"] = entry.bannedAt;
				} else {
					addSentence("IP not banned (local or unknown address).");
				}
				if (!profileId.empty()) {
					const auto profile = _dependencies.bans->BanProfile(profileId, a_reason, Now());
					ended += EndSessionsForProfile(profile.profileId, "profile_banned", profile.reason);
					addSentence("Banned profile " + profile.profileId + ".");
					data["profileId"] = profile.profileId;
				}
				if (!steamId.empty()) {
					const auto steam = _dependencies.bans->BanSteamId(steamId, a_reason, Now());
					ended += EndSessionsForSteamId(steam.steamId, "steam_banned", steam.reason);
					addSentence("Banned Steam ID " + steam.steamId + ".");
					data["steamId"] = steam.steamId;
				}
				data["sessionsEnded"] = ended;
				return AdminActionResult{ true, WithWarning(message), std::move(data) };
			} catch (const std::exception& exception) {
				return AdminActionResult{ false, message.empty() ? std::string{ exception.what() } : std::string{ exception.what() } + " Already applied: " + message, nullptr };
			}
		};
		a_hooks.banIp = [this](std::string_view a_ip, std::string_view a_reason) {
			try {
				const auto entry = _dependencies.bans->Ban(a_ip, a_reason, Now());
				const auto ended = EndSessionsForIp(entry.ip, "banned", entry.reason);
				return AdminActionResult{
					true,
					WithWarning("Banned IP " + entry.ip + "."),
					nlohmann::json{
						{ "ip", entry.ip }, { "reason", entry.reason }, { "bannedAt", entry.bannedAt }, { "sessionsEnded", ended }
					}
				};
			} catch (const std::exception& exception) {
				return AdminActionResult{ false, exception.what(), nullptr };
			}
		};
		a_hooks.unban = [this](std::string_view a_ip) {
			const auto normalized = BanStore::NormalizeAddress(a_ip);
			if (!normalized) {
				return AdminActionResult{ false, "Invalid IP address: '" + std::string{ a_ip } + "'.", nullptr };
			}
			try {
				if (!_dependencies.bans->Unban(*normalized)) {
					return AdminActionResult{ false, "IP " + *normalized + " is not banned.", nullptr };
				}
				return AdminActionResult{
					true,
					WithWarning("Unbanned IP " + *normalized + "."),
					nlohmann::json{ { "ip", *normalized } }
				};
			} catch (const std::exception& exception) {
				return AdminActionResult{ false, exception.what(), nullptr };
			}
		};
		a_hooks.banProfile = [this](std::string_view a_profileId, std::string_view a_reason) {
			try {
				const auto entry = _dependencies.bans->BanProfile(a_profileId, a_reason, Now());
				const auto ended = EndSessionsForProfile(entry.profileId, "profile_banned", entry.reason);
				std::size_t steamEnded = 0;
				std::vector<std::string> failedSteamIds;
				for (const auto& steamId : ProfileSteamIds(entry.profileId)) {
					try {
						(void)_dependencies.bans->BanSteamId(steamId, entry.reason, Now());
					} catch (const std::exception&) {
						failedSteamIds.push_back(steamId);
						continue;
					}
					steamEnded += EndSessions(
						[&steamId, &entry](const SteamAdminConnection& a_connection) { return a_connection.steamId == steamId && a_connection.profileId != entry.profileId; },
						"steam_banned",
						entry.reason);
				}
				std::string message = "Banned profile " + entry.profileId + ".";
				for (const auto& steamId : failedSteamIds) message += " Failed to ban Steam ID " + steamId + ".";
				return AdminActionResult{
					true,
					WithWarning(message),
					nlohmann::json{ { "profileId", entry.profileId }, { "reason", entry.reason }, { "bannedAt", entry.bannedAt }, { "sessionsEnded", ended + steamEnded }, { "steamIdsFailed", failedSteamIds } }
				};
			} catch (const std::exception& exception) {
				return AdminActionResult{ false, exception.what(), nullptr };
			}
		};
		a_hooks.unbanProfile = [this](std::string_view a_profileId) {
			try {
				if (!_dependencies.bans->UnbanProfile(a_profileId)) {
					return AdminActionResult{ false, "Profile " + std::string{ a_profileId } + " is not banned.", nullptr };
				}
				std::string message = "Unbanned profile " + std::string{ a_profileId } + ".";
				nlohmann::json stillBanned = nlohmann::json::array();
				for (const auto& steamId : ProfileSteamIds(a_profileId)) {
					if (!_dependencies.bans->GetSteamId(steamId)) continue;
					stillBanned.push_back(steamId);
					message += " Steam ID " + steamId + " is still banned; use steam_unban to lift it.";
				}
				return AdminActionResult{
					true,
					WithWarning(message),
					nlohmann::json{ { "profileId", a_profileId }, { "steamIdsStillBanned", std::move(stillBanned) } }
				};
			} catch (const std::exception& exception) {
				return AdminActionResult{ false, exception.what(), nullptr };
			}
		};
		a_hooks.steamLookup = [this](std::string_view a_steamId, std::string_view a_profileId) {
			const bool bySteamId = !a_steamId.empty();
			const auto found = bySteamId ? _identities.ForSteamId(a_steamId) : _identities.ForProfile(a_profileId);
			nlohmann::json records = nlohmann::json::array();
			std::vector<std::string> steamIds;
			if (bySteamId) steamIds.emplace_back(a_steamId);
			for (const auto& record : found) {
				records.push_back(SteamIdentityRegistry::RecordJson(record));
				if (std::find(steamIds.begin(), steamIds.end(), record.steamId) == steamIds.end()) steamIds.push_back(record.steamId);
			}
			nlohmann::json steamBans = nlohmann::json::array();
			for (const auto& steamId : steamIds) {
				if (const auto ban = _dependencies.bans->GetSteamId(steamId)) {
					steamBans.push_back(nlohmann::json{ { "steamId", ban->steamId }, { "reason", ban->reason }, { "bannedAt", ban->bannedAt } });
				}
			}
			nlohmann::json flags = nlohmann::json::array();
			for (const auto& flag : _identities.Flags()) {
				const bool related = bySteamId
					? flag.steamId == a_steamId || (flag.kind == SteamIdentityFlagKind::MultipleSteamIds && flag.otherId == a_steamId)
					: flag.profileId == a_profileId || (flag.kind == SteamIdentityFlagKind::SharedSteamId && flag.otherId == a_profileId);
				if (related) flags.push_back(SteamIdentityRegistry::FlagJson(flag));
			}
			nlohmann::json connected = nlohmann::json::array();
			for (const auto& connection : _dependencies.connections()) {
				if (connection.active && (bySteamId ? connection.steamId == a_steamId : connection.profileId == a_profileId)) {
					connected.push_back(connection.playerId);
				}
			}
			return AdminActionResult{
				true,
				"Found " + std::to_string(found.size()) + " Steam identity records.",
				nlohmann::json{ { "records", std::move(records) }, { "steamBans", std::move(steamBans) }, { "flags", std::move(flags) }, { "connectedPlayerIds", std::move(connected) } }
			};
		};
		a_hooks.banSteamId = [this](std::string_view a_steamId, std::string_view a_reason) {
			return BanSteamIdResult(a_steamId, a_reason);
		};
		a_hooks.banPlayerSteamId = [this](std::uint32_t a_playerId, std::string_view a_reason) {
			for (const auto& connection : _dependencies.connections()) {
				if (!connection.active || connection.playerId != a_playerId) continue;
				if (connection.steamId.empty()) {
					return AdminActionResult{ false, "Player " + std::to_string(a_playerId) + " has no verified Steam ID.", nullptr };
				}
				const auto steamId = connection.steamId;
				return BanSteamIdResult(steamId, a_reason);
			}
			return AdminActionResult{ false, "No connected player with id " + std::to_string(a_playerId), nullptr };
		};
		a_hooks.unbanSteamId = [this](std::string_view a_steamId) {
			try {
				if (!_dependencies.bans->UnbanSteamId(a_steamId)) return AdminActionResult{ false, "That Steam ID is not banned.", nullptr };
				return AdminActionResult{
					true,
					WithWarning("Unbanned the Steam ID."),
					nlohmann::json{ { "steamId", a_steamId } }
				};
			} catch (const std::exception& exception) {
				return AdminActionResult{ false, exception.what(), nullptr };
			}
		};
		a_hooks.steamFlags = [this] {
			nlohmann::json flags = nlohmann::json::array();
			for (const auto& flag : _identities.Flags()) flags.push_back(SteamIdentityRegistry::FlagJson(flag));
			return nlohmann::json{ { "total", flags.size() }, { "flags", std::move(flags) } };
		};
		a_hooks.clearSteamFlags = [this](std::string_view a_profileId, std::string_view a_steamId) {
			const auto cleared = _identities.ClearFlags(a_profileId, a_steamId);
			if (cleared == 0) return AdminActionResult{ false, "No open flags for that profile and Steam ID.", nullptr };
			Persist();
			return AdminActionResult{ true, "Cleared " + std::to_string(cleared) + " Steam identity flags.", nlohmann::json{ { "cleared", cleared } } };
		};
	}
}
