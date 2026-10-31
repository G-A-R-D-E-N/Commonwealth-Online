#include "ServerProtocolImpl.h"

#include <algorithm>

namespace CommonwealthOnline::Server
{

			bool ProtocolHost::Impl::EnableQuestSync(
				PersistenceGate* a_persistence,
				F4T::QuestProtocol::Mode a_mode,
				std::vector<F4T::QuestProtocol::OverrideEntry> a_overrides)
			{
				if (questsEnabled || hasEverActivatedSession) return false;
				auto mode = a_mode;
				if (a_persistence != nullptr) {
					const auto stored = a_persistence->GetQuests();
					if (const auto loaded = F4T::QuestState::Table::FromJson(stored)) {
						quests = *loaded;
						if (const auto node = stored.find("mode"); node != stored.end() && node->is_string()) {
							if (const auto parsed = F4T::QuestProtocol::ParseMode(node->get_ref<const std::string&>())) {
								mode = *parsed;
							}
						}
					if (const auto node = stored.find("tutorialExit"); node != stored.end() && node->is_object()) {
						for (const auto& [profile, stage] : node->items()) {
							if (stage.is_number_unsigned()) {
								const auto value = stage.get<std::uint64_t>();
								tutorialExitStages[profile] = value > 0xFFFF ? 0xFFFF : static_cast<std::uint16_t>(value);
							}
						}
					}
					} else {
						std::cerr << "Stored quest state is unreadable; starting with an empty quest table and keeping the payload under quests_backup.\n";
						try {
							a_persistence->SaveQuestsBackup(stored);
						} catch (const std::exception& error) {
							std::cerr << "Corrupt quest state could not be backed up: " << error.what() << '\n';
						}
					}
				}
				questPersistence = a_persistence;
				questMode = mode;
				questOverrides = std::move(a_overrides);
				storyOrder = F4T::QuestStory::Order::Default();
				questsEnabled = true;
				return true;
			}

			bool ProtocolHost::Impl::QuestSyncEnabled() const noexcept
			{
				return questsEnabled;
			}

			F4T::QuestProtocol::Mode ProtocolHost::Impl::QuestMode() const noexcept
			{
				return questMode;
			}

			Json ProtocolHost::Impl::QuestStatus() const
			{
				auto status = quests.ToJson();
				status["enabled"] = questsEnabled;
				status["mode"] = std::string{ F4T::QuestProtocol::ModeName(questMode) };
				Json overrides = Json::array();
				for (const auto& entry : questOverrides) {
					overrides.push_back(Json{
						{ "quest", F4T::QuestProtocol::FormatFormKey(entry.quest) },
						{ "policy", std::string{ F4T::QuestProtocol::OverrideName(entry.policy) } }
					});
				}
				status["overrides"] = std::move(overrides);
				return status;
			}

			bool ProtocolHost::Impl::IsQuestGated(const F4T::QuestProtocol::FormKey& a_quest, std::uint16_t a_stage) const
			{
				return quests.IsGated(a_quest, a_stage);
			}

			bool ProtocolHost::Impl::IsQuestSolo(std::string_view a_profileId, const F4T::QuestProtocol::FormKey& a_quest) const
			{
				return quests.IsSolo(a_profileId, a_quest);
			}

			void ProtocolHost::Impl::SetQuestStoryOrder(const F4T::QuestStory::Order& a_order)
			{
				storyOrder = a_order;
			}

			bool ProtocolHost::Impl::StoryContentAllowed(F4T::QuestProtocol::Mode a_mode) noexcept
			{
				switch (a_mode) {
				case F4T::QuestProtocol::Mode::CoopStory:
				case F4T::QuestProtocol::Mode::FactionWar:
				case F4T::QuestProtocol::Mode::InstancedStory:
					return true;
				case F4T::QuestProtocol::Mode::FreeRoam:
					return false;
				}
				return false;
			}

