#include "ServerFactionTestSupport.h"

using namespace FactionTestSupport;

namespace
{
	Json FactionChatPacket(std::string_view a_channel, std::string_view a_text)
	{
		return Json{ { "type", "chatSend" }, { "channel", std::string{ a_channel } }, { "text", std::string{ a_text } } };
	}

	void TestFactionChatReachesOnlyItsMembers()
	{
		ProtocolHost host{ "Faction Chat" };
		host.SetChatEnabled(true);
		assert(host.EnableFactions(nullptr));
		const auto alpha = Join(host, 10, "p10", 1.0, 100.0);
		const auto bob = Join(host, 20, "p20", 1.1, 100.2);
		Join(host, 30, "p30", 1.2, 100.4);
		Join(host, 40, "p40", 1.3, 100.6);
		Join(host, 50, "p50", 1.4, 100.8);
		FormFaction(host, 10, 20, bob.playerId, "Alpha Team");
		AssertResult(Send(host, 40, Create("Beta Team"), 2.5, 101.5), 40, "create", true);

		const auto frames = Send(host, 10, FactionChatPacket("faction", "hi team"), 3.0, 102.0);
		assert(frames.size() == 2);
		assert(HasPacket(frames, 10, "chatMessage"));
		assert(HasPacket(frames, 20, "chatMessage"));
		const auto message = RequirePacket(frames, 20, "chatMessage");
		assert(message.at("channel") == "faction");
		assert(message.at("text") == "hi team");
		assert(message.at("fromPlayerId") == alpha.playerId);
		assert(RequirePacket(frames, 10, "chatMessage") == message);

		const auto rival = Send(host, 40, FactionChatPacket("faction", "beta here"), 3.1, 102.1);
		assert(rival.size() == 1);
		assert(HasPacket(rival, 40, "chatMessage"));

		const auto outsider = Send(host, 30, FactionChatPacket("faction", "let me in"), 3.2, 102.2);
		assert(outsider.size() == 1);
		assert(RequirePacket(outsider, 30, "chatRejected").at("reason") == "notInFaction");

		const auto global = Send(host, 30, FactionChatPacket("global", "open"), 4.0, 103.0);
		assert(global.size() == 5);
		assert(RequirePacket(global, 10, "chatMessage").at("channel") == "global");
		const auto history = RequirePacket(Send(host, 50, Json{ { "type", "chatHistoryRequest" } }, 5.0, 104.0), 50, "chatHistory");
		assert(history.at("messages").size() == 1);
		assert(history.at("messages").front().at("text") == "open");
		const auto memberHistory = RequirePacket(Send(host, 20, Json{ { "type", "chatHistoryRequest" } }, 5.0, 104.0), 20, "chatHistory").at("messages");
		assert(memberHistory.size() == 2);
		assert(memberHistory.at(0).at("text") == "hi team");
		assert(!memberHistory.at(0).contains("fromProfileId"));
		assert(!memberHistory.at(0).contains("fromSocialKey"));
		assert(!memberHistory.at(0).contains("factionId"));
		assert(!memberHistory.at(0).contains("factionMembers"));
		const auto rivalHistory = RequirePacket(Send(host, 40, Json{ { "type", "chatHistoryRequest" } }, 5.0, 104.0), 40, "chatHistory").at("messages");
		assert(rivalHistory.size() == 2);
		assert(rivalHistory.at(0).at("text") == "beta here");

		assert(RequirePacket(Send(host, 10, FactionChatPacket("party", "x"), 6.0, 105.0), 10, "chatRejected").at("reason") == "channelUnavailable");

		host.SetChatMuted("p20", true);
		assert(RequirePacket(Send(host, 20, FactionChatPacket("faction", "muted"), 7.0, 106.0), 20, "chatRejected").at("reason") == "muted");
		host.SetChatMuted("p20", false);
	}

