#include "F4TGnsProtocolAdapter.h"
#include "F4TModpackManifest.h"
#include "F4TModpackMismatchReport.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using namespace F4T::ModpackManifest;

	constexpr std::string_view kHashA = "ed7002b439e9ac845f22357d822bac1444730fbdb6016d3ec9432297b9ec9f73";
	constexpr std::string_view kHashB = "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb";

	Json Plugin(std::string a_name, std::uint32_t a_order, std::string a_hash)
	{
		return Json{ { "kind", "plugin" }, { "name", std::move(a_name) }, { "pluginType", "esp" }, { "order", a_order }, { "sha256", std::move(a_hash) } };
	}

	Json Master(std::string a_name, std::uint32_t a_order, std::uint64_t a_size)
	{
		return Json{ { "kind", "plugin" }, { "name", std::move(a_name) }, { "pluginType", "esm" }, { "order", a_order }, { "size", a_size } };
	}

	Json Asset(std::string a_kind, std::string a_name, std::string a_hash)
	{
		return Json{ { "kind", std::move(a_kind) }, { "name", std::move(a_name) }, { "sha256", std::move(a_hash) } };
	}

	Manifest Parsed(Json a_entries)
	{
		const auto manifest = ParseManifest(Json{ { "version", 1 }, { "entries", std::move(a_entries) } }.dump());
		assert(manifest.has_value());
		return *manifest;
	}

	std::vector<std::string> Labels(const MismatchReport& a_report)
	{
		std::vector<std::string> labels;
		for (const auto& entry : a_report.entries) {
			labels.push_back(std::string{ FormatMismatchKind(entry.kind) } + ":" + entry.name);
		}
		return labels;
	}

	std::size_t CountLines(std::string_view a_text, std::string_view a_needle)
	{
		std::size_t count = 0;
		std::size_t offset = 0;
		while ((offset = a_text.find(a_needle, offset)) != std::string_view::npos) {
			++count;
			offset += a_needle.size();
		}
		return count;
	}

	Json RequiredFixture()
	{
		return Json::array({
			Master("Fallout4.esm", 0, 100),
			Plugin("MissingMod.esp", 1, std::string{ kHashA }),
			Plugin("Alpha.esp", 2, std::string{ kHashA }),
			Plugin("Beta.esp", 3, std::string{ kHashA }),
			Asset("native", "F4SE/Plugins/co_alpha.dll", std::string{ kHashA }),
			Asset("config", "F4SE/Plugins/co_alpha.json", std::string{ kHashA })
		});
	}

	Json ClientFixture()
	{
		return Json::array({
			Master("Fallout4.esm", 0, 200),
			Plugin("Beta.esp", 1, std::string{ kHashA }),
			Plugin("Alpha.esp", 2, std::string{ kHashA }),
			Plugin("ExtraMod.esp", 3, std::string{ kHashA }),
			Asset("native", "F4SE/Plugins/co_alpha.dll", std::string{ kHashB }),
			Asset("config", "F4SE/Plugins/co_alpha.json", std::string{ kHashB })
		});
	}

	void TestReportListsEveryDifferenceKind()
	{
		const auto report = BuildModpackMismatchReport(Parsed(RequiredFixture()), Parsed(ClientFixture()));
		assert(report.has_value());
		const std::vector<std::string> expected{
			"Missing:missingmod.esp",
			"Unexpected:extramod.esp",
			"Wrong load order:alpha.esp",
			"Wrong load order:beta.esp",
			"Different content:fallout4.esm",
			"Different native plugin:f4se/plugins/co_alpha.dll",
			"Different config:f4se/plugins/co_alpha.json"
		};
		assert(Labels(*report) == expected);
		assert(report->omitted == 0);
	}

	void TestReportBoundsEachSection()
	{
		Json required = Json::array();
		for (std::uint32_t index = 0; index < 40; ++index) {
			auto digits = std::to_string(index);
			digits.insert(0, 2 - digits.size(), '0');
			required.push_back(Asset("native", "n" + digits + ".dll", std::string{ kHashA }));
		}
		const auto client = Json::array({ Asset("native", "clientonly.dll", std::string{ kHashA }) });

		const auto report = BuildModpackMismatchReport(Parsed(required), Parsed(client));
		assert(report.has_value());
		assert(report->entries.size() == kMaximumReportNamesPerSection + 1);
		assert(report->omitted == 40 - kMaximumReportNamesPerSection);
		assert(report->entries.front().name == "n00.dll");
		assert(report->entries.back().name == "clientonly.dll");
		assert(report->entries.back().kind == MismatchKind::unexpected);
	}

	void TestFormatNamesKindAndCapsLines()
	{
		MismatchReport report;
		for (std::uint32_t index = 0; index < kMaximumReportLines + 5; ++index) {
			report.entries.push_back(MismatchReportEntry{ MismatchKind::wrongOrder, "mod" + std::to_string(index) + ".esp" });
		}

		const auto text = FormatModpackMismatchReport(report);
		assert(CountLines(text, "  Wrong load order: mod") == kMaximumReportLines);
		assert(text.find("  ...and 5 more") != std::string::npos);
		assert(text.size() <= kMaximumReportTextBytes);
	}

	void TestFormatUsesEveryKindLabel()
	{
		MismatchReport report;
		for (const auto kind : { MismatchKind::missing, MismatchKind::unexpected, MismatchKind::wrongContent, MismatchKind::wrongNative, MismatchKind::wrongConfig, MismatchKind::wrongArchive, MismatchKind::wrongOrder }) {
			report.entries.push_back(MismatchReportEntry{ kind, "entry.esp" });
		}

		const auto text = FormatModpackMismatchReport(report);
		for (const auto* label : { "Missing", "Unexpected", "Different content", "Different native plugin", "Different config", "Different archive", "Wrong load order" }) {
			assert(text.find(label) != std::string::npos);
		}
	}

	void TestArchiveMismatchIsReported()
	{
		Json required = RequiredFixture();
		required.push_back(Json{ { "kind", "archive" }, { "name", "Fallout4 - Textures1.ba2" }, { "sha256", std::string{ kHashA } }, { "size", 100 } });
		Json client = ClientFixture();
		client.push_back(Json{ { "kind", "archive" }, { "name", "Fallout4 - Textures1.ba2" }, { "sha256", std::string{ kHashB } }, { "size", 100 } });

		const auto report = BuildModpackMismatchReport(Parsed(required), Parsed(client));
		assert(report.has_value());
		bool found = false;
		for (const auto& entry : report->entries) {
			if (entry.kind == MismatchKind::wrongArchive && entry.name == "fallout4 - textures1.ba2") {
				found = true;
			}
		}
		assert(found);
		const auto text = FormatModpackMismatchReport(*report);
		assert(text.find("Different archive: fallout4 - textures1.ba2") != std::string::npos);
	}

	void TestFormatStaysWithinByteBudget()
	{
		MismatchReport report;
		for (std::uint32_t index = 0; index < kMaximumReportLines; ++index) {
			report.entries.push_back(MismatchReportEntry{ MismatchKind::missing, std::string(250, 'x') + std::to_string(index) + ".esp" });
		}

		const auto text = FormatModpackMismatchReport(report);
		assert(text.size() <= kMaximumReportTextBytes);
		assert(text.find("...and ") != std::string::npos);
	}

	void TestFormatOfAgreementReportExplainsShapeDifference()
	{
		const MismatchReport report;
		const auto text = FormatModpackMismatchReport(report);
		assert(!text.empty());
		assert(text.find("load order") != std::string::npos);
	}

	void TestFormatRequiredListBoundsAndMarksStatus()
	{
		Json entries = Json::array();
		for (std::uint32_t index = 0; index < kMaximumReportLines + 5; ++index) {
			entries.push_back(Plugin("mod" + std::to_string(index) + ".esp", index, std::string{ kHashA }));
		}

		const auto text = FormatRequiredModpackList(Parsed(entries));
		assert(CountLines(text, ": required by server") == kMaximumReportLines);
		assert(text.find("  ...and 5 more") != std::string::npos);
		assert(text.size() <= kMaximumReportTextBytes);

		const auto fallback = FormatRequiredModpackList(Manifest{});
		assert(!fallback.empty());
	}

	void TestFormatRequiredListNotesTruncatedProbe()
	{
		const auto required = Parsed(Json::array({ Plugin("mod.esp", 0, std::string{ kHashA }) }));
		const auto text = FormatRequiredModpackList(required, true);
		assert(text.starts_with(kProbeTruncatedNote));
		assert(text.find("\nClient files were not compared on this machine.\n") != std::string::npos);
		assert(text.find("  mod.esp: required by server") != std::string::npos);
		assert(text.size() <= kMaximumReportTextBytes);

		const auto plain = FormatRequiredModpackList(required);
		assert(!plain.starts_with(kProbeTruncatedNote));
	}

	void TestRequiredModpackNameLine()
	{
		assert(FormatRequiredModpackName("Main List") == "This server requires the Main List modlist.");
		assert(FormatRequiredModpackName("").empty());
		assert(FormatRequiredModpackName("<b>Main</b>").empty());
		assert(FormatRequiredModpackName(std::string(81, 'M')).empty());

		F4T::GnsProtocolAdapter::Session session;
		const auto named = session.AdaptInbound(Json{
			{ "type", "sessionEnded" }, { "code", "modpack_mismatch" }, { "requiredModpackName", "Main List" }
		}.dump());
		assert(named.rejection && named.rejection->requiredModpackName == "Main List");
		F4T::GnsProtocolAdapter::Session unsafe;
		const auto unsafeResult = unsafe.AdaptInbound(Json{
			{ "type", "sessionEnded" }, { "code", "modpack_mismatch" }, { "requiredModpackName", "<b>Main</b>" }
		}.dump());
		assert(unsafeResult.rejection && unsafeResult.rejection->requiredModpackName.empty());
	}

	void TestBuildFailsClosedOnInvalidClientManifest()
	{
		Manifest duplicate;
		duplicate.entries.push_back(Entry{ EntryKind::plugin, PluginType::esp, "Alpha.esp", 0, std::string{ kHashA }, std::nullopt });
		duplicate.entries.push_back(Entry{ EntryKind::plugin, PluginType::esp, "Alpha.esp", 1, std::string{ kHashA }, std::nullopt });

		assert(!BuildModpackMismatchReport(Parsed(RequiredFixture()), duplicate).has_value());
		assert(!BuildModpackMismatchReport(Manifest{}, Parsed(ClientFixture())).has_value());
	}

	Json SessionEndedPacket(Json a_requiredModpack)
	{
		return Json{
			{ "type", "sessionEnded" },
			{ "code", "modpack_mismatch" },
			{ "reason", "Your modpack does not match this server. The server manifest is in requiredModpack." },
			{ "serverTime", 1234.5 },
			{ "requiredModpack", std::move(a_requiredModpack) }
		};
	}

	void TestSessionRejectionCarriesRequiredManifest()
	{
		F4T::GnsProtocolAdapter::Session session;
		const auto result = session.AdaptInbound(
			SessionEndedPacket(Json{ { "version", 1 }, { "entries", RequiredFixture() } }).dump());

		assert(result.rejection.has_value());
		assert(result.rejection->code == "modpack_mismatch");
		assert(!result.rejection->reason.empty());
		assert(result.rejection->requiredModpack.has_value());
		assert(result.rejection->requiredModpack->entries.size() == 6);
		assert(result.rejection->requiredModpack->entries.front().name == "fallout4.esm");

		const auto report = BuildModpackMismatchReport(*result.rejection->requiredModpack, *result.rejection->requiredModpack);
		assert(report.has_value());
		assert(report->entries.empty());
	}

	void TestSessionRejectionRejectsMalformedManifest()
	{
		const std::vector<Json> malformed{
			Json{ { "version", 2 }, { "entries", RequiredFixture() } },
			Json{ { "version", 1 }, { "entries", Json::array({ Json{ { "kind", "native" }, { "name", "n.dll" } } }) } },
			Json{ { "version", 1 }, { "entries", Json::array({ Json{ { "kind", "plugin" }, { "name", "a.esp" }, { "pluginType", "esp" }, { "order", 1 }, { "sha256", std::string{ kHashA } } } }) } }
		};

		for (const auto& required : malformed) {
			F4T::GnsProtocolAdapter::Session session;
			const auto result = session.AdaptInbound(SessionEndedPacket(required).dump());
			assert(result.rejection.has_value());
			assert(result.rejection->code == "modpack_mismatch");
			assert(!result.rejection->requiredModpack.has_value());
		}

		F4T::GnsProtocolAdapter::Session nonObject;
		const auto stringPayload = nonObject.AdaptInbound(SessionEndedPacket(Json{ "not-an-object" }).dump());
		assert(stringPayload.rejection.has_value());
		assert(!stringPayload.rejection->requiredModpack.has_value());

		Json oversized = Json::array();
		for (std::uint32_t index = 0; index < 8300; ++index) {
			oversized.push_back(Asset("native", "n" + std::to_string(index) + ".dll", std::string{ kHashA }));
		}
		F4T::GnsProtocolAdapter::Session bounded;
		const auto boundedResult = bounded.AdaptInbound(
			SessionEndedPacket(Json{ { "version", 1 }, { "entries", std::move(oversized) } }).dump());
		assert(boundedResult.rejection.has_value());
		assert(!boundedResult.rejection->requiredModpack.has_value());
	}

	void TestMissingRequiredModpackFieldStaysAbsent()
	{
		F4T::GnsProtocolAdapter::Session session;
		const auto result = session.AdaptInbound(Json{
			{ "type", "sessionEnded" },
			{ "code", "server_full" },
			{ "reason", "The server is full." }
		}.dump());
		assert(result.rejection.has_value());
		assert(result.rejection->code == "server_full");
		assert(!result.rejection->requiredModpack.has_value());
	}
}

int main()
{
	TestRequiredModpackNameLine();
	TestReportListsEveryDifferenceKind();
	TestReportBoundsEachSection();
	TestFormatNamesKindAndCapsLines();
	TestFormatUsesEveryKindLabel();
	TestArchiveMismatchIsReported();
	TestFormatStaysWithinByteBudget();
	TestFormatOfAgreementReportExplainsShapeDifference();
	TestFormatRequiredListBoundsAndMarksStatus();
	TestFormatRequiredListNotesTruncatedProbe();
	TestBuildFailsClosedOnInvalidClientManifest();
	TestSessionRejectionCarriesRequiredManifest();
	TestSessionRejectionRejectsMalformedManifest();
	TestMissingRequiredModpackFieldStaysAbsent();
	return 0;
}
