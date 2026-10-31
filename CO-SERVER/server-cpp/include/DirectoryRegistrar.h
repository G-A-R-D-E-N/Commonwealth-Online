#pragma once

#include "WorkerExit.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace CommonwealthOnline::Server
{
	struct DirectoryHttpResponse
	{
		int status{};
		std::string body;
		std::string transportError;
	};

	using DirectoryHttpSender = std::function<DirectoryHttpResponse(
		std::string_view a_method,
		const std::string& a_url,
		const std::string& a_bearer,
		const std::string& a_body)>;

	struct DirectoryListing
	{
		std::string name;
		std::string description;
		std::uint16_t port{};
		std::size_t maximumPlayers{};
		std::optional<std::string> clientVersion;
		std::optional<std::string> falloutRuntime;
		std::optional<std::string> contentManifestFingerprint;
		std::vector<std::string> capabilities;
		bool passworded{};
		std::optional<std::string> modpackFingerprint;
		bool listensOnIpv6{};
	};

	enum class DirectoryState
	{
		Disabled,
		Registering,
		Registered,
		Error
	};

	struct DirectoryStatus
	{
		DirectoryState state{ DirectoryState::Disabled };
		std::string serverId;
		std::string publicAddress;
		std::string error;
		std::uint32_t consecutiveFailures{};
	};

	[[nodiscard]] std::string_view ToString(DirectoryState a_state) noexcept;
	[[nodiscard]] nlohmann::json ToJson(const DirectoryStatus& a_status);

	class DirectoryRegistrar
	{
	public:
		static constexpr std::chrono::seconds MinimumHeartbeat{ 5 };
		static constexpr std::chrono::seconds MaximumHeartbeat{ 300 };
		static constexpr std::chrono::seconds DefaultHeartbeat{ 30 };
		static constexpr std::chrono::seconds MaximumBackoff{ 300 };
		static constexpr std::chrono::seconds RejectedRetry{ 300 };
		static constexpr std::size_t MaximumResponseBytes = 64 * 1024;

		DirectoryRegistrar(
			std::string a_directoryUrl,
			DirectoryListing a_listing,
			const std::atomic_size_t& a_activePlayers,
			DirectoryHttpSender a_sender);
		~DirectoryRegistrar();

		DirectoryRegistrar(const DirectoryRegistrar&) = delete;
		DirectoryRegistrar& operator=(const DirectoryRegistrar&) = delete;

		[[nodiscard]] std::chrono::seconds Tick();
		[[nodiscard]] DirectoryListing ListingSnapshot() const;
		void Unregister();

		void Start();
		void Stop();

		[[nodiscard]] DirectoryStatus Status() const;
		void UpdateListing(DirectoryListing a_listing);
		[[nodiscard]] nlohmann::json BuildRegistrationBody() const;
		[[nodiscard]] nlohmann::json BuildHeartbeatBody() const;

	private:
		struct State
		{
			std::string directoryUrl;
			DirectoryListing listing;
			const std::atomic_size_t* activePlayers{};
			DirectoryHttpSender sender;
			mutable std::mutex statusMutex;
			DirectoryStatus status;
			std::string secret;
			std::chrono::seconds heartbeat{ DefaultHeartbeat };
		};

		[[nodiscard]] static std::chrono::seconds RegisterServer(State& a_state, std::size_t a_players, WorkerExit* a_exit);
		[[nodiscard]] static std::chrono::seconds SendHeartbeat(State& a_state, std::size_t a_players, WorkerExit* a_exit);
		[[nodiscard]] static std::chrono::seconds TickServer(State& a_state, std::size_t a_players, WorkerExit* a_exit);
		[[nodiscard]] static std::chrono::seconds FailStatus(State& a_state, std::string a_error, bool a_rejected);
		[[nodiscard]] static nlohmann::json RegistrationBody(const State& a_state, const DirectoryListing& a_listing, std::size_t a_players);
		[[nodiscard]] static nlohmann::json HeartbeatBody(const State& a_state, const DirectoryListing& a_listing, std::size_t a_players);
		static void Worker(std::shared_ptr<State> a_state, std::shared_ptr<WorkerExit> a_exit);

		std::shared_ptr<State> _state;
		std::shared_ptr<WorkerExit> _exit;
		std::thread _thread;
	};

	[[nodiscard]] DirectoryHttpSender CreatePlatformDirectorySender();
}
