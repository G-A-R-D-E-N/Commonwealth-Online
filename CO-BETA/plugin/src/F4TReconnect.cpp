#include "pch.h"

#include "F4TReconnect.h"

#include "F4TCharacterSlotsClient.h"
#include "F4TDebugLogging.h"
#include "F4TJoinFlow.h"
#include "F4TNetworking.h"
#include "F4TRemotePlayerState.h"
#include "F4TSecretWipe.h"
#include "F4TSessionSaveGuard.h"
#include "F4TTradeClient.h"
#include "F4TUIClient.h"
#include "F4TUIClientJoin.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace F4T::Reconnect
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr auto kReconnectWindow = std::chrono::seconds(55);
		constexpr auto kReconnectInterval = std::chrono::seconds(3);
		constexpr auto kPollInterval = std::chrono::milliseconds(25);

		enum class Attempt
		{
			resumed,
			retry,
			stop,
			rejoin,
		};

		std::mutex g_mutex{};
		bool g_active{};
		bool g_cancelled{};
		std::optional<Target> g_target{};
		std::optional<Clock::time_point> g_lostAt{};
		thread_local bool t_worker{};

		void WipePassword(std::optional<std::string>& a_password)
		{
			if (a_password) {
				F4T::Secret::Wipe(*a_password);
			}
		}

		void WipeTarget(std::optional<Target>& a_target)
		{
			if (a_target) {
				WipePassword(a_target->serverPassword);
			}
			a_target.reset();
		}

		bool Cancelled()
		{
			std::scoped_lock lock{ g_mutex };
			return g_cancelled || F4T::UIClient::IsVoluntaryExitRequested();
		}

		void Finish()
		{
			std::scoped_lock lock{ g_mutex };
			g_active = false;
			g_cancelled = false;
		}

		bool FinishUnlessCancelled()
		{
			std::scoped_lock lock{ g_mutex };
			if (g_cancelled || F4T::UIClient::IsVoluntaryExitRequested()) {
				return false;
			}
			g_active = false;
			return true;
		}

		void Rearm()
		{
			std::scoped_lock lock{ g_mutex };
			g_active = true;
		}

		bool CommitResume(const Target& a_target)
		{
			std::scoped_lock lock{ g_mutex };
			if (g_cancelled || !F4T::Networking::IsConnectedToServer()) {
				return false;
			}
			WipeTarget(g_target);
			g_target = a_target;
			g_active = false;
			g_cancelled = false;
			return true;
		}

		Attempt TryResumeOnce(Target& a_target, std::optional<std::uint32_t> a_previousPlayerId, Clock::time_point a_deadline)
		{
			struct ResumeScope
			{
				ResumeScope() { F4T::Networking::SetResumeAttempt(true); }
				~ResumeScope() { F4T::Networking::SetResumeAttempt(false); }
			} const resumeScope;
			F4T::RemotePlayerState::ClearAssignedPlayerId();
			const auto listSequence = F4T::CharacterSlotsClient::ListStateSequence();
			std::string error;
			std::optional<F4T::Networking::ConnectFailure> failure;
			if (!F4T::Networking::ConnectAndValidateServerOnce(
					a_target.host.c_str(),
					a_target.port,
					error,
					&failure,
					a_target.serverPassword,
					a_target.modpackFingerprint,
					a_target.joinId,
					false,
					[a_deadline] { return Cancelled() || Clock::now() >= a_deadline; }, true)) {
				if (failure && (failure->code == "banned" || failure->code == "profile_banned" || failure->code == "kicked")) {
					F4T::DebugLogging::Log("Reconnect", "resume stopped: the server refused the player code={}", failure->code);
					return Attempt::stop;
				}
				if (failure && failure->code == "modpack_mismatch" && !failure->matchingModpackFingerprint.empty()) {
					a_target.modpackFingerprint = failure->matchingModpackFingerprint;
				}
				return Attempt::retry;
			}
			while (F4T::Networking::IsConnectedToServer() && (F4T::CharacterSlotsClient::ListStateSequence() == listSequence || !F4T::RemotePlayerState::GetAssignedPlayerId()) &&
				Clock::now() < a_deadline && !Cancelled()) {
				std::this_thread::sleep_for(kPollInterval);
			}
			const bool replied = F4T::CharacterSlotsClient::ListStateSequence() != listSequence && F4T::RemotePlayerState::GetAssignedPlayerId();
			const bool verified = replied && Clock::now() < a_deadline &&
				a_previousPlayerId && F4T::RemotePlayerState::GetAssignedPlayerId() == a_previousPlayerId &&
				F4T::RemotePlayerState::GetAssignedPlayerResumed() &&
				!F4T::CharacterSlotsClient::NeedsSelection();
			if (verified && CommitResume(a_target)) {
				return Attempt::resumed;
			}
			if (!Cancelled() || !F4T::SessionSaveGuard::IsSessionExitPending()) {
				F4T::Networking::DisconnectFromLocalServer();
			}
			if (replied && !verified) {
				return Cancelled() ? Attempt::stop : Attempt::rejoin;
			}
			return Cancelled() ? Attempt::stop : Attempt::retry;
		}

		void OnRejoinComplete(bool a_success, const char* a_error, void*)
		{
			if (a_success) {
				return;
			}
			F4T::UIClient::NotifyTransportDisconnected(a_error && *a_error ? a_error : "Could not reconnect to the server.");
		}

		bool IsTransientRefusal(std::string_view a_error)
		{
			return a_error == "Finishing the previous disconnect; try again shortly." ||
				a_error == "Saving the current session before disconnect; try again shortly.";
		}

		bool RejoinThroughJoinPath(const Target& a_target, Clock::time_point a_deadline)
		{
			F4T::UIClient::PushConnectionStatus("Rejoining the server...");
			COMMONWEALTH_ONLINE_UI_API::JoinRequest request{};
			request.id = a_target.joinId.c_str();
			request.host = a_target.host.c_str();
			request.port = a_target.port;
			char errorBuf[512]{};
			bool started = false;
			while (FinishUnlessCancelled()) {
				errorBuf[0] = '\0';
				started = F4T::UIClient::BeginJoin(&request, a_target.serverPassword, OnRejoinComplete, nullptr, errorBuf, sizeof(errorBuf));
				if (started || !IsTransientRefusal(errorBuf) || Clock::now() + kReconnectInterval >= a_deadline) {
					break;
				}
				Rearm();
				const auto nextAttempt = Clock::now() + kReconnectInterval;
				while (Clock::now() < nextAttempt && !Cancelled()) {
					std::this_thread::sleep_for(kPollInterval);
				}
			}
			if (!started) {
				F4T::DebugLogging::Log("Reconnect", "rejoin could not start error={}", std::string{ errorBuf });
			}
			return started;
		}

		void RunWindow(Target a_target, std::optional<std::uint32_t> a_previousPlayerId, Clock::time_point a_lostAt)
		{
			t_worker = true;
			F4T::UIClient::PushConnectionStatus("Reconnecting...");
			const auto deadline = a_lostAt + kReconnectWindow;
			bool resumed = false;
			bool rejoin = false;
			while (!Cancelled() && Clock::now() < deadline) {
				const auto attemptStart = Clock::now();
				const auto outcome = TryResumeOnce(a_target, a_previousPlayerId, deadline);
				if (outcome == Attempt::resumed) {
					resumed = true;
					break;
				}
				if (outcome == Attempt::rejoin) {
					rejoin = true;
					break;
				}
				if (outcome == Attempt::stop) {
					break;
				}
				const auto nextAttempt = std::min(attemptStart + kReconnectInterval, deadline);
				while (Clock::now() < nextAttempt && !Cancelled()) {
					std::this_thread::sleep_for(kPollInterval);
				}
			}
			if (!resumed) {
				if (const auto* taskInterface = F4SE::GetTaskInterface()) {
					taskInterface->AddTask([]() {
						F4T::JoinFlow::ReleaseVaultExitControls("reconnect", true);
					});
				}
			}
			const bool rejoined = rejoin && RejoinThroughJoinPath(a_target, deadline);
			WipePassword(a_target.serverPassword);
			if (rejoined) {
				return;
			}
			if (resumed) {
				F4T::DebugLogging::Log("Reconnect", "session resumed under the same player id");
				F4T::UIClient::PushConnectionStatus();
				return;
			}
			if (Cancelled() || (rejoin && F4T::UIClient::IsJoinInProgress())) {
				F4T::TradeClient::Reset();
				Finish();
				F4T::UIClient::PushConnectionStatus();
				return;
			}
			F4T::DebugLogging::Log("Reconnect", "window ended without a resume");
			F4T::TradeClient::Reset();
			F4T::UIClient::NotifyTransportDisconnected("Could not reconnect to the server.");
			Finish();
		}
	}

	void Remember(Target a_target)
	{
		std::scoped_lock lock{ g_mutex };
		WipeTarget(g_target);
		g_target = std::move(a_target);
		g_lostAt.reset();
	}

	void MarkTransportLost()
	{
		std::scoped_lock lock{ g_mutex };
		g_lostAt = Clock::now();
	}

	void Forget()
	{
		std::scoped_lock lock{ g_mutex };
		if (g_active && !t_worker) {
			g_cancelled = true;
		}
		WipeTarget(g_target);
	}

	bool TryArm()
	{
		const auto previousPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
		std::optional<Target> armed;
		Clock::time_point lostAt{};
		{
			std::scoped_lock lock{ g_mutex };
			lostAt = g_lostAt.value_or(Clock::now());
			g_lostAt.reset();
			if (g_active || !g_target || !previousPlayerId) {
				return false;
			}
			g_active = true;
			g_cancelled = false;
			armed = g_target;
		}
		try {
			std::thread([target = std::move(*armed), previousPlayerId, lostAt]() mutable {
				RunWindow(std::move(target), previousPlayerId, lostAt);
			}).detach();
		} catch (const std::exception& a_error) {
			Finish();
			F4T::DebugLogging::Log("Reconnect", "reconnect worker start failed error={}", a_error.what());
			return false;
		}
		return true;
	}

	bool IsActive()
	{
		std::scoped_lock lock{ g_mutex };
		return g_active;
	}

	bool OnWorkerThread()
	{
		return t_worker;
	}

	void Cancel()
	{
		std::scoped_lock lock{ g_mutex };
		if (g_active) {
			g_cancelled = true;
		}
		WipeTarget(g_target);
	}
}
