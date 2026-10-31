#include "F4TVoiceSession.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

void RunUiProtocolTests();
void RunKeyCaptureTests();

namespace
{
	using F4T::Voice::AggregateStatus;
	using F4T::Voice::DesiredControl;
	using F4T::Voice::MicHot;
	using F4T::Voice::NoticeFlags;
	using F4T::Voice::PlayerVoiceSettings;
	using F4T::Voice::SaveVoiceSettings;
	using F4T::Voice::SelectVoiceStatus;
	using F4T::Voice::SessionPlayer;
	using F4T::Voice::VoicePlayerRow;
	using F4T::Voice::VoiceSession;
	using F4T::Voice::VoiceSettings;
	using F4T::Voice::VoiceStatus;
	using F4T::Voice::VoiceStatusInputs;
	using Ids = std::vector<std::uint32_t>;
	using Json = nlohmann::json;

	VoiceStatus Status(
		const VoiceStatusInputs& a_transmit,
		bool a_outputFailed,
		bool a_serverNoVoice,
		const NoticeFlags& a_notices,
		const VoiceSettings& a_settings)
	{
		return SelectVoiceStatus(AggregateStatus(a_transmit, a_outputFailed, a_serverNoVoice, a_notices, a_settings));
	}

	void SeedsRowsFromStoredNameOrDefaults()
	{
		VoiceSettings settings;
		settings.players["Alpha"] = PlayerVoiceSettings{ 15, true };
		const std::vector<SessionPlayer> roster{ { 7, "Alpha" }, { 3, "Bravo" }, { 9, "" } };
		VoiceSession session;
		assert(session.SyncPlayers(roster, settings));
		const auto& rows = session.Players();
		assert(rows.size() == 3);
		assert((rows[0] == VoicePlayerRow{ 3, "Bravo", false, 100 }));
		assert((rows[1] == VoicePlayerRow{ 7, "Alpha", true, 15 }));
		assert((rows[2] == VoicePlayerRow{ 9, "", false, 100 }));
	}

	void DefaultsFollowMutePlayersByDefault()
	{
		const std::vector<SessionPlayer> roster{ { 7, "Alpha" }, { 3, "" } };
		VoiceSettings muted;
		muted.mutePlayersByDefault = true;
		VoiceSession on;
		assert(on.SyncPlayers(roster, muted));
		assert((on.Players()[0] == VoicePlayerRow{ 3, "", true, 100 }));
		assert((on.Players()[1] == VoicePlayerRow{ 7, "Alpha", true, 100 }));

		VoiceSession off;
		VoiceSettings fresh;
		assert(off.SyncPlayers(roster, fresh));
		assert((off.Players()[0] == VoicePlayerRow{ 3, "", false, 100 }));
		assert(off.BlockedPlayerIds().empty());
	}

	void DepartureRemovesRowsAndSyncReportsChanges()
	{
		VoiceSettings settings;
		VoiceSession session;
		const std::vector<SessionPlayer> both{ { 7, "Alpha" }, { 3, "Bravo" } };
		const std::vector<SessionPlayer> alphaOnly{ { 7, "Alpha" } };
		const std::vector<SessionPlayer> none;
		assert(session.SyncPlayers(both, settings));
		assert(!session.SyncPlayers(both, settings));
		assert(session.SyncPlayers(alphaOnly, settings));
		assert(session.Players().size() == 1);
		assert(session.Players()[0].playerId == 7);
		assert(!session.SyncPlayers(alphaOnly, settings));
		assert(session.SyncPlayers(none, settings));
		assert(session.Players().empty());
	}

	void NameChangeTakesStoredEntryOrKeepsRow()
	{
		VoiceSettings settings;
		settings.players["Alpha"] = PlayerVoiceSettings{ 15, true };
		settings.players["Delta"] = PlayerVoiceSettings{ 40, false };
		VoiceSession session;
		const std::vector<SessionPlayer> alpha{ { 7, "Alpha" } };
		assert(session.SyncPlayers(alpha, settings));
		assert((session.Players()[0] == VoicePlayerRow{ 7, "Alpha", true, 15 }));

		const std::vector<SessionPlayer> delta{ { 7, "Delta" } };
		assert(session.SyncPlayers(delta, settings));
		assert((session.Players()[0] == VoicePlayerRow{ 7, "Delta", false, 40 }));

		const std::vector<SessionPlayer> echo{ { 7, "Echo" } };
		assert(session.SyncPlayers(echo, settings));
		assert((session.Players()[0] == VoicePlayerRow{ 7, "Echo", false, 40 }));
	}

