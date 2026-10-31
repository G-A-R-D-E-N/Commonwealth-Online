#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace F4T::ShotReplay
{
	inline constexpr std::string_view kCapabilityName = "shot-replay-v1";

	struct Direction
	{
		double x{};
		double y{};
		double z{};
	};

	inline constexpr std::size_t kRemoteAimPerPlayerCap = 16;
	inline constexpr std::size_t kRemoteAimPlayerCap = 64;
	inline constexpr std::chrono::milliseconds kRemoteAimExpiry{ 500 };

	class RemoteAimQueue
	{
	public:
		void Push(std::uint32_t a_playerId, const Direction& a_direction, std::chrono::steady_clock::time_point a_now);
		[[nodiscard]] std::optional<Direction> Take(std::uint32_t a_playerId, std::chrono::steady_clock::time_point a_now);

	private:
		struct Entry
		{
			Direction direction{};
			std::chrono::steady_clock::time_point received{};
		};

		std::unordered_map<std::uint32_t, std::deque<Entry>> m_queues;
	};

	[[nodiscard]] std::optional<Direction> NormalizeDirection(const Direction& a_direction) noexcept;
	[[nodiscard]] Direction DirectionFromLaunchAngles(double a_zAngle, double a_xAngle) noexcept;
	[[nodiscard]] std::optional<Direction> ParseProjectileAim(const nlohmann::json& a_packet);
	[[nodiscard]] bool IsReplayableAnimEvent(std::string_view a_eventName) noexcept;
	[[nodiscard]] bool IsRetryableAnimEvent(std::string_view a_eventName) noexcept;
	[[nodiscard]] std::optional<std::string> ParsePlayerAnimEvent(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<std::uint32_t> ParseRelayedPlayerId(const nlohmann::json& a_packet);
}
