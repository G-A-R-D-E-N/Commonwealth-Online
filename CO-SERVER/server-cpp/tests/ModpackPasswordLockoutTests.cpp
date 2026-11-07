#include "ServerModpackFiles.h"
#include "IdentityGate.h"
#include "F4TModpackDownload.h"
#include "F4TPayloadCodec.h"
#include "F4TContentManifest.h"
#include "F4TModpackManifest.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace CommonwealthOnline::Server;
using Json = nlohmann::json;
static Json response;
static std::string delivered;
static bool backpressure = false;

extern "C" int co_gns_server_send(co_gns_server_handle, std::uint32_t, const void* data, std::uint32_t size, std::uint32_t)
{
	const auto* bytes = static_cast<const std::uint8_t*>(data);
	const auto decoded = F4T::PayloadCodec::Decode({ bytes, size }, 1024 * 1024);
	assert(decoded);
	response = Json::parse(decoded->begin(), decoded->end());
	if (const auto chunk = F4T::ModpackDownload::ParseChunkPacket(response)) {
		if (backpressure) return CO_GNS_SEND_BACKPRESSURE;
		delivered += chunk->bytes;
	}
	return CO_GNS_SEND_SENT;
}

class Tokens final : public F4T::Identity::TokenService
{
public:
	std::string CreateToken() override { return "test"; }
	F4T::Identity::TokenHash HashToken(std::string_view) override { return {}; }
};

