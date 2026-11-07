#pragma once

#include "F4TLoadOrder.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace F4T::LoadOrderSync
{
	struct CapturedPlugin
	{
		std::string name;
		std::uint32_t compileIndex{};
		bool light{};
	};

	void CaptureLoadedPlugins();
	[[nodiscard]] std::vector<CapturedPlugin> CapturedPlugins();
	[[nodiscard]] std::vector<std::string> LoadedPluginNames();
	[[nodiscard]] F4T::LoadOrder::PluginList GatherLocalPlugins();

	void NoteWelcome();
	bool TryHandlePacket(const nlohmann::json& a_packet);
	void TranslateInboundPacket(nlohmann::json& a_packet);
	std::uint32_t ToLocal(std::uint32_t a_playerId, std::uint32_t a_formId);
	std::string Differences(std::uint32_t a_playerId);
	void Forget(std::uint32_t a_playerId);
	void Reset();
}
