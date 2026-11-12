#pragma once

#include "F4TGnsProtocolAdapter.h"
#include "F4TModpackManifest.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace F4T::GnsManifestFetch
{
	[[nodiscard]] std::optional<ModpackManifest::Manifest> FetchManifest(
		const std::string& a_host,
		std::uint16_t a_port,
		const std::string& a_fingerprint,
		const std::string& a_serverPassword,
		const GnsProtocolAdapter::ModpackChunksInfo& a_chunks,
		const std::function<bool()>& a_cancelRequested = {});
}
