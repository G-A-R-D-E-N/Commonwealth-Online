#include "lab_relay.h"

#include <algorithm>
#include <utility>

#include <timeapi.h>

namespace VoiceLab
{
	namespace
	{
		constexpr int kMaxWaitMs = 20;
		constexpr std::size_t kMaxDatagramBytes = 65536;

		bool SameEndpoint(const sockaddr_in& a_left, const sockaddr_in& a_right) noexcept
		{
			return a_left.sin_addr.s_addr == a_right.sin_addr.s_addr && a_left.sin_port == a_right.sin_port;
		}

		bool RetryAfterReadError(DirectionCounters& a_counters) noexcept
		{
			const int error = ::WSAGetLastError();
			if (error == WSAECONNRESET)
			{
				return true;
			}
			if (error != WSAEWOULDBLOCK)
			{
				++a_counters.receiveErrors;
			}
			return false;
		}
	}

	SOCKET OpenLoopbackUdp(std::uint16_t a_port) noexcept
	{
		const SOCKET handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (handle == INVALID_SOCKET)
		{
			return INVALID_SOCKET;
		}
		const sockaddr_in address = LoopbackAddress(a_port);
		u_long nonBlocking = 1;
		if (::bind(handle, reinterpret_cast<const sockaddr*>(&address), static_cast<int>(sizeof(address))) != 0 ||
			::ioctlsocket(handle, FIONBIO, &nonBlocking) != 0)
		{
			::closesocket(handle);
			return INVALID_SOCKET;
		}
		return handle;
	}

	sockaddr_in LoopbackAddress(std::uint16_t a_port) noexcept
	{
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = htons(a_port);
		return address;
	}

	bool StartNetworking()
	{
		WSADATA wsa{};
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		{
			return false;
		}
		timeBeginPeriod(1);
		return true;
	}

	void StopNetworking() noexcept
	{
		timeEndPeriod(1);
		WSACleanup();
	}

	Relay::Relay(const RelayConfig& a_config) :
		m_listenPort(a_config.listenPort),
		m_random(a_config.seed),
		m_clientToTarget{ a_config.clientToTarget },
		m_targetToClient{ a_config.targetToClient },
		m_buffer(kMaxDatagramBytes),
		m_target(LoopbackAddress(a_config.targetPort))
	{
	}

	Relay::~Relay()
	{
		Stop();
	}

	bool Relay::Start()
	{
		m_listen = OpenLoopbackUdp(m_listenPort);
		if (m_listen == INVALID_SOCKET)
		{
			return false;
		}
		sockaddr_in bound{};
		int boundLength = static_cast<int>(sizeof(bound));
		if (::getsockname(m_listen, reinterpret_cast<sockaddr*>(&bound), &boundLength) != 0)
		{
			CloseSockets();
			return false;
		}
		m_listenPort = ntohs(bound.sin_port);
		m_running.store(true);
		m_thread = std::thread(&Relay::Run, this);
		return true;
	}

	void Relay::Stop()
	{
		m_running.store(false);
		if (m_thread.joinable())
		{
			m_thread.join();
		}
		CloseSockets();
	}

