#include "F4TModpackDownload.h"

#include "F4TModpackChunks.h"

#include <algorithm>
#include <array>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace F4T::ModpackDownload
{
	namespace
	{
		using ModpackManifest::AutoInstallRefusal;
		using ModpackManifest::CheckAutoInstallName;

		constexpr std::size_t kMaximumBase64Bytes = ((kChunkPayloadBytes + 2) / 3) * 4;

		[[nodiscard]] const std::string* FindString(const nlohmann::json& a_object, std::string_view a_key)
		{
			if (!a_object.is_object()) {
				return nullptr;
			}
			const auto found = a_object.find(std::string{ a_key });
			if (found == a_object.end() || !found->is_string()) {
				return nullptr;
			}
			return &found->get_ref<const std::string&>();
		}

		[[nodiscard]] std::optional<std::uint64_t> FindUnsigned(const nlohmann::json& a_object, std::string_view a_key)
		{
			if (!a_object.is_object()) {
				return std::nullopt;
			}
			const auto found = a_object.find(std::string{ a_key });
			if (found == a_object.end() || !found->is_number_unsigned()) {
				return std::nullopt;
			}
			return found->get<std::uint64_t>();
		}

		[[nodiscard]] bool IsType(const nlohmann::json& a_packet, std::string_view a_type)
		{
			const auto* type = FindString(a_packet, "type");
			return type != nullptr && *type == a_type;
		}

		[[nodiscard]] bool IsHelloWithCapability(const nlohmann::json& a_hello)
		{
			if (!IsType(a_hello, "hello")) {
				return false;
			}
			const auto capabilities = a_hello.find("capabilities");
			if (capabilities == a_hello.end() || !capabilities->is_array()) {
				return false;
			}
			return std::any_of(capabilities->begin(), capabilities->end(), [](const nlohmann::json& a_item) {
				return a_item.is_string() && a_item.get_ref<const std::string&>() == kCapabilityName;
			});
		}

		[[nodiscard]] bool IsAllowedName(std::string_view a_name)
		{
			return CheckAutoInstallName(a_name) == AutoInstallRefusal::none;
		}

		[[nodiscard]] std::string LowerAscii(std::string_view a_text)
		{
			std::string lowered{ a_text };
			for (auto& character : lowered) {
				if (character >= 'A' && character <= 'Z') {
					character = static_cast<char>(character - 'A' + 'a');
				}
			}
			return lowered;
		}

		[[nodiscard]] const OfferedFile* FindOffered(const Offer& a_offer, std::string_view a_name)
		{
			for (const auto& offered : a_offer.files) {
				if (offered.name == a_name) {
					return &offered;
				}
			}
			return nullptr;
		}
	}

	nlohmann::json OfferRequestObject(const OfferRequest& a_request)
	{
		return nlohmann::json{
			{ "fingerprint", a_request.fingerprint },
			{ "names", a_request.names }
		};
	}

	std::optional<OfferRequest> ParseOfferRequest(const nlohmann::json& a_hello)
	{
		try {
			if (!IsHelloWithCapability(a_hello)) {
				return std::nullopt;
			}
			const auto request = a_hello.find(std::string{ kOfferRequestField });
			if (request == a_hello.end() || !request->is_object()) {
				return std::nullopt;
			}
			const auto* fingerprint = FindString(*request, "fingerprint");
			const auto names = request->find("names");
			if (fingerprint == nullptr || !ContentManifest::IsValidFingerprint(*fingerprint) ||
				names == request->end() || !names->is_array() || names->empty() ||
				names->size() > ModpackManifest::kMaximumAutoInstallFiles) {
				return std::nullopt;
			}
			OfferRequest parsed{ *fingerprint, {} };
			std::unordered_set<std::string> seen;
			for (const auto& name : *names) {
				if (!name.is_string() || !IsAllowedName(name.get_ref<const std::string&>()) ||
					!seen.insert(LowerAscii(name.get_ref<const std::string&>())).second) {
					return std::nullopt;
				}
				parsed.names.push_back(name.get<std::string>());
			}
			return parsed;
		} catch (...) {
			return std::nullopt;
		}
	}

	nlohmann::json OfferPacket(const Offer& a_offer)
	{
		auto files = nlohmann::json::array();
		for (const auto& file : a_offer.files) {
			files.push_back(nlohmann::json{
				{ "name", file.name },
				{ "sha256", file.sha256Hex },
				{ "size", file.sizeBytes }
			});
		}
		return nlohmann::json{
			{ "type", std::string{ kOfferPacketType } },
			{ "fingerprint", a_offer.fingerprint },
			{ "files", std::move(files) }
		};
	}

	std::optional<Offer> ParseOfferPacket(const nlohmann::json& a_packet)
	{
		try {
			if (!IsType(a_packet, kOfferPacketType)) {
				return std::nullopt;
			}
			const auto* fingerprint = FindString(a_packet, "fingerprint");
			const auto files = a_packet.find("files");
			if (fingerprint == nullptr || !ContentManifest::IsValidFingerprint(*fingerprint) ||
				files == a_packet.end() || !files->is_array() || files->size() > ModpackManifest::kMaximumAutoInstallFiles) {
				return std::nullopt;
			}
			Offer offer{ *fingerprint, {} };
			std::unordered_set<std::string> seen;
			for (const auto& file : *files) {
				const auto* name = FindString(file, "name");
				const auto* sha = FindString(file, "sha256");
				const auto size = FindUnsigned(file, "size");
				if (name == nullptr || !IsAllowedName(*name) || !seen.insert(LowerAscii(*name)).second ||
					sha == nullptr || !ContentManifest::IsValidFingerprint(*sha) ||
					!size || *size == 0 || *size > ModpackManifest::kMaximumAutoInstallFileBytes) {
					return std::nullopt;
				}
				offer.files.push_back(OfferedFile{ *name, *sha, *size });
			}
			return offer;
		} catch (...) {
			return std::nullopt;
		}
	}

	nlohmann::json FileRequestObject(const FileRequest& a_request)
	{
		return nlohmann::json{
			{ "fingerprint", a_request.fingerprint },
			{ "name", a_request.name },
			{ "sha256", a_request.sha256Hex },
			{ "offset", a_request.offset }
		};
	}

	std::optional<FileRequest> ParseFileRequest(const nlohmann::json& a_hello)
	{
		try {
			if (!IsHelloWithCapability(a_hello)) {
				return std::nullopt;
			}
			const auto request = a_hello.find(std::string{ kFileRequestField });
			if (request == a_hello.end() || !request->is_object()) {
				return std::nullopt;
			}
			const auto* fingerprint = FindString(*request, "fingerprint");
			const auto* name = FindString(*request, "name");
			const auto* sha = FindString(*request, "sha256");
			const auto offset = FindUnsigned(*request, "offset");
			if (fingerprint == nullptr || !ContentManifest::IsValidFingerprint(*fingerprint) ||
				name == nullptr || !IsAllowedName(*name) ||
				sha == nullptr || !ContentManifest::IsValidFingerprint(*sha) ||
				!offset || *offset > ModpackManifest::kMaximumAutoInstallFileBytes) {
				return std::nullopt;
			}
			return FileRequest{ *fingerprint, *name, *sha, *offset };
		} catch (...) {
			return std::nullopt;
		}
	}

	nlohmann::json ChunkPacket(std::string_view a_name, std::uint64_t a_offset, std::string_view a_bytes)
	{
		return nlohmann::json{
			{ "type", std::string{ kChunkPacketType } },
			{ "name", std::string{ a_name } },
			{ "offset", a_offset },
			{ "data", ModpackChunks::EncodeBase64(a_bytes) }
		};
	}

	std::optional<FileChunk> ParseChunkPacket(const nlohmann::json& a_packet)
	{
		try {
			if (!IsType(a_packet, kChunkPacketType)) {
				return std::nullopt;
			}
			const auto* name = FindString(a_packet, "name");
			const auto offset = FindUnsigned(a_packet, "offset");
			const auto* data = FindString(a_packet, "data");
			if (name == nullptr || !offset || data == nullptr || data->size() > kMaximumBase64Bytes) {
				return std::nullopt;
			}
			auto bytes = ModpackChunks::DecodeBase64(*data, kChunkPayloadBytes);
			if (!bytes || bytes->empty()) {
				return std::nullopt;
			}
			return FileChunk{ *name, *offset, std::move(*bytes) };
		} catch (...) {
			return std::nullopt;
		}
	}

	nlohmann::json EndPacket(const FileEnd& a_end)
	{
		return nlohmann::json{
			{ "type", std::string{ kEndPacketType } },
			{ "name", a_end.name },
			{ "sha256", a_end.sha256Hex },
			{ "totalBytes", a_end.totalBytes }
		};
	}

	std::optional<FileEnd> ParseEndPacket(const nlohmann::json& a_packet)
	{
		try {
			if (!IsType(a_packet, kEndPacketType)) {
				return std::nullopt;
			}
			const auto* name = FindString(a_packet, "name");
			const auto* sha = FindString(a_packet, "sha256");
			const auto total = FindUnsigned(a_packet, "totalBytes");
			if (name == nullptr || sha == nullptr || !ContentManifest::IsValidFingerprint(*sha) || !total) {
				return std::nullopt;
			}
			return FileEnd{ *name, *sha, *total };
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<ConsentPlan> BuildConsentPlan(
		std::string_view a_fingerprint,
		const ModpackManifest::NeedVerdict& a_verdict,
		const Offer& a_offer)
	{
		if (!ContentManifest::IsValidFingerprint(a_fingerprint) || a_offer.fingerprint != a_fingerprint) {
			return std::nullopt;
		}
		ConsentPlan plan;
		plan.fingerprint = std::string{ a_fingerprint };
		plan.manual = a_verdict.manual;
		for (const auto& need : a_verdict.download) {
			const auto* offered = FindOffered(a_offer, need.name);
			const bool consistent = offered != nullptr && offered->sha256Hex == need.sha256Hex &&
				(!need.sizeBytes || *need.sizeBytes == offered->sizeBytes) &&
				offered->sizeBytes != 0 && offered->sizeBytes <= ModpackManifest::kMaximumAutoInstallFileBytes;
			if (!consistent) {
				plan.notOffered.push_back(need.name);
				continue;
			}
			const bool replaces = need.reason == ModpackManifest::NeedReason::differentContent;
			plan.files.push_back(ConsentFile{
				need.name,
				need.kind,
				need.sha256Hex,
				offered->sizeBytes,
				replaces,
				need.replacedSha256Hex,
				need.replacedSizeBytes });
			if (replaces) {
				++plan.replacements;
			}
			plan.totalBytes = plan.totalBytes > UINT64_MAX - offered->sizeBytes ? UINT64_MAX : plan.totalBytes + offered->sizeBytes;
		}
		if (plan.files.empty()) {
			plan.issue = ConsentIssue::nothingOffered;
		} else if (plan.files.size() > ModpackManifest::kMaximumAutoInstallFiles) {
			plan.issue = ConsentIssue::tooManyFiles;
		} else if (plan.totalBytes > ModpackManifest::kMaximumAutoInstallTotalBytes) {
			plan.issue = ConsentIssue::totalTooLarge;
		}
		return plan;
	}

	bool ConsentPlanCanInstall(const ConsentPlan& a_plan) noexcept
	{
		return a_plan.issue == ConsentIssue::none && !a_plan.files.empty();
	}

	StagedFileWriter::StagedFileWriter(std::filesystem::path a_stagingDirectory, ConsentFile a_file) :
		directory(std::move(a_stagingDirectory)),
		file(std::move(a_file))
	{
		partPath = directory / std::filesystem::path{ file.name + std::string{ kPartSuffix } };
		stagedPath = directory / std::filesystem::path{ file.name };
	}

	bool StagedFileWriter::Open()
	{
		try {
			if (!IsAllowedName(file.name) || !ContentManifest::IsValidFingerprint(file.sha256Hex) ||
				file.sizeBytes == 0 || file.sizeBytes > ModpackManifest::kMaximumAutoInstallFileBytes) {
				return false;
			}
			std::error_code error;
			std::filesystem::create_directories(directory, error);
			if (error || std::filesystem::is_symlink(std::filesystem::symlink_status(directory))) {
				return false;
			}
			if (StagedFileMatches(stagedPath, file.sha256Hex, file.sizeBytes)) {
				written = file.sizeBytes;
				open = true;
				return true;
			}
			std::filesystem::remove(stagedPath, error);
			const auto partStatus = std::filesystem::symlink_status(partPath, error);
			if (std::filesystem::is_symlink(partStatus) || (std::filesystem::exists(partStatus) && !std::filesystem::is_regular_file(partStatus))) {
				return false;
			}
			if (std::filesystem::exists(partStatus)) {
				const auto existing = std::filesystem::file_size(partPath, error);
				if (error || existing >= file.sizeBytes) {
					std::filesystem::remove(partPath, error);
				} else {
					std::ifstream previous(partPath, std::ios::binary);
					std::array<char, 1 << 16> buffer{};
					std::uint64_t hashed = 0;
					while (previous && hashed < existing) {
						previous.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
						const auto count = static_cast<std::uint64_t>(previous.gcount());
						hasher.Update(std::string_view{ buffer.data(), static_cast<std::size_t>(count) });
						hashed += count;
					}
					if (hashed != existing) {
						return false;
					}
					written = existing;
				}
			}
			stream.open(partPath, std::ios::binary | std::ios::app);
			open = static_cast<bool>(stream);
			return open;
		} catch (...) {
			return false;
		}
	}

	std::uint64_t StagedFileWriter::Offset() const noexcept
	{
		return written;
	}

	WriteResult StagedFileWriter::Append(std::uint64_t a_offset, std::string_view a_bytes)
	{
		if (!open || !stream.is_open()) {
			return WriteResult::ioError;
		}
		if (a_offset != written) {
			return WriteResult::wrongOffset;
		}
		if (a_bytes.size() > file.sizeBytes - written) {
			Discard();
			return WriteResult::tooLarge;
		}
		stream.write(a_bytes.data(), static_cast<std::streamsize>(a_bytes.size()));
		if (!stream) {
			return WriteResult::ioError;
		}
		hasher.Update(a_bytes);
		written += a_bytes.size();
		return WriteResult::accepted;
	}

	WriteResult StagedFileWriter::Finish(const FileEnd& a_end)
	{
		if (!open) {
			return WriteResult::ioError;
		}
		if (a_end.name != file.name || a_end.sha256Hex != file.sha256Hex || a_end.totalBytes != file.sizeBytes || written != file.sizeBytes) {
			Discard();
			return WriteResult::hashMismatch;
		}
		if (!stream.is_open()) {
			return StagedFileMatches(stagedPath, file.sha256Hex, file.sizeBytes) ? WriteResult::complete : WriteResult::hashMismatch;
		}
		stream.flush();
		stream.close();
		if (hasher.FinishHex() != file.sha256Hex) {
			Discard();
			return WriteResult::hashMismatch;
		}
		std::error_code error;
		std::filesystem::rename(partPath, stagedPath, error);
		open = false;
		if (error) {
			return WriteResult::ioError;
		}
		return WriteResult::complete;
	}

	const std::filesystem::path& StagedFileWriter::StagedPath() const noexcept
	{
		return stagedPath;
	}

	void StagedFileWriter::Discard()
	{
		if (stream.is_open()) {
			stream.close();
		}
		std::error_code error;
		std::filesystem::remove(partPath, error);
		written = 0;
		open = false;
	}

	bool StagedFileMatches(const std::filesystem::path& a_path, std::string_view a_sha256Hex, std::uint64_t a_sizeBytes)
	{
		try {
			std::error_code error;
			const auto status = std::filesystem::symlink_status(a_path, error);
			if (error || !std::filesystem::is_regular_file(status)) {
				return false;
			}
			const auto size = std::filesystem::file_size(a_path, error);
			if (error || size != a_sizeBytes) {
				return false;
			}
			const auto sha = ContentManifest::Sha256HexFile(a_path);
			return sha && *sha == a_sha256Hex;
		} catch (...) {
			return false;
		}
	}
}