			ProtocolHost::Impl::StoryCheck ProtocolHost::Impl::StoryReachable(
				std::string_view a_profileId,
				const F4T::QuestProtocol::FormKey& a_quest,
				std::uint16_t a_stage) const
			{
				if (!StoryContentAllowed(questMode)) return StoryCheck{ StoryRefusal::NotStartable, std::nullopt };
				if (const auto held = StoryStageFor(a_profileId, a_quest)) {
					if (a_stage < *held) return StoryCheck{ StoryRefusal::StageRegression, std::nullopt };
					return StoryCheck{ StoryRefusal::None, std::nullopt };
				}
				const auto* row = storyOrder.Find(a_quest);
				if (row == nullptr) {
					const auto sharing = quests.ResolveSharing(a_profileId, a_quest, questMode, questOverrides);
					std::string track;
					if (sharing == F4T::QuestState::Sharing::Party) {
						track = factionService.FactionIdOf(std::string{ a_profileId });
					}
					if (const auto shared = quests.FindStage(a_quest, track)) {
						if (a_stage < shared->stage) return StoryCheck{ StoryRefusal::StageRegression, std::nullopt };
						return StoryCheck{ StoryRefusal::None, std::nullopt };
					}
					return StoryCheck{ StoryRefusal::UnknownQuest, std::nullopt };
				}
				if (!row->startable) return StoryCheck{ StoryRefusal::NotStartable, std::nullopt };
				for (const auto& requirement : row->prerequisites) {
					bool satisfied = false;
					if (requirement.completed) {
						satisfied = quests.ProfileCompleted(a_profileId, requirement.quest);
					} else if (const auto needed = StoryStageFor(a_profileId, requirement.quest)) {
						satisfied = *needed >= requirement.stage.value_or(0);
					}
					if (!satisfied) return StoryCheck{ StoryRefusal::PrerequisiteMissing, requirement.quest };
				}
				return StoryCheck{ StoryRefusal::None, std::nullopt };
			}

			bool ProtocolHost::Impl::StoryStageVisible(
				std::string_view a_profileId,
				const F4T::QuestProtocol::FormKey& a_quest,
				std::uint16_t a_stage) const
			{
				const auto check = StoryReachable(a_profileId, a_quest, a_stage);
				return check.refusal == StoryRefusal::None || check.refusal == StoryRefusal::StageRegression;
			}

			std::vector<OutboundFrame> ProtocolHost::Impl::SetQuestMode(F4T::QuestProtocol::Mode a_mode, double a_unixTimeSeconds)
			{
				if (!questsEnabled || a_mode == questMode) return {};
				const auto snapshot = quests;
				const auto previousMode = questMode;
				questMode = a_mode;
				quests.BumpVersion();
				if (!SaveQuests()) {
					quests = snapshot;
					questMode = previousMode;
					return {};
				}
				return QuestPolicyFrames(a_unixTimeSeconds);
			}

			std::vector<OutboundFrame> ProtocolHost::Impl::SetQuestGate(
				const F4T::QuestProtocol::FormKey& a_quest,
				std::uint16_t a_stage,
				bool a_release,
				double a_unixTimeSeconds)
			{
				if (!questsEnabled) return {};
				const auto snapshot = quests;
				std::vector<F4T::QuestState::HeldEntry> promoted;
				if (a_release) {
					if (!snapshot.IsGated(a_quest, a_stage)) return {};
					promoted = quests.ReleaseGate(a_quest, a_stage);
				} else if (!quests.AddGate(a_quest, a_stage)) {
					return {};
				}
				quests.BumpVersion();
				if (!SaveQuests()) {
					quests = snapshot;
					return {};
				}
				std::vector<OutboundFrame> frames;
				for (const auto& entry : promoted) {
					stats.questStagesRelayed++;
					AppendFrames(frames, BroadcastQuestStage(
						F4T::QuestProtocol::Stage{ entry.quest, entry.stage, entry.fromPlayerId, entry.completed },
						entry.party,
						QuestFactionOfPlayer(entry.fromPlayerId),
						a_unixTimeSeconds));
				}
				AppendFrames(frames, QuestPolicyFrames(a_unixTimeSeconds));
				return frames;
			}

			std::vector<OutboundFrame> ProtocolHost::Impl::SetQuestSolo(
				std::string_view a_profileId,
				const F4T::QuestProtocol::FormKey& a_quest,
				bool a_solo,
				double a_unixTimeSeconds,
				std::optional<std::uint32_t> a_requesterConnection,
				bool a_awaitCommit)
			{
				if (!questsEnabled) return {};
				const auto snapshot = quests;
				const auto changed = a_solo
					? quests.SetScope(a_profileId, a_quest, F4T::QuestProtocol::Scope::Solo)
					: quests.SetSolo(a_profileId, a_quest, false);
				if (!changed) return {};
				return CommitQuestScope(snapshot, a_profileId, a_unixTimeSeconds, a_requesterConnection, a_awaitCommit);
			}

