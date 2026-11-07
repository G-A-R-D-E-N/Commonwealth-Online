#include "F4TPlayerStateValidation.h"
#include "F4TWorkshopRaids.h"
#include "ServerFactionTestSupport.h"
#include "ServerHeldWeaponTestSupport.h"
#include "ServerProtocol.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

using namespace FactionTestSupport;

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolHost;
	using F4T::Workshop::BuildZonePolicy;
	using F4T::Workshop::FormKey;
	using F4T::WorkshopRaids::kFragGrenadeSourceFormId;
	using F4T::WorkshopRaids::kMiniNukeSourceFormId;
	using F4T::WorkshopRaids::kSidearmWeaponFormId;

	std::vector<std::uint8_t> Encode(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	Json Decode(const OutboundFrame& a_frame)
	{
		return Json::parse(std::string{
			reinterpret_cast<const char*>(a_frame.payload.data()),
			a_frame.payload.size()
		});
	}

	const OutboundFrame* Find(
		const std::vector<OutboundFrame>& a_frames,
		std::uint32_t a_connectionId,
		std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && !frame.payload.empty() && frame.payload.front() == '{' &&
				Decode(frame).value("type", std::string{}) == a_type) {
				return &frame;
			}
		}
		return nullptr;
	}

	BuildZonePolicy ZoneAt(std::uint64_t a_zoneId)
	{
		return BuildZonePolicy{ a_zoneId, FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 10.0F, 2.0F, 8 };
	}

	BuildZonePolicy Zone9()
	{
		return ZoneAt(9);
	}

	std::uint32_t Join(ProtocolHost& a_host, std::uint32_t a_connectionId, const char* a_profile, double a_monotonic)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_monotonic);
		assert(!a_host.HandleMessage(a_connectionId, Encode(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), a_monotonic + 0.1, a_monotonic + 1000.1).empty());
		a_host.BindSessionProfile(a_connectionId, a_profile);
		return Decode(welcome.front()).at("playerId").get<std::uint32_t>();
	}

	std::vector<OutboundFrame> Send(
		ProtocolHost& a_host,
		std::uint32_t a_connectionId,
		const Json& a_packet,
		double a_monotonic)
	{
		return a_host.HandleMessage(a_connectionId, Encode(a_packet), a_monotonic, a_monotonic + 1000.0);
	}

	Json TransformAt(double a_x)
	{
		return Json{
			{ "type", "transform" },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "x", a_x },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "movementType", "spawn" }
		};
	}

	Json Hit(std::uint64_t a_zoneId, std::uint64_t a_objectId, const char* a_explosive)
	{
		return Json{ { "type", "structureHit" }, { "buildZoneId", a_zoneId }, { "objectId", a_objectId }, { "explosive", a_explosive } };
	}

	Json WeaponHit(std::uint64_t a_zoneId, std::uint64_t a_objectId, std::uint64_t a_weaponKind)
	{
		return Json{ { "type", "structureHit" }, { "buildZoneId", a_zoneId }, { "objectId", a_objectId }, { "weaponKind", a_weaponKind } };
	}

	Json Equipped(std::uint32_t a_rightHandFormId, std::uint32_t a_throwableFormId)
	{
		Json items = Json::array();
		if (a_rightHandFormId != 0) {
			items.push_back(Json{ { "slot", F4T::PlayerStateValidation::kRightHandSlot }, { "formId", std::format("{:08X}", a_rightHandFormId) } });
		}
		if (a_throwableFormId != 0) {
			items.push_back(Json{ { "slot", F4T::PlayerStateValidation::kThrowableSlot }, { "formId", std::format("{:08X}", a_throwableFormId) } });
		}
		return Json{ { "type", "playerState" }, { "equippedItems", items } };
	}
	void TestFactionMateCannotRaidTheSettlement()
	{
		ProtocolHost host{ "Workshop Faction Mate Refusal" };
		assert(host.EnableFactions(nullptr));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		FactionTestSupport::Join(host, 70, "profile-owner", 1.0, 2000000.0);
		const auto member = FactionTestSupport::Join(host, 71, "profile-member", 1.0, 2000000.0);
		FormFaction(host, 70, 71, member.playerId, "Raiders");
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(failure.empty());
		(void)host.HandleMessage(71, Encode(TransformAt(0.0)), 2.0, 2000000.0);
		(void)host.HandleMessage(71, Encode(Equipped(0, kMiniNukeSourceFormId)), 2.0, 2000000.0);
		const auto refused = host.HandleMessage(71, Encode(Hit(9, 0, "mininuke")), 4.0, 2000100.0);
		const auto* frame = Find(refused, 71, "raidHitRefused");
		assert(frame != nullptr && Decode(*frame).at("reason") == "factionMate");
	}

	void TestAllowlistedBuilderFollowsRaidRules()
	{
		ProtocolHost host{ "Workshop Allowlist Raid Rules" };
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		FactionTestSupport::Join(host, 70, "profile-owner", 1.0, 2000000.0);
		FactionTestSupport::Join(host, 71, "profile-guest", 1.0, 2000000.0);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure, 2000000.0, std::uint64_t{ 2000600 });
		assert(failure.empty());
		const auto grant = Json{ { "type", "settlementAccessUpdate" }, { "buildZoneId", 9 }, { "op", "grant" }, { "targetProfileId", "profile-guest" } };
		(void)host.HandleMessage(70, Encode(grant), 2.0, 2000000.0);
		const auto access = host.HandleMessage(70, Encode(Json{ { "type", "settlementAccessGet" }, { "buildZoneId", 9 } }), 2.5, 2000000.0);
		assert(Decode(*Find(access, 70, "settlementAccessState")).dump().find("profile-guest") != std::string::npos);
		(void)host.HandleMessage(71, Encode(TransformAt(0.0)), 2.0, 2000000.0);
		(void)host.HandleMessage(71, Encode(Equipped(0, kMiniNukeSourceFormId)), 2.0, 2000000.0);
		const auto refused = host.HandleMessage(71, Encode(Hit(9, 0, "mininuke")), 4.0, 2000100.0);
		const auto* frame = Find(refused, 71, "raidHitRefused");
		assert(frame != nullptr && Decode(*frame).at("reason") == "settlementProtected");
	}

	void TestStructureHitMustMatchEquipment()
	{
		ProtocolHost host{ "Workshop Hit Equipment" };
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		Join(host, 70, "profile-owner", 1.0);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(failure.empty());
		assert(Find(Send(host, 70, TransformAt(0.0), 2.0), 70, "workshopSnapshot") != nullptr);
		Send(host, 70, Equipped(kSidearmWeaponFormId, 0), 2.5);
		assert(Send(host, 70, Hit(9, 0, "mininuke"), 3.0).empty());
		Send(host, 70, Equipped(0, 0), 3.2);
		assert(Send(host, 70, WeaponHit(9, 0, 2), 3.4).empty());
		Send(host, 70, Equipped(0, kMiniNukeSourceFormId), 3.5);
		assert(!Send(host, 70, Hit(9, 0, "mininuke"), 3.5).empty());
		Send(host, 70, Equipped(0, kFragGrenadeSourceFormId), 3.6);
		assert(Send(host, 70, Hit(9, 0, "mininuke"), 4.6).empty());
		assert(!Send(host, 70, Hit(9, 0, "grenade"), 4.7).empty());
	}

	void TestSidearmHitsOnlyDealLightDamage()
	{
		const auto directory = HeldWeaponTestSupport::MakeWorkshopDirectory("co-workshop-sidearm-class-");
		ProtocolHost host{ "Workshop Sidearm Hit Class" };
		assert(host.ConfigureWorkshop(nullptr, directory / "workshop-zones.json") == 0);
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		Join(host, 70, "profile-owner", 1.0);
		Send(host, 70, HeldWeaponTestSupport::PluginListPacket(), 1.5);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(failure.empty());
		(void)Send(host, 70, TransformAt(0.0), 2.0);
		Send(host, 70, Equipped(kSidearmWeaponFormId, 0), 2.5);
		assert(Send(host, 70, WeaponHit(9, 0, 3), 3.0).empty());
		assert(Send(host, 70, WeaponHit(9, 0, 0), 3.2).empty());
		assert(Send(host, 70, WeaponHit(9, 0, 2), 3.4).empty());
		const auto light = Send(host, 70, WeaponHit(9, 0, 1), 3.6);
		assert(Decode(*Find(light, 70, "structureDamage")).at("health").get<double>() == 1985.0);
		std::filesystem::remove_all(directory);
	}

	void TestUnarmedHitsOnlyClaimMelee()
	{
		ProtocolHost host{ "Workshop Unarmed Hit Class" };
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		Join(host, 70, "profile-owner", 1.0);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(failure.empty());
		(void)Send(host, 70, TransformAt(0.0), 2.0);
		Send(host, 70, Equipped(0, 0), 2.5);
		assert(Send(host, 70, WeaponHit(9, 0, 1), 3.0).empty());
		const auto melee = Send(host, 70, WeaponHit(9, 0, 0), 3.2);
		assert(Decode(*Find(melee, 70, "structureDamage")).at("health").get<double>() == 1975.0);
	}

	void TestBrokenBenchCaptureIsRefusedOnRaidRules()
	{
		ProtocolHost host{ "Workshop Capture Refusal" };
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		FactionTestSupport::Join(host, 70, "profile-owner", 1.0, 2000000.0);
		FactionTestSupport::Join(host, 71, "profile-raider", 1.0, 2000000.0);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure, 2000000.0, std::uint64_t{ 2000600 });
		assert(failure.empty());
		(void)host.HandleMessage(70, Encode(TransformAt(0.0)), 2.0, 2000000.0);
		(void)host.HandleMessage(70, Encode(Equipped(kSidearmWeaponFormId, kMiniNukeSourceFormId)), 2.0, 2000000.0);
		for (const double monotonic : { 3.0, 4.5, 6.0, 7.5 }) {
			(void)host.HandleMessage(70, Encode(Hit(9, 0, "mininuke")), monotonic, 2000000.0);
		}
		(void)host.HandleMessage(71, Encode(TransformAt(0.0)), 8.0, 2000100.0);
		const auto frames = host.HandleMessage(71, Encode(Json{ { "type", "settlementCapture" }, { "buildZoneId", 9 }, { "op", "start" } }), 8.0, 2000100.0);
		const auto* refused = Find(frames, 71, "raidHitRefused");
		assert(refused != nullptr && Decode(*refused).at("reason") == "settlementProtected");
		const auto* reject = Find(frames, 71, "settlementCaptureReject");
		assert(reject != nullptr && Decode(*reject).at("status") == "cancelled");
	}

	void TestFactionLeaveBlocksRaidHitsFromLeaver()
	{
		ProtocolHost host{ "Workshop Faction Leave Block" };
		assert(host.EnableFactions(nullptr));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		FactionTestSupport::Join(host, 70, "profile-owner", 1.0, 2000000.0);
		const auto member = FactionTestSupport::Join(host, 71, "profile-leaver", 1.0, 2000000.0);
		FormFaction(host, 70, 71, member.playerId, "Raiders");
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(failure.empty());
		(void)host.HandleMessage(71, Encode(TransformAt(0.0)), 2.0, 2000000.0);
		(void)host.HandleMessage(71, Encode(Equipped(0, kMiniNukeSourceFormId)), 2.0, 2000000.0);
		AssertResult(FactionTestSupport::Send(host, 71, Leave(), 3.0, 2000000.0), 71, "leave", true);
		const auto refused = host.HandleMessage(71, Encode(Hit(9, 0, "mininuke")), 4.0, 2000100.0);
		const auto* frame = Find(refused, 71, "raidHitRefused");
		assert(frame != nullptr && Decode(*frame).at("reason") == "factionLeaveBlocked");
		assert(Decode(*frame).at("secondsLeft") == 172700);
	}

	void TestSettlementAccessStateCarriesWorkbenchAndOwnerProtection()
	{
		const auto directory = TempDirectory("workshop-access-state");
		DurableStateFileStore store(directory / "durable-state.json", [] { return 1000.0; }, [] { return std::string{ "0123456789abcdef0123456789abcdef" }; });
		PersistenceGate gate(store);
		ProtocolHost host{ "Workshop Access State Protection" };
		host.SetProfileFirstSeenLookup([](std::string_view a_profileId) -> std::optional<double> {
			if (a_profileId == "profile-owner") {
				return 1999000.0;
			}
			return std::nullopt;
		});
		assert(host.EnableSettlementAccess(&gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		Join(host, 70, "profile-owner", 1.0);
		host.BindSessionProfileVerified(70, "profile-owner", false, "");
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(failure.empty());
		const auto frames = host.HandleMessage(70, Encode(Json{ { "type", "settlementAccessGet" }, { "buildZoneId", 9 } }), 3.0, 2000000.0);
		const auto* state = Find(frames, 70, "settlementAccessState");
		assert(state != nullptr);
		assert(Decode(*state).at("workbenchMaxHp") == 2000.0);
		assert(Decode(*state).at("ownerProtectedUntilUnix") == 2258200);
		store.WaitForCommits();
		std::filesystem::remove_all(directory);
	}

}

int main()
{
	TestFactionLeaveBlocksRaidHitsFromLeaver();
	TestSettlementAccessStateCarriesWorkbenchAndOwnerProtection();
	TestFactionMateCannotRaidTheSettlement();
	TestAllowlistedBuilderFollowsRaidRules();
	TestStructureHitMustMatchEquipment();
	TestSidearmHitsOnlyDealLightDamage();
	TestUnarmedHitsOnlyClaimMelee();
	TestBrokenBenchCaptureIsRefusedOnRaidRules();
	return 0;
}
