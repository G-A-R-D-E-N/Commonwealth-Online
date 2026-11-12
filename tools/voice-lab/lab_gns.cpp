#include "lab_gns.h"

#include "lab_relay.h"
#include "lab_stats.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using Clock = std::chrono::steady_clock;
	using VoiceLab::Bridge;
	using VoiceLab::DestroyFn;
	using VoiceLab::GnsEvent;
	using VoiceLab::Handle;

	constexpr std::uint32_t kDefaultSeed = 20261022U;
	constexpr int kDefaultSeconds = 60;
	constexpr int kDefaultSenders = 4;
	constexpr int kMaxSenders = 63;
	constexpr int kDefaultRate = 50;
	constexpr int kMaxRate = 1000;
	constexpr std::uint32_t kDeliveryUnreliableSequenced = 0U;
	constexpr std::uint32_t kEventConnected = 1U;
	constexpr std::uint32_t kEventDisconnected = 2U;
	constexpr std::uint32_t kEventMessage = 3U;
	constexpr int kSendSent = 0;
	constexpr std::size_t kMinPayloadBytes = 60;
	constexpr std::size_t kMaxPayloadBytes = 100;
	constexpr std::size_t kHeaderBytes = 2 * sizeof(std::uint32_t);
	constexpr std::size_t kPollBufferBytes = 64 * 1024;
	constexpr std::size_t kErrorBufferBytes = 256;
	constexpr int kConnectTimeoutSeconds = 10;
	constexpr int kDrainMs = 1000;
	constexpr int kLoopSleepMs = 1;
	constexpr std::int32_t kDisconnectReason = 1000;
	constexpr double kDeliveryLimitPercent = 99.5;
	constexpr double kPercentScale = 100.0;
	constexpr double kUdpIpv4HeaderBytes = 28.0;
	constexpr char kLoopbackHost[] = "127.0.0.1";

	struct GnsOptions
	{
		std::string bridgePath;
		int seconds{ kDefaultSeconds };
		int senders{ kDefaultSenders };
		int rate{ kDefaultRate };
		std::uint32_t seed{ kDefaultSeed };
	};

	struct Sender
	{
		std::unique_ptr<void, DestroyFn> client{ nullptr, nullptr };
		std::uint32_t index{};
		std::uint32_t counter{};
		bool connected{};
		std::uint64_t nominal{};
		std::uint64_t sent{};
		std::uint64_t refused{};
		std::uint64_t payloadBytes{};
		std::uint64_t delivered{};
		std::uint64_t duplicates{};
		std::uint64_t disconnects{};
		std::set<std::uint32_t> seen{};
		Clock::duration interval{};
		Clock::time_point nextDue{};
	};

	struct Harness
	{
		Harness(const Bridge& a_bridge, std::uint32_t a_seed) :
			bridge(a_bridge),
			random(a_seed),
			buffer(kPollBufferBytes)
		{
		}

		const Bridge& bridge;
		std::unique_ptr<void, DestroyFn> server{ nullptr, nullptr };
		std::vector<Sender> senders;
		std::mt19937 random;
		std::vector<char> buffer;
		std::uint64_t malformed{};
		bool bridgeFailed{};
	};

	bool ParseGns(int a_argc, char** a_argv, GnsOptions& a_options)
	{
		if (a_argc < 4 || (a_argc - 2) % 2 != 0)
		{
			return false;
		}
		for (int i = 2; i < a_argc; i += 2)
		{
			const std::string flag = a_argv[i];
			const char* value = a_argv[i + 1];
			if (flag == "--bridge")
			{
				a_options.bridgePath = value;
			}
			else if (flag == "--seconds")
			{
				a_options.seconds = std::atoi(value);
			}
			else if (flag == "--senders")
			{
				a_options.senders = std::atoi(value);
			}
			else if (flag == "--rate")
			{
				a_options.rate = std::atoi(value);
			}
			else if (flag == "--seed")
			{
				a_options.seed = static_cast<std::uint32_t>(std::strtoul(value, nullptr, 10));
			}
			else
			{
				return false;
			}
		}
		return !a_options.bridgePath.empty() && a_options.seconds >= 1 && a_options.senders >= 1 && a_options.senders <= kMaxSenders && a_options.rate >= 1 &&
			a_options.rate <= kMaxRate;
	}

	template <typename Fn>
	bool Resolve(HMODULE a_module, const char* a_name, Fn& a_function)
	{
		a_function = reinterpret_cast<Fn>(GetProcAddress(a_module, a_name));
		if (a_function == nullptr)
		{
			std::fprintf(stderr, "voice-lab gns: bridge has no export %s (GetLastError %lu)\n", a_name, GetLastError());
		}
		return a_function != nullptr;
	}

	std::uint16_t FreeLoopbackPort()
	{
		const SOCKET socket = VoiceLab::OpenLoopbackUdp(0);
		if (socket == INVALID_SOCKET)
		{
			return 0;
		}
		sockaddr_in address{};
		int length = static_cast<int>(sizeof(address));
		const bool named = ::getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) == 0;
		::closesocket(socket);
		return named ? ntohs(address.sin_port) : 0;
	}

	void Account(Harness& a_harness, std::uint32_t a_size)
	{
		if (a_size < kHeaderBytes)
		{
			++a_harness.malformed;
			return;
		}
		std::uint32_t index{};
		std::uint32_t counter{};
		std::memcpy(&index, a_harness.buffer.data(), sizeof(index));
		std::memcpy(&counter, a_harness.buffer.data() + sizeof(index), sizeof(counter));
		if (index >= a_harness.senders.size())
		{
			++a_harness.malformed;
			return;
		}
		Sender& sender = a_harness.senders[index];
		if (sender.seen.insert(counter).second)
		{
			++sender.delivered;
		}
		else
		{
			++sender.duplicates;
		}
	}

	void ReportPollError(Harness& a_harness, const char* a_side, int a_result)
	{
		if (a_result < 0 && !a_harness.bridgeFailed)
		{
			std::fprintf(stderr, "voice-lab gns: %s poll returned %d (bridge error)\n", a_side, a_result);
			a_harness.bridgeFailed = true;
		}
	}

	void PollServer(Harness& a_harness)
	{
		GnsEvent event{};
		int result = 0;
		while ((result = a_harness.bridge.serverPoll(a_harness.server.get(), &event, a_harness.buffer.data(), static_cast<std::uint32_t>(a_harness.buffer.size()))) > 0)
		{
			if (event.type == kEventMessage)
			{
				Account(a_harness, event.payloadSize);
			}
		}
		ReportPollError(a_harness, "server", result);
	}

	void PollClients(Harness& a_harness)
	{
		GnsEvent event{};
		for (Sender& sender : a_harness.senders)
		{
			int result = 0;
			while ((result = a_harness.bridge.clientPoll(sender.client.get(), &event, a_harness.buffer.data(), static_cast<std::uint32_t>(a_harness.buffer.size()))) > 0)
			{
				if (event.type == kEventConnected)
				{
					sender.connected = true;
				}
				else if (event.type == kEventDisconnected)
				{
					++sender.disconnects;
				}
			}
			ReportPollError(a_harness, "client", result);
		}
	}

	void Pump(Harness& a_harness)
	{
		PollServer(a_harness);
		PollClients(a_harness);
		std::this_thread::sleep_for(std::chrono::milliseconds(kLoopSleepMs));
	}

	bool AllConnected(const Harness& a_harness)
	{
		const std::uint32_t connections = a_harness.bridge.serverConnectionCount(a_harness.server.get());
		return connections >= a_harness.senders.size() &&
			std::all_of(a_harness.senders.begin(), a_harness.senders.end(), [](const Sender& a_sender) { return a_sender.connected; });
	}

	void SendOne(Harness& a_harness, Sender& a_sender)
	{
		std::uniform_int_distribution<std::size_t> sizeRoll(kMinPayloadBytes, kMaxPayloadBytes);
		std::vector<char> payload(sizeRoll(a_harness.random));
		std::memcpy(payload.data(), &a_sender.index, sizeof(a_sender.index));
		std::memcpy(payload.data() + sizeof(a_sender.index), &a_sender.counter, sizeof(a_sender.counter));
		const int result = a_harness.bridge.clientSend(a_sender.client.get(), payload.data(), static_cast<std::uint32_t>(payload.size()), kDeliveryUnreliableSequenced);
		if (result != kSendSent)
		{
			++a_sender.refused;
			return;
		}
		++a_sender.counter;
		++a_sender.sent;
		a_sender.payloadBytes += payload.size();
	}

	void SendDue(Harness& a_harness, Sender& a_sender, Clock::time_point a_until)
	{
		while (a_sender.nextDue <= a_until)
		{
			++a_sender.nominal;
			SendOne(a_harness, a_sender);
			a_sender.nextDue += a_sender.interval;
		}
	}

	double Ratio(double a_numerator, double a_denominator)
	{
		return a_denominator > 0.0 ? a_numerator / a_denominator : 0.0;
	}

	int Report(const std::vector<Sender>& a_senders, const VoiceLab::RelayCounters& a_before, const VoiceLab::RelayCounters& a_after,
		double a_seconds, std::uint64_t a_malformed)
	{
		std::uint64_t nominal = 0;
		std::uint64_t sent = 0;
		std::uint64_t delivered = 0;
		std::uint64_t duplicates = 0;
		std::uint64_t refused = 0;
		std::uint64_t disconnects = 0;
		std::uint64_t payloadBytes = 0;
		for (const Sender& sender : a_senders)
		{
			std::printf("sender %u nominal %llu sent %llu refused %llu delivered %llu duplicates %llu disconnects %llu\n", sender.index,
				sender.nominal, sender.sent, sender.refused, sender.delivered, sender.duplicates, sender.disconnects);
			nominal += sender.nominal;
			sent += sender.sent;
			delivered += sender.delivered;
			duplicates += sender.duplicates;
			refused += sender.refused;
			disconnects += sender.disconnects;
			payloadBytes += sender.payloadBytes;
		}
		const double deliveryPercent = kPercentScale * Ratio(static_cast<double>(delivered), static_cast<double>(nominal));
		std::printf("total nominal %llu\n", nominal);
		std::printf("total sent %llu\n", sent);
		std::printf("total refused %llu\n", refused);
		std::printf("total delivered %llu duplicates %llu malformed %llu disconnects %llu\n", delivered, duplicates, a_malformed, disconnects);
		std::printf("delivery %.3f%% of nominal\n", deliveryPercent);
		const std::uint64_t datagrams = a_after.clientToTarget.datagrams - a_before.clientToTarget.datagrams;
		const std::uint64_t bytes = a_after.clientToTarget.bytes - a_before.clientToTarget.bytes;
		const std::uint64_t serverBytes = a_after.targetToClient.bytes - a_before.targetToClient.bytes;
		const double messages = static_cast<double>(sent);
		const double meanPayload = Ratio(static_cast<double>(payloadBytes), messages);
		const double meanUdp = Ratio(static_cast<double>(bytes), static_cast<double>(datagrams));
		std::printf("client->server datagrams %llu udp payload bytes %llu in %.2f s window\n", datagrams, bytes, a_seconds);
		std::printf("datagrams per message %.4f\n", Ratio(static_cast<double>(datagrams), messages));
		std::printf("mean application payload bytes %.2f\n", meanPayload);
		std::printf("mean udp payload bytes per datagram %.2f\n", meanUdp);
		std::printf("on-wire bytes per datagram (udp payload + 28 B UDP/IP header) %.2f\n", meanUdp + kUdpIpv4HeaderBytes);
		std::printf("gns framing overhead per datagram (udp payload per datagram - application payload; plan section 5 term, UDP/IP excluded) %.2f\n", meanUdp - meanPayload);
		std::printf("udp payload bytes per message minus mean application payload %.2f\n",
			Ratio(static_cast<double>(bytes), messages) - meanPayload);
		std::printf("server->client bytes per second %.1f (%llu bytes in window)\n", Ratio(static_cast<double>(serverBytes), a_seconds), serverBytes);
		const bool pass = nominal > 0 && deliveryPercent >= kDeliveryLimitPercent;
		VoiceLab::PrintCriterion("delivery >= 99.5%", pass);
		return pass ? 0 : 1;
	}

	int RunSession(const Bridge& a_bridge, const GnsOptions& a_options)
	{
		const std::uint16_t requestedServerPort = FreeLoopbackPort();
		if (requestedServerPort == 0)
		{
			std::fprintf(stderr, "voice-lab gns: cannot find a free loopback UDP port\n");
			return 1;
		}

		std::optional<VoiceLab::Relay> relay;
		Harness harness(a_bridge, a_options.seed);
		std::array<char, kErrorBufferBytes> error{};
		Handle server = nullptr;
		const int created = a_bridge.serverCreate(kLoopbackHost, requestedServerPort, &server, error.data(), error.size());
		harness.server = std::unique_ptr<void, DestroyFn>(server, a_bridge.serverDestroy);
		if (created != 1)
		{
			std::fprintf(stderr, "voice-lab gns: server create failed: %s\n", error.data());
			return 1;
		}
		const std::uint16_t serverPort = a_bridge.serverLocalPort(server);

		relay.emplace(VoiceLab::RelayConfig{ 0, serverPort, VoiceLab::Impairment{}, VoiceLab::Impairment{}, a_options.seed });
		if (!relay->Start())
		{
			std::fprintf(stderr, "voice-lab gns: cannot start the relay\n");
			return 1;
		}
		const std::uint16_t relayPort = relay->ListenPort();

		const auto interval = std::chrono::duration_cast<Clock::duration>(std::chrono::seconds(1)) / a_options.rate;
		for (int i = 0; i < a_options.senders; ++i)
		{
			Handle client = nullptr;
			const int clientCreated = a_bridge.clientCreate(kLoopbackHost, relayPort, &client, error.data(), error.size());
			if (clientCreated != 1)
			{
				std::fprintf(stderr, "voice-lab gns: client %d create failed: %s\n", i, error.data());
				return 1;
			}
			Sender sender;
			sender.client = std::unique_ptr<void, DestroyFn>(client, a_bridge.clientDestroy);
			sender.index = static_cast<std::uint32_t>(i);
			sender.interval = interval;
			harness.senders.push_back(std::move(sender));
		}

		const auto connectDeadline = Clock::now() + std::chrono::seconds(kConnectTimeoutSeconds);
		while (!AllConnected(harness) && !harness.bridgeFailed)
		{
			if (Clock::now() >= connectDeadline)
			{
				std::fprintf(stderr, "voice-lab gns: not all %d clients connected to the server within %d s\n", a_options.senders, kConnectTimeoutSeconds);
				return 1;
			}
			Pump(harness);
		}
		if (harness.bridgeFailed)
		{
			return 1;
		}

		const VoiceLab::RelayCounters relayBefore = relay->Counters();
		const auto start = Clock::now();
		const auto end = start + std::chrono::seconds(a_options.seconds);
		for (Sender& sender : harness.senders)
		{
			sender.nextDue = start + sender.interval * sender.index / a_options.senders;
		}
		while (Clock::now() < end && !harness.bridgeFailed)
		{
			const auto now = Clock::now();
			for (Sender& sender : harness.senders)
			{
				SendDue(harness, sender, now);
			}
			Pump(harness);
		}
		for (Sender& sender : harness.senders)
		{
			SendDue(harness, sender, end - Clock::duration(1));
		}
		const auto drainEnd = Clock::now() + std::chrono::milliseconds(kDrainMs);
		while (Clock::now() < drainEnd)
		{
			Pump(harness);
		}
		const VoiceLab::RelayCounters relayAfter = relay->Counters();
		const int result = harness.bridgeFailed ? 1 : Report(harness.senders, relayBefore, relayAfter, std::chrono::duration<double>(Clock::now() - start).count(), harness.malformed);

		for (Sender& sender : harness.senders)
		{
			a_bridge.clientDisconnect(sender.client.get(), kDisconnectReason, "done");
		}
		harness.senders.clear();
		harness.server.reset();
		relay->Stop();
		return result;
	}
}