			std::vector<OutboundFrame> ProtocolHost::Impl::SetQuestScope(
				std::string_view a_profileId,
				const F4T::QuestProtocol::FormKey& a_quest,
				F4T::QuestProtocol::Scope a_scope,
				double a_unixTimeSeconds,
				std::optional<std::uint32_t> a_requesterConnection,
				bool a_awaitCommit)
			{
				if (!questsEnabled) return {};
				const auto snapshot = quests;
				if (!quests.SetScope(a_profileId, a_quest, a_scope)) return {};
				return CommitQuestScope(snapshot, a_profileId, a_unixTimeSeconds, a_requesterConnection, a_awaitCommit);
			}

			bool ProtocolHost::Impl::SaveQuests(bool a_awaitCommit)
			{
				if (questPersistence == nullptr) return true;
				auto blob = quests.ToJson();
				blob["mode"] = std::string{ F4T::QuestProtocol::ModeName(questMode) };
				blob["tutorialExit"] = tutorialExitStages;
				try {
					if (a_awaitCommit) {
						questPersistence->SaveQuests(blob);
					} else {
						questPersistence->QueueQuests(blob);
					}
					return true;
				} catch (const std::exception& error) {
					std::cerr << "Quest persistence unavailable: " << error.what() << '\n';
					return false;
				}
			}

			bool ProtocolHost::Impl::SupportsQuestSync(const Session& a_session) noexcept
			{
				return a_session.compatibilityCapabilities.Has(F4T::QuestProtocol::kSyncCapability);
			}

			bool ProtocolHost::Impl::RequiresQuestReady(const Session& a_session) noexcept
			{
				return SupportsQuestSync(a_session) &&
					a_session.compatibilityCapabilities.Has(F4T::QuestProtocol::kReadyCapability);
			}

			bool ProtocolHost::Impl::NoteTutorialProgress(std::string_view a_profileId, std::uint16_t a_stage)
			{
				const auto key = std::string{ a_profileId };
				const auto found = tutorialExitStages.find(key);
				const auto previous = found == tutorialExitStages.end() ? std::uint16_t{ 0 } : found->second;
				if (a_stage <= previous) return true;
				tutorialExitStages[key] = a_stage;
				if (SaveQuests(false)) return true;
				if (found == tutorialExitStages.end()) {
					tutorialExitStages.erase(key);
				} else {
					found->second = previous;
				}
				return false;
			}

			bool ProtocolHost::Impl::TutorialExitSeen(std::string_view a_profileId) const
			{
				const auto found = tutorialExitStages.find(std::string{ a_profileId });
				return found != tutorialExitStages.end() && found->second >= F4T::QuestProtocol::kTutorialExitStage;
			}

			std::optional<std::uint16_t> ProtocolHost::Impl::StoryStageFor(
				std::string_view a_profileId,
				const F4T::QuestProtocol::FormKey& a_quest) const
			{
				if (const auto recorded = quests.ProfileStage(a_profileId, a_quest)) return recorded;
				if (a_quest.plugin != F4T::QuestProtocol::kTutorialQuestPlugin ||
					a_quest.localId != F4T::QuestProtocol::kTutorialQuestLocalId) {
					return std::nullopt;
				}
				if (!TutorialExitSeen(a_profileId)) return std::nullopt;
				return tutorialExitStages.at(std::string{ a_profileId });
			}

			std::optional<std::uint32_t> ProtocolHost::Impl::QuestConnectionForProfile(std::string_view a_profileId) const
			{
				for (const auto& [connectionId, recipient] : sessions) {
					if (recipient.active && recipient.profileId == a_profileId) return connectionId;
				}
				return std::nullopt;
			}

			std::vector<OutboundFrame> ProtocolHost::Impl::CommitQuestScope(
				const F4T::QuestState::Table& a_snapshot,
				std::string_view a_profileId,
				double a_unixTimeSeconds,
				std::optional<std::uint32_t> a_requesterConnection,
				bool a_awaitCommit)
			{
				quests.BumpVersion();
				if (!SaveQuests(a_awaitCommit)) {
					quests = a_snapshot;
					return {};
				}
				std::optional<std::uint32_t> connectionId;
				if (a_requesterConnection) {
					const auto requester = sessions.find(*a_requesterConnection);
					if (requester != sessions.end() && requester->second.active && requester->second.profileId == a_profileId) {
						connectionId = *a_requesterConnection;
					}
				}
				if (!connectionId) connectionId = QuestConnectionForProfile(a_profileId);
				if (!connectionId) return {};
				const auto target = sessions.find(*connectionId);
				if (target == sessions.end() || !SupportsQuestSync(target->second)) return {};
				return { ReliableFrame(*connectionId, QuestPolicyPacket(target->second, a_unixTimeSeconds)) };
			}

