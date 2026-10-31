#pragma once

#include "F4TVoiceControl.h"
#include "F4TVoiceWire.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace F4T::GnsWinsockShimState
{
	struct SocketState;
}

namespace F4T::GnsVoice
{
	using VoiceSink = std::function<void(const F4T::Voice::VoicePacket&)>;
	using ControlEventHandler = std::function<void(const nlohmann::json&)>;

	struct Counters
	{
		std::uint64_t received{};
		std::uint64_t sunk{};
		std::uint64_t dropped{};
		std::uint64_t sent{};
		std::uint64_t sendFailures{};
	};

	struct ControlView
	{
		bool serverNoVoice{};
		bool canSend{};
		bool canReceive{};
		bool pending{};
	};

	void RegisterVoiceSink(VoiceSink a_sink);
	void RegisterControlEventHandler(ControlEventHandler a_handler);
	void SetDesiredControl(F4T::Voice::ControlState a_state);
	ControlView GetControlView();
	bool TryHandleInbound(
		const std::shared_ptr<F4T::GnsWinsockShimState::SocketState>& a_state,
		std::span<const std::uint8_t> a_datagram);
	bool SendVoicePacket(std::span<const std::byte> a_datagram);
	void PumpControl(double a_now);
	void NoteWelcome(const nlohmann::json& a_packet);
	void OnStateAck(const nlohmann::json& a_packet);
	void OnTalk(const nlohmann::json& a_packet);
	void OnNotice(const nlohmann::json& a_packet);
	bool TryHandleControlPacket(std::string_view a_type, const nlohmann::json& a_packet);
	Counters GetCounters();
}
