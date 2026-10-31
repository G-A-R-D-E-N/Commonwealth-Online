#include "ServerProtocol.h"

#include "F4TGnsSnapshotEnvelope.h"
#include "F4TWorldPolicy.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolHost;

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	Json DecodePacket(const OutboundFrame& a_frame)
	{
		F4T::GnsSnapshotEnvelope::Header header{};
		const auto decode = F4T::GnsSnapshotEnvelope::Decode(a_frame.payload, header);
		std::span<const std::uint8_t> payload{ a_frame.payload };
		if (decode == F4T::GnsSnapshotEnvelope::DecodeResult::Valid) {
			payload = payload.subspan(F4T::GnsSnapshotEnvelope::HeaderSize);
		} else {
			assert(decode == F4T::GnsSnapshotEnvelope::DecodeResult::NotEnvelope);
		}
		return Json::parse(std::string{
			reinterpret_cast<const char*>(payload.data()),
			payload.size()
		});
	}

	std::vector<Json> FramesOfType(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		std::vector<Json> packets;
		for (const auto& frame : a_frames) {
			if (frame.connectionId != a_connectionId || frame.payload.empty()) continue;
			auto packet = DecodePacket(frame);
			if (packet.value("type", std::string{}) == a_type) packets.push_back(std::move(packet));
		}
		return packets;
	}

	Json RequirePacket(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		const auto packets = FramesOfType(a_frames, a_connectionId, a_type);
		assert(!packets.empty());
		return packets.front();
	}

	std::vector<OutboundFrame> JoinFramesWithCapabilities(ProtocolHost& a_host, std::uint32_t a_connection, const Json& a_capabilities)
	{
		(void)a_host.HandleConnected(a_connection, 100.0);
		return a_host.HandleMessage(a_connection, EncodeJson(Json{
			{ "type", "hello" },
			{ "protocolVersion", 2u },
			{ "capabilities", a_capabilities } }), 1.0, 100.0);
	}

	std::vector<OutboundFrame> JoinFrames(ProtocolHost& a_host, std::uint32_t a_connection)
	{
		return JoinFramesWithCapabilities(a_host, a_connection, Json::array({ std::string{ F4T::WorldPolicy::kCapabilityName } }));
	}

	void Join(ProtocolHost& a_host, std::uint32_t a_connection)
	{
		assert(!JoinFrames(a_host, a_connection).empty());
	}

	std::filesystem::path SampleDataPath()
	{
		return std::filesystem::path{ CO_WORLD_POLICY_SAMPLE_DIR } / "world-policy.sample.json";
	}

	void TestCapabilityAdvertised()
	{
		ProtocolHost host{ "World Policy Test" };
		const auto welcome = RequirePacket(host.HandleConnected(10, 100.0), 10, "welcome");
		const auto capabilities = welcome.at("capabilities");
		assert(std::find(capabilities.begin(), capabilities.end(), "world-policy-v1") != capabilities.end());
	}

	void TestPolicySentOnJoin()
	{
		ProtocolHost host{ "World Policy Test" };
		const auto frames = JoinFrames(host, 10);
		const auto policy = RequirePacket(frames, 10, "worldPolicy");
		assert(policy.at("mode") == "coop-story");
		assert(policy.at("doors") == "shared");
		assert(policy.at("suppressQuests") == false);
		assert(policy.at("disable").empty());
		assert(policy.at("version") == 0u);
		assert(F4T::WorldPolicy::ParsePolicy(policy).has_value());
	}

	void TestPolicyBroadcastOnChange()
	{
		ProtocolHost host{ "World Policy Test" };
		Join(host, 10);
		Join(host, 20);

		const auto data = F4T::WorldPolicy::LoadData(SampleDataPath());
		assert(data);
		const auto frames = host.SetWorldPolicy(
			F4T::WorldPolicy::DerivePolicy(*data, F4T::WorldPolicy::Mode::FreeRoam, true, F4T::WorldPolicy::Vats::RealTime));
		assert(frames.size() == 2);

		for (const auto connectionId : { 10u, 20u }) {
			const auto policy = RequirePacket(frames, connectionId, "worldPolicy");
			assert(policy.at("mode") == "free-roam");
			assert(policy.at("suppressQuests") == true);
			assert(policy.at("version") == data->version);
			assert(policy.at("disable").size() == 3);
			assert(policy.at("disable").at(0) == "01CA6E:PlaceholderSample.esm");
			assert(policy.at("vats") == "real-time");
			const auto parsed = F4T::WorldPolicy::ParsePolicy(policy);
			assert(parsed && parsed->mode == F4T::WorldPolicy::Mode::FreeRoam);
			assert(parsed && parsed->vats == F4T::WorldPolicy::Vats::RealTime);
		}
		assert(host.CurrentWorldPolicy().mode == F4T::WorldPolicy::Mode::FreeRoam);
		assert(host.CurrentWorldPolicy().disable.size() == 3);

		const auto idle = host.SetWorldPolicy(
			F4T::WorldPolicy::DerivePolicy(*data, F4T::WorldPolicy::Mode::FreeRoam, true, F4T::WorldPolicy::Vats::RealTime));
		assert(idle.size() == 2);
		assert(RequirePacket(idle, 10, "worldPolicy").at("mode") == "free-roam");
	}

	void TestJoinAfterChangeGetsCurrentPolicy()
	{
		ProtocolHost host{ "World Policy Test" };
		const auto data = F4T::WorldPolicy::LoadData(SampleDataPath());
		assert(data);
		(void)host.SetWorldPolicy(F4T::WorldPolicy::DerivePolicy(
			*data, F4T::WorldPolicy::Mode::FreeRoam, false, F4T::WorldPolicy::Vats::Disabled));

		const auto frames = JoinFrames(host, 30);
		const auto policy = RequirePacket(frames, 30, "worldPolicy");
		assert(policy.at("mode") == "free-roam");
		assert(policy.at("doors") == "vanilla");
		assert(policy.at("suppressQuests") == true);
		assert(policy.at("disable").size() == 3);
	}

	void TestInactiveSessionIsNotSent()
	{
		ProtocolHost host{ "World Policy Test" };
		Join(host, 10);
		(void)host.HandleConnected(40, 100.0);
		const auto frames = host.SetWorldPolicy(F4T::WorldPolicy::Policy{});
		assert(frames.size() == 1);
		assert(frames.front().connectionId == 10);
		assert(frames.front().delivery == CommonwealthOnline::Server::Delivery::ReliableOrdered);
	}

	void TestPerModePolicy()
	{
		const auto data = F4T::WorldPolicy::LoadData(SampleDataPath());
		assert(data);
		const auto policy = F4T::WorldPolicy::DerivePolicy(
			*data, F4T::WorldPolicy::Mode::CoopStory, true, F4T::WorldPolicy::Vats::Vanilla);
		assert(policy.disable.empty());
		assert(!policy.suppressQuests);
		assert(policy.doors == F4T::WorldPolicy::Doors::Shared);
		assert(policy.vats == F4T::WorldPolicy::Vats::Vanilla);
		const auto packet = F4T::WorldPolicy::ToJson(policy);
		assert(packet.at("disable").empty());
		assert(packet.at("mode") == std::string{ F4T::WorldPolicy::ModeName(policy.mode) });
		assert(packet.at("mode") == "coop-story");
	}

	void TestSampleDataCannotServeFreeRoam()
	{
		const auto sample = F4T::WorldPolicy::LoadData(SampleDataPath());
		assert(sample && sample->sample);
		assert(F4T::WorldPolicy::CanServeMode(*sample, F4T::WorldPolicy::Mode::CoopStory));
		assert(!F4T::WorldPolicy::CanServeMode(*sample, F4T::WorldPolicy::Mode::FreeRoam));

		const F4T::WorldPolicy::Data empty;
		assert(F4T::WorldPolicy::CanServeMode(empty, F4T::WorldPolicy::Mode::CoopStory));
		assert(!F4T::WorldPolicy::CanServeMode(empty, F4T::WorldPolicy::Mode::FreeRoam));

		auto generated = *sample;
		generated.sample = false;
		assert(F4T::WorldPolicy::CanServeMode(generated, F4T::WorldPolicy::Mode::FreeRoam));

		const auto samplePath = SampleDataPath();
		const auto sampleError = F4T::WorldPolicy::UnservableModeError(samplePath, F4T::WorldPolicy::Mode::FreeRoam);
		assert(!sampleError.empty());
		assert(sampleError.find("free-roam") != std::string::npos);
		assert(sampleError.find(samplePath.string()) != std::string::npos);
		assert(F4T::WorldPolicy::UnservableModeError(samplePath, F4T::WorldPolicy::Mode::CoopStory).empty());

		const auto missingPath = samplePath.parent_path() / "no-such-world-policy.json";
		assert(!F4T::WorldPolicy::UnservableModeError(missingPath, F4T::WorldPolicy::Mode::FreeRoam).empty());
		assert(F4T::WorldPolicy::UnservableModeError(missingPath, F4T::WorldPolicy::Mode::CoopStory).empty());

		const auto generatedPath = std::filesystem::temp_directory_path() / "co-world-policy-generated-test.json";
		{
			std::ofstream output(generatedPath, std::ios::binary | std::ios::trunc);
			output << R"({ "version": 1, "game": "Fallout4.esm", "classes": { "story": ["01CA6E:Fallout4.esm"], "companion": [], "vendor": [], "town": [], "settler": [], "enemy": [], "animal": [] }, "doors": [] })";
		}
		assert(F4T::WorldPolicy::LoadData(generatedPath).has_value());
		assert(F4T::WorldPolicy::UnservableModeError(generatedPath, F4T::WorldPolicy::Mode::FreeRoam).empty());
		assert(F4T::WorldPolicy::UnservableModeError(generatedPath, F4T::WorldPolicy::Mode::CoopStory).empty());
		std::error_code removeError;
		std::filesystem::remove(generatedPath, removeError);

		const auto message = F4T::WorldPolicy::UnservableModeMessage(samplePath.string(), F4T::WorldPolicy::Mode::FreeRoam);
		assert(message.find("needs a generated world policy file with story or companion entries") != std::string::npos);
		assert(message == sampleError);
	}

	void TestPolicyGatedOnCapability()
	{
		ProtocolHost host{ "World Policy Test" };
		const auto legacy = JoinFramesWithCapabilities(host, 10, Json::array());
		assert(FramesOfType(legacy, 10, "worldPolicy").empty());
		assert(FramesOfType(legacy, 10, F4T::WorldPolicy::kPartialMessageType).empty());

		F4T::WorldPolicy::Policy policy;
		policy.mode = F4T::WorldPolicy::Mode::FreeRoam;
		policy.version = 5u;
		assert(host.SetWorldPolicy(policy).empty());

		const auto current = JoinFrames(host, 20);
		const auto delivered = RequirePacket(current, 20, "worldPolicy");
		assert(delivered.at("version") == 5u);
		assert(host.SetWorldPolicy(policy).size() == 1);
	}

	void TestChunkedBroadcast()
	{
		ProtocolHost host{ "World Policy Test" };
		Join(host, 10);
		Join(host, 20);

		F4T::WorldPolicy::Policy policy;
		policy.mode = F4T::WorldPolicy::Mode::FreeRoam;
		policy.version = 9u;
		policy.suppressQuests = true;
		policy.doors = F4T::WorldPolicy::Doors::Vanilla;
		for (std::size_t index = 0; index < F4T::WorldPolicy::kMaximumDisableEntriesPerMessage * 2 + 5; ++index) {
			policy.disable.push_back(F4T::WorldPolicy::FormKey{
				"Very Long Plugin Name For Frame Budget.esp",
				static_cast<std::uint32_t>(index + 1) });
		}

		const auto frames = host.SetWorldPolicy(policy);
		assert(frames.size() == 6);

		F4T::WorldPolicy::Reassembler reassembler;
		std::optional<F4T::WorldPolicy::Policy> received;
		for (const auto& frame : frames) {
			if (frame.connectionId != 10) continue;
			assert(frame.payload.size() < 64u * 1024u);
			const auto packet = DecodePacket(frame);
			assert(packet.at("type") == std::string{ F4T::WorldPolicy::kPartialMessageType });
			const auto applied = reassembler.Apply(packet);
			if (applied) received = applied;
		}
		assert(received && *received == policy);

		const auto joinFrames = JoinFrames(host, 30);
		F4T::WorldPolicy::Reassembler joinReassembler;
		std::optional<F4T::WorldPolicy::Policy> joined;
		for (const auto& packet : FramesOfType(joinFrames, 30, F4T::WorldPolicy::kPartialMessageType)) {
			if (const auto applied = joinReassembler.Apply(packet)) joined = applied;
		}
		assert(joined && *joined == policy);
	}
}

int main()
{
	TestCapabilityAdvertised();
	TestPolicySentOnJoin();
	TestPolicyBroadcastOnChange();
	TestJoinAfterChangeGetsCurrentPolicy();
	TestInactiveSessionIsNotSent();
	TestPerModePolicy();
	TestSampleDataCannotServeFreeRoam();
	TestPolicyGatedOnCapability();
	TestChunkedBroadcast();
	return 0;
}