			bool ProtocolHost::Impl::QuestAudience(
				const Session& a_recipient,
				const F4T::QuestProtocol::FormKey& a_quest,
				std::string_view a_track,
				std::string_view a_sourceParty) const
			{
				const auto party = factionService.FactionIdOf(a_recipient.profileId);
				const auto sharing = quests.ResolveSharing(a_recipient.profileId, a_quest, questMode, questOverrides);
				return F4T::QuestState::Table::Receives(sharing, party, a_track, a_sourceParty);
			}

			std::string ProtocolHost::Impl::QuestFactionOfPlayer(std::uint32_t a_playerId) const
			{
				for (const auto& entry : sessions) {
					if (entry.second.active && entry.second.playerId == a_playerId) return factionService.FactionIdOf(entry.second.profileId);
				}
				return {};
			}

			std::vector<OutboundFrame> ProtocolHost::Impl::BroadcastQuestStage(
				const F4T::QuestProtocol::Stage& a_stage,
				std::string_view a_track,
				std::string_view a_sourceParty,
				double a_unixTimeSeconds) const
			{
				const auto packet = F4T::QuestProtocol::MakeStage(a_stage, a_unixTimeSeconds);
				std::vector<OutboundFrame> frames;
				for (const auto& [connectionId, recipient] : sessions) {
					if (!recipient.active || !SupportsQuestSync(recipient)) continue;
					if (!QuestAudience(recipient, a_stage.quest, a_track, a_sourceParty)) continue;
					if (!StoryStageVisible(recipient.profileId, a_stage.quest, a_stage.stage)) continue;
					frames.push_back(ReliableFrame(connectionId, packet));
				}
				return frames;
			}

			std::vector<OutboundFrame> ProtocolHost::Impl::QuestSyncFrames(std::uint32_t a_connectionId, const Session& a_target, double a_unixTimeSeconds) const
			{
				std::vector<OutboundFrame> frames;
				F4T::QuestProtocol::Sync chunk;
				std::size_t used = 0;
				const auto flush = [&] {
					if (chunk.stages.empty() && chunk.completed.empty()) return;
					frames.push_back(ReliableFrame(a_connectionId, F4T::QuestProtocol::MakeSync(chunk, a_unixTimeSeconds)));
					chunk = F4T::QuestProtocol::Sync{};
					used = 0;
				};
				std::vector<F4T::QuestProtocol::StageRef> stages;
				for (const auto& entry : quests.Stages()) {
					if (!QuestAudience(a_target, entry.quest, entry.party, {})) continue;
					if (!StoryStageVisible(a_target.profileId, entry.quest, entry.stage)) continue;
					stages.push_back(F4T::QuestProtocol::StageRef{ entry.quest, entry.stage });
				}
				for (const auto& entry : quests.ProfileQuests(a_target.profileId)) {
					if (!StoryStageVisible(a_target.profileId, entry.quest, entry.stage)) continue;
					const auto existing = std::find_if(stages.begin(), stages.end(), [&entry](const auto& a_stage) {
						return a_stage.quest == entry.quest;
					});
					if (existing == stages.end()) {
						stages.push_back(F4T::QuestProtocol::StageRef{ entry.quest, entry.stage });
					} else if (existing->stage < entry.stage) {
						existing->stage = entry.stage;
					}
				}
				for (const auto& entry : stages) {
					const F4T::QuestProtocol::Sync probe{ { entry }, {} };
					const auto cost = F4T::QuestProtocol::MakeSync(probe, a_unixTimeSeconds).dump().size();
					if (!chunk.stages.empty() && used + cost > F4T::QuestProtocol::kMaximumSyncFrameBytes) flush();
					chunk.stages.push_back(entry);
					used += cost;
				}
				std::vector<F4T::QuestProtocol::FormKey> completed;
				for (const auto& tracked : quests.CompletedTracks()) {
					if (!QuestAudience(a_target, tracked.quest, tracked.party, {})) continue;
					if (!StoryStageVisible(a_target.profileId, tracked.quest, F4T::QuestProtocol::kMaximumStage)) continue;
					if (std::find(completed.begin(), completed.end(), tracked.quest) != completed.end()) continue;
					completed.push_back(tracked.quest);
				}
				for (const auto& entry : quests.ProfileQuests(a_target.profileId)) {
					if (!entry.completed) continue;
					if (!StoryStageVisible(a_target.profileId, entry.quest, F4T::QuestProtocol::kMaximumStage)) continue;
					if (std::find(completed.begin(), completed.end(), entry.quest) != completed.end()) continue;
					completed.push_back(entry.quest);
				}
				for (const auto& quest : completed) {
					const F4T::QuestProtocol::Sync probe{ {}, { quest } };
					const auto cost = F4T::QuestProtocol::MakeSync(probe, a_unixTimeSeconds).dump().size();
					if ((!chunk.stages.empty() || !chunk.completed.empty()) && used + cost > F4T::QuestProtocol::kMaximumSyncFrameBytes) {
						flush();
					}
					chunk.completed.push_back(quest);
					used += cost;
				}
				flush();
				return frames;
			}

