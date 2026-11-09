#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace F4T::NpcSyncPolicy
{
	enum class HitRoute : std::uint8_t
	{
		kApply,
		kRetry,
		kDrop,
	};

	inline constexpr std::chrono::milliseconds kDeathRetryBase{ 250 };
	inline constexpr std::uint8_t kMaximumDeathBackoffStep{ 4 };

	[[nodiscard]] HitRoute RouteAuthority(
		bool a_haveLocalView,
		bool a_localIsAuthority,
		std::uint64_t a_claimEpoch,
		std::uint64_t a_localEpoch) noexcept;

	[[nodiscard]] bool KeepPendingHit(
		HitRoute a_route,
		std::chrono::milliseconds a_age,
		std::chrono::milliseconds a_maximumAge) noexcept;

	[[nodiscard]] float DamageAboveFloor(float a_health, float a_damage, float a_floor) noexcept;

	[[nodiscard]] std::chrono::milliseconds DeathRetryDelay(std::uint8_t a_attempts) noexcept;

	[[nodiscard]] bool IsDeathRetryDue(std::chrono::milliseconds a_sinceLastAttempt, std::uint8_t a_attempts) noexcept;

	[[nodiscard]] bool AcceptRemoteNpcState(std::uint32_t a_remoteHost, std::optional<std::uint32_t> a_localId, bool a_localSendsSource) noexcept;

	enum class HostNpcIdStep : std::uint8_t
	{
		kKeep,
		kForget,
		kReissue,
	};

	template <class Time>
	struct HostNpcIdResolution
	{
		HostNpcIdStep step{ HostNpcIdStep::kKeep };
		std::optional<Time> missingSince{};
	};

	template <class Time, class Grace>
	[[nodiscard]] HostNpcIdResolution<Time> ResolveHostNpcIdRecord(
		std::uint32_t a_recordBaseFormId,
		std::optional<Time> a_missingSince,
		std::optional<std::uint32_t> a_loadedBaseFormId,
		Time a_now,
		Grace a_grace)
	{
		if (a_loadedBaseFormId) {
			const bool reissue = *a_loadedBaseFormId != 0 && *a_loadedBaseFormId != a_recordBaseFormId;
			return { reissue ? HostNpcIdStep::kReissue : HostNpcIdStep::kKeep, std::nullopt };
		}
		const auto missingSince = a_missingSince.value_or(a_now);
		return { a_now - missingSince < a_grace ? HostNpcIdStep::kKeep : HostNpcIdStep::kForget, missingSince };
	}

	inline constexpr std::chrono::milliseconds kPuppetRedrawInterval{ 1000 };
	inline constexpr std::chrono::milliseconds kPuppetRedrawSettle{ 5000 };
	inline constexpr std::uint8_t kMaximumPuppetRedraws{ 5 };

	struct PuppetRedrawDecision
	{
		bool redraw{};
		bool firstRedraw{};
	};

	[[nodiscard]] PuppetRedrawDecision DecidePuppetRedraw(
		bool a_neverRedrawn,
		std::chrono::milliseconds a_sinceLast,
		std::uint8_t a_redrawCount) noexcept;

	[[nodiscard]] bool IsRedrawEpisodeSettled(bool a_neverNeeded, std::chrono::milliseconds a_sinceLastNeeded) noexcept;

	[[nodiscard]] bool IsOrphanAdoptable(bool a_stateDead, bool a_slotDeathApplied, bool a_slotActorDead) noexcept;

	[[nodiscard]] bool IsRuntimeAdoptScopeCompatible(
		std::string_view a_slotCellId,
		std::string_view a_wantedCellId,
		std::string_view a_slotWorldspaceId,
		std::string_view a_wantedWorldspaceId) noexcept;

	struct OrphanEdge
	{
		std::size_t wanted{};
		std::size_t orphan{};
		float distance{};
	};

	[[nodiscard]] std::vector<OrphanEdge> AssignNearestOrphans(std::vector<OrphanEdge> a_edges);

	template <class Slot>
	void ResetAdoptedSlot(Slot& a_slot, bool a_stateDead)
	{
		if (a_stateDead) {
			return;
		}
		a_slot.deathApplied = false;
		a_slot.deathAttempts = 0;
		a_slot.lastDeathAttempt = {};
		a_slot.lastRedraw = {};
		a_slot.lastNeeded = {};
		a_slot.redrawCount = 0;
	}

	inline constexpr float kRuntimeClaimDistance{ 1024.0F };
	inline constexpr std::chrono::milliseconds kFreshDeathWindow{ 10000 };

	[[nodiscard]] int HostRank(bool a_dead, bool a_runtime, std::chrono::milliseconds a_sinceDead) noexcept;

	struct RuntimeCopy
	{
		std::uint32_t baseFormId{};
		float x{};
		float y{};
		float z{};
		std::uint32_t hostPlayerId{};
		bool inLocalCell{};
	};

	[[nodiscard]] bool IsRuntimeDuplicate(const RuntimeCopy& a_left, const RuntimeCopy& a_right, float a_maximumDistance) noexcept;

	[[nodiscard]] std::vector<std::size_t> FindRuntimeDuplicateLosers(const std::vector<RuntimeCopy>& a_copies, float a_maximumDistance);

	[[nodiscard]] bool PreferRuntimeCopy(const RuntimeCopy& a_candidate, const RuntimeCopy& a_incumbent) noexcept;

	struct RuntimeClaimCandidate
	{
		std::uint32_t actorFormId{};
		float distance{};
		bool dead{};
	};

	template <class Key>
	std::optional<std::uint32_t> ResolveRuntimeClaim(
		std::map<Key, std::uint32_t>& a_claims,
		const Key& a_key,
		const std::vector<RuntimeClaimCandidate>& a_candidates,
		float a_maximumDistance,
		bool a_stateDead = false)
	{
		const auto claimedByAnother = [&](std::uint32_t a_actorFormId) {
			for (const auto& [claimKey, claimedFormId] : a_claims) {
				if (claimKey != a_key && claimedFormId == a_actorFormId) {
					return true;
				}
			}
			return false;
		};
		const auto usable = [&](const RuntimeClaimCandidate& a_candidate) {
			return a_candidate.distance < a_maximumDistance && !claimedByAnother(a_candidate.actorFormId);
		};

		if (const auto held = a_claims.find(a_key); held != a_claims.end()) {
			for (const auto& candidate : a_candidates) {
				if (candidate.actorFormId == held->second && usable(candidate)) {
					return candidate.actorFormId;
				}
			}
		}

		const RuntimeClaimCandidate* best = nullptr;
		for (const auto& candidate : a_candidates) {
			if (!usable(candidate)) {
				continue;
			}
			const bool candidateMatches = candidate.dead == a_stateDead;
			if (!best || (best->dead != a_stateDead && candidateMatches) ||
				((best->dead == a_stateDead) == candidateMatches && candidate.distance < best->distance)) {
				best = &candidate;
			}
		}
		if (!best) {
			a_claims.erase(a_key);
			return std::nullopt;
		}
		a_claims.insert_or_assign(a_key, best->actorFormId);
		return best->actorFormId;
	}
}
