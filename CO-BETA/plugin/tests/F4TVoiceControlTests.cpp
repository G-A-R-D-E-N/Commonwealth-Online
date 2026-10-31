#include "F4TVoiceControl.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace
{
	using F4T::Voice::ControlState;
	using F4T::Voice::ControlSync;
	using Json = nlohmann::json;

	Json Parse(const std::optional<std::string>& a_packet)
	{
		assert(a_packet.has_value());
		return Json::parse(*a_packet);
	}

	ControlState Canned(bool a_canSend, bool a_canPlay, bool a_deafened)
	{
		ControlState state;
		state.canSend = a_canSend;
		state.canPlay = a_canPlay;
		state.deafened = a_deafened;
		return state;
	}

	void TestFirstSendImmediate()
	{
		ControlSync sync;
		assert(!sync.Poll(0.0).has_value());
		sync.Negotiate(true);
		const auto first = Parse(sync.Poll(0.0));
		assert(first["revision"] == 1);
		assert(!sync.Poll(0.5).has_value());
	}

	void TestStormSendsAtMostOncePerSecond()
	{
		ControlSync sync;
		sync.Negotiate(true);
		std::vector<double> sends;
		for (int step = 0; step <= 100; ++step) {
			const double now = step / 10.0;
			sync.SetDesired(Canned(false, step % 2 == 0, false));
			if (sync.Poll(now).has_value()) {
				sends.push_back(now);
			}
		}
		assert(sends.size() == 11);
		for (std::size_t i = 1; i < sends.size(); ++i) {
			assert(sends[i] - sends[i - 1] >= 1.0);
		}
	}

	void TestCoalescesToLatestState()
	{
		ControlSync sync;
		sync.Negotiate(true);
		assert(Parse(sync.Poll(0.0))["revision"] == 1);
		sync.SetDesired(Canned(true, false, false));
		sync.SetDesired(Canned(true, true, false));
		sync.SetDesired(Canned(false, false, true));
		assert(!sync.Poll(0.5).has_value());
		const auto latest = Parse(sync.Poll(1.0));
		assert(latest["revision"] == 4);
		assert(latest["canSend"] == false);
		assert(latest["canPlay"] == false);
		assert(latest["deafened"] == true);
		assert(!sync.Poll(1.5).has_value());
	}

	void TestOldAckIgnored()
	{
		ControlSync sync;
		sync.Negotiate(true);
		assert(sync.Poll(0.0).has_value());
		sync.SetDesired(Canned(true, false, false));
		assert(Parse(sync.Poll(1.0))["revision"] == 2);
		sync.OnAck(1);
		assert(!sync.CanSendVoice());
		assert(sync.Poll(2.0).has_value());
		sync.OnAck(2);
		assert(sync.CanSendVoice());
		sync.OnAck(1);
		assert(sync.CanSendVoice());
		assert(!sync.Poll(3.0).has_value());
		sync.SetDesired(Canned(false, false, false));
		sync.OnAck(3);
		assert(!sync.CanSendVoice());
		ControlSync unsent;
		unsent.Negotiate(true);
		assert(unsent.Poll(0.0).has_value());
		unsent.SetDesired(Canned(true, false, false));
		unsent.OnAck(2);
		assert(!unsent.CanSendVoice());
	}

	void TestCanSendVoiceNeedsAck()
	{
		ControlSync sync;
		sync.Negotiate(true);
		sync.SetDesired(Canned(true, false, false));
		assert(!sync.CanSendVoice());
		assert(Parse(sync.Poll(0.0))["revision"] == 2);
		assert(!sync.CanSendVoice());
		sync.OnAck(2);
		assert(sync.CanSendVoice());
		sync.SetDesired(Canned(true, false, true));
		assert(!sync.CanSendVoice());
	}

	void TestMutedAckKeepsSendDenied()
	{
		ControlSync sync;
		sync.Negotiate(true);
		assert(Parse(sync.Poll(0.0))["revision"] == 1);
		sync.OnAck(1);
		assert(sync.CanSendVoice());
		sync.SetDesired(Canned(false, true, false));
		assert(Parse(sync.Poll(1.0))["revision"] == 2);
		sync.OnAck(2);
		assert(!sync.CanSendVoice());
		sync.SetDesired(Canned(true, true, false));
		assert(Parse(sync.Poll(2.0))["revision"] == 3);
		sync.OnAck(3);
		assert(sync.CanSendVoice());
	}

	void TestServerWithoutVoiceStaysOff()
	{
		ControlSync sync;
		assert(!sync.ServerNoVoice());
		sync.Negotiate(false);
		assert(sync.ServerNoVoice());
		sync.SetDesired(Canned(true, true, false));
		assert(!sync.Poll(0.0).has_value());
		assert(!sync.Poll(5.0).has_value());
		sync.OnAck(1);
		assert(!sync.CanSendVoice());
	}

	void TestNegotiatedAwayDeniesSend()
	{
		ControlSync sync;
		sync.Negotiate(true);
		sync.SetDesired(Canned(true, false, false));
		assert(Parse(sync.Poll(0.0))["revision"] == 2);
		sync.OnAck(2);
		assert(sync.CanSendVoice());
		sync.Negotiate(false);
		assert(!sync.CanSendVoice());
	}

	void TestReceiveNeedsNegotiatedVoice()
	{
		ControlSync sync;
		assert(!sync.CanReceiveVoice());
		sync.Negotiate(false);
		assert(!sync.CanReceiveVoice());
		sync.Negotiate(true);
		assert(sync.CanReceiveVoice());
		sync.Reset();
		assert(!sync.CanReceiveVoice());
	}

	void TestBlockedListSortedUniqueCapped()
	{
		ControlSync sync;
		sync.Negotiate(true);
		ControlState state;
		for (std::uint32_t id = 50; id >= 1; --id) {
			state.blockedPlayerIds.push_back(id);
		}
		state.blockedPlayerIds.push_back(1);
		state.blockedPlayerIds.push_back(2);
		sync.SetDesired(state);
		const auto snapshot = Parse(sync.Poll(0.0));
		const auto& blocked = snapshot["blockedPlayerIds"];
		assert(blocked.size() == 15);
		for (std::uint32_t i = 0; i < 15; ++i) {
			assert(blocked[i] == i + 1);
		}
	}

	void TestResetRestartsRevisions()
	{
		ControlSync sync;
		sync.Negotiate(true);
		assert(sync.Poll(0.0).has_value());
		sync.SetDesired(Canned(true, false, false));
		sync.SetDesired(Canned(true, true, false));
		sync.SetDesired(Canned(true, true, true));
		assert(Parse(sync.Poll(1.0))["revision"] == 4);
		sync.OnAck(4);
		assert(sync.CanSendVoice());

		sync.Reset();
		assert(!sync.CanSendVoice());
		assert(!sync.ServerNoVoice());
		sync.OnAck(4);
		sync.Negotiate(true);
		const auto restored = Parse(sync.Poll(0.0));
		assert(restored["revision"] == 1);
		assert(restored["canPlay"] == true);
		assert(restored["deafened"] == true);
		assert(!sync.CanSendVoice());
	}

	void TestResetClearsAckOfFirstRevision()
	{
		ControlSync sync;
		sync.Negotiate(true);
		assert(Parse(sync.Poll(0.0))["revision"] == 1);
		sync.OnAck(1);
		sync.Reset();
		sync.Negotiate(true);
		assert(sync.Poll(0.0).has_value());
		assert(!sync.CanSendVoice());
	}

	void TestSnapshotFieldsMatchPlan()
	{
		ControlSync sync;
		sync.Negotiate(true);
		ControlState state = Canned(true, true, true);
		state.blockedPlayerIds = { 7, 2 };
		sync.SetDesired(state);
		const auto snapshot = Parse(sync.Poll(0.0));
		assert(snapshot.size() == 10);
		assert(snapshot["type"] == "voiceState");
		assert(snapshot["revision"] == 2);
		assert(snapshot["profile"] == 0);
		assert(snapshot["rate"] == 48000);
		assert(snapshot["channels"] == 1);
		assert(snapshot["frameMs"] == 20);
		assert(snapshot["canSend"] == true);
		assert(snapshot["canPlay"] == true);
		assert(snapshot["deafened"] == true);
		assert(snapshot["blockedPlayerIds"] == Json::array({ 2, 7 }));
	}

	void TestPendingFollowsAck()
	{
		ControlSync unnegotiated;
		unnegotiated.SetDesired(Canned(true, true, false));
		assert(!unnegotiated.Pending());

		ControlSync noVoice;
		noVoice.Negotiate(false);
		assert(!noVoice.Pending());

		ControlSync sync;
		assert(!sync.Pending());
		sync.Negotiate(true);
		assert(sync.Pending());
		assert(Parse(sync.Poll(0.0))["revision"] == 1);
		assert(sync.Pending());
		sync.OnAck(1);
		assert(!sync.Pending());
		sync.SetDesired(Canned(false, true, false));
		assert(sync.Pending());
		assert(Parse(sync.Poll(1.0))["revision"] == 2);
		sync.OnAck(2);
		assert(!sync.Pending());

		sync.Reset();
		sync.Negotiate(true);
		assert(sync.Pending());
		assert(Parse(sync.Poll(0.0))["revision"] == 1);
		sync.OnAck(1);
		assert(!sync.Pending());
	}

	void TestDefaultStateAllowsSendAndPlay()
	{
		ControlSync sync;
		sync.Negotiate(true);
		const auto snapshot = Parse(sync.Poll(0.0));
		assert(snapshot["canSend"] == true);
		assert(snapshot["canPlay"] == true);
		assert(snapshot["deafened"] == false);
	}

	void TestUnchangedContentsDoNotBump()
	{
		ControlSync sync;
		sync.Negotiate(true);
		ControlState state = Canned(true, false, false);
		state.blockedPlayerIds = { 7, 2 };
		sync.SetDesired(state);
		assert(Parse(sync.Poll(0.0))["revision"] == 2);
		sync.OnAck(2);
		state.blockedPlayerIds = { 2, 7, 7 };
		sync.SetDesired(state);
		assert(!sync.Poll(5.0).has_value());
		assert(sync.CanSendVoice());
	}
}

int main()
{
	TestFirstSendImmediate();
	TestStormSendsAtMostOncePerSecond();
	TestCoalescesToLatestState();
	TestOldAckIgnored();
	TestCanSendVoiceNeedsAck();
	TestMutedAckKeepsSendDenied();
	TestServerWithoutVoiceStaysOff();
	TestNegotiatedAwayDeniesSend();
	TestReceiveNeedsNegotiatedVoice();
	TestBlockedListSortedUniqueCapped();
	TestResetRestartsRevisions();
	TestResetClearsAckOfFirstRevision();
	TestSnapshotFieldsMatchPlan();
	TestDefaultStateAllowsSendAndPlay();
	TestUnchangedContentsDoNotBump();
	TestPendingFollowsAck();
	return 0;
}