	void SetMutedAndVolumeWriteStoredEntriesForNamedRowsOnly()
	{
		VoiceSettings settings;
		VoiceSession session;
		const std::vector<SessionPlayer> roster{ { 7, "Alpha" }, { 3, "Bravo" }, { 9, "" } };
		assert(session.SyncPlayers(roster, settings));
		assert(!session.SetMuted(42, true, settings));
		assert(!session.SetVolume(42, 50, settings));
		assert(!session.SetVolume(7, 101, settings));
		assert(!session.SetVolume(7, -1, settings));
		assert(settings.players.empty());

		assert(session.SetMuted(7, true, settings));
		assert((settings.players.at("Alpha") == PlayerVoiceSettings{ 100, true }));
		assert(session.SetVolume(3, 40, settings));
		assert((settings.players.at("Bravo") == PlayerVoiceSettings{ 40, false }));
		assert(session.SetMuted(3, true, settings));
		assert((settings.players.at("Bravo") == PlayerVoiceSettings{ 40, true }));
		assert(session.SetMuted(9, true, settings));
		assert(settings.players.size() == 2);
		assert((session.BlockedPlayerIds() == Ids{ 3, 7, 9 }));
	}

	void InvalidUtf8NameIsNotStoredSoSaveStillWorks()
	{
		VoiceSettings settings;
		VoiceSession session;
		const std::vector<SessionPlayer> roster{ { 7, "\xff\xfe" }, { 3, "Bravo" } };
		assert(session.SyncPlayers(roster, settings));
		assert(session.SetMuted(7, true, settings));
		assert(session.SetVolume(3, 40, settings));
		const auto root = std::filesystem::temp_directory_path() / "co-voice-session-tests";
		assert(SaveVoiceSettings(root / "voice-settings.json", settings));
		assert(settings.players.size() == 1);
		assert((settings.players.at("Bravo") == PlayerVoiceSettings{ 40, false }));
		assert((session.BlockedPlayerIds() == Ids{ 7 }));
		std::error_code ec;
		std::filesystem::remove_all(root, ec);
	}

	void TalkAddsRemovesAndIgnoresInvalidFields()
	{
		VoiceSettings settings;
		VoiceSession session;
		const std::vector<SessionPlayer> roster{ { 7, "Alpha" }, { 3, "Bravo" } };
		assert(session.SyncPlayers(roster, settings));
		assert(session.ApplyTalk(Json{ { "playerId", 7 }, { "talking", true }, { "tier", "normal" } }));
		assert(!session.ApplyTalk(Json{ { "playerId", 7 }, { "talking", true }, { "tier", "shout" } }));
		assert(session.ApplyTalk(Json{ { "playerId", 3 }, { "talking", true }, { "tier", "whisper" } }));
		assert((session.TalkSpeakers() == Ids{ 3, 7 }));
		assert(session.ApplyTalk(Json{ { "playerId", 7 }, { "talking", false }, { "tier", "normal" } }));
		assert(!session.ApplyTalk(Json{ { "playerId", 7 }, { "talking", false }, { "tier", "normal" } }));
		assert((session.TalkSpeakers() == Ids{ 3 }));

		assert(!session.ApplyTalk(Json{ { "playerId", 99 }, { "talking", true }, { "tier", "normal" } }));
		assert((session.TalkSpeakers() == Ids{ 3 }));

		const Json malformed[] = {
			Json{ { "playerId", "7" }, { "talking", true }, { "tier", "normal" } },
			Json{ { "playerId", -1 }, { "talking", true }, { "tier", "normal" } },
			Json{ { "playerId", 4294967296ULL }, { "talking", true }, { "tier", "normal" } },
			Json{ { "playerId", 1.5 }, { "talking", true }, { "tier", "normal" } },
			Json{ { "playerId", 7 }, { "talking", "yes" }, { "tier", "normal" } },
			Json{ { "playerId", 7 }, { "talking", true }, { "tier", "loud" } },
			Json{ { "playerId", 7 }, { "talking", true }, { "tier", 1 } },
			Json{ { "playerId", 7 }, { "talking", true } },
			Json{ { "talking", true }, { "tier", "normal" } },
			Json::array()
		};
		for (const auto& packet : malformed) {
			assert(!session.ApplyTalk(packet));
		}
		assert((session.TalkSpeakers() == Ids{ 3 }));

		assert(session.ApplyTalk(Json{ { "playerId", 7 }, { "talking", true }, { "tier", "normal" } }));
		const std::vector<SessionPlayer> alphaOnly{ { 7, "Alpha" } };
		assert(session.SyncPlayers(alphaOnly, settings));
		assert((session.TalkSpeakers() == Ids{ 7 }));

		session.Reset();
		assert(session.Players().empty());
		assert(session.TalkSpeakers().empty());
		assert(session.BlockedPlayerIds().empty());
	}

