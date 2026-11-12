#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

namespace F4T::ExplosionSyncPolicy
{
	inline constexpr double kReplayRadius = 9000.0;
	inline constexpr double kProxyMatchRadius = 300.0;
	inline constexpr std::chrono::milliseconds kProxyHold{ 500 };

	struct Point
	{
		double x{};
		double y{};
		double z{};
	};

	struct SeenRef
	{
		std::uintptr_t address{};
		std::uint32_t localId{};
		std::uint32_t handle{};
	};

	struct ChainChildRecord
	{
		std::uint32_t handle{};
		std::chrono::steady_clock::time_point seenAt{};
	};

	enum class Decision
	{
		Wait,
		Replay,
		Drop
	};

	struct DecisionInput
	{
		bool sameSpace{};
		bool inRange{};
		bool hasProxy{};
		bool proxyBlastMatched{};
		std::chrono::milliseconds elapsed{};
	};

	[[nodiscard]] bool WithinRadius(const Point& a_left, const Point& a_right, double a_radius) noexcept;
	[[nodiscard]] bool InReplayRange(const Point& a_spawn, const Point& a_player) noexcept;
	[[nodiscard]] bool SameSpace(
		bool a_exterior,
		std::uint32_t a_localCellId,
		std::uint32_t a_localWorldspaceId,
		std::uint32_t a_remoteCellId,
		std::uint32_t a_remoteWorldspaceId) noexcept;
	[[nodiscard]] bool ProxyBlastMatches(
		std::uint32_t a_blastPlayerId,
		std::uint32_t a_spawnPlayerId,
		const Point& a_blast,
		const Point& a_spawn,
		std::chrono::milliseconds a_gap) noexcept;
	[[nodiscard]] bool IsNewRef(const std::vector<SeenRef>& a_seen, const SeenRef& a_ref) noexcept;
	[[nodiscard]] bool IsChainChild(const std::vector<ChainChildRecord>& a_records, std::uint32_t a_handle) noexcept;
	void RecordChainChild(std::vector<ChainChildRecord>& a_records, std::uint32_t a_handle, std::chrono::steady_clock::time_point a_now);
	void PruneChainChildren(std::vector<ChainChildRecord>& a_records, std::chrono::steady_clock::time_point a_now, std::chrono::milliseconds a_keep);
	[[nodiscard]] Decision Decide(const DecisionInput& a_input) noexcept;
	[[nodiscard]] bool GiveBackAllowed(double a_healthAfterHit) noexcept;
}
