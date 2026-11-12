#include "F4TNexusSources.h"

#include <cassert>

int main()
{
	using namespace F4T::NexusSources;
	using namespace F4T::ModpackManifest;
	const Manifest manifest{ { Entry{ .kind = EntryKind::plugin, .pluginType = PluginType::esp, .name = "Example.esp", .sha256Hex = std::string(64, 'a') } } };
	const auto valid = Parse(R"({"version":1,"files":[{"name":"Example.esp","modId":123,"fileId":456}]})", manifest);
	assert(valid && valid->size() == 1);
	auto withoutHash = manifest;
	withoutHash.entries[0].sha256Hex.clear();
	assert(!Parse(R"({"version":1,"files":[{"name":"Example.esp","modId":123,"fileId":456}]})", withoutHash));
	assert(FilePage(valid->front()) == "https://www.nexusmods.com/fallout4/mods/123?tab=files#file-expander-header-456");
	assert(Parse(R"({"version":1,"files":[{"name":"example.esp","modId":123,"fileId":456}]})", manifest));
	assert(!Parse(R"({"version":1,"files":[{"name":"Other.esp","modId":123,"fileId":456}]})", manifest));
	assert(!Parse(R"({"version":1,"files":[{"name":"Example.esp","modId":0,"fileId":456}]})", manifest));
	assert(!Parse(R"({"version":1,"files":[{"name":"Example.esp","modId":"123","fileId":456}]})", manifest));
	assert(!Parse(R"({"version":1,"files":[{"name":"Example.esp","modId":123,"fileId":456,"url":"https://evil.invalid"}]})", manifest));
	assert(!Parse(R"({"version":1,"version":1,"files":[]})", manifest));
	assert(!Parse(R"({"version":1.0,"files":[]})", manifest));
	assert(!Parse(R"({"version":1,"files":[{"name":"Example.esp","modId":123,"modId":456,"fileId":456}]})", manifest));
	assert(!Parse(R"({"version":1,"files":[[[[[[]]]]]]})", manifest));
	const auto collection = ParseCollection("abc123", 42);
	assert(collection && CollectionPage(*collection) == "https://next.nexusmods.com/fallout4/collections/abc123/revisions/42");
	assert(!ParseCollection("../abc", 1));
	assert(!ParseCollection("abc123", 0));
	assert(!ParseCollection("abc123", UINT64_MAX));
	return 0;
}
