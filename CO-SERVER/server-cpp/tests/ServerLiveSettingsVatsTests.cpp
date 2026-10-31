#include "ServerLiveSettings.h"

#include "DirectoryRegistrar.h"
#include "LanDiscoveryService.h"
#include "ServerConfiguration.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace CommonwealthOnline::Server::RuntimeSupport
{
	double UnixTimeSeconds()
	{
		return 0.0;
	}
}

namespace CommonwealthOnline::Server
{
	DirectoryHttpSender CreatePlatformDirectorySender()
	{
		return {};
	}
}

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::Configuration::Options;

	Json BaseDocument(std::string a_vats)
	{
		return Json{
			{ "host", "127.0.0.1" },
			{ "server_name", "VATS Test" },
			{ "max_players", 2 },
			{ "enable_persistence", false },
			{ "vats", std::move(a_vats) },
		};
	}

	bool AppliedContains(const Json& a_result, const std::string& a_field)
	{
		const auto applied = a_result.find("applied");
		if (applied == a_result.end() || !applied->is_array()) {
			return false;
		}
		for (const auto& field : *applied) {
			if (field.is_string() && field.get<std::string>() == a_field) {
				return true;
			}
		}
		return false;
	}
}

int main()
{
	using namespace CommonwealthOnline::Server;

	Options options = Configuration::Parse(BaseDocument("disabled"), {});
	ProtocolHost protocol{ "VATS Test", "live apply" };
	std::atomic_size_t activePlayers{ 0 };
	F4T::Admission::AdmissionRegistry sessionAdmissions{ 16 };
	RuntimeSupport::AdmissionContext admission;
	LanDiscoveryService discovery{ "VATS Test", "live apply", 7777, 2, activePlayers, false };
	std::unique_ptr<DirectoryRegistrar> directory;
	F4T::WorldPolicy::Data worldPolicyData;
	F4T::WorldPolicy::Mode worldMode{ F4T::WorldPolicy::Mode::CoopStory };
	const std::filesystem::path baseDirectory{ "/tmp/co-vats-test" };
	const std::filesystem::path worldPolicyPath{ "/tmp/co-vats-test/world-policy.json" };
	std::vector<std::vector<OutboundFrame>> sentFrames;
	std::vector<Json> broadcastPackets;

	ServerLiveSettingsContext context{
		.options = &options,
		.protocol = &protocol,
		.persistence = nullptr,
		.identityGate = nullptr,
		.activePlayers = &activePlayers,
		.sessionAdmissions = &sessionAdmissions,
		.admission = &admission,
		.discovery = &discovery,
		.directory = &directory,
		.worldPolicyData = &worldPolicyData,
		.worldMode = &worldMode,
		.baseDirectory = &baseDirectory,
		.worldPolicyPath = &worldPolicyPath,
		.localPort = 7777,
		.sendFrames = [&](std::vector<OutboundFrame> a_frames) { sentFrames.push_back(std::move(a_frames)); },
		.broadcastPacket = [&](const Json& a_packet) { broadcastPackets.push_back(a_packet); },
	};

	const auto first = ApplyServerLiveSettings(context, BaseDocument("real-time"));
	assert(first.ok);
	assert(AppliedContains(first.data, "vats"));
	assert(options.vats == "real-time");
	assert(protocol.CurrentWorldPolicy().vats == F4T::WorldPolicy::Vats::RealTime);
	assert(!broadcastPackets.empty());

	const auto repeated = ApplyServerLiveSettings(context, BaseDocument("real-time"));
	assert(repeated.ok);
	assert(repeated.data.at("applied").empty());
	assert(protocol.CurrentWorldPolicy().vats == F4T::WorldPolicy::Vats::RealTime);

	const auto back = ApplyServerLiveSettings(context, BaseDocument("disabled"));
	assert(back.ok);
	assert(AppliedContains(back.data, "vats"));
	assert(options.vats == "disabled");
	assert(protocol.CurrentWorldPolicy().vats == F4T::WorldPolicy::Vats::Disabled);

	const auto invalid = ApplyServerLiveSettings(context, BaseDocument("slow-mo"));
	assert(!invalid.ok);
	assert(options.vats == "disabled");
	assert(protocol.CurrentWorldPolicy().vats == F4T::WorldPolicy::Vats::Disabled);

	return 0;
}
