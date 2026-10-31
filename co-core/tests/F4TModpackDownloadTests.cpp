#include "F4TModpackDownload.h"

#include "F4TModpackChunks.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace
{
	using namespace F4T::ModpackDownload;
	using F4T::ModpackManifest::AutoInstallFileKind;
	using F4T::ModpackManifest::NeedFile;
	using F4T::ModpackManifest::NeedReason;
	using F4T::ModpackManifest::NeedVerdict;

	const std::string kFingerprint(64, 'f');

	nlohmann::json Hello(const char* a_field, nlohmann::json a_request)
	{
		return nlohmann::json{
			{ "type", "hello" },
			{ "capabilities", nlohmann::json::array({ std::string{ kCapabilityName } }) },
			{ a_field, std::move(a_request) }
		};
	}

	void TestOfferRequestRoundTripAndRefusals()
	{
		const OfferRequest request{ kFingerprint, { "mymod.esp", "mymod - main.ba2" } };
		const auto parsed = ParseOfferRequest(Hello("requestModpackOffer", OfferRequestObject(request)));
		assert(parsed && parsed->names == request.names && parsed->fingerprint == kFingerprint);
		assert(!ParseOfferRequest(Hello("requestModpackOffer", OfferRequestObject({ kFingerprint, { "..\\x.esp" } }))));
		assert(!ParseOfferRequest(Hello("requestModpackOffer", OfferRequestObject({ kFingerprint, { "a.esp", "A.ESP" } }))));
		assert(!ParseOfferRequest(Hello("requestModpackOffer", OfferRequestObject({ kFingerprint, {} }))));
		assert(!ParseOfferRequest(Hello("requestModpackOffer", OfferRequestObject({ "x", { "a.esp" } }))));
		auto noCapability = Hello("requestModpackOffer", OfferRequestObject(request));
		noCapability["capabilities"] = nlohmann::json::array();
		assert(!ParseOfferRequest(noCapability));
		std::vector<std::string> tooMany;
		for (std::size_t index = 0; index <= F4T::ModpackManifest::kMaximumAutoInstallFiles; ++index) {
			tooMany.push_back("m" + std::to_string(index) + ".esp");
		}
		assert(!ParseOfferRequest(Hello("requestModpackOffer", OfferRequestObject({ kFingerprint, tooMany }))));
	}

	void TestOfferPacketFitsOneMessageAndParses()
	{
		Offer offer{ kFingerprint, {} };
		for (std::size_t index = 0; index < F4T::ModpackManifest::kMaximumAutoInstallFiles; ++index) {
			offer.files.push_back(OfferedFile{ std::string(190, 'a') + std::to_string(index) + ".esp", std::string(64, 'b'), F4T::ModpackManifest::kMaximumAutoInstallFileBytes });
		}
		const auto packet = OfferPacket(offer);
		assert(packet.dump().size() < 64 * 1024);
		const auto parsed = ParseOfferPacket(packet);
		assert(parsed && parsed->files.size() == offer.files.size());
		auto bad = packet;
		bad["files"][0]["size"] = 0;
		assert(!ParseOfferPacket(bad));
		bad = packet;
		bad["files"][0]["name"] = "Fallout4.esm";
		assert(!ParseOfferPacket(bad));
	}

	void TestFileRequestAndChunks()
	{
		const FileRequest request{ kFingerprint, "mymod.esp", std::string(64, 'a'), 65536 };
		const auto parsed = ParseFileRequest(Hello("requestModpackFile", FileRequestObject(request)));
		assert(parsed && parsed->offset == 65536 && parsed->name == "mymod.esp");
		assert(!ParseFileRequest(Hello("requestModpackFile", FileRequestObject({ kFingerprint, "x.dll", std::string(64, 'a'), 0 }))));
		assert(!ParseFileRequest(Hello("requestModpackFile", FileRequestObject({ kFingerprint, "x.esp", std::string(64, 'a'), F4T::ModpackManifest::kMaximumAutoInstallFileBytes + 1 }))));

		const std::string full(kChunkPayloadBytes, '\x7f');
		const auto chunk = ChunkPacket("mymod.esp", 32768, full);
		assert(chunk.dump().size() < 64 * 1024);
		const auto parsedChunk = ParseChunkPacket(chunk);
		assert(parsedChunk && parsedChunk->offset == 32768 && parsedChunk->bytes == full);
		auto oversize = chunk;
		oversize["data"] = F4T::ModpackChunks::EncodeBase64(std::string(kChunkPayloadBytes + 3, 'x'));
		assert(!ParseChunkPacket(oversize));
		const auto end = ParseEndPacket(EndPacket({ "mymod.esp", std::string(64, 'a'), 10 }));
		assert(end && end->totalBytes == 10);
	}

	NeedVerdict Verdict()
	{
		NeedVerdict verdict;
		verdict.download.push_back(NeedFile{ "mymod.esp", AutoInstallFileKind::plugin, "mymod", NeedReason::differentContent, std::string(64, 'a'), std::nullopt, std::string(64, 'e'), std::nullopt });
		verdict.download.push_back(NeedFile{ "mymod - main.ba2", AutoInstallFileKind::mainArchive, "mymod", NeedReason::missing, std::string(64, 'b'), 1000, {}, std::nullopt });
		verdict.download.push_back(NeedFile{ "other.esp", AutoInstallFileKind::plugin, "other", NeedReason::missing, std::string(64, 'c'), std::nullopt, {}, std::nullopt });
		verdict.download.push_back(NeedFile{ "liar - main.ba2", AutoInstallFileKind::mainArchive, "liar", NeedReason::missing, std::string(64, 'd'), 50, {}, std::nullopt });
		return verdict;
	}

	void TestConsentPlanBindsOfferToTheManifest()
	{
		const Offer offer{ kFingerprint, {
			OfferedFile{ "mymod.esp", std::string(64, 'a'), 300 },
			OfferedFile{ "mymod - main.ba2", std::string(64, 'b'), 1000 },
			OfferedFile{ "liar - main.ba2", std::string(64, 'd'), 51 },
		} };
		const auto plan = BuildConsentPlan(kFingerprint, Verdict(), offer);
		assert(plan);
		assert(plan->files.size() == 2);
		assert(plan->files[0].replaces && plan->files[0].replacedSha256Hex == std::string(64, 'e') && plan->files[0].sizeBytes == 300);
		assert(plan->totalBytes == 1300 && plan->replacements == 1);
		assert((plan->notOffered == std::vector<std::string>{ "other.esp", "liar - main.ba2" }));
		assert(ConsentPlanCanInstall(*plan));

		Offer wrongHash = offer;
		wrongHash.files[0].sha256Hex = std::string(64, '0');
		assert(BuildConsentPlan(kFingerprint, Verdict(), wrongHash)->files.size() == 1);
		Offer otherPack = offer;
		otherPack.fingerprint = std::string(64, '1');
		assert(!BuildConsentPlan(kFingerprint, Verdict(), otherPack));
		const auto empty = BuildConsentPlan(kFingerprint, Verdict(), Offer{ kFingerprint, {} });
		assert(empty && empty->issue == ConsentIssue::nothingOffered && !ConsentPlanCanInstall(*empty));
	}

	void TestStagedWriterVerifiesResumesAndRefuses()
	{
		const auto directory = std::filesystem::temp_directory_path() / ("co-modpack-stage-test-" + std::to_string(std::random_device{}()));
		std::filesystem::remove_all(directory);
		const std::string bytes = std::string(40000, 'x') + std::string(30000, 'y');
		const ConsentFile file{ "mymod.esp", AutoInstallFileKind::plugin, F4T::ContentManifest::Sha256Hex(bytes), bytes.size(), false, {}, std::nullopt };
		const FileEnd end{ file.name, file.sha256Hex, file.sizeBytes };
		{
			StagedFileWriter writer(directory, file);
			assert(writer.Open() && writer.Offset() == 0);
			assert(writer.Append(0, std::string_view{ bytes }.substr(0, 32768)) == WriteResult::accepted);
			assert(writer.Append(1, "x") == WriteResult::wrongOffset);
		}
		{
			StagedFileWriter writer(directory, file);
			assert(writer.Open() && writer.Offset() == 32768);
			assert(writer.Append(32768, std::string_view{ bytes }.substr(32768)) == WriteResult::accepted);
			assert(writer.Finish(end) == WriteResult::complete);
			assert(StagedFileMatches(writer.StagedPath(), file.sha256Hex, file.sizeBytes));
			assert(!std::filesystem::exists(directory / "mymod.esp.part"));
		}
		{
			StagedFileWriter writer(directory, file);
			assert(writer.Open() && writer.Offset() == file.sizeBytes);
			assert(writer.Finish(end) == WriteResult::complete);
		}
		std::filesystem::remove(directory / "mymod.esp");
		{
			StagedFileWriter writer(directory, file);
			assert(writer.Open());
			assert(writer.Append(0, std::string(bytes.size(), 'z')) == WriteResult::accepted);
			assert(writer.Finish(end) == WriteResult::hashMismatch);
			assert(!std::filesystem::exists(directory / "mymod.esp.part") && !std::filesystem::exists(directory / "mymod.esp"));
		}
		{
			StagedFileWriter writer(directory, file);
			assert(writer.Open());
			assert(writer.Append(0, bytes + "!") == WriteResult::tooLarge);
		}
		{
			StagedFileWriter writer(directory, file);
			assert(writer.Open());
			assert(writer.Append(0, bytes) == WriteResult::accepted);
			assert(writer.Finish(FileEnd{ file.name, std::string(64, '0'), file.sizeBytes }) == WriteResult::hashMismatch);
		}
		{
			StagedFileWriter writer(directory, ConsentFile{ "..\\evil.esp", AutoInstallFileKind::plugin, file.sha256Hex, file.sizeBytes, false, {}, std::nullopt });
			assert(!writer.Open());
		}
		std::filesystem::remove_all(directory);
	}
}

int main()
{
	TestOfferRequestRoundTripAndRefusals();
	TestOfferPacketFitsOneMessageAndParses();
	TestFileRequestAndChunks();
	TestConsentPlanBindsOfferToTheManifest();
	TestStagedWriterVerifiesResumesAndRefuses();
	return 0;
}
