#include "F4TModpackManifest.h"
#include "F4TContentManifest.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
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

#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
	constexpr auto kParseTimeLimit = std::chrono::seconds(60);
#else
	constexpr auto kParseTimeLimit = std::chrono::seconds(5);
#endif

	constexpr std::string_view kHashA = "ed7002b439e9ac845f22357d822bac1444730fbdb6016d3ec9432297b9ec9f73";
	constexpr std::string_view kHashB = "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb";

	Json Plugin(std::string name, std::string type, std::uint32_t order, std::string hash)
	{
		return Json{ { "kind", "plugin" }, { "name", std::move(name) }, { "pluginType", std::move(type) }, { "order", order }, { "sha256", std::move(hash) } };
	}

	Json SizedPlugin(std::string name, std::string type, std::uint32_t order, std::uint64_t size)
	{
		return Json{ { "kind", "plugin" }, { "name", std::move(name) }, { "pluginType", std::move(type) }, { "order", order }, { "size", size } };
	}

	Json Asset(std::string kind, std::string name, std::string hash)
	{
		return Json{ { "kind", std::move(kind) }, { "name", std::move(name) }, { "sha256", std::move(hash) } };
	}

	Json Archive(std::string name, std::string hash, std::uint64_t size)
	{
		return Json{ { "kind", "archive" }, { "name", std::move(name) }, { "sha256", std::move(hash) }, { "size", size } };
	}

	Json Root(Json a_entries)
	{
		return Json{ { "version", 1 }, { "entries", std::move(a_entries) } };
	}

	std::string Text(Json a_entries)
	{
		return Root(std::move(a_entries)).dump();
	}

	bool Accepts(Json a_entries)
	{
		return ParseManifest(Text(std::move(a_entries))).has_value();
	}

	Json Base()
	{
		return Json::array({ SizedPlugin("Fallout4.esm", "esm", 0, 100) });
	}

	Json With(Json a_entries, Json a_extra)
	{
		a_entries.push_back(std::move(a_extra));
		return a_entries;
	}

	Manifest Parsed(Json a_entries)
	{
		const auto manifest = ParseManifest(Text(std::move(a_entries)));
		assert(manifest.has_value());
		return *manifest;
	}

	ManifestDiff Diff(Json a_required, Json a_client)
	{
		const auto diff = DiffManifests(Parsed(std::move(a_required)), Parsed(std::move(a_client)));
		assert(diff.has_value());
		return *diff;
	}

	bool IsEmpty(const ManifestDiff& a_diff)
	{
		return a_diff.missing.empty() && a_diff.unexpected.empty() && a_diff.wrongContent.empty() &&
			a_diff.wrongOrder.empty() && a_diff.wrongNative.empty() && a_diff.wrongConfig.empty() &&
			a_diff.wrongArchive.empty();
	}

	Entry Named(std::string a_name)
	{
		Entry entry;
		entry.name = std::move(a_name);
		return entry;
	}

	Manifest Sized(std::size_t a_fixed, std::size_t a_first, std::size_t a_second)
	{
		Manifest manifest;
		for (std::size_t index = 0; index < a_fixed; ++index) {
			auto digits = std::to_string(index);
			digits.insert(0, 6 - digits.size(), '0');
			manifest.entries.push_back(Entry{ EntryKind::native, PluginType::none, "n" + digits + std::string(253, 'x'), 0, std::string{ kHashA }, std::nullopt });
		}
		manifest.entries.push_back(Entry{ EntryKind::native, PluginType::none, "v" + std::string(a_first - 1, 'x'), 0, std::string{ kHashA }, std::nullopt });
		manifest.entries.push_back(Entry{ EntryKind::native, PluginType::none, "w" + std::string(a_second - 1, 'x'), 0, std::string{ kHashA }, std::nullopt });
		return manifest;
	}

	std::size_t SerializedSize(const Manifest& a_manifest)
	{
		const auto text = SerializeManifest(a_manifest);
		assert(text.has_value());
		return text->size();
	}

	void TestFingerprintIgnoresInputOrder()
	{
		const auto first = Json::array({
			Plugin("CommonwealthOnline.esp", "esp", 1, std::string{ kHashA }),
			SizedPlugin("Fallout4.esm", "esm", 0, 100),
			Asset("native", "F4SE/Plugins/Foo.dll", std::string{ kHashB }),
			Asset("config", "CO/config.ini", std::string{ kHashA })
		});
		const auto second = Json::array({
			Asset("config", "CO/config.ini", std::string{ kHashA }),
			Asset("native", "F4SE/Plugins/Foo.dll", std::string{ kHashB }),
			SizedPlugin("Fallout4.esm", "esm", 0, 100),
			Plugin("CommonwealthOnline.esp", "esp", 1, std::string{ kHashA })
		});
		const auto a = ComputeFingerprint(Parsed(first));
		const auto b = ComputeFingerprint(Parsed(second));
		assert(a.has_value());
		assert(a == b);

		const auto swapped = Json::array({
			Plugin("CommonwealthOnline.esp", "esp", 0, std::string{ kHashA }),
			SizedPlugin("Fallout4.esm", "esm", 1, 100)
		});
		const auto ordered = Json::array({
			Plugin("CommonwealthOnline.esp", "esp", 1, std::string{ kHashA }),
			SizedPlugin("Fallout4.esm", "esm", 0, 100)
		});
		assert(ComputeFingerprint(Parsed(swapped)) != ComputeFingerprint(Parsed(ordered)));

		const auto changed = Json::array({ SizedPlugin("Fallout4.esm", "esm", 0, 101) });
		assert(ComputeFingerprint(Parsed(changed)) != ComputeFingerprint(Parsed(Base())));
		assert(!ComputeFingerprint(Manifest{}).has_value());
	}

	void TestEachMismatchClassIsDetected()
	{
		const auto missing = Diff(With(Base(), Plugin("CommonwealthOnline.esp", "esp", 1, std::string{ kHashA })), Base());
		assert(missing.missing.size() == 1 && missing.missing[0].name == "commonwealthonline.esp");
		assert(missing.unexpected.empty() && missing.wrongContent.empty() && missing.wrongOrder.empty());

		const auto unexpected = Diff(Base(), With(Base(), Asset("native", "F4SE/Plugins/Extra.dll", std::string{ kHashA })));
		assert(unexpected.unexpected.size() == 1 && unexpected.unexpected[0].name == "f4se/plugins/extra.dll");
		assert(unexpected.missing.empty() && unexpected.wrongNative.empty());

		const auto wrongContent = Diff(
			With(Base(), Plugin("CommonwealthOnline.esp", "esp", 1, std::string{ kHashA })),
			With(Base(), Plugin("CommonwealthOnline.esp", "esp", 1, std::string{ kHashB })));
		assert(wrongContent.wrongContent.size() == 1);
		assert(wrongContent.missing.empty() && wrongContent.wrongOrder.empty());

		const auto wrongSize = Diff(Base(), Json::array({ SizedPlugin("Fallout4.esm", "esm", 0, 101) }));
		assert(wrongSize.wrongContent.size() == 1 && wrongSize.wrongContent[0].name == "fallout4.esm");

		const auto sizeOnlyNeedsClientSize = Diff(Base(), Json::array({ Plugin("Fallout4.esm", "esm", 0, std::string{ kHashA }) }));
		assert(sizeOnlyNeedsClientSize.wrongContent.size() == 1);

		const auto wrongNative = Diff(
			With(Base(), Asset("native", "F4SE/Plugins/Foo.dll", std::string{ kHashA })),
			With(Base(), Asset("native", "F4SE/Plugins/Foo.dll", std::string{ kHashB })));
		assert(wrongNative.wrongNative.size() == 1 && wrongNative.wrongContent.empty());

		const auto wrongConfig = Diff(
			With(Base(), Asset("config", "CO/config.ini", std::string{ kHashA })),
			With(Base(), Asset("config", "CO/config.ini", std::string{ kHashB })));
		assert(wrongConfig.wrongConfig.size() == 1 && wrongConfig.wrongNative.empty());

		const auto wrongOrder = Diff(
			With(With(With(Base(), Plugin("A.esp", "esp", 1, std::string{ kHashA })), Plugin("B.esp", "esp", 2, std::string{ kHashB })), Plugin("C.esp", "esp", 3, std::string{ kHashA })),
			With(With(With(Base(), Plugin("A.esp", "esp", 1, std::string{ kHashA })), Plugin("C.esp", "esp", 2, std::string{ kHashA })), Plugin("B.esp", "esp", 3, std::string{ kHashB })));
		assert(wrongOrder.wrongOrder.size() == 2);
		assert(wrongOrder.wrongOrder[0].name == "b.esp" && wrongOrder.wrongOrder[1].name == "c.esp");
		assert(wrongOrder.missing.empty() && wrongOrder.wrongContent.empty());

		const auto missingDoesNotShiftOrder = Diff(
			With(With(With(Base(), Plugin("A.esp", "esp", 1, std::string{ kHashA })), Plugin("B.esp", "esp", 2, std::string{ kHashB })), Plugin("C.esp", "esp", 3, std::string{ kHashA })),
			With(With(Base(), Plugin("A.esp", "esp", 1, std::string{ kHashA })), Plugin("C.esp", "esp", 2, std::string{ kHashA })));
		assert(missingDoesNotShiftOrder.missing.size() == 1 && missingDoesNotShiftOrder.missing[0].name == "b.esp");
		assert(missingDoesNotShiftOrder.wrongOrder.empty());
	}

	void TestArchiveEntryIsContentChecked()
	{
		assert(Accepts(Json::array({ Archive("Fallout4 - Textures1.ba2", std::string{ kHashA }, 100) })));
		assert(Accepts(Json::array({ Archive("Data/Textures/Extra.ba2", std::string{ kHashA }, 100) })));
		assert(!Accepts(Json::array({ { { "kind", "archive" }, { "name", "a.ba2" }, { "sha256", std::string{ kHashA } } } })));
		assert(!Accepts(Json::array({ { { "kind", "archive" }, { "name", "a.ba2" }, { "size", 100 } } })));
		assert(!Accepts(Json::array({ { { "kind", "archive" }, { "name", "a.ba2" }, { "pluginType", "esp" }, { "sha256", std::string{ kHashA } }, { "size", 100 } } })));
		assert(!Accepts(Json::array({ { { "kind", "archive" }, { "name", "a.ba2" }, { "order", 0 }, { "sha256", std::string{ kHashA } }, { "size", 100 } } })));
		assert(!Accepts(Json::array({ { { "kind", "archive" }, { "name", "a.ba2" }, { "sha256", std::string{ kHashA } }, { "size", 100 }, { "extra", 1 } } })));

		const auto hashDiff = Diff(
			Json::array({ Archive("a.ba2", std::string{ kHashA }, 100) }),
			Json::array({ Archive("a.ba2", std::string{ kHashB }, 100) }));
		assert(hashDiff.wrongArchive.size() == 1);
		assert(hashDiff.wrongContent.empty() && hashDiff.wrongNative.empty() && hashDiff.wrongConfig.empty());

		const auto sizeDiff = Diff(
			Json::array({ Archive("a.ba2", std::string{ kHashA }, 100) }),
			Json::array({ Archive("a.ba2", std::string{ kHashA }, 101) }));
		assert(sizeDiff.wrongArchive.size() == 1);

		assert(ComputeFingerprint(Parsed(Json::array({ Archive("a.ba2", std::string{ kHashA }, 100) }))) !=
			ComputeFingerprint(Parsed(Json::array({ Archive("a.ba2", std::string{ kHashA }, 101) }))));
		assert(ComputeFingerprint(Parsed(Json::array({ Archive("a.ba2", std::string{ kHashA }, 100) }))) !=
			ComputeFingerprint(Parsed(Json::array({ Archive("a.ba2", std::string{ kHashB }, 100) }))));

		const auto serialized = SerializeManifest(Parsed(Json::array({ Archive("a.ba2", std::string{ kHashA }, 100) })));
		assert(serialized.has_value());
		assert(serialized->contains("\"kind\":\"archive\""));
		assert(serialized->contains("\"size\":100"));
		const auto reparsed = ParseManifest(*serialized);
		assert(reparsed.has_value());
		assert(SerializeManifest(*reparsed) == serialized);
	}

	void TestPluginTypeMismatchIsWrongContent()
	{
		const auto diff = Diff(
			With(Base(), Plugin("CommonwealthOnline.esp", "esp", 1, std::string{ kHashA })),
			With(Base(), Plugin("CommonwealthOnline.esp", "esl", 1, std::string{ kHashA })));
		assert(diff.wrongContent.size() == 1 && diff.wrongContent[0].pluginType == PluginType::esp);
		assert(diff.missing.empty() && diff.unexpected.empty() && diff.wrongOrder.empty());
	}

	void TestUnexpectedPluginDoesNotShiftOrder()
	{
		const auto diff = Diff(
			With(With(Base(), Plugin("A.esp", "esp", 1, std::string{ kHashA })), Plugin("B.esp", "esp", 2, std::string{ kHashB })),
			With(With(With(Base(), Plugin("Extra.esp", "esp", 1, std::string{ kHashA })), Plugin("A.esp", "esp", 2, std::string{ kHashA })), Plugin("B.esp", "esp", 3, std::string{ kHashB })));
		assert(diff.unexpected.size() == 1 && diff.unexpected[0].name == "extra.esp");
		assert(diff.wrongOrder.empty() && diff.missing.empty() && diff.wrongContent.empty());
	}

	void TestBounds()
	{
		Json maximum = Json::array();
		for (std::size_t index = 0; index < 8192; ++index) {
			maximum.push_back(Asset("native", "n" + std::to_string(index) + ".dll", std::string{ F4T::ContentManifest::Sha256Hex(std::to_string(index)) }));
		}
		const auto accepted = ParseManifest(Text(maximum));
		assert(accepted.has_value());
		assert(accepted->entries.size() == 8192);
		assert(!Accepts(With(maximum, Asset("native", "over.dll", std::string{ kHashA }))));

		Json plugins = Json::array();
		for (std::size_t index = 0; index < 4351; ++index) {
			plugins.push_back(SizedPlugin("p" + std::to_string(index) + ".esp", "esp", static_cast<std::uint32_t>(index), 1));
		}
		assert(Accepts(plugins));
		assert(!Accepts(With(plugins, SizedPlugin("p4351.esp", "esp", 4351, 1))));

		assert(Accepts(Json::array({ Asset("native", std::string(256, 'a') + ".dll", std::string{ kHashA }) })));
		assert(!Accepts(Json::array({ Asset("native", std::string(257, 'a') + ".dll", std::string{ kHashA }) })));

		const auto valid = Text(Base());
		assert(valid.size() < kMaximumManifestBytes);
		assert(ParseManifest(valid + std::string(kMaximumManifestBytes - valid.size(), ' ')).has_value());
		assert(!ParseManifest(valid + std::string(kMaximumManifestBytes - valid.size() + 1, ' ')).has_value());

		Json missing = Json::array();
		for (std::size_t index = 0; index < 4096; ++index) {
			missing.push_back(Asset("native", "n" + std::to_string(index) + ".dll", std::string{ kHashA }));
		}
		const auto text = FormatMismatch(Diff(missing, Base()));
		assert(text.size() <= 2048);
		assert(text.starts_with("MODPACK MISMATCH\n\nMissing:\n"));
		assert(text.ends_with("\n"));
	}

	void TestSerializeBoundary()
	{
		constexpr std::size_t cap = kMaximumManifestBytes;
		const auto base = SerializedSize(Sized(0, 2, 2));
		const auto perEntry = SerializedSize(Sized(1, 2, 2)) - base;
		const auto fixed = (cap - 1 - base) / perEntry;
		const auto start = SerializedSize(Sized(fixed, 2, 2));
		const auto extra = cap - start;
		assert(start < cap && extra <= perEntry);

		const auto withExtra = [fixed](std::size_t a_extra) {
			const auto first = std::min<std::size_t>(a_extra, 258);
			return Sized(fixed, 2 + first, 2 + (a_extra - first));
		};

		const auto under = withExtra(extra);
		assert(SerializedSize(under) == cap);
		const auto serialized = SerializeManifest(under);
		assert(serialized.has_value());
		const auto reparsed = ParseManifest(*serialized);
		assert(reparsed.has_value());
		assert(SerializeManifest(*reparsed) == serialized);

		assert(!SerializeManifest(withExtra(extra + 1)).has_value());
	}

	void TestMalformedInputIsRejected()
	{
		assert(!ParseManifest("").has_value());
		assert(!ParseManifest("{").has_value());
		assert(!ParseManifest("[]").has_value());
		assert(!ParseManifest(R"({"version":1,"entries":[]})").has_value());
		assert(!ParseManifest(R"({"version":1,"entries":{}})").has_value());

		const auto entries = Json::array({ SizedPlugin("Fallout4.esm", "esm", 0, 100) });
		assert(!ParseManifest(Json{ { "entries", entries } }.dump()).has_value());
		assert(!ParseManifest(Json{ { "version", 2 }, { "entries", entries } }.dump()).has_value());
		assert(!ParseManifest(Json{ { "version", "1" }, { "entries", entries } }.dump()).has_value());
		assert(!ParseManifest(R"({"version":1.0,"entries":[]})").has_value());
		assert(!ParseManifest(Json{ { "version", 1 }, { "entries", entries }, { "extra", 1 } }.dump()).has_value());

		assert(!ParseManifest(R"({"version":2,"version":1,"entries":[{"kind":"native","name":"a.dll","sha256":"ed7002b439e9ac845f22357d822bac1444730fbdb6016d3ec9432297b9ec9f73"}]})").has_value());
		assert(!ParseManifest(R"({"version":1,"entries":[{"kind":"native","kind":"plugin","name":"a.dll","sha256":"ed7002b439e9ac845f22357d822bac1444730fbdb6016d3ec9432297b9ec9f73"}]})").has_value());
		assert(!ParseManifest(R"({"version":1,"entries":[{"kind":"native","name":"a.dll","sha256":"ed7002b439e9ac845f22357d822bac1444730fbdb6016d3ec9432297b9ec9f73","sha256":"ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"}]})").has_value());
		assert(ParseManifest(R"({"version":1,"entries":[{"kind":"native","name":"a.dll","sha256":"ed7002b439e9ac845f22357d822bac1444730fbdb6016d3ec9432297b9ec9f73"},{"kind":"config","name":"b.ini","sha256":"ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"}]})").has_value());

		assert(!Accepts(Json::array({ { { "kind", "native" }, { "name", "a.dll" }, { "sha265", std::string{ kHashA } } } })));
		assert(!Accepts(Json::array({ { { "kind", "native" }, { "name", "a.dll" } } })));
		assert(!Accepts(Json::array({ { { "kind", "native" }, { "name", "a.dll" }, { "sha256", "" } } })));
		assert(!Accepts(Json::array({ { { "kind", "plugin" }, { "name", "a.esm" }, { "pluginType", "esm" }, { "order", 0 }, { "sha256", "" }, { "size", 1 } } })));
		assert(!Accepts(Json::array({ { { "kind", "native" }, { "name", "a.dll" }, { "sha256", std::string{ kHashA } }, { "size", 5 } } })));
		assert(!Accepts(Json::array({ { { "kind", "native" }, { "name", "a.dll" }, { "sha256", std::string{ kHashA } }, { "order", 0 } } })));
		assert(!Accepts(Json::array({ { { "kind", "native" }, { "name", "a.dll" }, { "size", 5 } } })));
		assert(!Accepts(Json::array({ { { "kind", "plugin" }, { "name", "a.esm" }, { "pluginType", "esm" }, { "order", 0 }, { "sha256", std::string{ kHashA } }, { "size", 1 } } })));
		assert(!Accepts(Json::array({ { { "kind", "plugin" }, { "name", "a.esm" }, { "pluginType", "esm" }, { "order", 0 } } })));
		assert(!Accepts(Json::array({ { { "kind", "plugin" }, { "name", "a.esm" }, { "pluginType", "esm" }, { "order", 0 }, { "sha256", std::string{ kHashA } }, { "extra", 1 } } })));
		assert(!Accepts(Json::array({ SizedPlugin("a.esm", "none", 0, 1) })));
		assert(!Accepts(Json::array({ { { "kind", "plugin" }, { "name", "a.esm" }, { "order", 0 }, { "size", 1 } } })));
		assert(!Accepts(Json::array({ SizedPlugin("a.esm", "esm", 4351, 1) })));
		assert(!ParseManifest(R"({"version":1,"entries":[{"kind":"plugin","name":"a.esm","pluginType":"esm","order":18446744073709551616,"size":1}]})").has_value());
		assert(!ParseManifest(R"({"version":1,"entries":[{"kind":"plugin","name":"a.esm","pluginType":"esm","order":18446744073709551615,"size":1}]})").has_value());
		assert(!Accepts(Json::array({ { { "kind", "plugin" }, { "name", "a.esm" }, { "pluginType", "esm" }, { "order", "0" }, { "size", 1 } } })));
		assert(!Accepts(Json::array({ { { "kind", "plugin" }, { "name", "a.esm" }, { "pluginType", "esm" }, { "order", 0 }, { "size", -1 } } })));
		assert(!Accepts(Json::array({ { { "kind", "plugin" }, { "name", "a.esm" }, { "pluginType", "esm" }, { "order", 0 }, { "size", 1.5 } } })));
		assert(!Accepts(Json::array({ { { "kind", "plugin" }, { "name", "a.esm" }, { "pluginType", "esm" }, { "order", 0 }, { "size", true } } })));
		assert(!Accepts(Json::array({ { { "kind", "native" }, { "name", 5 }, { "sha256", std::string{ kHashA } } } })));
		assert(!Accepts(Json::array({ { { "kind", "unknown" }, { "name", "a.dll" }, { "sha256", std::string{ kHashA } } } })));
		assert(!Accepts(Json::array({ Asset("native", "a.dll", "ED7002B439E9AC845F22357D822BAC1444730FBDB6016D3EC9432297B9EC9F73") })));
		assert(!Accepts(Json::array({ Asset("native", "a.dll", "abc") })));

		assert(!Accepts(Json::array({ SizedPlugin("a.esm", "esm", 0, 1), SizedPlugin("b.esp", "esp", 2, 1) })));
		assert(!Accepts(Json::array({ SizedPlugin("a.esm", "esm", 0, 1), SizedPlugin("b.esp", "esp", 0, 1) })));
		assert(!Accepts(Json::array({ Asset("native", "a.dll", std::string{ kHashA }), Asset("native", "A.DLL", std::string{ kHashB }) })));
		assert(Accepts(Json::array({ Asset("native", "a.dll", std::string{ kHashA }), Asset("config", "a.dll", std::string{ kHashB }) })));

		const auto badName = [](std::string_view a_name) {
			return !Accepts(Json::array({ Asset("native", std::string{ a_name }, std::string{ kHashA }) }));
		};
		assert(badName("../x.dll"));
		assert(badName("a//b.dll"));
		assert(badName("/a.dll"));
		assert(badName("a/"));
		assert(badName("a/./b.dll"));
		assert(badName("C:x.dll"));
		assert(badName("a\tb.dll"));
		assert(badName("a.dll."));
		assert(badName("a /b.dll"));
		assert(badName("a<b.dll"));
		assert(badName("a>b.dll"));
		assert(badName("a\"b.dll"));
		assert(badName("a|b.dll"));
		assert(badName("a?b.dll"));
		assert(badName("a*b.dll"));
		assert(badName("con.dll"));
		assert(badName("CON"));
		assert(badName("dir/nul.txt"));
		assert(badName("com1.dll"));
		assert(badName("LPT9"));
		assert(badName("conin$"));
		assert(badName("conout$.txt"));
		assert(badName("con .dll"));
		assert(badName("com\xc2\xb9.dll"));
		assert(badName("lpt\xc2\xb2"));
		assert(badName("com\xc2\xb3.txt"));
		assert(!badName("com\xc2\xb4.dll"));
		assert(!badName("com10.dll"));
		assert(!badName("conx.dll"));
		assert(!badName("a b.dll"));
		assert(!badName("F4SE\\Plugins\\X.dll"));

		assert(badName("a\xe2\x80\x8e.dll"));
		assert(badName("a\xe2\x80\x8f.dll"));
		assert(badName("a\xe2\x80\xaa.dll"));
		assert(badName("a\xe2\x80\xae.dll"));
		assert(badName("a\xe2\x81\xa6.dll"));
		assert(badName("a\xe2\x81\xa9.dll"));
		assert(!badName("a\xe2\x80\xaf.dll"));
		assert(badName("a\xd8\x9c.dll"));
		assert(!badName("a\xd8\x9b.dll"));

		assert(!Accepts(Json::array({ SizedPlugin("sub/a.esp", "esp", 0, 1) })));
		assert(!Accepts(Json::array({ SizedPlugin("sub\\a.esp", "esp", 0, 1) })));
	}

	void TestHostileStructureIsRejectedQuickly()
	{
		std::string hostile = R"({"entries":[)";
		const auto count = (kMaximumManifestBytes - 16) / 3;
		for (std::size_t index = 0; index < count; ++index) {
			hostile += "{},";
		}
		hostile += "{}]}";
		assert(hostile.size() <= kMaximumManifestBytes);
		const auto start = std::chrono::steady_clock::now();
		const auto manifest = ParseManifest(hostile);
		const auto elapsed = std::chrono::steady_clock::now() - start;
		assert(!manifest.has_value());
		assert(elapsed < kParseTimeLimit);
	}

	void TestRealisticMaximumParses()
	{
		Manifest manifest;
		for (std::size_t index = 0; index < 254; ++index) {
			auto name = "f" + std::to_string(index);
			name.append(260 - name.size(), 'x');
			manifest.entries.push_back(Entry{ EntryKind::plugin, PluginType::esp, name, static_cast<std::uint32_t>(index), std::string{ kHashA }, std::nullopt });
		}
		for (std::size_t index = 254; index < 4351; ++index) {
			manifest.entries.push_back(Entry{ EntryKind::plugin, PluginType::esp, "p" + std::to_string(index) + ".esp", static_cast<std::uint32_t>(index), std::string{ kHashB }, std::nullopt });
		}
		for (std::size_t index = 0; index < 3841; ++index) {
			manifest.entries.push_back(Entry{ EntryKind::native, PluginType::none, "F4SE/Plugins/mod" + std::to_string(index) + ".dll", 0, std::string{ kHashA }, std::nullopt });
		}
		const auto text = SerializeManifest(manifest);
		assert(text.has_value() && text->size() < kMaximumManifestBytes);
		const auto start = std::chrono::steady_clock::now();
		const auto parsed = ParseManifest(*text);
		const auto elapsed = std::chrono::steady_clock::now() - start;
		assert(parsed.has_value() && parsed->entries.size() == 8192);
		assert(elapsed < kParseTimeLimit);
	}

	void TestStructNormalizationMatchesParsed()
	{
		const Manifest built{ {
			Entry{ EntryKind::plugin, PluginType::esm, "Fallout4.esm", 0, {}, std::uint64_t{ 100 } },
			Entry{ EntryKind::native, PluginType::none, "F4SE\\Plugins\\X.dll", 0, std::string{ kHashA }, std::nullopt }
		} };
		const auto parsed = Parsed(Json::array({
			SizedPlugin("fallout4.esm", "esm", 0, 100),
			Asset("native", "f4se/plugins/x.dll", std::string{ kHashA })
		}));
		assert(ComputeFingerprint(built).has_value());
		assert(ComputeFingerprint(built) == ComputeFingerprint(parsed));
		assert(IsEmpty(*DiffManifests(built, parsed)));
		assert(SerializeManifest(built) == SerializeManifest(parsed));

		const Manifest bothFields{ {
			Entry{ EntryKind::plugin, PluginType::esm, "Fallout4.esm", 0, std::string{ kHashA }, std::uint64_t{ 100 } }
		} };
		assert(!NormalizeManifest(bothFields).has_value());
		assert(!SerializeManifest(bothFields).has_value());
		assert(!ComputeFingerprint(bothFields).has_value());
		assert(!DiffManifests(bothFields, parsed).has_value());

		const Manifest gap{ {
			Entry{ EntryKind::plugin, PluginType::esm, "Fallout4.esm", 0, {}, std::uint64_t{ 100 } },
			Entry{ EntryKind::plugin, PluginType::esp, "b.esp", 2, std::string{ kHashA }, {} }
		} };
		assert(!NormalizeManifest(gap).has_value());

		const Manifest invalidUtf8{ {
			Entry{ EntryKind::native, PluginType::none, "bad\xff.dll", 0, std::string{ kHashA }, std::nullopt }
		} };
		assert(!SerializeManifest(invalidUtf8).has_value());
		assert(!ComputeFingerprint(invalidUtf8).has_value());
	}

	void TestFormatMismatchMatchesIssueLayout()
	{
		ManifestDiff diff;
		diff.missing.push_back(Named("Munitions.esp"));
		diff.wrongContent.push_back(Named("TrueDamage.esp"));
		diff.wrongNative.push_back(Named("CombatOverhaul.dll"));
		diff.wrongConfig.push_back(Named("CombatOverhaul.ini"));
		diff.wrongArchive.push_back(Named("CombatOverhaul.ba2"));
		diff.wrongOrder.push_back(Named("WorkshopFramework.esm"));
		diff.wrongOrder.push_back(Named("SimSettlements2.esm"));
		const std::string expected =
			"MODPACK MISMATCH\n"
			"\n"
			"Missing:\n"
			"  Munitions.esp\n"
			"\n"
			"Wrong version/content:\n"
			"  TrueDamage.esp\n"
			"\n"
			"Wrong native plugin:\n"
			"  CombatOverhaul.dll\n"
			"\n"
			"Wrong config:\n"
			"  CombatOverhaul.ini\n"
			"\n"
			"Wrong archive:\n"
			"  CombatOverhaul.ba2\n"
			"\n"
			"Wrong load order:\n"
			"  WorkshopFramework.esm\n"
			"  SimSettlements2.esm\n";
		assert(FormatMismatch(diff) == expected);
	}

	void TestTruncationKeepsEverySection()
	{
		ManifestDiff diff;
		const std::array<std::vector<Entry>*, 7> lists{
			&diff.missing,
			&diff.unexpected,
			&diff.wrongContent,
			&diff.wrongNative,
			&diff.wrongConfig,
			&diff.wrongArchive,
			&diff.wrongOrder
		};
		for (std::size_t list = 0; list < lists.size(); ++list) {
			for (std::size_t index = 0; index < 300; ++index) {
				lists[list]->push_back(Named(std::string(250, static_cast<char>('a' + list)) + std::to_string(index)));
			}
		}
		const auto text = FormatMismatch(diff);
		assert(text.size() <= 2048);
		assert(text.ends_with("\n"));
		assert(text.contains("\nMissing:\n"));
		assert(text.contains("\nUnexpected:\n"));
		assert(text.contains("\nWrong version/content:\n"));
		assert(text.contains("\nWrong native plugin:\n"));
		assert(text.contains("\nWrong config:\n"));
		assert(text.contains("\nWrong archive:\n"));
		assert(text.contains("\nWrong load order:\n"));
		assert(text.contains("  ...and "));

		std::size_t start = 0;
		while (start < text.size()) {
			const auto end = text.find('\n', start);
			assert(end != std::string::npos);
			const auto line = std::string_view{ text }.substr(start, end - start);
			assert(line.empty() || line == "MODPACK MISMATCH" || line.starts_with("  ") || line.ends_with(':'));
			start = end + 1;
		}
	}

	void TestDiffIsEmptyExactlyWhenFingerprintsMatch()
	{
		const std::array<Json, 14> fixtures{
			Base(),
			With(Base(), Plugin("CommonwealthOnline.esp", "esp", 1, std::string{ kHashA })),
			With(Base(), Plugin("CommonwealthOnline.esp", "esp", 1, std::string{ kHashB })),
			With(Base(), Asset("native", "F4SE/Plugins/Extra.dll", std::string{ kHashA })),
			With(Base(), Plugin("CommonwealthOnline.esp", "esl", 1, std::string{ kHashA })),
			Json::array({ SizedPlugin("Fallout4.esm", "esm", 0, 101) }),
			Json::array({ Plugin("Fallout4.esm", "esm", 0, std::string{ kHashA }) }),
			With(With(Base(), Plugin("A.esp", "esp", 1, std::string{ kHashA })), Plugin("B.esp", "esp", 2, std::string{ kHashB })),
			With(With(Base(), Plugin("A.esp", "esp", 2, std::string{ kHashA })), Plugin("B.esp", "esp", 1, std::string{ kHashB })),
			With(Base(), Asset("native", "F4SE/Plugins/Foo.dll", std::string{ kHashA })),
			With(Base(), Asset("config", "CO/config.ini", std::string{ kHashB })),
			With(With(With(Base(), Plugin("Extra.esp", "esp", 1, std::string{ kHashA })), Plugin("A.esp", "esp", 2, std::string{ kHashA })), Plugin("B.esp", "esp", 3, std::string{ kHashB })),
			Json::array({ Archive("a.ba2", std::string{ kHashA }, 100) }),
			Json::array({ Archive("a.ba2", std::string{ kHashB }, 101) })
		};
		for (const auto& required : fixtures) {
			for (const auto& client : fixtures) {
				const auto diff = DiffManifests(Parsed(required), Parsed(client));
				assert(diff.has_value());
				const auto requiredFingerprint = ComputeFingerprint(Parsed(required));
				const auto clientFingerprint = ComputeFingerprint(Parsed(client));
				assert(requiredFingerprint.has_value() && clientFingerprint.has_value());
				assert(IsEmpty(*diff) == (*requiredFingerprint == *clientFingerprint));
			}
		}
	}

	void TestMatchingFingerprintReturnsOnlyExactMatches()
	{
		const auto required = Parsed(Base());
		const auto exact = MatchingFingerprint(required, Parsed(Base()));
		assert(exact.has_value());
		assert(*exact == *ComputeFingerprint(required));
		assert(exact->size() == 64);
		assert(!MatchingFingerprint(required, Parsed(Json::array({ SizedPlugin("Fallout4.esm", "esm", 0, 101) }))).has_value());
		assert(!MatchingFingerprint(required, Parsed(With(Base(), Plugin("Extra.esp", "esp", 1, std::string{ kHashA })))).has_value());
		assert(!MatchingFingerprint(required, Parsed(Json::array({ Plugin("Fallout4.esm", "esm", 0, std::string{ kHashA }) }))).has_value());
		const auto ordered = With(With(Base(), Plugin("A.esp", "esp", 1, std::string{ kHashA })), Plugin("B.esp", "esp", 2, std::string{ kHashB }));
		const auto swapped = With(With(Base(), Plugin("A.esp", "esp", 2, std::string{ kHashA })), Plugin("B.esp", "esp", 1, std::string{ kHashB }));
		assert(MatchingFingerprint(Parsed(ordered), Parsed(ordered)).has_value());
		assert(!MatchingFingerprint(Parsed(ordered), Parsed(swapped)).has_value());
		assert(!MatchingFingerprint(Manifest{}, required).has_value());
		assert(!MatchingFingerprint(required, Manifest{}).has_value());
	}

	void TestRoundTrip()
	{
		const auto source = Json::array({
			Asset("config", "CO/config.ini", std::string{ kHashB }),
			Plugin("CommonwealthOnline.esp", "esp", 1, std::string{ kHashA }),
			SizedPlugin("Fallout4.esm", "esm", 0, 100),
			Asset("native", "F4SE/Plugins/Foo.dll", std::string{ kHashA }),
			Archive("Fallout4 - Textures1.ba2", std::string{ kHashB }, 200)
		});
		const auto parsed = ParseManifest(Text(source));
		assert(parsed.has_value());
		const auto serialized = SerializeManifest(*parsed);
		assert(serialized.has_value());
		assert(serialized->contains("\"version\":1"));
		const auto reparsed = ParseManifest(*serialized);
		assert(reparsed.has_value());
		assert(SerializeManifest(*reparsed) == serialized);
		assert(ComputeFingerprint(*reparsed) == ComputeFingerprint(*parsed));
		assert(serialized->contains("fallout4.esm"));
		assert(!serialized->contains("Fallout4"));
		assert(serialized->contains("\"kind\":\"archive\""));
	}
}

int main()
{
	TestHostileStructureIsRejectedQuickly();
	TestRealisticMaximumParses();
	TestFingerprintIgnoresInputOrder();
	TestEachMismatchClassIsDetected();
	TestArchiveEntryIsContentChecked();
	TestPluginTypeMismatchIsWrongContent();
	TestUnexpectedPluginDoesNotShiftOrder();
	TestBounds();
	TestSerializeBoundary();
	TestMalformedInputIsRejected();
	TestStructNormalizationMatchesParsed();
	TestFormatMismatchMatchesIssueLayout();
	TestTruncationKeepsEverySection();
	TestDiffIsEmptyExactlyWhenFingerprintsMatch();
	TestMatchingFingerprintReturnsOnlyExactMatches();
	TestRoundTrip();
	return 0;
}
