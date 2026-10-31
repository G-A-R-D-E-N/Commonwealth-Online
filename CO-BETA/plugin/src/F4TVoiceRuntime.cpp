#include "pch.h"

#include "F4TVoiceRuntime.h"

#include "F4TDebugLogging.h"
#include "F4TF4SECapabilities.h"
#include "F4TGnsVoice.h"
#include "F4TLocalPlayerState.h"
#include "F4TRemotePlayerState.h"
#include "F4TSocialNameplate.h"
#include "F4TReplicationRuntime.h"
#include "F4TSessionWorldState.h"
#include "F4TTextEntryMode.h"
#include "F4TUIClient.h"
#include "F4TVoiceDevices.h"
#include "F4TVoiceKeyCapture.h"
#include "F4TVoiceReceive.h"
#include "F4TVoiceRuntimeLogic.h"
#include "F4TVoiceSession.h"
#include "F4TVoiceSettings.h"
#include "F4TVoiceStatus.h"
#include "F4TVoiceTransmit.h"
#include "F4TVoiceUi.h"
#include "F4TVoiceUiProtocol.h"

#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

namespace F4T::VoiceRuntime
{
	namespace
	{
		constexpr std::size_t kMaxQueuedControlEvents = 512;
		constexpr std::size_t kMaxQueuedActions = 64;
		constexpr double kDeviceRetrySeconds = 2.0;
		constexpr double kLevelIntervalSeconds = 0.1;

		struct Inbox
		{
			std::mutex actionMutex;
			std::deque<std::string> actions;
			std::uint64_t droppedActions{};
			std::mutex controlMutex;
			std::deque<nlohmann::json> controlEvents;
			std::uint64_t droppedControlEvents{};
		};

		struct RuntimeState
		{
			F4T::Voice::VoiceSettings settings;
			F4T::Voice::VoiceSession session;
			F4T::Voice::KeyCapture keyCapture;
			F4T::Voice::SaveDebounce saveDebounce;
			F4T::Voice::Transmitter* transmitter{};
			F4T::Voice::Receiver* receiver{};
			F4T::ReplicationRuntime::Generation generation{ F4T::ReplicationRuntime::kNoSession };
			bool transmitterRunning{};
			bool receiverOpened{};
			bool receiverRunning{};
			double transmitRetryAt{};
			double receiveRetryAt{};
			double levelAt{};
			std::string lastStatus;
			std::vector<std::uint32_t> lastSpeakers;
		};

		struct RosterSnapshot
		{
			std::vector<F4T::Voice::SessionPlayer> players;
			std::vector<F4T::Voice::RemoteVoicePlayer> remotes;
		};

		double Clock()
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		Inbox& GetInbox()
		{
			static Inbox* const inbox = new Inbox;
			return *inbox;
		}

		RuntimeState& GetState()
		{
			static RuntimeState state;
			return state;
		}

		std::mutex g_muteMutex;
		std::map<std::uint32_t, bool> g_muteMirror;

		void RememberMute(std::uint32_t a_playerId, bool a_muted)
		{
			if (a_playerId == 0) return;
			const std::scoped_lock lock(g_muteMutex);
			g_muteMirror[a_playerId] = a_muted;
		}

		void ForgetMuteMirror()
		{
			const std::scoped_lock lock(g_muteMutex);
			g_muteMirror.clear();
		}

		void OnUiAction(const char* a_actionJson)
		{
			auto& inbox = GetInbox();
			const std::scoped_lock lock(inbox.actionMutex);
			if (inbox.actions.size() >= kMaxQueuedActions) {
				++inbox.droppedActions;
				return;
			}
			inbox.actions.emplace_back(a_actionJson);
		}

		void OnControlEvent(const nlohmann::json& a_packet)
		{
			auto& inbox = GetInbox();
			const std::scoped_lock lock(inbox.controlMutex);
			if (inbox.controlEvents.size() >= kMaxQueuedControlEvents) {
				inbox.controlEvents.pop_front();
				++inbox.droppedControlEvents;
			}
			inbox.controlEvents.push_back(a_packet);
		}

