#pragma once

#include <cstdint>
#include <vector>
#include <chrono>

#include "F4AnimationDescriptor.h"
#include "RE/A/Actor.h"
#include "RE/A/ActorState.h"

namespace F4T::RemoteActionComponent
{
	struct RemoteActionSnapshot
	{
		std::uint32_t actionType{};
		std::string eventName;
		AnimationDescriptor::AnimationVariableSnapshot animationVariables;
		std::chrono::steady_clock::time_point captureTime{};
	};

	class RemoteActionQueue
	{
	public:
		RemoteActionQueue() = default;

		RemoteActionQueue(const RemoteActionQueue&) = delete;
		RemoteActionQueue& operator=(const RemoteActionQueue&) = delete;

		RemoteActionQueue(RemoteActionQueue&&) noexcept = default;
		RemoteActionQueue& operator=(RemoteActionQueue&&) noexcept = default;

		void Enqueue(const RemoteActionSnapshot& a_action);

		void Clear();

		std::size_t GetPendingActionCount() const { return m_actionQueue.size(); }

		bool ReplayNextAction(RE::Actor& a_proxy);

	private:
		std::vector<RemoteActionSnapshot> m_actionQueue;
		std::size_t m_maxQueueSize{ 16 };
		bool m_readyAlertSentForFront{ false };

		bool ApplyActionToProxy(RE::Actor& a_proxy, const RemoteActionSnapshot& a_action);

		bool TriggerActionAnimation(RE::Actor& a_proxy, const RemoteActionSnapshot& a_action);
	};
}