			F4T::QuestProtocol::Policy ProtocolHost::Impl::QuestPolicyFor(const Session& a_target) const
			{
				return F4T::QuestProtocol::Policy{
					questMode,
					quests.Version(),
					a_target.profileId.empty()
						? std::vector<F4T::QuestProtocol::FormKey>{}
						: quests.SoloQuests(a_target.profileId),
					quests.Gates(),
					a_target.profileId.empty()
						? std::vector<F4T::QuestProtocol::FormKey>{}
						: quests.PartyQuests(a_target.profileId)
				};
			}

			Json ProtocolHost::Impl::QuestPolicyPacket(const Session& a_target, double a_unixTimeSeconds) const
			{
				return F4T::QuestProtocol::MakePolicy(QuestPolicyFor(a_target), a_unixTimeSeconds);
			}

			std::vector<OutboundFrame> ProtocolHost::Impl::QuestPolicyFrames(double a_unixTimeSeconds) const
			{
				std::vector<OutboundFrame> frames;
				for (const auto& [connectionId, recipient] : sessions) {
					if (!recipient.active || !SupportsQuestSync(recipient)) continue;
					frames.push_back(ReliableFrame(connectionId, QuestPolicyPacket(recipient, a_unixTimeSeconds)));
				}
				return frames;
			}

			std::vector<OutboundFrame> ProtocolHost::Impl::QuestNotReadyFrame(
				std::uint32_t a_connectionId,
				const Json& a_packet,
				std::string_view a_reason,
				double a_unixTimeSeconds) const
			{
				std::string quest;
				if (const auto node = a_packet.find("quest"); node != a_packet.end() && node->is_string()) {
					quest = node->get<std::string>();
				}
				return { ReliableFrame(
					a_connectionId,
					F4T::QuestProtocol::MakeNotReady(a_reason, quest, a_unixTimeSeconds)) };
			}

			std::vector<OutboundFrame> ProtocolHost::Impl::QuestNotReachableFrame(
				std::uint32_t a_connectionId,
				const F4T::QuestProtocol::FormKey& a_quest,
				const StoryCheck& a_check,
				double a_unixTimeSeconds) const
			{
				std::string_view reason = F4T::QuestProtocol::kNotReachableUnknown;
				switch (a_check.refusal) {
				case StoryRefusal::PrerequisiteMissing:
					reason = F4T::QuestProtocol::kNotReachablePrerequisite;
					break;
				case StoryRefusal::NotStartable:
					reason = F4T::QuestProtocol::kNotReachableNotStartable;
					break;
				case StoryRefusal::StageRegression:
					reason = F4T::QuestProtocol::kNotReachableRegression;
					break;
				case StoryRefusal::UnknownQuest:
				case StoryRefusal::None:
					break;
				}
				std::string missing;
				std::string previousLabel;
				if (a_check.missing) {
					missing = F4T::QuestProtocol::FormatFormKey(*a_check.missing);
					if (const auto* row = storyOrder.Find(*a_check.missing)) {
						previousLabel = row->label;
					} else {
						previousLabel = missing;
					}
				}
				return { ReliableFrame(
					a_connectionId,
					F4T::QuestProtocol::MakeNotReachable(
						reason,
						F4T::QuestProtocol::FormatFormKey(a_quest),
						missing,
						previousLabel,
						a_unixTimeSeconds)) };
			}