		void PushSettings(const RuntimeState& a_state)
		{
			F4T::VoiceUi::PushEvent(F4T::Voice::SettingsEvent(a_state.settings).c_str());
		}

		void PushPlayers(const RuntimeState& a_state)
		{
			F4T::VoiceUi::PushEvent(F4T::Voice::PlayersEvent(a_state.session.Players()).c_str());
		}

		void PushSpeakers(RuntimeState& a_state, std::vector<std::uint32_t> a_speakers)
		{
			F4T::VoiceUi::PushEvent(F4T::Voice::SpeakersEvent(a_speakers).c_str());
			a_state.lastSpeakers = std::move(a_speakers);
		}

		void ApplySpeakerRow(F4T::Voice::Receiver& a_receiver, const F4T::Voice::VoicePlayerRow& a_row)
		{
			if (a_row.playerId > F4T::Voice::kMaxSpeakerId) {
				return;
			}
			const auto speakerId = static_cast<std::uint16_t>(a_row.playerId);
			a_receiver.SetSpeakerMuted(speakerId, a_row.muted);
			a_receiver.SetSpeakerVolume(speakerId, a_row.volume);
		}

		void ApplyRowFor(const RuntimeState& a_state, std::uint32_t a_playerId)
		{
			const auto& rows = a_state.session.Players();
			const auto row = std::find_if(rows.begin(), rows.end(), [a_playerId](const F4T::Voice::VoicePlayerRow& a_candidate) {
				return a_candidate.playerId == a_playerId;
			});
			if (row != rows.end()) {
				ApplySpeakerRow(*a_state.receiver, *row);
			}
		}

		void HandleSetting(RuntimeState& a_state, const F4T::Voice::SetSettingAction& a_action, double a_now)
		{
			if (F4T::Voice::ApplySetting(a_state.settings, a_action.key, a_action.value)) {
				a_state.saveDebounce.MarkDirty(a_now);
			}
			PushSettings(a_state);
		}

		void HandleMute(RuntimeState& a_state, const F4T::Voice::MutePlayerAction& a_action, double a_now)
		{
			// Constraint: the wheel reads this mirror, so it must only record a mute that took effect.
			if (a_state.session.SetMuted(a_action.playerId, a_action.muted, a_state.settings)) {
				a_state.saveDebounce.MarkDirty(a_now);
				ApplyRowFor(a_state, a_action.playerId);
				RememberMute(a_action.playerId, a_action.muted);
			}
			PushPlayers(a_state);
		}

		void HandleVolume(RuntimeState& a_state, const F4T::Voice::SetPlayerVolumeAction& a_action, double a_now)
		{
			if (a_state.session.SetVolume(a_action.playerId, a_action.volume, a_state.settings)) {
				a_state.saveDebounce.MarkDirty(a_now);
				ApplyRowFor(a_state, a_action.playerId);
			}
			PushPlayers(a_state);
		}

		void HandleAction(RuntimeState& a_state, std::string_view a_text, double a_now)
		{
			const auto action = F4T::Voice::ParseVoiceAction(a_text);
			if (!action) {
				F4T::DebugLogging::Log("Voice", "Dropped a malformed voice action.");
				return;
			}
			if (const auto* setting = std::get_if<F4T::Voice::SetSettingAction>(&*action)) {
				HandleSetting(a_state, *setting, a_now);
			} else if (const auto* capture = std::get_if<F4T::Voice::BeginKeyCaptureAction>(&*action)) {
				a_state.keyCapture.Begin(capture->target, a_now, F4T::Voice::IsVoiceKeyDown);
			} else if (const auto* mute = std::get_if<F4T::Voice::MutePlayerAction>(&*action)) {
				HandleMute(a_state, *mute, a_now);
			} else if (const auto* volume = std::get_if<F4T::Voice::SetPlayerVolumeAction>(&*action)) {
				HandleVolume(a_state, *volume, a_now);
			}
		}

