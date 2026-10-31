#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ClientCompatibility
{
	enum class BuildTarget : std::uint8_t
	{
		AE = 1,
		OG = 2,
		NG = 3,
		Current = AE,
		OldGen = OG
	};

	inline constexpr std::string_view kClientVersion = "0.1.0";

	// Protocol-relevant Fallout 4 runtime the client reports for each target.
	inline constexpr std::string_view kOgRuntimeVersion = "1.10.163";
	inline constexpr std::string_view kNgRuntimeVersion = "1.10.984";
	inline constexpr std::string_view kAeRuntimeVersion = "1.11.240";
	inline constexpr std::string_view kCurrentRuntimeVersion = kAeRuntimeVersion;
	inline constexpr std::string_view kOldGenRuntimeVersion = kOgRuntimeVersion;

#if (defined(F4T_TARGET_CURRENT) + defined(F4T_TARGET_AE) + defined(F4T_TARGET_NG) + defined(F4T_TARGET_OG)) > 1
	#    error "Commonwealth Online client target defines are mutually exclusive"
#elif defined(F4T_TARGET_CURRENT) || defined(F4T_TARGET_AE) || defined(F4T_TARGET_NG) || defined(F4T_TARGET_OG)
	inline constexpr bool kBuildTargetSelected = true;
#else
	inline constexpr bool kBuildTargetSelected = false;
#endif

	struct Metadata
	{
		BuildTarget buildTarget{};
		std::string clientVersion;
		std::string falloutRuntimeVersion;
		std::string contentManifestFingerprint;
		std::vector<std::string> capabilities;
	};

	[[nodiscard]] const Metadata& Current() noexcept;
	[[nodiscard]] const Metadata& OldGen() noexcept;
	[[nodiscard]] const Metadata& AE() noexcept;
	[[nodiscard]] const Metadata& NG() noexcept;
	[[nodiscard]] const Metadata& OG() noexcept;
	// nullptr for any BuildTarget value that is not a real target, so an
	// unsupported/ambiguous value can never resolve to partial metadata.
	[[nodiscard]] const Metadata* Find(BuildTarget a_target) noexcept;

	[[nodiscard]] std::optional<BuildTarget> TargetFromSelection(bool a_currentSelected, bool a_oldGenSelected) noexcept;
	[[nodiscard]] std::optional<BuildTarget> TargetFromSelection(bool a_aeSelected, bool a_ngSelected, bool a_ogSelected) noexcept;
	[[nodiscard]] std::optional<BuildTarget> TargetFromRuntime(std::string_view a_runtimeVersion) noexcept;
	[[nodiscard]] bool IsRuntimeCompatible(BuildTarget a_target, std::string_view a_runtimeVersion) noexcept;

	[[nodiscard]] inline std::optional<BuildTarget> ResolveCompileTimeTarget() noexcept
	{
#if (defined(F4T_TARGET_CURRENT) + defined(F4T_TARGET_AE) + defined(F4T_TARGET_NG) + defined(F4T_TARGET_OG)) > 1
		return std::nullopt;
#elif defined(F4T_TARGET_CURRENT) || defined(F4T_TARGET_AE)
		return TargetFromSelection(true, false, false);
#elif defined(F4T_TARGET_NG)
		return TargetFromSelection(false, true, false);
#elif defined(F4T_TARGET_OG)
		return TargetFromSelection(false, false, true);
#else
		return TargetFromSelection(false, false, false);
#endif
	}

	[[nodiscard]] bool IsComplete(const Metadata& a_metadata) noexcept;

	[[nodiscard]] std::optional<nlohmann::json> HelloFields(const Metadata& a_metadata) noexcept;

	// The per-release packaged client manifest file (CommonwealthOnlineClient.json)
	// the plugin reads from its own directory: the release's
	// contentManifestFingerprint plus the canonical clientVersion, so the client
	// attaches exactly what the content-manifest tooling recorded. std::nullopt
	// when the fingerprint is not a valid 64-hex lowercase SHA-256 (fail closed
	// - never emit a packaged manifest for a value the pipeline did not
	// produce). The file is staged outside the fingerprinted content set
	// (Data\F4SE\Plugins\, next to the client DLLs).
	[[nodiscard]] std::optional<nlohmann::json> PackagedClientManifest(std::string_view a_fingerprint) noexcept;
}
