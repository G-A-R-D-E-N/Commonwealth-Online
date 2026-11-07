#include "F4TAdmission.h"
#include "IdentityGate.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using F4T::Identity::TokenHash;

	class FakeTokenService final : public F4T::Identity::TokenService
	{
	public:
		std::string CreateToken() override
		{
			return "admission-token-" + std::to_string(++counter);
		}

		TokenHash HashToken(std::string_view a_token) override
		{
			TokenHash hash{};
			for (std::size_t index = 0; index < hash.size(); ++index) {
				hash[index] = static_cast<std::uint8_t>(
					(index * 31u + a_token.size() * 13u + (a_token.empty() ? 0u : static_cast<unsigned char>(a_token[index % a_token.size()]))) & 0xffu);
			}
			return hash;
		}

		std::uint32_t counter{};
	};

	std::vector<std::uint8_t> Encode(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	Json Hello(std::string_view a_profileId)
	{
		return Json{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "clientVersion", "1.2.0" },
			{ "falloutRuntimeVersion", "1.11.240" },
			{ "profileId", a_profileId },
			{ "contentManifestFingerprint", std::string(64, 'b') },
			{ "capabilities", Json::array({ "interest-v1", "save-attestation-v1" }) },
			{ "steamId", "76561198000000001" },
			{ "nickname", "Courier Six" }
		};
	}
}

int main()
{
	using namespace F4T::Admission;

	AdmissionRegistry admissions;
	const auto unrouted = admissions.Begin({ .ip = "10.0.0.7" }, 1000.0, 0);
	assert(unrouted.admitted);
	assert(admissions.PendingCount() == 1);

	auto atCap = admissions.Begin({ .ip = "10.0.0.8" }, 1000.0, 16);
	assert(atCap.admitted);
	assert(admissions.Reject(atCap.ticketId, "capacity_deferred"));

	assert(admissions.Reject(unrouted.ticketId, "unrouted"));
	assert(admissions.PendingCount() == 0);

	FakeTokenService tokens;
	F4T::Identity::Requirements requirements;
	CommonwealthOnline::Server::IdentityGate gate(
		requirements,
		tokens,
		nullptr,
		[](std::string_view a_profileId) -> std::optional<std::string> {
			if (a_profileId == "banned-profile") {
				return std::string{ "Banned by administrator." };
			}
			return std::nullopt;
		});

	const auto ticket = admissions.Begin({ .ip = "10.0.0.9" }, 1001.0, 0);
	assert(ticket.admitted);
	const auto hello = Encode(Hello("profile-courier"));
	const auto accepted = gate.InspectInbound(31, hello, 1001.0, true);
	assert(accepted.forward);
	assert(accepted.profileId == "profile-courier");
	assert(accepted.steamId.empty());
	assert(accepted.claimedSteamId == "76561198000000001");
	assert(accepted.nickname == "Courier Six");
	assert(admissions.Claim(ticket.ticketId, accepted.steamId, accepted.nickname, accepted.profileId, "hello", 1001.5));
	const auto claimed = admissions.Find(ticket.ticketId, 1002.0);
	assert(claimed.has_value());
	assert(claimed->identity.steamId.empty());
	assert(claimed->identity.nickname == "Courier Six");
	assert(claimed->identity.profileId == "profile-courier");
	assert(claimed->identity.ip == "10.0.0.9");
	assert(claimed->deadline == 1031.5);
	assert(admissions.Commit(ticket.ticketId, 1003.0));
	assert(admissions.AdmittedCount() == 1);

	const auto bannedTicket = admissions.Begin({ .ip = "10.0.0.10" }, 1010.0, 1);
	assert(bannedTicket.admitted);
	const auto bannedHello = Encode(Hello("banned-profile"));
	const auto rejected = gate.InspectInbound(32, bannedHello, 1010.0, true);
	assert(!rejected.forward);
	assert(rejected.code == "profile_banned");
	assert(rejected.reason == "Banned by administrator.");
	assert(admissions.Reject(bannedTicket.ticketId, rejected.code));
	assert(admissions.RejectedCount() == 3);
	assert(!admissions.Find(bannedTicket.ticketId, 1010.0).has_value());
	assert(admissions.LastRejectReason() == "profile_banned");

	const auto staleTicket = admissions.Begin({ .ip = "10.0.0.11" }, 2000.0, 1);
	assert(staleTicket.admitted);
	assert(!admissions.IsExpired(staleTicket.ticketId, 2029.999));
	assert(admissions.Update(staleTicket.ticketId, "keepAlive", 2015.0));
	assert(admissions.IsExpired(staleTicket.ticketId, 2030.0));
	const auto swept = admissions.Sweep(2030.0);
	assert(swept.size() == 1);
	assert(swept.front().ticketId == staleTicket.ticketId);
	assert(swept.front().identity.ip == "10.0.0.11");
	assert(admissions.PendingCount() == 0);
	assert(admissions.TimedOutCount() == 1);

	const auto floodTicket = admissions.Begin({ .ip = "10.0.0.12" }, 3000.0, 0);
	assert(floodTicket.admitted);
	assert(admissions.Claim(floodTicket.ticketId, "", "", "profile-flood", "hello", 3001.0));
	for (double now = 3010.0; now < 3031.0; now += 10.0) {
		assert(admissions.Update(floodTicket.ticketId, "keepAlive", now));
	}
	assert(!admissions.Update(floodTicket.ticketId, "keepAlive", 3031.0));
	assert(admissions.IsExpired(floodTicket.ticketId, 3031.0));
	assert(admissions.TimedOutCount() == 2);
	assert(admissions.PendingCount() == 0);

	return 0;
}
