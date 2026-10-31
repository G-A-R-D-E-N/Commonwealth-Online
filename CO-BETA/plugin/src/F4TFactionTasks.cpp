#include "pch.h"

#include "F4TFactions.h"
#include "F4TNetworkingInternal.h"
#include "F4TProxyActorController.h"
#include "F4TUIClient.h"

#include <functional>

namespace F4T::Factions
{
	void QueueGameTask(std::function<void()> a_task)
	{
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			F4T::Networking::Detail::LogWarningWithLocalPlayerPrefix("Cannot queue faction work: task interface unavailable.");
			return;
		}
		taskInterface->AddTask(std::move(a_task));
	}

	void QueueStateChanged()
	{
		QueueGameTask([]() { F4T::UIClient::NotifyFactionStateChanged(); });
	}

	void QueueKnownSetChanged()
	{
		QueueGameTask([]() { F4T::ProxyActorController::NoteKnownSetChanged(); });
	}
}
