#pragma once

#include <cassert>
#include <cstddef>
#include <mutex>

namespace F4T::PowerArmorSync
{
	class FrameSaveGate
	{
	public:
		enum class SaveDecision { kReady, kWaitForFrames, kBusy };

		bool ReserveFrame()
		{
			const std::scoped_lock lock{ mutex_ };
			if (blocked_ || tearingDown_) {
				return false;
			}
			++frames_;
			return true;
		}

		void ReleaseFrame()
		{
			const std::scoped_lock lock{ mutex_ };
			assert(frames_ != 0);
			--frames_;
		}

		SaveDecision BeginSave()
		{
			const std::scoped_lock lock{ mutex_ };
			if (saving_ || tearingDown_) {
				return SaveDecision::kBusy;
			}
			blocked_ = true;
			if (frames_ != 0) {
				return SaveDecision::kWaitForFrames;
			}
			saving_ = true;
			return SaveDecision::kReady;
		}

		void EndSave()
		{
			const std::scoped_lock lock{ mutex_ };
			assert(saving_);
			saving_ = false;
			blocked_ = false;
		}

		bool TryBeginTeardown()
		{
			const std::scoped_lock lock{ mutex_ };
			if (saving_ || tearingDown_) {
				return false;
			}
			tearingDown_ = true;
			return true;
		}

		void EndTeardown()
		{
			const std::scoped_lock lock{ mutex_ };
			assert(tearingDown_);
			tearingDown_ = false;
		}

		void CancelPendingSave()
		{
			const std::scoped_lock lock{ mutex_ };
			if (!saving_) {
				blocked_ = false;
			}
		}

		bool HasFrames()
		{
			const std::scoped_lock lock{ mutex_ };
			return frames_ != 0;
		}

		bool NeedsCleanup()
		{
			const std::scoped_lock lock{ mutex_ };
			return blocked_ && !saving_;
		}

	private:
		std::mutex mutex_;
		std::size_t frames_{ 0 };
		bool blocked_{ false };
		bool saving_{ false };
		bool tearingDown_{ false };
	};
}
