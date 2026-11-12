#include "F4TExplosionSyncPolicy.h"

#include <algorithm>

namespace F4T::ExplosionSyncPolicy
{
	bool WithinRadius(const Point& a_left, const Point& a_right, double a_radius) noexcept
	{
		const double dx = a_left.x - a_right.x;
		const double dy = a_left.y - a_right.y;
		const double dz = a_left.z - a_right.z;
		return dx * dx + dy * dy + dz * dz <= a_radius * a_radius;
	}

	bool InReplayRange(const Point& a_spawn, const Point& a_player) noexcept
	{
		return WithinRadius(a_spawn, a_player, kReplayRadius);
	}

	bool SameSpace(
		bool a_exterior,
		std::uint32_t a_localCellId,
		std::uint32_t a_localWorldspaceId,
		std::uint32_t a_remoteCellId,
		std::uint32_t a_remoteWorldspaceId) noexcept
	{
		if (a_exterior) {
			return a_localWorldspaceId != 0 && a_localWorldspaceId == a_remoteWorldspaceId;
		}
		return a_localCellId != 0 && a_localCellId == a_remoteCellId;
	}

	bool ProxyBlastMatches(
		std::uint32_t a_blastPlayerId,
		std::uint32_t a_spawnPlayerId,
		const Point& a_blast,
		const Point& a_spawn,
		std::chrono::milliseconds a_gap) noexcept
	{
		return a_blastPlayerId == a_spawnPlayerId && a_gap <= kProxyHold && WithinRadius(a_blast, a_spawn, kProxyMatchRadius);
	}

	bool IsNewRef(const std::vector<SeenRef>& a_seen, const SeenRef& a_ref) noexcept
	{
		return std::none_of(a_seen.begin(), a_seen.end(), [&](const SeenRef& a_entry) {
			return a_entry.address == a_ref.address && a_entry.localId == a_ref.localId && a_entry.handle == a_ref.handle;
		});
	}

	bool IsChainChild(const std::vector<ChainChildRecord>& a_records, std::uint32_t a_handle) noexcept
	{
		return a_handle != 0 && std::any_of(a_records.begin(), a_records.end(), [&](const ChainChildRecord& a_record) {
			return a_record.handle == a_handle;
		});
	}

	void RecordChainChild(std::vector<ChainChildRecord>& a_records, std::uint32_t a_handle, std::chrono::steady_clock::time_point a_now)
	{
		if (a_handle == 0) {
			return;
		}
		const auto existing = std::find_if(a_records.begin(), a_records.end(), [&](const ChainChildRecord& a_record) {
			return a_record.handle == a_handle;
		});
		if (existing != a_records.end()) {
			existing->seenAt = a_now;
			return;
		}
		a_records.push_back(ChainChildRecord{ a_handle, a_now });
	}

	void PruneChainChildren(std::vector<ChainChildRecord>& a_records, std::chrono::steady_clock::time_point a_now, std::chrono::milliseconds a_keep)
	{
		std::erase_if(a_records, [&](const ChainChildRecord& a_record) { return a_now - a_record.seenAt > a_keep; });
	}

	Decision Decide(const DecisionInput& a_input) noexcept
	{
		if (!a_input.sameSpace || !a_input.inRange) {
			return Decision::Drop;
		}
		if (!a_input.hasProxy) {
			return Decision::Replay;
		}
		if (a_input.proxyBlastMatched) {
			return Decision::Drop;
		}
		return a_input.elapsed < kProxyHold ? Decision::Wait : Decision::Replay;
	}

	bool GiveBackAllowed(double a_healthAfterHit) noexcept
	{
		return a_healthAfterHit > 0.0;
	}
}
