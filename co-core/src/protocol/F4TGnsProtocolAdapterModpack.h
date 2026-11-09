#pragma once

#include "F4TGnsProtocolAdapter.h"
#include "F4TModpackManifest.h"

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>

namespace F4T::GnsProtocolAdapter::Detail
{
	[[nodiscard]] std::optional<ModpackManifest::Manifest> ReadRequiredModpack(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<ModpackChunksInfo> ReadRequiredModpackChunks(const nlohmann::json& a_packet);
	[[nodiscard]] std::string ReadRequiredModpackFingerprint(const nlohmann::json& a_packet);
	[[nodiscard]] std::string ReadRequiredModpackName(const nlohmann::json& a_packet);
}
