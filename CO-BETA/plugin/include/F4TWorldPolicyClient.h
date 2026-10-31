#pragma once

#include "F4TWorldPolicy.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace F4T::WorldPolicyClient
{
	enum class RefClass
	{
		World,
		Actor,
		VendorActor,
		ProxyActor,
		Player
	};

	enum class RefState
	{
		Unknown,
		Live,
		DisabledNotDeleted,
		Deleted
	};

	enum class RefAction
	{
		Skip,
		Disable
	};

	enum class QuestAction
	{
		Keep,
		Stop
	};

	inline constexpr std::size_t kMaximumPendingCells = 64;
	inline constexpr std::uint64_t kNoSessionGeneration = 0;

	class InboundPolicyState
	{
	public:
		bool Store(const F4T::WorldPolicy::Policy& a_policy, std::uint64_t a_generation) noexcept;
		void OnSessionGeneration(std::uint64_t a_generation) noexcept;
		[[nodiscard]] bool TakeAdoptable(std::uint64_t a_generation, F4T::WorldPolicy::Policy& a_out) noexcept;

	private:
		F4T::WorldPolicy::Policy policy_;
		std::uint64_t generation_{ kNoSessionGeneration };
		bool stored_{ false };
		bool dirty_{ false };
		bool rebound_{ false };
	};

	[[nodiscard]] bool CanTrackDisabledRef(std::size_t a_tracked, std::size_t a_capacity, bool a_alreadyTracked) noexcept;

	class PendingCellQueue
	{
	public:
		void Push(std::uint32_t a_cellFormId) noexcept;
		void Clear() noexcept;
		[[nodiscard]] std::vector<std::uint32_t> Drain();
		[[nodiscard]] bool Contains(std::uint32_t a_cellFormId) const noexcept;
		[[nodiscard]] std::size_t Size() const noexcept;

	private:
		std::vector<std::uint32_t> cells_;
	};

	[[nodiscard]] bool IsStoryQuestType(std::int32_t a_questType) noexcept;
	[[nodiscard]] bool IsRemovalTarget(const F4T::WorldPolicy::Policy& a_policy, RefClass a_class, bool a_baseListed, bool a_refListed) noexcept;
	[[nodiscard]] RefAction DecideRefAction(RefState a_state) noexcept;
	[[nodiscard]] QuestAction DecideForQuest(const F4T::WorldPolicy::Policy& a_policy, bool a_running, std::int32_t a_questType) noexcept;
}
