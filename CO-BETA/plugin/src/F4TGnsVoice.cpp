#include "pch.h"

#include "F4TGnsVoice.h"

#include "F4TDebugLogging.h"
#include "F4TGnsProtocolAdapter.h"
#include "F4TGnsWinsockShimInternal.h"
#include "F4TGnsWinsockShimState.h"
#include "F4TNetworkingInternal.h"
#include "F4TVoiceControl.h"
#include "F4TVoiceUiProtocol.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace F4T::GnsVoice
{
	namespace
	{
		using SocketState = F4T::GnsWinsockShimState::SocketState;
		using Json = nlohmann::json;

		std::mutex g_socketMutex;
		std::weak_ptr<SocketState> g_socket;

		std::mutex g_sinkMutex;
		VoiceSink g_sink;

		std::mutex g_controlMutex;
		F4T::Voice::ControlSync g_control;

		std::mutex g_handlerMutex;
		ControlEventHandler g_controlEventHandler;

		std::atomic<std::uint64_t> g_received{};
		std::atomic<std::uint64_t> g_sunk{};
		std::atomic<std::uint64_t> g_dropped{};
		std::atomic<std::uint64_t> g_sent{};
		std::atomic<std::uint64_t> g_sendFailures{};

		std::shared_ptr<SocketState> BoundSocket()
		{
			const std::scoped_lock lock(g_socketMutex);
			return g_socket.lock();
		}

		ControlEventHandler CurrentControlEventHandler()
		{
			const std::scoped_lock lock(g_handlerMutex);
			return g_controlEventHandler;
		}
	}

	void RegisterVoiceSink(VoiceSink a_sink)
	{
		const std::scoped_lock lock(g_sinkMutex);
		g_sink = std::move(a_sink);
	}

	void RegisterControlEventHandler(ControlEventHandler a_handler)
	{
		const std::scoped_lock lock(g_handlerMutex);
		g_controlEventHandler = std::move(a_handler);
	}

	void SetDesiredControl(F4T::Voice::ControlState a_state)
	{
		const std::scoped_lock lock(g_controlMutex);
		g_control.SetDesired(std::move(a_state));
	}

	ControlView GetControlView()
	{
		const std::scoped_lock lock(g_controlMutex);
		return ControlView{
			.serverNoVoice = g_control.ServerNoVoice(),
			.canSend = g_control.CanSendVoice(),
			.canReceive = g_control.CanReceiveVoice(),
			.pending = g_control.Pending()
		};
	}

	bool TryHandleInbound(
		const std::shared_ptr<SocketState>& a_state,
		std::span<const std::uint8_t> a_datagram)
	{
		{
			const std::scoped_lock controlLock(g_controlMutex);
			const std::scoped_lock socketLock(g_socketMutex);
			if (g_socket.lock() != a_state) {
				g_socket = a_state;
				g_control.Reset();
			}
		}

		const auto bytes = std::as_bytes(a_datagram);
		if (!F4T::Voice::HasVoicePrefix(bytes)) {
			return false;
		}

		++g_received;
		bool receivable = false;
		{
			const std::scoped_lock lock(g_controlMutex);
			receivable = g_control.CanReceiveVoice();
		}
		if (!receivable) {
			++g_dropped;
			return true;
		}

		F4T::Voice::VoicePacket packet{};
		if (F4T::Voice::DecodeVoicePacket(bytes, F4T::Voice::VoiceDirection::ServerToClient, packet) != F4T::Voice::VoiceDecodeResult::Ok) {
			++g_dropped;
			return true;
		}

		VoiceSink sink;
		{
			const std::scoped_lock lock(g_sinkMutex);
			sink = g_sink;
		}
		if (!sink) {
			++g_dropped;
			return true;
		}
		sink(packet);
		++g_sunk;
		return true;
	}

	bool SendVoicePacket(std::span<const std::byte> a_datagram)
	{
		const std::scoped_lock controlLock(g_controlMutex);
		if (!g_control.CanSendVoice()) {
			++g_sendFailures;
			return false;
		}
		const auto state = BoundSocket();
		std::string bridgeError;
		auto* api = F4T::GnsWinsockShim::GetBridgeApi(bridgeError);
		if (!state || !api) {
			++g_sendFailures;
			return false;
		}

		const std::scoped_lock bridgeLock(state->bridgeMutex);
		if (!state->client || !state->connected.load(std::memory_order_acquire) || state->closed.load(std::memory_order_acquire)) {
			++g_sendFailures;
			return false;
		}
		const auto result = api->send(
			state->client,
			a_datagram.data(),
			static_cast<std::uint32_t>(a_datagram.size()),
			static_cast<std::uint32_t>(F4T::GnsProtocolAdapter::Delivery::UnreliableSequenced));
		if (result != F4T::GnsWinsockShimState::kSendSent) {
			++g_sendFailures;
			return false;
		}
		++g_sent;
		return true;
	}

	void PumpControl(double a_now)
	{
		const auto state = BoundSocket();
		if (!state) {
			return;
		}
		bool protocolReady = false;
		{
			const std::scoped_lock protocolLock(state->protocolMutex);
			protocolReady = state->protocol.IsReady();
		}
		if (!protocolReady) {
			return;
		}
		std::optional<std::string> snapshot;
		{
			const std::scoped_lock lock(g_controlMutex);
			snapshot = g_control.Poll(a_now);
		}
		if (snapshot) {
			F4T::Networking::Detail::SendPacketBytes(*snapshot);
		}
	}

	void NoteWelcome(const Json& a_packet)
	{
		bool serverHasVoice = false;
		if (const auto capabilities = a_packet.find("capabilities"); capabilities != a_packet.end() && capabilities->is_array()) {
			serverHasVoice = std::find(capabilities->begin(), capabilities->end(), Json("voice-v1")) != capabilities->end();
		}
		const std::scoped_lock lock(g_controlMutex);
		g_control.Reset();
		g_control.Negotiate(serverHasVoice);
	}

	void OnStateAck(const Json& a_packet)
	{
		const auto revision = a_packet.find("revision");
		if (revision == a_packet.end() || !revision->is_number_unsigned() ||
			revision->get<std::uint64_t>() > (std::numeric_limits<std::uint32_t>::max)()) {
			return;
		}
		const std::scoped_lock lock(g_controlMutex);
		g_control.OnAck(revision->get<std::uint32_t>());
	}

	void OnTalk(const Json& a_packet)
	{
		if (const auto handler = CurrentControlEventHandler()) {
			handler(a_packet);
		}
	}

	void OnNotice(const Json& a_packet)
	{
		if (!F4T::Voice::WithinNestingLimit(a_packet)) {
			return;
		}
		F4T::DebugLogging::Log("Voice", "notice received");
		if (const auto handler = CurrentControlEventHandler()) {
			handler(a_packet);
		}
	}

	bool TryHandleControlPacket(std::string_view a_type, const Json& a_packet)
	{
		if (a_type == "voiceStateAck") {
			OnStateAck(a_packet);
			return true;
		}
		if (a_type == "voiceTalk") {
			OnTalk(a_packet);
			return true;
		}
		if (a_type == "voiceNotice") {
			OnNotice(a_packet);
			return true;
		}
		return false;
	}

	Counters GetCounters()
	{
		return Counters{
			.received = g_received.load(),
			.sunk = g_sunk.load(),
			.dropped = g_dropped.load(),
			.sent = g_sent.load(),
			.sendFailures = g_sendFailures.load()
		};
	}
}
