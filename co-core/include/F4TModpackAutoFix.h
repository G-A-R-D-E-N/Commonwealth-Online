#pragma once

#include "F4TModpackDownload.h"
#include "F4TModpackInstall.h"
#include "F4TModpackManifest.h"
#include "F4TModpackNeed.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ModpackAutoFix
{
	inline constexpr std::size_t kAttemptsPerFile = 3;
	inline constexpr std::uint64_t kFreeSpaceMarginBytes = 512ull * 1024 * 1024;

	class Transport
	{
	public:
		using ChunkHandler = std::function<bool(const ModpackDownload::FileChunk&)>;

		virtual ~Transport() = default;

		[[nodiscard]] virtual std::optional<ModpackDownload::Offer> FetchOffer(const ModpackDownload::OfferRequest& a_request) = 0;
		[[nodiscard]] virtual std::optional<ModpackDownload::FileEnd> FetchFile(const ModpackDownload::FileRequest& a_request, const ChunkHandler& a_onChunk) = 0;
	};

	struct Progress
	{
		std::size_t fileIndex{};
		std::size_t fileCount{};
		std::string name;
		std::uint64_t doneBytes{};
		std::uint64_t totalBytes{};
	};

	struct StageResult
	{
		bool ok{};
		bool cancelled{};
		std::string failure;
		std::uint32_t scripts{};
		std::uint32_t interfaceFiles{};
	};

	[[nodiscard]] std::optional<ModpackDownload::OfferRequest> MakeOfferRequest(
		std::string_view a_fingerprint,
		const ModpackManifest::NeedVerdict& a_verdict);

	[[nodiscard]] bool HasRoomFor(std::optional<std::uint64_t> a_freeBytes, std::uint64_t a_neededBytes) noexcept;

	[[nodiscard]] StageResult DownloadAndStage(
		const ModpackDownload::ConsentPlan& a_plan,
		const std::filesystem::path& a_stagingDirectory,
		Transport& a_transport,
		const std::function<void(const Progress&)>& a_onProgress,
		const std::atomic_bool* a_cancel = nullptr);

	void RemoveOtherStaging(const std::filesystem::path& a_stagingRoot, std::string_view a_keepFingerprint);

	[[nodiscard]] ModpackInstall::InstallPlan MakeInstallPlan(
		const ModpackDownload::ConsentPlan& a_plan,
		const ModpackManifest::Manifest& a_required,
		std::string a_serverKey,
		std::string a_serverLabel,
		ModpackInstall::ServerEndpoint a_endpoint);

	[[nodiscard]] std::string FormatBytes(std::uint64_t a_bytes);
	[[nodiscard]] std::string DescribeConsent(const ModpackDownload::ConsentPlan& a_plan, std::string_view a_serverLabel);
	[[nodiscard]] std::string DescribeReady(
		const ModpackDownload::ConsentPlan& a_plan,
		const StageResult& a_staged,
		ModpackInstall::ModManager a_manager,
		const std::filesystem::path& a_stagingDirectory);
	[[nodiscard]] std::string DescribeResult(const ModpackInstall::Result& a_result);
}
