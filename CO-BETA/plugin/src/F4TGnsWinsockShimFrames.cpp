#include "pch.h"

#include "F4TGnsWinsockShim.h"
%:include "F4TCompatibilityShim.h"
#include "F4TGnsComponentReceive.h"
#include "F4TGnsProtocolAdapter.h"
#include "F4TGnsSnapshotEnvelope.h"
#include "F4TGnsWinsockShimState.h"
#include "F4TGnsManifestFetch.h"
#include "F4TGnsWinsockShimInternal.h"
#include "F4TClientCompatibility.h"
#include "F4TClientIdentity.h"
#include "F4TClientModpackProbe.h"
#include "F4TReliableOutbox.h"
#include "F4TServerProfileStore.h"
#include "F4TWorldPolicyEnforcer.h"

#include <Windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	using namespace F4T::GnsWinsockShimState;
	using SnapshotFamily = F4T::GnsSnapshotEnvelope::Family;

	std::string SanitizeFailureText(std::string a_text, std::size_t a_limit = 256)
	{
		if (a_text.size() > a_limit) {
			a_text.resize(a_limit);
		}
		for (auto& ch : a_text) {
			const auto value = static_cast<unsigned char>(ch);
			if (value < 0x20 || value == 0x7F) {
				ch = ' ';
			}
		}
		return a_text;
	}

	std::optional<SnapshotFamily> SnapshotFamilyForPayload(std::string_view a_payload)
	{
		const auto packet = nlohmann::json::parse(a_payload, nullptr, false);
		if (packet.is_discarded() || !packet.is_object()) {
			return std::nullopt;
		}
		const auto type = packet.find("type");
		if (type == packet.end() || !type->is_string()) {
			return std::nullopt;
		}
		const auto& text = type->get_ref<const std::string&>();
		if (text == "transform") {
			return SnapshotFamily::Transform;
		}
		if (text == "npcState") {
			return SnapshotFamily::NpcState;
		}
		return std::nullopt;
	}

	std::atomic<std::uint32_t>& OutgoingSnapshotSequence(SocketState& a_state, SnapshotFamily a_family) noexcept
	{
		return a_family == SnapshotFamily::Transform ?
			a_state.outgoingTransformSnapshotSequence :
			a_state.outgoingNpcSnapshotSequence;
	}

	std::uint32_t AdvanceSnapshotSequence(std::atomic<std::uint32_t>& a_sequence) noexcept
	{
		auto current = a_sequence.load(std::memory_order_relaxed);
		for (;;) {
			const auto next = F4T::GnsSnapshotEnvelope::NextSequence(current);
			if (a_sequence.compare_exchange_weak(
					current,
					next,
					std::memory_order_acq_rel,
					std::memory_order_relaxed)) {
				return next;
			}
		}
	}
}

namespace F4T::GnsWinsockShim
{
	std::atomic<std::uint32_t>& IncomingSnapshotSequence(SocketState& a_state, SnapshotFamily a_family) noexcept
	{
		return a_family == SnapshotFamily::Transform ?
			a_state.incomingTransformSnapshotSequence :
			a_state.incomingNpcSnapshotSequence;
	}

	std::string EventDebugText(const GnsEvent& a_event)
	{
		const auto end = std::find(std::begin(a_event.debug), std::end(a_event.debug), '\0');
		return SanitizeFailureText(std::string{ std::begin(a_event.debug), end }, 128);
	}

	int ServePendingReceive(const std::shared_ptr<SocketState>& a_state, char* a_buffer, int a_length)
	{
		const std::scoped_lock lock(a_state->receiveMutex);
		if (a_state->pendingReceive.empty()) {
			a_state->pendingReceiveOffset = 0;
			return 0;
		}

		auto& current = a_state->pendingReceive.front();
		if (a_state->pendingReceiveOffset >= current.size()) {
			a_state->pendingReceive.pop_front();
			a_state->pendingReceiveOffset = 0;
			if (a_state->pendingReceive.empty()) {
				return 0;
			}
		}

		auto& message = a_state->pendingReceive.front();
		const auto remaining = message.size() - a_state->pendingReceiveOffset;
		const auto copySize = (std::min)(remaining, static_cast<std::size_t>(a_length));
		std::memcpy(a_buffer, message.data() + a_state->pendingReceiveOffset, copySize);
		a_state->pendingReceiveOffset += copySize;
		if (a_state->pendingReceiveOffset == message.size()) {
			a_state->pendingReceive.pop_front();
			a_state->pendingReceiveOffset = 0;
		}
		return static_cast<int>(copySize);
	}