	void NoticesExpireOnTheirSchedule()
	{
		VoiceSession session;
		assert(session.ApplyNotice(Json{ { "code", "serverMuted" }, { "seconds", 5 } }, 100.0));
		assert(session.Notices(104.999).serverMuted);
		assert(!session.Notices(105.0).serverMuted);

		VoiceSession held;
		assert(held.ApplyNotice(Json{ { "code", "serverMuted" } }, 100.0));
		assert(held.Notices(1.0e9).serverMuted);
		assert(held.ApplyNotice(Json{ { "code", "serverMuted" }, { "seconds", 0 } }, 200.0));
		assert(held.Notices(1.0e9).serverMuted);
		assert(held.ApplyNotice(Json{ { "code", "serverMuted" }, { "seconds", "5" } }, 300.0));
		assert(held.Notices(1.0e9).serverMuted);
		assert(held.ApplyNotice(Json{ { "code", "serverUnmuted" } }, 400.0));
		assert(!held.Notices(400.0).serverMuted);
	}

	void RateLimitedDefaultsToThirtySeconds()
	{
		VoiceSession session;
		assert(session.ApplyNotice(Json{ { "code", "rateLimited" } }, 50.0));
		assert(session.Notices(79.999).rateLimited);
		assert(!session.Notices(80.0).rateLimited);
		assert(session.ApplyNotice(Json{ { "code", "rateLimited" }, { "seconds", 4 } }, 100.0));
		assert(session.Notices(103.999).rateLimited);
		assert(!session.Notices(104.0).rateLimited);
		assert(session.ApplyNotice(Json{ { "code", "rateLimited" }, { "seconds", 0 } }, 200.0));
		assert(session.Notices(229.999).rateLimited);
		assert(!session.Notices(230.0).rateLimited);
		assert(session.ApplyNotice(Json{ { "code", "rateLimited" }, { "seconds", "9" } }, 300.0));
		assert(session.Notices(329.999).rateLimited);
		assert(!session.Notices(330.0).rateLimited);
	}

	void ProfileMismatchIsStickyUntilReset()
	{
		VoiceSession session;
		assert(session.ApplyNotice(Json{ { "code", "profileMismatch" } }, 0.0));
		assert(session.Notices(1.0e9).profileMismatch);
		session.Reset();
		assert(session.Notices(0.0) == NoticeFlags{});
	}

	void UnknownNoticesAreIgnored()
	{
		VoiceSession session;
		assert(!session.ApplyNotice(Json{ { "code", "banana" } }, 0.0));
		assert(!session.ApplyNotice(Json{ { "seconds", 5 } }, 0.0));
		assert(!session.ApplyNotice(Json{ { "code", 5 } }, 0.0));
		assert(session.Notices(0.0) == NoticeFlags{});
	}

	void DesiredControlFollowsSettingsAndFailures()
	{
		VoiceSettings settings;
		const auto state = DesiredControl(settings, Ids{ 3, 7 }, false, false, false);
		assert(state.canSend && state.canPlay && !state.deafened);
		assert((state.blockedPlayerIds == Ids{ 3, 7 }));
		assert(!DesiredControl(settings, {}, true, false, false).canSend);
		assert(!DesiredControl(settings, {}, false, true, false).canSend);
		assert(DesiredControl(settings, {}, false, false, true).canSend);
		assert(!DesiredControl(settings, {}, false, false, true).canPlay);
		settings.micMuted = true;
		assert(!DesiredControl(settings, {}, false, false, false).canSend);
		assert(DesiredControl(settings, {}, false, false, false).canPlay);
		settings.micMuted = false;
		settings.voiceChat = false;
		const auto off = DesiredControl(settings, {}, false, false, false);
		assert(!off.canSend && !off.canPlay);
		settings.voiceChat = true;
		settings.deafened = true;
		assert(DesiredControl(settings, {}, false, false, false).deafened);
	}