			std::vector<OutboundFrame> ProtocolHost::Impl::HandleQuestReport(
				std::uint32_t a_connectionId,
				Session& a_session,
				const Json& a_packet,
				double a_monotonicSeconds,
				double a_unixTimeSeconds)
			{
				stats.questReportsReceived++;
				if (!F4T::PacketRateLimit::TakeToken(
						a_session.questReports,
						a_monotonicSeconds,
						kQuestReportRefillPerSecond,
						kQuestReportBurst)) {
					Reject();
					stats.questReportsRejected++;
					return {};
				}
				const auto report = F4T::QuestProtocol::NormalizeReport(a_packet);
				if (!report) {
					Reject();
					stats.questReportsRejected++;
					return {};
				}
				const bool tutorialReport = report->quest.plugin == F4T::QuestProtocol::kTutorialQuestPlugin &&
					report->quest.localId == F4T::QuestProtocol::kTutorialQuestLocalId;
				if (tutorialReport) {
					if (!NoteTutorialProgress(a_session.profileId, report->stage)) return {};
					if (!a_session.questReady && RequiresQuestReady(a_session)) return {};
				}
				if (!a_session.questReady && RequiresQuestReady(a_session)) {
					Reject();
					stats.questReportsRejected++;
					return QuestNotReadyFrame(a_connectionId, a_packet, F4T::QuestProtocol::kNotReadyReason, a_unixTimeSeconds);
				}
				const auto party = factionService.FactionIdOf(a_session.profileId);
				// ordering: Override::Blocked runs before StoryReachable.
				const auto sharing = quests.ResolveSharing(a_session.profileId, report->quest, questMode, questOverrides);
				if (sharing == F4T::QuestState::Sharing::Blocked) {
					(void)quests.Report(F4T::QuestState::ReportRequest{
						report->quest,
						report->stage,
						report->completed,
						sharing,
						a_session.playerId,
						party,
						a_session.profileId
					});
					stats.questReportsRejected++;
					return {};
				}
				const auto story = StoryReachable(a_session.profileId, report->quest, report->stage);
				if (story.refusal != StoryRefusal::None) {
					Reject();
					stats.questReportsRejected++;
					stats.questStoryRefused++;
					std::cerr << "Quest story guard refused a write for profile " << a_session.profileId << ".\n";
					auto frames = QuestNotReachableFrame(a_connectionId, report->quest, story, a_unixTimeSeconds);
					AppendFrames(frames, ExistingQuestFrames(a_connectionId, a_session, a_unixTimeSeconds));
					return frames;
				}
				const auto snapshot = quests;
				const auto result = quests.Report(F4T::QuestState::ReportRequest{
					report->quest,
					report->stage,
					report->completed,
					sharing,
					a_session.playerId,
					party,
					a_session.profileId
				});
				using F4T::QuestState::Outcome;
				if (result.outcome == Outcome::Held) {
					stats.questStagesHeld++;
					if (!SaveQuests(false)) {
						quests = snapshot;
						stats.questReportsRejected++;
					}
					return {};
				}
				if (result.outcome != Outcome::Applied && result.outcome != Outcome::CompletedOnly && result.outcome != Outcome::Corrected) {
					stats.questReportsRejected++;
					if (result.profileRecorded && !SaveQuests(false)) quests = snapshot;
					return {};
				}
				if (result.outcome == Outcome::CompletedOnly && !result.completed) {
					if (result.profileRecorded && !SaveQuests(false)) {
						quests = snapshot;
						stats.questReportsRejected++;
						return {};
					}
					const auto entry = *result.relayed;
					return { ReliableFrame(
						a_connectionId,
						F4T::QuestProtocol::MakeStage(
							F4T::QuestProtocol::Stage{ entry.quest, entry.stage, a_session.playerId, false },
							a_unixTimeSeconds)) };
				}
				if (!SaveQuests(false)) {
					quests = snapshot;
					stats.questReportsRejected++;
					return {};
				}
				stats.questStagesRelayed++;
				const auto entry = *result.relayed;
				auto frames = BroadcastQuestStage(
					F4T::QuestProtocol::Stage{ entry.quest, entry.stage, a_session.playerId, result.completed },
					entry.party,
					party,
					a_unixTimeSeconds);
				if (result.completed) {
					AppendFrames(frames, QuestSyncFrames(a_connectionId, a_session, a_unixTimeSeconds));
				}
				return frames;
			}

