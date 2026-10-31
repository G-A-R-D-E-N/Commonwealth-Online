#pragma once

#include "F4TF4SECapabilityPolicy.h"

#include <functional>
#include <memory>

namespace F4T::F4SECapabilities
{
	template <class TaskInterface, class Version>
	void SchedulePermanentTask(const TaskInterface* a_tasks, const Version& a_f4seVersion, std::function<void()> a_task)
	{
		if (!a_tasks) {
			return;
		}
		if (SupportsPermanentTasks(a_f4seVersion.major(), a_f4seVersion.minor())) {
			a_tasks->AddTaskPermanent(std::move(a_task));
			return;
		}
		// F4SE 0.6.x drains each queue until empty, so a re-armed task must hop through the other queue.
		auto task = std::make_shared<std::function<void()>>(std::move(a_task));
		auto tick = std::make_shared<std::function<void()>>();
		*tick = [a_tasks, task, tick]() {
			(*task)();
			a_tasks->AddUITask([a_tasks, tick]() { a_tasks->AddTask(*tick); });
		};
		a_tasks->AddTask(*tick);
	}
}
