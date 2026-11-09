#include "F4TNpcSyncPolicy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <tuple>

namespace F4T::NpcSyncPolicy
{
	HitRoute RouteAuthority(
		bool a_haveLocalView,
		bool a_localIsAuthority,
		std::uint64_t a_claimEpoch,
		std::uint64_t a_localEpoch) noexcept
	{
		if (!a_haveLocalView || !a_localIsAuthority || a_claimEpoch > a_localEpoch) {
			return HitRoute::kRetry;
		}
		if (a_claimEpoch != a_localEpoch) {
			return HitRoute::kDrop;
		}
		return HitRoute::kApply;
	}

	bool KeepPendingHit(
		HitRoute a_route,
		std::chrono::milliseconds a_age,
		std::chrono::milliseconds a_maximumAge) noexcept
	{
		return a_route == HitRoute::kRetry && a_age <= a_maximumAge;
	}

	float DamageAboveFloor(float a_health, float a_damage, float a_floor) noexcept
	{
		if (!std::isfinite(a_health) || !std::isfinite(a_floor) || !std::isfinite(a_damage) || !(a_damage > 0.0F)) {
			return 0.0F;
		}
		return (std::min)(a_damage, (std::max)(0.0F, a_health - a_floor));
	}

	std::chrono::milliseconds DeathRetryDelay(std::uint8_t a_attempts) noexcept
	{
		return kDeathRetryBase * (std::int64_t{ 1 } << (std::min)(a_attempts, kMaximumDeathBackoffStep));
	}

	bool IsDeathRetryDue(std::chrono::milliseconds a_sinceLastAttempt, std::uint8_t a_attempts) noexcept
	{
		return a_sinceLastAttempt >= DeathRetryDelay(a_attempts);
	}

	PuppetRedrawDecision DecidePuppetRedraw(
		bool a_neverRedrawn,
		std::chrono::milliseconds a_sinceLast,
		std::uint8_t a_redrawCount) noexcept
	{
		if (a_redrawCount >= kMaximumPuppetRedraws || (!a_neverRedrawn && a_sinceLast < kPuppetRedrawInterval)) {
			return {};
		}
		return { true, a_neverRedrawn };
	}

	bool IsRedrawEpisodeSettled(bool a_neverNeeded, std::chrono::milliseconds a_sinceLastNeeded) noexcept
	{
		return !a_neverNeeded && a_sinceLastNeeded >= kPuppetRedrawSettle;
	}

	bool IsOrphanAdoptable(bool a_stateDead, bool a_slotDeathApplied, bool a_slotActorDead) noexcept
	{
		return a_stateDead || !(a_slotDeathApplied || a_slotActorDead);
	}

	bool IsRuntimeAdoptScopeCompatible(
		std::string_view a_slotCellId,
		std::string_view a_wantedCellId,
		std::string_view a_slotWorldspaceId,
		std::string_view a_wantedWorldspaceId) noexcept
	{
		if (a_slotCellId == a_wantedCellId) {
			return true;
		}
		return !a_slotWorldspaceId.empty() && a_slotWorldspaceId == a_wantedWorldspaceId;
	}

	std::vector<OrphanEdge> AssignNearestOrphans(std::vector<OrphanEdge> a_edges)
	{
		std::erase_if(a_edges, [](const OrphanEdge& a_edge) { return !std::isfinite(a_edge.distance); });
		std::sort(a_edges.begin(), a_edges.end(), [](const OrphanEdge& a_left, const OrphanEdge& a_right) {
			if (a_left.distance != a_right.distance) {
				return a_left.distance < a_right.distance;
			}
			if (a_left.wanted != a_right.wanted) {
				return a_left.wanted < a_right.wanted;
			}
			return a_left.orphan < a_right.orphan;
		});
		std::set<std::size_t> usedWanted;
		std::set<std::size_t> usedOrphan;
		std::vector<OrphanEdge> chosen;
		for (const auto& edge : a_edges) {
			if (usedWanted.contains(edge.wanted) || usedOrphan.contains(edge.orphan)) {
				continue;
			}
			usedWanted.insert(edge.wanted);
			usedOrphan.insert(edge.orphan);
			chosen.push_back(edge);
		}
		return chosen;
	}

	int HostRank(bool a_dead, bool a_runtime, std::chrono::milliseconds a_sinceDead) noexcept
	{
		if (!a_dead) {
			return a_runtime ? 0 : 2;
		}
		return a_sinceDead < kFreshDeathWindow ? 1 : 3;
	}

	bool IsRuntimeDuplicate(const RuntimeCopy& a_left, const RuntimeCopy& a_right, float a_maximumDistance) noexcept
	{
		if (a_left.baseFormId != a_right.baseFormId || a_left.hostPlayerId == a_right.hostPlayerId) {
			return false;
		}
		const float dx = a_left.x - a_right.x;
		const float dy = a_left.y - a_right.y;
		const float dz = a_left.z - a_right.z;
		const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
		return std::isfinite(distance) && distance < a_maximumDistance;
	}

	bool PreferRuntimeCopy(const RuntimeCopy& a_candidate, const RuntimeCopy& a_incumbent) noexcept
	{
		if (a_candidate.inLocalCell != a_incumbent.inLocalCell) {
			return a_candidate.inLocalCell;
		}
		return a_candidate.hostPlayerId < a_incumbent.hostPlayerId;
	}

	std::vector<std::size_t> FindRuntimeDuplicateLosers(const std::vector<RuntimeCopy>& a_copies, float a_maximumDistance)
	{
		struct Edge
		{
			std::size_t left{};
			std::size_t right{};
			float distance{};
			std::uint32_t lowHost{};
			std::uint32_t highHost{};
		};
		std::vector<Edge> edges;
		for (std::size_t left = 0; left < a_copies.size(); ++left) {
			for (std::size_t right = left + 1; right < a_copies.size(); ++right) {
				const auto& a = a_copies[left];
				const auto& b = a_copies[right];
				if (!IsRuntimeDuplicate(a, b, a_maximumDistance)) {
					continue;
				}
				const float distance = std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
				edges.push_back({ left, right, distance, (std::min)(a.hostPlayerId, b.hostPlayerId), (std::max)(a.hostPlayerId, b.hostPlayerId) });
			}
		}
		std::sort(edges.begin(), edges.end(), [](const Edge& a_left, const Edge& a_right) {
			if (a_left.distance != a_right.distance) {
				return a_left.distance < a_right.distance;
			}
			if (a_left.lowHost != a_right.lowHost) {
				return a_left.lowHost < a_right.lowHost;
			}
			if (a_left.highHost != a_right.highHost) {
				return a_left.highHost < a_right.highHost;
			}
			return std::tie(a_left.left, a_left.right) < std::tie(a_right.left, a_right.right);
		});
		std::set<std::size_t> matched;
		std::vector<std::size_t> losers;
		for (const auto& edge : edges) {
			if (matched.contains(edge.left) || matched.contains(edge.right)) {
				continue;
			}
			matched.insert(edge.left);
			matched.insert(edge.right);
			losers.push_back(PreferRuntimeCopy(a_copies[edge.left], a_copies[edge.right]) ? edge.right : edge.left);
		}
		std::sort(losers.begin(), losers.end());
		return losers;
	}

	bool AcceptRemoteNpcState(std::uint32_t a_remoteHost, std::optional<std::uint32_t> a_localId, bool a_localSendsSource) noexcept
	{
		return !(a_localSendsSource && a_localId && a_remoteHost > *a_localId);
	}
}