	void QueueLegacyMessage(const std::shared_ptr<SocketState>& a_state, std::string a_message)
	{
		if (a_message.empty()) {
			return;
		}
		if (a_message.back() != '\n') {
			a_message.push_back('\n');
		}
		const std::scoped_lock lock(a_state->receiveMutex);
		a_state->pendingReceive.push_back(std::move(a_message));
	}

	bool PrepareWirePayload(
		const std::shared_ptr<SocketState>& a_state,
		const F4T::GnsProtocolAdapter::OutboundMessage& a_message,
		std::vector<std::uint8_t>& a_storage,
		const void*& a_data,
		std::size_t& a_size)
	{
		if (a_message.payload.empty()) {
			return false;
		}

		if (a_message.delivery == F4T::GnsProtocolAdapter::Delivery::UnreliableSequenced) {
			if (a_message.payload.size() > kMaximumMessageBytes - F4T::GnsSnapshotEnvelope::HeaderSize) {
				return false;
			}
			const auto family = SnapshotFamilyForPayload(a_message.payload);
			if (!family) {
				return false;
			}
			a_storage.resize(F4T::GnsSnapshotEnvelope::HeaderSize + a_message.payload.size());
			const auto sequence = AdvanceSnapshotSequence(OutgoingSnapshotSequence(*a_state, *family));
			if (!F4T::GnsSnapshotEnvelope::WriteHeader(a_storage, *family, sequence)) {
				return false;
			}
			std::memcpy(
				a_storage.data() + F4T::GnsSnapshotEnvelope::HeaderSize,
				a_message.payload.data(),
				a_message.payload.size());
			a_data = a_storage.data();
			a_size = a_storage.size();
			return true;
		}

		if (a_message.payload.size() > kMaximumMessageBytes) {
			return false;
		}
		a_data = a_message.payload.data();
		a_size = a_message.payload.size();
		return true;
	}

	bool FlushReliableOutbox(const std::shared_ptr<SocketState>& a_state)
	{
		std::string bridgeError;
		auto* api = GetBridgeApi(bridgeError);
		if (!api) {
			SetError(WSASYSNOTREADY);
			return false;
		}

		const std::scoped_lock bridgeLock(a_state->bridgeMutex);
		if (!a_state->client || a_state->closed.load(std::memory_order_acquire)) {
			return true;
		}
		return DrainReliableOutboxLocked(*api, *a_state);
	}

	bool SendAdaptedMessages(
		const std::shared_ptr<SocketState>& a_state,
		const std::vector<F4T::GnsProtocolAdapter::OutboundMessage>& a_messages)
	{
		if (a_messages.empty()) {
			return true;
		}

		std::string bridgeError;
		auto* api = GetBridgeApi(bridgeError);
		if (!api) {
			SetError(WSASYSNOTREADY);
			return false;
		}

		const std::scoped_lock bridgeLock(a_state->bridgeMutex);
		if (!a_state->client || a_state->closed.load(std::memory_order_acquire)) {
			SetError(WSAENOTCONN);
			return false;
		}
		if (!DrainReliableOutboxLocked(*api, *a_state)) {
			return false;
		}

		for (const auto& message : a_messages) {
			std::vector<std::uint8_t> wireStorage;
			const void* sendData = nullptr;
			std::size_t sendSize = 0;
			if (!PrepareWirePayload(a_state, message, wireStorage, sendData, sendSize)) {
				SetError(WSAEMSGSIZE);
				return false;
			}

			if (message.delivery == F4T::GnsProtocolAdapter::Delivery::UnreliableSequenced) {
				const auto result = api->send(
					a_state->client,
					sendData,
					static_cast<std::uint32_t>(sendSize),
					static_cast<std::uint32_t>(message.delivery));
				if (result != kSendSent && result != kSendDropped && result != kSendBackpressure) {
					SetSendFailure(*a_state, result);
					return false;
				}
				continue;
			}

			const auto submitted = a_state->reliableOutbox.Submit(
				message.payload,
				std::chrono::steady_clock::now(),
				[&](std::string_view a_payload) { return AttemptReliableSend(*api, *a_state, a_payload); });
			if (submitted == F4T::ReliableOutbox::SubmitResult::Full) {
				SetError(WSAENOBUFS);
				return false;
			}
			if (submitted == F4T::ReliableOutbox::SubmitResult::Failed) {
				return false;
			}
		}

		ClearError();
		return true;
	}

