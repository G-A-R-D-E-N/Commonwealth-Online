#include "ServerConfiguration.h"

#include "F4TContentManifest.h"
#include "F4TIdentityProtocol.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace CommonwealthOnline::Server::Configuration
{
	Options Parse(const nlohmann::json& a_root, const std::filesystem::path& a_baseDirectory, bool a_loadModpack)
	{
		auto options = F4T::ServerConfiguration::Parse(a_root, a_baseDirectory);
		if (a_loadModpack) {
			std::string modpackError;
			auto modpack = F4T::ServerConfiguration::LoadConfiguredRequiredModpack(a_root, a_baseDirectory, modpackError);
			if (!modpackError.empty()) throw std::invalid_argument(modpackError);
			if (modpack) F4T::ServerConfiguration::SetRequiredModpack(options, std::move(*modpack));
		}
		if (const auto sources = a_root.find("nexus_sources"); a_loadModpack && sources != a_root.end()) {
			if (!options.requiredModpackManifestJson) throw std::invalid_argument("nexus_sources requires a required modpack");
			const auto manifest = F4T::ModpackManifest::ParseManifest(*options.requiredModpackManifestJson);
			const auto parsed = manifest ? F4T::NexusSources::Parse(sources->dump(), *manifest) : std::nullopt;
			if (!parsed) throw std::invalid_argument("nexus_sources is invalid or references a file outside the required modpack");
			options.nexusSources = *parsed;
		}
		if (const auto collection = a_root.find("nexus_collection"); a_loadModpack && collection != a_root.end()) {
			if (!options.requiredModpackManifestJson || !collection->is_object() || collection->size() != 2 ||
				!collection->contains("slug") || !(*collection)["slug"].is_string() ||
				!collection->contains("revision") || !(*collection)["revision"].is_number_integer()) throw std::invalid_argument("nexus_collection requires a modpack, slug and revision");
			options.nexusCollection = F4T::NexusSources::ParseCollection((*collection)["slug"].get<std::string>(), (*collection)["revision"].get<std::uint64_t>());
			if (!options.nexusCollection) throw std::invalid_argument("nexus_collection is invalid");
			const auto manifest = F4T::ModpackManifest::ParseManifest(*options.requiredModpackManifestJson);
			if (!manifest || std::any_of(manifest->entries.begin(), manifest->entries.end(), [](const auto& entry) { return !F4T::ContentManifest::IsValidFingerprint(entry.sha256Hex); }))
				throw std::invalid_argument("nexus_collection requires SHA-256 hashes for all required files");
		}
		if (options.questStoryOrderPath) {
			const auto path = a_baseDirectory / *options.questStoryOrderPath;
			std::ifstream file{ path };
			if (!file) {
				throw std::invalid_argument("questStoryOrderPath cannot be read: " + path.string());
			}
			const auto document = nlohmann::json::parse(file, nullptr, false);
			options.questStoryOrder = document.is_discarded() ? std::optional<F4T::QuestStory::Order>{} : F4T::QuestStory::Order::Load(document);
			if (!options.questStoryOrder) {
				throw std::invalid_argument("questStoryOrderPath is not a valid quest story order: " + path.string());
			}
		}
		if (options.questStoryOrderPath) {
			const auto path = a_baseDirectory / *options.questStoryOrderPath;
			std::ifstream file{ path };
			if (!file) {
				throw std::invalid_argument("questStoryOrderPath cannot be read: " + path.string());
			}
			const auto document = nlohmann::json::parse(file, nullptr, false);
			options.questStoryOrder = document.is_discarded() ? std::optional<F4T::QuestStory::Order>{} : F4T::QuestStory::Order::Load(document);
			if (!options.questStoryOrder) {
				throw std::invalid_argument("questStoryOrderPath is not a valid quest story order: " + path.string());
			}
		}
		return options;
	}

	F4T::Identity::Requirements IdentityRequirements(const Options& a_options)
	{
		return F4T::Identity::Requirements{
			.clientVersion = a_options.requiredClientVersion,
			.falloutRuntimeVersion = a_options.requiredFalloutRuntimeVersion,
			.contentManifestFingerprint = a_options.requiredContentManifestFingerprint,
			.capabilities = a_options.requiredCapabilities,
			.resumeLifetimeSeconds = a_options.resumeTokenLifetimeSeconds
				? static_cast<double>(*a_options.resumeTokenLifetimeSeconds)
				: static_cast<double>(a_options.resumeTokenLifetimeHours) * 60.0 * 60.0,
			.serverPasswordSha256 = a_options.serverPassword
				? std::optional{ F4T::ContentManifest::Sha256Hex(*a_options.serverPassword) }
				: std::nullopt,
			.modpackFingerprint = a_options.requiredModpackFingerprint,
			.modpackManifestJson = a_options.requiredModpackManifestJson,
			.modpackViewlessFingerprint = a_options.requiredModpackViewlessFingerprint,
			.modpackViewlessManifestJson = a_options.requiredModpackViewlessManifestJson,
			.modpackViewsOnly = a_options.requiredModpackViewsOnly,
			.modpackName = a_options.modpackName,
			.modpackUrl = a_options.requiredModpackUrl,
			.requireKnownProfile = a_options.requireKnownProfile,
			.requireSteamIdentity = a_options.requireSteamIdentity
		};
	}
}
