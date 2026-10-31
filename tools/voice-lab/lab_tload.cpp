#include "lab_tload.h"

#include "lab_gns.h"
#include "lab_relay.h"
#include "lab_tload_state.h"
#include "lab_tload_wire.h"

#include "F4TConnectionPolicy.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <thread>

namespace Tload = VoiceLab::Tload;

namespace
{
	using Clock = Tload::Clock;
	using Seconds = Tload::Seconds;
	using Harness = Tload::Harness;
	using Options = Tload::Options;
	using Player = Tload::Player;
	using CpuMark = Tload::CpuMark;
	using VoiceLab::Bridge;
	using VoiceLab::DestroyFn;
	using VoiceLab::Handle;
	using VoiceLab::LoadBridge;
	using Tload::kChatIntervalSeconds;
	using Tload::kTransformIntervalSeconds;
	using Tload::kVoiceIntervalSeconds;

	constexpr int kMinClients = 2;
	constexpr int kMinSeconds = 10;
	constexpr int kMaximumSeconds = 100000;
	constexpr std::uint64_t kMaximumPort = 65535U;
	constexpr std::uint16_t kRelayPortOffset = 100;
	constexpr double kSettleSeconds = 1.0;
	constexpr double kAckSettleSeconds = 0.05;
	constexpr double kHandshakeTimeoutSeconds = 10.0;
	constexpr double kConnectPacingMarginSeconds = 0.5;
	constexpr std::size_t kConnectAttemptLimit = F4T::ConnectionPolicy::DefaultMaximumConnectAttempts;
	constexpr double kConnectAttemptWindowSeconds = F4T::ConnectionPolicy::DefaultConnectAttemptWindowSeconds;
	constexpr double kTransformStepUnits = 0.25;
	constexpr double kRingRadiusUnits = 10.0;
	constexpr double kTwoPi = 2.0 * std::numbers::pi;
	constexpr std::uint32_t kDeliveryUnreliableSequenced = 0U;
	constexpr std::uint32_t kDeliveryReliableOrdered = 1U;
	constexpr int kSendSent = 0;
	constexpr std::int32_t kDisconnectReason = 1000;
	constexpr std::size_t kErrorBufferBytes = 256;
	constexpr int kLoopSleepMs = 1;
	constexpr char kLoopbackHost[] = "127.0.0.1";

	bool ParseNumber(const char* a_text, std::uint64_t& a_value)
	{
		char* end = nullptr;
		const auto parsed = std::strtoull(a_text, &end, 10);
		if (end == a_text || *end != '\0') {
			return false;
		}
		a_value = parsed;
		return true;
	}

	bool ParseTload(int a_argc, char** a_argv, Options& a_options)
	{
		if (a_argc < 4 || (a_argc - 2) % 2 != 0) {
			return false;
		}
		for (int i = 2; i < a_argc; i += 2) {
			const std::string flag = a_argv[i];
			const char* value = a_argv[i + 1];
			std::uint64_t number = 0;
			if (flag == "--bridge") {
				a_options.bridgePath = value;
			} else if (flag == "--voice") {
				a_options.voice = value;
			} else if (flag == "--admin-token-file") {
				a_options.adminTokenFile = value;
			} else if (flag == "--baseline") {
				a_options.baselinePath = value;
			} else if (!ParseNumber(value, number)) {
				return false;
			} else if (flag == "--port") {
				if (number == 0 || number > kMaximumPort) {
					return false;
				}
				a_options.port = static_cast<std::uint16_t>(number);
			} else if (flag == "--admin-port") {
				if (number == 0 || number > kMaximumPort) {
					return false;
				}
				a_options.adminPort = static_cast<std::uint16_t>(number);
			} else if (flag == "--server-pid") {
				a_options.serverPid = static_cast<std::uint32_t>(number);
			} else if (flag == "--relay-cap-bps") {
				a_options.relayCapBps = number;
			} else if (flag == "--clients") {
				a_options.clients = static_cast<int>(std::min<std::uint64_t>(number, Tload::kMaxClients + 1U));
			} else if (flag == "--seconds") {
				a_options.seconds = static_cast<int>(std::min<std::uint64_t>(number, kMaximumSeconds));
			} else if (flag == "--seed") {
				a_options.seed = static_cast<std::uint32_t>(number);
			} else {
				return false;
			}
		}
		return !a_options.bridgePath.empty() && (a_options.voice == "on" || a_options.voice == "off") && a_options.port != 0 &&
			a_options.serverPid != 0 && a_options.clients >= kMinClients && a_options.clients <= Tload::kMaxClients &&
			a_options.seconds >= kMinSeconds &&
			(!a_options.relayCapBps || (*a_options.relayCapBps > 0 && a_options.port <= kMaximumPort - kRelayPortOffset)) &&
			(a_options.adminPort == 0) == a_options.adminTokenFile.empty() &&
			(a_options.baselinePath.empty() || a_options.voice == "on");
	}

