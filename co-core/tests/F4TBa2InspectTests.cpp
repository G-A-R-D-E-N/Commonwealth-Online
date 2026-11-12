#include "F4TBa2Inspect.h"

#include <cassert>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	using namespace F4T::Ba2Inspect;

	void PutLittle(std::string& a_out, std::uint64_t a_value, int a_bytes)
	{
		for (int index = 0; index < a_bytes; ++index) {
			a_out += static_cast<char>((a_value >> (8 * index)) & 0xFF);
		}
	}

	std::string Archive(std::uint32_t a_version, const char* a_type, const std::vector<std::string>& a_names, std::uint32_t a_claimedCount = 0)
	{
		std::string bytes = "BTDX";
		PutLittle(bytes, a_version, 4);
		bytes += std::string(a_type, 4);
		PutLittle(bytes, a_claimedCount ? a_claimedCount : static_cast<std::uint32_t>(a_names.size()), 4);
		const std::uint64_t nameTable = 24 + 100;
		PutLittle(bytes, nameTable, 8);
		bytes += std::string(100, '\0');
		for (const auto& name : a_names) {
			PutLittle(bytes, name.size(), 2);
			bytes += name;
		}
		return bytes;
	}

	std::optional<Report> InspectBytes(const std::string& a_bytes)
	{
		std::istringstream stream(a_bytes);
		return Inspect(stream, a_bytes.size());
	}

	void TestCleanArchiveIsAcceptable()
	{
		for (const auto version : { 1u, 7u, 8u }) {
			const auto report = InspectBytes(Archive(version, "GNRL", { "Meshes\\MyMod\\a.nif", "Sound\\b.wav" }));
			assert(report && report->fileCount == 2 && report->refusedCount == 0 && IsAcceptable(*report));
		}
		const auto textures = InspectBytes(Archive(8, "DX10", { "Textures\\a.dds" }));
		assert(textures && textures->type == ArchiveType::textures && IsAcceptable(*textures));
	}

	void TestScriptsAndInterfaceAreCounted()
	{
		const auto report = InspectBytes(Archive(1, "GNRL", { "Scripts\\Actor.pex", "Interface\\HUDMenu.swf", "Meshes\\a.nif" }));
		assert(report && report->scripts == 1 && report->interfaceFiles == 1 && IsAcceptable(*report));
	}

	void TestDangerousInnerPathsAreRefused()
	{
		const auto report = InspectBytes(Archive(1, "GNRL", { "F4SE\\Plugins\\evil.dll", "..\\..\\x.esp", "C:\\Windows\\a.txt", "\\abs.nif", "Meshes\\ok.nif", "Data\\Fallout4Custom.ini", "tools\\run.bat" }));
		assert(report && report->refusedCount == 6 && !IsAcceptable(*report));
		assert(report->refusedPaths.front() == "F4SE\\Plugins\\evil.dll");
		assert(IsRefusedInnerPath("f4se/plugins/x.txt"));
		assert(!IsRefusedInnerPath("Meshes\\..x\\ok.nif"));
	}

	void TestMalformedArchivesAreRejected()
	{
		assert(!InspectBytes("BTDX"));
		assert(!InspectBytes(std::string("XXXX") + std::string(40, '\0')));
		assert(!InspectBytes(Archive(1, "ABCD", { "a" })));
		assert(!InspectBytes(Archive(1, "GNRL", { "a" }, 1000)));
		auto truncated = Archive(1, "GNRL", { "Meshes\\a.nif" });
		truncated.resize(truncated.size() - 3);
		assert(!InspectBytes(truncated));
		const auto unknownVersion = InspectBytes(Archive(2, "GNRL", { "Meshes\\a.nif" }));
		assert(unknownVersion && !IsAcceptable(*unknownVersion));
	}
}

int main()
{
	TestCleanArchiveIsAcceptable();
	TestScriptsAndInterfaceAreCounted();
	TestDangerousInnerPathsAreRefused();
	TestMalformedArchivesAreRejected();
	return 0;
}
