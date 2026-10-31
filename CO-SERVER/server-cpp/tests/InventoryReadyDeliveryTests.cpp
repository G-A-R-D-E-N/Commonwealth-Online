#include "F4TIdentityProtocol.h"
#include "F4TVoiceWire.h"
#include "IdentityGate.h"
#include "OpenSslTokenService.h"
#include "PersistenceGate.h"
#include "F4TInventoryContract.h"
#include "co_gns_server_bridge.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
	using Json = nlohmann::json;
	using namespace CommonwealthOnline::Server;
	std::vector<Json> sent;
	int sendResult = CO_GNS_SEND_SENT;
	int sendCount = 0;
	int failSendIndex = -1;
	std::uint32_t lastSendBytes{};
	DurableStateFileStore* inspectedStore{};

	std::vector<std::uint8_t> JsonBytes(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	std::optional<std::string> RawPacketType(std::span<const std::uint8_t> a_payload)
	{
		const auto packet = Json::parse(a_payload.begin(), a_payload.end(), nullptr, false);
		return packet.is_object() && packet.contains("type") && packet.at("type").is_string() ?
			std::optional{ packet.at("type").get<std::string>() } : std::nullopt;
	}

	bool SendReliableJson(co_gns_server_handle a_server, std::uint32_t a_connectionId, const Json& a_packet)
	{
		const auto bytes = JsonBytes(a_packet);
		return co_gns_server_send(a_server, a_connectionId, bytes.data(), static_cast<std::uint32_t>(bytes.size()), 1) == CO_GNS_SEND_SENT;
	}

	%:include "../src/ServerFrameDelivery.inc"
	%:include "ComponentDeliveryTests.inc"
	%:include "OwnedInventoryProtocolTests.inc"
	%:include "../src/ServerSessionEnded.inc"

	void TestSessionEndedPacketShape()
	{
		const std::string reason = "Your modpack does not match this server.";
		Json manifest{ { "version", 1 }, { "entries", Json::array() } };
		while (JsonBytes(manifest).size() + 160 <= F4T::Identity::kMaximumAdmissionManifestBytes) {
			manifest["entries"].push_back(Json{
				{ "kind", "native" },
				{ "name", "F4SE/Plugins/mod-" + std::to_string(manifest["entries"].size()) + ".dll" },
				{ "sha256", std::string(64, 'e') }
			});
		}
		assert(JsonBytes(manifest).size() <= F4T::Identity::kMaximumAdmissionManifestBytes);

		sent.clear();
		SendSessionEnded(nullptr, 7, "modpack_mismatch", reason, 1000.0, manifest, std::string_view{}, Json{}, std::string_view{});
		assert(sent.size() == 1);
		assert(lastSendBytes <= 64 * 1024);
		const auto& packet = sent.back();
		assert(packet.at("type") == "sessionEnded");
		assert(packet.at("code") == "modpack_mismatch");
		assert(packet.at("reason") == reason);
		assert(packet.at("serverTime") == 1000.0);
		assert(packet.at("requiredModpack") == manifest);

		SendSessionEnded(nullptr, 7, "banned", reason, 1001.0, Json{}, std::string_view{}, Json{}, std::string_view{});
		assert(sent.size() == 2);
		assert(!sent.back().contains("requiredModpack"));
		const auto fingerprint = std::string(64, 'b');
		const auto chunkInfo = Json{ { "totalBytes", 60000 }, { "chunkCount", 2 }, { "sha256", std::string(64, 'a') } };
		SendSessionEnded(nullptr, 7, "modpack_mismatch", reason, 1002.0, Json{}, fingerprint, chunkInfo, "Main List");
		assert(sent.size() == 3);
		assert(!sent.back().contains("requiredModpack"));
		assert(sent.back().at("requiredModpackFingerprint") == fingerprint);
		assert(sent.back().at("requiredModpackChunks") == chunkInfo);
		assert(sent.back().at("requiredModpackName") == "Main List");
		assert(lastSendBytes <= 64 * 1024);
		sent.clear();
	}
}

