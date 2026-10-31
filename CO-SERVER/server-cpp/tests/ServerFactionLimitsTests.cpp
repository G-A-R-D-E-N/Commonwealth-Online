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

	return 0;
}
