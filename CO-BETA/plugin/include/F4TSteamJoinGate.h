#pragma once

#include <cstdint>
#include <mutex>

namespace F4T::SteamJoinGate
{
	enum class Operation : unsigned
	{
		None = 0,
		HostStart = 1,
		Join = 2
	};

	constexpr Operation operator|(Operation a_left, Operation a_right)
	{
		return static_cast<Operation>(static_cast<unsigned>(a_left) | static_cast<unsigned>(a_right));
	}

	constexpr Operation operator&(Operation a_left, Operation a_right)
	{
		return static_cast<Operation>(static_cast<unsigned>(a_left) & static_cast<unsigned>(a_right));
	}

	constexpr bool Has(Operation a_set, Operation a_operation)
	{
		return (a_set & a_operation) != Operation::None;
	}

	class Gate
	{
	public:
		std::uint64_t Epoch() const
		{
			const std::scoped_lock lock(mutex);
			return epoch;
		}

		bool IsCurrent(std::uint64_t a_epoch) const
		{
			const std::scoped_lock lock(mutex);
			return epoch == a_epoch;
		}

		std::uint64_t Begin(Operation a_operation)
		{
			const std::scoped_lock lock(mutex);
			pending = pending | a_operation;
			return epoch;
		}

		bool Finish(Operation a_operation, std::uint64_t a_epoch)
		{
			const std::scoped_lock lock(mutex);
			if (epoch != a_epoch) {
				return false;
			}
			pending = static_cast<Operation>(static_cast<unsigned>(pending) & ~static_cast<unsigned>(a_operation));
			return true;
		}

		Operation Pending() const
		{
			const std::scoped_lock lock(mutex);
			return pending;
		}

		Operation Cancel()
		{
			const std::scoped_lock lock(mutex);
			++epoch;
			const auto cancelled = pending;
			pending = Operation::None;
			return cancelled;
		}

	private:
		mutable std::mutex mutex;
		std::uint64_t      epoch{ 0 };
		Operation          pending{ Operation::None };
	};
}
