#include "F4TGnsProtocolAdapterModpack.h"

#include "F4TContentManifest.h"
#include "F4TModpackChunks.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace F4T::GnsProtocolAdapter::Detail
{
	std::optional<ModpackChunksInfo> ReadRequiredModpackChunks(const nlohmann::json& a_packet)
	{
		const auto iterator = a_packet.find("requiredModpackChunks");
		if (iterator == a_packet.end() || !iterator->is_object()) {
			return std::nullopt;
		}
		try {
			const auto total = iterator->find("totalBytes");
			const auto count = iterator->find("chunkCount");
			const auto sha = iterator->find("sha256");
			if (total == iterator->end() || !total->is_number_unsigned() ||
				count == iterator->end() || !count->is_number_unsigned() ||
				sha == iterator->end() || !sha->is_string()) {
				return std::nullopt;
			}
			const auto totalBytes = total->get<std::uint64_t>();
			const auto chunkCount = count->get<std::uint64_t>();
			const auto sha256 = sha->get<std::string>();
			if (totalBytes == 0 || totalBytes > F4T::ModpackChunks::kMaximumTotalBytes ||
				chunkCount == 0 || chunkCount > F4T::ModpackChunks::kMaximumChunks ||
				!F4T::ContentManifest::IsValidFingerprint(sha256)) {
				return std::nullopt;
			}
			return ModpackChunksInfo{ totalBytes, static_cast<std::uint32_t>(chunkCount), sha256 };
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<ModpackManifest::Manifest> ReadRequiredModpack(const nlohmann::json& a_packet)
	{
		const auto iterator = a_packet.find("requiredModpack");
		if (iterator == a_packet.end() || !iterator->is_object()) {
			return std::nullopt;
		}
		try {
			const auto text = iterator->dump();
			if (text.size() > F4T::ModpackManifest::kMaximumManifestBytes) {
				return std::nullopt;
			}
			return F4T::ModpackManifest::ParseManifest(text);
		} catch (...) {
			return std::nullopt;
		}
	}

	std::string ReadRequiredModpackName(const nlohmann::json& a_packet)
	{
		const auto iterator = a_packet.find("requiredModpackName");
		if (iterator == a_packet.end() || !iterator->is_string()) {
			return {};
		}
		const auto& name = iterator->get_ref<const std::string&>();
		return F4T::ModpackManifest::IsValidModpackName(name) ? name : std::string{};
	}

	std::string ReadRequiredModpackFingerprint(const nlohmann::json& a_packet)
	{
		const auto iterator = a_packet.find("requiredModpackFingerprint");
		if (iterator == a_packet.end() || !iterator->is_string()) {
			return {};
		}
		try {
			const auto& fingerprint = iterator->get_ref<const std::string&>();
			return F4T::ContentManifest::IsValidFingerprint(fingerprint) ? fingerprint : std::string{};
		} catch (...) {
			return {};
		}
	}
}
