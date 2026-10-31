#pragma once

#include "F4TModpackManifest.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ModpackManifest
{
	enum class MismatchKind
	{
		missing,
		unexpected,
		wrongContent,
		wrongNative,
		wrongConfig,
		wrongArchive,
		wrongOrder
	};

	inline constexpr std::size_t kMaximumReportNamesPerSection = 32;
	inline constexpr std::size_t kMaximumReportLines = 20;
	inline constexpr std::size_t kMaximumReportTextBytes = 1024;
	inline constexpr std::string_view kProbeTruncatedNote =
		"Only part of your local files were checked; the rest are listed as required.";

	struct MismatchReportEntry
	{
		MismatchKind kind{ MismatchKind::missing };
		std::string name;
	};

	struct MismatchReport
	{
		std::vector<MismatchReportEntry> entries;
		std::size_t omitted{};
	};

	[[nodiscard]] std::string_view FormatMismatchKind(MismatchKind a_kind);
	[[nodiscard]] std::optional<MismatchReport> BuildModpackMismatchReport(
		const Manifest& a_required,
		const Manifest& a_client);
	[[nodiscard]] std::string FormatModpackMismatchReport(const MismatchReport& a_report);
	[[nodiscard]] std::string FormatRequiredModpackList(const Manifest& a_required, bool a_probeTruncated = false);
	[[nodiscard]] std::string FormatRequiredModpackName(std::string_view a_modpackName);
}
