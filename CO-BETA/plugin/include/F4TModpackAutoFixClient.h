#pragma once

#include "F4TModpackManifest.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::ModpackAutoFixClient
{
	inline constexpr std::string_view kOfferActionId = "fix_modpack";

	void NoteMismatch(
		std::string_view a_serverKey,
		const std::string& a_fingerprint,
		const ModpackManifest::Manifest& a_required,
		const ModpackManifest::Manifest& a_client);

	void NoteEndpoint(
		const std::string& a_serverKey,
		const std::string& a_host,
		std::uint16_t a_port,
		const std::string& a_joinId,
		const std::string& a_serverName,
		const std::optional<std::string>& a_serverPassword);

	[[nodiscard]] bool CanOffer(const std::string& a_serverKey);
	void Begin(const std::string& a_serverKey);

	void OnPluginLoad();
	void OnGameDataReady();
}