	Clock::time_point Advance(Clock::time_point a_previous, Clock::time_point a_now, double a_intervalSeconds)
	{
		const auto next = a_previous + Tload::ToClock(a_intervalSeconds);
		return next > a_now ? next : a_now + Tload::ToClock(a_intervalSeconds);
	}

	bool SendReliable(Harness& a_h, Player& a_p, const std::string& a_text)
	{
		return a_h.bridge.clientSend(a_p.client.get(), a_text.data(), static_cast<std::uint32_t>(a_text.size()), kDeliveryReliableOrdered) == kSendSent;
	}

	void Pump(Harness& a_h, double a_seconds)
	{
		const auto end = Clock::now() + Tload::ToClock(a_seconds);
		while (Clock::now() < end) {
			Tload::PollAll(a_h);
			std::this_thread::sleep_for(std::chrono::milliseconds(kLoopSleepMs));
		}
	}

	template <typename Done>
	bool Await(Harness& a_h, Done a_done, double a_seconds)
	{
		const auto deadline = Clock::now() + Tload::ToClock(a_seconds);
		while (!a_done()) {
			if (Clock::now() >= deadline) {
				return false;
			}
			Tload::PollAll(a_h);
			std::this_thread::sleep_for(std::chrono::milliseconds(kLoopSleepMs));
		}
		return true;
	}

	bool Handshake(Harness& a_h, Player& a_p, bool a_voice)
	{
		if (!Await(a_h, [&a_p] { return a_p.hasPlayerId || a_p.failed; }, kHandshakeTimeoutSeconds) || !a_p.hasPlayerId) {
			return false;
		}
		if (!SendReliable(a_h, a_p, Tload::HelloJson(a_p.profileId, a_voice))) {
			return false;
		}
		if (!Await(a_h, [&a_p] { return a_p.sessionReady || a_p.failed; }, kHandshakeTimeoutSeconds) || !a_p.sessionReady) {
			return false;
		}
		if (!SendReliable(a_h, a_p, Tload::KeepAliveJson(a_p.resumeToken))) {
			return false;
		}
		if (a_voice && !SendReliable(a_h, a_p, Tload::VoiceStateJson())) {
			return false;
		}
		Pump(a_h, kAckSettleSeconds);
		return !a_p.failed;
	}

	void AwaitConnectSlot(Harness& a_h, std::size_t a_index)
	{
		if (a_index < kConnectAttemptLimit) {
			return;
		}
		const auto earliest = a_h.connectTimes[a_index - kConnectAttemptLimit] + Tload::ToClock(kConnectAttemptWindowSeconds + kConnectPacingMarginSeconds);
		const double wait = Seconds(earliest - Clock::now()).count();
		if (wait > 0.0) {
			Pump(a_h, wait);
		}
	}

	bool Connect(Harness& a_h, std::size_t a_index, std::uint16_t a_targetPort, bool a_voice)
	{
		AwaitConnectSlot(a_h, a_index);
		a_h.connectTimes.push_back(Clock::now());
		a_h.players.emplace_back();
		Player& player = a_h.players.back();
		player.index = a_index;
		std::array<char, 32> profile{};
		std::snprintf(profile.data(), profile.size(), "tload-%02zu", a_index);
		player.profileId = profile.data();
		player.lastSeen.assign(static_cast<std::size_t>(a_h.options.clients), 0.0);
		std::array<char, kErrorBufferBytes> error{};
		Handle client = nullptr;
		if (a_h.bridge.clientCreate(kLoopbackHost, a_targetPort, &client, error.data(), error.size()) != 1) {
			std::fprintf(stderr, "voice-lab tload: client %zu create failed: %s\n", a_index, error.data());
			return false;
		}
		player.client = std::unique_ptr<void, DestroyFn>(client, a_h.bridge.clientDestroy);
		if (!Handshake(a_h, player, a_voice)) {
			std::fprintf(stderr, "voice-lab tload: handshake failed for %s\n", player.profileId.c_str());
			return false;
		}
		return true;
	}

