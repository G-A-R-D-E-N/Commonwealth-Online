#pragma once

#include "F4TCompatibilityShim.h"
#include "F4TGnsProtocolAdapter.h"
#include "F4TGnsWinsockShim.h"
#include "F4TReliableOutbox.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::GnsWinsockShimState
{
	enum GnsEventType : std::uint32_t
	{
		kEventNone = 0,
		kEventConnected = 1,
		kEventDisconnected = 2,
		kEventMessage = 3,
		kEventOversizeMessage = 4
	};

	enum GnsSendResult : int
	{
		kSendError = -1,
		kSendSent = 0,
		kSendDropped = 1,
		kSendBackpressure = 2,
		kSendNotConnected = 3,
		kSendTooLarge = 4
	};

	struct GnsEvent
	{
		std::uint32_t type{};
		std::uint32_t connectionId{};
		std::int32_t reason{};
		std::uint32_t payloadSize{};
		char debug[128]{};
	};

	static_assert(sizeof(GnsEvent) == 144);

	using GnsClientHandle = void*;
	using ClientCreateFn = int(__cdecl*)(const char*, std::uint16_t, GnsClientHandle*, char*, std::size_t);
	using ClientDestroyFn = void(__cdecl*)(GnsClientHandle);
	using ClientPollFn = int(__cdecl*)(GnsClientHandle, GnsEvent*, void*, std::uint32_t);
	using ClientSendFn = int(__cdecl*)(GnsClientHandle, const void*, std::uint32_t, std::uint32_t);
	using ClientDisconnectFn = int(__cdecl*)(GnsClientHandle, std::int32_t, const char*);

	struct BridgeApi
	{
		HMODULE module{};
		ClientCreateFn create{};
		ClientDestroyFn destroy{};
		ClientPollFn poll{};
		ClientSendFn send{};
		ClientDisconnectFn disconnect{};
	};

	inline void WipeServerPassword(std::string& a_password) noexcept
	{
		SecureZeroMemory(a_password.data(), a_password.size());
		a_password.clear();
	}

	struct SocketState
	{
		GnsClientHandle client{};
		std::atomic_bool connected{ false };
		std::atomic_bool closed{ false };
		std::atomic_bool cancelRequested{ false };
		std::atomic<std::uint32_t> outgoingTransformSnapshotSequence{ 0 };
		std::atomic<std::uint32_t> outgoingNpcSnapshotSequence{ 0 };
		std::atomic<std::uint32_t> incomingTransformSnapshotSequence{ 0 };
		std::atomic<std::uint32_t> incomingNpcSnapshotSequence{ 0 };
		std::mutex bridgeMutex;
		F4T::ReliableOutbox::Outbox reliableOutbox;
		std::mutex protocolMutex;
		F4T::GnsProtocolAdapter::Session protocol;
		std::string serverKey;
		std::string host;
		std::string serverPassword;
		std::uint16_t port{};
		F4T::Compatibility::CompatibilityRegistry compatibilityRegistry;
		bool componentReceiveInstalled{ false };
		F4T::Replication::FrozenComponentDefinitionTable componentDefinitions;
		F4T::Compatibility::SessionCapabilities componentCapabilities;
		std::mutex receiveMutex;
		std::deque<std::string> pendingReceive;
		std::size_t pendingReceiveOffset{};
		std::mutex failureMutex;
		std::optional<F4T::GnsWinsockShim::ConnectFailure> connectFailure;
		~SocketState()
		{
			WipeServerPassword(serverPassword);
		}
	};

	inline thread_local int g_lastError = 0;

	inline void SetError(int a_error) noexcept
	{
		g_lastError = a_error;
	}

	inline void ClearError() noexcept
	{
		g_lastError = 0;
	}

	void SetSendFailure(SocketState& a_state, int a_result);
	F4T::ReliableOutbox::Attempt AttemptReliableSend(BridgeApi& a_api, SocketState& a_state, std::string_view a_payload);
	bool DrainReliableOutboxLocked(BridgeApi& a_api, SocketState& a_state);
}
