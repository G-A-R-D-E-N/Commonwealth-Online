#include "F4TModpackMismatchReport.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	using F4T::ModpackManifest::Entry;
	using F4T::ModpackManifest::Manifest;
	using F4T::ModpackManifest::ManifestDiff;
	using F4T::ModpackManifest::MismatchKind;
	using F4T::ModpackManifest::MismatchReport;
	using F4T::ModpackManifest::MismatchReportEntry;

	constexpr std::string_view kDifferencesHeader = "Differences from this server:";
	constexpr std::string_view kAgreementText =
		"Every entry this server requires was found locally; the difference is in load order, extra plugins, configs, or archives.";
	constexpr std::string_view kMismatchFallback = "Your modpack does not match this server.";
	constexpr std::string_view kRequiredHeader = "Client files were not compared on this machine.";
	constexpr std::string_view kRequiredStatus = ": required by server";
	constexpr std::string_view kOverflowSuffix = "  ...and ";
	constexpr std::string_view kOverflowTail = " more";
	constexpr std::size_t kMaximumOverflowBytes = 32;

	std::size_t TotalEntries(const ManifestDiff& a_diff)
	{
		return a_diff.missing.size() + a_diff.unexpected.size() + a_diff.wrongContent.size() +
			a_diff.wrongNative.size() + a_diff.wrongConfig.size() + a_diff.wrongArchive.size() +
			a_diff.wrongOrder.size();
	}

	void AppendOverflow(std::string& a_text, std::size_t a_remaining)
	{
		if (a_remaining == 0) {
			return;
		}
		a_text += kOverflowSuffix;
		a_text += std::to_string(a_remaining);
		a_text += kOverflowTail;
		a_text += '\n';
	}

	bool Fits(std::string_view a_text, std::string_view a_line)
	{
		return a_text.size() + a_line.size() + kMaximumOverflowBytes <= F4T::ModpackManifest::kMaximumReportTextBytes;
	}
}

namespace F4T::ModpackManifest
{
	std::string_view FormatMismatchKind(MismatchKind a_kind)
	{
		switch (a_kind) {
		case MismatchKind::missing:
			return "Missing";
		case MismatchKind::unexpected:
			return "Unexpected";
		case MismatchKind::wrongContent:
			return "Different content";
		case MismatchKind::wrongNative:
			return "Different native plugin";
		case MismatchKind::wrongConfig:
			return "Different config";
		case MismatchKind::wrongArchive:
			return "Different archive";
		case MismatchKind::wrongOrder:
		default:
			return "Wrong load order";
		}
	}

	std::optional<MismatchReport> BuildModpackMismatchReport(const Manifest& a_required, const Manifest& a_client)
	{
		try {
			const auto diff = DiffManifests(a_required, a_client);
			if (!diff) {
				return std::nullopt;
			}

			const std::array<std::pair<MismatchKind, const std::vector<Entry>*>, 7> sections{
				std::pair{ MismatchKind::missing, &diff->missing },
				std::pair{ MismatchKind::unexpected, &diff->unexpected },
				std::pair{ MismatchKind::wrongOrder, &diff->wrongOrder },
				std::pair{ MismatchKind::wrongContent, &diff->wrongContent },
				std::pair{ MismatchKind::wrongNative, &diff->wrongNative },
				std::pair{ MismatchKind::wrongArchive, &diff->wrongArchive },
				std::pair{ MismatchKind::wrongConfig, &diff->wrongConfig }
			};

			MismatchReport report;
			for (const auto& [kind, entries] : sections) {
				std::size_t shown = 0;
				for (const auto& entry : *entries) {
					if (shown == kMaximumReportNamesPerSection) {
						break;
					}
					report.entries.push_back(MismatchReportEntry{ kind, entry.name });
					++shown;
				}
			}
			report.omitted = TotalEntries(*diff) - report.entries.size();
			return report;
		} catch (...) {
			return std::nullopt;
		}
	}

	std::string FormatModpackMismatchReport(const MismatchReport& a_report)
	{
		try {
			if (a_report.entries.empty()) {
				return std::string{ kAgreementText };
			}

			std::string text{ kDifferencesHeader };
			text += '\n';
			std::size_t shown = 0;
			for (const auto& entry : a_report.entries) {
				if (shown == kMaximumReportLines) {
					break;
				}
				const auto line = std::string{ "  " } + std::string{ FormatMismatchKind(entry.kind) } + ": " + entry.name + "\n";
				if (!Fits(text, line)) {
					break;
				}
				text += line;
				++shown;
			}
			AppendOverflow(text, a_report.entries.size() + a_report.omitted - shown);
			return text;
		} catch (...) {
			try {
				return std::string{ kMismatchFallback };
			} catch (...) {
				return std::string{};
			}
		}
	}

	std::string FormatRequiredModpackName(std::string_view a_modpackName)
	{
		if (!IsValidModpackName(a_modpackName)) {
			return {};
		}
		return std::string{ "This server requires the " } + std::string{ a_modpackName } + " modlist.";
	}

	std::string FormatRequiredModpackList(const Manifest& a_required, bool a_probeTruncated)
	{
		try {
			const auto normalized = NormalizeManifest(a_required);
			if (!normalized) {
				return std::string{ kMismatchFallback };
			}

			std::string text;
			if (a_probeTruncated) {
				text += kProbeTruncatedNote;
				text += '\n';
			}
			text += kRequiredHeader;
			text += '\n';
			std::size_t shown = 0;
			for (const auto& entry : normalized->entries) {
				if (shown == kMaximumReportLines) {
					break;
				}
				const auto line = std::string{ "  " } + entry.name + std::string{ kRequiredStatus } + "\n";
				if (!Fits(text, line)) {
					break;
				}
				text += line;
				++shown;
			}
			AppendOverflow(text, normalized->entries.size() - shown);
			return text;
		} catch (...) {
			try {
				return std::string{ kMismatchFallback };
			} catch (...) {
				return std::string{};
			}
		}
	}
}
