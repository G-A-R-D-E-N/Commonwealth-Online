#include "F4TVoiceStatus.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <string_view>
#include <utility>

namespace
{
	using F4T::Voice::SelectVoiceStatus;
	using F4T::Voice::VoiceStatus;
	using F4T::Voice::VoiceStatusCode;
	using F4T::Voice::VoiceStatusInputs;
	using F4T::Voice::VoiceStatusText;

	constexpr std::array<std::pair<VoiceStatus, std::string_view>, 14> WireNames{ {
		{ VoiceStatus::ready, "ready" }, { VoiceStatus::transmitting, "transmitting" },
		{ VoiceStatus::idle, "idle" }, { VoiceStatus::serverNoVoice, "serverNoVoice" },
		{ VoiceStatus::micFailed, "micFailed" }, { VoiceStatus::micRecovering, "micRecovering" },
		{ VoiceStatus::micSilentHint, "micSilentHint" }, { VoiceStatus::noOutput, "noOutput" },
		{ VoiceStatus::serverMuted, "serverMuted" }, { VoiceStatus::rateLimited, "rateLimited" },
		{ VoiceStatus::profileMismatch, "profileMismatch" }, { VoiceStatus::deafened, "deafened" },
		{ VoiceStatus::micMuted, "micMuted" }, { VoiceStatus::codecFailed, "codecFailed" } } };

	void TestStatusCodesMatchWireNames()
	{
		for (std::size_t i = 0; i < WireNames.size(); ++i) {
			assert(VoiceStatusCode(WireNames[i].first) == WireNames[i].second && !WireNames[i].second.empty());
			for (std::size_t j = i + 1; j < WireNames.size(); ++j) {
				assert(WireNames[i].second != WireNames[j].second);
			}
		}
	}

	constexpr std::array<std::pair<VoiceStatus, std::string_view>, 6> UiTexts{ {
		{ VoiceStatus::serverNoVoice, "This server does not support voice" },
		{ VoiceStatus::micFailed, "Microphone not available" },
		{ VoiceStatus::micRecovering, "Reconnecting microphone" },
		{ VoiceStatus::micSilentHint, "Your microphone may be muted in Windows" },
		{ VoiceStatus::noOutput, "Voice playback failed: no audio output device" },
		{ VoiceStatus::codecFailed, "Voice encoding failed; retry voice" } } };

	void TestStatusUiTextIsExact()
	{
		for (const auto& entry : UiTexts) {
			assert(VoiceStatusText(entry.first) == entry.second);
		}
		assert(VoiceStatusText(VoiceStatus::ready).empty());
	}

	struct StatusCase { bool VoiceStatusInputs::*field; VoiceStatus status; };

	constexpr std::array<StatusCase, 13> PriorityOrder{ {
		{ &VoiceStatusInputs::profileMismatch, VoiceStatus::profileMismatch },
		{ &VoiceStatusInputs::serverNoVoice, VoiceStatus::serverNoVoice },
		{ &VoiceStatusInputs::micRecovering, VoiceStatus::micRecovering },
		{ &VoiceStatusInputs::micFailed, VoiceStatus::micFailed },
		{ &VoiceStatusInputs::codecFailed, VoiceStatus::codecFailed },
		{ &VoiceStatusInputs::serverMuted, VoiceStatus::serverMuted },
		{ &VoiceStatusInputs::rateLimited, VoiceStatus::rateLimited },
		{ &VoiceStatusInputs::noOutput, VoiceStatus::noOutput },
		{ &VoiceStatusInputs::micMuted, VoiceStatus::micMuted },
		{ &VoiceStatusInputs::deafened, VoiceStatus::deafened },
		{ &VoiceStatusInputs::micSilentHint, VoiceStatus::micSilentHint },
		{ &VoiceStatusInputs::transmitting, VoiceStatus::transmitting },
		{ &VoiceStatusInputs::idle, VoiceStatus::idle } } };

	void TestStatusPriority()
	{
		assert(SelectVoiceStatus(VoiceStatusInputs{}) == VoiceStatus::ready);

		for (std::size_t i = 0; i < PriorityOrder.size(); ++i) {
			VoiceStatusInputs only{};
			only.*(PriorityOrder[i].field) = true;
			assert(SelectVoiceStatus(only) == PriorityOrder[i].status);

			for (std::size_t j = i + 1; j < PriorityOrder.size(); ++j) {
				VoiceStatusInputs pair{};
				pair.*(PriorityOrder[i].field) = true;
				pair.*(PriorityOrder[j].field) = true;
				assert(SelectVoiceStatus(pair) == PriorityOrder[i].status);
			}
		}

		VoiceStatusInputs all{};
		for (const StatusCase& entry : PriorityOrder) {
			all.*(entry.field) = true;
		}
		assert(SelectVoiceStatus(all) == VoiceStatus::profileMismatch);
	}
}

int main()
{
	TestStatusCodesMatchWireNames();
	TestStatusUiTextIsExact();
	TestStatusPriority();
	return 0;
}
