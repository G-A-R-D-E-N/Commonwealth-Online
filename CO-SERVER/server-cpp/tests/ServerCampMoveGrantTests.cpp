#include "DurableStateFileStore.h"
#include "F4TWorkshopCamps.h"
#include "PersistenceGate.h"
#include "ServerProtocol.h"
#include "StartingGrantFixture.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::DurableStateFileStore;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::PersistenceGate;
	using CommonwealthOnline::Server::ProtocolHost;
	using namespace F4T::Drops;

	constexpr std::uint32_t kPieceForm = 0x1234;
	constexpr std::uint32_t kFirstComponent = 0x7301;
	constexpr int kComponentCount = 5;

	std::vector<std::uint8_t> Encode(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	Json Decode(const OutboundFrame& a_frame)
	{
		return Json::parse(std::string{ reinterpret_cast<const char*>(a_frame.payload.data()), a_frame.payload.size() });
	}

	const OutboundFrame* Find(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && !frame.payload.empty() && frame.payload.front() == '{' &&
				Decode(frame).value("type", std::string{}) == a_type) {
				return &frame;
			}
		}
		return nullptr;
	}

	std::vector<OutboundFrame> Send(ProtocolHost& a_host, std::uint32_t a_connectionId, const Json& a_packet, double a_monotonic)
	{
		return a_host.HandleMessage(a_connectionId, Encode(a_packet), a_monotonic, a_monotonic + 1000.0);
	}

	void Join(ProtocolHost& a_host, std::uint32_t a_connectionId, const char* a_profile, double a_monotonic)
	{
		a_host.HandleConnected(a_connectionId, a_monotonic);
		assert(!a_host.HandleMessage(a_connectionId, Encode(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), a_monotonic + 0.1, a_monotonic + 1000.1).empty());
		a_host.BindSessionProfile(a_connectionId, a_profile);
	}

	Json TransformAt(double a_x)
	{
		return Json{ { "type", "transform" }, { "cellId", "CAFE" }, { "worldspaceId", "F00D" }, { "x", a_x }, { "y", 0.0 }, { "z", 0.0 },
			{ "angleZ", 0.0 }, { "movementType", "normal" } };
	}

	Json Place(std::uint64_t a_operationId, std::uint64_t a_revision, std::uint64_t a_zoneId, double a_x)
	{
		return Json{ { "type", "workshopOp" }, { "operationId", a_operationId }, { "expectedRevision", a_revision }, { "buildZoneId", a_zoneId },
			{ "kind", "place" }, { "form", Json{ { "plugin", "Fallout4.esm" }, { "localFormId", kPieceForm } } },
			{ "space", Json{ { "plugin", "Fallout4.esm" }, { "localFormId", 0xF00D } } },
			{ "transform", Json{ { "x", a_x }, { "y", 0.0 }, { "z", 0.0 }, { "pitch", 0.0 }, { "yaw", 0.0 }, { "roll", 0.0 }, { "scale", 1.0 } } } };
	}

	Json FoundAt(std::uint64_t a_operationId, std::uint64_t a_dropEntityId, double a_x)
	{
		return Json{ { "type", "campFound" }, { "operationId", a_operationId }, { "dropEntityId", a_dropEntityId },
			{ "space", Json{ { "plugin", "Fallout4.esm" }, { "localFormId", 0xF00D } } }, { "x", a_x }, { "y", 0.0 }, { "z", 0.0 },
			{ "probe", Json{ { "height", 0.0 }, { "slopeDegrees", 0.0 }, { "navmeshHit", true } } } };
	}

	Json MoveTo(std::uint64_t a_operationId, std::uint64_t a_zoneId, double a_x)
	{
		return Json{ { "type", "campMove" }, { "operationId", a_operationId }, { "buildZoneId", a_zoneId },
			{ "space", Json{ { "plugin", "Fallout4.esm" }, { "localFormId", 0xF00D } } }, { "x", a_x }, { "y", 0.0 }, { "z", 0.0 },
			{ "probe", Json{ { "height", 0.0 }, { "slopeDegrees", 0.0 }, { "navmeshHit", true } } } };
	}

	std::string ComponentKey(int a_index)
	{
		char form[9]{};
		std::snprintf(form, sizeof(form), "%08X", kFirstComponent + static_cast<std::uint32_t>(a_index));
		return std::string{ "fallout4.esm:" } + form;
	}

	enum class Costs { Priced, OtherPieceOnly, Missing, Empty };

	void WriteConfiguration(const std::filesystem::path& a_directory, Costs a_costs)
	{
		std::filesystem::create_directories(a_directory);
		{
			std::ofstream output(a_directory / "workshop-zones.json", std::ios::binary);
			output << R"({"version":1,"zones":[]})";
		}
		if (a_costs == Costs::Missing) {
			return;
		}
		Json components = Json::array();
		for (int index = 0; index < kComponentCount; ++index) {
			components.push_back(Json{ { "plugin", "fallout4.esm" }, { "localFormId", kFirstComponent + index }, { "count", index + 1 } });
		}
		Json priced = Json::array();
		if (a_costs == Costs::Priced || a_costs == Costs::OtherPieceOnly) {
			priced.push_back(Json{ { "plugin", "fallout4.esm" }, { "localFormId", a_costs == Costs::Priced ? kPieceForm : kPieceForm + 1 }, { "components", components } });
		}
		const Json costs{ { "format", 1 }, { "costs", priced } };
		std::ofstream output(a_directory / "workshop-costs.json", std::ios::binary);
		output << costs.dump();
	}

	struct Fixture
	{
		std::filesystem::path directory;
		DurableStateFileStore store;
		PersistenceGate gate;
		ProtocolHost host;
		std::uint64_t zoneId{};
		std::uint64_t nextDrop{ kFirstDroppedEntityId };

		static std::filesystem::path MakeDirectory(const char* a_name, Costs a_costs)
		{
			auto directory = std::filesystem::temp_directory_path() / (std::string{ a_name } + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			WriteConfiguration(directory, a_costs);
			return directory;
		}

		explicit Fixture(const char* a_name, std::uint32_t a_kits = 1, Costs a_costs = Costs::Priced) :
			directory(MakeDirectory(a_name, a_costs)),
			store(directory / "state.json", [] { return 1000.0; }, [] { return "0123456789abcdef0123456789abcdef"; }),
			gate(store),
			host("Camp grant")
		{
			F4T::Identity::TokenHash hash{};
			store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
			store.SaveResumeCredential("profile-b", hash, std::nullopt, true, 5000.0);
			(void)store.InitializeInventory("profile-a", Json::array({ Json{ { "plugin", "CommonwealthOnline.esp" }, { "localFormId", "997F" }, { "count", a_kits } } }));
			(void)ReconcileStartingGrantFixture(store, "profile-a");
			assert(host.EnableInventoryTransactions(gate));
			assert(host.EnableSettlementAccess(&gate));
			assert(host.ConfigureWorkshop(&gate, directory / "workshop-zones.json") == 0);
		}

		~Fixture() { std::filesystem::remove_all(directory); }

		void DropKit(const std::string& a_operation)
		{
			const auto revision = store.GetInventory("profile-a")->at("revision").get<std::uint64_t>();
			const Json drop{ { "type", "inventoryDropRequest" }, { "operationId", a_operation }, { "expectedRevision", revision },
				{ "itemKey", "commonwealthonline.esp:0000997F" }, { "count", 1u } };
			assert(store.DropInventory("profile-a", drop, InventoryPosition{ "CAFE", "F00D", 0, 0, 0 }).outcome == Outcome::Committed);
		}

		std::uint64_t BuildCamp(int a_pieces)
		{
			DropKit("kit");
			Join(host, 10, "profile-a", 1.0);
			(void)Send(host, 10, TransformAt(0.0), 2.0);
			const auto created = Send(host, 10, FoundAt(1, nextDrop++, 0.0), 2.1);
			const auto* createdFrame = Find(created, 10, "campCreated");
			assert(createdFrame != nullptr);
			zoneId = Decode(*createdFrame).at("buildZoneId").get<std::uint64_t>();
			for (int index = 0; index < a_pieces; ++index) {
				const auto placed = Send(host, 10, Place(3 + index, static_cast<std::uint64_t>(index), zoneId, 1.0 + index), 2.2 + 0.1 * index);
				assert(Find(placed, 10, "workshopCommit") != nullptr);
			}
			return zoneId;
		}

		std::size_t PieceCount() const
		{
			return store.GetWorkshop().at("zones").at(std::to_string(zoneId)).at("objects").size();
		}

		std::uint64_t Revision() const { return store.GetInventory("profile-a")->at("revision").get<std::uint64_t>(); }

		std::uint64_t Held(const std::string& a_key) const
		{
			const auto inventory = store.GetInventory("profile-a");
			const auto& items = inventory->at("items");
			const auto found = items.find(a_key);
			return found == items.end() ? 0U : found->at("count").get<std::uint64_t>();
		}

		std::vector<OutboundFrame> Move(std::uint64_t a_operationId, std::uint32_t a_connectionId = 10)
		{
			const auto x = -10.0 * F4T::WorkshopCamps::UnitsPerMeter;
			(void)Send(host, a_connectionId, TransformAt(x), 3999.9);
			return Send(host, a_connectionId, MoveTo(a_operationId, zoneId, x), 4000.0);
		}
	};

	void AssertHalfRefund(const Json& a_grant)
	{
		assert(a_grant.at("chunks").size() == 1);
		const auto& lines = a_grant.at("chunks")[0].at("lines");
		assert(lines.size() == kComponentCount);
		for (int index = 0; index < kComponentCount; ++index) {
			const auto total = 3 * (index + 1);
			bool matched = false;
			for (const auto& line : lines) {
				if (line.at("itemKey") == ComponentKey(index)) {
					assert(line.at("count") == total / 2);
					matched = true;
				}
			}
			assert(matched);
		}
	}

	void TestMoveCullsAllAndGrantsHalfOnce()
	{
		Fixture fixture("co-camp-grant-");
		fixture.BuildCamp(3);
		assert(fixture.PieceCount() == 3);
		const auto before = fixture.Revision();
		const auto moved = fixture.Move(10);
		assert(Find(moved, 10, "campMoved") != nullptr && Find(moved, 10, "campRejected") == nullptr);
		const auto refund = Decode(*Find(moved, 10, "campRefund"));
		assert(refund.at("removedPieces") == 3 && refund.at("unpricedPieces") == 0 && refund.at("items").size() == kComponentCount);
		const auto grant = Decode(*Find(moved, 10, "campGrant"));
		assert(grant.at("operationId") == 10);
		AssertHalfRefund(grant);
		assert(fixture.PieceCount() == 0);
		assert(fixture.Revision() == before + 1);
		for (int index = 0; index < kComponentCount; ++index) {
			assert(fixture.Held(ComponentKey(index)) == static_cast<std::uint64_t>(3 * (index + 1) / 2));
		}
		assert(fixture.store.PendingCampGrants("profile-a").size() == 1);

		const auto repeated = fixture.Move(10);
		assert(Find(repeated, 10, "campGrant") != nullptr);
		assert(fixture.Revision() == before + 1);
		for (int index = 0; index < kComponentCount; ++index) {
			assert(fixture.Held(ComponentKey(index)) == static_cast<std::uint64_t>(3 * (index + 1) / 2));
		}
	}

	void TestGrantIsResentAfterReconnectUntilAcked()
	{
		Fixture fixture("co-camp-resend-");
		fixture.BuildCamp(3);
		const auto moved = fixture.Move(10);
		AssertHalfRefund(Decode(*Find(moved, 10, "campGrant")));
		const auto settled = fixture.Revision();
		(void)fixture.host.HandleDisconnected(10, 4100.0);

		Join(fixture.host, 12, "profile-a", 4200.0);
		const auto resent = Send(fixture.host, 12, TransformAt(0.0), 4200.5);
		const auto* grantFrame = Find(resent, 12, "campGrant");
		assert(grantFrame != nullptr);
		AssertHalfRefund(Decode(*grantFrame));
		assert(Decode(*grantFrame).at("operationId") == 10);
		assert(fixture.Revision() == settled);

		const auto acked = Send(fixture.host, 12, Json{ { "type", "campGrantAck" }, { "operationId", 10 } }, 4201.0);
		assert(Find(acked, 12, "campRejected") == nullptr);
		assert(fixture.store.PendingCampGrants("profile-a").empty());
		(void)fixture.host.HandleDisconnected(12, 4300.0);

		Join(fixture.host, 13, "profile-a", 4400.0);
		const auto quiet = Send(fixture.host, 13, TransformAt(0.0), 4400.5);
		assert(Find(quiet, 13, "campGrant") == nullptr);
		assert(fixture.Revision() == settled);
		const auto replayed = Send(fixture.host, 13, MoveTo(10, fixture.zoneId, -10.0 * F4T::WorkshopCamps::UnitsPerMeter), 4401.0);
		assert(Find(replayed, 13, "campGrant") == nullptr);
		assert(fixture.Revision() == settled);
	}

	void TestFailedCommitLeavesNothing()
	{
		Fixture fixture("co-camp-rollback-");
		fixture.BuildCamp(3);
		const auto backup = std::filesystem::path{ (fixture.directory / "state.json").string() + ".bak" };
		const auto oldWorkshop = fixture.store.GetWorkshop();
		const auto oldInventory = fixture.store.GetInventory("profile-a");
		std::filesystem::remove(backup);
		std::filesystem::create_directories(backup / "occupied");
		const auto failed = fixture.Move(10);
		const auto* rejected = Find(failed, 10, "campRejected");
		assert(rejected != nullptr && Decode(*rejected).at("status") == "grantFailed");
		assert(Find(failed, 10, "campGrant") == nullptr && Find(failed, 10, "campMoved") == nullptr);
		assert(fixture.store.GetWorkshop() == oldWorkshop);
		assert(fixture.store.GetInventory("profile-a") == oldInventory);
		assert(fixture.store.PendingCampGrants("profile-a").empty());
		assert(fixture.PieceCount() == 3);
		std::filesystem::remove_all(backup);

		const auto retried = fixture.Move(10);
		assert(Find(retried, 10, "campGrant") != nullptr);
		assert(fixture.PieceCount() == 0);
	}

	void TestUnpricedPiecesAreCulledWithoutGrant()
	{
		Fixture fixture("co-camp-unpriced-", 1, Costs::OtherPieceOnly);
		fixture.BuildCamp(3);
		const auto before = fixture.Revision();
		const auto moved = fixture.Move(10);
		assert(Find(moved, 10, "campMoved") != nullptr);
		assert(Decode(*Find(moved, 10, "campRefund")).at("unpricedPieces") == 3);
		assert(Find(moved, 10, "campGrant") == nullptr);
		assert(fixture.store.PendingCampGrants("profile-a").empty());
		assert(fixture.PieceCount() == 0 && fixture.Revision() == before);
	}

	void TestStoreThenRefoundCullsAllAndGrantsHalf()
	{
		Fixture fixture("co-camp-refound-");
		fixture.BuildCamp(3);
		assert(Find(Send(fixture.host, 10, Json{ { "type", "campStore" }, { "operationId", 2 }, { "buildZoneId", fixture.zoneId } }, 2.3), 10, "campStored") != nullptr);
		assert(fixture.PieceCount() == 3);
		const auto x = -10.0 * F4T::WorkshopCamps::UnitsPerMeter;
		(void)Send(fixture.host, 10, TransformAt(x), 2.4);
		const auto early = Send(fixture.host, 10, FoundAt(4, kFirstDroppedEntityId + 1, x), 2.5);
		const auto* cooling = Find(early, 10, "campRejected");
		assert(cooling != nullptr && Decode(*cooling).at("status") == "cooldown");
		assert(!fixture.store.GetLiveDrop(kFirstDroppedEntityId + 1).is_null());
		(void)Send(fixture.host, 10, TransformAt(x), 3699.9);
		const auto refound = Send(fixture.host, 10, FoundAt(4, kFirstDroppedEntityId + 1, x), 3700.0);
		assert(Find(refound, 10, "campCreated") != nullptr && Find(refound, 10, "campRejected") == nullptr);
		const auto refund = Decode(*Find(refound, 10, "campRefund"));
		assert(refund.at("removedPieces") == 3 && refund.at("unpricedPieces") == 0);
		AssertHalfRefund(Decode(*Find(refound, 10, "campGrant")));
		assert(fixture.PieceCount() == 0);
		assert(fixture.store.GetLiveDrop(kFirstDroppedEntityId + 1).is_null());
		assert(fixture.store.PendingCampGrants("profile-a").size() == 1);
		for (int index = 0; index < kComponentCount; ++index) {
			assert(fixture.Held(ComponentKey(index)) == static_cast<std::uint64_t>(3 * (index + 1) / 2));
		}
	}

	void TestMoveOnStoredCampIsRefused()
	{
		Fixture fixture("co-camp-stored-move-");
		fixture.BuildCamp(3);
		assert(Find(Send(fixture.host, 10, Json{ { "type", "campStore" }, { "operationId", 2 }, { "buildZoneId", fixture.zoneId } }, 2.3), 10, "campStored") != nullptr);
		const auto moved = fixture.Move(5);
		const auto* rejected = Find(moved, 10, "campRejected");
		assert(rejected != nullptr && Decode(*rejected).at("status") == "notFound");
		assert(Find(moved, 10, "campMoved") == nullptr && Find(moved, 10, "campGrant") == nullptr);
		assert(fixture.PieceCount() == 3);
		assert(fixture.store.PendingCampGrants("profile-a").empty());
	}

	void TestCampMoveAndReactivationRefuseWithoutPrices()
	{
		for (const auto costs : { Costs::Missing, Costs::Empty }) {
			Fixture fixture("co-camp-no-prices-", 1, costs);
			fixture.BuildCamp(3);
			const auto before = fixture.Revision();
			const auto moved = fixture.Move(10);
			const auto* rejected = Find(moved, 10, "campRejected");
			assert(rejected != nullptr && Decode(*rejected).at("status") == "refundUnavailable");
			assert(Find(moved, 10, "campMoved") == nullptr && Find(moved, 10, "campGrant") == nullptr);
			assert(fixture.PieceCount() == 3 && fixture.Revision() == before);
		}
		for (const auto costs : { Costs::Missing, Costs::Empty }) {
			Fixture fixture("co-camp-no-prices-reactivate-", 1, costs);
			fixture.BuildCamp(3);
			assert(Find(Send(fixture.host, 10, Json{ { "type", "campStore" }, { "operationId", 2 }, { "buildZoneId", fixture.zoneId } }, 2.3), 10, "campStored") != nullptr);
			const auto x = -10.0 * F4T::WorkshopCamps::UnitsPerMeter;
			(void)Send(fixture.host, 10, TransformAt(x), 3699.9);
			const auto refused = Send(fixture.host, 10, FoundAt(4, kFirstDroppedEntityId + 1, x), 3700.0);
			const auto* rejected = Find(refused, 10, "campRejected");
			assert(rejected != nullptr && Decode(*rejected).at("status") == "refundUnavailable");
			assert(Find(refused, 10, "campCreated") == nullptr && Find(refused, 10, "campGrant") == nullptr);
			assert(!fixture.store.GetLiveDrop(kFirstDroppedEntityId + 1).is_null());
			assert(fixture.PieceCount() == 3);
		}
	}

	void TestUngroundedProbeIsTerrain()
	{
		Fixture fixture("co-camp-probe-");
		fixture.DropKit("kit");
		Join(fixture.host, 10, "profile-a", 1.0);
		(void)Send(fixture.host, 10, TransformAt(0.0), 2.0);
		auto airborne = FoundAt(1, kFirstDroppedEntityId, 0.0);
		airborne["probe"]["height"] = 200.0;
		const auto high = Send(fixture.host, 10, airborne, 2.1);
		assert(Find(high, 10, "campRejected") != nullptr && Decode(*Find(high, 10, "campRejected")).at("status") == "terrain");
		auto noGround = FoundAt(2, kFirstDroppedEntityId, 0.0);
		noGround["probe"]["navmeshHit"] = false;
		const auto missing = Send(fixture.host, 10, noGround, 2.2);
		assert(Find(missing, 10, "campRejected") != nullptr && Decode(*Find(missing, 10, "campRejected")).at("status") == "terrain");
		assert(!fixture.store.GetLiveDrop(kFirstDroppedEntityId).is_null());
	}

	void TestNonOwnerStoreIsRefusedWithoutChange()
	{
		Fixture fixture("co-camp-notowner-");
		fixture.BuildCamp(2);
		Join(fixture.host, 11, "profile-b", 1.0);
		(void)Send(fixture.host, 11, TransformAt(0.0), 2.0);
		const auto before = fixture.store.GetWorkshop();
		const auto drops = fixture.store.GetLiveDrop(kFirstDroppedEntityId + 1);
		const auto refused = Send(fixture.host, 11, Json{ { "type", "campStore" }, { "operationId", 90 }, { "buildZoneId", fixture.zoneId } }, 2.5);
		const auto* rejected = Find(refused, 11, "campRejected");
		assert(rejected != nullptr && Decode(*rejected).at("status") == "notOwner");
		assert(Find(refused, 11, "campStored") == nullptr && Find(refused, 10, "campStored") == nullptr);
		assert(fixture.store.GetWorkshop() == before);
		assert(fixture.store.GetLiveDrop(kFirstDroppedEntityId + 1) == drops);
		assert(fixture.PieceCount() == 2);
	}

	void TestStoreNeedsTheOwnerNearTheCamp()
	{
		Fixture fixture("co-camp-store-reach-");
		fixture.BuildCamp(2);
		(void)Send(fixture.host, 10, TransformAt(-10.0 * F4T::WorkshopCamps::UnitsPerMeter), 2.5);
		const auto before = fixture.store.GetWorkshop();
		const auto far = Send(fixture.host, 10, Json{ { "type", "campStore" }, { "operationId", 2 }, { "buildZoneId", fixture.zoneId } }, 2.6);
		const auto* rejected = Find(far, 10, "campRejected");
		assert(rejected != nullptr && Decode(*rejected).at("status") == "tooFar");
		assert(Find(far, 10, "campStored") == nullptr);
		assert(fixture.store.GetWorkshop() == before);
		assert(fixture.PieceCount() == 2);
		assert(fixture.store.GetLiveDrop(kFirstDroppedEntityId + 1).is_null());

		(void)Send(fixture.host, 10, TransformAt(-5.0 * F4T::WorkshopCamps::UnitsPerMeter), 2.7);
		assert(Find(Send(fixture.host, 10, Json{ { "type", "campStore" }, { "operationId", 3 }, { "buildZoneId", fixture.zoneId } }, 2.8), 10, "campStored") != nullptr);
		assert(!fixture.store.GetLiveDrop(kFirstDroppedEntityId + 1).is_null());
	}

	void TestReplayedMoveIsNotClaimedForAnotherOwnersCamp()
	{
		Fixture fixture("co-camp-replay-owner-");
		fixture.BuildCamp(3);
		assert(Find(fixture.Move(10), 10, "campMoved") != nullptr);
		(void)fixture.store.InitializeInventory("profile-b", Json::array({ Json{ { "plugin", "CommonwealthOnline.esp" }, { "localFormId", "997F" }, { "count", 1u } } }));
		(void)ReconcileStartingGrantFixture(fixture.store, "profile-b");
		const auto kitRevision = fixture.store.GetInventory("profile-b")->at("revision").get<std::uint64_t>();
		const Json kitDrop{ { "type", "inventoryDropRequest" }, { "operationId", "kit-b" }, { "expectedRevision", kitRevision },
			{ "itemKey", "commonwealthonline.esp:0000997F" }, { "count", 1u } };
		assert(fixture.store.DropInventory("profile-b", kitDrop, InventoryPosition{ "CAFE", "F00D", 0, 0, 0 }).outcome == Outcome::Committed);
		Join(fixture.host, 11, "profile-b", 1.0);
		(void)Send(fixture.host, 11, TransformAt(20000.0), 2.0);
		const auto created = Send(fixture.host, 11, FoundAt(50, fixture.nextDrop++, 20000.0), 2.1);
		const auto* createdFrame = Find(created, 11, "campCreated");
		assert(createdFrame != nullptr);
		const auto otherZone = Decode(*createdFrame).at("buildZoneId").get<std::uint64_t>();

		const auto replayed = Send(fixture.host, 10, MoveTo(10, otherZone, 0.0), 4500.0);
		const auto* rejected = Find(replayed, 10, "campRejected");
		assert(rejected != nullptr && Decode(*rejected).at("status") == "notOwner");
		assert(Find(replayed, 10, "campMoved") == nullptr);

		const auto again = fixture.Move(10);
		assert(Decode(*Find(again, 10, "campMoved")).at("own") == true);
	}

	void TestKitDropCapGivesDistinctStatus()
	{
		Fixture fixture("co-camp-droplimit-", static_cast<std::uint32_t>(kMaximumLiveDropsPerPlayer) + 4);
		fixture.BuildCamp(0);
		for (std::size_t index = 0; index < kMaximumLiveDropsPerPlayer; ++index) {
			fixture.DropKit("fill-" + std::to_string(index));
		}
		const auto before = fixture.store.GetWorkshop();
		const auto refused = Send(fixture.host, 10, Json{ { "type", "campStore" }, { "operationId", 91 }, { "buildZoneId", fixture.zoneId } }, 2.5);
		const auto* rejected = Find(refused, 10, "campRejected");
		assert(rejected != nullptr && Decode(*rejected).at("status") == "dropLimit");
		assert(fixture.store.GetWorkshop() == before);
		assert(Find(refused, 10, "campStored") == nullptr);
	}

	void TestRemoveRefundsTheKitOnce()
	{
		Fixture fixture("co-camp-remove-");
		fixture.BuildCamp(2);
		const auto kit = std::string(F4T::WorkshopCamps::CampKitItemKey);
		const auto held = fixture.Held(kit);
		const auto removed = Send(fixture.host, 10, Json{ { "type", "campRemove" }, { "operationId", 60 }, { "buildZoneId", fixture.zoneId } }, 2.4);
		assert(Find(removed, 10, "campRemoved") != nullptr && Find(removed, 10, "campRejected") == nullptr);
		const auto* grantFrame = Find(removed, 10, "campGrant");
		assert(grantFrame != nullptr);
		const auto grant = Decode(*grantFrame);
		const auto& lines = grant.at("chunks")[0].at("lines");
		assert(grant.at("chunks").size() == 1 && lines.size() == 1);
		assert(lines[0].at("itemKey") == kit && lines[0].at("count") == 1);
		assert(fixture.Held(kit) == held + 1);
		assert(fixture.store.PendingCampGrants("profile-a").size() == 1);
		const auto again = Send(fixture.host, 10, Json{ { "type", "campRemove" }, { "operationId", 61 }, { "buildZoneId", fixture.zoneId } }, 2.5);
		const auto* rejected = Find(again, 10, "campRejected");
		assert(rejected != nullptr && Decode(*rejected).at("status") == "notFound");
		assert(Find(again, 10, "campGrant") == nullptr);
		assert(fixture.Held(kit) == held + 1);
		assert(fixture.store.PendingCampGrants("profile-a").size() == 1);
	}

	void TestRemovingAStoredCampGivesNoSecondKit()
	{
		Fixture fixture("co-camp-stored-remove-");
		fixture.BuildCamp(3);
		assert(Find(Send(fixture.host, 10, Json{ { "type", "campStore" }, { "operationId", 2 }, { "buildZoneId", fixture.zoneId } }, 2.3), 10, "campStored") != nullptr);
		const auto kit = std::string(F4T::WorkshopCamps::CampKitItemKey);
		const auto held = fixture.Held(kit);
		const auto removed = Send(fixture.host, 10, Json{ { "type", "campRemove" }, { "operationId", 60 }, { "buildZoneId", fixture.zoneId } }, 2.4);
		assert(Find(removed, 10, "campRemoved") != nullptr && Find(removed, 10, "campRejected") == nullptr);
		assert(Find(removed, 10, "campGrant") == nullptr);
		assert(fixture.Held(kit) == held);
		assert(fixture.store.PendingCampGrants("profile-a").empty());
		assert(!fixture.store.GetLiveDrop(kFirstDroppedEntityId + 1).is_null());
	}

	void TestNonOwnerRemoveIsRefusedWithoutChange()
	{
		Fixture fixture("co-camp-notowner-remove-");
		fixture.BuildCamp(2);
		Join(fixture.host, 11, "profile-b", 1.0);
		(void)Send(fixture.host, 11, TransformAt(0.0), 2.0);
		const auto before = fixture.store.GetWorkshop();
		const auto grantsBefore = fixture.store.PendingCampGrants("profile-a").size();
		const auto refused = Send(fixture.host, 11, Json{ { "type", "campRemove" }, { "operationId", 90 }, { "buildZoneId", fixture.zoneId } }, 2.5);
		const auto* rejected = Find(refused, 11, "campRejected");
		assert(rejected != nullptr && Decode(*rejected).at("status") == "notOwner");
		assert(Find(refused, 11, "campRemoved") == nullptr && Find(refused, 10, "campRemoved") == nullptr);
		assert(Find(refused, 11, "campGrant") == nullptr && Find(refused, 10, "campGrant") == nullptr);
		assert(fixture.store.GetWorkshop() == before);
		assert(fixture.store.PendingCampGrants("profile-a").size() == grantsBefore);
		assert(fixture.PieceCount() == 2);
	}

	void TestRemoveNeedsTheOwnerNearTheCamp()
	{
		Fixture fixture("co-camp-remove-reach-");
		fixture.BuildCamp(2);
		const auto before = fixture.store.GetWorkshop();
		(void)Send(fixture.host, 10, TransformAt(-10.0 * F4T::WorkshopCamps::UnitsPerMeter), 2.5);
		const auto far = Send(fixture.host, 10, Json{ { "type", "campRemove" }, { "operationId", 60 }, { "buildZoneId", fixture.zoneId } }, 2.6);
		const auto* rejected = Find(far, 10, "campRejected");
		assert(rejected != nullptr && Decode(*rejected).at("status") == "tooFar");
		assert(Find(far, 10, "campRemoved") == nullptr && Find(far, 10, "campGrant") == nullptr);
		assert(fixture.store.GetWorkshop() == before);
		assert(fixture.PieceCount() == 2);
	}
}

int main()
{
	TestMoveCullsAllAndGrantsHalfOnce();
	TestGrantIsResentAfterReconnectUntilAcked();
	TestFailedCommitLeavesNothing();
	TestUnpricedPiecesAreCulledWithoutGrant();
	TestStoreThenRefoundCullsAllAndGrantsHalf();
	TestMoveOnStoredCampIsRefused();
	TestCampMoveAndReactivationRefuseWithoutPrices();
	TestUngroundedProbeIsTerrain();
	TestNonOwnerStoreIsRefusedWithoutChange();
	TestKitDropCapGivesDistinctStatus();
	TestReplayedMoveIsNotClaimedForAnotherOwnersCamp();
	TestStoreNeedsTheOwnerNearTheCamp();
	TestRemoveRefundsTheKitOnce();
	TestRemovingAStoredCampGivesNoSecondKit();
	TestNonOwnerRemoveIsRefusedWithoutChange();
	TestRemoveNeedsTheOwnerNearTheCamp();
	return 0;
}
