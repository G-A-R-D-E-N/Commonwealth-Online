#include "ServerProtocolImpl.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace CommonwealthOnline::Server
{
			constexpr double kTeamSyncIntervalSeconds = 1.0;

			std::vector<OutboundFrame> ProtocolHost::Impl::PollTeamQuestSync(double a_monotonicSeconds, double a_unixTimeSeconds)
			{
				std::vector<OutboundFrame> frames;
				if (!questsEnabled || a_monotonicSeconds < nextTeamSyncMonotonic) return frames;
				nextTeamSyncMonotonic = a_monotonicSeconds + kTeamSyncIntervalSeconds;
				for (const auto& entry : sessions) {
					if (!entry.second.active) continue;
					AppendFrames(frames, ReconcileTeamQuests(entry.second, a_unixTimeSeconds));
				}
				return frames;
			}

			std::vector<OutboundFrame> ProtocolHost::Impl::ReconcileTeamQuests(const Session& a_session, double a_unixTimeSeconds)
			{
				std::vector<OutboundFrame> frames;
				if (!questsEnabled || !a_session.active || (questMode != F4T::QuestProtocol::Mode::CoopStory && questMode != F4T::QuestProtocol::Mode::FactionWar)) return frames;

				struct Member
				{
					std::uint32_t connectionId{};
					const Session* session{};
					bool adoptable{};
					std::vector<F4T::QuestProtocol::Stage> adopted;
				};
				std::vector<Member> team;
				for (const auto memberId : fireteamService.MembersOfPlayer(a_session.playerId)) {
					for (const auto& entry : sessions) {
						const auto& candidate = entry.second;
						if (!candidate.active || candidate.playerId != memberId || candidate.profileId.empty()) continue;
						if (!SupportsQuestSync(candidate) || !TutorialExitSeen(candidate.profileId)) continue;
						team.push_back(Member{ entry.first, &candidate, !RequiresQuestReady(candidate) || candidate.questReady, {} });
						break;
					}
				}
				if (team.size() < 2) return frames;

				std::optional<F4T::QuestState::Table> snapshot;
				const auto& rows = storyOrder.Rows();
				bool advanced = true;
				for (std::size_t pass = 0; advanced && pass < rows.size(); ++pass) {
					advanced = false;
					for (const auto& row : rows) {
						if (!row.startable) continue;
						F4T::QuestProtocol::Stage position{};
				position.quest = row.quest;
						bool contributed = false;
						for (const auto& member : team) {
							const auto& profile = member.session->profileId;
							if (quests.ResolveSharing(profile, row.quest, questMode, questOverrides) != F4T::QuestState::Sharing::Shared) continue;
							const auto personalProfile = SessionQuestProfileView(*member.session);
							const auto stage = quests.ProfileStage(personalProfile, row.quest);
							if (!stage) continue;
							if (!contributed || *stage > position.stage) {
								position.stage = *stage;
								position.fromPlayerId = member.session->playerId;
							}
							position.completed = position.completed || quests.ProfileCompleted(personalProfile, row.quest);
							contributed = true;
						}
						if (!contributed || IsQuestGated(row.quest, position.stage)) continue;
						for (auto& member : team) {
							const auto& profile = member.session->profileId;
							if (!member.adoptable || quests.ResolveSharing(profile, row.quest, questMode, questOverrides) != F4T::QuestState::Sharing::Shared) continue;
							const auto personalProfile = SessionQuestProfileView(*member.session);
							if (personalProfile.empty()) continue;
							const auto recorded = quests.ProfileStage(personalProfile, row.quest);
							const bool behind = !recorded || *recorded < position.stage || (position.completed && !quests.ProfileCompleted(personalProfile, row.quest));
							if (!behind || StoryReachable(profile, row.quest, position.stage).refusal != StoryRefusal::None) continue;
							if (!snapshot) snapshot.emplace(quests);
							if (!quests.RecordProfileProgress(F4T::QuestState::ReportRequest{ row.quest, position.stage, position.completed, F4T::QuestState::Sharing::Shared, position.fromPlayerId, {}, std::string{ personalProfile } })) continue;
							member.adopted.push_back(position);
							advanced = true;
						}
					}
				}

				std::uint64_t changedMembers = 0;
				for (const auto& member : team) {
					if (!member.adopted.empty()) ++changedMembers;
				}
				if (changedMembers == 0) return frames;
				if (!SaveQuests()) {
					quests = *snapshot;
					return frames;
				}
				stats.questTeamResynced += changedMembers;
				for (const auto& member : team) {
					if (member.adopted.empty()) continue;
					const bool completed = std::any_of(member.adopted.begin(), member.adopted.end(), [](const F4T::QuestProtocol::Stage& a_stage) { return a_stage.completed; });
					if (completed) AppendFrames(frames, QuestSyncFrames(member.connectionId, *member.session, a_unixTimeSeconds));
					for (const auto& stage : member.adopted) {
						if (!stage.completed) frames.push_back(ReliableFrame(member.connectionId, F4T::QuestProtocol::MakeStage(stage, a_unixTimeSeconds)));
					}
				}
				return frames;
			}
}
