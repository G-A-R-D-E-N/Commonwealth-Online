#include "pch.h"

#include "F4TPauseMenuMap.h"

#include "F4TPauseMenuMapIcons.h"
#include "F4TPauseMenuMapMarkerData.h"
#include "F4TMapFogMemory.h"

#include "F4TLocalAdminState.h"
#include "F4TMultiplayerProfile.h"
#include "F4TPrismaUI.h"
#include "F4TTravelCharge.h"
#include "SessionCallbacks.h"

#include "RE/S/SendHUDMessage.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <format>
#include <mutex>
#include <optional>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::PauseMenuMap
{
	namespace
	{
		constexpr float kCellSize = 4096.0F;
		constexpr float kRadToDeg = 57.2957795F;
		constexpr std::uint8_t kFlagVisible = 0x01;
		constexpr std::uint8_t kFlagCanTravelTo = 0x02;
		constexpr const char* kIconBase = "../MapMarkers/";
		constexpr std::size_t kMaxMarkers = 512;
		constexpr std::size_t kMaxHostiles = 64;
		// Must stay Caps001, MISC 00000F; source:Fallout4.esm
		constexpr std::uint32_t kCapsFormId = 0x0000000F;
		constexpr std::int32_t kTravelBaseCost = 5;
		constexpr float kTravelUnitsPerCost = 1500.0F;
		constexpr std::int32_t kTravelMaxCost = 100;
		bool g_loggedMarkerDump = false;

		bool g_streaming = false;
		std::uint32_t g_tickCounter = 0;
		bool g_markersDisabled = false;
		std::string g_jsonBuffer;

		std::mutex g_pendingTravelMutex;
		std::string g_pendingTravelMarkerId;
		std::string g_pendingTravelPlayerId;
		std::int32_t g_pendingTravelCost = 0;
		bool g_fastTravelCloseRequested = false;
		std::atomic<void (*)()> g_teleportBeganCallback{ nullptr };
		RE::TESFormID g_deferredTravelFormId = 0;
		std::uint32_t g_deferredTravelPlayerId = 0;
		std::int32_t g_deferredTravelCost = 0;
		std::uint32_t g_deferredTravelDelayFrames = 0;

		struct MapFrame
		{
			const char* key;
			const char* editorId;
			float x0;
			float y0;
			float x1;
			float y1;
		};

		// Each frame must match the calibration in pause-menu/assets/maps/<key>/map.json.
		constexpr MapFrame kMapFrames[] = {
			{ "commonwealth", "Commonwealth", 67.46763287F, 0.0F, 2008.84518231F, 2048.0F },
			{ "farharbor", "DLC03FarHarbor", 68.0F, 59.0F, 1985.0F, 1975.0F },
			{ "nukaworld", "NukaWorld", 64.0F, 56.0F, 1984.0F, 1985.0F },
		};

		struct MapBounds
		{
			float minX{};
			float maxX{};
			float minY{};
			float maxY{};
			bool  valid{ false };
			bool  known{ false };
			const MapFrame* frame{ nullptr };
		};

		constexpr float kMapImageSize = 2048.0F;

		const MapFrame* ResolveMapFrame(const RE::TESWorldSpace* a_world)
		{
			const char* editorId = a_world ? a_world->editorID.c_str() : nullptr;
			if (editorId) {
				for (const auto& frame : kMapFrames) {
					if (std::string_view{ frame.editorId } == editorId) {
						return &frame;
					}
				}
			}
			return nullptr;
		}

		RE::TESWorldSpace* ResolveMapWorld(RE::TESWorldSpace* a_world)
		{
			constexpr int kMaxParentSteps = 8;
			auto* mapWorld = a_world;
			for (int step = 0; step < kMaxParentSteps; ++step) {
				auto* parent = mapWorld->GetParentWorld(RE::TESWorldSpace::PARENT_USE_FLAG::kMap);
				if (!parent) {
					break;
				}
				mapWorld = parent;
			}
			return mapWorld;
		}

		MapBounds ResolveMapBounds(RE::TESWorldSpace* a_world)
		{
			MapBounds bounds{};
			if (!a_world) {
				return bounds;
			}

			const auto* mapWorld = ResolveMapWorld(a_world);
			const auto* frame = ResolveMapFrame(mapWorld);
			bounds.known = frame != nullptr;
			bounds.frame = frame ? frame : &kMapFrames[0];
			const auto& map = mapWorld->worldMapData;
			bounds.minX = static_cast<float>(map.nwCellX) * kCellSize;
			bounds.maxX = static_cast<float>(map.seCellX + 1) * kCellSize;
			bounds.minY = static_cast<float>(map.seCellY) * kCellSize;
			bounds.maxY = static_cast<float>(map.nwCellY + 1) * kCellSize;

			if (!(bounds.maxX > bounds.minX) || !(bounds.maxY > bounds.minY)) {
				return bounds;
			}

			bounds.valid = true;
			return bounds;
		}

		RE::NiPoint3 ResolveMarkerWorldPos(RE::TESObjectREFR* a_refr)
		{
			return a_refr->data.location;
		}

		bool WorldToPct(const MapBounds& a_bounds, float a_worldX, float a_worldY, float& a_xPct, float& a_yPct)
		{
			if (!a_bounds.valid) {
				return false;
			}

			const float normX = (a_worldX - a_bounds.minX) / (a_bounds.maxX - a_bounds.minX);
			const float normY = 1.0F - ((a_worldY - a_bounds.minY) / (a_bounds.maxY - a_bounds.minY));

			const auto& frame = *a_bounds.frame;
			const float pixelX = frame.x0 + normX * (frame.x1 - frame.x0);
			const float pixelY = frame.y0 + normY * (frame.y1 - frame.y0);
			a_xPct = (pixelX / kMapImageSize) * 100.0F;
			a_yPct = (pixelY / kMapImageSize) * 100.0F;

			return std::isfinite(a_xPct) && std::isfinite(a_yPct);
		}

		RE::TESWorldSpace* ResolvePlayerWorld(RE::PlayerCharacter* a_player)
		{
			if (!a_player) {
				return nullptr;
			}

			if (a_player->cachedWorldspace) {
				return a_player->cachedWorldspace;
			}

			if (const auto* cell = a_player->GetParentCell(); cell && cell->IsExterior()) {
				return cell->worldSpace;
			}

			return nullptr;
		}

		bool IsHighlightedType(std::uint8_t a_type)
		{
			using MT = RE::MARKER_TYPE;
			const auto typed = static_cast<MT>(a_type);
			return typed == MT::kPlayerSet ||
				typed == MT::kQuest ||
				typed == MT::kQuestDoor ||
				typed == MT::kQuestMultiple;
		}

		RE::MapMarkerData* TryGetMapMarkerData(RE::TESObjectREFR* a_refr)
		{
			if (!a_refr) {
				return nullptr;
			}

			// Engine helper — do not touch ExtraMapMarker::mapMarkerData (private in CommonLibF4).
			return a_refr->GetMapMarkerData();
		}

		bool ParseMarkerFormId(std::string_view a_id, RE::TESFormID& a_out)
		{
			if (a_id.empty()) {
				return false;
			}

			std::string cleaned{ a_id };
			if (cleaned.size() >= 2 && cleaned[0] == '0' && (cleaned[1] == 'x' || cleaned[1] == 'X')) {
				cleaned = cleaned.substr(2);
			}

			try {
				a_out = static_cast<RE::TESFormID>(std::stoul(cleaned, nullptr, 16));
				return a_out != 0;
			} catch (...) {
				return false;
			}
		}

		bool FindTravelableMarker(RE::TESFormID a_formId, RE::ObjectRefHandle& a_outHandle, const char** a_outName)
		{
			a_outHandle = {};
			if (a_outName) {
				*a_outName = nullptr;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || a_formId == 0) {
				return false;
			}

			const auto& markerHandles = player->currentMapMarkers;
			const auto totalMarkers = static_cast<std::size_t>(markerHandles.size());
			const auto markerCount = totalMarkers < kMaxMarkers ? totalMarkers : kMaxMarkers;

			for (std::size_t i = 0; i < markerCount; ++i) {
				const auto handle = markerHandles[static_cast<std::uint32_t>(i)];
				const auto refr = handle.get();
				if (!refr || refr->IsDisabled() || refr->IsDeleted()) {
					continue;
				}
				if (refr->GetFormID() != a_formId) {
					continue;
				}

				auto* data = TryGetMapMarkerData(refr.get());
				if (!data) {
					return false;
				}

				const auto fields = ResolveMarkerFields(data);
				if ((fields.flags & kFlagVisible) == 0 || (fields.flags & kFlagCanTravelTo) == 0) {
					return false;
				}

				a_outHandle = handle;
				if (a_outName) {
					*a_outName = SafeGetLocationName(data);
				}
				return true;
			}

			return false;
		}

		std::optional<RE::TESFormID> ParseFormId(const nlohmann::json& a_entry, const char* a_key)
		{
			const auto found = a_entry.find(a_key);
			if (found == a_entry.end() || !found->is_string()) {
				return std::nullopt;
			}
			const auto text = found->get<std::string>();
			if (text.empty()) {
				return std::nullopt;
			}
			try {
				const auto value = std::stoul(text, nullptr, 16);
				if (value == 0 || value > 0xFFFFFFFFull) {
					return std::nullopt;
				}
				return static_cast<RE::TESFormID>(value);
			} catch (...) {
				return std::nullopt;
			}
		}

		bool IsInteriorCell(RE::TESFormID a_cellId)
		{
			auto* form = RE::TESForm::GetFormByID(a_cellId);
			auto* cell = form ? form->As<RE::TESObjectCELL>() : nullptr;
			return cell != nullptr && cell->IsInterior();
		}

		bool ParsePlayerId(std::string_view a_id, std::uint32_t& a_out)
		{
			try {
				std::size_t consumed = 0;
				const auto value = std::stoull(std::string{ a_id }, &consumed);
				if (consumed != a_id.size() || value == 0 || value > 0xFFFFFFFFull) {
					return false;
				}
				a_out = static_cast<std::uint32_t>(value);
				return true;
			} catch (...) {
				return false;
			}
		}

		bool RemoteFlag(const nlohmann::json& a_entry, const char* a_key)
		{
			return a_entry.contains(a_key) && a_entry[a_key].is_boolean() && a_entry[a_key].get<bool>();
		}

		RE::NiPoint3 PlayerMapPosition(RE::PlayerCharacter* a_player)
		{
			const auto* cell = a_player->GetParentCell();
			return cell && cell->IsInterior() ? a_player->exteriorPosition : a_player->data.location;
		}

		std::uint32_t CapsCount(RE::PlayerCharacter* a_player)
		{
			const auto* caps = RE::TESForm::GetFormByID<RE::TESBoundObject>(kCapsFormId);
			return caps ? a_player->GetInventoryObjectCount(caps) : 0;
		}

		bool SpendCaps(RE::PlayerCharacter* a_player, std::int32_t a_cost)
		{
			auto* caps = RE::TESForm::GetFormByID<RE::TESBoundObject>(kCapsFormId);
			if (!caps) {
				return false;
			}
			const auto before = a_player->GetInventoryObjectCount(caps);
			RE::TESObjectREFR::RemoveItemData data{ caps, a_cost };
			(void)a_player->RemoveItem(data);
			return a_player->GetInventoryObjectCount(caps) < before;
		}

		struct TravelTarget
		{
			RE::TESFormID worldId{ 0 };
			RE::NiPoint3 location{};
			float angleZ{ 0.0F };
		};

		std::int32_t TravelDistanceCost(RE::TESWorldSpace* a_playerWorld, const RE::NiPoint3& a_playerPos, const TravelTarget& a_target)
		{
			if (!a_playerWorld || a_playerWorld->GetFormID() != a_target.worldId) {
				return kTravelMaxCost;
			}
			const float dx = a_target.location.x - a_playerPos.x;
			const float dy = a_target.location.y - a_playerPos.y;
			const float units = std::floor(std::sqrt(dx * dx + dy * dy) / kTravelUnitsPerCost);
			return static_cast<std::int32_t>(std::min(static_cast<float>(kTravelMaxCost), static_cast<float>(kTravelBaseCost) + units));
		}

		std::int32_t TravelCost(RE::TESWorldSpace* a_playerWorld, const RE::NiPoint3& a_playerPos, const TravelTarget& a_target)
		{
			return F4T::TravelCharge::ResolveCost(F4T::LocalAdmin::Get(), TravelDistanceCost(a_playerWorld, a_playerPos, a_target));
		}

		bool FindRemoteTravelTarget(std::uint32_t a_playerId, TravelTarget& a_out)
		{
			if (!COUI::Session::HasGetRemotePlayersMapJson()) {
				return false;
			}
			std::string remoteJson;
			std::string error;
			if (!COUI::Session::GetRemotePlayersMapJson(remoteJson, error)) {
				return false;
			}
			try {
				const auto parsed = nlohmann::json::parse(remoteJson.empty() ? "[]" : remoteJson);
				const auto& remote = parsed.is_object() && parsed.contains("players") ? parsed["players"] : parsed;
				if (!remote.is_array()) {
					return false;
				}
				for (const auto& entry : remote) {
					if (!entry.is_object() || entry.value("id", 0u) != a_playerId) {
						continue;
					}
					const auto worldId = ParseFormId(entry, "worldspaceId");
					const auto cellId = ParseFormId(entry, "cellId");
					if (!worldId || (cellId && IsInteriorCell(*cellId)) || !(RemoteFlag(entry, "party") || RemoteFlag(entry, "friend") || RemoteFlag(entry, "faction"))) {
						return false;
					}
					if (!ResolveMapBounds(RE::TESForm::GetFormByID<RE::TESWorldSpace>(*worldId)).known) {
						return false;
					}
					a_out.worldId = *worldId;
					a_out.location.x = entry.value("x", 0.0F);
					a_out.location.y = entry.value("y", 0.0F);
					a_out.location.z = entry.value("z", 0.0F);
					a_out.angleZ = static_cast<float>(entry.value("angleZ", 0.0));
					return true;
				}
			} catch (...) {
			}
			return false;
		}

		void NotifyTeleportBegan()
		{
			const auto callback = g_teleportBeganCallback.load();
			REX::INFO("{} [PauseMenuMap] Teleport overlay requested; callback {}",
				COUI::Session::GetLogPrefix(), callback ? "registered" : "missing");
			if (callback) {
				callback();
			}
		}

		void QueueTargetLocation(RE::PlayerCharacter* a_player, RE::TESWorldSpace* a_world, RE::TESObjectCELL* a_interior, const RE::NiPoint3& a_location, const RE::NiPoint3& a_angle, const RE::ObjectRefHandle& a_fastTravelMarker)
		{
			// Must use queuedTargetLoc; QueueFastTravel and the Papyrus path crash on this runtime.
			auto& target = a_player->queuedTargetLoc;
			target.world = a_world;
			target.interior = a_interior;
			target.transitionTel = nullptr;
			target.location = a_location;
			target.angle = a_angle;
			target.walkThroughDoor = nullptr;
			target.arrivalFunc = nullptr;
			target.arrivalFuncData = nullptr;
			target.furnitureRef.reset();
			target.fastTravelMarker = a_fastTravelMarker;
			target.fastTravelDist = 0.0F;
			target.resetWeather = true;
			target.allowAutoSave = true;
			target.preventLoadMenu = false;
			target.skyTransition = true;
			target.isValid = true;
			a_player->positioning = true;
			NotifyTeleportBegan();
		}

		void ShowTravelRefused(std::string_view a_reason)
		{
			static std::string s_hudText;
			s_hudText = std::format("Travel cancelled: {}", a_reason);
			RE::SendHUDMessage::ShowHUDMessage(s_hudText.c_str(), nullptr, true, true);
		}

		bool ValidateAndDeferPlayerTravel(std::string_view a_playerId, std::int32_t a_quotedCost)
		{
			std::uint32_t playerId = 0;
			if (!ParsePlayerId(a_playerId, playerId)) {
				REX::WARN("{} [PauseMenuMap] Travel to player rejected: invalid player id \"{}\"",
					COUI::Session::GetLogPrefix(), a_playerId);
				ShowTravelRefused("that player can't be reached right now");
				return false;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			TravelTarget target{};
			if (!player || !FindRemoteTravelTarget(playerId, target)) {
				REX::WARN("{} [PauseMenuMap] Travel to player rejected: player {} missing or not travelable",
					COUI::Session::GetLogPrefix(), playerId);
				ShowTravelRefused("that player can't be reached right now");
				return false;
			}
			if (player->IsInCombat()) {
				REX::WARN("{} [PauseMenuMap] Travel to player rejected: local player is in combat",
					COUI::Session::GetLogPrefix());
				ShowTravelRefused("you are in combat");
				return false;
			}
			const auto cost = TravelCost(ResolvePlayerWorld(player), PlayerMapPosition(player), target);
			if (cost != a_quotedCost) {
				ShowTravelRefused("the price changed; please try again");
				return false;
			}
			if (CapsCount(player) < static_cast<std::uint32_t>(cost)) {
				REX::WARN("{} [PauseMenuMap] Travel to player rejected: {} caps needed",
					COUI::Session::GetLogPrefix(), cost);
				ShowTravelRefused(std::format("you need {} caps", cost));
				return false;
			}

			g_deferredTravelPlayerId = playerId;
			g_deferredTravelCost = cost;
			g_deferredTravelFormId = 0;
			g_deferredTravelDelayFrames = 0;
			REX::INFO("{} [PauseMenuMap] Travel to player {} deferred for {} caps — closing pause menu first",
				COUI::Session::GetLogPrefix(), playerId, cost);
			return true;
		}

		bool ExecuteDeferredPlayerTravel(std::uint32_t a_playerId)
		{
			g_deferredTravelPlayerId = 0;

			auto* player = RE::PlayerCharacter::GetSingleton();
			TravelTarget target{};
			if (!player || !FindRemoteTravelTarget(a_playerId, target)) {
				REX::WARN("{} [PauseMenuMap] Deferred travel to player aborted: player {} no longer travelable",
					COUI::Session::GetLogPrefix(), a_playerId);
				ShowTravelRefused("that player can't be reached right now");
				return true;
			}
			if (player->IsInCombat()) {
				REX::WARN("{} [PauseMenuMap] Deferred travel to player aborted: local player is in combat",
					COUI::Session::GetLogPrefix());
				ShowTravelRefused("you are in combat");
				return true;
			}
			auto* destinationWorld = RE::TESForm::GetFormByID<RE::TESWorldSpace>(target.worldId);
			const auto cost = TravelCost(ResolvePlayerWorld(player), PlayerMapPosition(player), target);
			if (cost != g_deferredTravelCost) {
				ShowTravelRefused("the price changed; please try again");
				return true;
			}
			if (!destinationWorld || CapsCount(player) < static_cast<std::uint32_t>(cost)) {
				REX::WARN("{} [PauseMenuMap] Deferred travel to player aborted: caps check failed for {} caps",
					COUI::Session::GetLogPrefix(), cost);
				ShowTravelRefused(destinationWorld ? std::format("you need {} caps", cost) : std::string{ "that player can't be reached right now" });
				return true;
			}
			if (cost > 0 && !SpendCaps(player, cost)) {
				REX::WARN("{} [PauseMenuMap] Deferred travel to player aborted: caps charge did not drop the count",
					COUI::Session::GetLogPrefix());
				ShowTravelRefused("your caps could not be taken");
				return true;
			}

			RE::NiPoint3 angle{};
			angle.z = target.angleZ;
			QueueTargetLocation(player, destinationWorld, nullptr, target.location, angle, RE::ObjectRefHandle{});
			REX::INFO("{} [PauseMenuMap] Player target location queued for player {} ({} caps)",
				COUI::Session::GetLogPrefix(), a_playerId, cost);
			return true;
		}

		bool ValidateAndDeferFastTravel(std::string_view a_markerId)
		{
			RE::TESFormID formId = 0;
			if (!ParseMarkerFormId(a_markerId, formId)) {
				REX::WARN("{} [PauseMenuMap] Fast travel rejected: invalid marker id \"{}\"",
					COUI::Session::GetLogPrefix(), a_markerId);
				return false;
			}

			RE::ObjectRefHandle handle{};
			const char* locName = nullptr;
			if (!FindTravelableMarker(formId, handle, &locName)) {
				REX::WARN("{} [PauseMenuMap] Fast travel rejected: marker 0x{:08X} missing or not travelable",
					COUI::Session::GetLogPrefix(), formId);
				return false;
			}

			g_deferredTravelFormId = formId;
			g_deferredTravelPlayerId = 0;
			g_deferredTravelDelayFrames = 0;
			REX::INFO("{} [PauseMenuMap] Fast travel deferred to \"{}\" (0x{:08X}) — closing pause menu first",
				COUI::Session::GetLogPrefix(),
				locName ? locName : "",
				formId);
			return true;
		}

		bool ExecuteDeferredFastTravel()
		{
			const auto formId = g_deferredTravelFormId;
			const auto playerId = g_deferredTravelPlayerId;
			if (formId == 0 && playerId == 0) {
				return false;
			}

			auto* ui = RE::UI::GetSingleton();
			if (ui && ui->GetMenuOpen<RE::PauseMenu>()) {
				g_deferredTravelDelayFrames = 3;
				return false;
			}

			// Give the engine several frames after PauseMenu teardown.
			if (g_deferredTravelDelayFrames > 0) {
				--g_deferredTravelDelayFrames;
				return false;
			}

			if (playerId != 0) {
				return ExecuteDeferredPlayerTravel(playerId);
			}

			RE::ObjectRefHandle handle{};
			const char* locName = nullptr;
			if (!FindTravelableMarker(formId, handle, &locName)) {
				REX::WARN("{} [PauseMenuMap] Deferred fast travel aborted: marker 0x{:08X} no longer travelable",
					COUI::Session::GetLogPrefix(), formId);
				g_deferredTravelFormId = 0;
				return true;
			}

			const auto marker = handle.get();
			if (!marker) {
				REX::WARN("{} [PauseMenuMap] Deferred fast travel aborted: marker handle could not be resolved",
					COUI::Session::GetLogPrefix());
				g_deferredTravelFormId = 0;
				return true;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				REX::WARN("{} [PauseMenuMap] Deferred fast travel aborted: player unavailable",
					COUI::Session::GetLogPrefix());
				g_deferredTravelFormId = 0;
				return true;
			}

			auto* cell = marker->GetParentCell();
			auto* destinationWorld = cell && cell->IsExterior() ? cell->worldSpace : ResolvePlayerWorld(player);
			auto* destinationInterior = cell && cell->IsInterior() ? cell : nullptr;
			if (!destinationWorld && !destinationInterior) {
				REX::WARN("{} [PauseMenuMap] Deferred fast travel aborted: destination world could not be resolved",
					COUI::Session::GetLogPrefix());
				g_deferredTravelFormId = 0;
				return true;
			}

			REX::INFO("{} [PauseMenuMap] Queueing player target location for \"{}\" (0x{:08X})",
				COUI::Session::GetLogPrefix(),
				locName ? locName : "",
				formId);

			g_deferredTravelFormId = 0;

			QueueTargetLocation(player, destinationWorld, destinationInterior, ResolveMarkerWorldPos(marker.get()), marker->data.angle, handle);

			REX::INFO("{} [PauseMenuMap] Player target location queued for 0x{:08X}",
				COUI::Session::GetLogPrefix(), formId);
			return true;
		}

		void ProcessPendingFastTravel()
		{
			std::string pendingId;
			std::string pendingPlayerId;
			std::int32_t pendingCost = 0;
			{
				std::lock_guard lock(g_pendingTravelMutex);
				if (g_pendingTravelMarkerId.empty() && g_pendingTravelPlayerId.empty()) {
					return;
				}
				pendingId.swap(g_pendingTravelMarkerId);
				pendingPlayerId.swap(g_pendingTravelPlayerId);
				pendingCost = g_pendingTravelCost;
			}

			const bool deferred = pendingId.empty() ? ValidateAndDeferPlayerTravel(pendingPlayerId, pendingCost) : ValidateAndDeferFastTravel(pendingId);
			if (deferred) {
				g_fastTravelCloseRequested = true;
			}
		}

		nlohmann::json BuildCaptures(const MapBounds& a_bounds, RE::TESWorldSpace* a_world, const nlohmann::json& a_captures)
		{
			nlohmann::json drawn = nlohmann::json::array();
			if (!a_world || !a_captures.is_array()) {
				return drawn;
			}
			for (const auto& entry : a_captures) {
				if (!entry.is_object()) {
					continue;
				}
				const auto world = ParseFormId(entry, "worldspaceId");
				float xPct = 0.0F;
				float yPct = 0.0F;
				if (!world || *world != a_world->GetFormID() || !WorldToPct(a_bounds, entry.value("x", 0.0F), entry.value("y", 0.0F), xPct, yPct)) {
					continue;
				}
				drawn.push_back({ { "id", entry.value("id", std::string{}) }, { "xPct", xPct }, { "yPct", yPct } });
			}
			return drawn;
		}

		nlohmann::json BuildOthers(const MapBounds& a_bounds, RE::TESWorldSpace* a_world, const RE::NiPoint3& a_playerPos, std::uint32_t& a_elsewhere, bool& a_publicVisible, nlohmann::json& a_captures)
		{
			nlohmann::json others = nlohmann::json::array();
			a_elsewhere = 0;
			a_publicVisible = true;
			a_captures = nlohmann::json::array();
			if (!COUI::Session::HasGetRemotePlayersMapJson()) {
				return others;
			}

			std::string remoteJson;
			std::string error;
			if (!COUI::Session::GetRemotePlayersMapJson(remoteJson, error)) {
				return others;
			}

			try {
				const auto parsed = nlohmann::json::parse(remoteJson.empty() ? "[]" : remoteJson);
				a_publicVisible = !parsed.is_object() || !parsed.contains("publicPlayersVisible") || parsed["publicPlayersVisible"] != false;
				if (parsed.is_object() && parsed.contains("captures")) {
					a_captures = BuildCaptures(a_bounds, a_world, parsed["captures"]);
				}
				const auto& remote = parsed.is_object() && parsed.contains("players") ? parsed["players"] : parsed;
				if (!remote.is_array()) {
					return others;
				}

				for (const auto& entry : remote) {
					if (!entry.is_object()) {
						continue;
					}

					const auto remoteWorld = ParseFormId(entry, "worldspaceId");
					if (!remoteWorld) {
						++a_elsewhere;
						continue;
					}

					const auto remoteCell = ParseFormId(entry, "cellId");
					if (remoteCell && IsInteriorCell(*remoteCell)) {
						++a_elsewhere;
						continue;
					}

					MapBounds bounds = a_bounds;
					if (!a_world || *remoteWorld != a_world->GetFormID()) {
						bounds = ResolveMapBounds(RE::TESForm::GetFormByID<RE::TESWorldSpace>(*remoteWorld));
						if (!bounds.valid || !bounds.known) {
							++a_elsewhere;
							continue;
						}
					}

					const float wx = entry.value("x", 0.0F);
					const float wy = entry.value("y", 0.0F);
					float xPct = 0.0F;
					float yPct = 0.0F;
					if (!WorldToPct(bounds, wx, wy, xPct, yPct)) {
						continue;
					}

					const auto headingDeg = static_cast<float>(entry.value("angleZ", 0.0) * kRadToDeg);
					const bool isFriend = RemoteFlag(entry, "friend");
					const bool isParty = RemoteFlag(entry, "party");
					const bool canTravel = (isParty || isFriend || RemoteFlag(entry, "faction")) && bounds.known;
					const TravelTarget target{ *remoteWorld, { wx, wy, entry.value("z", 0.0F) }, 0.0F };
					others.push_back({ { "id", entry.value("id", 0u) }, { "xPct", xPct }, { "yPct", yPct }, { "worldspace", bounds.frame->key },
						{ "rotDeg", std::isfinite(headingDeg) ? headingDeg : 0.0F },
						{ "faction", entry.contains("faction") && entry["faction"].is_boolean() && entry["faction"].get<bool>() },
						{ "friend", isFriend },
						{ "rel", isParty ? "party" : (isFriend ? "friend" : "") },
						{ "factionName", entry.contains("factionName") && entry["factionName"].is_string() ? entry["factionName"].get<std::string>() : std::string{} },
						{ "downed", entry.contains("downed") && entry["downed"].is_boolean() && entry["downed"].get<bool>() },
						{ "name", entry.contains("name") && entry["name"].is_string() ? entry["name"].get<std::string>() : std::string{} },
						{ "travelCost", TravelCost(a_world, a_playerPos, target) },
						{ "canTravel", canTravel } });
				}
			} catch (...) {
				return nlohmann::json::array();
			}

			return others;
		}

		nlohmann::json BuildHostiles(const MapBounds& a_bounds, RE::TESWorldSpace* a_mapWorld, RE::PlayerCharacter* a_player)
		{
			nlohmann::json hostiles = nlohmann::json::array();
			const auto* processLists = RE::ProcessLists::GetSingleton();
			if (!processLists) {
				return hostiles;
			}

			struct Hostile {
				float distanceSq;
				float xPct;
				float yPct;
			};
			std::vector<Hostile> candidates;
			candidates.reserve(kMaxHostiles);
			const auto playerPos = a_player->GetPosition();
			const bool playerInCombat = a_player->IsInCombat();
			for (const auto& handle : processLists->highActorHandles) {
				const auto actor = handle.get();
				if (!actor || actor.get() == a_player || actor->IsDead(false) || actor->IsDisabled() || actor->IsDeleted()) {
					continue;
				}
				const auto* cell = actor->GetParentCell();
				if (!cell || cell->IsInterior() || !cell->worldSpace || ResolveMapWorld(cell->worldSpace) != a_mapWorld) {
					continue;
				}
				if (!actor->IsInCombat() || (!playerInCombat && actor->currentCombatTarget.get().get() != a_player)) {
					continue;
				}
				if (!actor->GetHostileToActor(a_player)) {
					continue;
				}
				float xPct = 0.0F;
				float yPct = 0.0F;
				const auto pos = actor->GetPosition();
				if (!WorldToPct(a_bounds, pos.x, pos.y, xPct, yPct)) {
					continue;
				}
				const float dx = pos.x - playerPos.x;
				const float dy = pos.y - playerPos.y;
				candidates.push_back({ dx * dx + dy * dy, xPct, yPct });
			}
			const auto nearest = std::min(candidates.size(), kMaxHostiles);
			std::partial_sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(nearest), candidates.end(), [](const Hostile& a_lhs, const Hostile& a_rhs) { return a_lhs.distanceSq < a_rhs.distanceSq; });
			for (std::size_t i = 0; i < nearest; ++i) {
				hostiles.push_back({ { "x", candidates[i].xPct }, { "y", candidates[i].yPct } });
			}
			return hostiles;
		}

		void BuildUpdateJsonWorker()
		{
			REX::INFO("{} [PauseMenuMap] Build: begin", COUI::Session::GetLogPrefix());

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				g_jsonBuffer.clear();
				return;
			}

			auto* world = ResolvePlayerWorld(player);
			const auto bounds = ResolveMapBounds(world);
			if (!bounds.valid) {
				REX::WARN("{} [PauseMenuMap] Build: invalid world bounds (world={})",
					COUI::Session::GetLogPrefix(),
					world ? "set" : "null");
				g_jsonBuffer.clear();
				return;
			}

			if (!g_loggedMarkerDump && world) {
				const auto* mapWorld = ResolveMapWorld(world);
				const auto& map = mapWorld->worldMapData;
				const auto& off = mapWorld->worldMapOffsetData;
				REX::INFO(
					"{} [PauseMenuMap] Bounds cells NW=({},{}) SE=({},{}) usable={}x{} "
					"world=({:.0f},{:.0f})-({:.0f},{:.0f}) width={:.0f} height={:.0f} offset=({:.3f},{:.3f},{:.3f}) scale={:.6f} "
					"minCoords=({:.0f},{:.0f}) maxCoords=({:.0f},{:.0f})",
					COUI::Session::GetLogPrefix(),
					map.nwCellX, map.nwCellY, map.seCellX, map.seCellY,
					map.usableWidth, map.usableHeight,
					bounds.minX, bounds.minY, bounds.maxX, bounds.maxY,
					(bounds.maxX - bounds.minX), (bounds.maxY - bounds.minY),
					off.mapOffsetX, off.mapOffsetY, off.mapOffsetZ, off.mapScale,
					mapWorld->minimumCoords.x, mapWorld->minimumCoords.y,
					mapWorld->maximumCoords.x, mapWorld->maximumCoords.y);
			}

			const bool interior = [&]() {
				if (const auto* cell = player->GetParentCell()) {
					return cell->IsInterior();
				}
				return false;
			}();

			const RE::NiPoint3 pos = PlayerMapPosition(player);
			float playerXPct = 0.0F;
			float playerYPct = 0.0F;
			if (!WorldToPct(bounds, pos.x, pos.y, playerXPct, playerYPct)) {
				g_jsonBuffer.clear();
				return;
			}

			const float rotDeg = player->data.angle.z * kRadToDeg;

			nlohmann::json root;
			root["mapKey"] = bounds.frame->key;
			root["fogGridSize"] = F4T::MapFogMemory::kGridSize;
			root["fogExplored"] = F4T::MapFogMemory::ExploredBase64(bounds.frame->key);
			root["player"] = {
				{ "xPct", playerXPct },
				{ "yPct", playerYPct },
				{ "rotDeg", rotDeg },
			};
			REX::INFO("{} [PauseMenuMap] Build: player ok ({:.1f},{:.1f}) world=({:.0f},{:.0f}) interior={}",
				COUI::Session::GetLogPrefix(), playerXPct, playerYPct, pos.x, pos.y, interior);

			std::uint32_t othersElsewhere = 0;
			bool publicVisible = true;
			nlohmann::json captures;
			root["others"] = BuildOthers(bounds, world, pos, othersElsewhere, publicVisible, captures);
			root["captures"] = std::move(captures);
			root["publicPlayersVisible"] = publicVisible;
			root["othersElsewhere"] = othersElsewhere;
			const auto prefs = F4T::MultiplayerProfile::GetPreferences();
			root["prefs"] = {
				{ "showPlayerMarkers", prefs.showPlayerMarkers },
				{ "showFriendMarkers", prefs.showFriendMarkers },
				{ "mapMarkerSize", prefs.mapMarkerSize == 0 ? "Small" : prefs.mapMarkerSize == 2 ? "Large" : "Medium" },
			};
			root["caps"] = CapsCount(player);
			root["inCombat"] = player->IsInCombat();
			root["hostiles"] = interior ? nlohmann::json::array() : BuildHostiles(bounds, ResolveMapWorld(world), player);
			REX::INFO("{} [PauseMenuMap] Build: others={} elsewhere={}",
				COUI::Session::GetLogPrefix(), root["others"].size(), othersElsewhere);

			nlohmann::json markers = nlohmann::json::array();
			if (!g_markersDisabled) {
				const auto& markerHandles = player->currentMapMarkers;
				const auto totalMarkers = static_cast<std::size_t>(markerHandles.size());
				const auto markerCount = totalMarkers < kMaxMarkers ? totalMarkers : kMaxMarkers;
				REX::INFO("{} [PauseMenuMap] Build: scanning {} markers",
					COUI::Session::GetLogPrefix(), markerCount);

				std::size_t visibleCount = 0;
				std::size_t discoveredCount = 0;

				for (std::size_t i = 0; i < markerCount; ++i) {
					const auto refr = markerHandles[static_cast<std::uint32_t>(i)].get();
					if (!refr || refr->IsDisabled() || refr->IsDeleted()) {
						continue;
					}

					auto* data = TryGetMapMarkerData(refr.get());
					if (!data) {
						continue;
					}

					const auto fields = ResolveMarkerFields(data);

					// Pip-Boy parity: GetMapMarkerVisible ↔ Visible bit.
					if ((fields.flags & kFlagVisible) == 0) {
						continue;
					}
					++visibleCount;

					if (!IsMarkerInWorldSpace(refr.get(), world)) {
						continue;
					}

					const bool discovered = (fields.flags & kFlagCanTravelTo) != 0;
					if (discovered) {
						++discoveredCount;
					}

					const auto worldPos = ResolveMarkerWorldPos(refr.get());
					float xPct = 0.0F;
					float yPct = 0.0F;
					if (!WorldToPct(bounds, worldPos.x, worldPos.y, xPct, yPct)) {
						continue;
					}

					const char* locName = SafeGetLocationName(data);
					const std::string name = locName ? locName : "";

					const auto type = static_cast<std::uint8_t>(OverrideTypeFromName(fields.type, name.c_str()));
					const bool highlighted = IsHighlightedType(type);
					const auto& icons = IconsFor(type);
					const char* iconFile = discovered ? icons.discovered : icons.undiscovered;

					if (!g_loggedMarkerDump) {
						const auto* cell = refr->GetParentCell();
						REX::INFO(
							"{} [PauseMenuMap] Marker[{}] form=0x{:08X} flags=0x{:02X} type={} ({}) discovered={} "
							"name=\"{}\" world=({:.0f},{:.0f},{:.0f}) runtime=({:.0f},{:.0f}) pct=({:.1f},{:.1f}) cell={}",
							COUI::Session::GetLogPrefix(),
							i,
							refr->GetFormID(),
							fields.flags,
							type,
							icons.typeName,
							discovered,
							name,
							worldPos.x, worldPos.y, worldPos.z,
							refr->data.location.x, refr->data.location.y,
							xPct, yPct,
							cell ? (cell->IsInterior() ? "interior" : "exterior") : "null");
					}

					markers.push_back({
						{ "id", std::format("0x{:08X}", refr->GetFormID()) },
						{ "name", name },
						{ "type", icons.typeName },
						{ "icon", std::string(kIconBase) + iconFile },
						{ "xPct", xPct },
						{ "yPct", yPct },
						{ "discovered", discovered },
						{ "highlighted", highlighted },
					});
				}

				if (!g_loggedMarkerDump) {
					g_loggedMarkerDump = true;
				}

				REX::INFO("{} [PauseMenuMap] Build: visible={} discovered={} emitted={}",
					COUI::Session::GetLogPrefix(), visibleCount, discoveredCount, markers.size());
			}
			root["markers"] = std::move(markers);
			REX::INFO("{} [PauseMenuMap] Build: markers={}",
				COUI::Session::GetLogPrefix(), root["markers"].size());

			g_jsonBuffer = F4T::ViewJson::Dump(root);
		}

		// POD-only function so MSVC allows SEH.
		int BuildUpdateJsonProtected()
		{
			__try {
				BuildUpdateJsonWorker();
				return 1;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				g_markersDisabled = true;
				g_jsonBuffer.clear();
				return 0;
			}
		}
	}

	void StartStreaming()
	{
		g_streaming = true;
		g_tickCounter = 0;
		g_loggedMarkerDump = false;
		g_fastTravelCloseRequested = false;
		{
			std::lock_guard lock(g_pendingTravelMutex);
			g_pendingTravelMarkerId.clear();
			g_pendingTravelPlayerId.clear();
		}
		REX::INFO("{} [PauseMenuMap] Streaming started.", COUI::Session::GetLogPrefix());
	}

	void StopStreaming()
	{
		if (g_streaming) {
			REX::INFO("{} [PauseMenuMap] Streaming stopped.", COUI::Session::GetLogPrefix());
		}
		g_streaming = false;
		{
			std::lock_guard lock(g_pendingTravelMutex);
			g_pendingTravelMarkerId.clear();
			g_pendingTravelPlayerId.clear();
		}
		// Keep g_deferredTravelFormId — travel runs after PauseMenu closes.
	}

	bool IsStreaming()
	{
		return g_streaming;
	}

	void RequestFastTravel(std::string_view a_markerId)
	{
		std::lock_guard lock(g_pendingTravelMutex);
		g_pendingTravelMarkerId = std::string{ a_markerId };
		g_pendingTravelPlayerId.clear();
		REX::INFO("{} [PauseMenuMap] Fast travel requested for \"{}\"",
			COUI::Session::GetLogPrefix(), a_markerId);
	}

	void RequestTravelToPlayer(std::string_view a_playerId, std::int32_t a_cost)
	{
		std::lock_guard lock(g_pendingTravelMutex);
		g_pendingTravelPlayerId = std::string{ a_playerId };
		g_pendingTravelCost = a_cost;
		g_pendingTravelMarkerId.clear();
		REX::INFO("{} [PauseMenuMap] Travel to player requested for \"{}\"",
			COUI::Session::GetLogPrefix(), a_playerId);
	}

	void SetTeleportBeganCallback(void (*a_callback)())
	{
		g_teleportBeganCallback.store(a_callback);
	}

	bool ConsumeFastTravelCloseRequest()
	{
		if (!g_fastTravelCloseRequested) {
			return false;
		}
		g_fastTravelCloseRequested = false;
		return true;
	}

	void TickDeferredFastTravel()
	{
		ExecuteDeferredFastTravel();
	}

	std::string BuildUpdateJson()
	{
		g_jsonBuffer.clear();
		if (!BuildUpdateJsonProtected()) {
			REX::WARN("{} [PauseMenuMap] Access violation while building map JSON. Retrying without markers.",
				COUI::Session::GetLogPrefix());
			BuildUpdateJsonProtected();
		}
		return g_jsonBuffer;
	}

	void SampleFogOfWar()
	{
		auto* ui = RE::UI::GetSingleton();
		if (ui && ui->GetMenuOpen<RE::LoadingMenu>()) {
			return;
		}
		static auto s_nextSampleAt = std::chrono::steady_clock::time_point{};
		const auto now = std::chrono::steady_clock::now();
		if (now < s_nextSampleAt) {
			return;
		}
		s_nextSampleAt = now + std::chrono::seconds(1);

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const auto* cell = player->GetParentCell();
		if (!cell || cell->IsInterior()) {
			return;
		}
		const auto bounds = ResolveMapBounds(ResolvePlayerWorld(player));
		if (!bounds.known || !bounds.valid) {
			return;
		}
		float xPct = 0.0F;
		float yPct = 0.0F;
		if (WorldToPct(bounds, player->data.location.x, player->data.location.y, xPct, yPct)) {
			F4T::MapFogMemory::MarkExplored(bounds.frame->key, xPct, yPct);
		}
	}

	void Tick()
	{
		if (!g_streaming || !F4T::PrismaUI::IsPauseMenuVisible()) {
			return;
		}

		ProcessPendingFastTravel();

		++g_tickCounter;
		if (g_tickCounter != 1 && (g_tickCounter % 6) != 0) {
			return;
		}

		const auto json = BuildUpdateJson();
		if (json.empty()) {
			return;
		}

		F4T::PrismaUI::DispatchPauseMenuMapUpdate(json);
	}
}
