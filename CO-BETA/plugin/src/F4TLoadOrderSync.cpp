#include "pch.h"

#include "F4TLoadOrderSync.h"

#include "F4TNetworkingInternal.h"

#include "F4TLoadOrder.h"
#include "F4TFormIdentity.h"

// constraint: source:CO-BETA/plugin/lib/commonlibf4/include/RE/T/TESDataHandler.h and RE/T/TESFile.h define the plugin API used here.
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESFile.h"

#include <array>
#include <charconv>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace
{
	using Json = nlohmann::json;
	using F4T::LoadOrder::PluginKind;
	using F4T::LoadOrder::PluginList;

	constexpr int kMaximumTranslateDepth = 16;
	constexpr std::size_t kMaximumTranslateArray = 4096;

	constexpr std::array<std::string_view, 11> kFormIdKeys{
		"formId",
		"cellId",
		"worldspaceId",
		"baseFormId",
		"sourceFormId",
		"weaponFormId",
		"raceFormId",
		"hairColorFormId",
		"facialHairColorFormId",
		"complexionFormId",
		"weatherFormId"
	};

	F4T::LoadOrder::Registry g_registry;
	std::mutex g_capturedPluginsMutex;
	std::vector<F4T::LoadOrderSync::CapturedPlugin> g_capturedPlugins;

	PluginList GatherFromCaptured(const std::vector<F4T::LoadOrderSync::CapturedPlugin>& a_captured)
	{
		PluginList list;
		for (const auto& plugin : a_captured) {
			if (plugin.light) {
				list.light.push_back({
					PluginKind::light,
					static_cast<std::uint16_t>(plugin.compileIndex),
					plugin.name
				});
			} else {
				list.full.push_back({
					PluginKind::full,
					static_cast<std::uint8_t>(plugin.compileIndex),
					plugin.name
				});
			}
		}
		return list;
	}

	bool IsFormIdKey(std::string_view a_key)
	{
		for (const auto key : kFormIdKeys) {
			if (a_key == key) {
				return true;
			}
		}
		return false;
	}

	void TranslateStringValue(Json& a_value, std::uint32_t a_playerId)
	{
		if (!a_value.is_string()) {
			return;
		}
		const auto text = a_value.get<std::string>();
		if (text.empty() || text.size() > 8) {
			return;
		}
		std::uint32_t formId = 0;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), formId, 16);
		if (error != std::errc{} || end != text.data() + text.size()) {
			return;
		}
		const auto local = g_registry.ToLocal(a_playerId, formId);
		if (local == formId) {
			return;
		}
		a_value = std::format("{:08X}", local);
	}

	void TranslateNode(Json& a_node, std::uint32_t a_playerId, int a_depth)
	{
		if (a_depth > kMaximumTranslateDepth) {
			return;
		}
		if (a_node.is_object()) {
			for (auto& [key, child] : a_node.items()) {
				if (IsFormIdKey(key)) {
					TranslateStringValue(child, a_playerId);
				} else {
					TranslateNode(child, a_playerId, a_depth + 1);
				}
			}
		} else if (a_node.is_array()) {
			if (a_node.size() > kMaximumTranslateArray) {
				return;
			}
			for (auto& child : a_node) {
				TranslateNode(child, a_playerId, a_depth + 1);
			}
		}
	}

	void TranslateAppearanceIds(Json& a_packet, std::uint32_t a_playerId)
	{
		const auto appearance = a_packet.find("appearance");
		if (appearance == a_packet.end() || !appearance->is_object()) {
			return;
		}
		if (const auto headParts = appearance->find("headParts");
			headParts != appearance->end() && headParts->is_array() &&
			headParts->size() <= kMaximumTranslateArray) {
			for (auto& part : *headParts) {
				TranslateStringValue(part, a_playerId);
			}
		}
		for (const char* arrayKey : { "morphs", "facialBoneMorphs" }) {
			const auto entries = appearance->find(arrayKey);
			if (entries == appearance->end() || !entries->is_array() ||
				entries->size() > kMaximumTranslateArray) {
				continue;
			}
			for (auto& entry : *entries) {
				if (!entry.is_object()) {
					continue;
				}
				if (const auto id = entry.find("id"); id != entry.end()) {
					TranslateStringValue(*id, a_playerId);
				}
			}
		}
	}
}

