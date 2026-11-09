#pragma once

#include <cstdint>

namespace F4T::Downed
{
	inline constexpr std::uint32_t kMaximumRecoveryDispatches = 8;

	enum class RecoveryStep
	{
		kIdle,
		kWaiting,
		kDispatched,
		kStoodUp,
		kGaveUp
	};

	class BleedoutRecoveryRunner
	{
	public:
		void Request() noexcept
		{
			if (gaveUp_) {
				return;
			}
			if (!pending_) {
				attempts_ = 0;
			}
			pending_ = true;
			resendOwed_ = false;
		}

		template <class Send>
		void Cancel(double a_now, bool a_canSend, Send&& a_send)
		{
			if (pending_ && attempts_ > 0 && !resendOwed_) {
				resendOwed_ = true;
				resendAttempts_ = 0;
				nextSendSeconds_ = a_now;
			}
			pending_ = false;
			if (!a_canSend || !resendOwed_) {
				return;
			}
			if (resendAttempts_ >= kMaximumRecoveryDispatches) {
				resendOwed_ = false;
			} else if (a_now >= nextSendSeconds_) {
				nextSendSeconds_ = a_now + 1.0;
				++resendAttempts_;
				if (a_send(true)) {
					resendOwed_ = false;
				}
			}
		}

		template <class Send>
		[[nodiscard]] RecoveryStep Update(double a_now, bool a_canTouch, bool a_essentialDown, bool a_sessionApplied, Send&& a_send)
		{
			if (a_canTouch) {
				if (wasEssentialDown_ && !a_essentialDown) {
					attempts_ = 0;
				}
				wasEssentialDown_ = a_essentialDown;
			}
			if (resendOwed_ && a_canTouch) {
				if (resendAttempts_ >= kMaximumRecoveryDispatches) {
					resendOwed_ = false;
				} else if (a_now >= nextSendSeconds_) {
					nextSendSeconds_ = a_now + 1.0;
					++resendAttempts_;
					if (a_send(true)) {
						resendOwed_ = false;
					}
				}
			}
			if (!pending_ || !a_canTouch) {
				return RecoveryStep::kIdle;
			}
			if (!a_essentialDown && !a_sessionApplied) {
				pending_ = false;
				return RecoveryStep::kStoodUp;
			}
			if (a_now < nextSendSeconds_) {
				return RecoveryStep::kWaiting;
			}
			if (attempts_ >= kMaximumRecoveryDispatches) {
				Cancel(a_now, true, a_send);
				gaveUp_ = true;
				return RecoveryStep::kGaveUp;
			}
			nextSendSeconds_ = a_now + 1.0;
			++attempts_;
			if (a_essentialDown) {
				static_cast<void>(a_send(false));
				return RecoveryStep::kDispatched;
			}
			if (a_send(true)) {
				pending_ = false;
				return RecoveryStep::kStoodUp;
			}
			return RecoveryStep::kWaiting;
		}

		void Reset() noexcept
		{
			gaveUp_ = false;
			pending_ = false;
			resendOwed_ = false;
			attempts_ = 0;
			resendAttempts_ = 0;
			nextSendSeconds_ = 0.0;
			wasEssentialDown_ = false;
		}

		void ClearGaveUp() noexcept
		{
			gaveUp_ = false;
		}

		[[nodiscard]] bool IsPending() const noexcept { return pending_; }
		[[nodiscard]] bool IsResendOwed() const noexcept { return resendOwed_; }
		[[nodiscard]] std::uint32_t Attempts() const noexcept { return attempts_; }

	private:
		bool pending_{ false };
		bool resendOwed_{ false };
		bool gaveUp_{ false };
		bool wasEssentialDown_{ false };
		std::uint32_t attempts_{ 0 };
		std::uint32_t resendAttempts_{ 0 };
		double nextSendSeconds_{ 0.0 };
	};
}
