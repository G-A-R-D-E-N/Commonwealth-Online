#include "F4TAdmission.h"

#include <cassert>
#include <cstdint>
#include <string>

int main()
{
	using namespace F4T::Admission;

	AdmissionRegistry registry{ 16, 30.0 };
	assert(registry.TimeoutSeconds() == 30.0);

	auto first = registry.Begin({ .steamId = {}, .nickname = {}, .ip = "10.0.0.1" }, 100.0, 0);
	assert(first.admitted);
	assert(first.ticketId != 0);
	assert(registry.PendingCount() == 1);

	auto second = registry.Begin({ .steamId = "76561198000000000", .nickname = "Ada", .ip = "10.0.0.2" }, 100.0, 1);
	assert(second.admitted);
	assert(second.ticketId != first.ticketId);
	assert(registry.PendingCount() == 2);

	const auto found = registry.Find(first.ticketId, 129.0);
	assert(found.has_value());
	assert(found->identity.ip == "10.0.0.1");
	assert(found->deadline == 130.0);
	assert(!registry.Find(first.ticketId, 130.0).has_value());
	assert(!registry.IsExpired(first.ticketId, 129.999));
	assert(registry.IsExpired(first.ticketId, 130.0));

	assert(registry.Update(first.ticketId, "keepAlive", 125.0));
	assert(registry.Find(first.ticketId, 129.0)->deadline == 130.0);
	assert(registry.Update(first.ticketId, "keepAlive", 126.0));
	assert(registry.Find(first.ticketId, 129.0)->lastMessage == "keepAlive");

	assert(registry.Claim(second.ticketId, "76561198000000000", "Ada Lovelace", "profile-ada", "hello", 110.0));
	const auto claimed = registry.Find(second.ticketId, 111.0);
	assert(claimed.has_value());
	assert(claimed->identity.steamId == "76561198000000000");
	assert(claimed->identity.nickname == "Ada Lovelace");
	assert(claimed->identity.profileId == "profile-ada");
	assert(claimed->deadline == 140.0);

	assert(!registry.Update(9999, "unknown", 100.0));
	assert(!registry.Claim(9999, "1", "x", "y", "hello", 100.0));
	assert(!registry.Commit(9999, 100.0));
	assert(!registry.Reject(9999, "unknown"));

	assert(registry.Commit(second.ticketId, 120.0));
	assert(registry.AdmittedCount() == 1);
	assert(registry.PendingCount() == 1);
	assert(!registry.Commit(second.ticketId, 120.0));

	assert(registry.Reject(first.ticketId, "identity_rejected"));
	assert(registry.RejectedCount() == 1);
	assert(registry.PendingCount() == 0);

	auto atCap = registry.Begin({ .ip = "10.0.0.9" }, 200.0, 16);
	assert(atCap.admitted);
	assert(registry.Reject(atCap.ticketId, "capacity_deferred"));

	AdmissionRegistry zero{ 0 };
	auto zeroDenied = zero.Begin({ .ip = "10.0.0.1" }, 0.0, 0);
	assert(!zeroDenied.admitted);
	assert(zeroDenied.code == "server_full");

	auto invalid = registry.Begin({ .ip = {} }, 200.0, 0);
	assert(!invalid.admitted);
	assert(invalid.code == "admission_invalid");

	AdmissionRegistry expiring{ 16, 30.0 };
	auto expiringTicket = expiring.Begin({ .ip = "10.0.0.3" }, 500.0, 0);
	assert(expiringTicket.admitted);
	assert(expiring.PendingCount() == 1);
	auto swept = expiring.Sweep(531.0);
	assert(swept.size() == 1);
	assert(swept.front().ticketId == expiringTicket.ticketId);
	assert(swept.front().identity.ip == "10.0.0.3");
	assert(expiring.PendingCount() == 0);
	assert(expiring.TimedOutCount() == 1);

	auto expiredCommit = expiring.Begin({ .ip = "10.0.0.4" }, 600.0, 0);
	assert(expiredCommit.admitted);
	assert(!expiring.Commit(expiredCommit.ticketId, 631.0));
	assert(expiring.TimedOutCount() == 2);
	assert(expiring.PendingCount() == 0);

	AdmissionRegistry bounded{ 16, 30.0 };
	for (std::size_t index = 0; index < 32; ++index) {
		assert(bounded.Begin({ .ip = "10.0.0.5" }, 1.0, 0).admitted);
	}
	auto over = bounded.Begin({ .ip = "10.0.0.6" }, 1.0, 0);
	assert(!over.admitted);
	assert(over.code == "admission_busy");

	for (std::uint64_t ticket = 1; ticket <= 8; ++ticket) {
		assert(bounded.Commit(ticket, 2.0));
	}
	assert(bounded.AdmittedCount() == 8);
	assert(bounded.PendingCount() == 24);

	AdmissionRegistry flooded{ 16, 30.0, 60.0 };
	const auto floodTicket = flooded.Begin({ .ip = "10.0.0.12" }, 1000.0, 0);
	assert(floodTicket.admitted);
	for (double now = 1005.0; now < 1030.0; now += 5.0) {
		assert(flooded.Update(floodTicket.ticketId, "keepAlive", now));
	}
	assert(flooded.IsExpired(floodTicket.ticketId, 1030.0));
	assert(!flooded.Update(floodTicket.ticketId, "keepAlive", 1030.0));
	assert(flooded.IsExpired(floodTicket.ticketId, 1030.0));
	assert(flooded.TimedOutCount() == 1);
	assert(flooded.PendingCount() == 0);

	AdmissionRegistry capped{ 16, 30.0, 60.0 };
	const auto helloTicket = capped.Begin({ .ip = "10.0.0.13" }, 2000.0, 0);
	assert(helloTicket.admitted);
	assert(capped.Claim(helloTicket.ticketId, "76561198000000003", "Replay", "profile-replay", "hello", 2005.0));
	assert(capped.Claim(helloTicket.ticketId, "", "", "", "hello", 2020.0));
	assert(capped.Find(helloTicket.ticketId, 2049.0)->deadline == 2050.0);
	assert(capped.Claim(helloTicket.ticketId, "", "", "", "hello", 2049.0));
	assert(capped.Find(helloTicket.ticketId, 2059.0)->deadline == 2060.0);
	assert(!capped.Claim(helloTicket.ticketId, "", "", "", "hello", 2060.0));
	assert(capped.TimedOutCount() == 1);

	const auto rejectTicket = capped.Begin({ .ip = "10.0.0.14" }, 2100.0, 0);
	assert(rejectTicket.admitted);
	assert(capped.LastRejectReason().empty());
	assert(capped.Reject(rejectTicket.ticketId, "profile_banned"));
	assert(capped.LastRejectReason() == "profile_banned");

	const auto cancelTicket = capped.Begin({ .ip = "10.0.0.15" }, 2200.0, 0);
	assert(cancelTicket.admitted);
	const auto rejectedBeforeCancel = capped.RejectedCount();
	assert(capped.Cancel(cancelTicket.ticketId));
	assert(capped.RejectedCount() == rejectedBeforeCancel);
	assert(capped.LastRejectReason() == "profile_banned");
	assert(!capped.Cancel(cancelTicket.ticketId));
	assert(!capped.Cancel(9999));
	assert(!capped.Find(cancelTicket.ticketId, 2201.0).has_value());
	assert(!capped.Commit(cancelTicket.ticketId, 2201.0));
	assert(!capped.Reject(cancelTicket.ticketId, "connection_closed"));

	return 0;
}