	void TestRemovedMembersStopReceivingFactionChat()
	{
		ProtocolHost host{ "Faction Chat Removal" };
		host.SetChatEnabled(true);
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto bob = Join(host, 20, "p20", 1.1, 100.2);
		const auto carol = Join(host, 30, "p30", 1.2, 100.4);
		FormFaction(host, 10, 20, bob.playerId, "Alpha Team");
		const auto invite = Send(host, 10, Invite(carol.playerId), 2.3, 101.3);
		AssertResult(invite, 10, "invite", true);
		AssertResult(Send(host, 30, Respond(InviteIdFrom(invite, 30), true), 2.4, 101.4), 30, "inviteResponse", true);

		const auto everyone = Send(host, 10, FactionChatPacket("faction", "one"), 3.0, 102.0);
		assert(everyone.size() == 3);
		assert(HasPacket(everyone, 10, "chatMessage"));
		assert(HasPacket(everyone, 20, "chatMessage"));
		assert(HasPacket(everyone, 30, "chatMessage"));

		AssertResult(Send(host, 10, Kick("p20"), 6.0, 105.0), 10, "kick", true);
		const auto kickedHistory = RequirePacket(Send(host, 20, Json{ { "type", "chatHistoryRequest" } }, 6.1, 105.1), 20, "chatHistory").at("messages");
		assert(kickedHistory.empty());

		const auto afterKick = Send(host, 10, FactionChatPacket("faction", "two"), 7.0, 106.0);
		assert(afterKick.size() == 2);
		assert(HasPacket(afterKick, 10, "chatMessage"));
		assert(!HasPacket(afterKick, 20, "chatMessage"));
		assert(HasPacket(afterKick, 30, "chatMessage"));

		assert(RequirePacket(Send(host, 20, FactionChatPacket("faction", "three"), 8.0, 107.0), 20, "chatRejected").at("reason") == "notInFaction");

		AssertResult(Send(host, 30, Leave(), 9.0, 108.0), 30, "leave", true);

		const auto afterLeave = Send(host, 10, FactionChatPacket("faction", "four"), 10.0, 109.0);
		assert(afterLeave.size() == 1);
		assert(HasPacket(afterLeave, 10, "chatMessage"));
		assert(!HasPacket(afterLeave, 30, "chatMessage"));
	}

	void TestHistoryUsesMembershipAtSendAndCurrentNameVisibility()
	{
		ProtocolHost host{ "Faction History Privacy" };
		host.SetChatEnabled(true);
		assert(host.EnableSocial(nullptr));
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto bob = Join(host, 20, "p20", 1.1, 100.2);
		const auto carol = Join(host, 30, "p30", 1.2, 100.4);
		Send(host, 10, Json{ { "type", "playerState" }, { "characterName", "Alice" } }, 1.5, 100.5);
		FormFaction(host, 10, 20, bob.playerId, "Alpha Team");
		const auto chat = Send(host, 10, FactionChatPacket("faction", "private"), 3.0, 102.0);
		assert(RequirePacket(chat, 20, "chatMessage").at("displayName") == "Alice");
		assert(RequirePacket(chat, 20, "chatMessage").at("fromName") == "Alice");
		const auto invite = Send(host, 10, Invite(carol.playerId), 4.0, 103.0);
		AssertResult(Send(host, 30, Respond(InviteIdFrom(invite, 30), true), 4.1, 103.1), 30, "inviteResponse", true);
		const auto newcomerHistory = RequirePacket(Send(host, 30, Json{ { "type", "chatHistoryRequest" } }, 5.0, 104.0), 30, "chatHistory").at("messages");
		assert(newcomerHistory.empty());
		AssertResult(Send(host, 10, Leave(), 6.0, 105.0), 10, "leave", true);
		const auto history = RequirePacket(Send(host, 20, Json{ { "type", "chatHistoryRequest" } }, 7.0, 106.0), 20, "chatHistory").at("messages");
		assert(history.size() == 1);
		assert(history.at(0).at("displayName") == "Alice");
		assert(history.at(0).at("fromName") == "Alice");
	}

	void TestFactionTrafficDoesNotEvictGlobalHistory()
	{
		ProtocolHost host{ "Faction History Capacity" };
		host.SetChatEnabled(true);
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		Join(host, 20, "p20", 1.1, 100.2);
		AssertResult(Send(host, 10, Create("Alpha Team"), 2.0, 101.0), 10, "create", true);
		Send(host, 20, FactionChatPacket("global", "global remains"), 3.0, 102.0);
		for (int i = 0; i < 21; ++i) {
			const auto now = 10.0 + i * 11.0;
			assert(HasPacket(Send(host, 10, FactionChatPacket("faction", "team " + std::to_string(i)), now, now + 99.0), 10, "chatMessage"));
		}
		const auto history = RequirePacket(Send(host, 20, Json{ { "type", "chatHistoryRequest" } }, 250.0, 349.0), 20, "chatHistory").at("messages");
		assert(history.size() == 1);
		assert(history.at(0).at("text") == "global remains");
	}

	void TestFactionBucketsEvictIndependently()
	{
		ProtocolHost host{ "Faction History Buckets" };
		host.SetChatEnabled(true);
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto bob = Join(host, 20, "p20", 1.1, 100.2);
		Join(host, 40, "p40", 1.3, 100.6);
		FormFaction(host, 10, 20, bob.playerId, "Alpha Team");
		AssertResult(Send(host, 40, Create("Beta Team"), 2.0, 101.0), 40, "create", true);
		assert(HasPacket(Send(host, 40, FactionChatPacket("faction", "beta first"), 3.0, 102.0), 40, "chatMessage"));
		for (int i = 0; i < 21; ++i) {
			const auto now = 10.0 + i * 11.0;
			assert(HasPacket(Send(host, 10, FactionChatPacket("faction", "alpha " + std::to_string(i)), now, now + 99.0), 10, "chatMessage"));
		}
		const auto history = RequirePacket(Send(host, 40, Json{ { "type", "chatHistoryRequest" } }, 250.0, 349.0), 40, "chatHistory").at("messages");
		assert(history.size() == 1);
		assert(history.at(0).at("text") == "beta first");
	}

