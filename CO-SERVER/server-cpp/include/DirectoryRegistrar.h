#pragma once

#include "WorkerExit.h"

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server
{
	struct DirectoryHttpResponse
	{
		int status{};
		std::string body;
		std::string transportError;
	};

	class DirectoryHttpSender
	{
	public:
		using Legacy = std::function<DirectoryHttpResponse(
			std::string_view a_method,
			const std::string& a_url,
			const std::string& a_bearer,
			const std::string& a_body)>;
		using Scoped = std::function<DirectoryHttpResponse(
			std::string_view a_method,
			const std::string& a_url,
			const std::string& a_bearer,
			const std::string& a_body,
			bool a_ipv4Only)>;

		DirectoryHttpSender() = default;

		template <typename F>
			requires(!std::same_as<std::remove_cvref_t<F>, DirectoryHttpSender> &&
				std::is_invocable_r_v<DirectoryHttpResponse, F&, std::string_view, const std::string&, const std::string&, const std::string&, bool>)
		DirectoryHttpSender(F a_transport) :
			_transport(std::move(a_transport))
		{}

		template <typename F>
			requires(!std::same_as<std::remove_cvref_t<F>, DirectoryHttpSender> &&
				std::is_invocable_r_v<DirectoryHttpResponse, F&, std::string_view, const std::string&, const std::string&, const std::string&> &&
				!std::is_invocable_r_v<DirectoryHttpResponse, F&, std::string_view, const std::string&, const std::string&, const std::string&, bool>)
		DirectoryHttpSender(F a_legacy) :
			_transport([legacy = std::move(a_legacy)](std::string_view a_method, const std::string& a_url, const std::string& a_bearer, const std::string& a_body, bool) {
				return legacy(a_method, a_url, a_bearer, a_body);
			})
		{}

		[[nodiscard]] DirectoryHttpResponse operator()(
			std::string_view a_method,
			const std::string& a_url,
			const std::string& a_bearer,
			const std::string& a_body,
			bool a_ipv4Only = false) const
		{
			if (!_transport) {
				return DirectoryHttpResponse{ 0, {}, "no HTTP client is available" };
			}
			return _transport(a_method, a_url, a_bearer, a_body, a_ipv4Only);
		}

		explicit operator bool() const noexcept { return static_cast<bool>(_transport); }

	private:
		Scoped _transport;
	};

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

	[[nodiscard]] std::string DescribeDirectoryFailure(const DirectoryHttpResponse& a_response);
	[[nodiscard]] std::string PublicListingStartupNotice(std::uint16_t a_gamePort, std::string_view a_directoryUrl, bool a_listensOnIpv6);

	[[nodiscard]] inline std::span<const std::string_view> DirectoryCaBundleCandidates() noexcept
	{
		static constexpr std::array<std::string_view, 3> candidates{
			"/etc/ssl/certs/ca-certificates.crt",
			"/etc/pki/tls/certs/ca-bundle.crt",
			"/etc/ssl/cert.pem",
		};
		return candidates;
	}

	[[nodiscard]] inline std::optional<std::string> SelectDirectoryCaBundle(
		std::span<const std::string_view> a_candidates,
		const std::function<bool(std::string_view)>& a_exists)
	{
		for (const auto candidate : a_candidates) {
			if (a_exists(candidate)) {
				return std::string{ candidate };
			}
		}
		return std::nullopt;
	}

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
