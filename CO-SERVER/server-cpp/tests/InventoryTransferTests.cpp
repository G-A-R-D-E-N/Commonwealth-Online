#include "DurableStateFileStore.h"
#include "PersistenceGate.h"

#include "F4TInventoryContract.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using namespace CommonwealthOnline::Server;

	std::vector<std::uint8_t> Encode(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	Json SessionReady(std::uint32_t a_playerId, const std::string& a_profileId)
	{
		return Json{
			{ "type", "sessionReady" },
			{ "playerId", a_playerId },
			{ "profileId", a_profileId },
			{ "resumeToken", "opaque" }
		};
	}

	std::string PluginName(std::size_t a_index, std::size_t a_length)
	{
		std::string name = "plugin" + std::to_string(a_index);
		name.resize(a_length, 'a');
		return name;
	}

	Json Inventory(std::uint32_t a_revision, std::size_t a_count, std::size_t a_pluginLength)
	{
		Json items = Json::object();
		for (std::size_t index = 0; index < a_count; ++index) {
			const auto plugin = PluginName(index, a_pluginLength);
			char form[9]{};
			std::snprintf(form, sizeof(form), "%08X", static_cast<unsigned int>(index + 1));
			items[plugin + ":" + form] = Json{
				{ "plugin", plugin },
				{ "localFormId", form },
				{ "count", 1u + static_cast<std::uint32_t>(index % 65535) }
			};
		}
		return Json{ { "initialized", true }, { "revision", a_revision }, { "items", items } };
	}

	Json Catalog(std::size_t a_count, std::size_t a_pluginLength)
	{
		Json catalog = Json::array();
		const auto inventory = Inventory(0, a_count, a_pluginLength);
		for (const auto& item : inventory.at("items")) catalog.push_back(item);
		return catalog;
	}

	std::optional<Json> Reassemble(const std::vector<OutboundFrame>& a_frames)
	{
		if (a_frames.empty()) return std::nullopt;
		const auto first = Json::parse(a_frames.front().payload.begin(), a_frames.front().payload.end(), nullptr, false);
		if (!first.is_object()) return std::nullopt;
		const Json headerJson{
			{ "initialized", true },
			{ "revision", first.at("revision") },
			{ "itemCount", first.at("itemCount") },
			{ "chunkCount", first.at("chunkCount") }
		};
		const auto header = F4T::InventoryContract::ParseInventoryTransferHeader(headerJson);
		if (!header) return std::nullopt;
		F4T::InventoryContract::InventoryTransferReceiver receiver;
		if (!receiver.Begin(*header)) return std::nullopt;
		for (const auto& frame : a_frames) {
			const auto packet = Json::parse(frame.payload.begin(), frame.payload.end(), nullptr, false);
			if (!packet.is_object()) return std::nullopt;
			Json assembled;
			const auto result = receiver.Accept(packet, assembled);
			if (result == F4T::InventoryContract::InventoryTransferReceiver::Result::Complete) return assembled;
			if (result != F4T::InventoryContract::InventoryTransferReceiver::Result::Accepted) return std::nullopt;
		}
		return std::nullopt;
	}

	void TestStoredInventoryTravelsAsHeaderAndChunks()
	{
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		const auto directory = std::filesystem::temp_directory_path() / ("co-inventory-transfer-" + std::to_string(stamp));
		const auto path = directory / "state.json";
		try {
			DurableStateFileStore store(path, [] { return 1000.0; }, [] { return "0123456789abcdef0123456789abcdef"; });
			F4T::Identity::TokenHash hash{};
			store.SaveResumeCredential("profile-big", hash, std::nullopt, false, 5000.0);
			const auto catalog = Catalog(100, 120);
			PersistenceGate gate(store, catalog);
			OutboundFrame ready{ 7, Delivery::ReliableOrdered, Encode(SessionReady(41, "profile-big")) };
			std::string error;
			assert(gate.PrepareSessionReady(ready, error));
			assert(error.empty());
			const auto readyPacket = Json::parse(ready.payload.begin(), ready.payload.end());
			assert(ready.payload.size() < 4 * 1024);
			assert(readyPacket.contains("serverInventory"));
			const auto& header = readyPacket.at("serverInventory");
			assert(header.size() == 4);
			assert(!header.contains("items"));
			const auto stored = store.GetInventory("profile-big");
			assert(stored.has_value());
			assert(header.at("revision") == stored->at("revision"));
			assert(header.at("itemCount").get<std::size_t>() == stored->at("items").size());
			const auto frames = gate.TakeInventoryTransferFrames(7);
			assert(!frames.empty());
			assert(frames.size() == header.at("chunkCount").get<std::size_t>());
			for (const auto& frame : frames) {
				assert(frame.connectionId == 7);
				assert(frame.payload.size() <= F4T::InventoryContract::kMaximumInventoryTransferChunkBytes);
			}
			const auto assembled = Reassemble(frames);
			assert(assembled.has_value());
			assert(*assembled == *stored);
		} catch (...) {
			std::filesystem::remove_all(directory);
			throw;
		}
		std::filesystem::remove_all(directory);
	}

	void TestSyntheticInventoryOver64KibChunksAndReassembles()
	{
		const auto inventory = Inventory(5, 400, 120);
		assert(inventory.dump().size() > 64 * 1024);
		std::string error;
		const auto chunks = F4T::InventoryContract::BuildInventoryTransferChunks(inventory, error);
		assert(chunks.has_value());
		assert(chunks->size() > 1);
		std::vector<OutboundFrame> frames;
		for (const auto& chunk : *chunks) {
			assert(chunk.dump().size() <= F4T::InventoryContract::kMaximumInventoryTransferChunkBytes);
			assert(chunk.dump().size() <= 64 * 1024);
			const auto text = chunk.dump();
			frames.push_back(OutboundFrame{ 3, Delivery::ReliableOrdered, { text.begin(), text.end() } });
		}
		const auto assembled = Reassemble(frames);
		assert(assembled.has_value());
		assert(*assembled == inventory);
	}

	void TestTransferByteCap()
	{
		const auto inventory = Inventory(1, 1800, 120);
		assert(inventory.dump().size() > F4T::InventoryContract::kMaximumInventoryTransferBytes);
		std::string error;
		assert(!F4T::InventoryContract::BuildInventoryTransferChunks(inventory, error).has_value());
		assert(error.find("total byte limit") != std::string::npos);
		F4T::InventoryContract::InventoryTransferReceiver receiver;
		assert(!receiver.Begin(F4T::InventoryContract::InventoryTransferHeader{
			0, 0, static_cast<std::uint32_t>(F4T::InventoryContract::kMaximumInventoryTransferChunks + 1) }));
	}

	void TestConnectionCloseDropsPendingTransfer()
	{
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		const auto directory = std::filesystem::temp_directory_path() / ("co-inventory-transfer-close-" + std::to_string(stamp));
		const auto path = directory / "state.json";
		try {
			DurableStateFileStore store(path, [] { return 1000.0; }, [] { return "0123456789abcdef0123456789abcdef"; });
			F4T::Identity::TokenHash hash{};
			store.SaveResumeCredential("profile-close", hash, std::nullopt, false, 5000.0);
			PersistenceGate gate(store, Catalog(8, 32));
			OutboundFrame ready{ 9, Delivery::ReliableOrdered, Encode(SessionReady(42, "profile-close")) };
			std::string error;
			assert(gate.PrepareSessionReady(ready, error));
			assert(!gate.TakeInventoryTransferFrames(9).empty());
			OutboundFrame again{ 9, Delivery::ReliableOrdered, Encode(SessionReady(42, "profile-close")) };
			assert(gate.PrepareSessionReady(again, error));
			gate.ConnectionClosed(9);
			assert(gate.TakeInventoryTransferFrames(9).empty());
		} catch (...) {
			std::filesystem::remove_all(directory);
			throw;
		}
		std::filesystem::remove_all(directory);
	}

	void TestInvalidReadinessFrameIsRefused()
	{
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		const auto directory = std::filesystem::temp_directory_path() / ("co-inventory-transfer-invalid-" + std::to_string(stamp));
		const auto path = directory / "state.json";
		try {
			DurableStateFileStore store(path, [] { return 1000.0; }, [] { return "0123456789abcdef0123456789abcdef"; });
			PersistenceGate gate(store, Catalog(1, 16));
			OutboundFrame notReady{ 1, Delivery::ReliableOrdered, Encode(Json{ { "type", "welcome" }, { "playerId", 1u } }) };
			std::string error;
			assert(!gate.PrepareSessionReady(notReady, error));
			assert(!error.empty());
			assert(gate.TakeInventoryTransferFrames(1).empty());
			OutboundFrame tooLarge{ 2, Delivery::ReliableOrdered, Encode(SessionReady(43, "profile-huge")) };
			auto packet = Json::parse(tooLarge.payload.begin(), tooLarge.payload.end());
			packet["metadata"] = std::string(64 * 1024, 'x');
			tooLarge.payload = Encode(packet);
			assert(!gate.PrepareSessionReady(tooLarge, error));
			assert(error.find("readiness limit") != std::string::npos);
		} catch (...) {
			std::filesystem::remove_all(directory);
			throw;
		}
		std::filesystem::remove_all(directory);
	}
}

int main()
{
	TestStoredInventoryTravelsAsHeaderAndChunks();
	TestSyntheticInventoryOver64KibChunksAndReassembles();
	TestTransferByteCap();
	TestConnectionCloseDropsPendingTransfer();
	TestInvalidReadinessFrameIsRefused();
	return EXIT_SUCCESS;
}
