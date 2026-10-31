#include "F4TModpackServe.h"

#include "F4TContentManifest.h"
#include "F4TModpackContentPolicy.h"

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
				if ((entry.kind != ModpackManifest::EntryKind::plugin && entry.kind != ModpackManifest::EntryKind::archive) ||
					entry.sha256Hex.empty() || CheckAutoInstallName(entry.name) != AutoInstallRefusal::none ||
					!IsDownloadable(entry.name, a_downloadable)) {
					continue;
				}
				const auto found = present.find(Lower(entry.name));
				if (found == present.end()) {
					catalog.problems.push_back(CatalogProblem{ entry.name, "not in modpack_download_dir" });
					continue;
				}
				const auto status = std::filesystem::symlink_status(found->second, error);
				if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
					catalog.problems.push_back(CatalogProblem{ entry.name, "is not a plain file" });
					continue;
				}
				const auto size = std::filesystem::file_size(found->second, error);
				const auto writeTime = std::filesystem::last_write_time(found->second, error);
				if (error || size == 0 || size > ModpackManifest::kMaximumAutoInstallFileBytes || (entry.sizeBytes && *entry.sizeBytes != size)) {
					catalog.problems.push_back(CatalogProblem{ entry.name, "has a different size from the manifest or is too large" });
					continue;
				}
				const auto sha = ContentManifest::Sha256HexFile(found->second);
				if (!sha || *sha != entry.sha256Hex) {
					catalog.problems.push_back(CatalogProblem{ entry.name, "does not match the manifest's SHA-256" });
					continue;
				}
				catalog.entries.push_back(CatalogEntry{ entry.name, found->second, *sha, size, writeTime });
			}
		} catch (...) {
			catalog.problems.push_back(CatalogProblem{ {}, "modpack_download_dir could not be read" });
		}
		return catalog;
	}

	const CatalogEntry* FindEntry(const Catalog& a_catalog, std::string_view a_name)
	{
		for (const auto& entry : a_catalog.entries) {
			if (EqualsIgnoringAsciiCase(entry.name, a_name)) {
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

	ModpackDownload::Offer BuildOffer(const Catalog& a_catalog, const ModpackDownload::OfferRequest& a_request)
	{
		ModpackDownload::Offer offer{ a_catalog.fingerprint, {} };
		if (a_request.fingerprint != a_catalog.fingerprint) {
			return offer;
		}
		for (const auto& name : a_request.names) {
			const auto* entry = FindEntry(a_catalog, name);
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

	Limiter::Usage& Limiter::Current(const std::string& a_address, double a_now)
	{
		auto& entry = usage[a_address];
		if (a_now - entry.windowStart >= kQuotaWindowSeconds || a_now < entry.windowStart) {
			entry.windowStart = a_now;
			entry.bytes = 0;
		}
		return entry;
	}

	bool Limiter::TryStart(const std::string& a_address, double a_now)
	{
		if (active >= maximumConcurrent) {
			return false;
		}
		auto& entry = Current(a_address, a_now);
		if (entry.bytes >= bytesPerWindow) {
			return false;
		}
		++entry.active;
		++active;
		return true;
	}

	bool Limiter::Charge(const std::string& a_address, std::uint64_t a_bytes, double a_now)
	{
		auto& entry = Current(a_address, a_now);
		entry.bytes = entry.bytes > UINT64_MAX - a_bytes ? UINT64_MAX : entry.bytes + a_bytes;
		return entry.bytes <= bytesPerWindow;
	}

	void Limiter::Finish(const std::string& a_address)
	{
		const auto found = usage.find(a_address);
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