		void DrainActions(RuntimeState& a_state, double a_now)
		{
			std::deque<std::string> actions;
			{
				auto& inbox = GetInbox();
				const std::scoped_lock lock(inbox.actionMutex);
				actions.swap(inbox.actions);
			}
			for (const auto& text : actions) {
				HandleAction(a_state, text, a_now);
			}
		}

		void HandleControlEvent(RuntimeState& a_state, const nlohmann::json& a_packet, double a_now)
		{
			const auto type = a_packet.value("type", std::string{});
			if (type == "voiceTalk") {
				a_state.session.ApplyTalk(a_packet);
			} else if (type == "voiceNotice") {
				a_state.session.ApplyNotice(a_packet, a_now);
			}
		}

		void DrainControlEvents(RuntimeState& a_state, double a_now)
		{
			std::deque<nlohmann::json> events;
			{
				auto& inbox = GetInbox();
				const std::scoped_lock lock(inbox.controlMutex);
				events.swap(inbox.controlEvents);
			}
			for (const auto& packet : events) {
				HandleControlEvent(a_state, packet, a_now);
			}
		}

		RosterSnapshot ReadRoster(bool a_active)
		{
			RosterSnapshot roster;
			if (!a_active) {
				return roster;
			}
			for (const auto& player : F4T::RemotePlayerState::GetRemotePlayerSnapshot()) {
				roster.players.push_back(F4T::Voice::SessionPlayer{
					player.playerId, F4T::SocialNameplate::ResolveDisplayName(player.displayName, player.characterName) });
				roster.remotes.push_back(F4T::Voice::RemoteVoicePlayer{ player.playerId, player.x, player.y, player.z });
			}
			return roster;
		}

		void UpdateRoster(RuntimeState& a_state, const RosterSnapshot& a_roster, double a_now)
		{
			const auto before = a_state.session.Players();
			const auto storedBefore = a_state.settings.players;
			if (!a_state.session.SyncPlayers(a_roster.players, a_state.settings)) {
				return;
			}
			if (a_state.settings.players != storedBefore) a_state.saveDebounce.MarkDirty(a_now);
			PushPlayers(a_state);
			for (const auto speakerId : F4T::Voice::DepartedSpeakers(before, a_state.session.Players())) {
				a_state.receiver->RemoveSpeaker(speakerId);
			}
			for (const auto& row : a_state.session.Players()) {
				ApplySpeakerRow(*a_state.receiver, row);
				RememberMute(row.playerId, row.muted);
			}
		}

		void UpdatePositions(RuntimeState& a_state, bool a_active, const RosterSnapshot& a_roster)
		{
			if (!a_active) {
				return;
			}
			const auto* player = F4T::LocalPlayerState::TryGetLocalPlayer();
			if (!player) {
				return;
			}
			const auto transform = F4T::LocalPlayerState::GetPlayerTransform(*player);
			const F4T::Voice::ListenerPose listener{ transform.x, transform.y, transform.z, transform.angleZ };
			a_state.receiver->SetPositions(listener, F4T::Voice::SpeakerPositionsOf(a_roster.remotes));
		}

		void UpdateDevices(RuntimeState& a_state, bool a_wanted, double a_now)
		{
			if (!a_wanted) {
				if (a_state.transmitterRunning) {
					a_state.transmitter->Stop();
					a_state.transmitterRunning = false;
				}
				if (a_state.receiverRunning) {
					a_state.receiver->Stop();
					a_state.receiverRunning = false;
				}
				return;
			}
			if (!a_state.transmitterRunning && a_now >= a_state.transmitRetryAt) {
				a_state.transmitterRunning = a_state.transmitter->Start();
				if (!a_state.transmitterRunning) {
					a_state.transmitRetryAt = a_now + kDeviceRetrySeconds;
				}
			}
			if (!a_state.receiverOpened) {
				if (a_now >= a_state.receiveRetryAt) {
					a_state.receiverOpened = a_state.receiver->Open();
					if (!a_state.receiverOpened) {
						a_state.receiveRetryAt = a_now + kDeviceRetrySeconds;
					}
				}
			} else if (!a_state.receiverRunning && a_now >= a_state.receiveRetryAt) {
				a_state.receiverRunning = a_state.receiver->Start();
				if (!a_state.receiverRunning) {
					a_state.receiveRetryAt = a_now + kDeviceRetrySeconds;
				}
			}
		}

