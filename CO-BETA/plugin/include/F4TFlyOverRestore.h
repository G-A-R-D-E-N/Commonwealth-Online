#pragma once

#include <cstdint>

namespace F4T::FlyOver
{
	enum class ExitReason : std::uint8_t
	{
		Ready,
		Failed,
		Cancelled,
		KickBan,
		Disconnect,
		LoadGame,
		NewGame,
		Quit,
		SessionTeardown,
		WatchdogTimeout
	};

	[[nodiscard]] constexpr bool IsWorldReset(ExitReason a_reason)
	{
		return a_reason == ExitReason::LoadGame || a_reason == ExitReason::NewGame || a_reason == ExitReason::Quit;
	}

	class RestoreGuard
	{
	public:
		void MarkTakeoverInstalled()
		{
			installed_ = true;
			restored_ = false;
		}

		[[nodiscard]] bool IsInstalled() const { return installed_; }

		[[nodiscard]] bool Restore(ExitReason a_reason)
		{
			++restoreRequests_;
			if (!installed_ || IsWorldReset(a_reason)) {
				installed_ = false;
				return false;
			}
			installed_ = false;
			++engineRestores_;
			return true;
		}

		void AbandonAfterWorldReset()
		{
			installed_ = false;
			++abandons_;
		}

		[[nodiscard]] std::uint32_t RestoreRequests() const { return restoreRequests_; }
		[[nodiscard]] std::uint32_t EngineRestores() const { return engineRestores_; }
		[[nodiscard]] std::uint32_t Abandons() const { return abandons_; }

	private:
		bool          installed_{ false };
		bool          restored_{ false };
		std::uint32_t restoreRequests_{ 0 };
		std::uint32_t engineRestores_{ 0 };
		std::uint32_t abandons_{ 0 };
	};
}
