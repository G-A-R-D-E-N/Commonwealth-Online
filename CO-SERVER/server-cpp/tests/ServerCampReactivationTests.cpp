#include "DurableStateFileStore.h"
#include "F4TWorkshopCamps.h"
#include "PersistenceGate.h"
#include "ServerProtocol.h"
#include "StartingGrantFixture.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
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

	std::string RejectedStatus(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		const auto* rejected = Find(a_frames, a_connectionId, "campRejected");
		return rejected == nullptr ? std::string{} : Decode(*rejected).at("status").get<std::string>();
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

	Json TransformAt(double a_x, const char* a_movementType)
	{
		return Json{ { "type", "transform" }, { "cellId", "CAFE" }, { "worldspaceId", "F00D" }, { "x", a_x }, { "y", 0.0 }, { "z", 0.0 },
			{ "angleZ", 0.0 }, { "movementType", a_movementType } };
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

	void WriteConfiguration(const std::filesystem::path& a_directory)
	{
		std::filesystem::create_directories(a_directory);
		{
			std::ofstream output(a_directory / "workshop-zones.json", std::ios::binary);
			output << R"({"version":1,"zones":[]})";
		}
		Json components = Json::array();
		for (int index = 0; index < kComponentCount; ++index) {
			components.push_back(Json{ { "plugin", "fallout4.esm" }, { "localFormId", kFirstComponent + index }, { "count", index + 1 } });
		}
		const Json costs{ { "format", 1 }, { "costs", Json::array({ Json{ { "plugin", "fallout4.esm" }, { "localFormId", kPieceForm }, { "components", components } } }) } };
		std::ofstream output(a_directory / "workshop-costs.json", std::ios::binary);
		output << costs.dump();
	}

	struct Fixture
	{
		std::filesystem::path directory;
		DurableStateFileStore store;
		PersistenceGate gate;
		ProtocolHost host;

		static std::filesystem::path MakeDirectory(const char* a_name)
		{
			auto directory = std::filesystem::temp_directory_path() /
				(std::string{ a_name } + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			WriteConfiguration(directory);
			return directory;
		}

		explicit Fixture(const char* a_name) :
			directory(MakeDirectory(a_name)),
			store(directory / "state.json", [] { return 1000.0; }, [] { return "0123456789abcdef0123456789abcdef"; }),
			gate(store),
			host("Camp reactivate", {}, {}, true, false, std::string(64, 'k'))
		{
			F4T::Identity::TokenHash hash{};
			store.SaveResumeCredential("profile-a", hash, std::nullopt, true, 5000.0);
			(void)store.InitializeInventory("profile-a", Json::array({ Json{ { "plugin", "CommonwealthOnline.esp" }, { "localFormId", "997F" }, { "count", 2u } } }));
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
	};

	void TestCampReactivatesStoredCamp()
	{
		Fixture fixture("co-camp-reactivate-");
		fixture.DropKit("kit");
		Join(fixture.host, 10, "profile-a", 1.0);
		(void)Send(fixture.host, 10, TransformAt(0.0, "spawn"), 2.0);
		const auto created = Send(fixture.host, 10, FoundAt(1, kFirstDroppedEntityId, 0.0), 2.1);
		const auto* createdFrame = Find(created, 10, "campCreated");
		assert(createdFrame != nullptr);
		const auto zoneId = Decode(*createdFrame).at("buildZoneId").get<std::uint64_t>();
		assert(Find(Send(fixture.host, 10, Place(3, 0, zoneId, 1.0), 2.2), 10, "workshopCommit") != nullptr);
		assert(Find(Send(fixture.host, 10, Place(6, 1, zoneId, 55.0 * F4T::WorkshopCamps::UnitsPerMeter), 2.25), 10, "workshopCommit") != nullptr);
		const Json storeRequest{ { "type", "campStore" }, { "operationId", 2u }, { "buildZoneId", zoneId } };
		assert(Find(Send(fixture.host, 10, storeRequest, 2.3), 10, "campStored") != nullptr);
		assert(!fixture.store.GetLiveDrop(kFirstDroppedEntityId + 1).is_null());
		fixture.DropKit("kit2");
		assert(!fixture.store.GetLiveDrop(kFirstDroppedEntityId + 2).is_null());

		const auto reactivationX = -10.0 * F4T::WorkshopCamps::UnitsPerMeter;
		(void)Send(fixture.host, 10, TransformAt(reactivationX, "normal"), 3699.9);
		const auto reactivated = Send(fixture.host, 10, FoundAt(4, kFirstDroppedEntityId + 1, reactivationX), 3700.0);
		const auto* reactivatedFrame = Find(reactivated, 10, "campCreated");
		assert(reactivatedFrame != nullptr && Find(reactivated, 10, "campRejected") == nullptr);
		assert(Decode(*reactivatedFrame).at("buildZoneId").get<std::uint64_t>() == zoneId);
		const auto* refund = Find(reactivated, 10, "campRefund");
		assert(refund != nullptr);
		assert(Decode(*refund).at("removedPieces") == 2 && Decode(*refund).at("unpricedPieces") == 0);
		const auto* snapshot = Find(reactivated, 10, "workshopSnapshot");
		assert(snapshot != nullptr && Decode(*snapshot).at("objects").empty());
		assert(fixture.store.GetLiveDrop(kFirstDroppedEntityId + 1).is_null());

		const auto nearX = 5000.0;
		(void)Send(fixture.host, 10, TransformAt(nearX, "normal"), 3705.0);
		const auto tooClose = Send(fixture.host, 10, FoundAt(5, kFirstDroppedEntityId + 2, nearX), 3705.1);
		assert(RejectedStatus(tooClose, 10) == "tooCloseCamp");
		assert(Find(tooClose, 10, "campCreated") == nullptr);
		assert(!fixture.store.GetLiveDrop(kFirstDroppedEntityId + 2).is_null());

		const auto farX = 10000.0;
		(void)Send(fixture.host, 10, TransformAt(farX, "normal"), 3710.0);
		const auto limited = Send(fixture.host, 10, FoundAt(6, kFirstDroppedEntityId + 2, farX), 3710.1);
		assert(RejectedStatus(limited, 10) == "limitProfile");
		assert(Find(limited, 10, "campCreated") == nullptr);
		assert(!fixture.store.GetLiveDrop(kFirstDroppedEntityId + 2).is_null());
	}
}

int main()
{
	TestCampReactivatesStoredCamp();
	return 0;
}