	void TestTotalHistoryIsBoundedAcrossDisbandedFactions()
	{
		ProtocolHost host{ "Faction History Total Capacity" };
		host.SetChatEnabled(true);
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		Join(host, 20, "p20", 1.1, 100.1);
		AssertResult(Send(host, 10, Create("Keep Team"), 2.0, 101.0), 10, "create", true);
		assert(HasPacket(Send(host, 10, FactionChatPacket("global", "global remains"), 3.0, 102.0), 10, "chatMessage"));
		assert(HasPacket(Send(host, 10, FactionChatPacket("faction", "old team"), 14.0, 113.0), 10, "chatMessage"));
		double now = 30.0;
		for (int faction = 0; faction < 102; ++faction) {
			AssertResult(Send(host, 20, Create("Churn Team"), now, now + 99.0), 20, "create", true);
			for (int message = 0; message < 20; ++message) {
				now += 11.0;
				assert(HasPacket(Send(host, 20, FactionChatPacket("faction", "team " + std::to_string(message)), now, now + 99.0), 20, "chatMessage"));
			}
			now += 11.0;
			if (faction != 101) AssertResult(Send(host, 20, Disband(), now, now + 99.0), 20, "disband", true);
			now += 11.0;
		}
		const auto oldHistory = RequirePacket(Send(host, 10, Json{ { "type", "chatHistoryRequest" } }, now, now + 99.0), 10, "chatHistory").at("messages");
		assert(oldHistory.size() == 1 && oldHistory.at(0).at("text") == "global remains");
		const auto currentHistory = RequirePacket(Send(host, 20, Json{ { "type", "chatHistoryRequest" } }, now, now + 99.0), 20, "chatHistory").at("messages");
		assert(currentHistory.size() == 20 && currentHistory.at(0).at("text") == "team 0" && currentHistory.at(19).at("text") == "team 19");
	}

	void TestFactionSenderNameIsSanitized()
	{
		ProtocolHost host{ "Faction Sender Name" };
		host.SetChatEnabled(true);
		assert(host.EnableSocial(nullptr));
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto bob = Join(host, 20, "p20", 1.1, 100.2);
		Send(host, 10, Json{ { "type", "playerState" }, { "characterName", "[ADMIN] Alice" } }, 1.5, 100.5);
		FormFaction(host, 10, 20, bob.playerId, "Alpha Team");
		const auto chat = Send(host, 10, FactionChatPacket("faction", "hi"), 3.0, 102.0);
		assert(RequirePacket(chat, 20, "chatMessage").at("displayName") == "Alice");
		assert(RequirePacket(chat, 20, "chatMessage").at("fromName") == "Alice");
		const auto history = RequirePacket(Send(host, 20, Json{ { "type", "chatHistoryRequest" } }, 5.0, 104.0), 20, "chatHistory").at("messages");
		assert(history.size() == 1);
		assert(history.at(0).at("fromName") == "Alice");
	}

	void TestDisbandedFactionChatIsRejectedForEveryone()
	{
		ProtocolHost host{ "Faction Chat Disband" };
		host.SetChatEnabled(true);
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto bob = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, bob.playerId, "Doomed Team");
		assert(Send(host, 10, FactionChatPacket("faction", "before"), 3.0, 102.0).size() == 2);

		AssertResult(Send(host, 10, Disband(), 4.0, 103.0), 10, "disband", true);
		const auto history = RequirePacket(Send(host, 20, Json{ { "type", "chatHistoryRequest" } }, 4.1, 103.1), 20, "chatHistory").at("messages");
		assert(history.empty());

		const auto leader = Send(host, 10, FactionChatPacket("faction", "after"), 5.0, 104.0);
		assert(leader.size() == 1 && RequirePacket(leader, 10, "chatRejected").at("reason") == "notInFaction");
		const auto member = Send(host, 20, FactionChatPacket("faction", "after too"), 6.0, 105.0);
		assert(member.size() == 1 && RequirePacket(member, 20, "chatRejected").at("reason") == "notInFaction");
		assert(!HasPacket(member, 10, "chatMessage"));
	}
}

int main()
{
	TestFactionChatReachesOnlyItsMembers();
	TestRemovedMembersStopReceivingFactionChat();
	TestHistoryUsesMembershipAtSendAndCurrentNameVisibility();
	TestFactionTrafficDoesNotEvictGlobalHistory();
	TestFactionBucketsEvictIndependently();
	TestTotalHistoryIsBoundedAcrossDisbandedFactions();
	TestFactionSenderNameIsSanitized();
	TestDisbandedFactionChatIsRejectedForEveryone();
	return 0;
}
