#include "pch.h"

#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TEquipmentState.h"

// Source:https://git.nomadicinteractive.dev/GARDEN/CommonwealthOnline-Matrix/src/commit/e78801911c6d0d9628a49773236bb4874a62402e/CO-BETA/plugin/src/F4TNetworking.cpp; protocol field names and encoding must remain unchanged.
#include "F4TPvPCombatContract.h"
#include "F4TDebugLogging.h"
#include "F4TNetworkValidation.h"
#include "F4TReceivePolicy.h"
#include "F4TRemoteNpcState.h"
#include "F4TRemotePlayerState.h"
#include "F4TReplicationRuntime.h"
#include "F4TSessionLaunch.h"
#include "F4TUIClient.h"
#include "F4TWorldStateSync.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace
{
	std::mutex g_packetDiagnosticMutex;
	constexpr auto kRemoteUpdateLogInterval = 1s;
	constexpr auto kWarningLogInterval = 2s;
	std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> g_lastRemoteUpdateLogTimes;
	std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> g_lastRemoteMovementStateLogTimes;
	std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_lastWarningLogTimes;
}

namespace F4T::Networking::Detail
{
	std::string GetLocalPlayerLogMessage(std::string_view a_message)
	{
		return F4T::Networking::GetLocalPlayerLogPrefix() + " " + std::string(a_message);
	}

	void LogInfoWithLocalPlayerPrefix(std::string_view a_message)
	{
		F4T::DebugLogging::Write("Networking", GetLocalPlayerLogMessage(a_message));
	}

	void LogStatusWithLocalPlayerPrefix(std::string_view a_message)
	{
		REX::INFO("{}", GetLocalPlayerLogMessage(a_message));
	}

	void LogWarningWithLocalPlayerPrefix(std::string_view a_message)
	{
		const auto message = GetLocalPlayerLogMessage(a_message);
		REX::WARN(std::string_view{ message });
	}

	bool ShouldLogWarning(std::string a_key)
	{
		const std::scoped_lock lock(g_packetDiagnosticMutex);
		const auto now = std::chrono::steady_clock::now();
		const auto lastLog = g_lastWarningLogTimes.find(a_key);
		if (lastLog != g_lastWarningLogTimes.end() && now - lastLog->second < kWarningLogInterval) {
			return false;
		}

		g_lastWarningLogTimes[std::move(a_key)] = now;
		return true;
	}

	void LogThrottledWarning(const std::string& a_key, std::string_view a_message)
	{
		if (ShouldLogWarning(a_key)) {
			LogWarningWithLocalPlayerPrefix(a_message);
		}
	}

	std::optional<std::uint32_t> ReadPlayerId(const Json& a_packet, const char* a_fieldName)
	{
		const auto playerId = a_packet.find(a_fieldName);
		if (playerId == a_packet.end()) {
			return std::nullopt;
		}

		try {
			if (playerId->is_number_integer()) {
				const auto value = playerId->get<std::int64_t>();
				if (value >= 0 && value <= (std::numeric_limits<std::uint32_t>::max)()) {
					return static_cast<std::uint32_t>(value);
				}
			}

			if (playerId->is_string()) {
				const auto text = playerId->get<std::string>();
				std::uint32_t value = 0;
				const auto* begin = text.data();
				const auto* end = begin + text.size();
				const auto result = std::from_chars(begin, end, value);
				if (result.ec == std::errc{} && result.ptr == end) {
					return value;
				}
			}
		} catch (const std::exception&) {
			return std::nullopt;
		}

		return std::nullopt;
	}

	std::optional<double> ReadDouble(const Json& a_packet, const char* a_fieldName)
	{
		const auto field = a_packet.find(a_fieldName);
		if (field == a_packet.end()) {
			return std::nullopt;
		}

		try {
			if (field->is_number()) {
				const auto value = field->get<double>();
				return std::isfinite(value) ? std::optional<double>{ value } : std::nullopt;
			}

			if (field->is_string()) {
				const auto text = field->get<std::string>();
				double value = 0.0;
				const auto* begin = text.data();
				const auto* end = begin + text.size();
				const auto result = std::from_chars(begin, end, value);
				if (result.ec == std::errc{} && result.ptr == end && std::isfinite(value)) {
					return value;
				}
			}
		} catch (const std::exception&) {
			return std::nullopt;
		}

		return std::nullopt;
	}