	void DesiredControlTruthTable()
	{
		for (int bits = 0; bits < 64; ++bits) {
			const auto bit = [bits](int a_mask) { return (bits & a_mask) != 0; };
			VoiceSettings settings;
			settings.voiceChat = bit(1);
			settings.micMuted = bit(2);
			settings.deafened = bit(32);
			const bool codecFailed = bit(4);
			const bool micFailed = bit(8);
			const bool outputFailed = bit(16);
			const auto state = DesiredControl(settings, {}, codecFailed, micFailed, outputFailed);
			assert(state.canSend == (settings.voiceChat && !settings.micMuted && !codecFailed && !micFailed));
			assert(state.canPlay == (settings.voiceChat && !outputFailed));
			assert(state.deafened == settings.deafened);
		}
	}

	void AggregateStatusSelectsTheExpectedCode()
	{
		const VoiceSettings on;
		const VoiceStatusInputs quiet{};
		assert(Status(quiet, false, false, {}, on) == VoiceStatus::ready);

		VoiceStatusInputs transmitting{};
		transmitting.transmitting = true;
		assert(Status(transmitting, false, false, {}, on) == VoiceStatus::transmitting);

		assert(Status(quiet, true, false, {}, on) == VoiceStatus::noOutput);
		assert(Status(quiet, false, true, {}, on) == VoiceStatus::serverNoVoice);
		assert(Status(quiet, false, false, NoticeFlags{ .serverMuted = true }, on) == VoiceStatus::serverMuted);
		assert(Status(quiet, false, false, NoticeFlags{ .rateLimited = true }, on) == VoiceStatus::rateLimited);
		assert(Status(quiet, false, false, NoticeFlags{ .profileMismatch = true }, on) == VoiceStatus::profileMismatch);

		VoiceSettings deafened = on;
		deafened.deafened = true;
		assert(Status(quiet, false, false, {}, deafened) == VoiceStatus::deafened);
		VoiceSettings muted = on;
		muted.micMuted = true;
		assert(Status(quiet, false, false, {}, muted) == VoiceStatus::micMuted);

		VoiceStatusInputs failed{};
		failed.micFailed = true;
		assert(Status(failed, false, false, {}, on) == VoiceStatus::micFailed);
	}

	void VoiceChatOffShowsMicMutedAndClearsDeviceFailures()
	{
		VoiceSettings off;
		off.voiceChat = false;
		VoiceStatusInputs failing{};
		failing.micFailed = true;
		failing.micRecovering = true;
		failing.micSilentHint = true;
		failing.codecFailed = true;
		failing.transmitting = true;
		assert(Status(failing, true, false, {}, off) == VoiceStatus::micMuted);

		const auto inputs = AggregateStatus(failing, true, false, {}, off);
		assert(!inputs.micFailed && !inputs.micRecovering && !inputs.micSilentHint);
		assert(!inputs.codecFailed && !inputs.noOutput && !inputs.transmitting);
		assert(inputs.micMuted && inputs.deafened);
	}

	void StrangerMuteStaysInSessionMemoryOnly()
	{
		VoiceSettings settings;
		VoiceSession session;
		const std::vector<SessionPlayer> roster{ { 7, "Stranger" }, { 3, "Stranger" } };
		assert(session.SyncPlayers(roster, settings));
		assert(session.SetMuted(7, true, settings));
		assert(settings.players.empty());
		assert((session.BlockedPlayerIds() == Ids{ 7 }));
		assert(session.SetMuted(3, true, settings));
		assert(settings.players.empty());
		assert((session.BlockedPlayerIds() == Ids{ 3, 7 }));
		VoiceSession again;
		assert(again.SyncPlayers(roster, settings));
		assert(again.BlockedPlayerIds().empty());
	}