extern "C" int co_gns_server_send(co_gns_server_handle, std::uint32_t,
	const void* a_payload, std::uint32_t a_size, std::uint32_t)
{
	const auto* bytes = static_cast<const std::uint8_t*>(a_payload);
	const auto packet = Json::parse(bytes, bytes + a_size, nullptr, false);
	assert(a_size <= 64 * 1024);
	lastSendBytes = a_size;
	if (!packet.is_object()) {
		return sendResult;
	}
	if (packet.at("type") == "sessionReady") {
		const auto& header = packet.at("serverInventory");
		assert(header.size() == 4);
		assert(header.at("initialized") == true);
		assert(header.contains("itemCount") && header.contains("chunkCount"));
		assert(!header.contains("items"));
	} else if (packet.at("type") == "inventoryTransfer") {
		assert(a_size <= F4T::InventoryContract::kMaximumInventoryTransferChunkBytes);
	}
	sent.push_back(packet);
	const auto result = sendCount == failSendIndex ? CO_GNS_SEND_BACKPRESSURE : sendResult;
	++sendCount;
	return result;
}

int main()
{
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto directory = std::filesystem::temp_directory_path() / ("co-inventory-ready-" + std::to_string(stamp));
	const auto path = directory / "state.json";
	const auto backup = std::filesystem::path{ path.string() + ".bak" };
	try {
		DurableStateFileStore store(path, [] { return 1000.0; }, [] { return "0123456789abcdef0123456789abcdef"; });
		inspectedStore = &store;
		OpenSslTokenService tokens;
		IdentityGate identity({}, tokens, &store);
		PersistenceGate persistence(store, Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5 } } }));
		ProtocolHost protocol("Test", "");
		const auto hello = JsonBytes(Json{
			{ "type", "hello" }, { "protocolVersion", 2 }, { "clientVersion", "1.2.0" },
			{ "falloutRuntimeVersion", "1.11.240" }, { "profileId", "profile-001" },
			{ "contentManifestFingerprint", std::string(64, 'a') }, { "capabilities", Json::array({ "save-attestation-v1" }) }
		});
		assert(identity.InspectInbound(7, hello, 1000.0).forward);
		std::vector<OutboundFrame> frames{ { 7, Delivery::ReliableOrdered,
			JsonBytes(Json{ { "type", "sessionReady" }, { "playerId", 1u }, { "profileId", "forged-victim" } }) } };
		std::vector<std::uint32_t> retire;
		std::filesystem::remove(backup);
		std::filesystem::create_directories(backup / "occupied");
		SendFrames(nullptr, protocol, frames, &identity, &persistence, 1.0, 1000.0, &retire);
		assert(sent.empty());
		assert(retire == std::vector<std::uint32_t>{ 7 });
		assert(!store.GetInventory("profile-001") && !store.GetInventory("forged-victim"));
		assert(!persistence.ProfileForConnection(7));
		std::filesystem::remove_all(backup);
		retire.clear();
		sendResult = CO_GNS_SEND_BACKPRESSURE;
		SendFrames(nullptr, protocol, frames, &identity, &persistence, 2.0, 1000.0, &retire);
		assert(sent.size() == 1 && retire == std::vector<std::uint32_t>{ 7 });
		assert(!persistence.ProfileForConnection(7));
		const auto granted = store.GetInventory("profile-001");
		assert(granted && !store.GetInventory("forged-victim"));
		retire.clear();
		sendResult = CO_GNS_SEND_SENT;
		failSendIndex = 2;
		SendFrames(nullptr, protocol, frames, &identity, &persistence, 3.0, 1000.0, &retire);
		assert(sent.size() == 3);
		assert(sent.back().at("type") == "inventoryTransfer");
		assert(retire == std::vector<std::uint32_t>{ 7 });
		assert(!persistence.ProfileForConnection(7));
		retire.clear();
		failSendIndex = -1;
		SendFrames(nullptr, protocol, frames, &identity, &persistence, 4.0, 1000.0, &retire);
		assert(sent.size() == 5 && retire.empty());
		assert(sent[3].at("type") == "sessionReady");
		assert(sent[4].at("type") == "inventoryTransfer");
		assert(sent[4].at("items").size() == 1);
		assert(store.GetInventory("profile-001") == granted);
		assert(persistence.ProfileForConnection(7) == "profile-001");
	} catch (...) {
		std::filesystem::remove_all(directory);
		throw;
	}
	std::filesystem::remove_all(directory);
	TestComponentSendRecovery();
	TestCompatibilitySendRecovery();
	TestOwnedInventoryProtocol();
	TestSessionEndedPacketShape();
}
