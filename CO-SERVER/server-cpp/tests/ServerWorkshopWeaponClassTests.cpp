#include "F4TPlayerStateValidation.h"
#include "F4TWorkshopRaids.h"
#include "ServerHeldWeaponTestSupport.h"
#include "ServerProtocol.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolHost;
	using F4T::Workshop::BuildZonePolicy;
	using F4T::Workshop::FormKey;
	using F4T::WorkshopRaids::kSidearmWeaponFormId;
	using HeldWeaponTestSupport::kHuntingRifleFormId;
	using HeldWeaponTestSupport::kMacheteFormId;
	using HeldWeaponTestSupport::kMinigunFormId;

	constexpr std::uint32_t kUnlistedFormId{ 0x00000001 };

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

	BuildZonePolicy Zone9()
	{
		return BuildZonePolicy{ 9, FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 10.0F, 2.0F, 8 };
	}

	void Join(ProtocolHost& a_host, std::uint32_t a_connectionId, const char* a_profile, double a_monotonic)
	{
		(void)a_host.HandleConnected(a_connectionId, a_monotonic);
		assert(!a_host.HandleMessage(a_connectionId, Encode(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), a_monotonic + 0.1, a_monotonic + 1000.1).empty());
		a_host.BindSessionProfile(a_connectionId, a_profile);
	}

	std::vector<OutboundFrame> Send(ProtocolHost& a_host, std::uint32_t a_connectionId, const Json& a_packet, double a_monotonic)
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

	Json WeaponHit(std::uint64_t a_zoneId, std::uint64_t a_objectId, std::uint64_t a_weaponKind)
	{
		return Json{ { "type", "structureHit" }, { "buildZoneId", a_zoneId }, { "objectId", a_objectId }, { "weaponKind", a_weaponKind } };
	}

	Json Equipped(std::uint32_t a_rightHandFormId)
	{
		return Json{
			{ "type", "playerState" },
			{ "equippedItems", Json::array({ Json{ { "slot", F4T::PlayerStateValidation::kRightHandSlot }, { "form" "Id", std::format("{:08X}", a_rightHandFormId) } } }) }
		};
	}

	std::vector<OutboundFrame> ClaimFrames(const char* a_name, std::uint32_t a_rightHandFormId, std::uint64_t a_weaponKind)
	{
		const auto directory = HeldWeaponTestSupport::MakeWorkshopDirectory(std::string{ "co-weapon-classes-" } + a_name);
		ProtocolHost host{ a_name };
		assert(host.ConfigureWorkshop(nullptr, directory / "workshop-zones.json") == 0);
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		Join(host, 70, "profile-owner", 1.0);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(failure.empty());
		(void)Send(host, 70, HeldWeaponTestSupport::PluginListPacket(), 1.5);
		(void)Send(host, 70, TransformAt(0.0), 2.0);
		(void)Send(host, 70, Equipped(a_rightHandFormId), 2.5);
		auto frames = Send(host, 70, WeaponHit(9, 0, a_weaponKind), 3.0);
		std::filesystem::remove_all(directory);
		return frames;
	}

	std::optional<double> ClaimHealth(const char* a_name, std::uint32_t a_rightHandFormId, std::uint64_t a_weaponKind)
	{
		const auto frames = ClaimFrames(a_name, a_rightHandFormId, a_weaponKind);
		const auto* damage = Find(frames, 70, "structureDamage");
		if (damage == nullptr) {
			return std::nullopt;
		}
		return Decode(*damage).at("health").get<double>();
	}

	void TestSidearmCannotClaimHeavy()
	{
		assert(ClaimHealth("sidearm-heavy", kSidearmWeaponFormId, 3) == std::nullopt);
		assert(ClaimHealth("sidearm-light", kSidearmWeaponFormId, 1) == 1985.0);
	}

	void TestRealWeaponClassesDealTheirDamage()
	{
		assert(ClaimHealth("rifle", kHuntingRifleFormId, 2) == 1970.0);
		assert(ClaimHealth("heavy", kMinigunFormId, 3) == 1940.0);
		assert(ClaimHealth("melee", kMacheteFormId, 0) == 1975.0);
	}

	void AssertWeaponUnknownRefusal(const char* a_name, std::uint64_t a_weaponKind)
	{
		const auto frames = ClaimFrames(a_name, kUnlistedFormId, a_weaponKind);
		assert(Find(frames, 70, "structureDamage") == nullptr);
		const auto* refused = Find(frames, 70, "raidHitRefused");
		assert(refused != nullptr);
		assert(Decode(*refused).at("type") == "raidHitRefused");
		assert(Decode(*refused).at("reason") == "weaponUnknown");
	}

	void TestUnlistedHeldItemIsRefused()
	{
		AssertWeaponUnknownRefusal("unlisted-rifle", 2);
		AssertWeaponUnknownRefusal("unlisted-melee", 0);
		AssertWeaponUnknownRefusal("unlisted-light", 1);
	}
}

int main()
{
	TestSidearmCannotClaimHeavy();
	TestRealWeaponClassesDealTheirDamage();
	TestUnlistedHeldItemIsRefused();
	return 0;
}
