#include "ServerFactionTestSupport.h"

#include "F4TSettlementAccess.h"

#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace FactionTestSupport;

namespace
{
	using F4T::Workshop::BuildZonePolicy;
	using F4T::Workshop::FormKey;

	std::size_t CountPackets(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		std::size_t count = 0;
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && DecodePacket(frame).value("type", std::string{}) == std::string{ a_type }) {
				++count;
			}
		}
		return count;
	}

	std::size_t PayloadBytes(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		std::size_t total = 0;
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && DecodePacket(frame).value("type", std::string{}) == std::string{ a_type }) {
				total += frame.payload.size();
			}
		}
		return total;
	}

	std::string ReadFileText(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		return std::string{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
	}

	BuildZonePolicy ZonePolicy(std::uint64_t a_zoneId)
	{
		return BuildZonePolicy{ a_zoneId, FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 10.0F, 2.0F, 4 };
	}

	Json UpdateAccess(const char* a_op, std::uint64_t a_zoneId, const std::string& a_targetProfileId)
	{
		return Json{ { "type", "settlementAccessUpdate" }, { "buildZoneId", a_zoneId }, { "op", a_op }, { "targetProfileId", a_targetProfileId } };
	}

	Json GetAccess(std::uint64_t a_zoneId)
	{
		return Json{ { "type", "settlementAccessGet" }, { "buildZoneId", a_zoneId } };
	}

	Json PlaceObject(std::uint64_t a_operationId, std::uint64_t a_revision, std::uint64_t a_zoneId, double a_x)
	{
		return Json{
			{ "type", "workshopOp" },
			{ "operationId", a_operationId },
			{ "expectedRevision", a_revision },
			{ "buildZoneId", a_zoneId },
			{ "kind", "place" },
			{ "form", Json{ { "plugin", "Fallout4.esm" }, { "localFormId", 0x1234 } } },
			{ "space", Json{ { "plugin", "Fallout4.esm" }, { "localFormId", 1 } } },
			{ "transform", Json{ { "x", a_x }, { "y", 0.0 }, { "z", 0.0 }, { "pitch", 0.0 }, { "yaw", 0.0 }, { "roll", 0.0 }, { "scale", 1.0 } } }
		};
	}

	void TestGrantAndRevokeBroadcastState()
	{
		ProtocolHost host{ "Settlement Access" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy(9)));
		Join(host, 10, "profile-owner", 1.0, 100.0);
		Join(host, 20, "profile-guest", 1.1, 100.2);
		Join(host, 30, "profile-stranger", 1.2, 100.4);

		std::string failure;
		const auto ownerFrames = host.SetSettlementOwner(9, "profile-owner", failure);
		assert(failure.empty());
		assert(RequirePacket(ownerFrames, 10, "settlementAccessState").at("ownerProfileId") == "profile-owner");
		assert(RequirePacket(ownerFrames, 20, "settlementAccessState").at("ownerProfileId") == "profile-owner");
		assert(RequirePacket(ownerFrames, 30, "settlementAccessState").at("ownerProfileId") == "profile-owner");

		const auto granted = Send(host, 10, UpdateAccess("grant", 9, "profile-guest"), 2.0, 102.0);
		assert(RequirePacket(granted, 10, "settlementAccessState").at("allowedProfileIds") == Json::array({ "profile-guest" }));
		assert(RequirePacket(granted, 20, "settlementAccessState").at("allowedProfileIds") == Json::array({ "profile-guest" }));
		assert(RequirePacket(granted, 30, "settlementAccessState").at("allowedProfileIds") == Json::array({ "profile-guest" }));

		const auto denied = Send(host, 30, UpdateAccess("grant", 9, "profile-stranger"), 2.0, 102.0);
		assert(RequirePacket(denied, 30, "settlementAccessReject").at("status") == "notOwner");

		const auto self = Send(host, 10, UpdateAccess("grant", 9, "profile-owner"), 3.0, 103.0);
		assert(RequirePacket(self, 10, "settlementAccessReject").at("status") == "invalidRequest");
		const auto repeated = Send(host, 10, UpdateAccess("grant", 9, "profile-guest"), 4.0, 104.0);
		assert(RequirePacket(repeated, 10, "settlementAccessReject").at("status") == "alreadyAllowed");
		const auto unlisted = Send(host, 10, UpdateAccess("revoke", 9, "profile-stranger"), 5.0, 105.0);
		assert(RequirePacket(unlisted, 10, "settlementAccessReject").at("status") == "notAllowed");
		const auto unknown = Send(host, 10, UpdateAccess("grant", 77, "profile-guest"), 6.0, 106.0);
		assert(RequirePacket(unknown, 10, "settlementAccessReject").at("status") == "unknownBuildZone");

		const auto revoked = Send(host, 10, UpdateAccess("revoke", 9, "profile-guest"), 7.0, 107.0);
		assert(RequirePacket(revoked, 10, "settlementAccessState").at("allowedProfileIds").empty());
		assert(RequirePacket(revoked, 20, "settlementAccessState").at("allowedProfileIds").empty());
		assert(RequirePacket(revoked, 30, "settlementAccessState").at("allowedProfileIds").empty());

		const auto before = host.Stats().packetsRejected;
		const auto malformed = Send(host, 10, Json{ { "type", "settlementAccessUpdate" }, { "buildZoneId", 9U }, { "op", "toggle" }, { "targetProfileId", "profile-guest" } }, 8.0, 108.0);
		assert(!HasPacket(malformed, 10, "settlementAccessState"));
		assert(!HasPacket(malformed, 10, "settlementAccessReject"));
		assert(host.Stats().packetsRejected == before + 1);
	}

	void TestGetAnswersSenderAndRejectsUnknownZone()
	{
		ProtocolHost host{ "Settlement Get" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy(9)));
		Join(host, 10, "profile-owner", 1.0, 100.0);
		Join(host, 20, "profile-guest", 1.1, 100.2);
		std::string failure;
		assert(host.SetSettlementOwner(9, "profile-owner", failure).size() == 2);
		assert(failure.empty());

		const auto state = Send(host, 20, GetAccess(9), 2.0, 102.0);
		assert(RequirePacket(state, 20, "settlementAccessState").at("ownerProfileId") == "profile-owner");
		assert(!HasPacket(state, 10, "settlementAccessState"));
		const auto unknown = Send(host, 20, GetAccess(77), 3.0, 103.0);
		assert(RequirePacket(unknown, 20, "settlementAccessReject").at("status") == "unknownBuildZone");
		std::string unknownFailure;
		assert(host.SetSettlementOwner(77, "profile-owner", unknownFailure).empty());
		assert(unknownFailure == "unknownBuildZone");
	}

	void TestRateLimitAllowsFourThenRefillsOnePerSecond()
	{
		ProtocolHost host{ "Settlement Rate" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy(9)));
		Join(host, 10, "profile-owner", 1.0, 100.0);
		std::string failure;
		assert(host.SetSettlementOwner(9, "profile-owner", failure).size() == 1);

		for (int index = 0; index < 4; ++index) {
			assert(RequirePacket(Send(host, 10, GetAccess(9), 50.0, 150.0), 10, "settlementAccessState").at("buildZoneId") == 9);
		}
		assert(RequirePacket(Send(host, 10, GetAccess(9), 50.0, 150.0), 10, "settlementAccessReject").at("status") == "rateLimited");
		assert(RequirePacket(Send(host, 10, GetAccess(9), 51.0, 151.0), 10, "settlementAccessState").at("buildZoneId") == 9);
		assert(RequirePacket(Send(host, 10, GetAccess(9), 51.0, 151.0), 10, "settlementAccessReject").at("status") == "rateLimited");
	}

	void TestAllowListHoldsFifteenProfiles()
	{
		ProtocolHost host{ "Settlement Cap" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy(9)));
		Join(host, 10, "profile-owner", 1.0, 100.0);
		std::string failure;
		assert(host.SetSettlementOwner(9, "profile-owner", failure).size() == 1);

		for (std::size_t index = 0; index < F4T::SettlementAccess::MaximumAllowedProfiles; ++index) {
			const auto seconds = 10.0 + static_cast<double>(index);
			const auto frames = Send(host, 10, UpdateAccess("grant", 9, "profile-guest-" + std::to_string(index)), seconds, 100.0 + seconds);
			assert(RequirePacket(frames, 10, "settlementAccessState").at("allowedProfileIds").size() == index + 1);
		}
		const auto full = Send(host, 10, UpdateAccess("grant", 9, "profile-guest-late"), 40.0, 140.0);
		assert(RequirePacket(full, 10, "settlementAccessReject").at("status") == "full");
	}

	void TestBuildListGatesWorkshopPlacement()
	{
		ProtocolHost host{ "Settlement Build List" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy(9)));
		assert(host.RegisterWorkshopBuildZone(ZonePolicy(10)));
		Join(host, 10, "profile-owner", 1.0, 100.0);
		Join(host, 20, "profile-guest", 1.1, 100.2);
		Join(host, 30, "profile-stranger", 1.2, 100.4);

		std::string failure;
		assert(host.SetSettlementOwner(9, "profile-owner", failure).size() == 3);
		assert(failure.empty());
		assert(!Send(host, 10, UpdateAccess("grant", 9, "profile-guest"), 2.0, 102.0).empty());

		assert(HasPacket(Send(host, 20, PlaceObject(1, 0, 9, 1.0), 3.0, 103.0), 20, "workshopCommit"));
		const auto blocked = Send(host, 30, PlaceObject(2, 1, 9, 2.0), 3.1, 103.1);
		assert(RequirePacket(blocked, 30, "workshopReject").at("status") == "notAuthorized");
		assert(!HasPacket(blocked, 30, "workshopCommit"));
		assert(HasPacket(Send(host, 30, PlaceObject(3, 0, 10, 1.0), 3.2, 103.2), 30, "workshopCommit"));

		assert(!Send(host, 10, UpdateAccess("revoke", 9, "profile-guest"), 4.0, 104.0).empty());
		const auto revoked = Send(host, 20, PlaceObject(4, 1, 9, 3.0), 5.0, 105.0);
		assert(RequirePacket(revoked, 20, "workshopReject").at("status") == "notAuthorized");

		assert(host.SetSettlementOwner(9, "", failure).size() == 3);
		assert(failure.empty());
		assert(HasPacket(Send(host, 30, PlaceObject(5, 1, 9, 3.0), 6.0, 106.0), 30, "workshopCommit"));
	}

	void TestAllowListSurvivesRestart()
	{
		const auto directory = TempDirectory("settlement-access-reload");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Settlement Access Persist" };
			assert(host.EnableSettlementAccess(&gate));
			assert(host.RegisterWorkshopBuildZone(ZonePolicy(9)));
			Join(host, 10, "profile-owner", 1.0, 100.0);
			std::string failure;
			assert(host.SetSettlementOwner(9, "profile-owner", failure).size() == 1);
			assert(!Send(host, 10, UpdateAccess("grant", 9, "profile-guest"), 2.0, 102.0).empty());
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Settlement Access Reload" };
			assert(host.EnableSettlementAccess(&gate));
			assert(host.RegisterWorkshopBuildZone(ZonePolicy(9)));
			Join(host, 10, "profile-owner", 1.0, 100.0);
			const auto state = RequirePacket(Send(host, 10, GetAccess(9), 3.0, 103.0), 10, "settlementAccessState");
			assert(state.at("ownerProfileId") == "profile-owner");
			assert(state.at("allowedProfileIds") == Json::array({ "profile-guest" }));
		}
		std::filesystem::remove_all(directory);
	}

	void TestGrantRollsBackOnSaveFailure()
	{
		const auto directory = TempDirectory("settlement-access-rollback");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Settlement Access Rollback" };
		assert(host.EnableSettlementAccess(&gate));
		assert(host.RegisterWorkshopBuildZone(ZonePolicy(9)));
		Join(host, 10, "profile-owner", 1.0, 100.0);
		std::string failure;
		assert(host.SetSettlementOwner(9, "profile-owner", failure).size() == 1);
		assert(!Send(host, 10, UpdateAccess("grant", 9, "profile-guest"), 2.0, 102.0).empty());
		store.WaitForCommits();
		std::filesystem::remove_all(directory);

		const auto rejected = Send(host, 10, UpdateAccess("grant", 9, "profile-stranger"), 3.0, 103.0);
		assert(RequirePacket(rejected, 10, "settlementAccessReject").at("status") == "persistenceUnavailable");
		const auto state = RequirePacket(Send(host, 10, GetAccess(9), 4.0, 104.0), 10, "settlementAccessState");
		assert(state.at("allowedProfileIds") == Json::array({ "profile-guest" }));
	}

	void TestSettlementListIsSentOnceOnJoin()
	{
		ProtocolHost host{ "Settlement List" };
		assert(host.RegisterWorkshopBuildZone(ZonePolicy(9)));
		const auto joined = Join(host, 10, "profile-owner", 1.0, 100.0);
		assert(CountPackets(joined.hello, 10, "settlementAccessState") == 1);
		const auto later = Send(host, 10, GetAccess(9), 2.0, 102.0);
		assert(CountPackets(later, 10, "settlementAccessState") == 1);
		assert(host.RegisterWorkshopBuildZone(ZonePolicy(10)));
		const auto extended = Send(host, 10, GetAccess(9), 3.0, 103.0);
		assert(CountPackets(extended, 10, "settlementAccessState") == 3);
	}

	void TestSettlementListFitsTheClientStateBudget()
	{
		ProtocolHost host{ "Settlement List Size" };
		const std::string owner{ "co-0123456789abcdef0123456789abcdef" };
		for (std::uint64_t zone = 1; zone <= 40; ++zone) {
			assert(host.RegisterWorkshopBuildZone(ZonePolicy(zone)));
		}
		Join(host, 10, "profile-owner", 1.0, 100.0);
		std::string failure;
		for (std::uint64_t zone = 1; zone <= 40; ++zone) {
			assert(!host.SetSettlementOwner(zone, owner, failure).empty());
			assert(failure.empty());
		}
		const auto joined = Join(host, 20, "co-fedcba9876543210fedcba9876543210", 2.0, 200.0);
		assert(CountPackets(joined.hello, 20, "settlementAccessState") == 40);
		assert(PayloadBytes(joined.hello, 20, "settlementAccessState") < 30 * 1024);
	}

	void TestSettlementListIsCapped()
	{
		ProtocolHost host{ "Settlement List Cap" };
		for (std::uint64_t zone = 1; zone <= 520; ++zone) {
			assert(host.RegisterWorkshopBuildZone(ZonePolicy(zone)));
		}
		const auto joined = Join(host, 10, "profile-observer", 1.0, 100.0);
		assert(CountPackets(joined.hello, 10, "settlementAccessState") == 512);
	}

	void TestCorruptStoredAccessFailsClosed()
	{
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };

		{
			const auto directory = TempDirectory("settlement-access-corrupt-object");
			const auto path = directory / "durable-state.json";
			{
				DurableStateFileStore store(path, clock, instanceId);
				store.SaveSettlementAccess(Json{ { "not", "a list" } });
				store.WaitForCommits();
			}
			{
				DurableStateFileStore store(path, clock, instanceId);
				PersistenceGate gate(store);
				ProtocolHost host{ "Settlement Access Corrupt Object" };
				assert(!host.EnableSettlementAccess(&gate));
				assert(host.RegisterWorkshopBuildZone(ZonePolicy(9)));
				const auto joined = Join(host, 10, "profile-owner", 1.0, 100.0);
				assert(CountPackets(joined.hello, 10, "settlementAccessState") == 0);
				const auto blocked = Send(host, 10, PlaceObject(1, 0, 9, 1.0), 2.0, 102.0);
				assert(RequirePacket(blocked, 10, "workshopReject").at("status") == "notAuthorized");
				std::string failure;
				assert(host.SetSettlementOwner(9, "profile-owner", failure).empty());
				assert(failure == "persistenceUnavailable");
				assert(RequirePacket(Send(host, 10, GetAccess(9), 3.0, 103.0), 10, "settlementAccessReject").at("status") == "persistenceUnavailable");
			}
			const auto stored = ReadFileText(path);
			assert(stored.find("\"not\"") != std::string::npos);
			assert(stored.find("profile-owner") == std::string::npos);
			std::filesystem::remove_all(directory);
		}

		{
			const auto directory = TempDirectory("settlement-access-corrupt-entries");
			const auto path = directory / "durable-state.json";
			{
				DurableStateFileStore store(path, clock, instanceId);
				store.SaveSettlementAccess(Json::array({ Json{
					{ "buildZoneId", 0 },
					{ "ownerProfileId", "profile-owner" },
					{ "allowedProfileIds", Json::array() } } }));
				store.WaitForCommits();
			}
			{
				DurableStateFileStore store(path, clock, instanceId);
				PersistenceGate gate(store);
				ProtocolHost host{ "Settlement Access Corrupt Entries" };
				assert(!host.EnableSettlementAccess(&gate));
				assert(host.RegisterWorkshopBuildZone(ZonePolicy(9)));
				const auto joined = Join(host, 10, "profile-owner", 1.0, 100.0);
				assert(CountPackets(joined.hello, 10, "settlementAccessState") == 0);
				const auto blocked = Send(host, 10, PlaceObject(2, 0, 9, 1.0), 2.0, 102.0);
				assert(RequirePacket(blocked, 10, "workshopReject").at("status") == "notAuthorized");
			}
			std::filesystem::remove_all(directory);
		}
	}
}

int main()
{
	TestGrantAndRevokeBroadcastState();
	TestGetAnswersSenderAndRejectsUnknownZone();
	TestRateLimitAllowsFourThenRefillsOnePerSecond();
	TestAllowListHoldsFifteenProfiles();
	TestBuildListGatesWorkshopPlacement();
	TestAllowListSurvivesRestart();
	TestGrantRollsBackOnSaveFailure();
	TestSettlementListIsSentOnceOnJoin();
	TestSettlementListFitsTheClientStateBudget();
	TestSettlementListIsCapped();
	TestCorruptStoredAccessFailsClosed();
	return 0;
}
