#include "ServerProtocolImpl.h"

#include "F4TFormIdentity.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace CommonwealthOnline::Server
{
			constexpr std::size_t kMaximumPendingKillAssists = 8;
			constexpr double kPendingKillAssistSeconds = 5.0;

			[[nodiscard]] static std::string CreditClaimKey(const F4T::QuestProtocol::Assist& a_assist)
			{
				return F4T::QuestProtocol::FormatFormKey(a_assist.quest) + "#" + std::to_string(a_assist.objectiveId);
			}

			[[nodiscard]] static std::optional<F4T::QuestProtocol::FormKey> ResolveSource(
				const F4T::LoadOrder::PluginList& a_list,
				const std::string& a_sourceFormId)
			{
				if (a_sourceFormId.size() != 8 || a_sourceFormId == "00000000") return std::nullopt;
				std::uint32_t packed{};
				const char* last = a_sourceFormId.data() + a_sourceFormId.size();
				const auto parsed = std::from_chars(a_sourceFormId.data(), last, packed, 16);
				if (parsed.ec != std::errc{} || parsed.ptr != last) return std::nullopt;
				const auto split = F4T::FormIdentity::SplitFormId(packed);
				if (split.kind == F4T::FormIdentity::Kind::Runtime) return std::nullopt;
				const auto& entries = split.kind == F4T::FormIdentity::Kind::Light ? a_list.light : a_list.full;
				for (const auto& entry : entries) {
					if (entry.index != split.fileIndex) continue;
					std::string plugin = entry.name;
					std::ranges::transform(plugin, plugin.begin(), [](unsigned char a_char) { return static_cast<char>(std::tolower(a_char)); });
					return F4T::QuestProtocol::FormKey{ plugin, split.localId };
				}
				return std::nullopt;
			}

			[[nodiscard]] static bool NonKillAssistAccepted(
				SessionTypes::Session& a_session,
				const F4T::QuestProtocol::Assist& a_assist,
				std::size_t a_teamSize,
				bool a_questHeldAndOpen,
				double a_monotonicSeconds)
			{
				if (a_teamSize < 2 || !a_questHeldAndOpen || a_assist.target.plugin.empty()) return false;
				if (!a_session.lastMovement || !a_session.lastMovementMonotonic) return false;
				const double age = a_monotonicSeconds - *a_session.lastMovementMonotonic;
				if (age < 0.0 || age > SessionTypes::kNonKillAssistPositionAgeSeconds) return false;

				auto& state = a_session.nonKillAssists;
				const double windowStart = a_monotonicSeconds - SessionTypes::kNonKillAssistWindowSeconds;
				const double claimStart = a_monotonicSeconds - SessionTypes::kNonKillClaimCooldownSeconds;
				std::erase_if(state.recent, [windowStart](double a_time) { return a_time <= windowStart; });
				std::erase_if(state.perQuest, [windowStart](auto& a_entry) {
					std::erase_if(a_entry.second, [windowStart](double a_time) { return a_time <= windowStart; });
					return a_entry.second.empty();
				});
				std::erase_if(state.claims, [claimStart](const auto& a_entry) { return a_entry.second <= claimStart; });

				const auto questKey = F4T::QuestProtocol::FormatFormKey(a_assist.quest);
				const auto claimKey = CreditClaimKey(a_assist) + "#" + std::string{ F4T::QuestProtocol::AssistKindName(a_assist.kind) };
				const auto perQuest = state.perQuest.find(questKey);
				if (perQuest != state.perQuest.end()) {
					if (perQuest->second.size() >= SessionTypes::kMaximumNonKillAssistsPerQuestWindow) return false;
				} else if (state.perQuest.size() >= SessionTypes::kMaximumNonKillQuestKeys) {
					return false;
				}
				if (state.recent.size() >= SessionTypes::kMaximumNonKillAssistsPerWindow) return false;
				if (state.claims.contains(claimKey) || state.claims.size() >= SessionTypes::kMaximumNonKillClaims) return false;

				state.recent.push_back(a_monotonicSeconds);
				state.perQuest[questKey].push_back(a_monotonicSeconds);
				state.claims[claimKey] = a_monotonicSeconds;
				return true;
			}

			SessionTypes::AcceptedHitRecord* ProtocolHost::Impl::AssistKillMatches(
				Session& a_session,
				const F4T::QuestProtocol::Assist& a_assist,
				double a_monotonicSeconds)
			{
				auto* record = FindAcceptedHit(a_session, a_assist.attackSequence, a_assist.sequence, a_monotonicSeconds);
				if (record == nullptr || !record->npc || record->creditKeys.contains(CreditClaimKey(a_assist))) return nullptr;
				if (!a_session.pluginListPacket) return nullptr;
				const auto data = ReadString(*a_session.pluginListPacket, "data");
				const auto list = data ? F4T::LoadOrder::DecodePluginList(*data) : std::nullopt;
				if (!list) return nullptr;
				const auto source = ResolveSource(*list, record->sourceFormId);
				if (!source || source->plugin != a_assist.target.plugin || source->localId != a_assist.target.localId) return nullptr;
				return record;
			}

			bool ProtocolHost::Impl::AssistTargetDead(const SessionTypes::AcceptedHitRecord& a_hit, double a_monotonicSeconds)
			{
				const Scope scope{ a_hit.cellId, a_hit.worldspaceId };
				const auto snapshot = lastNpcStateByScope.find(scope);
				if (snapshot == lastNpcStateByScope.end() || ReadUInt32(snapshot->second, "authorityEpoch").value_or(0) != a_hit.authorityEpoch) return false;
				const auto* npc = FindSnapshotNpc(scope, a_hit.targetId, a_hit.sourceFormId, a_monotonicSeconds);
				if (npc == nullptr) return false;
				const auto health = ReadDouble(*npc, "health");
				return npc->value("isDead", false) || (health && !(*health > 0.0));
			}

			std::vector<OutboundFrame> ProtocolHost::Impl::MintAssistCredits(
				Session& a_session,
				const F4T::QuestProtocol::Assist& a_assist,
				SessionTypes::AcceptedHitRecord* a_hit,
				double a_monotonicSeconds,
				double a_unixTimeSeconds)
			{
				const auto* assist = &a_assist;
				const bool kill = a_hit != nullptr;
				auto* hit = a_hit;
				std::vector<std::uint32_t> candidates{ a_session.playerId };
				for (const auto memberId : fireteamService.MembersOfPlayer(a_session.playerId)) {
					if (std::find(candidates.begin(), candidates.end(), memberId) == candidates.end()) candidates.push_back(memberId);
				}
				std::vector<OutboundFrame> frames;
				for (const auto candidateId : candidates) {
					const bool isSender = candidateId == a_session.playerId;
					const Session* target = nullptr;
					std::uint32_t targetConnection = 0;
					for (const auto& entry : sessions) {
						if (entry.second.active && entry.second.playerId == candidateId) {
							target = &entry.second;
							targetConnection = entry.first;
							break;
						}
					}
					if (target == nullptr) continue;
					if (!target->compatibilityCapabilities.Has(F4T::QuestProtocol::kFireteamCapability)) continue;
					const auto& profile = target->profileId;
					if (profile.empty()) continue;
					const auto personalProfile = SessionQuestProfileView(*target);
					if (personalProfile.empty()) continue;
					const auto sharing = quests.ResolveSharing(profile, assist->quest, questMode, questOverrides);
					if (sharing == F4T::QuestState::Sharing::Blocked ||
						(!isSender && sharing != F4T::QuestState::Sharing::Fireteam)) {
						continue;
					}
					const auto recordedStage = quests.ProfileStage(personalProfile, assist->quest);
					if (!recordedStage) continue;
					if (quests.ProfileCompleted(personalProfile, assist->quest)) continue;
					if (StoryReachable(profile, assist->quest, *recordedStage).refusal != StoryRefusal::None) continue;
					if (!kill && !isSender) continue;
					const auto killClaim = profile + "|" + CreditClaimKey(*assist);
					if (kill && npcLedger.KillClaimed(killClaim, Scope{ hit->cellId, hit->worldspaceId }, NpcLedger::LedgerSourceKey(hit->authorityEpoch, hit->targetId, hit->sourceFormId), a_monotonicSeconds)) continue;
					const auto creditId = creditLedger.Mint(personalProfile, assist->quest, assist->objectiveId, assist->kind, a_unixTimeSeconds);
					if (creditId.empty()) continue;
					if (kill) npcLedger.RecordKill(killClaim, Scope{ hit->cellId, hit->worldspaceId }, NpcLedger::LedgerSourceKey(hit->authorityEpoch, hit->targetId, hit->sourceFormId), a_monotonicSeconds);
					stats.questCreditsMinted++;
					frames.push_back(ReliableFrame(
						targetConnection,
						F4T::QuestProtocol::MakeCredit(
							F4T::QuestProtocol::Credit{
								creditId,
								a_session.playerId,
								assist->quest,
								assist->objectiveId,
								assist->kind,
								kill ? assist->target : F4T::QuestProtocol::FormKey{}
							},
							a_unixTimeSeconds)));
				}
				if (hit != nullptr && !frames.empty()) hit->creditKeys.insert(CreditClaimKey(*assist));
				return frames;
			}

			std::vector<OutboundFrame> ProtocolHost::Impl::PollQuestAssists(double a_monotonicSeconds, double a_unixTimeSeconds)
			{
				std::vector<OutboundFrame> frames;
				for (auto& entry : sessions) {
					auto& pending = entry.second.pendingKillAssists;
					for (auto it = pending.begin(); it != pending.end();) {
						const auto assist = F4T::QuestProtocol::NormalizeAssist(it->packet);
						auto* hit = (assist && a_monotonicSeconds <= it->deadlineMonotonic)
							? AssistKillMatches(entry.second, *assist, a_monotonicSeconds)
							: nullptr;
						if (hit != nullptr && !AssistTargetDead(*hit, a_monotonicSeconds)) {
							++it;
							continue;
						}
						if (hit != nullptr) {
							auto minted = MintAssistCredits(entry.second, *assist, hit, a_monotonicSeconds, a_unixTimeSeconds);
							frames.insert(frames.end(), std::make_move_iterator(minted.begin()), std::make_move_iterator(minted.end()));
						} else {
							stats.questAssistsRejected++;
						}
						it = pending.erase(it);
					}
				}
				return frames;
			}

			std::vector<OutboundFrame> ProtocolHost::Impl::HandleQuestAssist(
				std::uint32_t,
				Session& a_session,
				const Json& a_packet,
				double a_monotonicSeconds,
				double a_unixTimeSeconds)
			{
				stats.questAssistsReceived++;
				if (!F4T::PacketRateLimit::TakeToken(
						a_session.questAssists,
						a_monotonicSeconds,
						kQuestReportRefillPerSecond,
						kQuestReportBurst)) {
					Reject();
					stats.questAssistsRejected++;
					return {};
				}
				const auto assist = F4T::QuestProtocol::NormalizeAssist(a_packet);
				if (!assist) {
					Reject();
					stats.questAssistsRejected++;
					return {};
				}
				const bool kill = assist->kind == F4T::QuestProtocol::AssistKind::KillActor;
				SessionTypes::AcceptedHitRecord* hit = nullptr;
				if (kill) {
					hit = AssistKillMatches(a_session, *assist, a_monotonicSeconds);
					if (hit == nullptr) {
						stats.questAssistsRejected++;
						return {};
					}
					if (!AssistTargetDead(*hit, a_monotonicSeconds)) {
						if (a_session.pendingKillAssists.size() >= kMaximumPendingKillAssists) {
							stats.questAssistsRejected++;
							return {};
						}
						a_session.pendingKillAssists.push_back({ a_packet, a_monotonicSeconds + kPendingKillAssistSeconds });
						return {};
					}
				} else {
					const auto personalProfile = SessionQuestProfileView(a_session);
					const bool questHeldAndOpen = !personalProfile.empty() &&
						quests.ProfileStage(personalProfile, assist->quest).has_value() &&
						!quests.ProfileCompleted(personalProfile, assist->quest);
					if (!NonKillAssistAccepted(a_session, *assist, fireteamService.MembersOfPlayer(a_session.playerId).size(), questHeldAndOpen, a_monotonicSeconds)) {
						Reject();
						stats.questAssistsRejected++;
						return {};
					}
				}
				return MintAssistCredits(a_session, *assist, hit, a_monotonicSeconds, a_unixTimeSeconds);
			}

			std::vector<OutboundFrame> ProtocolHost::Impl::HandleQuestCreditAck(
				std::uint32_t a_connectionId,
				Session& a_session,
				const Json& a_packet,
				double a_monotonicSeconds,
				double a_unixTimeSeconds)
			{
				stats.questCreditAcksReceived++;
				if (!F4T::PacketRateLimit::TakeToken(
						a_session.questAssists,
						a_monotonicSeconds,
						kQuestReportRefillPerSecond,
						kQuestReportBurst)) {
					Reject();
					stats.questCreditAcksRejected++;
					return {};
				}
				const auto ack = F4T::QuestProtocol::NormalizeCreditAck(a_packet);
				if (!ack) {
					Reject();
					stats.questCreditAcksRejected++;
					return {};
				}
				const auto personalProfile = SessionQuestProfileView(a_session);
				if (personalProfile.empty()) return {};
				const auto pending = creditLedger.Peek(ack->creditId, personalProfile, a_unixTimeSeconds);
				if (!pending) {
					Reject();
					stats.questCreditAcksRejected++;
					return {};
				}
				if (StoryReachable(a_session.profileId, pending->quest, ack->stage).refusal != StoryRefusal::None ||
					quests.ProfileCompleted(personalProfile, pending->quest)) {
					stats.questCreditAcksRejected++;
					return {};
				}
				const auto currentStage = quests.ProfileStage(personalProfile, pending->quest);
				if (!currentStage || ack->stage < *currentStage ||
					static_cast<std::uint32_t>(ack->stage) - *currentStage > F4T::QuestState::kMaximumStageAdvance) {
					stats.questCreditAcksRejected++;
					return {};
				}
				const Json normalized{
					{ "type", "questReport" },
					{ "quest", F4T::QuestProtocol::FormatFormKey(pending->quest) },
					{ "stage", ack->stage },
					{ "completed", false }
				};
				if (VetoQuest(a_connectionId, a_session, "questReport", normalized, a_unixTimeSeconds)) {
					stats.questCreditAcksRejected++;
					return {};
				}
				const auto snapshot = quests;
				const auto advance = quests.AdvanceProfileStage(personalProfile, pending->quest, ack->stage);
				if (advance == F4T::QuestState::ProfileAdvance::Refused) {
					stats.questCreditAcksRejected++;
					return {};
				}
				if (advance == F4T::QuestState::ProfileAdvance::Advanced && !SaveQuests(false)) {
					quests = snapshot;
					stats.questCreditAcksRejected++;
					return {};
				}
				(void)creditLedger.Take(ack->creditId, personalProfile, a_unixTimeSeconds);
				if (advance == F4T::QuestState::ProfileAdvance::Advanced) stats.questCreditAcksApplied++;
				return {};
			}

			std::vector<OutboundFrame> ProtocolHost::Impl::QuestPresenceFrames(
				std::uint32_t a_ownerPlayerId,
				const F4T::QuestProtocol::FormKey& a_quest,
				std::uint16_t a_stage,
				double a_unixTimeSeconds) const
			{
				std::vector<OutboundFrame> frames;
				if (!questsEnabled) return frames;
				for (const auto memberId : fireteamService.MembersOfPlayer(a_ownerPlayerId)) {
					if (memberId == a_ownerPlayerId) continue;
					for (const auto& entry : sessions) {
						if (!entry.second.active || entry.second.playerId != memberId) continue;
						if (!entry.second.compatibilityCapabilities.Has(F4T::QuestProtocol::kFireteamCapability)) continue;
						frames.push_back(ReliableFrame(
							entry.first,
							F4T::QuestProtocol::MakePresence(
								F4T::QuestProtocol::Presence{ a_ownerPlayerId, a_quest, a_stage, {} },
								a_unixTimeSeconds)));
						break;
					}
				}
				return frames;
			}
}
