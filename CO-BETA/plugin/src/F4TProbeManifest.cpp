#include "F4TProbeManifest.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <set>
#include <utility>

namespace
{
	using F4T::ProbeManifest::Document;
	using F4T::ProbeManifest::Entry;
	using Json = nlohmann::json;

	bool Fail(std::string& a_error, std::string a_message)
	{
		a_error = std::move(a_message);
		return false;
	}

	bool ReadBounded(const Json& a_value, std::uint32_t a_min, std::uint32_t a_max, std::uint32_t& a_out)
	{
		if (!a_value.is_number_integer()) {
			return false;
		}
		const auto value = a_value.get<std::int64_t>();
		if (value < static_cast<std::int64_t>(a_min) || value > static_cast<std::int64_t>(a_max)) {
			return false;
		}
		a_out = static_cast<std::uint32_t>(value);
		return true;
	}
}

namespace F4T::ProbeManifest
{
	bool Parse(std::string_view a_text, Document& a_out, std::string& a_error)
	{
		try {
			const auto root = Json::parse(a_text.begin(), a_text.end());
			if (!root.is_object()) {
				return Fail(a_error, "manifest root is not an object");
			}
			if (const auto version = root.find("version"); version != root.end()) {
				std::uint32_t parsed = 0;
				if (!ReadBounded(*version, FormatVersion, FormatVersion, parsed)) {
					return Fail(a_error, "unsupported manifest version");
				}
			}
			const auto probes = root.find("probes");
			if (probes == root.end() || !probes->is_array()) {
				return Fail(a_error, "manifest has no probes array");
			}

			Document document;
			std::set<std::string> names;
			for (const auto& item : *probes) {
				if (!item.is_object()) {
					return Fail(a_error, "probe entry is not an object");
				}
				const auto name = item.find("name");
				if (name == item.end() || !name->is_string()) {
					return Fail(a_error, "probe entry has no name");
				}

				Entry entry;
				entry.name = name->get<std::string>();
				if (entry.name.empty()) {
					return Fail(a_error, "probe entry has an empty name");
				}
				if (!names.insert(entry.name).second) {
					return Fail(a_error, "duplicate probe name '" + entry.name + "'");
				}

				if (const auto enabled = item.find("enabled"); enabled != item.end()) {
					if (!enabled->is_boolean()) {
						return Fail(a_error, "probe '" + entry.name + "' has a non-boolean enabled");
					}
					entry.enabled = enabled->get<bool>();
				}
				if (const auto sampling = item.find("sampling"); sampling != item.end()) {
					if (!ReadBounded(*sampling, 1, MaxSampling, entry.sampling)) {
						return Fail(a_error, "probe '" + entry.name + "' has an out-of-range sampling");
					}
				}
				if (const auto cap = item.find("perProbeCap"); cap != item.end()) {
					if (!ReadBounded(*cap, 1, MaxPerProbeCap, entry.perProbeCap)) {
						return Fail(a_error, "probe '" + entry.name + "' has an out-of-range perProbeCap");
					}
				}
				document.probes.push_back(std::move(entry));
			}

			a_out = std::move(document);
			return true;
		} catch (const std::exception& error) {
			a_error = error.what();
			return false;
		}
	}

	bool Load(const std::filesystem::path& a_path, Document& a_out, std::string& a_error)
	{
		std::ifstream input(a_path, std::ios::binary);
		if (!input) {
			a_error = "cannot open " + a_path.string();
			return false;
		}
		const std::string text{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
		return Parse(text, a_out, a_error);
	}
}
