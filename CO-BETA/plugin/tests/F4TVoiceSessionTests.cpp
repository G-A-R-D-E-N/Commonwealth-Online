#include "F4TVoiceSession.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
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

	SessionPlayer Keyed(std::uint32_t a_playerId, std::string a_name, bool a_nameResolved = false)
	{
		return SessionPlayer{ a_playerId, std::move(a_name), a_nameResolved, "key-" + std::to_string(a_playerId) };
	}

	void SeedsRowsFromStoredKeyOrDefaults()
	{
		VoiceSettings settings;
		settings.players["key-7"] = PlayerVoiceSettings{ 15, true };
		const std::vector<SessionPlayer> roster{ Keyed(7, "Alpha", true), Keyed(3, "Bravo", true), Keyed(9, "") };
		VoiceSession session;
		assert(session.SyncPlayers(roster, settings));
		const auto& rows = session.Players();
		assert(rows.size() == 3);
		assert((rows[0] == VoicePlayerRow{ 3, "Bravo", false, 100, true, "key-3" }));
		assert((rows[1] == VoicePlayerRow{ 7, "Alpha", true, 15, true, "key-7" }));
		assert((rows[2] == VoicePlayerRow{ 9, "", false, 100, false, "key-9" }));
	}

	void DefaultsFollowMutePlayersByDefault()
	{
		const std::vector<SessionPlayer> roster{ Keyed(7, "Alpha", true), Keyed(3, "") };
		VoiceSettings muted;
		muted.mutePlayersByDefault = true;
		VoiceSession on;
		assert(on.SyncPlayers(roster, muted));
		assert((on.Players()[0] == VoicePlayerRow{ 3, "", true, 100, false, "key-3" }));
		assert((on.Players()[1] == VoicePlayerRow{ 7, "Alpha", true, 100, true, "key-7" }));

		VoiceSession off;
		VoiceSettings fresh;
		assert(off.SyncPlayers(roster, fresh));
		assert((off.Players()[0] == VoicePlayerRow{ 3, "", false, 100, false, "key-3" }));
		assert(off.BlockedPlayerIds().empty());
	}

	void DepartureRemovesRowsAndSyncReportsChanges()
	{
		VoiceSettings settings;
		VoiceSession session;
		const std::vector<SessionPlayer> both{ Keyed(7, "Alpha", true), Keyed(3, "Bravo", true) };
		const std::vector<SessionPlayer> alphaOnly{ Keyed(7, "Alpha", true) };
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

	void RenameKeepsStoredMuteByKey()
	{
		VoiceSettings settings;
		settings.players["key-7"] = PlayerVoiceSettings{ 15, true };
		VoiceSession session;
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ Keyed(7, "Alpha", true) }, settings));
		assert((session.Players()[0] == VoicePlayerRow{ 7, "Alpha", true, 15, true, "key-7" }));

		assert(session.SyncPlayers(std::vector<SessionPlayer>{ Keyed(7, "Delta", true) }, settings));
		assert((session.Players()[0] == VoicePlayerRow{ 7, "Delta", true, 15, true, "key-7" }));
		assert(session.BlockedPlayerIds() == Ids{ 7 });
		assert(settings.players.size() == 1);
	}

	void MutedRowsBeyondFifteenAllReachTheServer()
	{
		VoiceSettings settings;
		VoiceSession session;
		std::vector<SessionPlayer> roster;
		for (std::uint32_t id = 1; id <= 100; ++id) {
			roster.push_back(Keyed(id, "Player" + std::to_string(id), true));
		}
		roster.push_back(Keyed(70000, "Far", true));
		assert(session.SyncPlayers(roster, settings));
		for (std::uint32_t id = 1; id <= 100; ++id) {
			assert(session.SetMuted(id, true, settings));
		}
		assert(session.SetMuted(70000, true, settings));
		const auto blocked = session.BlockedPlayerIds();
		assert(blocked.size() == 100);
		assert(std::find(blocked.begin(), blocked.end(), 100U) != blocked.end());
		assert(std::find(blocked.begin(), blocked.end(), 70000U) == blocked.end());
	}

	void LegacyNameKeyIsNotMigratedToAnUnprovenIdentity()
	{
		VoiceSettings settings;
		settings.players["Alpha"] = PlayerVoiceSettings{ 15, true };
		VoiceSession session;
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ Keyed(7, "Alpha", true) }, settings));
		assert((settings.players.at("Alpha") == PlayerVoiceSettings{ 15, true }));
		assert(settings.players.count("key-7") == 0);
		assert(session.BlockedPlayerIds().empty());
	}

	void LegacyNameKeyIsKeptWithoutPlayerKey()
	{
		VoiceSettings settings;
		settings.players["Alpha"] = PlayerVoiceSettings{ 15, true };
		VoiceSession session;
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ SessionPlayer{ 7, "Alpha", true, "" } }, settings));
		assert((settings.players.at("Alpha") == PlayerVoiceSettings{ 15, true }));
		assert((session.BlockedPlayerIds() == Ids{ 7 }));
		assert(session.SetVolume(7, 40, settings));
		assert((settings.legacyPlayers.at("Alpha") == PlayerVoiceSettings{ 40, true }));
	}

	void SetMutedAndVolumeWriteStoredEntriesForNamedRowsOnly()
	{
		VoiceSettings settings;
		VoiceSession session;
		const std::vector<SessionPlayer> roster{ Keyed(7, "Alpha", true), Keyed(3, "Bravo", true), Keyed(9, "") };
		assert(session.SyncPlayers(roster, settings));
		assert(!session.SetMuted(42, true, settings));
		assert(!session.SetVolume(42, 50, settings));
		assert(!session.SetVolume(7, 101, settings));
		assert(!session.SetVolume(7, -1, settings));
		assert(settings.players.empty());

		assert(session.SetMuted(7, true, settings));
		assert((settings.players.at("key-7") == PlayerVoiceSettings{ 100, true }));
		assert(session.SetVolume(3, 40, settings));
		assert((settings.players.at("key-3") == PlayerVoiceSettings{ 40, false }));
		assert(session.SetMuted(3, true, settings));
		assert((settings.players.at("key-3") == PlayerVoiceSettings{ 40, true }));
		assert(session.SetMuted(9, true, settings));
		assert(settings.players.size() == 2);
		assert((session.BlockedPlayerIds() == Ids{ 3, 7, 9 }));
	}

	void InvalidUtf8NameDoesNotBlockSave()
	{
		VoiceSettings settings;
		VoiceSession session;
		const std::vector<SessionPlayer> roster{ Keyed(7, "\xff\xfe", true), Keyed(3, "Bravo", true) };
		assert(session.SyncPlayers(roster, settings));
		assert(session.SetMuted(7, true, settings));
		assert(session.SetVolume(3, 40, settings));
		const auto root = std::filesystem::temp_directory_path() / "co-voice-session-tests";
		assert(SaveVoiceSettings(root / "voice-settings.json", settings));
		assert(settings.players.size() == 2);
		assert((settings.players.at("key-3") == PlayerVoiceSettings{ 40, false }));
		assert((settings.players.at("key-7") == PlayerVoiceSettings{ 100, true }));
		assert((session.BlockedPlayerIds() == Ids{ 7 }));
		std::error_code ec;
		std::filesystem::remove_all(root, ec);
	}

	void TalkAddsRemovesAndIgnoresInvalidFields()
	{
		VoiceSettings settings;
		VoiceSession session;
		const std::vector<SessionPlayer> roster{ Keyed(7, "Alpha", true), Keyed(3, "Bravo", true) };
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
		const std::vector<SessionPlayer> alphaOnly{ Keyed(7, "Alpha", true) };
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

	void DuplicateNameLeavesLegacyKeyUntouched()
	{
		VoiceSettings settings;
		settings.players["Alpha"] = PlayerVoiceSettings{ 15, true };
		VoiceSession session;
		const std::vector<SessionPlayer> roster{ Keyed(7, "Alpha", true), Keyed(8, "Alpha", true) };
		assert(session.SyncPlayers(roster, settings));
		assert((settings.players.at("Alpha") == PlayerVoiceSettings{ 15, true }));
		assert(settings.players.count("key-7") == 0);
		assert(settings.players.count("key-8") == 0);
		assert((session.Players()[0] == VoicePlayerRow{ 7, "Alpha", false, 100, true, "key-7" }));
	}

	void StrangerMuteStaysInSessionMemoryOnly()
	{
		VoiceSettings settings;
		VoiceSession session;
		const std::vector<SessionPlayer> roster{ Keyed(7, "Stranger"), Keyed(3, "Stranger") };
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

	void ResolvedStrangerNameIsStoredLikeAnyName()
	{
		VoiceSettings settings;
		settings.players["key-7"] = PlayerVoiceSettings{ 100, true };
		VoiceSession session;
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ Keyed(7, "Stranger", true) }, settings));
		assert((session.BlockedPlayerIds() == Ids{ 7 }));
		assert(session.SetVolume(7, 40, settings));
		assert((settings.players.at("key-7") == PlayerVoiceSettings{ 40, true }));
		assert(session.SetMuted(7, false, settings));
		assert((settings.players.at("key-7") == PlayerVoiceSettings{ 40, false }));
		assert(session.BlockedPlayerIds().empty());
	}

	void UnresolvedStrangerPlaceholderIgnoresStoredName()
	{
		VoiceSettings settings;
		settings.players["key-7"] = PlayerVoiceSettings{ 100, true };
		VoiceSession session;
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ Keyed(7, "Stranger", false) }, settings));
		assert(session.BlockedPlayerIds().empty());
		assert(session.SetMuted(7, true, settings));
		assert(session.SetVolume(7, 40, settings));
		assert((settings.players.at("key-7") == PlayerVoiceSettings{ 100, true }));
		assert((session.BlockedPlayerIds() == Ids{ 7 }));
	}

	void PlaceholderBecomingResolvedCarriesMuteToStoredKey()
	{
		VoiceSettings settings;
		VoiceSession session;
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ Keyed(7, "Stranger", false) }, settings));
		assert(session.SetMuted(7, true, settings));
		assert(settings.players.empty());
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ Keyed(7, "Stranger", true) }, settings));
		assert((session.BlockedPlayerIds() == Ids{ 7 }));
		assert((settings.players.at("key-7") == PlayerVoiceSettings{ 100, true }));
	}

	void LegacyPlayerIdKeysAreNeverRead()
	{
		VoiceSettings settings;
		settings.players["playerId:7"] = PlayerVoiceSettings{ 100, true };
		VoiceSession session;
		const std::vector<SessionPlayer> roster{ Keyed(7, "Stranger") };
		assert(session.SyncPlayers(roster, settings));
		assert(session.BlockedPlayerIds().empty());
	}

	void StrangerMuteCarriesToKeyOnIdentify()
	{
		VoiceSettings settings;
		VoiceSession session;
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ Keyed(7, "Stranger") }, settings));
		assert(session.SetMuted(7, true, settings));
		assert(settings.players.empty());
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ Keyed(7, "Alpha", true) }, settings));
		assert((session.BlockedPlayerIds() == Ids{ 7 }));
		assert((settings.players.at("key-7") == PlayerVoiceSettings{ 100, true }));
		VoiceSession again;
		assert(again.SyncPlayers(std::vector<SessionPlayer>{ Keyed(7, "Alpha", true) }, settings));
		assert((again.BlockedPlayerIds() == Ids{ 7 }));
	}

	void UnmutedStrangerKeepsStoredKeyMuteOnIdentify()
	{
		VoiceSettings settings;
		settings.players["key-7"] = PlayerVoiceSettings{ 100, true };
		VoiceSession session;
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ Keyed(7, "Stranger") }, settings));
		assert(session.BlockedPlayerIds().empty());
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ Keyed(7, "Alpha", true) }, settings));
		assert((session.BlockedPlayerIds() == Ids{ 7 }));
	}

	void MutedKnownNameStaysBlockedWhenItTurnsStranger()
	{
		VoiceSettings settings;
		VoiceSession session;
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ Keyed(7, "Bob", true) }, settings));
		assert(session.SetMuted(7, true, settings));
		assert((settings.players.at("key-7") == PlayerVoiceSettings{ 100, true }));
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ Keyed(7, "Stranger") }, settings));
		assert(session.Players().size() == 1 && session.Players()[0].name == "Stranger");
		assert((session.BlockedPlayerIds() == Ids{ 7 }));
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ Keyed(7, "Bob", true) }, settings));
		assert((session.BlockedPlayerIds() == Ids{ 7 }));
	}

	void KeyChangeDoesNotTransferSettingsWithoutIdentityProof()
	{
		VoiceSettings settings;
		VoiceSession session;
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ SessionPlayer{ 7, "Alpha", true, "old-key" } }, settings));
		assert(session.SetMuted(7, true, settings));
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ SessionPlayer{ 7, "Alpha", true, "new-key" } }, settings));
		assert((session.BlockedPlayerIds() == Ids{ 7 }));
		assert(settings.players.count("new-key") == 0);
		assert(session.SetVolume(7, 40, settings));
		assert((session.BlockedPlayerIds() == Ids{ 7 }));
		assert((settings.players.at("new-key") == PlayerVoiceSettings{ 40, false }));
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ SessionPlayer{ 7, "Renamed", true, "new-key" } }, settings));
		assert((session.BlockedPlayerIds() == Ids{ 7 }));
		assert((settings.players.at("new-key") == PlayerVoiceSettings{ 40, false }));
		assert(session.SetMuted(7, true, settings));
		assert((settings.players.at("new-key") == PlayerVoiceSettings{ 40, true }));
	}

	void KeylessNameCannotCollideWithPlayerKey()
	{
		const std::string name(64, 'a');
		VoiceSettings settings;
		settings.players[name] = PlayerVoiceSettings{ 20, true };
		VoiceSession session;
		assert(session.SyncPlayers(std::vector<SessionPlayer>{ SessionPlayer{ 7, name, true, "" } }, settings));
		assert(session.BlockedPlayerIds().empty());
		assert(session.SetVolume(7, 40, settings));
		assert((settings.players.at(name) == PlayerVoiceSettings{ 20, true }));
		assert((settings.legacyPlayers.at(name) == PlayerVoiceSettings{ 40, false }));
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
	SeedsRowsFromStoredKeyOrDefaults();
	MutedRowsBeyondFifteenAllReachTheServer();
	DefaultsFollowMutePlayersByDefault();
	DepartureRemovesRowsAndSyncReportsChanges();
	RenameKeepsStoredMuteByKey();
	LegacyNameKeyIsNotMigratedToAnUnprovenIdentity();
	LegacyNameKeyIsKeptWithoutPlayerKey();
	KeyChangeDoesNotTransferSettingsWithoutIdentityProof();
	KeylessNameCannotCollideWithPlayerKey();
	DuplicateNameLeavesLegacyKeyUntouched();
	SetMutedAndVolumeWriteStoredEntriesForNamedRowsOnly();
	StrangerMuteStaysInSessionMemoryOnly();
	LegacyPlayerIdKeysAreNeverRead();
	StrangerMuteCarriesToKeyOnIdentify();
	UnmutedStrangerKeepsStoredKeyMuteOnIdentify();
	MutedKnownNameStaysBlockedWhenItTurnsStranger();
	ResolvedStrangerNameIsStoredLikeAnyName();
	UnresolvedStrangerPlaceholderIgnoresStoredName();
	PlaceholderBecomingResolvedCarriesMuteToStoredKey();

	InvalidUtf8NameDoesNotBlockSave();
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
