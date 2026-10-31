#include "F4TModpackNeed.h"

#include "F4TContentManifest.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ModpackManifest
{
	namespace
	{
		[[nodiscard]] bool IsRequiredPluginStem(const Manifest& a_required, std::string_view a_stem)
		{
			for (const auto& entry : a_required.entries) {
				if (entry.kind != EntryKind::plugin) {
					continue;
				}
				const auto classified = ClassifyAutoInstallName(entry.name);
				if (classified && classified->kind == AutoInstallFileKind::plugin && EqualsIgnoringAsciiCase(classified->pluginStem, a_stem)) {
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] const Entry* FindEntry(const Manifest& a_manifest, const Entry& a_like)
		{
			for (const auto& entry : a_manifest.entries) {
				if (entry.kind == a_like.kind && entry.name == a_like.name) {
					return &entry;
				}
			}
			return nullptr;
		}

		void Consider(
			const Manifest& a_required,
			const Manifest& a_client,
			const Entry& a_entry,
			NeedReason a_reason,
			NeedVerdict& a_verdict)
		{
			if (a_entry.kind != EntryKind::plugin && a_entry.kind != EntryKind::archive) {
				a_verdict.manual.push_back(ManualFile{ a_entry.name, ManualReason::notAutoInstallable, AutoInstallRefusal::none });
				return;
			}
			const auto refusal = CheckAutoInstallName(a_entry.name);
			if (refusal != AutoInstallRefusal::none) {
				a_verdict.manual.push_back(ManualFile{ a_entry.name, ManualReason::refusedName, refusal });
				return;
			}
			const auto classified = ClassifyAutoInstallName(a_entry.name);
			const bool isPlugin = classified && classified->kind == AutoInstallFileKind::plugin;
			if (!classified || isPlugin != (a_entry.kind == EntryKind::plugin)) {
				a_verdict.manual.push_back(ManualFile{ a_entry.name, ManualReason::refusedName, AutoInstallRefusal::unsupportedType });
				return;
			}
			if (!isPlugin && !IsRequiredPluginStem(a_required, classified->pluginStem)) {
				a_verdict.manual.push_back(ManualFile{ a_entry.name, ManualReason::notACompanion, AutoInstallRefusal::none });
				return;
			}
			if (!F4T::ContentManifest::IsValidFingerprint(a_entry.sha256Hex)) {
				a_verdict.manual.push_back(ManualFile{ a_entry.name, ManualReason::noHash, AutoInstallRefusal::none });
				return;
			}
			if (a_entry.sizeBytes && *a_entry.sizeBytes > kMaximumAutoInstallFileBytes) {
				a_verdict.manual.push_back(ManualFile{ a_entry.name, ManualReason::tooLarge, AutoInstallRefusal::none });
				return;
			}
			NeedFile file{
				a_entry.name,
				classified->kind,
				classified->pluginStem,
				a_reason,
				a_entry.sha256Hex,
				a_entry.sizeBytes,
				{},
				std::nullopt };
			if (a_reason == NeedReason::differentContent) {
				if (const auto* local = FindEntry(a_client, a_entry)) {
					file.replacedSha256Hex = local->sha256Hex;
					file.replacedSizeBytes = local->sizeBytes;
				}
				++a_verdict.replacements;
			}
			a_verdict.download.push_back(std::move(file));
		}
	}

	std::optional<NeedVerdict> BuildNeedVerdict(const Manifest& a_required, const Manifest& a_client)
	{
		try {
			const auto required = NormalizeManifest(a_required);
			const auto client = NormalizeManifest(a_client);
			const auto diff = DiffManifests(a_required, a_client);
			if (!required || !client || !diff) {
				return std::nullopt;
			}
			NeedVerdict verdict;
			for (const auto& entry : diff->missing) {
				Consider(*required, *client, entry, NeedReason::missing, verdict);
			}
			for (const auto& entry : diff->wrongContent) {
				Consider(*required, *client, entry, NeedReason::differentContent, verdict);
			}
			for (const auto& entry : diff->wrongArchive) {
				Consider(*required, *client, entry, NeedReason::differentContent, verdict);
			}
			for (const auto& entry : diff->wrongNative) {
				verdict.manual.push_back(ManualFile{ entry.name, ManualReason::notAutoInstallable, AutoInstallRefusal::none });
			}
			for (const auto& entry : diff->wrongConfig) {
				verdict.manual.push_back(ManualFile{ entry.name, ManualReason::notAutoInstallable, AutoInstallRefusal::none });
			}
			verdict.otherDifferences = diff->unexpected.size() + diff->wrongOrder.size();

			std::sort(verdict.download.begin(), verdict.download.end(), [](const NeedFile& a_lhs, const NeedFile& a_rhs) {
				return a_lhs.name < a_rhs.name;
			});
			std::sort(verdict.manual.begin(), verdict.manual.end(), [](const ManualFile& a_lhs, const ManualFile& a_rhs) {
				return a_lhs.name < a_rhs.name;
			});

			for (const auto& file : verdict.download) {
				if (!file.sizeBytes) {
					++verdict.unknownSizeFiles;
					continue;
				}
				verdict.knownBytes = *file.sizeBytes > kMaximumAutoInstallTotalBytes - std::min(verdict.knownBytes, kMaximumAutoInstallTotalBytes) ?
					kMaximumAutoInstallTotalBytes + 1 :
					verdict.knownBytes + *file.sizeBytes;
			}
			if (verdict.download.size() > kMaximumAutoInstallFiles) {
				verdict.capIssue = NeedCapIssue::tooManyFiles;
			} else if (verdict.knownBytes > kMaximumAutoInstallTotalBytes) {
				verdict.capIssue = NeedCapIssue::totalTooLarge;
			}
			return verdict;
		} catch (...) {
			return std::nullopt;
		}
	}

	bool NeedVerdictCanDownload(const NeedVerdict& a_verdict) noexcept
	{
		return !a_verdict.download.empty() && a_verdict.capIssue == NeedCapIssue::none;
	}

	std::string_view DescribeManualReason(ManualReason a_reason) noexcept
	{
		switch (a_reason) {
		case ManualReason::refusedName:
			return "cannot be installed automatically";
		case ManualReason::notACompanion:
			return "is not an archive of a plugin this server requires";
		case ManualReason::noHash:
			return "has no checksum the download could be verified against";
		case ManualReason::tooLarge:
			return "is larger than the download limit";
		case ManualReason::notAutoInstallable:
			return "is not a plugin or plugin archive";
		}
		return "unknown";
	}

	std::string_view DescribeNeedCapIssue(NeedCapIssue a_issue) noexcept
	{
		switch (a_issue) {
		case NeedCapIssue::none:
			return "within limits";
		case NeedCapIssue::tooManyFiles:
			return "more files than the download limit";
		case NeedCapIssue::totalTooLarge:
			return "larger in total than the download limit";
		}
		return "unknown";
	}
}
