#include "F4TServerConfiguration.h"
#include "F4TServerConfigJson.h"
#include "F4TContentManifest.h"
#include "F4TModOrganizer.h"
#include "F4TModpackManifest.h"
#include "F4TUtf8Path.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace F4T::ServerConfiguration
{
	using namespace JsonRead;

	std::optional<std::filesystem::path> RequiredModpackPath(
		const nlohmann::json& a_root,
		const std::filesystem::path& a_baseDirectory)
	{
		const auto* node = Find(a_root, "required_modpack_path", "requiredModpackPath");
		if (node == nullptr) {
			return std::nullopt;
		}
		if (!node->is_string()) {
			throw std::invalid_argument("required_modpack_path must be a string");
		}
		auto path = std::filesystem::path{ node->get<std::string>() };
		if (path.is_relative()) {
			path = a_baseDirectory / path;
		}
		return path;
	}

	static std::optional<RequiredModpack> RequiredFromManifest(const F4T::ModpackManifest::Manifest& a_manifest, std::string& a_error)
	{
		const auto canonical = F4T::ModpackManifest::SerializeManifest(a_manifest);
		const auto fingerprint = F4T::ModpackManifest::ComputeFingerprint(a_manifest);
		if (!canonical || !fingerprint) { a_error = "Invalid modpack manifest"; return std::nullopt; }
		RequiredModpack required{ *canonical, *fingerprint };
		required.requiresAssetCapability = a_manifest.entries.size() > 8192 || canonical->size() > 2 * 1024 * 1024 || std::any_of(a_manifest.entries.begin(), a_manifest.entries.end(), [](const auto& entry) {
			return entry.kind == F4T::ModpackManifest::EntryKind::asset;
		});
		const auto viewless = F4T::ModpackManifest::WithoutViewEntries(a_manifest);
		if (viewless.entries.size() == a_manifest.entries.size()) return required;
		if (viewless.entries.empty()) { required.viewsOnly = true; return required; }
		const auto viewlessCanonical = F4T::ModpackManifest::SerializeManifest(viewless);
		const auto viewlessFingerprint = F4T::ModpackManifest::ComputeFingerprint(viewless);
		if (!viewlessCanonical || !viewlessFingerprint) { a_error = "Invalid modpack manifest without its resource views"; return std::nullopt; }
		required.viewlessManifestJson = *viewlessCanonical;
		required.viewlessFingerprint = *viewlessFingerprint;
		return required;
	}

	std::optional<RequiredModpack> LoadRequiredModpack(
		const std::filesystem::path& a_path,
		std::string& a_error)
	{
		const auto fail = [&a_path, &a_error](std::string_view a_problem) {
			a_error = "required_modpack_path " + std::string{ a_problem } + ": " + a_path.string();
			return std::nullopt;
		};
		std::error_code error;
		const auto size = std::filesystem::file_size(a_path, error);
		if (error) {
			return fail("could not be read");
		}
		if (size > F4T::ModpackManifest::kMaximumManifestBytes) {
			return fail("is larger than the 8 MiB manifest limit");
		}
		std::ifstream input(a_path, std::ios::binary);
		if (!input) {
			return fail("could not be opened");
		}
		const std::string text{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
		const auto manifest = F4T::ModpackManifest::ParseManifest(text);
		if (!manifest) {
			return fail("is not a valid modpack manifest");
		}
		return RequiredFromManifest(*manifest, a_error);
	}

	std::optional<RequiredModpack> LoadConfiguredRequiredModpack(
		const nlohmann::json& a_root,
		const std::filesystem::path& a_baseDirectory,
		std::string& a_error)
	{
		a_error.clear();
		(void)HasConfiguredRequiredModpack(a_root, a_baseDirectory);
		const auto path = RequiredModpackPath(a_root, a_baseDirectory);
		const auto instance = ReadOptionalString(Find(a_root, "required_mo2_instance", "requiredMo2Instance"), "required_mo2_instance");
		const auto profile = ReadOptionalString(Find(a_root, "required_mo2_profile", "requiredMo2Profile"), "required_mo2_profile");
		if (path) return LoadRequiredModpack(*path, a_error);
		if (!instance) return std::nullopt;
		auto directory = F4T::Utf8Path::Decode(*instance);
		if (directory.is_relative()) directory = a_baseDirectory / directory;
		const auto hashCache = a_baseDirectory.empty() ? std::filesystem::path{} : a_baseDirectory / "mo2-hash-cache.txt";
		const auto manifest = F4T::ModOrganizer::BuildProfileManifest(directory, *profile, a_error, hashCache);
		if (!manifest) return std::nullopt;
		auto required = RequiredFromManifest(*manifest, a_error);
		if (required) required->requiresAssetCapability = true;
		return required;
	}

	bool HasConfiguredRequiredModpack(const nlohmann::json& a_root, const std::filesystem::path& a_baseDirectory)
	{
		const auto path = RequiredModpackPath(a_root, a_baseDirectory);
		const auto instance = ReadOptionalString(Find(a_root, "required_mo2_instance", "requiredMo2Instance"), "required_mo2_instance");
		const auto profile = ReadOptionalString(Find(a_root, "required_mo2_profile", "requiredMo2Profile"), "required_mo2_profile");
		if (path && (instance || profile)) throw std::invalid_argument("Choose either required_modpack_path or an MO2 profile, not both");
		if (instance.has_value() != profile.has_value()) throw std::invalid_argument("Both required_mo2_instance and required_mo2_profile are needed");
		if (profile && (*profile == "." || *profile == ".." || profile->find_first_of("/\\:") != std::string::npos ||
			std::any_of(profile->begin(), profile->end(), [](unsigned char ch) { return ch < 0x20 || ch == 0x7f; }))) throw std::invalid_argument("Invalid MO2 profile name");
		return path.has_value() || instance.has_value();
	}

	bool HasConfiguredMo2Profile(const nlohmann::json& a_root, const std::filesystem::path& a_baseDirectory)
	{
		(void)HasConfiguredRequiredModpack(a_root, a_baseDirectory);
		return ReadOptionalString(Find(a_root, "required_mo2_instance", "requiredMo2Instance"), "required_mo2_instance").has_value();
	}

	void SetRequiredModpack(Options& a_options, RequiredModpack a_modpack)
	{
		const std::string capability{ F4T::ModpackManifest::kAssetCapability };
		if (a_modpack.requiresAssetCapability && std::find(a_options.requiredCapabilities.begin(), a_options.requiredCapabilities.end(), capability) == a_options.requiredCapabilities.end()) {
			a_options.requiredCapabilities.push_back(capability);
		}
		a_options.requiredModpackManifestJson = std::move(a_modpack.manifestJson);
		a_options.requiredModpackFingerprint = std::move(a_modpack.fingerprint);
		a_options.requiredModpackViewlessManifestJson = std::move(a_modpack.viewlessManifestJson);
		a_options.requiredModpackViewlessFingerprint = std::move(a_modpack.viewlessFingerprint);
		a_options.requiredModpackViewsOnly = a_modpack.viewsOnly;
	}

	std::optional<std::string> RequiredModpackError(
		const nlohmann::json& a_root,
		const std::filesystem::path& a_baseDirectory)
	{
		try {
			(void)HasConfiguredRequiredModpack(a_root, a_baseDirectory);
			const auto path = RequiredModpackPath(a_root, a_baseDirectory);
			std::string error;
			if (path) (void)LoadRequiredModpack(*path, error);
			if (!error.empty()) return error;
			return std::nullopt;
		} catch (const std::exception& exception) {
			return std::string{ exception.what() };
		}
	}
}