bool VoiceLab::LoadBridge(const std::string& a_path, Bridge& a_bridge)
{
	const HMODULE module = LoadLibraryW(std::filesystem::path(a_path).wstring().c_str());
	if (module == nullptr)
	{
		std::fprintf(stderr, "voice-lab gns: LoadLibraryW failed for '%s' (GetLastError %lu)\n", a_path.c_str(), GetLastError());
		return false;
	}
	return Resolve(module, "co_gns_server_create", a_bridge.serverCreate) &&
		Resolve(module, "co_gns_server_destroy", a_bridge.serverDestroy) &&
		Resolve(module, "co_gns_server_local_port", a_bridge.serverLocalPort) &&
		Resolve(module, "co_gns_server_connection_count", a_bridge.serverConnectionCount) &&
		Resolve(module, "co_gns_server_poll", a_bridge.serverPoll) &&
		Resolve(module, "co_gns_client_create", a_bridge.clientCreate) &&
		Resolve(module, "co_gns_client_destroy", a_bridge.clientDestroy) &&
		Resolve(module, "co_gns_client_poll", a_bridge.clientPoll) &&
		Resolve(module, "co_gns_client_send", a_bridge.clientSend) &&
		Resolve(module, "co_gns_client_disconnect", a_bridge.clientDisconnect);
}

int RunGns(int a_argc, char** a_argv)
{
	GnsOptions options;
	if (!ParseGns(a_argc, a_argv, options))
	{
		std::fprintf(stderr, "voice-lab gns: bad arguments (usage: run voice-lab with no arguments)\n");
		return 2;
	}
	VoiceLab::Bridge bridge;
	if (!VoiceLab::LoadBridge(options.bridgePath, bridge))
	{
		return 2;
	}
	if (!VoiceLab::StartNetworking())
	{
		return 1;
	}
	const int result = RunSession(bridge, options);
	VoiceLab::StopNetworking();
	return result;
}