int main()
{
	Tokens tokens;
	double clock = 1000;
	SteamIdentityHooks hooks;
	hooks.passwordLockout = { .threshold = 3, .cooldownSeconds = 60, .maximumTracked = 4 };
	hooks.monotonicNow = [&] { return clock; };
	IdentityGate gate({}, tokens, nullptr, {}, nullptr, {}, hooks);
	Configuration::Options options;
	options.allowModpackDownloads = true;
	options.requiredModpackFingerprint = std::string(64, 'a');
	options.serverPassword = "correct";
	F4T::Admission::AdmissionRegistry admissions(8);
	std::uint64_t ticket = 0;
	constexpr std::uint64_t source = 0x0a000001;
	auto attempt = [&](std::string_view password, bool file) {
		Json packet{ { "type", "hello" }, { "serverPassword", password },
			{ std::string(file ? F4T::ModpackDownload::kFileRequestField : F4T::ModpackDownload::kOfferRequestField), Json::object() } };
		const auto text = packet.dump();
		const std::vector<std::uint8_t> bytes(text.begin(), text.end());
		response = {};
		const auto result = ServeModpackFilesIfRequested(nullptr, 1, bytes, options, clock, "10.0.0.1",
			[](std::uint32_t, const char*) {}, admissions, ticket, &gate, source);
		assert(result == ModpackFileRequest::refused);
		return response.at("code").get<std::string>();
	};
	assert(attempt("wrong", false) == "password_invalid");
	assert(attempt("wrong", true) == "password_invalid");
	assert(attempt("wrong", false) == "password_invalid");
	assert(attempt("correct", true) == "password_locked");
	assert(gate.CheckManifestPassword(1, source)->code == "password_locked");
	clock += 60;
	assert(!gate.CheckManifestPassword(1, source));

	options.allowModpackDownloads = false;
	options.nexusSources = { { "test.esp", 123, 456 } };
	const Json sourceRequest{ { "type", "hello" }, { "serverPassword", "correct" },
		{ "capabilities", Json::array({ F4T::ModpackDownload::kCapabilityName }) },
		{ std::string(F4T::ModpackDownload::kOfferRequestField),
		  F4T::ModpackDownload::OfferRequestObject({ *options.requiredModpackFingerprint, { "test.esp" }, { "test.esp" } }) } };
	const auto requestText = sourceRequest.dump();
	const std::vector<std::uint8_t> requestBytes(requestText.begin(), requestText.end());
	const auto sourceResult = ServeModpackFilesIfRequested(nullptr, 4, requestBytes, options, clock, "10.0.0.1",
		[](std::uint32_t, const char*) {}, admissions, ticket, &gate, source);
	assert(sourceResult == ModpackFileRequest::served);
	assert(response.at("nexusSources").at("files").at(0).at("modId") == 123);
	options.nexusSources = { { "f4se/plugins/example.dll", 789, 456 } };
	const Json manualRequest{ { "type", "hello" }, { "serverPassword", "correct" },
		{ "capabilities", Json::array({ F4T::ModpackDownload::kCapabilityName }) },
		{ std::string(F4T::ModpackDownload::kOfferRequestField),
		  F4T::ModpackDownload::OfferRequestObject({ *options.requiredModpackFingerprint, {}, { "f4se/plugins/example.dll" } }) } };
	const auto manualText = manualRequest.dump();
	const std::vector<std::uint8_t> manualBytes(manualText.begin(), manualText.end());
	assert(ServeModpackFilesIfRequested(nullptr, 5, manualBytes, options, clock, "10.0.0.1",
		[](std::uint32_t, const char*) {}, admissions, ticket, &gate, source) == ModpackFileRequest::served);
	assert(response.at("nexusSources").at("files").at(0).at("modId") == 789);
	options.nexusSources.clear();
	options.nexusCollection = F4T::NexusSources::Collection{ "abc123", 3 };
	const Json collectionRequest{ { "type", "hello" }, { "serverPassword", "correct" },
		{ "capabilities", Json::array({ F4T::ModpackDownload::kCapabilityName }) },
		{ std::string(F4T::ModpackDownload::kOfferRequestField),
		  F4T::ModpackDownload::OfferRequestObject({ *options.requiredModpackFingerprint, {}, {}, true }) } };
	const auto collectionText = collectionRequest.dump();
	const std::vector<std::uint8_t> collectionBytes(collectionText.begin(), collectionText.end());
	assert(ServeModpackFilesIfRequested(nullptr, 6, collectionBytes, options, clock, "10.0.0.1",
		[](std::uint32_t, const char*) {}, admissions, ticket, &gate, source) == ModpackFileRequest::served);
	assert(response.at("nexusCollection").at("slug") == "abc123");
	options.nexusCollection.reset();
	options.allowModpackDownloads = true;
	options.nexusSources.clear();

	const auto folder = std::filesystem::temp_directory_path() / ("co-modpack-quota-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(folder);
	const std::string content = "0123456789";
	{ std::ofstream file(folder / "Test.esp", std::ios::binary); file << content; }
	const auto hash = F4T::ContentManifest::Sha256Hex(content);
	const F4T::ModpackManifest::Manifest manifest{ { { F4T::ModpackManifest::EntryKind::plugin,
		F4T::ModpackManifest::PluginType::esp, "Test.esp", 0, hash, std::nullopt } } };
	options.requiredModpackManifestJson = *F4T::ModpackManifest::SerializeManifest(manifest);
	options.modpackDownloadDirectory = folder.string();
	options.modpackDownloadable = { "*" };
	options.modpackDownloadBytesPerHourPerAddress = 5;
	StartModpackFileCatalog(options);
	Json request{ { "type", "hello" }, { "serverPassword", "correct" }, { "capabilities", Json::array({ F4T::ModpackDownload::kCapabilityName }) },
		{ std::string(F4T::ModpackDownload::kFileRequestField),
		  F4T::ModpackDownload::FileRequestObject({ *options.requiredModpackFingerprint, "Test.esp", hash, 0 }) } };
	const auto text = request.dump();
	const std::vector<std::uint8_t> bytes(text.begin(), text.end());
	ModpackFileRequest result = ModpackFileRequest::refused;
	for (int i = 0; i < 100 && result != ModpackFileRequest::started; ++i) {
		result = ServeModpackFilesIfRequested(nullptr, 2, bytes, options, clock, "10.0.0.2",
			[](std::uint32_t, const char*) {}, admissions, ticket, nullptr, 0);
		if (result != ModpackFileRequest::started) std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	assert(result == ModpackFileRequest::started);
	backpressure = true;
	AdvanceModpackFileDeliveries(nullptr, clock, [](std::uint32_t, const char*) {});
	assert(delivered.empty());
	backpressure = false;
	AdvanceModpackFileDeliveries(nullptr, clock, [](std::uint32_t, const char*) {});
	assert(delivered == "01234");
	assert(response.at("code").get<std::string>() == F4T::ModpackDownload::kUnavailableCode);
	std::filesystem::remove_all(folder);
	return 0;
}
