#pragma once

#include <cstdint>
#include <optional>

namespace F4T::NpcSyncSweep
{
	struct AuthoritySnapshot
	{
		bool host{};
		std::uint32_t hostPlayerId{};
		std::uint64_t epoch{};
	};

	[[nodiscard]] constexpr bool AuthorityChanged(const AuthoritySnapshot& a_previous, const AuthoritySnapshot& a_current) noexcept
	{
		return a_previous.host != a_current.host ||
		       a_previous.hostPlayerId != a_current.hostPlayerId ||
		       a_previous.epoch != a_current.epoch;
	}

	class AuthorityFlipGate
	{
	public:
		enum class Action : std::uint8_t
		{
			kNone,
			kApply,
			kHold,
		};

		[[nodiscard]] Action Observe(const AuthoritySnapshot& a_current, bool a_hasAuthority, std::uint64_t a_nowMs, std::uint64_t a_holdMs) noexcept
		{
			if (a_hasAuthority) {
				heldSince_.reset();
				if (!AuthorityChanged(committed_, a_current)) {
					return Action::kNone;
				}
				committed_ = a_current;
				return Action::kApply;
			}

			const AuthoritySnapshot none{};
			if (!AuthorityChanged(committed_, none)) {
				heldSince_.reset();
				return Action::kNone;
			}
			if (!heldSince_) {
				heldSince_ = a_nowMs;
			}
			if (a_nowMs - *heldSince_ < a_holdMs) {
				return Action::kHold;
			}
			committed_ = none;
			heldSince_.reset();
			return Action::kApply;
		}

		void Reset() noexcept
		{
			committed_ = {};
			heldSince_.reset();
		}

	private:
		AuthoritySnapshot committed_{};
		std::optional<std::uint64_t> heldSince_{};
	};

	enum class PuppetDeleteOutcome : std::uint8_t
	{
		kDeleted,
		kAlreadyDeleted,
		kUnresolved,
	};

	[[nodiscard]] constexpr PuppetDeleteOutcome DecidePuppetDelete(bool a_resolved, bool a_deleted) noexcept
	{
		if (!a_resolved) {
			return PuppetDeleteOutcome::kUnresolved;
		}
		return a_deleted ? PuppetDeleteOutcome::kAlreadyDeleted : PuppetDeleteOutcome::kDeleted;
	}

	struct SweepTally
	{
		std::uint32_t deleted{};
		std::uint32_t alreadyDeleted{};
		std::uint32_t unresolved{};

		constexpr void Count(PuppetDeleteOutcome a_outcome) noexcept
		{
			switch (a_outcome) {
			case PuppetDeleteOutcome::kDeleted:
				++deleted;
				break;
			case PuppetDeleteOutcome::kAlreadyDeleted:
				++alreadyDeleted;
				break;
			case PuppetDeleteOutcome::kUnresolved:
				++unresolved;
				break;
			}
		}

		[[nodiscard]] constexpr bool Empty() const noexcept
		{
			return deleted == 0 && alreadyDeleted == 0 && unresolved == 0;
		}

		constexpr void Add(const SweepTally& a_other) noexcept
		{
			deleted += a_other.deleted;
			alreadyDeleted += a_other.alreadyDeleted;
			unresolved += a_other.unresolved;
		}
	};

	class ThrottledTally
	{
	public:
		[[nodiscard]] std::optional<SweepTally> Take(const SweepTally& a_added, std::uint64_t a_nowMs, std::uint64_t a_intervalMs) noexcept
		{
			pending_.Add(a_added);
			if (pending_.Empty()) {
				return std::nullopt;
			}
			if (emitted_ && a_nowMs - lastEmitMs_ < a_intervalMs) {
				return std::nullopt;
			}
			const SweepTally out = pending_;
			pending_ = {};
			lastEmitMs_ = a_nowMs;
			emitted_ = true;
			return out;
		}

	private:
		SweepTally pending_{};
		std::uint64_t lastEmitMs_{};
		bool emitted_{ false };
	};
}
