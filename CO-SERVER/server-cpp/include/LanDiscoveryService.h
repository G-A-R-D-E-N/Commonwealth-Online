#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace CommonwealthOnline::Server
{
	class LanDiscoveryService
	{
	public:
		static constexpr std::uint16_t DefaultPort = 7778;

		LanDiscoveryService(
			std::string a_serverName,
			std::string a_serverDescription,
			std::uint16_t a_gamePort,
			std::size_t a_maximumPlayers,
			const std::atomic_size_t& a_activePlayers,
			bool a_passworded,
			std::uint16_t a_discoveryPort = DefaultPort);
		~LanDiscoveryService();

		LanDiscoveryService(const LanDiscoveryService&) = delete;
		LanDiscoveryService& operator=(const LanDiscoveryService&) = delete;

		[[nodiscard]] bool Start(std::string& a_error);
		void Stop();
		void UpdateAdvertisement(std::string a_serverName, std::string a_serverDescription, std::size_t a_maximumPlayers, bool a_passworded);
		[[nodiscard]] std::uint16_t LocalPort() const noexcept;
		[[nodiscard]] bool Running() const noexcept { return _running.load(std::memory_order_acquire); }

	public:
		[[nodiscard]] static bool IsLocalNetworkSource(std::uint32_t a_hostOrderAddress) noexcept;

		static constexpr std::size_t MaximumRepliesPerSecond = 32;
		static constexpr std::uint32_t MaximumRepliesPerSourcePerSecond = 4;
		static constexpr std::size_t MaximumTrackedDiscoverySources = 256;

	private:
		void Run();
		[[nodiscard]] bool MayReplyTo(std::uint32_t a_hostOrderAddress, double a_nowSeconds);

		mutable std::mutex _advertisementMutex;
		std::string _serverName;
		std::string _serverDescription;
		std::uint16_t _gamePort{};
		std::size_t _maximumPlayers{};
		const std::atomic_size_t& _activePlayers;
		bool _passworded{};
		std::uint16_t _requestedPort{};
		std::atomic_uint16_t _localPort{};
		std::atomic_bool _running{};
		std::thread _thread;
		std::atomic_uintptr_t _socketHandle{};
		struct SourceBudget
		{
			double windowStartSeconds{};
			std::uint32_t replies{};
		};

		std::unordered_map<std::uint32_t, SourceBudget> _lastReplyBySource;
		double _replyWindowStartSeconds{};
		std::size_t _repliesInWindow{};
		bool _socketRuntimeHeld{};
	};
}
