#pragma once

#include <cstdint>
#include <optional>

namespace F4T::WorkshopObserver
{
	struct Angle
	{
		float x{ 0.0F };
		float y{ 0.0F };
		float z{ 0.0F };
	};

	[[nodiscard]] constexpr std::optional<Angle> EffectiveAngle(
		const Angle& a_raw,
		bool a_isActor,
		std::optional<bool> a_actorAngleFlag) noexcept
	{
		if (!a_isActor) {
			return a_raw;
		}
		if (!a_actorAngleFlag) {
			return std::nullopt;
		}
		return *a_actorAngleFlag ? a_raw : Angle{ 0.0F, 0.0F, a_raw.z };
	}

	// main thread only: both hooks run inside one OnButtonEvent
	class ReleaseTracker
	{
	public:
		[[nodiscard]] bool OnReleaseArm(std::uint32_t a_heldHandle, bool a_heldIsSpline) noexcept
		{
			m_wirePlaceHandle = a_heldIsSpline ? a_heldHandle : 0;
			return a_heldHandle != 0;
		}

		[[nodiscard]] bool OnPlaceAttempt(std::uint32_t a_heldHandle) noexcept
		{
			const bool secondHalfOfRelease = m_wirePlaceHandle != 0 && m_wirePlaceHandle == a_heldHandle;
			m_wirePlaceHandle = 0;
			return !secondHalfOfRelease;
		}

	private:
		std::uint32_t m_wirePlaceHandle{ 0 };
	};

	inline constexpr std::int32_t kScrapCancelId = 1;

	enum class ScrapSource
	{
		kButton,
		kClick,
	};

	class ScrapTracker
	{
	public:
		void OnConfirmCall(std::int32_t a_userData) noexcept
		{
			m_clickAccepted = a_userData != kScrapCancelId;
		}

		[[nodiscard]] ScrapSource OnAccept() noexcept
		{
			const auto source = m_clickAccepted ? ScrapSource::kClick : ScrapSource::kButton;
			m_clickAccepted = false;
			return source;
		}

	private:
		bool m_clickAccepted{ false };
	};
}