	std::optional<bool> ReadBool(const Json& a_packet, const char* a_fieldName)
	{
		const auto field = a_packet.find(a_fieldName);
		if (field == a_packet.end()) {
			return std::nullopt;
		}

		if (field->is_boolean()) {
			return field->get<bool>();
		}

		return std::nullopt;
	}

	std::optional<std::string> ReadString(const Json& a_packet, const char* a_fieldName)
	{
		const auto field = a_packet.find(a_fieldName);
		if (field == a_packet.end() || !field->is_string()) {
			return std::nullopt;
		}

		return field->get<std::string>();
	}

	std::optional<std::uint32_t> ReadUint32(const Json& a_packet, const char* a_fieldName)
	{
		const auto field = a_packet.find(a_fieldName);
		if (field == a_packet.end()) {
			return std::nullopt;
		}

		try {
			if (field->is_number_integer()) {
				const auto value = field->get<std::int64_t>();
				if (value >= 0 && value <= (std::numeric_limits<std::uint32_t>::max)()) {
					return static_cast<std::uint32_t>(value);
				}
			}
			if (field->is_string()) {
				const auto text = field->get<std::string>();
				std::uint32_t value = 0;
				const auto* begin = text.data();
				const auto* end = begin + text.size();
				const auto result = std::from_chars(begin, end, value);
				if (result.ec == std::errc{} && result.ptr == end) {
					return value;
				}
			}
		} catch (...) {
		}

		return std::nullopt;
	}

	std::optional<float> ReadFloat(const Json& a_packet, const char* a_fieldName)
	{
		const auto field = a_packet.find(a_fieldName);
		if (field == a_packet.end() || !field->is_number()) {
			return std::nullopt;
		}

		try {
			const auto value = field->get<double>();
			if (std::isfinite(value) &&
				std::fabs(value) <= static_cast<double>((std::numeric_limits<float>::max)())) {
				return static_cast<float>(value);
			}
		} catch (...) {
		}

		return std::nullopt;
	}

	bool ShouldLogRemoteUpdate(std::uint32_t a_playerId)
	{
		if (!F4T::DebugLogging::IsEnabled()) {
			return false;
		}
		const std::scoped_lock lock(g_packetDiagnosticMutex);
		const auto now = std::chrono::steady_clock::now();
		const auto lastLog = g_lastRemoteUpdateLogTimes.find(a_playerId);
		if (lastLog != g_lastRemoteUpdateLogTimes.end() && now - lastLog->second < kRemoteUpdateLogInterval) {
			return false;
		}

		g_lastRemoteUpdateLogTimes[a_playerId] = now;
		return true;
	}

	bool ShouldLogRemoteMovementState(std::uint32_t a_playerId)
	{
		if (!F4T::DebugLogging::IsEnabled()) {
			return false;
		}
		const std::scoped_lock lock(g_packetDiagnosticMutex);
		const auto now = std::chrono::steady_clock::now();
		const auto lastLog = g_lastRemoteMovementStateLogTimes.find(a_playerId);
		if (lastLog != g_lastRemoteMovementStateLogTimes.end() && now - lastLog->second < kRemoteUpdateLogInterval) {
			return false;
		}

		g_lastRemoteMovementStateLogTimes[a_playerId] = now;
		return true;
	}

	std::optional<std::string_view> GetMissingRequiredTransformField(const Json& a_packet)
	{
		constexpr std::array<std::string_view, 7> requiredFields{
			"type",
			"playerId",
			"x",
			"y",
			"z",
			"angleZ",
			"cellId"
		};

		for (const auto fieldName : requiredFields) {
			if (a_packet.find(fieldName) == a_packet.end()) {
				return fieldName;
			}
		}

		return std::nullopt;
	}

