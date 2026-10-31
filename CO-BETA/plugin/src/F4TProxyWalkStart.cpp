#include "pch.h"

#include "F4TProxyWalkStart.h"

#include "F4TDebugLogging.h"
#include "F4TProxyAnimationSyncInternal.h"
#include "F4TProxySneakAction.h"
#include "RE/A/Actor.h"
#include "RE/B/BSFixedString.h"
#include "RE/I/IAnimationGraphManagerHolder.h"

namespace F4T::ProxyWalkStart
{
	namespace
	{
		constexpr std::uint32_t kStanceCheckTicks = 15;
		constexpr std::uint32_t kMaxRefusedTicks = 120;

		namespace GraphEvent
		{
			// TODO-verify(#407) source:docs/code-notes/CO-BETA-plugin.md
			inline constexpr const char* kMoveStart = "moveStart";
			inline constexpr const char* kSneakStart = "sneakStart";
			inline constexpr const char* kSneakStop = "sneakStop";
		}
	}

	bool SendEvents(
		RE::Actor& a_proxy,
		bool a_isSneaking,
		std::uint32_t a_remotePlayerId,
		std::uint32_t a_attempt)
	{
		const auto accepted = F4T::ProxyAnimationSync::detail::TrySendGraphEvent(a_proxy, GraphEvent::kMoveStart);
		if (F4T::ProxySneakAction::ShouldTraceCount(a_attempt)) {
			F4T::DebugLogging::Log(
				"ProxyAnim",
				"[RT-VERIFY 364-B] walk start for remote player {} actor={:08X}: sneaking={}, attempt={}, event='{}' accepted={}.",
				a_remotePlayerId,
				a_proxy.GetFormID(),
				a_isSneaking,
				a_attempt,
				GraphEvent::kMoveStart,
				accepted);
		}

		return accepted;
	}

	void RetryPending(
		RetryState& a_state,
		RE::Actor& a_proxy,
		bool a_isMoving,
		bool a_isJumping,
		bool a_isSneaking,
		std::uint32_t a_remotePlayerId)
	{
		if (!a_state.pending) {
			return;
		}

		const auto actorId = a_proxy.GetFormID();
		if (!a_isMoving || a_isJumping) {
			a_state.pending = false;
			F4T::DebugLogging::Log(
				"ProxyAnim",
				"[RT-VERIFY 364-B] walk start retry dropped for remote player {} actor={:08X}: moving={}, jumping={}, refusedTicks={}.",
				a_remotePlayerId,
				actorId,
				a_isMoving,
				a_isJumping,
				a_state.refusedTicks);
			return;
		}

		++a_state.refusedTicks;
		if (a_state.refusedTicks > kMaxRefusedTicks) {
			a_state.pending = false;
			F4T::DebugLogging::Log(
				"ProxyAnim",
				"[RT-VERIFY 364-B] walk start gave up for remote player {} actor={:08X} after {} refused ticks: moving={}, sneaking={}.",
				a_remotePlayerId,
				actorId,
				a_state.refusedTicks,
				a_isMoving,
				a_isSneaking);
			return;
		}

		if (a_state.refusedTicks % kStanceCheckTicks == 0) {
			std::int32_t graphInSneak = 0;
			auto* graphHolder = static_cast<RE::IAnimationGraphManagerHolder*>(&a_proxy);
			const auto readOk = graphHolder->GetGraphVariableImplInt(RE::BSFixedString{ F4T::ProxyAnimationSync::GraphVar::kIsInSneak }, graphInSneak);
			const auto stanceOff = readOk && ((graphInSneak != 0) != a_isSneaking);
			const auto resendDue = stanceOff && a_state.stanceResentFor != a_isSneaking;
			if (resendDue) {
				a_state.stanceResentFor = a_isSneaking;
				const auto stanceResendAccepted =
					F4T::ProxyAnimationSync::detail::TrySendGraphEvent(a_proxy, a_isSneaking ? GraphEvent::kSneakStart : GraphEvent::kSneakStop);
				F4T::DebugLogging::Log(
					"ProxyAnim",
					"[RT-VERIFY 364-B] walk start stance resend for remote player {} actor={:08X}: wantedSneaking={}, iIsInSneak={}, refusedTicks={}, accepted={}.",
					a_remotePlayerId,
					actorId,
					a_isSneaking,
					graphInSneak,
					a_state.refusedTicks,
					stanceResendAccepted);
			}
			if (F4T::ProxySneakAction::ShouldTraceCount(a_state.refusedTicks / kStanceCheckTicks)) {
				F4T::DebugLogging::Log(
					"ProxyAnim",
					"[RT-VERIFY 364-B] walk start refused {} ticks for remote player {} actor={:08X}: iIsInSneak read={}, value={}, wantedSneaking={}, stanceOff={}, resendDue={}.",
					a_state.refusedTicks,
					a_remotePlayerId,
					actorId,
					readOk,
					graphInSneak,
					a_isSneaking,
					stanceOff,
					resendDue);
			}
		}

		if (SendEvents(a_proxy, a_isSneaking, a_remotePlayerId, a_state.refusedTicks)) {
			a_state.pending = false;
			a_state.stanceResentFor.reset();
			F4T::DebugLogging::Log(
				"ProxyAnim",
				"[RT-VERIFY 364-B] walk start accepted for remote player {} actor={:08X} after {} refused ticks.",
				a_remotePlayerId,
				actorId,
				a_state.refusedTicks);
		}
	}
}
