#include "F4TServerModpackCache.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <format>
#include <string>

namespace
{
	std::filesystem::path MakeScratchDirectory()
	{
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		return std::filesystem::temp_directory_path() / std::format("co-modpack-cache-tests-{}", stamp);
	}

	constexpr std::string_view kServerKey = "127.0.0.1:7777";
	constexpr std::string_view kBlob = R"({"version":1,"fingerprint":"ed7002b439e9ac845f22357d822bac1444730fbdb6016d3ec9432297b9ec9f73","files":[],"plugins":[]})";
}

int main()
{
	using namespace F4T::ServerModpackCache;

	const auto scratch = MakeScratchDirectory();
	std::filesystem::create_directories(scratch);
	const auto cachePath = scratch / "cache" / "modpack-cache.json";

	SetCachePathForTesting(cachePath);
	assert(!Lookup(std::string{ kServerKey }).has_value());
	assert(Store(std::string{ kServerKey }, std::string{ kBlob }));
	assert(Lookup(std::string{ kServerKey }) == std::string{ kBlob });
	assert(std::filesystem::exists(cachePath));

	SetCachePathForTesting(cachePath);
	assert(Lookup(std::string{ kServerKey }) == std::string{ kBlob });

	assert(!Store("", std::string{ kBlob }));
	assert(!Store(std::string{ kServerKey }, "not json"));
	assert(!Store(std::string{ kServerKey }, std::string(2 * 1024 * 1024 + 1, 'x')));
	assert(!Store("bad\x01key", std::string{ kBlob }));

	{
		std::ofstream corrupt(cachePath, std::ios::binary | std::ios::trunc);
		corrupt << "{";
	}
	SetCachePathForTesting(cachePath);
	assert(!Lookup(std::string{ kServerKey }).has_value());
	assert(Store(std::string{ kServerKey }, std::string{ kBlob }));
	assert(Lookup(std::string{ kServerKey }) == std::string{ kBlob });

	assert(Clear(std::string{ kServerKey }));
	assert(!Lookup(std::string{ kServerKey }).has_value());
	assert(Clear(std::string{ kServerKey }));
	assert(!Clear(""));

	SetCachePathForTesting(cachePath);
	assert(!Lookup(std::string{ kServerKey }).has_value());

	for (std::size_t index = 0; index < 64; ++index) {
		assert(Store(std::format("10.0.0.{}:7777", index), std::string{ kBlob }));
	}
	assert(Store("10.0.0.64:7777", std::string{ kBlob }));
	assert(Lookup("10.0.0.64:7777") == std::string{ kBlob });
	std::size_t cached = 0;
	for (std::size_t index = 0; index <= 64; ++index) {
		if (Lookup(std::format("10.0.0.{}:7777", index))) {
			++cached;
		}
	}
	assert(cached == 64);
	assert(Store("10.0.0.65:7777", std::string{ kBlob }));
	assert(!Lookup("10.0.0.1:7777"));
	assert(Lookup("10.0.0.2:7777") == std::string{ kBlob });

	std::error_code error;
	std::filesystem::remove_all(scratch, error);
	return 0;
}