	RelayCounters Relay::Counters() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return RelayCounters{ m_clientToTarget.counters, m_targetToClient.counters };
	}

	std::uint16_t Relay::ListenPort() const
	{
		return m_listenPort;
	}

	void Relay::Run()
	{
		while (m_running.load())
		{
			fd_set readable{};
			FD_SET(m_listen, &readable);
			for (const auto& client : m_clients)
			{
				FD_SET(client.upstream, &readable);
			}
			const long long micros = WaitMicros(Clock::now());
			timeval timeout{};
			timeout.tv_sec = static_cast<long>(micros / 1000000);
			timeout.tv_usec = static_cast<long>(micros % 1000000);
			const int ready = ::select(0, &readable, nullptr, nullptr, &timeout);
			std::lock_guard<std::mutex> lock(m_mutex);
			if (ready > 0)
			{
				if (FD_ISSET(m_listen, &readable))
				{
					DrainListen();
				}
				const std::size_t clients = m_clients.size();
				for (std::size_t i = 0; i < clients; ++i)
				{
					if (FD_ISSET(m_clients[i].upstream, &readable))
					{
						DrainUpstream(i);
					}
				}
			}
			Release(Clock::now());
		}
	}

	void Relay::DrainListen()
	{
		for (;;)
		{
			sockaddr_in from{};
			int fromLength = static_cast<int>(sizeof(from));
			const int size = ::recvfrom(m_listen, m_buffer.data(), static_cast<int>(m_buffer.size()), 0, reinterpret_cast<sockaddr*>(&from), &fromLength);
			if (size == SOCKET_ERROR)
			{
				if (RetryAfterReadError(m_clientToTarget.counters))
				{
					continue;
				}
				return;
			}
			++m_clientToTarget.counters.datagrams;
			m_clientToTarget.counters.bytes += static_cast<std::uint64_t>(size);
			const auto client = ClientFor(from);
			if (client)
			{
				Schedule(m_clientToTarget, true, *client, std::vector<char>(m_buffer.begin(), m_buffer.begin() + size), Clock::now());
			}
			else
			{
				++m_clientToTarget.counters.failed;
			}
		}
	}

	void Relay::DrainUpstream(std::size_t a_client)
	{
		const SOCKET upstream = m_clients[a_client].upstream;
		for (;;)
		{
			const int size = ::recv(upstream, m_buffer.data(), static_cast<int>(m_buffer.size()), 0);
			if (size == SOCKET_ERROR)
			{
				if (RetryAfterReadError(m_targetToClient.counters))
				{
					continue;
				}
				return;
			}
			++m_targetToClient.counters.datagrams;
			m_targetToClient.counters.bytes += static_cast<std::uint64_t>(size);
			Schedule(m_targetToClient, false, a_client, std::vector<char>(m_buffer.begin(), m_buffer.begin() + size), Clock::now());
		}
	}

	std::optional<std::size_t> Relay::ClientFor(const sockaddr_in& a_from)
	{
		for (std::size_t i = 0; i < m_clients.size(); ++i)
		{
			if (SameEndpoint(m_clients[i].address, a_from))
			{
				return i;
			}
		}
		const SOCKET upstream = OpenLoopbackUdp(0);
		if (upstream == INVALID_SOCKET)
		{
			return std::nullopt;
		}
		m_clients.push_back(Client{ a_from, upstream });
		return m_clients.size() - 1;
	}

	void Relay::Schedule(Lane& a_lane, bool a_toTarget, std::size_t a_client, std::vector<char>&& a_bytes, Clock::time_point a_now)
	{
		std::uniform_real_distribution<double> lossRoll(0.0, 100.0);
		if (lossRoll(m_random) < a_lane.impairment.lossPercent)
		{
			++a_lane.counters.droppedLoss;
			return;
		}
		auto departure = a_now;
		const double capacity = a_lane.impairment.capBytesPerSecond;
		if (capacity > 0.0)
		{
			const double bytes = static_cast<double>(a_bytes.size());
			const double queued = a_lane.linkFree > a_now ? std::chrono::duration<double>(a_lane.linkFree - a_now).count() * capacity : 0.0;
			if (queued + bytes > capacity)
			{
				++a_lane.counters.droppedQueue;
				return;
			}
			departure = (std::max)(a_now, a_lane.linkFree) + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(bytes / capacity));
			a_lane.linkFree = departure;
		}
		std::uniform_int_distribution<int> jitterRoll(0, a_lane.impairment.jitterMaxMs);
		const auto delay = std::chrono::milliseconds(a_lane.impairment.fixedDelayMs + jitterRoll(m_random));
		m_queue.emplace(departure + delay, Pending{ a_client, a_toTarget, std::move(a_bytes) });
	}

	void Relay::Release(Clock::time_point a_now)
	{
		while (!m_queue.empty() && m_queue.begin()->first <= a_now)
		{
			auto node = m_queue.extract(m_queue.begin());
			const Pending& pending = node.mapped();
			const Client& client = m_clients[pending.client];
			Lane& lane = pending.toTarget ? m_clientToTarget : m_targetToClient;
			const SOCKET sender = pending.toTarget ? client.upstream : m_listen;
			const sockaddr* destination = pending.toTarget ? reinterpret_cast<const sockaddr*>(&m_target) : reinterpret_cast<const sockaddr*>(&client.address);
			const int size = static_cast<int>(pending.bytes.size());
			if (::sendto(sender, pending.bytes.data(), size, 0, destination, static_cast<int>(sizeof(sockaddr_in))) == size)
			{
				++lane.counters.forwarded;
			}
			else
			{
				++lane.counters.failed;
			}
		}
	}

	long long Relay::WaitMicros(Clock::time_point a_now) const
	{
		auto wait = std::chrono::microseconds(kMaxWaitMs * 1000);
		if (!m_queue.empty())
		{
			wait = (std::min)(wait, std::chrono::duration_cast<std::chrono::microseconds>(m_queue.begin()->first - a_now));
		}
		return (std::max)(wait, std::chrono::microseconds(0)).count();
	}

	void Relay::CloseSockets() noexcept
	{
		for (const auto& entry : m_queue)
		{
			Lane& lane = entry.second.toTarget ? m_clientToTarget : m_targetToClient;
			++lane.counters.abandoned;
		}
		for (const auto& client : m_clients)
		{
			::closesocket(client.upstream);
		}
		m_clients.clear();
		m_queue.clear();
		if (m_listen != INVALID_SOCKET)
		{
			::closesocket(m_listen);
			m_listen = INVALID_SOCKET;
		}
	}
}
