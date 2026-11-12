#include "BanStore.h"

#include "F4TConnectionPolicy.h"
#include "F4TSteamId.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace CommonwealthOnline::Server
{
	namespace
	{
		bool IsBanLive(double a_expiresAt)
		{
			const auto now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
			return a_expiresAt == 0.0 || a_expiresAt > now;
		}

		bool ExistingOutlasts(double a_existingExpiresAt, double a_expiresAt)
		{
			return a_expiresAt != 0.0 && (a_existingExpiresAt == 0.0 || a_existingExpiresAt >= a_expiresAt);
		}

		std::string Trim(std::string_view a_value)
		{
			std::size_t begin = 0;
			std::size_t end = a_value.size();
			while (begin < end && std::isspace(static_cast<unsigned char>(a_value[begin])) != 0) {
				++begin;
			}
			while (end > begin && std::isspace(static_cast<unsigned char>(a_value[end - 1])) != 0) {
				--end;
			}
			return std::string{ a_value.substr(begin, end - begin) };
		}

	}

	BanStore::BanStore(std::filesystem::path a_path, PostCommitHook a_postCommitHook) :
		_path(std::filesystem::absolute(std::move(a_path))),
		_postCommitHook(std::move(a_postCommitHook))
	{
		Load();
	}

	std::optional<std::string> BanStore::TakePersistenceWarning()
	{
		auto warning = std::move(_persistenceWarning);
		_persistenceWarning.reset();
		return warning;
	}

	std::optional<BanEntry> BanStore::Get(std::string_view a_ip) const
	{
		const auto normalized = NormalizeAddress(a_ip);
		if (!normalized) {
			return std::nullopt;
		}
		const auto found = std::find_if(_entries.begin(), _entries.end(), [&](const BanEntry& entry) {
			return entry.profileId.empty() && entry.ip == *normalized && IsBanLive(entry.expiresAt);
		});
		return found == _entries.end() ? std::nullopt : std::optional{ *found };
	}

	std::optional<BanEntry> BanStore::GetProfile(std::string_view a_profileId) const
	{
		const auto normalized = NormalizeProfileId(a_profileId);
		if (!normalized) return std::nullopt;
		const auto found = std::find_if(_entries.begin(), _entries.end(), [&](const BanEntry& entry) {
			return !entry.profileId.empty() && entry.profileId == *normalized && IsBanLive(entry.expiresAt);
		});
		return found == _entries.end() ? std::nullopt : std::optional{ *found };
	}

	std::vector<BanEntry> BanStore::List() const
	{
		auto entries = _entries;
		std::erase_if(entries, [](const BanEntry& entry) { return !IsBanLive(entry.expiresAt); });
		std::sort(entries.begin(), entries.end(), [](const BanEntry& left, const BanEntry& right) {
			if (left.profileId.empty() != right.profileId.empty()) return left.profileId.empty();
			return left.profileId.empty() ? left.ip < right.ip : left.profileId < right.profileId;
		});
		return entries;
	}

	BanEntry BanStore::Ban(std::string_view a_ip, std::string_view a_reason, double a_bannedAt, double a_expiresAt)
	{
		const auto normalized = NormalizeAddress(a_ip);
		if (!normalized) {
			throw std::invalid_argument("Invalid IP address: '" + std::string{ a_ip } + "'.");
		}
		if (!std::isfinite(a_bannedAt)) {
			throw std::invalid_argument("Ban timestamp must be finite.");
		}
		if (!std::isfinite(a_expiresAt)) {
			throw std::invalid_argument("Ban expiry must be finite.");
		}
		const auto reasonLength = (std::min)(a_reason.size(), static_cast<std::size_t>(1024));
		BanEntry entry{ .ip = *normalized, .profileId = {}, .reason = std::string{ a_reason.substr(0, reasonLength) }, .bannedAt = a_bannedAt, .expiresAt = a_expiresAt };
		const auto found = std::find_if(_entries.begin(), _entries.end(), [&](const BanEntry& existing) {
			return existing.profileId.empty() && existing.ip == entry.ip;
		});
		if (found != _entries.end() && ExistingOutlasts(found->expiresAt, a_expiresAt)) {
			return *found;
		}
		// The entry list is restored if the write fails, so an admin who is told
		// the ban failed is not left with a ban that only exists in memory.
		auto snapshot = _entries;
		if (found == _entries.end()) {
			_entries.push_back(entry);
		} else {
			*found = entry;
		}
		bool committed = false;
		try {
			Save(committed);
		} catch (...) {
			if (!committed) {
				_entries = std::move(snapshot);
			}
			throw;
		}
		return entry;
	}

	BanEntry BanStore::BanProfile(std::string_view a_profileId, std::string_view a_reason, double a_bannedAt, double a_expiresAt)
	{
		const auto normalized = NormalizeProfileId(a_profileId);
		if (!normalized) throw std::invalid_argument("Profile ban requires a valid profileId.");
		if (!std::isfinite(a_bannedAt)) throw std::invalid_argument("Ban timestamp must be finite.");
		if (!std::isfinite(a_expiresAt)) throw std::invalid_argument("Ban expiry must be finite.");
		const auto reasonLength = (std::min)(a_reason.size(), static_cast<std::size_t>(1024));
		BanEntry entry{ .ip = {}, .profileId = *normalized, .reason = std::string{ a_reason.substr(0, reasonLength) }, .bannedAt = a_bannedAt, .expiresAt = a_expiresAt };
		const auto found = std::find_if(_entries.begin(), _entries.end(), [&](const BanEntry& existing) {
			return !existing.profileId.empty() && existing.profileId == entry.profileId;
		});
		if (found != _entries.end() && ExistingOutlasts(found->expiresAt, a_expiresAt)) return *found;
		auto snapshot = _entries;
		if (found == _entries.end()) _entries.push_back(entry);
		else *found = entry;
		bool committed = false;
		try { Save(committed); } catch (...) {
			if (!committed) _entries = std::move(snapshot);
			throw;
		}
		return entry;
	}

	bool BanStore::UnbanProfile(std::string_view a_profileId)
	{
		const auto normalized = NormalizeProfileId(a_profileId);
		if (!normalized) return false;
		auto snapshot = _entries;
		const auto oldSize = _entries.size();
		std::erase_if(_entries, [&](const BanEntry& entry) { return entry.profileId == *normalized; });
		if (_entries.size() == oldSize) return false;
		bool committed = false;
		try { Save(committed); } catch (...) {
			if (!committed) _entries = std::move(snapshot);
			throw;
		}
		return true;
	}

	bool BanStore::Unban(std::string_view a_ip)
	{
		const auto normalized = NormalizeAddress(a_ip);
		if (!normalized) {
			return false;
		}
		auto snapshot = _entries;
		const auto oldSize = _entries.size();
		std::erase_if(_entries, [&](const BanEntry& entry) { return entry.profileId.empty() && entry.ip == *normalized; });
		if (_entries.size() == oldSize) {
			return false;
		}
		bool committed = false;
		try {
			Save(committed);
		} catch (...) {
			if (!committed) {
				_entries = std::move(snapshot);
			}
			throw;
		}
		return true;
	}

	std::optional<SteamBanEntry> BanStore::GetSteamId(std::string_view a_steamId) const
	{
		const auto found = std::find_if(_steamEntries.begin(), _steamEntries.end(), [&](const SteamBanEntry& entry) {
			return entry.steamId == a_steamId;
		});
		return found == _steamEntries.end() || !IsBanLive(found->expiresAt) ? std::nullopt : std::optional{ *found };
	}

	std::vector<SteamBanEntry> BanStore::ListSteamIds() const
	{
		auto entries = _steamEntries;
		std::erase_if(entries, [](const SteamBanEntry& entry) { return !IsBanLive(entry.expiresAt); });
		std::sort(entries.begin(), entries.end(), [](const SteamBanEntry& left, const SteamBanEntry& right) {
			return left.steamId < right.steamId;
		});
		return entries;
	}

	SteamBanEntry BanStore::BanSteamId(std::string_view a_steamId, std::string_view a_reason, double a_bannedAt, double a_expiresAt)
	{
		if (!F4T::SteamId::ParseIndividual(a_steamId)) throw std::invalid_argument("Steam ban requires an individual SteamID64.");
		if (!std::isfinite(a_bannedAt)) throw std::invalid_argument("Ban timestamp must be finite.");
		if (!std::isfinite(a_expiresAt)) throw std::invalid_argument("Ban expiry must be finite.");
		const auto reasonLength = (std::min)(a_reason.size(), static_cast<std::size_t>(1024));
		SteamBanEntry entry{ .steamId = std::string{ a_steamId }, .reason = std::string{ a_reason.substr(0, reasonLength) }, .bannedAt = a_bannedAt, .expiresAt = a_expiresAt };
		const auto found = std::find_if(_steamEntries.begin(), _steamEntries.end(), [&](const SteamBanEntry& existing) {
			return existing.steamId == entry.steamId;
		});
		if (found != _steamEntries.end() && ExistingOutlasts(found->expiresAt, a_expiresAt)) return *found;
		auto snapshot = _steamEntries;
		if (found == _steamEntries.end()) _steamEntries.push_back(entry);
		else *found = entry;
		bool committed = false;
		try { Save(committed); } catch (...) {
			if (!committed) _steamEntries = std::move(snapshot);
			throw;
		}
		return entry;
	}

	bool BanStore::UnbanSteamId(std::string_view a_steamId)
	{
		auto snapshot = _steamEntries;
		const auto oldSize = _steamEntries.size();
		std::erase_if(_steamEntries, [&](const SteamBanEntry& entry) { return entry.steamId == a_steamId; });
		if (_steamEntries.size() == oldSize) return false;
		bool committed = false;
		try { Save(committed); } catch (...) {
			if (!committed) _steamEntries = std::move(snapshot);
			throw;
		}
		return true;
	}

	std::optional<std::string> BanStore::NormalizeIpv4(std::string_view a_ip)
	{
		const auto text = Trim(a_ip);
		std::array<unsigned int, 4> octets{};
		std::size_t begin = 0;
		for (std::size_t part = 0; part < octets.size(); ++part) {
			const auto end = part + 1 == octets.size() ? text.size() : text.find('.', begin);
			if (end == std::string::npos || end == begin) {
				return std::nullopt;
			}
			unsigned int value = 0;
			const auto result = std::from_chars(text.data() + begin, text.data() + end, value);
			if (result.ec != std::errc{} || result.ptr != text.data() + end || value > 255) {
				return std::nullopt;
			}
			octets[part] = value;
			begin = end + 1;
		}
		if (begin != text.size() + 1) {
			return std::nullopt;
		}
		return std::to_string(octets[0]) + "." + std::to_string(octets[1]) + "." +
			std::to_string(octets[2]) + "." + std::to_string(octets[3]);
	}

	std::optional<std::string> BanStore::NormalizeAddress(std::string_view a_address)
	{
		if (auto ipv4 = NormalizeIpv4(a_address)) {
			return ipv4;
		}
		auto text = Trim(a_address);
		{
			std::string host = text;
			std::ranges::transform(host, host.begin(), [](unsigned char a_char) { return static_cast<char>(std::tolower(a_char)); });
			if (host.size() > 2 && host.front() == '[' && host.back() == ']') {
				host = host.substr(1, host.size() - 2);
			}
			for (const std::string_view mappedPrefix : { std::string_view{ "::ffff:" }, std::string_view{ "0:0:0:0:0:ffff:" } }) {
				if (!host.starts_with(mappedPrefix)) {
					continue;
				}
				const std::string_view tail = std::string_view{ host }.substr(mappedPrefix.size());
				if (tail.find('.') != std::string_view::npos) {
					return NormalizeIpv4(tail);
				}
				const auto colon = tail.find(':');
				if (colon == std::string_view::npos || colon == 0 || colon > 4 || tail.size() - colon - 1 == 0 || tail.size() - colon - 1 > 4) {
					return std::nullopt;
				}
				unsigned int high = 0;
				unsigned int low = 0;
				const auto highEnd = tail.data() + colon;
				const auto highResult = std::from_chars(tail.data(), highEnd, high, 16);
				const auto lowResult = std::from_chars(highEnd + 1, tail.data() + tail.size(), low, 16);
				if (highResult.ec != std::errc{} || highResult.ptr != highEnd || lowResult.ec != std::errc{} || lowResult.ptr != tail.data() + tail.size()) {
					return std::nullopt;
				}
				return std::to_string(high >> 8) + "." + std::to_string(high & 0xFFU) + "." + std::to_string(low >> 8) + "." + std::to_string(low & 0xFFU);
			}
		}
		if (text.size() > 3 && text.ends_with("/64")) {
			text.resize(text.size() - 3);
		}
		const auto prefix = F4T::ConnectionPolicy::Ipv6Prefix64(text);
		if (!prefix) {
			return std::nullopt;
		}
		return F4T::ConnectionPolicy::Ipv6Prefix64Text(*prefix);
	}

	std::optional<std::string> BanStore::NormalizeProfileId(std::string_view a_profileId)
	{
		const auto value = Trim(a_profileId);
		if (value.size() < 8 || value.size() > 128) return std::nullopt;
		for (const unsigned char byte : value) {
			if (byte < 0x20 || byte == 0x7f) return std::nullopt;
			const char ch = static_cast<char>(byte);
			const bool alnum = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
			if (!alnum && ch != '-' && ch != '_' && ch != '.' && ch != ':' && ch != '@') return std::nullopt;
		}
		return value;
	}

}
