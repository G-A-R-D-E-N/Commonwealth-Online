#include "pch.h"

#include "F4TLocalPlayerTransformSend.h"

#include "F4TDebugLogging.h"
#include "F4TEquipmentState.h"
#include "F4TLocalPlayerState.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TPowerArmorSync.h"
#include "F4TProxyMovementDiagnostics.h"
#include "F4TRemotePlayerState.h"
#include "F4TReplicationRuntime.h"
#include "F4TTransformAnimationFields.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <span>
#include <string>

#include <nlohmann/json.hpp>

namespace F4T::LocalPlayerTransformSend
{
	bool Send(
		const F4T::ReplicationRuntime::PlayerTransform& a_transform,
		const F4T::ReplicationRuntime::PlayerLocation& a_location,
		const F4T::ReplicationRuntime::PlayerMovementState& a_movement,
		const char* a_movementType,
		float a_animationGraphSpeed,
		std::span<const F4T::RemotePlayerState::RemoteEquippedItem> a_equippedItems,
		const F4T::RemotePlayerState::RemoteAppearance* a_appearance,
		std::span<const F4T::RemotePlayerState::RemoteActionEvent> a_actionEvents,
		const char* a_characterName,
		const F4T::PowerArmorSync::PowerArmorState* a_powerArmor)
	{
		if (!F4T::Networking::IsConnectedToServer()) {
			return false;
		}

		const auto clientTime =
			std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();

		nlohmann::json packet{
			{ "type", "transform" },
			{ "x", a_transform.x },
			{ "y", a_transform.y },
			{ "z", a_transform.z },
			{ "angleZ", a_transform.angleZ },
			{ "movementType", a_movementType ? a_movementType : "normal" },
			{ "clientTime", clientTime },
			{ "isMoving", a_movement.isMoving },
			{ "isSprinting", a_movement.isSprinting },
			{ "isSneaking", a_movement.isSneaking },
			{ "isJumping", a_movement.isJumping },
			{ "isCrouching", a_movement.isCrouching },
			{ "weaponDrawn", a_movement.weaponDrawn },
			{ "movementSpeed", a_movement.movementSpeed }
		};

		if (a_characterName && a_characterName[0] != '\0') {
			packet["characterName"] = a_characterName;
		}

		if (a_animationGraphSpeed >= 0.0F && std::isfinite(a_animationGraphSpeed)) {
			packet["animationGraphSpeed"] = a_animationGraphSpeed;
		}

		F4T::TransformAnimationFields::WriteAnimationFields(
			packet,
			a_movement.animationDirectionDegrees,
			a_movement.aimPitchDegrees,
			a_movement.turnDeltaDegreesPerSecond);

		static std::atomic<std::uint64_t> s_transformSendCount{ 0 };
		const auto transformSendCount = s_transformSendCount.fetch_add(1, std::memory_order_relaxed) + 1;
		if (transformSendCount <= 5 || transformSendCount % 300 == 0) {
			F4T::DebugLogging::Log(
				"Networking",
				"[RT-VERIFY 364-A] transform send #{} movementType={} sneaking={} crouching={} jumping={} weaponDrawn={} movementSpeed={:.1f} graphSpeed={:.1f} animationDirection={:.2f} aimPitch={:.2f} turnDelta={:.2f}",
				transformSendCount,
				a_movementType ? a_movementType : "normal",
				a_movement.isSneaking,
				a_movement.isCrouching,
				a_movement.isJumping,
				a_movement.weaponDrawn,
				a_movement.movementSpeed,
				a_animationGraphSpeed,
				a_movement.animationDirectionDegrees,
				a_movement.aimPitchDegrees,
				a_movement.turnDeltaDegreesPerSecond);
		}

		if (a_location.hasCellId) {
			packet["cellId"] = std::format("{:08X}", a_location.cellId);
		}

		if (a_location.hasWorldspaceId) {
			packet["worldspaceId"] = std::format("{:08X}", a_location.worldspaceId);
		}

		if (!a_equippedItems.empty()) {
			if (auto equippedItems = F4T::EquipmentState::Encode(a_equippedItems)) {
				packet["equippedItems"] = std::move(*equippedItems);
			} else {
				F4T::Networking::Detail::LogThrottledWarning("transform_equipment_encode_failed", "Could not encode Commonwealth Online equipment; transform sent without equippedItems.");
			}
		}

		if (a_powerArmor) {
			if (auto powerArmor = F4T::PowerArmorSync::Encode(*a_powerArmor)) {
				packet["powerArmor"] = std::move(*powerArmor);
			} else {
				F4T::Networking::Detail::LogThrottledWarning("transform_power_armor_encode_failed", "Could not encode Commonwealth Online power armor; transform sent without powerArmor.");
			}
		}

		if (a_appearance) {
			auto appearance = nlohmann::json{
				{ "version", a_appearance->version },
				{ "raceFormId", a_appearance->raceFormId == 0 ? std::string{} : std::format("{:08X}", a_appearance->raceFormId) },
				{ "height", a_appearance->height },
				{ "morphWeight", {
					{ "thin", a_appearance->morphWeight.thin },
					{ "muscular", a_appearance->morphWeight.muscular },
					{ "large", a_appearance->morphWeight.large }
				} },
				{ "bodyTintColor", {
					{ "r", a_appearance->bodyTintColor.r },
					{ "g", a_appearance->bodyTintColor.g },
					{ "b", a_appearance->bodyTintColor.b },
					{ "a", a_appearance->bodyTintColor.a }
				} },
				{ "hairColorFormId", a_appearance->hairColorFormId == 0 ? std::string{} : std::format("{:08X}", a_appearance->hairColorFormId) },
				{ "facialHairColorFormId", a_appearance->facialHairColorFormId == 0 ? std::string{} : std::format("{:08X}", a_appearance->facialHairColorFormId) },
				{ "complexionFormId", a_appearance->complexionFormId == 0 ? std::string{} : std::format("{:08X}", a_appearance->complexionFormId) }
			};
			if (a_appearance->isFemale.has_value()) {
				appearance["isFemale"] = *a_appearance->isFemale;
			}

			auto headParts = nlohmann::json::array();
			for (const auto headPartId : a_appearance->headPartFormIds) {
				if (headPartId != 0) {
					headParts.push_back(std::format("{:08X}", headPartId));
				}
			}
			appearance["headParts"] = std::move(headParts);

			auto morphs = nlohmann::json::array();
			for (const auto& morph : a_appearance->morphSliders) {
				morphs.push_back({
					{ "id", std::format("{:08X}", morph.id) },
					{ "value", morph.value }
				});
			}
			appearance["morphs"] = std::move(morphs);

			auto morphRegions = nlohmann::json::array();
			for (const auto value : a_appearance->morphRegionSliders) {
				morphRegions.push_back(value);
			}
			appearance["morphRegions"] = std::move(morphRegions);

			auto facialBoneMorphs = nlohmann::json::array();
			for (const auto& morph : a_appearance->facialBoneMorphs) {
				facialBoneMorphs.push_back({
					{ "id", std::format("{:08X}", morph.id) },
					{ "position", { morph.position[0], morph.position[1], morph.position[2] } },
					{ "rotation", { morph.rotation[0], morph.rotation[1], morph.rotation[2] } },
					{ "scale", { morph.scale[0], morph.scale[1], morph.scale[2] } }
				});
			}
			appearance["facialBoneMorphs"] = std::move(facialBoneMorphs);

			auto tints = nlohmann::json::array();
			for (const auto& tint : a_appearance->tints) {
				auto entry = nlohmann::json{
					{ "id", tint.id },
					{ "type", tint.type },
					{ "value", tint.value }
				};
				if (tint.hasColor) {
					entry["color"] = std::format("{:08X}", tint.color);
					entry["swatch"] = tint.swatch;
				}
				tints.push_back(std::move(entry));
			}
			appearance["tints"] = std::move(tints);

		packet["appearance"] = std::move(appearance);
	}

	if (!a_actionEvents.empty()) {
		auto actionEvents = nlohmann::json::array();
		for (const auto& event : a_actionEvents) {
			actionEvents.push_back({
				{ "sequence", event.sequence },
				{ "type", event.type },
				{ "eventName", event.eventName }
			});
		}
		packet["actionEvents"] = std::move(actionEvents);
	}

	auto packetText = packet.dump();
		if (!F4T::Networking::Detail::SendPacketBytes(packetText)) {
			F4T::Networking::Detail::LogThrottledWarning("transform_send_failed", "Could not send Commonwealth Online transform packet.");
			return false;
		}
		static auto s_sendDiagnostics = F4T::ProxyMovementDiagnostics::Window{};
		F4T::ProxyMovementDiagnostics::NoteSend(s_sendDiagnostics);
		if (const auto window = F4T::ProxyMovementDiagnostics::TakeIfDue(s_sendDiagnostics, std::chrono::steady_clock::now())) {
			F4T::LocalPlayerState::LogInfoWithLocalPlayerPrefix(F4T::ProxyMovementDiagnostics::FormatSendWindow(*window));
		}
		return true;
	}
}