	std::optional<std::uint32_t> ParseHexFormId(std::string_view a_hex)
	{
		if (a_hex.empty()) {
			return std::nullopt;
		}

		std::uint32_t value = 0;
		const auto* begin = a_hex.data();
		const auto* end = begin + a_hex.size();
		const auto result = std::from_chars(begin, end, value, 16);
		if (result.ec != std::errc{} || result.ptr != end) {
			return std::nullopt;
		}

		return value;
	}

	std::vector<F4T::RemotePlayerState::RemoteActionEvent> ReadActionEvents(const Json& a_packet)
	{
		std::vector<F4T::RemotePlayerState::RemoteActionEvent> actionEvents;
		const auto events = a_packet.find("actionEvents");
		if (events == a_packet.end() || !events->is_array()) {
			return actionEvents;
		}

		constexpr std::size_t kMaximumActionEvents = 16;
		for (std::size_t index = 0; index < events->size() && index < kMaximumActionEvents; ++index) {
			const auto& eventJson = (*events)[index];
			if (!eventJson.is_object()) {
				continue;
			}

			const auto type = ReadUint32(eventJson, "type");
			const auto sequence = ReadUint32(eventJson, "sequence");
			const auto eventName = ReadString(eventJson, "eventName");
			if (!type || !sequence || *sequence == 0 || !eventName ||
				!F4T::NetworkValidation::IsAllowedRemoteAction(*type, *eventName)) {
				continue;
			}

			F4T::RemotePlayerState::RemoteActionEvent event{};
			event.type = *type;
			event.sequence = *sequence;
			event.eventName = *eventName;
			event.captureTime = std::chrono::steady_clock::now();
			actionEvents.push_back(std::move(event));
		}

		return actionEvents;
	}

	std::vector<F4T::RemotePlayerState::RemoteEquippedItem> ReadEquippedItems(
		const Json& a_packet,
		bool& a_hasEquipmentUpdate)
	{
		a_hasEquipmentUpdate = false;
		std::vector<F4T::RemotePlayerState::RemoteEquippedItem> equippedItems;

		const auto items = a_packet.find("equippedItems");
		if (items == a_packet.end()) {
			return equippedItems;
		}

		auto decoded = F4T::EquipmentState::Decode(*items);
		if (!decoded) {
			LogThrottledWarning("transform_equippedItems_invalid", "Ignoring invalid equipment update.");
			return equippedItems;
		}
		a_hasEquipmentUpdate = true;
		return std::move(*decoded);
	}

	F4T::PowerArmorSync::PowerArmorState ReadPowerArmor(
		const Json& a_packet,
		bool& a_hasPowerArmorUpdate)
	{
		a_hasPowerArmorUpdate = false;
		F4T::PowerArmorSync::PowerArmorState powerArmor;

		const auto node = a_packet.find("powerArmor");
		if (node == a_packet.end()) {
			return powerArmor;
		}

		auto decoded = F4T::PowerArmorSync::Decode(*node);
		if (!decoded) {
			LogThrottledWarning("transform_powerArmor_invalid", "Ignoring invalid power armor update.");
			return powerArmor;
		}
		a_hasPowerArmorUpdate = true;
		return std::move(*decoded);
	}

	std::uint8_t ReadColorChannel(const Json& a_object, const char* a_fieldName, std::uint8_t a_default = 0)
	{
		const auto value = ReadDouble(a_object, a_fieldName);
		if (!value || !std::isfinite(*value)) {
			return a_default;
		}

		return static_cast<std::uint8_t>(std::clamp(*value, 0.0, 255.0));
	}

