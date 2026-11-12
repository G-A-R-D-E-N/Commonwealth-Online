#include "pch.h"

#include "F4TWorkshopReplicationClient.h"

#include "F4TCampGround.h"
#include "F4TClientIdentity.h"
#include "F4TJsonFloat.h"
#include "F4TWorkshopCamps.h"
#include "F4TWorkshopRaids.h"
#include "RE/S/SendHUDMessage.h"

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::WorkshopReplicationClient
{
	namespace
	{
		constexpr std::pair<std::string_view, const char*> kCampRefusalNotices[]{
			{ "tooCloseCamp", "Camp refused: too close to another camp." },
			{ "tooCloseSettlement", "Camp refused: too close to a settlement workbench." },
			{ "terrain", "Camp refused: the ground here is too steep, not walkable, or missing. Try flat open ground." },
			{ "noBuildZone", "Camp refused: camps cannot be set up in this protected area." },
			{ "limit", "Camp refused: the server has reached its camp limit." },
			{ "limitProfile", "Camp refused: you already have a camp. Pick it up first." },
			{ "limitArea", "Camp refused: this area already has enough camps. Try somewhere else." },
			{ "underAttack", "Camp refused: the area is under attack. Try again once it is over." },
			{ "cooldown", "Camp refused: wait before moving this camp again." },
			{ "notFound", "Camp refused: that camp is packed up or no longer on the server." },
			{ "notOwner", "Camp refused: that camp belongs to someone else." },
			{ "invalidRequest", "Camp refused: the server could not read the placement. Try again." },
			{ "dropLimit", "Camp refused: too many of your drops are on the ground. Pick some up first." },
			{ "grantFailed", "Camp move refused: the server could not save your refund. Nothing changed. Try again." },
			{ "refundUnavailable", "Camp refused: the server has no refund prices loaded, so nothing changed. Try again later." }
		};

		std::string CampRefusalNotice(std::string_view a_status, std::uint32_t a_distance, std::uint32_t a_required, const std::string& a_subject)
		{
			if (a_status == "tooCloseCamp" && a_required > a_distance) {
				return std::format(
					"Camp refused: another camp is {} m away. Camps need {} m between them. Move {} m further away.",
					a_distance,
					a_required,
					a_required - a_distance);
			}
			if (a_status == "tooCloseSettlement" && a_required > a_distance) {
				const auto where = a_subject.empty() ? std::string{ "a settlement workbench" } : "the " + a_subject + " workbench";
				return std::format(
					"Camp refused: you are {} m from {}. Camps need {} m. Move {} m further away.",
					a_distance,
					where,
					a_required,
					a_required - a_distance);
			}
			for (const auto& [status, notice] : kCampRefusalNotices) {
				if (a_status == status) {
					return notice;
				}
			}
			return "Camp refused.";
		}

		constexpr std::pair<std::string_view, const char*> kCampStoreRefusalNotices[]{
			{ "notFound", "Could not pack up the camp: it is no longer on the server." },
			{ "notOwner", "Could not pack up the camp: it belongs to someone else." },
			{ "underAttack", "Could not pack up the camp: the area is under attack. Try again once it is over." },
			{ "dropLimit", "Could not pack up the camp: too many of your drops are on the ground, so the kit cannot drop. Pick some up first." },
			{ "tooFar", "Could not pack up the camp: walk back to your camp and try again." },
			{ "invalidRequest", "Could not pack up the camp: the server could not save it. Try again." }
		};

		std::string CampStoreRefusalNotice(std::string_view a_status)
		{
			for (const auto& [status, notice] : kCampStoreRefusalNotices) {
				if (a_status == status) {
					return notice;
				}
			}
			return "Could not pack up the camp.";
		}

		std::string CampRemoveRefusalNotice(std::string_view a_status)
		{
			if (a_status == "tooFar") {
				return "Could not delete the camp: walk back to your camp and try again.";
			}
			return "Could not delete the camp.";
		}

		std::optional<Workshop::ObjectState> ParseCampWorkbench(const nlohmann::json& a_camp)
		{
			const auto zone = a_camp.find("buildZoneId");
			const auto x = a_camp.find("centerX");
			const auto y = a_camp.find("centerY");
			const auto z = a_camp.find("centerZ");
			const auto space = a_camp.find("space");
			const auto coordinate = [&](auto it) { return it == a_camp.end() ? std::optional<float>{} : F4T::JsonFloat::ReadFloat(*it); };
			const auto centerX = coordinate(x);
			const auto centerY = coordinate(y);
			const auto centerZ = coordinate(z);
			if (zone == a_camp.end() || !zone->is_number_unsigned() || !centerX || !centerY || !centerZ ||
				space == a_camp.end() || !space->is_object()) {
				return std::nullopt;
			}
			const auto plugin = space->find("plugin");
			const auto local = space->find("localFormId");
			if (plugin == space->end() || local == space->end() || !plugin->is_string() || !local->is_number_unsigned()) {
				return std::nullopt;
			}
			Workshop::ObjectState workbench;
			workbench.objectId = F4T::WorkshopRaids::WorkbenchObjectId;
			workbench.buildZoneId = zone->get<Workshop::BuildZoneId>();
			workbench.form = kCampWorkbenchForm;
			workbench.space = Workshop::FormKey{ plugin->get<std::string>(), local->get<std::uint32_t>() };
			workbench.transform.x = *centerX;
			workbench.transform.y = *centerY;
			const auto surface = F4T::CampGround::SurfaceAt(workbench.transform.x, workbench.transform.y, *centerZ);
			workbench.transform.z = surface ? surface->z : *centerZ;
			workbench.transform.scale = 1.0F;
			return workbench;
		}
	}

	bool HandleCampPlacement(const nlohmann::json& a_packet)
	{
		const auto workbench = ParseCampWorkbench(a_packet);
		if (!workbench) {
			F4T::DebugLogging::Log("CampKit", "campCreated/campMoved dropped: malformed camp");
			return false;
		}
		NoteActiveBuildZone(workbench->buildZoneId);
		if (const auto operation = a_packet.find("operationId"); operation != a_packet.end() && operation->is_number_unsigned()) {
			ResolvePendingCamp(operation->get<std::uint64_t>(), true, workbench->buildZoneId);
		}
		ApplyCampWorkbench(*workbench, a_packet.value("own", false));
		return true;
	}

	bool HandleCampRejected(const nlohmann::json& a_packet)
	{
		const auto operation = a_packet.find("operationId");
		if (operation == a_packet.end() || !operation->is_number_unsigned()) {
			return false;
		}
		const auto status = a_packet.value("status", std::string{});
		const auto distance = a_packet.value("distanceMeters", 0U);
		const auto required = a_packet.value("requiredMeters", 0U);
		const auto subject = a_packet.value("subjectName", std::string{});
		F4T::DebugLogging::Log("CampKit", "camp refused: status {} distance {} m required {} m subject '{}'", status, distance, required, subject);
		const auto operationId = operation->get<std::uint64_t>();
		const bool storeRefused = ResolvePendingCampStore(operationId);
		const bool removeRefused = !storeRefused && ResolvePendingCampRemove(operationId);
		const auto notice = storeRefused ? CampStoreRefusalNotice(status) : removeRefused ? CampRemoveRefusalNotice(status) : CampRefusalNotice(status, distance, required, subject);
		RE::SendHUDMessage::ShowHUDMessage(notice.c_str(), nullptr, true, true);
		ResolvePendingCamp(operationId, false);
		return true;
	}

	bool HandleCampList(const nlohmann::json& a_packet)
	{
		const auto camps = a_packet.find("camps");
		if (camps == a_packet.end() || !camps->is_array()) {
			F4T::DebugLogging::Log("CampKit", "campList dropped: no camps array");
			return false;
		}
		const auto self = F4T::ClientIdentity::ConnectedProfileId();
		for (const auto& camp : *camps) {
			const auto workbench = ParseCampWorkbench(camp);
			if (!workbench) {
				F4T::DebugLogging::Log("CampKit", "campList entry skipped: malformed");
				continue;
			}
			const auto owner = camp.find("ownerProfileId");
			const bool own = !self.empty() && owner != camp.end() && owner->is_string() && owner->get<std::string>() == self;
			ApplyCampWorkbench(*workbench, own);
		}
		return true;
	}
}
