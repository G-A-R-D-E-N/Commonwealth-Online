#include "pch.h"

#include "F4TPauseMenuMap.h"

#include "F4TPauseMenuMapIcons.h"
#include "F4TPauseMenuMapMarkerData.h"

#include "F4TPrismaUI.h"
#include "SessionCallbacks.h"

#include <Windows.h>

#include <cctype>
#include <cmath>
#include <format>
#include <mutex>
#include <optional>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

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
		bool g_loggedMarkerDump = false;

		bool g_streaming = false;
		std::uint32_t g_tickCounter = 0;
		bool g_markersDisabled = false;
		std::string g_jsonBuffer;

		std::mutex g_pendingTravelMutex;
		std::string g_pendingTravelMarkerId;
		bool g_fastTravelCloseRequested = false;
		RE::TESFormID g_deferredTravelFormId = 0;
		std::uint32_t g_deferredTravelDelayFrames = 0;

		struct MapBounds
		{
			float minX{};
			float maxX{};
			float minY{};
			float maxY{};
			bool  valid{ false };
		};

		// Pixel frame of the world map within map.png
		// (pause-menu/assets/maps/commonwealth/map.json).
		// Engine worldMapData cells map into this rectangle, not the full 2048² image.
		constexpr float kMapImageSize = 2048.0F;
		constexpr float kMapFrameX0 = 67.46763287F;
		constexpr float kMapFrameY0 = 0.0F;
		constexpr float kMapFrameX1 = 2008.84518231F;
		constexpr float kMapFrameY1 = 2048.0F;

		MapBounds ResolveMapBounds(RE::TESWorldSpace* a_world)
		{
			MapBounds bounds{};
			if (!a_world) {
				return bounds;
			}

			const auto& map = a_world->worldMapData;
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

			// Remap engine-normalized coords into the calibrated image frame.
			const float pixelX = kMapFrameX0 + normX * (kMapFrameX1 - kMapFrameX0);
			const float pixelY = kMapFrameY0 + normY * (kMapFrameY1 - kMapFrameY0);
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
			if (formId == 0) {
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

			// Queue the same target-location state consumed by the player's normal
			// positioning update. This avoids the crashing QueueFastTravel relocation
			// and Papyrus dispatch paths in the current runtime/CommonLib combination.
			auto& target = player->queuedTargetLoc;
			target.world = destinationWorld;
			target.interior = destinationInterior;
			target.transitionTel = nullptr;
			target.location = ResolveMarkerWorldPos(marker.get());
			target.angle = marker->data.angle;
			target.walkThroughDoor = nullptr;
			target.arrivalFunc = nullptr;
			target.arrivalFuncData = nullptr;
			target.furnitureRef.reset();
			target.fastTravelMarker = handle;
			target.fastTravelDist = 0.0F;
			target.resetWeather = true;
			target.allowAutoSave = true;
			target.preventLoadMenu = false;
			target.skyTransition = true;
			target.isValid = true;
			player->positioning = true;

			REX::INFO("{} [PauseMenuMap] Player target location queued for 0x{:08X}",
				COUI::Session::GetLogPrefix(), formId);
			return true;
		}

		void ProcessPendingFastTravel()
		{
			std::string pendingId;
			{
				std::lock_guard lock(g_pendingTravelMutex);
				if (g_pendingTravelMarkerId.empty()) {
					return;
				}
				pendingId.swap(g_pendingTravelMarkerId);
			}

			if (ValidateAndDeferFastTravel(pendingId)) {
				g_fastTravelCloseRequested = true;
			}
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

		nlohmann::json BuildOthers(const MapBounds& a_bounds, RE::TESWorldSpace* a_world, std::uint32_t& a_elsewhere)
		{
			nlohmann::json others = nlohmann::json::array();
			a_elsewhere = 0;
			if (!COUI::Session::HasGetRemotePlayersMapJson()) {
				return others;
			}

			std::string remoteJson;
			std::string error;
			if (!COUI::Session::GetRemotePlayersMapJson(remoteJson, error)) {
				return others;
			}

			try {
				const auto remote = nlohmann::json::parse(remoteJson.empty() ? "[]" : remoteJson);
				if (!remote.is_array()) {
					return others;
				}

				for (const auto& entry : remote) {
					if (!entry.is_object()) {
						continue;
					}

					const auto remoteWorld = ParseFormId(entry, "worldspaceId");
					if (!a_world || !remoteWorld || *remoteWorld != a_world->GetFormID()) {
						++a_elsewhere;
						continue;
					}

					const auto remoteCell = ParseFormId(entry, "cellId");
					if (remoteCell && IsInteriorCell(*remoteCell)) {
						++a_elsewhere;
						continue;
					}

					const float wx = entry.value("x", 0.0F);
					const float wy = entry.value("y", 0.0F);
					float xPct = 0.0F;
					float yPct = 0.0F;
					if (!WorldToPct(a_bounds, wx, wy, xPct, yPct)) {
						continue;
					}

					const auto headingDeg = static_cast<float>(entry.value("angleZ", 0.0) * kRadToDeg);
					others.push_back({ { "id", entry.value("id", 0u) }, { "xPct", xPct }, { "yPct", yPct },
						{ "rotDeg", std::isfinite(headingDeg) ? headingDeg : 0.0F },
						{ "faction", entry.contains("faction") && entry["faction"].is_boolean() && entry["faction"].get<bool>() },
						{ "name", entry.contains("name") && entry["name"].is_string() ? entry["name"].get<std::string>() : std::string{} } });
				}
			} catch (...) {
				return nlohmann::json::array();
			}

			return others;
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
				const auto& map = world->worldMapData;
				const auto& off = world->worldMapOffsetData;
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
					world->minimumCoords.x, world->minimumCoords.y,
					world->maximumCoords.x, world->maximumCoords.y);
			}

			const bool interior = [&]() {
				if (const auto* cell = player->GetParentCell()) {
					return cell->IsInterior();
				}
				return false;
			}();

			const RE::NiPoint3 pos = interior ? player->exteriorPosition : player->data.location;
			float playerXPct = 0.0F;
			float playerYPct = 0.0F;
			if (!WorldToPct(bounds, pos.x, pos.y, playerXPct, playerYPct)) {
				g_jsonBuffer.clear();
				return;
			}

			const float rotDeg = player->data.angle.z * kRadToDeg;

			nlohmann::json root;
			root["player"] = {
				{ "xPct", playerXPct },
				{ "yPct", playerYPct },
				{ "rotDeg", rotDeg },
			};
			REX::INFO("{} [PauseMenuMap] Build: player ok ({:.1f},{:.1f}) world=({:.0f},{:.0f}) interior={}",
				COUI::Session::GetLogPrefix(), playerXPct, playerYPct, pos.x, pos.y, interior);

			std::uint32_t othersElsewhere = 0;
			root["others"] = BuildOthers(bounds, world, othersElsewhere);
			root["othersElsewhere"] = othersElsewhere;
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

			g_jsonBuffer = root.dump();
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
		REX::INFO("{} [PauseMenuMap] Fast travel requested for \"{}\"",
			COUI::Session::GetLogPrefix(), a_markerId);
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
