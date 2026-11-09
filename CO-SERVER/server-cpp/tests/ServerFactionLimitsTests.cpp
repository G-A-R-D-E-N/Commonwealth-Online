#include "ServerFactionTestSupport.h"

using namespace FactionTestSupport;

int main()
{
	{
		ProtocolHost host{ "Faction Member Limit" };
		F4T::ServerConfiguration::FactionLimits limits;
		limits.maximumMembers = 2;
		limits.mutationCapacity = 100.0;
		host.SetFactionLimits(limits);
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto second = Join(host, 20, "p20", 1.1, 100.1);
		const auto third = Join(host, 30, "p30", 1.2, 100.2);
		AssertResult(Send(host, 10, Create("Alpha Team"), 2.0, 101.0), 10, "create", true);
		const auto invite = Send(host, 10, Invite(second.playerId), 2.1, 101.1);
		AssertResult(invite, 10, "invite", true);
		AssertResult(Send(host, 20, Respond(InviteIdFrom(invite, 20), true), 2.2, 101.2), 20, "inviteResponse", true);
		AssertResult(Send(host, 10, Invite(third.playerId), 2.3, 101.3), 10, "invite", false, "factionFull");
	}

	{
		ProtocolHost host{ "Faction Capacity Limit" };
		F4T::ServerConfiguration::FactionLimits limits;
		limits.maximumFactions = 1;
		limits.mutationCapacity = 100.0;
		host.SetFactionLimits(limits);
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		Join(host, 20, "p20", 1.1, 100.1);
		AssertResult(Send(host, 10, Create("Alpha Team"), 2.0, 101.0), 10, "create", true);
		AssertResult(Send(host, 20, Create("Beta Team"), 2.1, 101.1), 20, "create", false, "factionCapacity");
	}

	{
		ProtocolHost host{ "Faction Member Ceiling" };
		F4T::ServerConfiguration::FactionLimits limits;
		limits.maximumMembers = F4T::FactionCapacity::MaximumMembers;
		limits.invitesPerWindow = 100;
		limits.mutationCapacity = 100.0;
		limits.mutationRefillPerSecond = 100.0;
		host.SetFactionLimits(limits);
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		AssertResult(Send(host, 10, Create("Big Team"), 2.0, 101.0), 10, "create", true);
		double monotonic = 3.0;
		for (std::uint32_t number = 1; number < F4T::FactionCapacity::MaximumMembers; ++number) {
			const auto connection = 10 + number;
			const auto joined = Join(host, connection, "m" + std::to_string(1000 + number).substr(1), monotonic, 100.0 + monotonic);
			const auto invite = Send(host, 10, Invite(joined.playerId), monotonic + 0.1, 101.0 + monotonic);
			AssertResult(invite, 10, "invite", true);
			const auto accepted = Send(host, connection, Respond(InviteIdFrom(invite, connection), true), monotonic + 0.2, 101.0 + monotonic);
			AssertResult(accepted, connection, "inviteResponse", true);
			if (number == 16) assert(RequirePacket(accepted, connection, "factionState").at("faction").at("members").size() == 17);
			monotonic += 0.5;
		}
		const auto last = Join(host, 100, "p100", monotonic, 100.0 + monotonic);
		AssertResult(Send(host, 10, Invite(last.playerId), monotonic + 0.1, 101.0 + monotonic), 10, "invite", false, "factionFull");
	}

	{
		ProtocolHost host{ "Faction Invite Ceiling" };
		F4T::ServerConfiguration::FactionLimits limits;
		limits.pendingInvitesPerTarget = 17;
		limits.invitesPerWindow = 100;
		limits.mutationCapacity = 100.0;
		host.SetFactionLimits(limits);
		assert(host.EnableFactions(nullptr));
		const auto target = Join(host, 99, "target", 1.0, 100.0);
		for (std::uint32_t number = 1; number <= 17; ++number) {
			const auto connection = 10 + number;
			const auto at = 2.0 + number * 0.5;
			Join(host, connection, "l" + std::to_string(1000 + number).substr(1), at, 100.0 + at);
			AssertResult(Send(host, connection, Create("Faction " + std::to_string(number)), at + 0.1, 101.0 + at), connection, "create", true);
			AssertResult(Send(host, connection, Invite(target.playerId), at + 0.2, 101.0 + at), connection, "invite", true);
		}
		Join(host, 40, "l018", 12.0, 112.0);
		AssertResult(Send(host, 40, Create("Faction 18"), 12.1, 112.1), 40, "create", true);
		AssertResult(Send(host, 40, Invite(target.playerId), 12.2, 112.2), 40, "invite", false, "inviteCapacity");
	}

	{
		ProtocolHost host{ "Faction Invite Persistence" };
		F4T::ServerConfiguration::FactionLimits limits;
		limits.pendingInvitesPerTarget = 256;
		limits.invitesPerWindow = 256;
		limits.mutationCapacity = 100.0;
		host.SetFactionLimits(limits);
		assert(host.EnableFactions(nullptr));
		const auto target = Join(host, 999, "target", 1.0, 100.0);
		for (std::uint32_t number = 1; number <= 256; ++number) {
			const auto connection = 10 + number;
			const auto at = 2.0 + number * 0.1;
			Join(host, connection, "l" + std::to_string(1000 + number).substr(1), at, 100.0 + at);
			AssertResult(Send(host, connection, Create("Faction " + std::to_string(1000 + number).substr(1)), at + 0.01, 101.0 + at), connection, "create", true);
			AssertResult(Send(host, connection, Invite(target.playerId), at + 0.02, 101.0 + at), connection, "invite", true);
		}
		host.HandleDisconnected(999, 130.0);
		const auto rejoined = Join(host, 1000, "target", 30.0, 130.0);
		const auto state = RequirePacket(rejoined.hello, 1000, "factionState");
		assert(state.at("pendingInvites").size() == 256);
		assert(state.dump().size() < 256 * 1024);
	}

	return 0;
}
