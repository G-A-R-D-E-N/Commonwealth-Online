#include "pch.h"

#include "F4TJoinFlow.h"
#include "F4TNetworking.h"
#include "F4TReconnect.h"
#include "F4TRemotePlayerState.h"
#include "F4TSessionSaveGuard.h"
#include "F4TUIClient.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace
{
	using namespace std::chrono_literals;

	constexpr auto kLossSaveWait = 20s;
	constexpr auto kLossSavePoll = 25ms;
	std::atomic_bool g_lossSaveHold{ false };
	thread_local bool g_resumeAttempt = false;
}

namespace
{
	bool ShouldSaveBeforeLossClose()
	{
		return !F4T::UIClient::IsJoinInProgress() && !F4T::UIClient::IsVoluntaryExitRequested() &&
		       !F4T::SessionSaveGuard::IsSessionExitPending() && F4T::RemotePlayerState::GetAssignedPlayerId().has_value() && !F4T::Reconnect::IsActive();
	}
}

namespace F4T::Networking
{
	bool IsLossSaveHeld()
	{
		return g_lossSaveHold.load();
	}

	void SetResumeAttempt(bool a_resume)
	{
		g_resumeAttempt = a_resume;
	}

	bool IsResumeAttempt()
	{
		return g_resumeAttempt;
	}

	void CloseAfterLossSave(const std::atomic_bool& a_receiveRunning, void (*a_close)())
	{
		F4T::Reconnect::MarkTransportLost();
		if (!a_receiveRunning.load() || F4T::JoinFlow::IsActive() || !ShouldSaveBeforeLossClose()) {
			a_close();
			return;
		}
		const auto saved = std::make_shared<std::atomic_bool>(false);
		g_lossSaveHold.store(true);
		if (const auto* taskInterface = F4SE::GetTaskInterface()) {
			taskInterface->AddTask([saved]() {
				F4T::SessionSaveGuard::RunOrDeferSessionExit("connection lost", [saved]() { saved->store(true); });
			});
			const auto deadline = std::chrono::steady_clock::now() + kLossSaveWait;
			while (!saved->load() && a_receiveRunning.load() && std::chrono::steady_clock::now() < deadline) {
				std::this_thread::sleep_for(kLossSavePoll);
			}
		}
		a_close();
		g_lossSaveHold.store(false);
	}
}