	void DisconnectForProtocolViolation(const std::shared_ptr<SocketState>& a_state, std::string_view a_error)
	{
		REX::WARN("Commonwealth Online Protocol V2 violation: {}", a_error);
		a_state->connected.store(false, std::memory_order_release);
		a_state->closed.store(true, std::memory_order_release);
		WipeServerPassword(a_state->serverPassword);

		std::string bridgeError;
		if (auto* api = GetBridgeApi(bridgeError)) {
			const std::scoped_lock bridgeLock(a_state->bridgeMutex);
			if (a_state->client) {
				api->disconnect(a_state->client, 4000, "Protocol V2 violation");
			}
		}
	}

	void DisconnectForSessionRejection(
		const std::shared_ptr<SocketState>& a_state,
		const F4T::GnsProtocolAdapter::SessionRejection& a_rejection)
	{
		auto rejection = a_rejection;
		if (!rejection.requiredModpack && rejection.requiredModpackChunks) {
			if (const auto fetched = F4T::GnsManifestFetch::FetchManifest(
					a_state->host,
					a_state->port,
					rejection.requiredModpackFingerprint,
					a_state->serverPassword,
					*rejection.requiredModpackChunks,
					std::addressof(a_state->cancelRequested))) {
				rejection.requiredModpack = *fetched;
			} else if (!rejection.reason.empty()) {
				rejection.reason += "\nThe server modpack manifest could not be downloaded in chunks.";
			}
		}
		WipeServerPassword(a_state->serverPassword);
		RecordConnectFailure(a_state, F4T::ClientModpackProbe::MakeSessionRejectionFailure(rejection, a_state->serverKey));
		a_state->connected.store(false, std::memory_order_release);
		a_state->closed.store(true, std::memory_order_release);
		REX::WARN(
			"Commonwealth Online session rejected during handshake (code={}, reason={}).",
			a_rejection.code.empty() ? "<none>" : SanitizeFailureText(std::string{ a_rejection.code }, 64),
			a_rejection.reason.empty() ? "<none>" : SanitizeFailureText(std::string{ a_rejection.reason }));

		std::string bridgeError;
		if (auto* api = GetBridgeApi(bridgeError)) {
			const std::scoped_lock bridgeLock(a_state->bridgeMutex);
			if (a_state->client) {
				api->disconnect(a_state->client, 4000, "Session rejected");
			}
		}
	}

	void DisconnectForLocalHandshakeFailure(const std::shared_ptr<SocketState>& a_state, std::string_view a_error)
	{
		const auto detail = SanitizeFailureText(std::string{ a_error });
		RecordConnectFailure(a_state, ConnectFailure{
			.kind = ConnectFailureKind::LocalHandshakeFailure,
			.detail = detail
		});
		a_state->connected.store(false, std::memory_order_release);
		a_state->closed.store(true, std::memory_order_release);
		REX::WARN("Commonwealth Online client-local handshake failure: {}", detail);

		std::string bridgeError;
		if (auto* api = GetBridgeApi(bridgeError)) {
			const std::scoped_lock bridgeLock(a_state->bridgeMutex);
			if (a_state->client) {
				api->disconnect(a_state->client, 4000, "Client handshake failure");
			}
		}
	}
}
