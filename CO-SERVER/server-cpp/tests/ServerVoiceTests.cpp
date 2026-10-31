#include "ServerVoice.h"
#include "ServerVoiceTestSupport.h"

#include "F4TVoiceWire.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::Delivery;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolStats;
	using CommonwealthOnline::Server::VoicePeerEntry;
	using CommonwealthOnline::Server::VoiceService;
	using CommonwealthOnline::Server::VoiceSessionState;
	using F4T::Voice::Tier;
	using F4T::Voice::kVoiceHeaderBytes;
	using F4T::Voice::kVoiceMaxPayloadBytes;
	using namespace ServerVoiceTestSupport;

	void RelaysToInRangeListenersOnly()
	{
		Harness harness;
		AddPeer(harness, 1, 1, 0.0);
		AddPeer(harness, 2, 2, 100.0);
		AddPeer(harness, 3, 3, 1000.0);
		AddPeer(harness, 4, 4, 50.0);
		harness.peers.back().peer.canPlay = false;
		const auto frames = harness.service.HandleFrame(1, MakeDatagram(1, true), harness.peers, 1.0);
		std::size_t relayed = 0;
		std::size_t talk = 0;
		for (const auto& frame : frames) {
			if (frame.delivery == Delivery::UnreliableSequenced) {
				assert(frame.connectionId == 2);
				F4T::Voice::VoicePacket packet;
				assert(F4T::Voice::DecodeVoicePacket(std::as_bytes(std::span{ frame.payload }), F4T::Voice::VoiceDirection::ServerToClient, packet) == F4T::Voice::VoiceDecodeResult::Ok);
				assert(packet.header.speakerId == 1);
				assert(packet.header.sequence == 1);
				assert(packet.payload.size() == kPayloadBytes);
				relayed++;
			} else {
				assert(frame.connectionId == 4);
				const auto packet = ParseFrame(frame);
				assert(packet.at("type") == "voiceTalk");
				assert(packet.at("playerId") == 1);
				assert(packet.at("talking") == true);
				talk++;
			}
		}
		assert(relayed == 1);
		assert(talk == 1);
		assert(harness.stats.voiceAccepted == 1);
		assert(harness.stats.voiceRelayed == 1);
	}

	void RangeBoundariesPerTier()
	{
		const std::array<std::pair<Tier, double>, 3> tiers{ { { Tier::Whisper, 256.0 }, { Tier::Normal, 1024.0 }, { Tier::Shout, 2560.0 } } };
		for (const auto& [tier, range] : tiers) {
			Harness harness;
			AddPeer(harness, 1, 1, 0.0);
			AddPeer(harness, 2, 2, range);
			AddPeer(harness, 3, 3, range + 0.5);
			const auto frames = SendVoice(harness, 1, 1, 1.0, tier);
			assert(RelayCount(frames, 2) == 1);
			assert(RelayCount(frames, 3) == 0);
		}
	}

	void VerticalDistanceCounts()
	{
		Harness harness;
		AddPeer(harness, 1, 1, 0.0);
		AddPeerAt(harness, 2, 2, F4T::InterestScope::StateScope{ "Cell", "WS", 0.0, 0.0 }, 200.0);
		AddPeerAt(harness, 3, 3, F4T::InterestScope::StateScope{ "Cell", "WS", 0.0, 0.0 }, 300.0);
		const auto frames = SendVoice(harness, 1, 1, 1.0);
		assert(RelayCount(frames, 2) == 1);
		assert(RelayCount(frames, 3) == 0);
	}

	void SameWorldspaceExteriorCellsRelayInteriorCellsDoNot()
	{
		Harness exterior;
		AddPeer(exterior, 1, 1, 0.0);
		AddPeerAt(exterior, 2, 2, F4T::InterestScope::StateScope{ "OtherCell", "WS", 100.0, 0.0 });
		assert(RelayCount(SendVoice(exterior, 1, 1, 1.0), 2) == 1);

		Harness interior;
		AddPeerAt(interior, 1, 1, F4T::InterestScope::StateScope{ "InteriorA", "", 0.0, 0.0 });
		AddPeerAt(interior, 2, 2, F4T::InterestScope::StateScope{ "InteriorB", "", 0.0, 0.0 });
		AddPeerAt(interior, 3, 3, F4T::InterestScope::StateScope{ "InteriorA", "", 0.0, 0.0 });
		const auto frames = SendVoice(interior, 1, 1, 1.0);
		assert(RelayCount(frames, 2) == 0);
		assert(RelayCount(frames, 3) == 1);
	}

	void ExclusionsAndBlockDirection()
	{
		Harness harness;
		AddPeer(harness, 1, 1, 0.0);
		AddPeer(harness, 2, 2, 100.0);
		AddPeer(harness, 3, 3, 100.0);
		harness.peers.back().peer.deafened = true;
		AddPeer(harness, 4, 4, 100.0);
		harness.peers.back().peer.canPlay = false;
		AddPeer(harness, 5, 5, 100.0);
		harness.peers.back().peer.blocked[0] = 1;
		harness.peers.back().peer.blockedCount = 1;
		AddPeerAt(harness, 6, 6, F4T::InterestScope::StateScope{ "Other", "OTHER", 100.0, 0.0 });
		AddPeer(harness, 7, 7, 100.0);
		AddPeerAt(harness, 8, 8, F4T::InterestScope::StateScope{ "Other", "WS", 200.0, 0.0 });
		AddPeerAt(harness, 9, 9, F4T::InterestScope::StateScope{ "Other", "WS", 300.0, 0.0 });
		AddPeerAt(harness, 10, 10, F4T::InterestScope::StateScope{ "Other", "", 0.0, 0.0 });
		harness.peers[0].peer.blocked[0] = 7;
		harness.peers[0].peer.blockedCount = 1;
		const auto frames = SendVoice(harness, 1, 1, 1.0);
		assert(RelayCount(frames, 2) == 1);
		assert(RelayCount(frames, 7) == 1);
		assert(RelayCount(frames, 8) == 1);
		for (const std::uint32_t excluded : { 1U, 3U, 4U, 5U, 6U, 9U, 10U }) {
			assert(RelayCount(frames, excluded) == 0);
		}
		assert(harness.stats.voiceRelayed == 3);
	}

	void SixTalkersFillFourSlotsWithoutFlapping()
	{
		Harness harness;
		for (std::uint32_t talker = 1; talker <= 6; ++talker) {
			AddPeer(harness, talker, static_cast<std::uint16_t>(talker), 100.0);
			harness.peers.back().peer.canPlay = false;
		}
		AddPeer(harness, 7, 7, 0.0);
		std::set<std::uint16_t> expected;
		for (std::uint32_t tick = 0; tick < 1000; ++tick) {
			const double now = 1000.0 + tick * 0.02;
			std::set<std::uint16_t> heard;
			for (std::uint32_t talker = 1; talker <= 6; ++talker) {
				const auto frames = harness.service.HandleFrame(talker, MakeDatagram(tick + 1, false), harness.peers, now);
				for (const auto& frame : frames) {
					if (frame.connectionId == 7 && frame.delivery == Delivery::UnreliableSequenced) {
						heard.insert(SpeakerOf(frame));
					}
				}
			}
			assert(heard.size() == 4);
			if (tick == 0) {
				expected = heard;
			}
			assert(heard == expected);
		}
		assert(harness.stats.voiceRelayed == 4000);
		assert(harness.stats.voiceDroppedEgress == 0);
	}

	void MidBurstReplacementRequiresClearlyCloserNewcomer()
	{
		const std::array<std::pair<double, std::size_t>, 2> cases{ { { 80.0, 1 }, { 90.0, 0 } } };
		for (const auto& [newcomerDistance, expectedRelays] : cases) {
			Harness harness;
			AddPeer(harness, 9, 9, 0.0);
			for (std::uint32_t talker = 1; talker <= 4; ++talker) {
				AddPeer(harness, talker, static_cast<std::uint16_t>(talker), 100.0);
				harness.peers.back().peer.canPlay = false;
			}
			AddPeer(harness, 5, 5, newcomerDistance);
			harness.peers.back().peer.canPlay = false;
			for (std::uint32_t talker = 1; talker <= 4; ++talker) {
				SendVoice(harness, talker, 1, 1000.0);
			}
			const auto frames = SendVoice(harness, 5, 1, 1000.01);
			assert(RelayCount(frames, 9) == expectedRelays);
		}
	}

	void DuplicateAndOldSequencesDropped()
	{
		Harness harness;
		AddPeer(harness, 1, 1, 0.0);
		AddPeer(harness, 2, 2, 10.0);
		assert(RelayCount(SendVoice(harness, 1, 5, 1.0), 2) == 1);
		assert(SendVoice(harness, 1, 5, 1.1).empty());
		assert(RelayCount(SendVoice(harness, 1, 100, 1.2), 2) == 1);
		assert(SendVoice(harness, 1, 5, 1.3).empty());
		assert(harness.stats.voiceDroppedDuplicate == 1);
		assert(harness.stats.voiceDroppedStale == 1);
	}

	void SoftLimitAutoMutesAndNotices()
	{
		Harness harness;
		AddPeer(harness, 1, 1, 0.0);
		std::uint32_t nextSequence = 1;
		std::size_t notices = 0;
		for (int window = 0; window <= 5; ++window) {
			for (int packet = 0; packet < 50; ++packet) {
				const auto acceptedBefore = harness.stats.voiceAccepted;
				const auto frames = harness.service.HandleFrame(1, MakeDatagram(nextSequence, false), harness.peers, window + 0.5);
				if (harness.stats.voiceAccepted > acceptedBefore) {
					nextSequence++;
				}
				for (const auto& frame : frames) {
					assert(frame.delivery == Delivery::ReliableOrdered);
					assert(ParseFrame(frame).at("code") == "rateLimited");
					notices++;
				}
			}
		}
		assert(harness.stats.voiceDroppedSoftLimit > 0);
		assert(harness.stats.voiceAutoMutes == 1);
		assert(notices == 1);
		const auto muted = harness.service.HandleFrame(1, MakeDatagram(nextSequence, false), harness.peers, 6.0);
		assert(muted.empty());
		assert(harness.stats.voiceDroppedMuted == 51);
		assert(harness.stats.voiceReceived == harness.stats.voiceAccepted + DroppedInbound(harness.stats));
	}

	void EightyPacketsPerSecondAutoMutesOnceAndHoldsThirtySeconds()
	{
		Harness harness;
		AddPeer(harness, 1, 1, 0.0);
		AddPeer(harness, 2, 2, 10.0);
		std::uint32_t sequence = 0;
		std::size_t notices = 0;
		double trigger = 0.0;
		for (int packet = 0; packet < 800; ++packet) {
			const double now = 1000.0 + packet / 80.0;
			const auto autoMutes = harness.stats.voiceAutoMutes;
			const auto frames = SendVoice(harness, 1, ++sequence, now);
			if (harness.stats.voiceAutoMutes > autoMutes) {
				trigger = now;
			}
			notices += NoticeCount(frames, 1, "rateLimited");
		}
		assert(harness.stats.voiceAutoMutes == 1);
		assert(notices == 1);
		assert(trigger > 0.0);
		const auto muted = harness.stats.voiceDroppedMuted;
		assert(SendVoice(harness, 1, ++sequence, trigger + 29.5).empty());
		assert(harness.stats.voiceDroppedMuted == muted + 1);
		assert(RelayCount(SendVoice(harness, 1, ++sequence, trigger + 30.5), 2) == 1);
		assert(harness.stats.voiceReceived == harness.stats.voiceAccepted + DroppedInbound(harness.stats));
	}

	void SixtyPacketsPerSecondNeverAutoMutes()
	{
		Harness harness;
		AddPeer(harness, 1, 1, 0.0);
		AddPeer(harness, 2, 2, 10.0);
		std::uint32_t sequence = 0;
		std::size_t notices = 0;
		for (int packet = 0; packet < 600; ++packet) {
			notices += NoticeCount(SendVoice(harness, 1, ++sequence, 2000.0 + packet / 60.0), 1, "rateLimited");
		}
		assert(harness.stats.voiceAutoMutes == 0);
		assert(notices == 0);
		assert(harness.stats.voiceDroppedSoftLimit > 0);
		assert(harness.stats.voiceDroppedMuted == 0);
		assert(harness.stats.voiceReceived == harness.stats.voiceAccepted + DroppedInbound(harness.stats));
	}

	void EgressBudgetDegradesWithoutRelayingOverBudget()
	{
		Harness harness;
		AddPeer(harness, 1, 1, 0.0);
		for (std::uint32_t connectionId = 2; connectionId <= 16; ++connectionId) {
			AddPeer(harness, connectionId, static_cast<std::uint16_t>(connectionId), 10.0);
		}
		for (std::uint32_t sequence = 1; sequence <= 10; ++sequence) {
			harness.service.HandleFrame(1, MakeDatagram(sequence, false), harness.peers, 1.0);
		}
		assert(harness.stats.voiceAccepted == 10);
		assert(harness.stats.voiceRelayed == 95);
		assert(harness.stats.voiceRelayed * F4T::Voice::kVoiceMaxDatagramBytes <= 16384);
		assert(harness.stats.voiceDroppedEgress == 150 - 95);
	}

	void DropsUnappliedAndUnnegotiatedSenders()
	{
		Harness harness;
		auto& state = AddPeer(harness, 1, 1, 0.0);
		AddPeer(harness, 2, 2, 10.0);
		state.stateApplied = false;
		assert(harness.service.HandleFrame(1, MakeDatagram(1, false), harness.peers, 1.0).empty());
		assert(harness.stats.voiceDroppedNoState == 1);
		state.stateApplied = true;
		harness.peers[0].peer.voiceV1 = false;
		assert(harness.service.HandleFrame(1, MakeDatagram(2, false), harness.peers, 1.0).empty());
		assert(harness.stats.voiceDroppedUnnegotiated == 1);
	}

	void CollidingTruncatedPlayerIdNeverTakesRelay()
	{
		Harness harness;
		AddPeer(harness, 1, 5, 0.0);
		AddPeer(harness, 2, 1, 100.0);
		harness.peers.back().peer.voiceV1 = false;
		AddPeer(harness, 3, 1, 100.0);
		const auto frames = SendVoice(harness, 1, 1, 1.0);
		assert(RelayCount(frames, 3) == 1);
		assert(RelayCount(frames, 2) == 0);
	}

	void NotSendableCountedSeparately()
	{
		Harness harness;
		auto& state = AddPeer(harness, 1, 1, 0.0);
		AddPeer(harness, 2, 2, 10.0);
		state.canSend = false;
		assert(SendVoice(harness, 1, 1, 1.0).empty());
		assert(harness.stats.voiceDroppedNotSendable == 1);
		assert(harness.stats.voiceDroppedUnnegotiated == 0);
	}

	void ListenerCapAboveSixteenRelaysNothing()
	{
		Harness harness;
		AddPeer(harness, 1, 1, 0.0);
		for (std::uint16_t playerId = 2; playerId <= 16; ++playerId) {
			AddPeer(harness, playerId, playerId, 10.0);
		}
		assert(SendVoice(harness, 1, 1, 1.0).size() == 15);
		AddPeer(harness, 17, 17, 10.0);
		assert(SendVoice(harness, 1, 2, 2.0).empty());
		assert(harness.stats.voiceRelayed == 15);
		assert(harness.stats.voiceAccepted == 2);
		harness.peers.pop_back();
		assert(RelayCount(SendVoice(harness, 1, 3, 2.5), 2) == 1);
	}

	void DatagramSizeBoundariesMatchWireLimits()
	{
		Harness harness;
		AddPeer(harness, 1, 1, 0.0);
		AddPeer(harness, 2, 2, 10.0);
		const std::array<std::pair<std::size_t, std::size_t>, 5> cases{ {
			{ kVoiceHeaderBytes - 1, 0 },
			{ kVoiceHeaderBytes, 0 },
			{ kVoiceHeaderBytes + 1, 1 },
			{ kVoiceHeaderBytes + kVoiceMaxPayloadBytes, 1 },
			{ kVoiceHeaderBytes + kVoiceMaxPayloadBytes + 1, 0 }
		} };
		std::uint32_t sequence = 0;
		double now = 1.0;
		for (const auto& [size, relayed] : cases) {
			auto datagram = MakeDatagram(++sequence, false);
			datagram.resize(size, 0x2A);
			const auto frames = harness.service.HandleFrame(1, datagram, harness.peers, now);
			assert(RelayCount(frames, 2) == relayed);
			now += 1.0;
		}
		assert(harness.stats.voiceDroppedMalformed == 3);
		assert(harness.stats.voiceRelayed == 2);
	}

	void TalkIndicatorReachesDeafenedListenersThatCannotPlay()
	{
		Harness harness;
		AddPeer(harness, 1, 1, 0.0);
		AddPeer(harness, 2, 2, 100.0);
		harness.peers.back().peer.canPlay = false;
		harness.peers.back().peer.deafened = true;
		const auto frames = harness.service.HandleFrame(1, MakeDatagram(1, true), harness.peers, 1.0);
		std::size_t talk = 0;
		for (const auto& frame : frames) {
			if (frame.connectionId == 2 && frame.delivery == Delivery::ReliableOrdered) {
				assert(ParseFrame(frame).at("type") == "voiceTalk");
				talk++;
			}
		}
		assert(talk == 1);
	}

	void BothBurstFlagsEmitOneTalkTransition()
	{
		Harness harness;
		AddPeer(harness, 1, 1, 0.0);
		AddPeer(harness, 2, 2, 100.0);
		harness.peers.back().peer.canPlay = false;
		std::size_t talk = 0;
		for (std::uint32_t sequence = 1; sequence <= 10; ++sequence) {
			talk += TalkCount(harness.service.HandleFrame(1, MakeDatagram(sequence, true, Tier::Whisper, true), harness.peers, 1.0 + sequence * 0.1), 2);
		}
		assert(talk == 1);
	}

	void TalkEndReachesListenerThatMovedOutOfRange()
	{
		Harness harness;
		AddPeer(harness, 1, 1, 0.0);
		AddPeer(harness, 2, 2, 100.0);
		harness.peers.back().peer.canPlay = false;
		assert(TalkCount(harness.service.HandleFrame(1, MakeDatagram(1, true), harness.peers, 1.0), 2) == 1);
		harness.peers.back().peer.scope->x = 1500.0;
		const auto ended = harness.service.Poll(harness.peers, 1.5);
		assert(ended.size() == 1);
		assert(ended.front().connectionId == 2);
		assert(ParseFrame(ended.front()).at("talking") == false);
	}

	void BlockedListenerGetsNoTalkFrame()
	{
		Harness harness;
		AddPeer(harness, 1, 1, 0.0);
		AddPeer(harness, 2, 2, 100.0);
		harness.peers.back().peer.canPlay = false;
		harness.peers.back().peer.blocked[0] = 1;
		harness.peers.back().peer.blockedCount = 1;
		AddPeer(harness, 3, 3, 100.0);
		harness.peers.back().peer.canPlay = false;
		const auto frames = harness.service.HandleFrame(1, MakeDatagram(1, true), harness.peers, 1.0);
		assert(TalkCount(frames, 2) == 0);
		assert(TalkCount(frames, 3) == 1);
	}
}

int main()
{
	RelaysToInRangeListenersOnly();
	RangeBoundariesPerTier();
	VerticalDistanceCounts();
	SameWorldspaceExteriorCellsRelayInteriorCellsDoNot();
	ExclusionsAndBlockDirection();
	SixTalkersFillFourSlotsWithoutFlapping();
	MidBurstReplacementRequiresClearlyCloserNewcomer();
	DuplicateAndOldSequencesDropped();
	SoftLimitAutoMutesAndNotices();
	EightyPacketsPerSecondAutoMutesOnceAndHoldsThirtySeconds();
	SixtyPacketsPerSecondNeverAutoMutes();
	EgressBudgetDegradesWithoutRelayingOverBudget();
	DropsUnappliedAndUnnegotiatedSenders();
	CollidingTruncatedPlayerIdNeverTakesRelay();
	NotSendableCountedSeparately();
	ListenerCapAboveSixteenRelaysNothing();
	TalkIndicatorReachesDeafenedListenersThatCannotPlay();
	DatagramSizeBoundariesMatchWireLimits();
	BothBurstFlagsEmitOneTalkTransition();
	TalkEndReachesListenerThatMovedOutOfRange();
	BlockedListenerGetsNoTalkFrame();
	return 0;
}
