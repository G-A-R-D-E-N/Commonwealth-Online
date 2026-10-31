#pragma once

#include "F4TConnectFailure.h"

#include <mutex>
#include <optional>
#include <utility>

namespace F4T
{
	struct ValidationAttempt
	{
		using ConnectFailure = Networking::ConnectFailure;
		using ConnectFailureKind = Networking::ConnectFailureKind;

		void Publish(ConnectFailure a_failure)
		{
			const std::scoped_lock lock(mutex);
			if (!failure ||
				Networking::ConnectFailurePriority(a_failure.kind) > Networking::ConnectFailurePriority(failure->kind)) {
				failure = std::move(a_failure);
			}
		}

		std::optional<ConnectFailure> Take()
		{
			const std::scoped_lock lock(mutex);
			auto result = std::move(failure);
			failure.reset();
			return result;
		}

	private:
		std::mutex mutex;
		std::optional<ConnectFailure> failure;
	};
}
