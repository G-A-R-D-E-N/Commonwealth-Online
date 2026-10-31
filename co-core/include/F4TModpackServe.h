#pragma once

#include "F4TModpackDownload.h"
#include "F4TModpackManifest.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ModpackServe
{
	inline constexpr double kQuotaWindowSeconds = 60.0 * 60.0;

	struct CatalogEntry
	{
		std::string name;
		std::filesystem::path path;
		std::string sha256Hex;
		std::uint64_t sizeBytes{};
		std::filesystem::file_time_type writeTime{};
	};

	struct CatalogProblem
	{
		std::string name;
		std::string reason;
	};

	struct Catalog
	{
		std::string fingerprint;
		std::vector<CatalogEntry> entries;
		std::vector<CatalogProblem> problems;
	};

	[[nodiscard]] bool IsDownloadable(std::string_view a_name, const std::vector<std::string>& a_downloadable);

	[[nodiscard]] Catalog BuildCatalog(
		const ModpackManifest::Manifest& a_required,
		std::string_view a_fingerprint,
		const std::filesystem::path& a_directory,
		const std::vector<std::string>& a_downloadable);

	[[nodiscard]] const CatalogEntry* FindEntry(const Catalog& a_catalog, std::string_view a_name);
	[[nodiscard]] bool StillMatches(const CatalogEntry& a_entry);
	[[nodiscard]] ModpackDownload::Offer BuildOffer(const Catalog& a_catalog, const ModpackDownload::OfferRequest& a_request);

	class Limiter
	{
	public:
		Limiter(std::size_t a_maximumConcurrent, std::uint64_t a_bytesPerWindow);

		[[nodiscard]] bool TryStart(const std::string& a_address, double a_now);
		[[nodiscard]] bool Charge(const std::string& a_address, std::uint64_t a_bytes, double a_now);
		void Finish(const std::string& a_address);
		[[nodiscard]] std::size_t Active() const noexcept;

	private:
		struct Usage
		{
			double windowStart{};
			std::uint64_t bytes{};
			std::size_t active{};
		};

		Usage& Current(const std::string& a_address, double a_now);

		std::size_t maximumConcurrent;
		std::uint64_t bytesPerWindow;
		std::size_t active{};
		std::map<std::string, Usage> usage;
	};
}
