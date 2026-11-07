#include "ServerQuestTestSupport.h"

#include "F4TGnsSnapshotEnvelope.h"
#include "F4TLoadOrder.h"
#include "F4TQuestProtocol.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace QuestTestSupport;

namespace
{
	using F4T::GnsSnapshotEnvelope::Family;

	struct Client
	{
		std::uint32_t connectionId{};
		std::uint32_t playerId{};
		std::vector<OutboundFrame> hello;
	};

	Client JoinCapable(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_profileId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		a_host.BindSessionProfile(a_connectionId, a_profileId);
		Client client;
		client.connectionId = a_connectionId;
		client.playerId = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		client.hello = Send(a_host, a_connectionId, Json{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "capabilities", Json::array({
				std::string{ F4T::QuestProtocol::kSyncCapability },
				std::string{ F4T::QuestProtocol::kFireteamCapability } }) } }, a_monotonicSeconds, a_unixTimeSeconds + 0.1);
		assert(HasPacket(client.hello, a_connectionId, "sessionReady"));
		return client;
	}

	void FormFireteam(ProtocolHost& a_host, const Client& a_leader, const Client& a_member, double a_unixTimeSeconds)
	{
		const auto invite = Send(a_host, a_leader.connectionId, Json{
			{ "type", std::string{ F4T::QuestProtocol::kFireteamInviteType } },
			{ "targetPlayerId", a_member.playerId } }, a_unixTimeSeconds, a_unixTimeSeconds);
		const auto inviteId = RequirePacket(invite, a_member.connectionId, F4T::QuestProtocol::kFireteamInviteType).at("inviteId").get<std::string>();
		const auto accepted = Send(a_host, a_member.connectionId, Json{
			{ "type", std::string{ F4T::QuestProtocol::kFireteamRespondType } },
			{ "inviteId", inviteId },
			{ "accept", true } }, a_unixTimeSeconds + 0.1, a_unixTimeSeconds + 0.1);
		assert(HasPacket(accepted, a_leader.connectionId, F4T::QuestProtocol::kFireteamSnapshotType));
	}

	Json Assist(const char* a_kind, std::uint32_t a_sequence, std::uint32_t a_attackSequence, const char* a_target = "000123:fallout4.esm", std::uint32_t a_objectiveId = 7)
	{
		Json packet{
			{ "type", std::string{ F4T::QuestProtocol::kQuestAssistType } },
			{ "sequence", a_sequence },
			{ "quest", std::string{ kQuest } },
			{ "objectiveId", a_objectiveId },
			{ "kind", std::string{ a_kind } },
			{ "targetFormKey", std::string{ a_target } }
		};
		if (a_attackSequence != 0) packet["attackSequence"] = a_attackSequence;
		return packet;
	}

	std::string AcceptableCreditId(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		return RequirePacket(a_frames, a_connectionId, F4T::QuestProtocol::kQuestCreditType).at("creditId").get<std::string>();
	}

	void EnterFireteamQuest(ProtocolHost& a_host, const Client& a_client, double a_unixTimeSeconds)
	{
		assert(HasPacket(
			Send(a_host, a_client.connectionId, ScopeRequestPacket(kQuest, "fireteam"), a_unixTimeSeconds, a_unixTimeSeconds),
			a_client.connectionId,
			"questPolicy"));
		Send(a_host, a_client.connectionId, Report(kQuest, 30), a_unixTimeSeconds + 0.05, a_unixTimeSeconds + 0.05);
	}

	std::vector<std::uint8_t> EncodeSnapshot(const Json& a_packet, Family a_family, std::uint32_t a_sequence)
	{
		const auto json = a_packet.dump();
		std::vector<std::uint8_t> output(F4T::GnsSnapshotEnvelope::HeaderSize + json.size());
		assert(F4T::GnsSnapshotEnvelope::WriteHeader(std::span<std::uint8_t>{ output }, a_family, a_sequence));
		std::copy(json.begin(), json.end(), output.begin() + static_cast<std::ptrdiff_t>(F4T::GnsSnapshotEnvelope::HeaderSize));
		return output;
	}

	std::string EncodedPluginList()
	{
		F4T::LoadOrder::PluginList list;
		list.full.push_back(F4T::LoadOrder::Plugin{ F4T::LoadOrder::PluginKind::full, 1, "Mod.esp" });
		list.light.push_back(F4T::LoadOrder::Plugin{ F4T::LoadOrder::PluginKind::light, 1, "Light.esl" });
		const auto encoded = F4T::LoadOrder::EncodePluginList(list);
		assert(encoded.has_value());
		return *encoded;
	}

	Json PluginListPacket()
	{
		return Json{ { "type", std::string{ F4T::LoadOrder::kPacketType } }, { "data", EncodedPluginList() } };
	}

	Json SpawnTransform()
	{
		return Json{
			{ "type", "transform" },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "x", 20.0 },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "movementType", "spawn" }
		};
	}

	Json RangedPlayerState(std::uint32_t a_actionSequence)
	{
		// constraint: wire key name, source:CO-SERVER/server-cpp/src/ServerCombatChecks.cpp:RecordAcceptedHit
		return Json{
			{ "type", "playerState" },
			{ "equippedItems", Json::array({ Json{ { "slot", "rightHand" }, { "formId", "1234" } } }) },
			{ "actionEvents", Json::array({ Json{ { "sequence", a_actionSequence }, { "type", 3 }, { "eventName", "fireSingle" } } }) }
		};
	}

	enum class Death { alive, flagged, healthOnly };

	Json Npc(std::uint32_t a_npcId, const char* a_sourceFormId, Death a_death)
	{
		Json npc{
			{ "npcId", a_npcId },
			{ "baseFormId", "5678" },
			{ "sourceFormId", std::string{ a_sourceFormId } },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "x", 20.0 },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "isDead", a_death == Death::flagged },
			{ "health", a_death == Death::alive ? 100.0 : 0.0 },
			{ "maxHealth", 100.0 }
		};
		if (a_death == Death::healthOnly) npc.erase("isDead");
		return npc;
	}

	Json NpcStateSnapshot(std::uint32_t a_authorityEpoch, Death a_death = Death::alive)
	{
		return Json{
			{ "type", "npcState" },
			{ "authorityEpoch", a_authorityEpoch },
			{ "authorityCellId", "CAFE" },
			{ "authorityWorldspaceId", "F00D" },
			{ "fullReplace", true },
			{ "npcs", Json::array({ Npc(42, "01000ABC", a_death), Npc(43, "FE001ABC", a_death), Npc(44, "02000ABC", a_death) }) }
		};
	}

	void SendNpcState(ProtocolHost& a_host, std::uint32_t a_authorityEpoch, std::uint32_t a_sequence, Death a_death, double a_monotonicSeconds)
	{
		a_host.HandleMessage(10, EncodeSnapshot(NpcStateSnapshot(a_authorityEpoch, a_death), Family::NpcState, a_sequence), a_monotonicSeconds, a_monotonicSeconds + 100.0);
	}

	std::size_t CreditFrames(const std::vector<OutboundFrame>& a_frames)
	{
		return static_cast<std::size_t>(std::ranges::count_if(a_frames, [](const OutboundFrame& a_frame) {
			return DecodePacket(a_frame).value("type", std::string{}) == std::string{ F4T::QuestProtocol::kQuestCreditType };
		}));
	}

	Json NpcHit(std::uint32_t a_npcId, std::uint32_t a_sequence, std::uint32_t a_attackSequence, const char* a_sourceFormId, std::uint32_t a_authorityEpoch)
	{
		return Json{
			{ "type", "npcHit" },
			{ "npcId", a_npcId },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "sequence", a_sequence },
			{ "attackSequence", a_attackSequence },
			{ "damage", 5.0 },
			{ "sourceFormId", std::string{ a_sourceFormId } },
			{ "weaponFormId", "1234" },
			{ "authorityEpoch", a_authorityEpoch }
		};
	}

	std::uint32_t SpawnAuthority(ProtocolHost& a_host, const Client& a_authority, double a_monotonicSeconds)
	{
		const auto frames = a_host.HandleMessage(a_authority.connectionId, EncodeSnapshot(SpawnTransform(), Family::Transform, 1), a_monotonicSeconds, a_monotonicSeconds + 100.0);
		const auto authority = RequirePacket(frames, a_authority.connectionId, "npcAuthority");
		assert(authority.at("authorityPlayerId").get<std::uint32_t>() == a_authority.playerId);
		return authority.at("authorityEpoch").get<std::uint32_t>();
	}

	void SendPosition(ProtocolHost& a_host, std::uint32_t a_connectionId, std::uint32_t a_sequence, const char* a_movementType, double a_monotonicSeconds)
	{
		auto packet = SpawnTransform();
		packet["movementType"] = a_movementType;
		a_host.HandleMessage(a_connectionId, EncodeSnapshot(packet, Family::Transform, a_sequence), a_monotonicSeconds, a_monotonicSeconds + 100.0);
	}

	std::uint32_t PrepareAcceptedKillHit(ProtocolHost& a_host, std::uint32_t a_npcId, const char* a_sourceFormId)
	{
		assert(QuestTestSupport::EnableStoryFixture(a_host, nullptr, QuestTestSupport::Mode::CoopStory, {}));
		const auto leader = JoinCapable(a_host, 10, "p10", 1.0, 100.0);
		const auto member = JoinCapable(a_host, 20, "p20", 1.1, 100.1);
		FormFireteam(a_host, leader, member, 2.0);
		EnterFireteamQuest(a_host, leader, 3.0);
		const auto epoch = SpawnAuthority(a_host, leader, 4.0);
		Send(a_host, 10, PluginListPacket(), 4.1, 104.1);
		SendNpcState(a_host, epoch, 1, Death::alive, 4.2);
		Send(a_host, 10, RangedPlayerState(50), 4.3, 104.3);
		Send(a_host, 10, NpcHit(a_npcId, 1, 50, a_sourceFormId, epoch), 4.4, 104.4);
		return epoch;
	}

	bool KillAssistCreditsAfterHit(std::uint32_t a_npcId, const char* a_sourceFormId, const char* a_targetFormKey)
	{
		ProtocolHost host{ "Quest Assist Sources" };
		const auto epoch = PrepareAcceptedKillHit(host, a_npcId, a_sourceFormId);
		SendNpcState(host, epoch, 2, Death::flagged, 4.45);
		return HasPacket(Send(host, 10, Assist("killActor", 1, 50, a_targetFormKey), 4.5, 104.5), 10, F4T::QuestProtocol::kQuestCreditType);
	}

	void TestForgedKillAssistCreditsNobody()
	{
		ProtocolHost host{ "Quest Assist Forged" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::CoopStory, {}));
		const auto leader = JoinCapable(host, 10, "p10", 1.0, 100.0);
		const auto member = JoinCapable(host, 20, "p20", 1.1, 100.1);
		FormFireteam(host, leader, member, 2.0);
		EnterFireteamQuest(host, leader, 3.0);
		EnterFireteamQuest(host, member, 3.2);

		const auto forged = Send(host, leader.connectionId, Assist("killActor", 11, 99), 4.0, 104.0);
		assert(!HasPacket(forged, leader.connectionId, F4T::QuestProtocol::kQuestCreditType));
		assert(!HasPacket(forged, member.connectionId, F4T::QuestProtocol::kQuestCreditType));
		assert(host.Stats().questAssistsReceived == 1);
		assert(host.Stats().questCreditsMinted == 0);
	}

	void TestAuthoritySelfHitCreditsKillAssist()
	{
		ProtocolHost host{ "Quest Assist Authority" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::CoopStory, {}));
		const auto leader = JoinCapable(host, 10, "p10", 1.0, 100.0);
		const auto member = JoinCapable(host, 20, "p20", 1.1, 100.1);
		FormFireteam(host, leader, member, 2.0);
		EnterFireteamQuest(host, leader, 3.0);
		const auto epoch = SpawnAuthority(host, leader, 4.0);
		Send(host, 10, PluginListPacket(), 4.1, 104.1);
		host.HandleMessage(10, EncodeSnapshot(NpcStateSnapshot(epoch), Family::NpcState, 1), 4.2, 104.2);
		Send(host, 10, RangedPlayerState(50), 4.3, 104.3);

		const auto before = host.Stats();
		const auto hit = Send(host, 10, NpcHit(42, 1, 50, "01000ABC", epoch), 4.4, 104.4);
		assert(hit.empty());
		assert(host.Stats().npcHitsReceived == before.npcHitsReceived + 1);
		assert(host.Stats().npcHitsRouted == before.npcHitsRouted);
		SendNpcState(host, epoch, 2, Death::flagged, 4.45);

		const auto credited = Send(host, 10, Assist("killActor", 1, 50, "000ABC:Mod.esp"), 4.5, 104.5);
		assert(AcceptableCreditId(credited, 10).starts_with("c-"));
		assert(RequirePacket(credited, 10, F4T::QuestProtocol::kQuestCreditType).at("targetFormKey") == "000ABC:mod.esp");
		assert(host.Stats().questCreditsMinted == 1);

		const auto duplicate = Send(host, 10, Assist("killActor", 1, 50, "000ABC:Mod.esp"), 4.6, 104.6);
		assert(!HasPacket(duplicate, 10, F4T::QuestProtocol::kQuestCreditType));
		assert(host.Stats().questCreditsMinted == 1);
	}

	void TestKillCreditIsPerProfileAndQuestObjective()
	{
		ProtocolHost host{ "Quest Assist Claim Key" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::CoopStory, {}));
		const auto leader = JoinCapable(host, 10, "p10", 1.0, 100.0);
		const auto member = JoinCapable(host, 20, "p20", 1.1, 100.1);
		FormFireteam(host, leader, member, 2.0);
		EnterFireteamQuest(host, leader, 3.0);
		EnterFireteamQuest(host, member, 3.2);
		const auto epoch = SpawnAuthority(host, leader, 4.0);
		Send(host, 10, PluginListPacket(), 4.1, 104.1);
		SendNpcState(host, epoch, 1, Death::alive, 4.2);
		Send(host, 10, RangedPlayerState(50), 4.3, 104.3);
		Send(host, 10, NpcHit(42, 1, 50, "01000ABC", epoch), 4.4, 104.4);
		Send(host, 10, RangedPlayerState(51), 4.41, 104.41);
		Send(host, 10, NpcHit(42, 2, 51, "01000ABC", epoch), 4.42, 104.42);
		SendNpcState(host, epoch, 2, Death::flagged, 4.45);

		const auto credited = Send(host, 10, Assist("killActor", 1, 50, "000ABC:Mod.esp"), 4.5, 104.5);
		assert(HasPacket(credited, 10, F4T::QuestProtocol::kQuestCreditType));
		assert(HasPacket(credited, 20, F4T::QuestProtocol::kQuestCreditType));
		assert(host.Stats().questCreditsMinted == 2);

		const auto sameObjective = Send(host, 10, Assist("killActor", 2, 51, "000ABC:Mod.esp"), 4.6, 104.6);
		assert(!HasPacket(sameObjective, 10, F4T::QuestProtocol::kQuestCreditType));
		assert(!HasPacket(sameObjective, 20, F4T::QuestProtocol::kQuestCreditType));
		assert(host.Stats().questCreditsMinted == 2);

		const auto nextObjective = Send(host, 10, Assist("killActor", 1, 50, "000ABC:Mod.esp", 8), 4.7, 104.7);
		assert(HasPacket(nextObjective, 10, F4T::QuestProtocol::kQuestCreditType));
		assert(HasPacket(nextObjective, 20, F4T::QuestProtocol::kQuestCreditType));
		assert(host.Stats().questCreditsMinted == 4);
	}

	void TestQuarantinedKillTargetIsNotCredited()
	{
		ProtocolHost host{ "Quest Assist Quarantine" };
		const auto epoch = PrepareAcceptedKillHit(host, 42, "01000ABC");
		SendNpcState(host, epoch, 2, Death::flagged, 4.45);
		SendNpcState(host, epoch, 3, Death::alive, 4.6);
		SendNpcState(host, epoch, 4, Death::flagged, 4.65);
		assert(CreditFrames(Send(host, 10, Assist("killActor", 1, 50, "000ABC:Mod.esp"), 4.7, 104.7)) == 0);
		assert(host.Stats().questCreditsMinted == 0);
	}

	void TestKillAssistMatchesPluginFormsByFullFormId()
	{
		assert(KillAssistCreditsAfterHit(42, "01000ABC", "000ABC:Mod.esp"));
		assert(KillAssistCreditsAfterHit(43, "FE001ABC", "000ABC:Light.esl"));
		assert(!KillAssistCreditsAfterHit(42, "01000ABC", "000ABD:Mod.esp"));
		assert(!KillAssistCreditsAfterHit(42, "01000ABC", "000ABC:Light.esl"));
		assert(!KillAssistCreditsAfterHit(43, "FE001ABC", "000ABC:Mod.esp"));
		assert(!KillAssistCreditsAfterHit(44, "02000ABC", "000ABC:Mod.esp"));
	}

	void TestKillAssistWaitsForDeadSnapshot()
	{
		ProtocolHost host{ "Quest Assist Pending" };
		const auto epoch = PrepareAcceptedKillHit(host, 42, "01000ABC");
		const auto assist = Assist("killActor", 1, 50, "000ABC:Mod.esp");

		assert(CreditFrames(Send(host, 10, assist, 4.5, 104.5)) == 0);
		assert(CreditFrames(Send(host, 10, assist, 4.5, 104.5)) == 0);
		assert(host.PollQuestAssists(4.6, 104.6).empty());
		assert(host.Stats().questCreditsMinted == 0);

		SendNpcState(host, epoch, 2, Death::flagged, 4.8);
		const auto minted = host.PollQuestAssists(5.0, 105.0);
		assert(CreditFrames(minted) == 1);
		assert(AcceptableCreditId(minted, 10).starts_with("c-"));
		assert(host.Stats().questCreditsMinted == 1);

		assert(host.PollQuestAssists(5.5, 105.5).empty());
		assert(CreditFrames(Send(host, 10, assist, 5.6, 105.6)) == 0);
		assert(host.Stats().questCreditsMinted == 1);
	}

	void TestPendingKillAssistIgnoresSnapshotOfNewerEpoch()
	{
		ProtocolHost host{ "Quest Assist New Epoch" };
		const auto epoch = PrepareAcceptedKillHit(host, 42, "01000ABC");
		assert(CreditFrames(Send(host, 10, Assist("killActor", 1, 50, "000ABC:Mod.esp"), 4.5, 104.5)) == 0);

		host.HandleMessage(20, EncodeSnapshot(SpawnTransform(), Family::Transform, 1), 4.6, 104.6);
		const auto handoff = Send(host, 10, Json{ { "type", "visibilityPause" }, { "paused", true }, { "movementEpoch", std::uint32_t{ 1000000 } } }, 4.7, 104.7);
		const auto nextEpoch = RequirePacket(handoff, 20, "npcAuthority").at("authorityEpoch").get<std::uint32_t>();
		assert(nextEpoch != epoch);

		host.HandleMessage(20, EncodeSnapshot(NpcStateSnapshot(nextEpoch, Death::flagged), Family::NpcState, 1), 4.8, 104.8);
		assert(CreditFrames(host.PollQuestAssists(5.0, 105.0)) == 0);
		assert(host.Stats().questCreditsMinted == 0);
	}

	void TestPendingKillAssistExpires()
	{
		ProtocolHost host{ "Quest Assist Expiry" };
		const auto epoch = PrepareAcceptedKillHit(host, 42, "01000ABC");
		assert(CreditFrames(Send(host, 10, Assist("killActor", 1, 50, "000ABC:Mod.esp"), 4.5, 104.5)) == 0);
		assert(host.Stats().questAssistsRejected == 0);

		assert(host.PollQuestAssists(9.6, 109.6).empty());
		assert(host.Stats().questAssistsRejected == 1);

		SendNpcState(host, epoch, 2, Death::flagged, 9.7);
		assert(host.PollQuestAssists(9.8, 109.8).empty());
		assert(host.Stats().questCreditsMinted == 0);
	}

	void TestKillAssistCountsZeroHealthWithoutDeadFlag()
	{
		ProtocolHost host{ "Quest Assist Health" };
		const auto epoch = PrepareAcceptedKillHit(host, 42, "01000ABC");
		assert(CreditFrames(Send(host, 10, Assist("killActor", 1, 50, "000ABC:Mod.esp"), 4.5, 104.5)) == 0);

		SendNpcState(host, epoch, 2, Death::healthOnly, 4.8);
		assert(CreditFrames(host.PollQuestAssists(5.0, 105.0)) == 1);
		assert(host.Stats().questCreditsMinted == 1);
	}

	void TestPendingKillAssistsAreCapped()
	{
		ProtocolHost host{ "Quest Assist Cap" };
		const auto epoch = PrepareAcceptedKillHit(host, 42, "01000ABC");
		const auto assist = Assist("killActor", 1, 50, "000ABC:Mod.esp");
		for (int index = 0; index < 8; ++index) {
			assert(CreditFrames(Send(host, 10, assist, 4.5, 104.5)) == 0);
		}
		assert(host.Stats().questAssistsRejected == 0);

		assert(CreditFrames(Send(host, 10, assist, 5.5, 105.5)) == 0);
		assert(host.Stats().questAssistsRejected == 1);

		SendNpcState(host, epoch, 2, Death::flagged, 5.6);
		assert(CreditFrames(host.PollQuestAssists(5.7, 105.7)) == 1);
		assert(host.Stats().questCreditsMinted == 1);
		assert(host.Stats().questAssistsRejected == 8);
	}

	void TestSenderSelfCreditAndAckLifecycle()
	{
		ProtocolHost host{ "Quest Assist Credit" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::CoopStory, {}));
		const auto leader = JoinCapable(host, 10, "p10", 1.0, 100.0);
		const auto member = JoinCapable(host, 20, "p20", 1.1, 100.1);
		FormFireteam(host, leader, member, 2.0);
		EnterFireteamQuest(host, leader, 3.0);
		EnterFireteamQuest(host, member, 3.2);
		SendPosition(host, 10, 1, "spawn", 3.9);

		const auto credited = Send(host, leader.connectionId, Assist("activate", 21, 0), 4.0, 104.0);
		const auto creditId = AcceptableCreditId(credited, leader.connectionId);
		assert(creditId.starts_with("c-"));
		assert(!HasPacket(credited, member.connectionId, F4T::QuestProtocol::kQuestCreditType));
		assert(host.Stats().questCreditsMinted == 1);

		Send(host, member.connectionId, Json{
			{ "type", std::string{ F4T::QuestProtocol::kQuestCreditAckType } },
			{ "creditId", creditId },
			{ "stage", 40 } }, 5.0, 105.0);
		assert(host.Stats().questCreditAcksReceived == 1);
		assert(host.Stats().questCreditAcksRejected == 1);
		assert(host.Stats().questCreditAcksApplied == 0);

		Send(host, leader.connectionId, Json{
			{ "type", std::string{ F4T::QuestProtocol::kQuestCreditAckType } },
			{ "creditId", creditId },
			{ "stage", 65535 } }, 5.05, 105.05);
		assert(host.Stats().questCreditAcksReceived == 2);
		assert(host.Stats().questCreditAcksRejected == 2);
		assert(host.Stats().questCreditAcksApplied == 0);

		Send(host, leader.connectionId, Json{
			{ "type", std::string{ F4T::QuestProtocol::kQuestCreditAckType } },
			{ "creditId", creditId },
			{ "stage", 25 } }, 5.1, 105.1);
		assert(host.Stats().questCreditAcksReceived == 3);
		assert(host.Stats().questCreditAcksRejected == 3);
		assert(host.Stats().questCreditAcksApplied == 0);

		Send(host, leader.connectionId, Json{
			{ "type", std::string{ F4T::QuestProtocol::kQuestCreditAckType } },
			{ "creditId", creditId },
			{ "stage", 30 } }, 5.15, 105.15);
		assert(host.Stats().questCreditAcksReceived == 4);
		assert(host.Stats().questCreditAcksRejected == 3);
		assert(host.Stats().questCreditAcksApplied == 0);

		const auto secondCredit = Send(host, leader.connectionId, Assist("activate", 22, 0, "000123:fallout4.esm", 8), 5.2, 105.2);
		const auto secondCreditId = AcceptableCreditId(secondCredit, leader.connectionId);
		Send(host, leader.connectionId, Json{
			{ "type", std::string{ F4T::QuestProtocol::kQuestCreditAckType } },
			{ "creditId", secondCreditId },
			{ "stage", 40 } }, 5.25, 105.25);
		assert(host.Stats().questCreditAcksReceived == 5);
		assert(host.Stats().questCreditAcksRejected == 3);
		assert(host.Stats().questCreditAcksApplied == 1);

		const auto regression = Send(host, leader.connectionId, Report(kQuest, 35), 5.3, 105.3);
		assert(!HasPacket(regression, leader.connectionId, F4T::QuestProtocol::kNotReachableType));

		Send(host, leader.connectionId, Json{
			{ "type", std::string{ F4T::QuestProtocol::kQuestCreditAckType } },
			{ "creditId", creditId },
			{ "stage", 50 } }, 5.4, 105.4);
		assert(host.Stats().questCreditAcksReceived == 6);
		assert(host.Stats().questCreditAcksRejected == 4);
		assert(host.Stats().questCreditAcksApplied == 1);

		const auto thirdCredit = Send(host, leader.connectionId, Assist("activate", 23, 0, "000123:fallout4.esm", 9), 5.5, 105.5);
		const auto thirdCreditId = AcceptableCreditId(thirdCredit, leader.connectionId);
		Send(host, member.connectionId, Json{
			{ "type", std::string{ F4T::QuestProtocol::kQuestCreditAckType } },
			{ "creditId", thirdCreditId },
			{ "stage", 45 } }, 5.6, 105.6);
		assert(host.Stats().questCreditAcksRejected == 5);
		assert(host.Stats().questCreditAcksApplied == 1);
		Send(host, leader.connectionId, Json{
			{ "type", std::string{ F4T::QuestProtocol::kQuestCreditAckType } },
			{ "creditId", thirdCreditId },
			{ "stage", 45 } }, 5.65, 105.65);
		assert(host.Stats().questCreditAcksRejected == 5);
		assert(host.Stats().questCreditAcksApplied == 2);
	}

	void TestNonKillAssistRejectedWithoutTeam()
	{
		ProtocolHost host{ "Quest Assist Solo" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::CoopStory, {}));
		const auto solo = JoinCapable(host, 10, "p10", 1.0, 100.0);
		EnterFireteamQuest(host, solo, 3.0);
		SendPosition(host, 10, 1, "spawn", 3.5);

		const auto frames = Send(host, 10, Assist("activate", 1, 0), 4.0, 104.0);
		assert(!HasPacket(frames, 10, F4T::QuestProtocol::kQuestCreditType));
		assert(host.Stats().questAssistsRejected == 1);
		assert(host.Stats().questCreditsMinted == 0);
	}

	void TestNonKillAssistRejectedWithoutFreshPosition()
	{
		ProtocolHost host{ "Quest Assist Position" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::CoopStory, {}));
		const auto leader = JoinCapable(host, 10, "p10", 1.0, 100.0);
		const auto member = JoinCapable(host, 20, "p20", 1.1, 100.1);
		FormFireteam(host, leader, member, 2.0);
		EnterFireteamQuest(host, leader, 3.0);

		assert(!HasPacket(Send(host, 10, Assist("activate", 1, 0), 4.0, 104.0), 10, F4T::QuestProtocol::kQuestCreditType));
		assert(host.Stats().questAssistsRejected == 1);

		SendPosition(host, 10, 1, "spawn", 4.1);
		assert(!HasPacket(Send(host, 10, Assist("activate", 2, 0), 20.0, 120.0), 10, F4T::QuestProtocol::kQuestCreditType));
		assert(host.Stats().questAssistsRejected == 2);

		SendPosition(host, 10, 2, "normal", 20.5);
		assert(CreditFrames(Send(host, 10, Assist("activate", 3, 0), 21.0, 121.0)) == 1);
		assert(host.Stats().questCreditsMinted == 1);
		assert(host.Stats().questAssistsRejected == 2);
	}

	void TestNonKillAssistRequiresTarget()
	{
		ProtocolHost host{ "Quest Assist Target" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::CoopStory, {}));
		const auto leader = JoinCapable(host, 10, "p10", 1.0, 100.0);
		const auto member = JoinCapable(host, 20, "p20", 1.1, 100.1);
		FormFireteam(host, leader, member, 2.0);
		EnterFireteamQuest(host, leader, 3.0);
		SendPosition(host, 10, 1, "spawn", 3.9);

		Json untargeted = Assist("escort", 1, 0);
		untargeted.erase("targetFormKey");
		assert(!HasPacket(Send(host, 10, untargeted, 4.0, 104.0), 10, F4T::QuestProtocol::kQuestCreditType));
		assert(host.Stats().questAssistsRejected == 1);

		assert(HasPacket(Send(host, 10, Assist("escort", 2, 0), 4.1, 104.1), 10, F4T::QuestProtocol::kQuestCreditType));
		assert(host.Stats().questCreditsMinted == 1);
	}

	void TestNonKillAssistRequiresHeldQuest()
	{
		ProtocolHost host{ "Quest Assist Held" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::CoopStory, {}));
		const auto leader = JoinCapable(host, 10, "p10", 1.0, 100.0);
		const auto member = JoinCapable(host, 20, "p20", 1.1, 100.1);
		FormFireteam(host, leader, member, 2.0);
		SendPosition(host, 10, 1, "spawn", 3.4);

		assert(!HasPacket(Send(host, 10, Assist("activate", 1, 0), 3.5, 103.5), 10, F4T::QuestProtocol::kQuestCreditType));
		assert(host.Stats().questAssistsRejected == 1);

		EnterFireteamQuest(host, leader, 3.6);
		assert(HasPacket(Send(host, 10, Assist("activate", 2, 0), 4.0, 104.0), 10, F4T::QuestProtocol::kQuestCreditType));
		assert(host.Stats().questCreditsMinted == 1);
		assert(host.Stats().questAssistsRejected == 1);
	}

	void TestNonKillAssistRateLimit()
	{
		ProtocolHost host{ "Quest Assist Rate" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::CoopStory, {}));
		const auto leader = JoinCapable(host, 10, "p10", 1.0, 100.0);
		const auto member = JoinCapable(host, 20, "p20", 1.1, 100.1);
		FormFireteam(host, leader, member, 2.0);
		EnterFireteamQuest(host, leader, 3.0);
		SendPosition(host, 10, 1, "spawn", 4.0);

		for (std::uint32_t objective = 1; objective <= 6; ++objective) {
			const double at = 4.0 + 0.5 * objective;
			assert(HasPacket(Send(host, 10, Assist("activate", objective, 0, "000123:fallout4.esm", objective), at, at + 100.0), 10, F4T::QuestProtocol::kQuestCreditType));
		}
		assert(!HasPacket(Send(host, 10, Assist("activate", 7, 0, "000123:fallout4.esm", 7), 7.5, 107.5), 10, F4T::QuestProtocol::kQuestCreditType));
		assert(host.Stats().questAssistsRejected == 1);
		assert(host.Stats().questCreditsMinted == 6);

		SendPosition(host, 10, 2, "normal", 67.0);
		assert(HasPacket(Send(host, 10, Assist("activate", 8, 0, "000123:fallout4.esm", 7), 67.5, 167.5), 10, F4T::QuestProtocol::kQuestCreditType));
		assert(host.Stats().questCreditsMinted == 7);
	}

	void TestNonKillAssistIdempotency()
	{
		ProtocolHost host{ "Quest Assist Claim" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::CoopStory, {}));
		const auto leader = JoinCapable(host, 10, "p10", 1.0, 100.0);
		const auto member = JoinCapable(host, 20, "p20", 1.1, 100.1);
		FormFireteam(host, leader, member, 2.0);
		EnterFireteamQuest(host, leader, 3.0);
		SendPosition(host, 10, 1, "spawn", 4.0);

		const auto credited = Send(host, 10, Assist("activate", 1, 0), 4.5, 104.5);
		const auto creditId = AcceptableCreditId(credited, 10);
		Send(host, 10, Json{
			{ "type", std::string{ F4T::QuestProtocol::kQuestCreditAckType } },
			{ "creditId", creditId },
			{ "stage", 40 } }, 4.6, 104.6);
		assert(host.Stats().questCreditAcksApplied == 1);

		assert(!HasPacket(Send(host, 10, Assist("activate", 2, 0), 6.0, 106.0), 10, F4T::QuestProtocol::kQuestCreditType));
		assert(host.Stats().questCreditsMinted == 1);
		assert(HasPacket(Send(host, 10, Assist("dialogue", 3, 0), 6.1, 106.1), 10, F4T::QuestProtocol::kQuestCreditType));
		assert(host.Stats().questCreditsMinted == 2);

		SendPosition(host, 10, 2, "normal", 124.9);
		assert(HasPacket(Send(host, 10, Assist("activate", 4, 0), 125.0, 125.0), 10, F4T::QuestProtocol::kQuestCreditType));
		assert(host.Stats().questCreditsMinted == 3);
	}

	void TestAssistRejectedWithoutCapability()
	{
		ProtocolHost host{ "Quest Assist Legacy" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, QuestTestSupport::Mode::CoopStory, {}));
		const auto welcome = host.HandleConnected(10, 100.0);
		host.BindSessionProfile(10, "p10");
		Send(host, 10, Json{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "capabilities", Json::array({ std::string{ F4T::QuestProtocol::kSyncCapability } }) } }, 1.0, 100.1);
		const auto frames = Send(host, 10, Assist("activate", 31, 0), 2.0, 102.0);
		assert(!HasPacket(frames, 10, F4T::QuestProtocol::kQuestCreditType));
		assert(host.Stats().questAssistsRejected == 1);
		assert(host.Stats().questAssistsReceived == 0);
	}
}

int main()
{
	TestForgedKillAssistCreditsNobody();
	TestAuthoritySelfHitCreditsKillAssist();
	TestKillCreditIsPerProfileAndQuestObjective();
	TestQuarantinedKillTargetIsNotCredited();
	TestKillAssistMatchesPluginFormsByFullFormId();
	TestKillAssistWaitsForDeadSnapshot();
	TestPendingKillAssistIgnoresSnapshotOfNewerEpoch();
	TestPendingKillAssistExpires();
	TestKillAssistCountsZeroHealthWithoutDeadFlag();
	TestPendingKillAssistsAreCapped();
	TestSenderSelfCreditAndAckLifecycle();
	TestNonKillAssistRejectedWithoutTeam();
	TestNonKillAssistRejectedWithoutFreshPosition();
	TestNonKillAssistRequiresTarget();
	TestNonKillAssistRequiresHeldQuest();
	TestNonKillAssistRateLimit();
	TestNonKillAssistIdempotency();
	TestAssistRejectedWithoutCapability();
	return 0;
}