		void PushLevel(RuntimeState& a_state, double a_levelDbfs, double a_now)
		{
			if (!a_state.transmitterRunning || a_now < a_state.levelAt) {
				return;
			}
			a_state.levelAt = a_now + kLevelIntervalSeconds;
			F4T::VoiceUi::PushEvent(F4T::Voice::LevelEvent(a_levelDbfs).c_str());
		}

		void PollKeyCapture(RuntimeState& a_state, double a_now)
		{
			if (!a_state.keyCapture.Active()) {
				return;
			}
			const auto result = a_state.keyCapture.Poll(a_now, F4T::Voice::IsVoiceKeyDown);
			if (!result) {
				return;
			}
			if (result->captured) {
				F4T::Voice::ApplyKeyCapture(a_state.settings, result->target, result->name, result->vk);
				a_state.saveDebounce.MarkDirty(a_now);
			}
			F4T::VoiceUi::PushEvent(F4T::Voice::KeyCaptureEvent(result->target, result->captured, result->name, result->vk).c_str());
			PushSettings(a_state);
		}

		void ResetForSession(RuntimeState& a_state, F4T::ReplicationRuntime::Generation a_generation)
		{
			a_state.generation = a_generation;
			a_state.transmitter->ResetSession();
			a_state.receiver->ResetSession();
			a_state.session.Reset();
			ForgetMuteMirror();
			PushPlayers(a_state);
			PushSpeakers(a_state, {});
		}

		void WatchDevices(F4T::Voice::Transmitter* a_transmitter, F4T::Voice::Receiver* a_receiver)
		{
			if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
				F4T::DebugLogging::Log("Voice", "COM init failed; device changes will not reopen voice devices.");
				return;
			}
			auto* capture = new F4T::Voice::DeviceWatcher(eCapture, [a_transmitter]() {
				a_transmitter->RequestReopen();
			});
			auto* render = new F4T::Voice::DeviceWatcher(eRender, [a_receiver]() {
				a_receiver->RequestReopen();
			});
			if (!capture->Register() || !render->Register()) {
				F4T::DebugLogging::Log("Voice", "Device watcher registration failed; device changes will not reopen voice devices.");
				return;
			}
			for (;;) {
				std::this_thread::sleep_for(std::chrono::hours{ 24 });
			}
		}