			std::vector<OutboundFrame> ProtocolHost::Impl::HandleQuestScope(
				std::uint32_t a_connectionId,
				Session& a_session,
				const Json& a_packet,
				double a_monotonicSeconds,
				double a_unixTimeSeconds)
			{
				stats.questScopesReceived++;
				if (!F4T::PacketRateLimit::TakeToken(
						a_session.questReports,
						a_monotonicSeconds,
						kQuestReportRefillPerSecond,
						kQuestReportBurst)) {
					Reject();
					stats.questScopesRejected++;
					return {};
				}
				const auto policyReply = [&]() {
					return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, QuestPolicyPacket(a_session, a_unixTimeSeconds)) };
				};
				if (!a_session.questReady && RequiresQuestReady(a_session)) {
					Reject();
					stats.questScopesRejected++;
					return QuestNotReadyFrame(a_connectionId, a_packet, F4T::QuestProtocol::kNotReadyReason, a_unixTimeSeconds);
				}
				const auto request = F4T::QuestProtocol::NormalizeScope(a_packet);
				if (!request) {
					Reject();
					stats.questScopesRejected++;
					return policyReply();
				}
				const auto pinned = std::find_if(questOverrides.begin(), questOverrides.end(), [&request](const auto& a_entry) {
					return a_entry.quest == request->quest;
				});
				if (pinned != questOverrides.end()) {
					stats.questScopesRejected++;
					return policyReply();
				}
				const auto versionBefore = quests.Version();
				auto frames = SetQuestScope(
					a_session.profileId, request->quest, request->scope, a_unixTimeSeconds, a_connectionId, false);
				if (quests.Version() != versionBefore) stats.questScopesApplied++;
				if (frames.empty()) return policyReply();
				return frames;
			}

			std::optional<std::vector<OutboundFrame>> ProtocolHost::Impl::DispatchQuestPacket(
				const std::string& a_type,
				std::uint32_t a_connectionId,
				Session& a_session,
				const Json& a_packet,
				double a_monotonicSeconds,
				double a_unixTimeSeconds)
			{
				if (!questsEnabled) return std::nullopt;
				if (a_type == "questReady") {
					if (SupportsQuestSync(a_session)) {
						if (!RequiresQuestReady(a_session) || TutorialExitSeen(a_session.profileId)) {
							a_session.questReady = true;
						} else {
							Reject();
							stats.questReadyRejected++;
							return std::vector<OutboundFrame>{ QuestNotReadyFrame(
								a_connectionId, a_packet, F4T::QuestProtocol::kNotReadyReason, a_unixTimeSeconds) };
						}
					}
					return std::vector<OutboundFrame>{};
				}
				const bool report = a_type == "questReport";
				if (!report && a_type != "questScope") return std::nullopt;
				if (!SupportsQuestSync(a_session)) return std::vector<OutboundFrame>{};
				auto frames = !report
					? HandleQuestScope(a_connectionId, a_session, a_packet, a_monotonicSeconds, a_unixTimeSeconds)
					: HandleQuestReport(a_connectionId, a_session, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
				if (!a_session.questReady && !RequiresQuestReady(a_session) && !a_session.questUpdateNoticeSent) {
					// One visible notice that the mod must be updated; the writes keep the previous behaviour.
					a_session.questUpdateNoticeSent = true;
					AppendFrames(
						frames,
						QuestNotReadyFrame(a_connectionId, a_packet, F4T::QuestProtocol::kUpdateRequiredReason, a_unixTimeSeconds));
				}
				return frames;
			}

			std::vector<OutboundFrame> ProtocolHost::Impl::ExistingQuestFrames(
				std::uint32_t a_connectionId,
				const Session& a_target,
				double a_unixTimeSeconds)
			{
				if (!questsEnabled || !SupportsQuestSync(a_target)) return {};
				if (quests.IsEmpty() && questMode == F4T::QuestProtocol::Mode::CoopStory) return {};
				if (!quests.Stages().empty() || !quests.CompletedTracks().empty()) stats.questSyncReplays++;
				auto frames = QuestSyncFrames(a_connectionId, a_target, a_unixTimeSeconds);
				frames.push_back(ReliableFrame(a_connectionId, QuestPolicyPacket(a_target, a_unixTimeSeconds)));
				return frames;
			}
}
