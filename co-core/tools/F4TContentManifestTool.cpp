#include "F4TContentManifest.h"
#include "F4TClientCompatibility.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
	using F4T::ContentManifest::Entry;

	constexpr std::string_view kPackagedClientManifestPath = "F4SE/Plugins/CommonwealthOnlineClient.json";

	struct Options
	{
		std::filesystem::path root;
		std::unordered_set<std::string> excludedNames;
		std::filesystem::path emitClientJson;
	};

	void PrintUsage(std::ostream& a_out)
	{
		a_out <<
			"Usage: F4TContentManifestTool --root <dir> [--exclude-name <name>]... "
			"[--emit-client-json <path>]\n"
			"\n"
			"Computes the canonical Commonwealth Online content-manifest fingerprint of the\n"
			"staged package under <dir>: every regular file, hashed, with its path made\n"
			"relative to <dir> (forward slashes), sorted, and fingerprinted exactly as\n"
			"F4T::ContentManifest::ComputeFingerprint defines. Prints one 64-hex SHA-256 line.\n"
			"\n"
			"The client binaries are not content and must be excluded so both targets and the\n"
			"server agree on one fingerprint. The canonical release invocation passes:\n"
			"  --exclude-name CommonwealthOnline.dll\n"
			"  --exclude-name CommonwealthOnline-OG.dll\n"
			"  --exclude-name commonwealth_online_gns_bridge.dll\n"
			"\n"
			"F4SE/Plugins/CommonwealthOnlineClient.json is packaged metadata and is always\n"
			"excluded from the content set, so recomputing a staged package is stable.\n"
			"\n"
			"--emit-client-json <path> also writes the per-release packaged client manifest\n"
			"(CommonwealthOnlineClient.json: clientVersion + this fingerprint) so the next\n"
			"release ships the exact value the server will pin. Stage it at\n"
			"Data\\F4SE\\Plugins\\CommonwealthOnlineClient.json.\n"
			"\n"
			"Exit status: 0 on success, 1 on a content/manifest error, 2 on bad arguments.\n";
	}

	bool ParseOptions(int a_argc, char** a_argv, Options& a_options)
	{
		for (int index = 1; index < a_argc; ++index) {
			const std::string_view arg{ a_argv[index] };
			if (arg == "--help" || arg == "-h") {
				PrintUsage(std::cout);
				std::exit(0);
			}
			if (arg == "--root") {
				if (index + 1 >= a_argc) {
					return false;
				}
				a_options.root = a_argv[++index];
				continue;
			}
			if (arg == "--exclude-name") {
				if (index + 1 >= a_argc) {
					return false;
				}
				a_options.excludedNames.insert(a_argv[++index]);
				continue;
			}
			if (arg == "--emit-client-json") {
				if (index + 1 >= a_argc) {
					return false;
				}
				a_options.emitClientJson = a_argv[++index];
				continue;
			}
			return false;
		}
		return !a_options.root.empty();
	}
}

int main(int argc, char** argv)
{
	Options options;
	if (!ParseOptions(argc, argv, options)) {
		std::cerr << "error: expected --root <dir> [--exclude-name <name>]... (see --help)\n";
		return 2;
	}

	std::error_code error;
	if (!std::filesystem::is_directory(options.root, error) || error) {
		std::cerr << "error: --root is not a readable directory: " << options.root << "\n";
		return 1;
	}

	std::vector<Entry> entries;
	std::filesystem::recursive_directory_iterator iterator{
		options.root,
		std::filesystem::directory_options::skip_permission_denied,
		error
	};
	const std::filesystem::recursive_directory_iterator end;
	while (iterator != end) {
		if (error) {
			std::cerr << "error: cannot traverse " << options.root << ": " << error.message() << "\n";
			return 1;
		}
		if (!iterator->is_regular_file(error) || error) {
			iterator.increment(error);
			continue;
		}
		const auto name = iterator->path().filename().string();
		if (options.excludedNames.contains(name)) {
			iterator.increment(error);
			continue;
		}
		auto relative = std::filesystem::relative(iterator->path(), options.root, error);
		if (error) {
			std::cerr << "error: cannot relativize " << iterator->path() << ": " << error.message() << "\n";
			return 1;
		}
		const auto relativePath = relative.generic_string();
		if (relativePath == kPackagedClientManifestPath) {
			iterator.increment(error);
			continue;
		}
		const auto hash = F4T::ContentManifest::Sha256HexFile(iterator->path());
		if (!hash) {
			std::cerr << "error: cannot hash " << iterator->path() << "\n";
			return 1;
		}
		entries.push_back(Entry{ relativePath, *hash });
		iterator.increment(error);
	}

	const auto fingerprint = F4T::ContentManifest::ComputeFingerprint(entries);
	if (!fingerprint) {
		std::cerr << "error: staged content under " << options.root
		          << " produced an invalid or empty manifest\n";
		return 1;
	}
	std::cout << *fingerprint << "\n";

	if (!options.emitClientJson.empty()) {
		const auto manifest = F4T::ClientCompatibility::PackagedClientManifest(*fingerprint);
		if (!manifest) {
			std::cerr << "error: cannot package an invalid fingerprint\n";
			return 1;
		}

		std::error_code error;
		if (options.emitClientJson.has_parent_path()) {
			std::filesystem::create_directories(options.emitClientJson.parent_path(), error);
			if (error) {
				std::cerr << "error: cannot create " << options.emitClientJson.parent_path()
				          << ": " << error.message() << "\n";
				return 1;
			}
		}

		std::ofstream output(options.emitClientJson, std::ios::binary | std::ios::trunc);
		if (!output) {
			std::cerr << "error: cannot open " << options.emitClientJson << " for write\n";
			return 1;
		}
		output << manifest->dump(2) << '\n';
		output.close();
		if (output.fail() || !output.good()) {
			std::cerr << "error: failed to write " << options.emitClientJson << "\n";
			return 1;
		}
	}
	return 0;
}