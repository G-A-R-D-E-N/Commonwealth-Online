#pragma once

#include "F4TModpackContentPolicy.h"
#include "F4TModpackManifest.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ModpackManifest
{
	enum class NeedReason
	{
		missing,
		differentContent
	};

	enum class ManualReason
	{
		refusedName,
		notACompanion,
		noHash,
		tooLarge,
		notAutoInstallable
	};

	enum class NeedCapIssue
	{
		none,
		tooManyFiles,
		totalTooLarge
	};

	struct NeedFile
	{
		std::string name;
		AutoInstallFileKind kind{ AutoInstallFileKind::plugin };
		std::string pluginStem;
		NeedReason reason{ NeedReason::missing };
		std::string sha256Hex;
		std::optional<std::uint64_t> sizeBytes;
		std::string replacedSha256Hex;
		std::optional<std::uint64_t> replacedSizeBytes;
	};

	struct ManualFile
	{
		std::string name;
		ManualReason reason{ ManualReason::refusedName };
		AutoInstallRefusal refusal{ AutoInstallRefusal::none };
	};

	struct NeedVerdict
	{
		std::vector<NeedFile> download;
		std::vector<ManualFile> manual;
		std::uint64_t knownBytes{};
		std::size_t unknownSizeFiles{};
		std::size_t replacements{};
		NeedCapIssue capIssue{ NeedCapIssue::none };
		std::size_t otherDifferences{};
	};

	[[nodiscard]] std::optional<NeedVerdict> BuildNeedVerdict(const Manifest& a_required, const Manifest& a_client);
	[[nodiscard]] bool NeedVerdictCanDownload(const NeedVerdict& a_verdict) noexcept;
	[[nodiscard]] std::string_view DescribeManualReason(ManualReason a_reason) noexcept;
	[[nodiscard]] std::string_view DescribeNeedCapIssue(NeedCapIssue a_issue) noexcept;
}