	void SendTransform(Harness& a_h, Player& a_p)
	{
		const double stamp = a_h.StampOf(Clock::now());
		const double x = a_p.x + kTransformStepUnits * static_cast<double>(a_p.transformSequence & 1U);
		const auto bytes = Tload::TransformEnvelope(Tload::TransformJson(x, a_p.y, stamp), a_p.transformSequence);
		const bool sent = a_h.bridge.clientSend(a_p.client.get(), bytes.data(), static_cast<std::uint32_t>(bytes.size()), kDeliveryUnreliableSequenced) == kSendSent;
		if (a_h.InWindow(stamp)) {
			if (sent) {
				++a_h.tally.transformsSent;
			} else {
				++a_h.tally.transformRefused;
			}
		}
	}

	void SendVoice(Harness& a_h, Player& a_p)
	{
		const double stamp = a_h.StampOf(Clock::now());
		const auto datagram = Tload::VoiceDatagram(a_p.voiceSequence, !a_p.voiceStarted);
		a_p.voiceStarted = true;
		if (datagram.empty()) {
			++a_h.tally.encodeFailures;
			return;
		}
		const bool sent = a_h.bridge.clientSend(a_p.client.get(), datagram.data(), static_cast<std::uint32_t>(datagram.size()), kDeliveryUnreliableSequenced) == kSendSent;
		if (a_h.InWindow(stamp)) {
			if (sent) {
				++a_h.tally.voiceSent;
			} else {
				++a_h.tally.voiceRefused;
			}
		}
	}

	void SendChat(Harness& a_h, Player& a_p)
	{
		const double stamp = a_h.StampOf(Clock::now());
		std::array<char, 64> text{};
		std::snprintf(text.data(), text.size(), "tload %02zu %zu", a_p.index, a_p.chatMessages.size() + 1);
		a_p.chatMessages.push_back({ stamp });
		const bool sent = SendReliable(a_h, a_p, Tload::ChatJson(text.data()));
		if (a_h.InWindow(stamp)) {
			if (sent) {
				++a_h.tally.chatSent;
			} else {
				++a_h.tally.chatRefused;
			}
		}
	}

	void SendDue(Harness& a_h, Player& a_p, Clock::time_point a_now, bool a_voice)
	{
		if (a_p.nextTransform <= a_now) {
			SendTransform(a_h, a_p);
			a_p.nextTransform = Advance(a_p.nextTransform, a_now, kTransformIntervalSeconds);
		}
		if (a_voice && a_p.nextVoice <= a_now) {
			SendVoice(a_h, a_p);
			a_p.nextVoice = Advance(a_p.nextVoice, a_now, kVoiceIntervalSeconds);
		}
		if (a_p.nextChat <= a_now) {
			SendChat(a_h, a_p);
			a_p.nextChat = a_now + Tload::ToClock(kChatIntervalSeconds);
		}
	}

	void PlaceAll(Harness& a_h)
	{
		const auto count = static_cast<double>(a_h.players.size());
		for (Player& player : a_h.players) {
			const double angle = kTwoPi * static_cast<double>(player.index) / count;
			player.x = kRingRadiusUnits * std::cos(angle);
			player.y = kRingRadiusUnits * std::sin(angle);
			SendTransform(a_h, player);
		}
		Pump(a_h, kSettleSeconds);
	}

	std::optional<Tload::VoiceCounters> ReadCounters(const Options& a_options)
	{
		Tload::VoiceCounters counters{};
		if (a_options.adminPort == 0 || !Tload::ReadVoiceCounters(a_options.adminPort, a_options.adminTokenFile, counters)) {
			return std::nullopt;
		}
		return counters;
	}

