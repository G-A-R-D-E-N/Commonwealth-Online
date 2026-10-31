#pragma once

#include "F4TContentManifest.h"
#include "F4TModpackNeed.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ModpackDownload
{
	inline constexpr std::string_view kCapabilityName = "modpack-file-download-v1";
	inline constexpr std::string_view kOfferRequestField = "requestModpackOffer";
	inline constexpr std::string_view kFileRequestField = "requestModpackFile";
	inline constexpr std::string_view kOfferPacketType = "modpackOffer";
	inline constexpr std::string_view kChunkPacketType = "modpackFileChunk";
	inline constexpr std::string_view kEndPacketType = "modpackFileEnd";
	inline constexpr std::string_view kUnavailableCode = "modpack_file_unavailable";
	inline constexpr std::size_t kChunkPayloadBytes = 32 * 1024;
	inline constexpr std::string_view kPartSuffix = ".part";

	struct OfferRequest
	{
		std::string fingerprint;
		std::vector<std::string> names;
	};

	struct OfferedFile
	{
		std::string name;
		std::string sha256Hex;
		std::uint64_t sizeBytes{};
	};

	struct Offer
	{
		std::string fingerprint;
		std::vector<OfferedFile> files;
	};

	struct FileRequest
	{
		std::string fingerprint;
		std::string name;
		std::string sha256Hex;
		std::uint64_t offset{};
	};

	struct FileChunk
	{
		std::string name;
		std::uint64_t offset{};
		std::string bytes;
	};

	struct FileEnd
	{
		std::string name;
		std::string sha256Hex;
		std::uint64_t totalBytes{};
	};

	[[nodiscard]] nlohmann::json OfferRequestObject(const OfferRequest& a_request);
	[[nodiscard]] std::optional<OfferRequest> ParseOfferRequest(const nlohmann::json& a_hello);
	[[nodiscard]] nlohmann::json OfferPacket(const Offer& a_offer);
	[[nodiscard]] std::optional<Offer> ParseOfferPacket(const nlohmann::json& a_packet);

	[[nodiscard]] nlohmann::json FileRequestObject(const FileRequest& a_request);
	[[nodiscard]] std::optional<FileRequest> ParseFileRequest(const nlohmann::json& a_hello);
	[[nodiscard]] nlohmann::json ChunkPacket(std::string_view a_name, std::uint64_t a_offset, std::string_view a_bytes);
	[[nodiscard]] std::optional<FileChunk> ParseChunkPacket(const nlohmann::json& a_packet);
	[[nodiscard]] nlohmann::json EndPacket(const FileEnd& a_end);
	[[nodiscard]] std::optional<FileEnd> ParseEndPacket(const nlohmann::json& a_packet);

	enum class ConsentIssue
	{
		none,
		nothingOffered,
		tooManyFiles,
		totalTooLarge
	};

	struct ConsentFile
	{
		std::string name;
		ModpackManifest::AutoInstallFileKind kind{ ModpackManifest::AutoInstallFileKind::plugin };
		std::string sha256Hex;
		std::uint64_t sizeBytes{};
		bool replaces{};
		std::string replacedSha256Hex;
		std::optional<std::uint64_t> replacedSizeBytes;
	};

	struct ConsentPlan
	{
		std::string fingerprint;
		std::vector<ConsentFile> files;
		std::vector<ModpackManifest::ManualFile> manual;
		std::vector<std::string> notOffered;
		std::uint64_t totalBytes{};
		std::size_t replacements{};
		ConsentIssue issue{ ConsentIssue::none };
	};

	[[nodiscard]] std::optional<ConsentPlan> BuildConsentPlan(
		std::string_view a_fingerprint,
		const ModpackManifest::NeedVerdict& a_verdict,
		const Offer& a_offer);
	[[nodiscard]] bool ConsentPlanCanInstall(const ConsentPlan& a_plan) noexcept;

	enum class WriteResult
	{
		accepted,
		complete,
		wrongOffset,
		tooLarge,
		hashMismatch,
		ioError
	};

	class StagedFileWriter
	{
	public:
		StagedFileWriter(std::filesystem::path a_stagingDirectory, ConsentFile a_file);

		[[nodiscard]] bool Open();
		[[nodiscard]] std::uint64_t Offset() const noexcept;
		[[nodiscard]] WriteResult Append(std::uint64_t a_offset, std::string_view a_bytes);
		[[nodiscard]] WriteResult Finish(const FileEnd& a_end);
		[[nodiscard]] const std::filesystem::path& StagedPath() const noexcept;

	private:
		void Discard();

		std::filesystem::path directory;
		ConsentFile file;
		std::filesystem::path partPath;
		std::filesystem::path stagedPath;
		std::ofstream stream;
		ContentManifest::Sha256Stream hasher;
		std::uint64_t written{};
		bool open{};
	};

	[[nodiscard]] bool StagedFileMatches(const std::filesystem::path& a_path, std::string_view a_sha256Hex, std::uint64_t a_sizeBytes);
}
