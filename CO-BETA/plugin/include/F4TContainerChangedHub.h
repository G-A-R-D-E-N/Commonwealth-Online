#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace F4T::ContainerChangedHub
{
	inline constexpr std::size_t kRingCapacity = 1024;
	inline constexpr std::size_t kMaximumSubscribers = 32;
	inline constexpr std::uint32_t kDiagnosticLinesPerSecond = 8;

	struct ContainerChange
	{
		std::uint32_t oldContainer;
		std::uint32_t newContainer;
		std::uint32_t baseObj;
		std::int32_t itemCount;
		std::uint32_t referenceFormID;
		std::uint16_t uniqueID;
		std::uint32_t threadId;
		std::uint64_t monotonicMs;
	};
	static_assert(std::is_trivial_v<ContainerChange> && std::is_standard_layout_v<ContainerChange>);

	using Callback = std::function<void(const ContainerChange&)>;

	void Install();
	void Shutdown();
	[[nodiscard]] bool Subscribe(Callback a_callback);
	void Drain(const Callback& a_callback = {});
	void SetDiagnosticsEnabled(bool a_enabled);
	[[nodiscard]] std::uint64_t DroppedEvents() noexcept;
}
