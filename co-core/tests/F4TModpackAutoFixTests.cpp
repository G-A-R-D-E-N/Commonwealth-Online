#include "F4TModpackAutoFix.h"

#include "F4TModpackServe.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace
{
	using namespace F4T::ModpackAutoFix;
	using F4T::ModpackManifest::Entry;
	using F4T::ModpackManifest::EntryKind;
	using F4T::ModpackManifest::Manifest;
	using F4T::ModpackManifest::PluginType;

	void Write(const std::filesystem::path& a_path, const std::string& a_bytes)
	{
		std::filesystem::create_directories(a_path.parent_path());
		std::ofstream stream(a_path, std::ios::binary | std::ios::trunc);
		stream << a_bytes;
	}

	std::string Read(const std::filesystem::path& a_path)
	{
		std::ifstream stream(a_path, std::ios::binary);
		return std::string{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
	}

	std::string PutLittle(std::uint64_t a_value, int a_bytes)
	{
		std::string out;
		for (int index = 0; index < a_bytes; ++index) {
			out += static_cast<char>((a_value >> (8 * index)) & 0xFF);
		}
		return out;
	}

	std::string Ba2(const std::vector<std::string>& a_names, std::size_t a_padding)
	{
		std::string bytes = "BTDX" + PutLittle(1, 4) + "GNRL" + PutLittle(a_names.size(), 4) + PutLittle(24 + a_padding, 8);
		bytes += std::string(a_padding, 'p');
		for (const auto& name : a_names) {
			bytes += PutLittle(name.size(), 2) + name;
		}
		return bytes;
	}

	class FolderTransport final : public Transport
	{
	public:
		F4T::ModpackServe::Catalog catalog;
		int dropAfterChunks{ -1 };
		bool corruptOnce{};
		int fileRequests{};

		std::optional<F4T::ModpackDownload::Offer> FetchOffer(const F4T::ModpackDownload::OfferRequest& a_request) override
		{
			return F4T::ModpackDownload::ParseOfferPacket(F4T::ModpackDownload::OfferPacket(F4T::ModpackServe::BuildOffer(catalog, a_request)));
		}

		std::optional<F4T::ModpackDownload::FileEnd> FetchFile(const F4T::ModpackDownload::FileRequest& a_request, const ChunkHandler& a_onChunk) override
		{
			++fileRequests;
			const auto* entry = F4T::ModpackServe::FindEntry(catalog, a_request.name);
			if (entry == nullptr || entry->sha256Hex != a_request.sha256Hex) {
				return std::nullopt;
			}
			auto bytes = Read(entry->path);
			if (corruptOnce) {
				corruptOnce = false;
				bytes.back() ^= 0x55;
			}
			int sent = 0;
			for (auto offset = a_request.offset; offset < bytes.size(); offset += F4T::ModpackDownload::kChunkPayloadBytes) {
				if (dropAfterChunks >= 0 && sent++ == dropAfterChunks) {
					dropAfterChunks = -1;
					return std::nullopt;
				}
				const auto length = std::min<std::uint64_t>(F4T::ModpackDownload::kChunkPayloadBytes, bytes.size() - offset);
				const auto packet = F4T::ModpackDownload::ChunkPacket(a_request.name, offset, std::string_view{ bytes }.substr(offset, length));
				const auto chunk = F4T::ModpackDownload::ParseChunkPacket(nlohmann::json::parse(packet.dump()));
				if (!chunk || !a_onChunk(*chunk)) {
					return std::nullopt;
				}
			}
			return F4T::ModpackDownload::FileEnd{ a_request.name, entry->sha256Hex, entry->sizeBytes };
		}
	};

	struct World
	{
		std::filesystem::path root{ std::filesystem::canonical(std::filesystem::temp_directory_path()) / ("co-modpack-autofix-test-" + std::to_string(std::random_device{}())) };
		std::filesystem::path serverFolder{ root / "server" };
		std::filesystem::path game{ root / "Fallout 4" };
		std::filesystem::path pluginsTxt{ root / "Local" / "plugins.txt" };
		std::string plugin = std::string(100000, 'P');
		std::string archive = Ba2({ "Meshes\\MyMod\\a.nif", "Scripts\\MyModQuest.pex" }, 70000);
		Manifest required;
		Manifest client;

		World()
		{
			std::filesystem::remove_all(root);
			Write(serverFolder / "MyMod.esp", plugin);
			Write(serverFolder / "MyMod - Main.ba2", archive);
			Write(game / "Data" / "Fallout4.esm", "official");
			Write(game / "Data" / "mymod.esp", "old");
			Write(game / "Data" / "Extra.esp", "extra");
			Write(pluginsTxt, "*mymod.esp\r\n*Extra.esp\r\n");
			required = Manifest{ {
				Entry{ EntryKind::plugin, PluginType::esm, "Fallout4.esm", 0, {}, 8 },
				Entry{ EntryKind::plugin, PluginType::esp, "MyMod.esp", 1, F4T::ContentManifest::Sha256Hex(plugin), std::nullopt },
				Entry{ EntryKind::archive, PluginType::none, "MyMod - Main.ba2", 0, F4T::ContentManifest::Sha256Hex(archive), archive.size() },
			} };
			client = Manifest{ {
				Entry{ EntryKind::plugin, PluginType::esm, "Fallout4.esm", 0, {}, 8 },
				Entry{ EntryKind::plugin, PluginType::esp, "MyMod.esp", 1, F4T::ContentManifest::Sha256Hex("old"), std::nullopt },
				Entry{ EntryKind::plugin, PluginType::esp, "Extra.esp", 2, F4T::ContentManifest::Sha256Hex("extra"), std::nullopt },
			} };
		}

		~World()
		{
			std::error_code error;
			std::filesystem::remove_all(root, error);
		}
	};

	const std::string kFingerprint(64, 'a');

	void TestEndToEndDownloadStageAndInstall()
	{
		World world;
		FolderTransport transport;
		transport.catalog = F4T::ModpackServe::BuildCatalog(world.required, kFingerprint, world.serverFolder, { "*" });
		transport.dropAfterChunks = 2;
		const auto verdict = F4T::ModpackManifest::BuildNeedVerdict(world.required, world.client);
		assert(verdict && verdict->download.size() == 2);
		const auto request = MakeOfferRequest(kFingerprint, *verdict);
		assert(request);
		const auto offer = transport.FetchOffer(*request);
		assert(offer && offer->files.size() == 2);
		const auto consent = F4T::ModpackDownload::BuildConsentPlan(kFingerprint, *verdict, *offer);
		assert(consent && F4T::ModpackDownload::ConsentPlanCanInstall(*consent));
		const auto consentText = DescribeConsent(*consent, "example.org:7777");
		assert(consentText.find("Replaces your copy of: mymod.esp") != std::string::npos);
		assert(consentText.find("can contain scripts") != std::string::npos);

		const auto paths = F4T::ModpackInstall::PathsForGame(world.game, world.pluginsTxt);
		const auto staging = paths.StagingFor(kFingerprint);
		Write(paths.StagingFor(std::string(64, 'b')) / "old.esp", "stale");
		RemoveOtherStaging(paths.stagingRoot, kFingerprint);
		assert(!std::filesystem::exists(paths.StagingFor(std::string(64, 'b'))));

		std::size_t progressCalls = 0;
		const auto staged = DownloadAndStage(*consent, staging, transport, [&](const Progress& a_progress) {
			++progressCalls;
			assert(a_progress.doneBytes <= a_progress.totalBytes);
		});
		assert(staged.ok && staged.scripts == 1);
		assert(transport.fileRequests == 3);
		assert(progressCalls > 4);
		assert(Read(staging / "mymod.esp") == world.plugin);
		assert(DescribeReady(*consent, staged, F4T::ModpackInstall::ModManager::none, staging).find("1 script") != std::string::npos);
		assert(DescribeReady(*consent, staged, F4T::ModpackInstall::ModManager::vortex, staging).find("Vortex manages your Data folder") != std::string::npos);

		const auto plan = MakeInstallPlan(*consent, world.required, "key", "example.org:7777", { "example.org", 7777, "" });
		assert((plan.loadOrder == std::vector<std::string>{ "Fallout4.esm", "MyMod.esp" }));
		auto fileSystem = F4T::ModpackInstall::MakeNativeFileSystem();
		F4T::ModpackInstall::Installer installer(*fileSystem, paths);
		assert(installer.WritePendingPlan(plan));
		assert(installer.Recover().outcome == F4T::ModpackInstall::Outcome::nothingToDo);
		const auto applied = installer.ApplyPending(42);
		assert(applied.outcome == F4T::ModpackInstall::Outcome::applied);
		assert(Read(world.game / "Data" / "mymod.esp") == world.plugin);
		assert(Read(world.game / "Data" / "mymod - main.ba2") == world.archive);
		assert(Read(world.pluginsTxt) == "*mymod.esp\r\nExtra.esp\r\n");
		assert(DescribeResult(applied).find("Switched off: Extra.esp") != std::string::npos);
		assert(installer.ConfirmVerified().outcome == F4T::ModpackInstall::Outcome::applied);

		const auto after = F4T::ModpackManifest::BuildNeedVerdict(world.required, Manifest{ {
			Entry{ EntryKind::plugin, PluginType::esm, "Fallout4.esm", 0, {}, 8 },
			Entry{ EntryKind::plugin, PluginType::esp, "MyMod.esp", 1, F4T::ContentManifest::Sha256Hex(Read(world.game / "Data" / "mymod.esp")), std::nullopt },
			Entry{ EntryKind::archive, PluginType::none, "MyMod - Main.ba2", 0, F4T::ContentManifest::Sha256Hex(Read(world.game / "Data" / "mymod - main.ba2")), world.archive.size() },
		} });
		assert(after && after->download.empty() && after->otherDifferences == 0);
	}

	void TestCorruptTransferIsRetriedAndRefusedArchivesAreDropped()
	{
		World world;
		FolderTransport transport;
		transport.catalog = F4T::ModpackServe::BuildCatalog(world.required, kFingerprint, world.serverFolder, { "*" });
		transport.corruptOnce = true;
		const auto verdict = F4T::ModpackManifest::BuildNeedVerdict(world.required, world.client);
		const auto consent = F4T::ModpackDownload::BuildConsentPlan(kFingerprint, *verdict, *transport.FetchOffer(*MakeOfferRequest(kFingerprint, *verdict)));
		const auto staging = world.root / "staging";
		const auto staged = DownloadAndStage(*consent, staging, transport, {});
		assert(staged.ok && transport.fileRequests == 3);

		World evil;
		evil.archive = Ba2({ "F4SE\\Plugins\\evil.dll" }, 10);
		Write(evil.serverFolder / "MyMod - Main.ba2", evil.archive);
		evil.required.entries[2] = Entry{ EntryKind::archive, PluginType::none, "MyMod - Main.ba2", 0, F4T::ContentManifest::Sha256Hex(evil.archive), evil.archive.size() };
		FolderTransport evilTransport;
		evilTransport.catalog = F4T::ModpackServe::BuildCatalog(evil.required, kFingerprint, evil.serverFolder, { "*" });
		const auto evilVerdict = F4T::ModpackManifest::BuildNeedVerdict(evil.required, evil.client);
		const auto evilConsent = F4T::ModpackDownload::BuildConsentPlan(kFingerprint, *evilVerdict, *evilTransport.FetchOffer(*MakeOfferRequest(kFingerprint, *evilVerdict)));
		const auto evilStaged = DownloadAndStage(*evilConsent, evil.root / "staging", evilTransport, {});
		assert(!evilStaged.ok && evilStaged.failure.find("F4SE\\Plugins\\evil.dll") != std::string::npos);
		assert(!std::filesystem::exists(evil.root / "staging" / "mymod - main.ba2"));
	}

	void TestCancelStopsTheDownload()
	{
		World world;
		FolderTransport transport;
		transport.catalog = F4T::ModpackServe::BuildCatalog(world.required, kFingerprint, world.serverFolder, { "*" });
		const auto verdict = F4T::ModpackManifest::BuildNeedVerdict(world.required, world.client);
		const auto consent = F4T::ModpackDownload::BuildConsentPlan(kFingerprint, *verdict, *transport.FetchOffer(*MakeOfferRequest(kFingerprint, *verdict)));
		std::atomic_bool cancel{ false };
		const auto staged = DownloadAndStage(*consent, world.root / "staging", transport, [&](const Progress& a_progress) {
			if (a_progress.doneBytes > 40000) {
				cancel.store(true);
			}
		}, &cancel);
		assert(!staged.ok && staged.cancelled);
	}

	void TestHelpers()
	{
		assert(FormatBytes(512) == "512 bytes");
		assert(FormatBytes(1536) == "1.5 KB");
		assert(FormatBytes(5ull * 1024 * 1024 * 1024) == "5.0 GB");
		assert(HasRoomFor(kFreeSpaceMarginBytes + 10, 10));
		assert(!HasRoomFor(kFreeSpaceMarginBytes + 9, 10));
		assert(!HasRoomFor(std::nullopt, 1));
		F4T::ModpackManifest::NeedVerdict empty;
		assert(!MakeOfferRequest(kFingerprint, empty));
		F4T::ModpackInstall::Result failed{ F4T::ModpackInstall::Outcome::refused, "Data is a link." };
		assert(DescribeResult(failed) == "The modpack install did not run. Data is a link.");
	}
}

int main()
{
	TestEndToEndDownloadStageAndInstall();
	TestCorruptTransferIsRetriedAndRefusedArchivesAreDropped();
	TestCancelStopsTheDownload();
	TestHelpers();
	return 0;
}
