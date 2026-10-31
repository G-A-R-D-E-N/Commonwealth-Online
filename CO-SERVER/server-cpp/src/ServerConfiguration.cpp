#include "ServerConfiguration.h"

#include "F4TContentManifest.h"
#include "F4TIdentityProtocol.h"

#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace CommonwealthOnline::Server::Configuration
{
	Options Parse(const nlohmann::json& a_root, const std::filesystem::path& a_baseDirectory)
	{
		auto options = F4T::ServerConfiguration::Parse(a_root, a_baseDirectory);
		if (const auto modpackPath = F4T::ServerConfiguration::RequiredModpackPath(a_root, a_baseDirectory)) {
			std::string error;
			auto modpack = F4T::ServerConfiguration::LoadRequiredModpack(*modpackPath, error);
			if (!modpack) {
				throw std::invalid_argument(error);
			}
			options.requiredModpackManifestJson = std::move(modpack->manifestJson);
			options.requiredModpackFingerprint = std::move(modpack->fingerprint);
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
			.modpackName = a_options.modpackName,
			.modpackUrl = a_options.requiredModpackUrl
		};
	}
}