	void LegacyPlayerIdKeysAreNeverRead()
	{
		VoiceSettings settings;
		settings.players["playerId:7"] = PlayerVoiceSettings{ 100, true };
		VoiceSession session;
		const std::vector<SessionPlayer> roster{ { 7, "Stranger" } };
		assert(session.SyncPlayers(roster, settings));
		assert(session.BlockedPlayerIds().empty());
	}

	void StrangerMuteCarriesToNameKeyOnIdentify()
	{
		VoiceSettings settings;
		VoiceSession session;
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ { 7, "Stranger" } }, settings));
		assert(session.SetMuted(7, true, settings));
		assert(settings.players.empty());
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ { 7, "Alpha" } }, settings));
		assert((session.BlockedPlayerIds() == Ids{ 7 }));
		assert((settings.players.at("Alpha") == PlayerVoiceSettings{ 100, true }));
		VoiceSession again;
		assert(again.SyncPlayers(std::vector<SessionPlayer>{ { 7, "Alpha" } }, settings));
		assert((again.BlockedPlayerIds() == Ids{ 7 }));
	}

	void UnmutedStrangerKeepsStoredNameMuteOnIdentify()
	{
		VoiceSettings settings;
		settings.players["Alpha"] = PlayerVoiceSettings{ 100, true };
		VoiceSession session;
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ { 7, "Stranger" } }, settings));
		assert(session.BlockedPlayerIds().empty());
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ { 7, "Alpha" } }, settings));
		assert((session.BlockedPlayerIds() == Ids{ 7 }));
	}

	void MutedKnownNameStaysBlockedWhenItTurnsStranger()
	{
		VoiceSettings settings;
		VoiceSession session;
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ { 7, "Bob" } }, settings));
		assert(session.SetMuted(7, true, settings));
		assert((settings.players.at("Bob") == PlayerVoiceSettings{ 100, true }));
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ { 7, "Stranger" } }, settings));
		assert(session.Players().size() == 1 && session.Players()[0].name == "Stranger");
		assert((session.BlockedPlayerIds() == Ids{ 7 }));
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ { 7, "Bob" } }, settings));
		assert((session.BlockedPlayerIds() == Ids{ 7 }));
	}

	void MicHotTruthTable()
	{
		for (int bits = 0; bits < 64; ++bits) {
			const auto bit = [bits](int a_mask) { return (bits & a_mask) != 0; };
			VoiceSettings settings;
			settings.voiceChat = bit(2);
			settings.micMuted = bit(4);
			settings.pushToTalk = bit(8);
			const bool active = bit(1);
			const bool pttHeld = bit(16);
			const bool transmitting = bit(32);
			const bool expected = active && (transmitting || (settings.voiceChat && !settings.micMuted && (!settings.pushToTalk || pttHeld)));
			assert(MicHot(active, settings, pttHeld, transmitting) == expected);
		}
	}
}

int main()
{
	SeedsRowsFromStoredNameOrDefaults();
	DefaultsFollowMutePlayersByDefault();
	DepartureRemovesRowsAndSyncReportsChanges();
	NameChangeTakesStoredEntryOrKeepsRow();
	SetMutedAndVolumeWriteStoredEntriesForNamedRowsOnly();
	StrangerMuteStaysInSessionMemoryOnly();
	LegacyPlayerIdKeysAreNeverRead();
	StrangerMuteCarriesToNameKeyOnIdentify();
	UnmutedStrangerKeepsStoredNameMuteOnIdentify();
	MutedKnownNameStaysBlockedWhenItTurnsStranger();

	InvalidUtf8NameIsNotStoredSoSaveStillWorks();
	TalkAddsRemovesAndIgnoresInvalidFields();
	NoticesExpireOnTheirSchedule();
	RateLimitedDefaultsToThirtySeconds();
	ProfileMismatchIsStickyUntilReset();
	UnknownNoticesAreIgnored();
	DesiredControlFollowsSettingsAndFailures();
	DesiredControlTruthTable();
	AggregateStatusSelectsTheExpectedCode();
	VoiceChatOffShowsMicMutedAndClearsDeviceFailures();
	MicHotTruthTable();
	RunUiProtocolTests();
	RunKeyCaptureTests();
	return 0;
}
