#include "F4TShotReplay.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::ShotReplay
{
	namespace
	{
		constexpr double kMinimumDirectionLength = 1e-6;
		constexpr double kUnitLengthTolerance = 0.01;
		constexpr std::size_t kMaximumEventNameLength = 64;
		constexpr std::array<std::string_view, 10> kReplayablePrefixes{
			"attack", "meleeattack", "powerattack", "block", "bash", "riflesighted", "reload", "throw", "grenade", "weap"
		};

		char LowerAscii(char a_character) noexcept
		{
			return a_character >= 'A' && a_character <= 'Z' ? static_cast<char>(a_character - 'A' + 'a') : a_character;
		}

		bool IsEventNameCharacter(char a_character) noexcept
		{
			return (a_character >= 'a' && a_character <= 'z') || (a_character >= 'A' && a_character <= 'Z') ||
				(a_character >= '0' && a_character <= '9') || a_character == '_';
		}

		bool EqualsIgnoreAsciiCase(std::string_view a_left, std::string_view a_right) noexcept
		{
			if (a_left.size() != a_right.size()) {
				return false;
			}
			for (std::size_t i = 0; i < a_left.size(); ++i) {
				if (LowerAscii(a_left[i]) != LowerAscii(a_right[i])) {
					return false;
				}
			}
			return true;
		}

		bool StartsWithIgnoreAsciiCase(std::string_view a_text, std::string_view a_prefix) noexcept
		{
			return a_text.size() >= a_prefix.size() && EqualsIgnoreAsciiCase(a_text.substr(0, a_prefix.size()), a_prefix);
		}

		std::optional<double> ReadFiniteNumber(const nlohmann::json& a_object, const char* a_key)
		{
			const auto found = a_object.find(a_key);
			if (found == a_object.end() || !found->is_number()) {
				return std::nullopt;
			}
			const auto value = found->get<double>();
			if (!std::isfinite(value)) {
				return std::nullopt;
			}
			return value;
		}
	}

	Direction DirectionFromLaunchAngles(double a_zAngle, double a_xAngle) noexcept
	{
		const double cosX = std::cos(a_xAngle);
		return Direction{ cosX * std::sin(a_zAngle), cosX * std::cos(a_zAngle), -std::sin(a_xAngle) };
	}

	void RemoteAimQueue::Push(std::uint32_t a_playerId, const Direction& a_direction, std::chrono::steady_clock::time_point a_now)
	{
		auto found = m_queues.find(a_playerId);
		if (found == m_queues.end()) {
			if (m_queues.size() >= kRemoteAimPlayerCap) {
				std::erase_if(m_queues, [&](const auto& a_queue) { return a_now - a_queue.second.back().received >= kRemoteAimExpiry; });
			}
			if (m_queues.size() >= kRemoteAimPlayerCap) {
				return;
			}
			found = m_queues.emplace(a_playerId, std::deque<Entry>{}).first;
		}
		auto& queue = found->second;
		while (!queue.empty() && a_now - queue.front().received >= kRemoteAimExpiry) {
			queue.pop_front();
		}
		if (queue.size() >= kRemoteAimPerPlayerCap) {
			queue.pop_front();
		}
		queue.push_back(Entry{ a_direction, a_now });
	}

	std::optional<Direction> RemoteAimQueue::Take(std::uint32_t a_playerId, std::chrono::steady_clock::time_point a_now)
	{
		const auto found = m_queues.find(a_playerId);
		if (found == m_queues.end()) {
			return std::nullopt;
		}
		auto& queue = found->second;
		while (!queue.empty() && a_now - queue.front().received >= kRemoteAimExpiry) {
			queue.pop_front();
		}
		if (queue.empty()) {
			m_queues.erase(found);
			return std::nullopt;
		}
		const Direction direction = queue.front().direction;
		queue.pop_front();
		if (queue.empty()) {
			m_queues.erase(found);
		}
		return direction;
	}

	std::optional<Direction> NormalizeDirection(const Direction& a_direction) noexcept
	{
		const auto length = std::sqrt(a_direction.x * a_direction.x + a_direction.y * a_direction.y + a_direction.z * a_direction.z);
		if (!std::isfinite(length) || length < kMinimumDirectionLength) {
			return std::nullopt;
		}
		return Direction{ a_direction.x / length, a_direction.y / length, a_direction.z / length };
	}

	std::optional<Direction> ParseProjectileAim(const nlohmann::json& a_packet)
	{
		const auto found = a_packet.find("direction");
		if (found == a_packet.end() || !found->is_object()) {
			return std::nullopt;
		}
		const auto x = ReadFiniteNumber(*found, "x");
		const auto y = ReadFiniteNumber(*found, "y");
		const auto z = ReadFiniteNumber(*found, "z");
		if (!x || !y || !z) {
			return std::nullopt;
		}
		const auto length = std::sqrt((*x) * (*x) + (*y) * (*y) + (*z) * (*z));
		if (!(std::abs(length - 1.0) <= kUnitLengthTolerance)) {
			return std::nullopt;
		}
		return Direction{ *x, *y, *z };
	}

	bool IsReplayableAnimEvent(std::string_view a_eventName) noexcept
	{
		if (a_eventName.empty() || a_eventName.size() > kMaximumEventNameLength) {
			return false;
		}
		for (const char character : a_eventName) {
			if (!IsEventNameCharacter(character)) {
				return false;
			}
		}
		if (EqualsIgnoreAsciiCase(a_eventName, "attackStart") || EqualsIgnoreAsciiCase(a_eventName, "meleeattackStart")) {
			return false;
		}
		for (const auto prefix : kReplayablePrefixes) {
			if (StartsWithIgnoreAsciiCase(a_eventName, prefix)) {
				return true;
			}
		}
		return false;
	}

	bool IsRetryableAnimEvent(std::string_view a_eventName) noexcept
	{
		return StartsWithIgnoreAsciiCase(a_eventName, "attackStart") || StartsWithIgnoreAsciiCase(a_eventName, "reloadStart") ||
			EqualsIgnoreAsciiCase(a_eventName, "rifleSightedStart");
	}

	std::optional<std::string> ParsePlayerAnimEvent(const nlohmann::json& a_packet)
	{
		const auto found = a_packet.find("event");
		if (found == a_packet.end() || !found->is_string()) {
			return std::nullopt;
		}
		auto name = found->get<std::string>();
		if (!IsReplayableAnimEvent(name)) {
			return std::nullopt;
		}
		return name;
	}

	std::optional<std::uint32_t> ParseRelayedPlayerId(const nlohmann::json& a_packet)
	{
		const auto found = a_packet.find("playerId");
		if (found == a_packet.end() || !found->is_number_integer()) {
			return std::nullopt;
		}
		const auto value = found->get<std::int64_t>();
		if (value < 1 || value > static_cast<std::int64_t>((std::numeric_limits<std::uint32_t>::max)())) {
			return std::nullopt;
		}
		return static_cast<std::uint32_t>(value);
	}
}