	void ReadFloat3(const Json& a_object, const char* a_fieldName, float (&a_out)[3])
	{
		const auto field = a_object.find(a_fieldName);
		if (field == a_object.end() || !field->is_array()) {
			return;
		}

		const auto count = std::min<std::size_t>(field->size(), 3);
		for (std::size_t index = 0; index < count; ++index) {
			const auto& element = (*field)[index];
			if (!element.is_number()) {
				continue;
			}
			const auto number = element.get<double>();
			if (std::isfinite(number) &&
				std::fabs(number) <= static_cast<double>((std::numeric_limits<float>::max)())) {
				a_out[index] = static_cast<float>(number);
			}
		}
	}

	std::uint32_t ReadOptionalHexFormId(const Json& a_object, const char* a_fieldName)
	{
		const auto text = ReadString(a_object, a_fieldName);
		if (!text || text->empty()) {
			return 0;
		}

		if (const auto formId = ParseHexFormId(*text)) {
			return *formId;
		}

		LogThrottledWarning(
			"transform_appearance_invalid_form_" + std::string(a_fieldName),
			std::format(
				"Ignoring appearance field '{}' with invalid formId '{}'.",
				a_fieldName,
				F4T::DebugLogging::SanitizeLogText(*text, 32)));
		return 0;
	}

