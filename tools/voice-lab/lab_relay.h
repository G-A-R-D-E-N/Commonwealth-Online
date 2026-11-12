#pragma once

#include <winsock2.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <thread>
#include <vector>

namespace VoiceLab
{
	struct Impairment
	{
		double lossPercent{};
		int fixedDelayMs{};
		int jitterMaxMs{};
		double capBytesPerSecond{};
	};

	struct DirectionCounters
	{
		std::uint64_t datagrams{};
		std::uint64_t bytes{};
		std::uint64_t droppedLoss{};
		std::uint64_t droppedQueue{};
		std::uint64_t forwarded{};
		std::uint64_t failed{};
		std::uint64_t abandoned{};
		std::uint64_t receiveErrors{};
	};

	struct RelayCounters
	{
		DirectionCounters clientToTarget{};
		DirectionCounters targetToClient{};
	};

	struct RelayConfig
	{
		std::uint16_t listenPort{};
		std::uint16_t targetPort{};
		Impairment clientToTarget{};
		Impairment targetToClient{};
		std::uint32_t seed{};
	};

	SOCKET OpenLoopbackUdp(std::uint16_t a_port) noexcept;
	sockaddr_in LoopbackAddress(std::uint16_t a_port) noexcept;
	bool StartNetworking();
	void StopNetworking() noexcept;

	class Relay
	{
	public:
		explicit Relay(const RelayConfig& a_config);
		~Relay();
		Relay(const Relay&) = delete;
		Relay& operator=(const Relay&) = delete;

		bool Start();
		void Stop();
		RelayCounters Counters() const;
		std::uint16_t ListenPort() const;

	private:
		using Clock = std::chrono::steady_clock;

		struct Lane
		{
			Impairment impairment{};
			DirectionCounters counters{};
			Clock::time_point linkFree{};
		};

		struct Client
		{
			sockaddr_in address{};
			SOCKET upstream{ INVALID_SOCKET };
		};

		struct Pending
		{
			std::size_t client{};
			bool toTarget{};
			std::vector<char> bytes{};
		};

		void Run();
		void DrainListen();
		void DrainUpstream(std::size_t a_client);
		std::optional<std::size_t> ClientFor(const sockaddr_in& a_from);
		void Schedule(Lane& a_lane, bool a_toTarget, std::size_t a_client, std::vector<char>&& a_bytes, Clock::time_point a_now);
		void Release(Clock::time_point a_now);
		long long WaitMicros(Clock::time_point a_now) const;
		void CloseSockets() noexcept;

		std::uint16_t m_listenPort{};
		std::mt19937 m_random;
		Lane m_clientToTarget;
		Lane m_targetToClient;
		std::vector<char> m_buffer;
		SOCKET m_listen{ INVALID_SOCKET };
		sockaddr_in m_target{};
		std::vector<Client> m_clients;
		std::multimap<Clock::time_point, Pending> m_queue;
		std::atomic<bool> m_running{ false };
		mutable std::mutex m_mutex;
		std::thread m_thread;
	};
}