namespace F4T::LoadOrderSync
{
	F4T::LoadOrder::PluginList GatherLocalPlugins()
	{
		if (const auto captured = CapturedPlugins(); !captured.empty()) {
			return GatherFromCaptured(captured);
		}
		PluginList list;
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			return list;
		}
		for (auto* file : dataHandler->compiledFileCollection.files) {
			if (!file || !file->IsActive() ||
				file->GetCompileIndex() >= F4T::FormIdentity::LightFileMarker) {
				continue;
			}
			list.full.push_back({
				PluginKind::full,
				file->GetCompileIndex(),
				std::string{ file->GetFilename() }
			});
		}
		for (auto* file : dataHandler->compiledFileCollection.smallFiles) {
			if (!file || !file->IsActive()) {
				continue;
			}
			list.light.push_back({
				PluginKind::light,
				file->GetSmallFileCompileIndex(),
				std::string{ file->GetFilename() }
			});
		}
		return list;
	}

	void CaptureLoadedPlugins()
	{
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			return;
		}
		std::vector<CapturedPlugin> captured;
		for (auto* file : dataHandler->compiledFileCollection.files) {
			if (!file || !file->IsActive() ||
				file->GetCompileIndex() >= F4T::FormIdentity::LightFileMarker) {
				continue;
			}
			captured.push_back(CapturedPlugin{ std::string{ file->GetFilename() }, file->GetCompileIndex(), false });
		}
		for (auto* file : dataHandler->compiledFileCollection.smallFiles) {
			if (!file || !file->IsActive()) {
				continue;
			}
			captured.push_back(CapturedPlugin{ std::string{ file->GetFilename() }, file->GetSmallFileCompileIndex(), true });
		}
		std::scoped_lock lock(g_capturedPluginsMutex);
		g_capturedPlugins = std::move(captured);
	}

	std::vector<CapturedPlugin> CapturedPlugins()
	{
		std::scoped_lock lock(g_capturedPluginsMutex);
		return g_capturedPlugins;
	}

	std::vector<std::string> LoadedPluginNames()
	{
		std::vector<std::string> names;
		const auto captured = CapturedPlugins();
		names.reserve(captured.size());
		for (const auto& plugin : captured) {
			names.push_back(plugin.name);
		}
		return names;
	}

	void NoteWelcome()
	{
		auto list = GatherLocalPlugins();
		if (!F4T::LoadOrder::IsValidPluginList(list)) {
			F4T::Networking::Detail::LogWarningWithLocalPlayerPrefix(
				"The local plugin list is unavailable; FormIDs from other players will not be translated.");
			return;
		}
		g_registry.SetLocalList(list);
		const auto packet = F4T::LoadOrder::BuildPluginListPacket(list);
		if (!packet) {
			F4T::Networking::Detail::LogWarningWithLocalPlayerPrefix(
				"The local plugin list is too large to send; FormIDs from other players will not be translated.");
			return;
		}
		if (!F4T::Networking::Detail::SendPacketBytes(*packet)) {
			F4T::Networking::Detail::LogThrottledWarning(
				"load_order_send_failed",
				"Could not announce the local plugin list to the server.");
			return;
		}
		F4T::Networking::Detail::LogInfoWithLocalPlayerPrefix(std::format(
			"Announced the local load order: {} full and {} light plugins.",
			list.full.size(),
			list.light.size()));
	}

	bool TryHandlePacket(const nlohmann::json& a_packet)
	{
		const auto type = F4T::Networking::Detail::ReadString(a_packet, "type");
		if (!type || *type != std::string{ F4T::LoadOrder::kPacketType }) {
			return false;
		}
		const auto playerId = F4T::Networking::Detail::ReadPlayerId(a_packet);
		const auto data = F4T::Networking::Detail::ReadString(a_packet, "data");
		if (!playerId || *playerId == 0 || !data) {
			F4T::Networking::Detail::LogThrottledWarning(
				"load_order_packet_shape",
				"Ignoring pluginList packet without a valid playerId or data.");
			return true;
		}
		auto list = F4T::LoadOrder::DecodePluginList(*data);
		if (!list) {
			F4T::Networking::Detail::LogThrottledWarning(
				"load_order_packet_invalid",
				"Ignoring malformed pluginList packet.");
			return true;
		}
		g_registry.SetRemoteList(*playerId, std::move(*list));
		const auto differences = g_registry.Differences(*playerId);
		if (differences.empty()) {
			F4T::Networking::Detail::LogStatusWithLocalPlayerPrefix(std::format(
				"Player {} runs the same plugins in the same order.",
				*playerId));
		} else {
			F4T::Networking::Detail::LogStatusWithLocalPlayerPrefix(std::format(
				"Player {} load order: {}",
				*playerId,
				differences));
		}
		return true;
	}

	void TranslateInboundPacket(nlohmann::json& a_packet)
	{
		const auto playerId = F4T::Networking::Detail::ReadPlayerId(a_packet);
		if (!playerId || !g_registry.Known(*playerId) || g_registry.Identical(*playerId)) {
			return;
		}
		TranslateNode(a_packet, *playerId, 0);
		TranslateAppearanceIds(a_packet, *playerId);
	}

	std::uint32_t ToLocal(std::uint32_t a_playerId, std::uint32_t a_formId)
	{
		return g_registry.ToLocal(a_playerId, a_formId);
	}

	std::string Differences(std::uint32_t a_playerId)
	{
		return g_registry.Differences(a_playerId);
	}

	void Forget(std::uint32_t a_playerId)
	{
		g_registry.Forget(a_playerId);
	}

	void Reset()
	{
		g_registry.Reset();
	}
}