	int Measure(Harness& a_h, HANDLE a_process, const VoiceLab::Relay* a_relay, bool a_voice)
	{
		const auto t0 = Clock::now();
		const double unixT0 = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
		const auto tStart = t0 + Tload::ToClock(Tload::kWarmupSeconds);
		const auto tEnd = tStart + Tload::ToClock(a_h.options.seconds);
		const auto pollEnd = tEnd + Tload::ToClock(Tload::kDrainSeconds);
		a_h.windowStart = a_h.StampOf(tStart);
		a_h.windowEnd = a_h.StampOf(tEnd);
		a_h.unixWindowStart = unixT0 + Seconds(tStart - t0).count();
		a_h.unixWindowEnd = unixT0 + Seconds(tEnd - t0).count();
		a_h.tally.voiceBins.assign(static_cast<std::size_t>(a_h.options.seconds), 0U);
		const auto count = static_cast<double>(a_h.players.size());
		for (Player& player : a_h.players) {
			const double share = static_cast<double>(player.index) / count;
			player.nextTransform = t0 + Tload::ToClock(share * kTransformIntervalSeconds);
			player.nextVoice = t0 + Tload::ToClock(share * kVoiceIntervalSeconds);
			player.nextChat = t0 + Tload::ToClock(share * kChatIntervalSeconds);
		}
		CpuMark start{};
		CpuMark end{};
		bool haveStart = false;
		bool haveEnd = false;
		auto previous = t0;
		while (Clock::now() < pollEnd) {
			const auto now = Clock::now();
			if (now >= tStart && now < pollEnd) {
				a_h.tally.loopPeriods.push_back(Seconds(now - previous).count());
			}
			previous = now;
			if (!haveStart && now >= tStart) {
				start = Tload::TakeMark(a_process);
				a_h.adminStart = ReadCounters(a_h.options);
				haveStart = true;
			}
			if (!haveEnd && now >= tEnd) {
				end = Tload::TakeMark(a_process);
				a_h.adminEnd = ReadCounters(a_h.options);
				haveEnd = true;
			}
			if (now < tEnd) {
				for (Player& player : a_h.players) {
					SendDue(a_h, player, now, a_voice);
				}
			}
			Tload::PollAll(a_h);
			std::this_thread::sleep_for(std::chrono::milliseconds(kLoopSleepMs));
		}
		Tload::PrintReport(a_h, start, end, a_relay);
		const auto& t = a_h.tally;
		return t.disconnects == 0 && t.sessionEnded == 0 && t.pollErrors == 0 && t.encodeFailures == 0 ? 0 : 1;
	}

	int RunSession(const Bridge& a_bridge, const Options& a_options)
	{
		const bool voice = a_options.voice == "on";
		const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, a_options.serverPid);
		if (process == nullptr) {
			std::fprintf(stderr, "voice-lab tload: cannot open server pid %u (GetLastError %lu)\n", a_options.serverPid, GetLastError());
			return 1;
		}
		Harness harness(a_bridge, a_options);
		harness.players.reserve(static_cast<std::size_t>(a_options.clients));
		std::optional<VoiceLab::Relay> relay;
		std::uint16_t targetPort = a_options.port;
		if (a_options.relayCapBps) {
			VoiceLab::Impairment cap{};
			cap.capBytesPerSecond = static_cast<double>(*a_options.relayCapBps);
			relay.emplace(VoiceLab::RelayConfig{ static_cast<std::uint16_t>(a_options.port + kRelayPortOffset), a_options.port, VoiceLab::Impairment{}, cap, a_options.seed });
			if (!relay->Start()) {
				std::fprintf(stderr, "voice-lab tload: cannot start the relay on port %u\n", a_options.port + kRelayPortOffset);
				CloseHandle(process);
				return 1;
			}
			targetPort = relay->ListenPort();
		}
		bool connected = true;
		for (std::size_t i = 0; connected && i < static_cast<std::size_t>(a_options.clients); ++i) {
			connected = Connect(harness, i, targetPort, voice);
		}
		int result = 1;
		if (connected) {
			PlaceAll(harness);
			result = Measure(harness, process, relay ? &*relay : nullptr, voice);
		}
		for (Player& player : harness.players) {
			if (player.client) {
				a_bridge.clientDisconnect(player.client.get(), kDisconnectReason, "tload done");
			}
		}
		harness.players.clear();
		relay.reset();
		CloseHandle(process);
		return result;
	}
}

int RunTload(int a_argc, char** a_argv)
{
	Options options;
	if (!ParseTload(a_argc, a_argv, options)) {
		std::fprintf(stderr, "voice-lab tload: bad arguments (see usage)\n");
		return 2;
	}
	Bridge bridge;
	if (!LoadBridge(options.bridgePath, bridge)) {
		return 2;
	}
	if (!VoiceLab::StartNetworking()) {
		return 1;
	}
	const int result = RunSession(bridge, options);
	VoiceLab::StopNetworking();
	return result;
}
