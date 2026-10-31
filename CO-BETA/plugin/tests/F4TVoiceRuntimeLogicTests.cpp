#include "F4TVoiceRuntimeLogic.h"

#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

namespace
{
	using F4T::Voice::DepartedSpeakers;
	using F4T::Voice::DevicesWanted;
	using F4T::Voice::MergeSpeakers;
	using F4T::Voice::RemoteVoicePlayer;
	using F4T::Voice::SaveDebounce;
	using F4T::Voice::SpeakerPositionsOf;
	using F4T::Voice::TransmitConfigOf;
	using F4T::Voice::VoicePlayerRow;
	using F4T::Voice::VoiceSettings;

	VoicePlayerRow Row(std::uint32_t a_playerId)
	{
		return VoicePlayerRow{ a_playerId, "", false, 100 };
	}

	void SpeakerPositionsKeepOrderCoordinatesAndIdLimit()
	{
		const std::vector<RemoteVoicePlayer> players{
			{ 9, 1.5F, -2.0F, 3.0F },
			{ 0xFFFF, 4.0F, 5.0F, 6.0F },
			{ 0x10000, 7.0F, 8.0F, 9.0F },
			{ 2, 0.0F, 0.5F, 0.0F }
		};
		const auto positions = SpeakerPositionsOf(players);
		assert(positions.size() == 3);
		assert(positions[0].playerId == 9 && positions[0].x == 1.5 && positions[0].y == -2.0 && positions[0].z == 3.0);
		assert(positions[1].playerId == 0xFFFF && positions[1].x == 4.0 && positions[1].y == 5.0 && positions[1].z == 6.0);
		assert(positions[2].playerId == 2 && positions[2].x == 0.0 && positions[2].y == 0.5 && positions[2].z == 0.0);
		assert(SpeakerPositionsOf(std::span<const RemoteVoicePlayer>{}).empty());
	}

	void MergeSpeakersIsSortedUniqueAndRowLimited()
	{
		const std::vector<VoicePlayerRow> rows{ Row(5), Row(2), Row(9) };
		const std::vector<std::uint32_t> talk{ 9, 7, 5, 5 };
		const std::vector<std::uint16_t> heard{ 2, 3, 9 };
		const std::vector<std::uint32_t> expected{ 2, 5, 9 };
		assert(MergeSpeakers(talk, heard, rows) == expected);
		assert(MergeSpeakers(std::span<const std::uint32_t>{}, std::span<const std::uint16_t>{}, rows).empty());
		assert(MergeSpeakers(talk, heard, std::span<const VoicePlayerRow>{}).empty());
	}

	void DepartedSpeakersSkipStayedNewAndHighIds()
	{
		const std::vector<VoicePlayerRow> before{ Row(1), Row(2), Row(3), Row(0x10000) };
		const std::vector<VoicePlayerRow> after{ Row(2), Row(4) };
		const std::vector<std::uint16_t> expected{ 1, 3 };
		assert(DepartedSpeakers(before, after) == expected);
		const std::vector<VoicePlayerRow> lastOne{ Row(0xFFFF) };
		const std::vector<std::uint16_t> lastExpected{ 0xFFFF };
		assert(DepartedSpeakers(lastOne, std::span<const VoicePlayerRow>{}) == lastExpected);
		assert(DepartedSpeakers(std::span<const VoicePlayerRow>{}, before).empty());
	}

	void TransmitConfigMapsEveryField()
	{
		VoiceSettings settings;
		settings.voiceChat = false;
		settings.pushToTalk = false;
		settings.openMicSensitivity = -45;
		settings.pushToTalkVirtualKey = 0x51;
		settings.whisperVirtualKey = 0x52;
		settings.shoutVirtualKey = 0x53;
		settings.micMuted = true;
		const auto config = TransmitConfigOf(settings);
		assert(!config.voiceChat);
		assert(!config.pushToTalk);
		assert(config.openMicThresholdDbfs == -45.0F);
		assert(config.pttVk == 0x51);
		assert(config.whisperVk == 0x52);
		assert(config.shoutVk == 0x53);
		assert(config.micMuted);
	}

	void DevicesWantedTruthTable()
	{
		VoiceSettings on;
		VoiceSettings off;
		off.voiceChat = false;
		assert(DevicesWanted(true, on, false));
		assert(!DevicesWanted(false, on, false));
		assert(!DevicesWanted(true, off, false));
		assert(!DevicesWanted(true, on, true));
		assert(!DevicesWanted(false, off, false));
		assert(!DevicesWanted(false, on, true));
		assert(!DevicesWanted(true, off, true));
		assert(!DevicesWanted(false, off, true));
	}

	void SaveDebounceFiresOnceAfterQuietPeriod()
	{
		SaveDebounce debounce;
		assert(!debounce.TakeDue(0.0));
		debounce.MarkDirty(0.0);
		assert(!debounce.TakeDue(0.49));
		assert(debounce.TakeDue(0.5));
		assert(!debounce.TakeDue(0.6));
		assert(!debounce.TakeDue(10.0));
	}

	void SaveDebounceMarkDirtyRestartsDelay()
	{
		SaveDebounce debounce;
		debounce.MarkDirty(0.0);
		debounce.MarkDirty(0.3);
		assert(!debounce.TakeDue(0.79));
		assert(debounce.TakeDue(0.8));
		assert(!debounce.TakeDue(1.0));
	}
}

int main()
{
	SpeakerPositionsKeepOrderCoordinatesAndIdLimit();
	MergeSpeakersIsSortedUniqueAndRowLimited();
	DepartedSpeakersSkipStayedNewAndHighIds();
	TransmitConfigMapsEveryField();
	DevicesWantedTruthTable();
	SaveDebounceFiresOnceAfterQuietPeriod();
	SaveDebounceMarkDirtyRestartsDelay();
	return 0;
}