		void Tick()
		{
			auto& state = GetState();
			const double now = Clock();
			const auto view = F4T::GnsVoice::GetControlView();
			const auto generation = F4T::ReplicationRuntime::CurrentGeneration();
			const bool active = generation != F4T::ReplicationRuntime::kNoSession;
			if (generation != state.generation) {
				ResetForSession(state, generation);
			}
			DrainActions(state, now);
			DrainControlEvents(state, now);
			const auto roster = ReadRoster(active);
			UpdateRoster(state, roster, now);
			UpdatePositions(state, active, roster);
			const bool wanted = F4T::Voice::DevicesWanted(active, state.settings, view.serverNoVoice);
			UpdateDevices(state, wanted, now);

			DWORD foregroundProcess = 0;
			GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
			const bool inputAllowed = foregroundProcess == GetCurrentProcessId() && !F4T::UIClient::IsCustomMenuInputCaptured() && !F4T::TextEntryMode::IsChatInputFocused() && !F4T::SessionWorldState::IsMenuOpen(RE::Console::MENU_NAME.data());

			state.transmitter->Configure(F4T::Voice::TransmitConfigOf(state.settings));
			state.receiver->SetMasterVolume(state.settings.voiceChatVolume);
			state.receiver->SetDeafened(state.settings.deafened);
			state.transmitter->SetInputAllowed(inputAllowed);
			state.transmitter->SetCanSend(view.canSend);

			const auto status = state.transmitter->Status();
			const bool outputFailed = state.receiver->OutputFailed() || (wanted && !state.receiverOpened);
			F4T::GnsVoice::SetDesiredControl(F4T::Voice::DesiredControl(
				state.settings,
				state.session.BlockedPlayerIds(),
				status.inputs.codecFailed,
				status.inputs.micFailed,
				outputFailed));

			const auto inputs = F4T::Voice::AggregateStatus(status.inputs, outputFailed, view.serverNoVoice, state.session.Notices(now), state.settings);
			const bool pttHeld = inputAllowed && state.settings.pushToTalkVirtualKey != 0 && F4T::Voice::IsVoiceKeyDown(state.settings.pushToTalkVirtualKey);
			const auto text = F4T::Voice::StatusEvent(active, F4T::Voice::SelectVoiceStatus(inputs), view.pending, F4T::Voice::MicHot(active, state.settings, pttHeld, inputs.transmitting));
			if (text != state.lastStatus) {
				state.lastStatus = text;
				F4T::VoiceUi::PushEvent(text.c_str());
			}

			auto speakers = F4T::Voice::MergeSpeakers(state.session.TalkSpeakers(), state.receiver->Speakers(), state.session.Players());
			if (speakers != state.lastSpeakers) {
				PushSpeakers(state, std::move(speakers));
			}
			PushLevel(state, status.levelDbfs, now);
			PollKeyCapture(state, now);

			if (state.saveDebounce.TakeDue(now) && !F4T::Voice::SaveVoiceSettings(F4T::Voice::DefaultVoiceSettingsPath(), state.settings)) {
				F4T::DebugLogging::Log("Voice", "Failed to save voice settings.");
			}
		}
	}

	void SubmitAction(std::string_view a_actionJson)
	{
		if (a_actionJson.empty() || a_actionJson.size() > 512) return;
		const std::string text{ a_actionJson };
		OnUiAction(text.c_str());
	}

	bool IsPlayerMuted(std::uint32_t a_playerId)
	{
		if (a_playerId == 0) return false;
		const std::scoped_lock lock(g_muteMutex);
		const auto found = g_muteMirror.find(a_playerId);
		return found != g_muteMirror.end() && found->second;
	}

	void Start()
	{
		static std::atomic<bool> started{ false };
		if (started.exchange(true)) {
			return;
		}

		auto& state = GetState();
		state.settings = F4T::Voice::LoadVoiceSettings(F4T::Voice::DefaultVoiceSettingsPath());
		state.transmitter = new F4T::Voice::Transmitter(F4T::Voice::TransmitDeps{
			.send = F4T::GnsVoice::SendVoicePacket,
			.keyDown = F4T::Voice::IsVoiceKeyDown,
			.clock = Clock
		});
		state.receiver = new F4T::Voice::Receiver(Clock);

		F4T::GnsVoice::RegisterVoiceSink([receiver = state.receiver](const F4T::Voice::VoicePacket& a_packet) {
			static_cast<void>(receiver->OnPacket(a_packet, Clock()));
		});
		F4T::GnsVoice::RegisterControlEventHandler(OnControlEvent);
		F4T::VoiceUi::SetActionHandler(OnUiAction);
		std::thread([transmitter = state.transmitter, receiver = state.receiver]() {
			WatchDevices(transmitter, receiver);
		}).detach();

		F4T::VoiceUi::PushEvent(F4T::Voice::SettingsEvent(state.settings).c_str());
		F4T::VoiceUi::PushEvent(F4T::Voice::StatusEvent(false, F4T::Voice::VoiceStatus::idle, false, false).c_str());
		F4T::F4SECapabilities::AddPermanentTask(Tick);
	}
}