	F4T::RemotePlayerState::RemoteAppearance ReadAppearance(const Json& a_packet, bool& a_hasAppearanceUpdate)
	{
		a_hasAppearanceUpdate = false;
		F4T::RemotePlayerState::RemoteAppearance appearance{};

		const auto appearanceObject = a_packet.find("appearance");
		if (appearanceObject == a_packet.end()) {
			return appearance;
		}

		a_hasAppearanceUpdate = true;
		if (!appearanceObject->is_object()) {
			LogThrottledWarning(
				"transform_appearance_not_object",
				"Ignoring transform appearance field because it is not an object.");
			return appearance;
		}

		if (const auto version = ReadDouble(*appearanceObject, "version")) {
			if (std::isfinite(*version) && *version >= 1.0 && *version <= 1000.0) {
				appearance.version = static_cast<std::uint32_t>(*version);
			}
		}
	appearance.raceFormId = ReadOptionalHexFormId(*appearanceObject, "raceFormId");
	appearance.hairColorFormId = ReadOptionalHexFormId(*appearanceObject, "hairColorFormId");
	appearance.facialHairColorFormId = ReadOptionalHexFormId(*appearanceObject, "facialHairColorFormId");
	appearance.complexionFormId = ReadOptionalHexFormId(*appearanceObject, "complexionFormId");
	if (const auto isFemale = ReadBool(*appearanceObject, "isFemale")) {
		appearance.isFemale = *isFemale;
	}

		if (const auto height = ReadDouble(*appearanceObject, "height")) {
			if (std::isfinite(*height) && *height > 0.0 && *height < 10.0) {
				appearance.height = static_cast<float>(*height);
			}
		}

		if (const auto morphWeight = appearanceObject->find("morphWeight");
			morphWeight != appearanceObject->end() && morphWeight->is_object()) {
			appearance.morphWeight.thin = static_cast<float>(ReadDouble(*morphWeight, "thin").value_or(0.0));
			appearance.morphWeight.muscular = static_cast<float>(ReadDouble(*morphWeight, "muscular").value_or(0.0));
			appearance.morphWeight.large = static_cast<float>(ReadDouble(*morphWeight, "large").value_or(0.0));
		}

		if (const auto tintColor = appearanceObject->find("bodyTintColor");
			tintColor != appearanceObject->end() && tintColor->is_object()) {
			appearance.bodyTintColor.r = ReadColorChannel(*tintColor, "r");
			appearance.bodyTintColor.g = ReadColorChannel(*tintColor, "g");
			appearance.bodyTintColor.b = ReadColorChannel(*tintColor, "b");
			appearance.bodyTintColor.a = ReadColorChannel(*tintColor, "a", 255);
		}

		if (const auto headParts = appearanceObject->find("headParts");
			headParts != appearanceObject->end() && headParts->is_array()) {
			for (const auto& headPart : *headParts) {
				if (!headPart.is_string()) {
					continue;
				}
				if (const auto formId = ParseHexFormId(headPart.get<std::string>())) {
					appearance.headPartFormIds.push_back(*formId);
				}
			}
		}

		if (const auto morphs = appearanceObject->find("morphs");
			morphs != appearanceObject->end() && morphs->is_array()) {
			for (const auto& morph : *morphs) {
				if (!morph.is_object()) {
					continue;
				}

				const auto idText = ReadString(morph, "id");
				const auto value = ReadDouble(morph, "value");
				if (!idText || !value || !std::isfinite(*value)) {
					continue;
				}

				if (const auto id = ParseHexFormId(*idText)) {
					appearance.morphSliders.push_back({ *id, static_cast<float>(*value) });
				}
			}
		}

		if (const auto morphRegions = appearanceObject->find("morphRegions");
			morphRegions != appearanceObject->end() && morphRegions->is_array()) {
			for (const auto& value : *morphRegions) {
				if (!value.is_number()) {
					continue;
				}
				const auto number = value.get<double>();
				appearance.morphRegionSliders.push_back(std::isfinite(number) ? static_cast<float>(number) : 0.0F);
			}
		}

		if (const auto facialBoneMorphs = appearanceObject->find("facialBoneMorphs");
			facialBoneMorphs != appearanceObject->end() && facialBoneMorphs->is_array()) {
			for (const auto& morph : *facialBoneMorphs) {
				if (!morph.is_object()) {
					continue;
				}

				const auto idText = ReadString(morph, "id");
				if (!idText) {
					continue;
				}

				const auto id = ParseHexFormId(*idText);
				if (!id) {
					continue;
				}

				F4T::RemotePlayerState::RemoteAppearanceFacialBoneMorph boneMorph{};
				boneMorph.id = *id;
				ReadFloat3(morph, "position", boneMorph.position);
				ReadFloat3(morph, "rotation", boneMorph.rotation);
				ReadFloat3(morph, "scale", boneMorph.scale);
				appearance.facialBoneMorphs.push_back(boneMorph);
			}
		}

		if (const auto tints = appearanceObject->find("tints");
			tints != appearanceObject->end() && tints->is_array()) {
			for (const auto& tint : *tints) {
				if (!tint.is_object()) {
					continue;
				}

				const auto id = ReadDouble(tint, "id");
				const auto type = ReadDouble(tint, "type");
				const auto value = ReadDouble(tint, "value");
				if (!id || !std::isfinite(*id) || *id < 0.0 || *id > 65535.0) {
					continue;
				}

				F4T::RemotePlayerState::RemoteAppearanceTint entry{};
				entry.id = static_cast<std::uint16_t>(*id);
				entry.type = (type && std::isfinite(*type) && *type >= 0.0 && *type <= 3.0)
				                 ? static_cast<std::uint32_t>(*type)
				                 : 0U;
				entry.value = (value && std::isfinite(*value))
				                  ? static_cast<std::uint8_t>(std::clamp(*value, 0.0, 255.0))
				                  : 0U;

				if (const auto colorText = ReadString(tint, "color"); colorText && !colorText->empty()) {
					if (const auto color = ParseHexFormId(*colorText)) {
						entry.color = *color;
						entry.hasColor = true;
						if (const auto swatch = ReadDouble(tint, "swatch");
							swatch && std::isfinite(*swatch) && *swatch >= 0.0 && *swatch <= 65535.0) {
							entry.swatch = static_cast<std::uint16_t>(*swatch);
						}
					}
				}

				appearance.tints.push_back(entry);
			}
		}

		return appearance;
	}

	void ResetPacketDiagnostics()
	{
		const std::scoped_lock lock(g_packetDiagnosticMutex);
		g_lastRemoteUpdateLogTimes.clear();
		g_lastRemoteMovementStateLogTimes.clear();
	}

	void ErasePacketDiagnostics(std::uint32_t a_playerId)
	{
		const std::scoped_lock lock(g_packetDiagnosticMutex);
		g_lastRemoteUpdateLogTimes.erase(a_playerId);
		g_lastRemoteMovementStateLogTimes.erase(a_playerId);
	}
}
