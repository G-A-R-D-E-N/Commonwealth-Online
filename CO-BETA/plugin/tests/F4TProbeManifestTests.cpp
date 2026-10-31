#include "F4TProbeManifest.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace
{
	using F4T::ProbeManifest::DefaultPerProbeCap;
	using F4T::ProbeManifest::DefaultSampling;
	using F4T::ProbeManifest::Document;
	using F4T::ProbeManifest::Load;
	using F4T::ProbeManifest::Parse;

	void ExpectRejected(std::string_view a_text)
	{
		Document document;
		std::string error;
		assert(!Parse(a_text, document, error));
		assert(!error.empty());
	}

	void TestValidDocument()
	{
		constexpr std::string_view text = R"({
			"version": 1,
			"probes": [
				{ "name": "selftest", "enabled": true, "sampling": 2, "perProbeCap": 10 },
				{ "name": "other" }
			]
		})";

		Document document;
		std::string error;
		assert(Parse(text, document, error));
		assert(error.empty());
		assert(document.probes.size() == 2);

		const auto& first = document.probes[0];
		assert(first.name == "selftest");
		assert(first.enabled);
		assert(first.sampling == 2);
		assert(first.perProbeCap == 10);

		const auto& second = document.probes[1];
		assert(second.name == "other");
		assert(!second.enabled);
		assert(second.sampling == DefaultSampling);
		assert(second.perProbeCap == DefaultPerProbeCap);
	}

	void TestRejectedDocuments()
	{
		ExpectRejected("");
		ExpectRejected("[]");
		ExpectRejected("{}");
		ExpectRejected(R"({"version": 2, "probes": []})");
		ExpectRejected(R"({"probes": {}})");
		ExpectRejected(R"({"probes": [1]})");
		ExpectRejected(R"({"probes": [{}]})");
		ExpectRejected(R"({"probes": [{"name": ""}]})");
		ExpectRejected(R"({"probes": [{"name": "a"}, {"name": "a"}]})");
		ExpectRejected(R"({"probes": [{"name": "a", "enabled": 1}]})");
		ExpectRejected(R"({"probes": [{"name": "a", "sampling": 0}]})");
		ExpectRejected(R"({"probes": [{"name": "a", "sampling": "2"}]})");
		ExpectRejected(R"({"probes": [{"name": "a", "perProbeCap": 0}]})");
	}

	void TestLoad()
	{
		const auto directory = std::filesystem::temp_directory_path();
		const auto path = directory / "co-probe-manifest-tests.json";
		{
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			assert(output.is_open());
			output << R"({"probes": [{"name": "selftest", "enabled": true}]})";
		}

		Document document;
		std::string error;
		assert(Load(path, document, error));
		assert(document.probes.size() == 1);
		assert(document.probes[0].enabled);

		std::filesystem::remove(path);

		Document missing;
		std::string missingError;
		assert(!Load(path, missing, missingError));
		assert(!missingError.empty());
	}
}

int main()
{
	TestValidDocument();
	TestRejectedDocuments();
	TestLoad();
	return 0;
}
