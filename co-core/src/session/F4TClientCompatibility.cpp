#include "F4TClientCompatibility.h"
#include "F4TContentManifest.h"
#include "F4TIdentityProtocol.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::ClientCompatibility
{
	namespace
	{
		const Metadata& Canonical(BuildTarget a_target)
		{
			static const Metadata ae{
				.buildTarget = BuildTarget::AE,
				.clientVersion = std::string{ kClientVersion },
				.falloutRuntimeVersion = std::string{ kAeRuntimeVersion },
				.contentManifestFingerprint = {},
				.capabilities = {}
			};
			static const Metadata ng{
				.buildTarget = BuildTarget::NG,
				.clientVersion = std::string{ kClientVersion },
				.falloutRuntimeVersion = std::string{ kNgRuntimeVersion },
				.contentManifestFingerprint = {},
				.capabilities = {}
			};
			static const Metadata og{
				.buildTarget = BuildTarget::OG,
				.clientVersion = std::string{ kClientVersion },
				.falloutRuntimeVersion = std::string{ kOgRuntimeVersion },
				.contentManifestFingerprint = {},
				.capabilities = {}
			};
			switch (a_target) {
			case BuildTarget::OG:
				return og;
			case BuildTarget::NG:
				return ng;
			case BuildTarget::AE:
			default:
				return ae;
			}
		}

	}

	const Metadata& Current() noexcept
	{
		return AE();
	}

	const Metadata& OldGen() noexcept
	{
		return OG();
	}

	const Metadata& AE() noexcept
	{
		return Canonical(BuildTarget::AE);
	}

	const Metadata& NG() noexcept
	{
		return Canonical(BuildTarget::NG);
	}

	const Metadata& OG() noexcept
	{
		return Canonical(BuildTarget::OG);
	}

	const Metadata* Find(BuildTarget a_target) noexcept
	{
		switch (a_target) {
		case BuildTarget::AE:
		case BuildTarget::NG:
		case BuildTarget::OG:
			return &Canonical(a_target);
		}
		return nullptr;
	}

	std::optional<BuildTarget> TargetFromSelection(bool a_currentSelected, bool a_oldGenSelected) noexcept
	{
		if (a_currentSelected == a_oldGenSelected) {
			return std::nullopt;
		}
		return a_currentSelected ? BuildTarget::AE : BuildTarget::OG;
	}

	std::optional<BuildTarget> TargetFromSelection(bool a_aeSelected, bool a_ngSelected, bool a_ogSelected) noexcept
	{
		const auto selected = static_cast<unsigned>(a_aeSelected) +
		                      static_cast<unsigned>(a_ngSelected) +
		                      static_cast<unsigned>(a_ogSelected);
		if (selected != 1) {
			return std::nullopt;
		}
		return a_aeSelected ? BuildTarget::AE : (a_ngSelected ? BuildTarget::NG : BuildTarget::OG);
	}

	std::optional<BuildTarget> TargetFromRuntime(std::string_view a_runtimeVersion) noexcept
	{
		if (a_runtimeVersion == kOgRuntimeVersion) {
			return BuildTarget::OG;
		}
		if (a_runtimeVersion == kNgRuntimeVersion) {
			return BuildTarget::NG;
		}
		if (a_runtimeVersion == kAeRuntimeVersion) {
			return BuildTarget::AE;
		}
		return std::nullopt;
	}

	bool IsRuntimeCompatible(BuildTarget a_target, std::string_view a_runtimeVersion) noexcept
	{
		const auto detected = TargetFromRuntime(a_runtimeVersion);
		return detected && *detected == a_target;
	}

	bool IsComplete(const Metadata& a_metadata) noexcept
	{
		return !a_metadata.clientVersion.empty() &&
		       !a_metadata.falloutRuntimeVersion.empty() &&
		       F4T::ContentManifest::IsValidFingerprint(a_metadata.contentManifestFingerprint);
	}

	std::optional<nlohmann::json> HelloFields(const Metadata& a_metadata) noexcept
	{
		if (!IsComplete(a_metadata)) {
			return std::nullopt;
		}

		auto capabilities = a_metadata.capabilities;
		capabilities.emplace_back(F4T::Identity::SaveAttestationCapability);
		capabilities.emplace_back(F4T::Identity::SocialNamesCapability);
		capabilities.emplace_back(F4T::Identity::FriendsCapability);
		std::sort(capabilities.begin(), capabilities.end());
		capabilities.erase(std::unique(capabilities.begin(), capabilities.end()), capabilities.end());

		return nlohmann::json{
			{ "clientVersion", a_metadata.clientVersion },
			{ "falloutRuntimeVersion", a_metadata.falloutRuntimeVersion },
			{ "contentManifestFingerprint", a_metadata.contentManifestFingerprint },
			{ "capabilities", std::move(capabilities) }
		};
	}

	std::optional<nlohmann::json> PackagedClientManifest(std::string_view a_fingerprint) noexcept
	{
		if (!F4T::ContentManifest::IsValidFingerprint(a_fingerprint)) {
			return std::nullopt;
		}
		return nlohmann::json{
			{ "clientVersion", std::string{ kClientVersion } },
			{ "contentManifestFingerprint", std::string{ a_fingerprint } }
		};
	}
}
