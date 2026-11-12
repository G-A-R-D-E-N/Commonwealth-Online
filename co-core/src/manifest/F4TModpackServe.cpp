#include "F4TModpackServe.h"

#include "F4TContentManifest.h"
#include "F4TModpackContentPolicy.h"

#include <fstream>
#include <iterator>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace F4T::ModpackServe
{
	namespace
	{
		using ModpackManifest::AutoInstallRefusal;
		using ModpackManifest::CheckAutoInstallName;
		using ModpackManifest::EqualsIgnoringAsciiCase;

		[[nodiscard]] std::string Lower(std::string_view a_text)
		{
			std::string lowered{ a_text };
			for (auto& character : lowered) {
				if (character >= 'A' && character <= 'Z') {
					character = static_cast<char>(character - 'A' + 'a');
				}
			}
			return lowered;
		}

		[[nodiscard]] bool IsPlainFileInside(const std::filesystem::path& a_directory, const std::filesystem::path& a_relative)
		{
			std::filesystem::path current = a_directory;
			std::error_code error;
			for (auto part = a_relative.begin(); part != a_relative.end(); ++part) {
				current /= *part;
				const auto status = std::filesystem::symlink_status(current, error);
				if (error || std::filesystem::is_symlink(status)) {
					return false;
				}
				const bool leaf = std::next(part) == a_relative.end();
				if (leaf ? !std::filesystem::is_regular_file(status) : !std::filesystem::is_directory(status)) {
					return false;
				}
			}
			return true;
		}
	}

	bool IsDownloadable(std::string_view a_name, const std::vector<std::string>& a_downloadable)
	{
		for (const auto& allowed : a_downloadable) {
			if (allowed == "*" || EqualsIgnoringAsciiCase(allowed, a_name)) {
				return true;
			}
		}
		return false;
	}

	Catalog BuildCatalog(
		const ModpackManifest::Manifest& a_required,
		std::string_view a_fingerprint,
		const std::filesystem::path& a_directory,
		const std::vector<std::string>& a_downloadable)
	{
		Catalog catalog;
		catalog.fingerprint = std::string{ a_fingerprint };
		try {
			const auto required = ModpackManifest::NormalizeManifest(a_required);
			if (!required) {
				catalog.problems.push_back(CatalogProblem{ {}, "the required modpack manifest is invalid" });
				return catalog;
			}
			const auto viewless = ModpackManifest::WithoutViewEntries(*required);
			if (!viewless.entries.empty() && viewless.entries.size() != required->entries.size()) {
				const auto viewlessFingerprint = ModpackManifest::ComputeFingerprint(viewless);
				if (!viewlessFingerprint) {
					catalog.problems.push_back(CatalogProblem{ {}, "the modpack without its resource views is invalid" });
					return catalog;
				}
				catalog.viewlessFingerprint = *viewlessFingerprint;
			}
			std::error_code error;
			const auto directoryStatus = std::filesystem::symlink_status(a_directory, error);
			if (error || !std::filesystem::is_directory(directoryStatus)) {
				catalog.problems.push_back(CatalogProblem{ {}, "modpack_download_dir is not a plain folder" });
				return catalog;
			}
			std::unordered_map<std::string, std::filesystem::path> present;
			for (std::filesystem::directory_iterator it(a_directory, error), end; !error && it != end; it.increment(error)) {
				present.emplace(Lower(it->path().filename().string()), it->path());
			}
			for (const auto& entry : required->entries) {
				const bool view = entry.kind == ModpackManifest::EntryKind::view;
				const bool flat = entry.kind == ModpackManifest::EntryKind::plugin || entry.kind == ModpackManifest::EntryKind::archive;
				if ((!view && !flat) || entry.sha256Hex.empty() || (flat && CheckAutoInstallName(entry.name) != AutoInstallRefusal::none) ||
					!IsDownloadable(entry.name, a_downloadable)) {
					continue;
				}
				std::filesystem::path source;
				if (view) {
					const auto relative = ModpackManifest::InstallRelativeName(entry.name);
					if (!IsPlainFileInside(a_directory, relative)) {
						catalog.problems.push_back(CatalogProblem{ entry.name, "not in modpack_download_dir" });
						continue;
					}
					source = a_directory / relative;
				} else {
					const auto found = present.find(Lower(entry.name));
					if (found == present.end()) {
						catalog.problems.push_back(CatalogProblem{ entry.name, "not in modpack_download_dir" });
						continue;
					}
					source = found->second;
				}
				const auto status = std::filesystem::symlink_status(source, error);
				if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
					catalog.problems.push_back(CatalogProblem{ entry.name, "is not a plain file" });
					continue;
				}
				const auto size = std::filesystem::file_size(source, error);
				const auto writeTime = std::filesystem::last_write_time(source, error);
				const auto maximum = view ? ModpackManifest::kMaximumViewFileBytes : ModpackManifest::kMaximumAutoInstallFileBytes;
				if (error || size == 0 || size > maximum || (entry.sizeBytes && *entry.sizeBytes != size)) {
					catalog.problems.push_back(CatalogProblem{ entry.name, "has a different size from the manifest or is too large" });
					continue;
				}
				const auto sha = ContentManifest::Sha256HexFile(source);
				if (!sha || *sha != entry.sha256Hex) {
					catalog.problems.push_back(CatalogProblem{ entry.name, "does not match the manifest's SHA-256" });
					continue;
				}
				if (view) {
					std::ifstream stream(source, std::ios::binary);
					const std::string content{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
					if (!stream || !ModpackManifest::IsSafeResourceViewContent(entry.name, content)) {
						catalog.problems.push_back(CatalogProblem{ entry.name, "references a remote origin or another folder" });
						continue;
					}
				}
				catalog.entries.push_back(CatalogEntry{ entry.name, source, *sha, size, writeTime, view });
			}
		} catch (...) {
			catalog.problems.push_back(CatalogProblem{ {}, "modpack_download_dir could not be read" });
		}
		return catalog;
	}

	const std::string& ExpectedFingerprint(const Catalog& a_catalog, bool a_fullManifest)
	{
		return a_fullManifest || a_catalog.viewlessFingerprint.empty() ? a_catalog.fingerprint : a_catalog.viewlessFingerprint;
	}

	const CatalogEntry* FindEntry(const Catalog& a_catalog, std::string_view a_name, bool a_fullManifest)
	{
		for (const auto& entry : a_catalog.entries) {
			if (EqualsIgnoringAsciiCase(entry.name, a_name) && (a_fullManifest || !entry.view)) {
				return &entry;
			}
		}
		return nullptr;
	}

	bool StillMatches(const CatalogEntry& a_entry)
	{
		std::error_code error;
		const auto status = std::filesystem::symlink_status(a_entry.path, error);
		if (error || !std::filesystem::is_regular_file(status)) {
			return false;
		}
		const auto size = std::filesystem::file_size(a_entry.path, error);
		const auto writeTime = std::filesystem::last_write_time(a_entry.path, error);
		return !error && size == a_entry.sizeBytes && writeTime == a_entry.writeTime;
	}

	ModpackDownload::Offer BuildOffer(const Catalog& a_catalog, const ModpackDownload::OfferRequest& a_request, bool a_fullManifest)
	{
		const auto& expected = ExpectedFingerprint(a_catalog, a_fullManifest);
		ModpackDownload::Offer offer{ expected, {} };
		if (a_request.fingerprint != expected) {
			return offer;
		}
		for (const auto& name : a_request.names) {
			const auto* entry = FindEntry(a_catalog, name, a_fullManifest);
			if (entry != nullptr && StillMatches(*entry)) {
				offer.files.push_back(ModpackDownload::OfferedFile{ name, entry->sha256Hex, entry->sizeBytes });
			}
		}
		return offer;
	}

	Limiter::Limiter(std::size_t a_maximumConcurrent, std::uint64_t a_bytesPerWindow) :
		maximumConcurrent(a_maximumConcurrent),
		bytesPerWindow(a_bytesPerWindow)
	{}

	Limiter::Usage& Limiter::Current(std::uint64_t a_source, double a_now)
	{
		auto& entry = usage[a_source];
		if (a_now - entry.windowStart >= kQuotaWindowSeconds) {
			entry.windowStart = a_now;
			entry.bytes = 0;
		}
		return entry;
	}

	bool Limiter::TryStart(std::uint64_t a_source, double a_now)
	{
		if (active >= maximumConcurrent) {
			return false;
		}
		if (!usage.contains(a_source) && usage.size() >= kMaximumQuotaAddresses) {
			std::erase_if(usage, [a_now](const auto& item) {
				return item.second.active == 0 && a_now - item.second.windowStart >= kQuotaWindowSeconds;
			});
			if (usage.size() >= kMaximumQuotaAddresses) return false;
		}
		auto& entry = Current(a_source, a_now);
		if (entry.bytes >= bytesPerWindow) {
			return false;
		}
		++entry.active;
		++active;
		return true;
	}

	bool Limiter::Charge(std::uint64_t a_source, std::uint64_t a_bytes, double a_now)
	{
		auto& entry = Current(a_source, a_now);
		entry.bytes = entry.bytes > UINT64_MAX - a_bytes ? UINT64_MAX : entry.bytes + a_bytes;
		return entry.bytes <= bytesPerWindow;
	}

	std::uint64_t Limiter::Remaining(std::uint64_t a_source, double a_now)
	{
		if (!usage.contains(a_source)) {
			return bytesPerWindow;
		}
		const auto& entry = Current(a_source, a_now);
		return entry.bytes >= bytesPerWindow ? 0 : bytesPerWindow - entry.bytes;
	}

	void Limiter::Finish(std::uint64_t a_source)
	{
		const auto found = usage.find(a_source);
		if (found == usage.end() || found->second.active == 0) {
			return;
		}
		--found->second.active;
		if (active > 0) {
			--active;
		}
	}

	std::size_t Limiter::Active() const noexcept
	{
		return active;
	}
}
