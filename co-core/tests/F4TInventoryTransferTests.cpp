#include "F4TInventoryContract.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using namespace F4T::InventoryContract;

	std::string PluginName(std::size_t a_index, std::size_t a_length)
	{
		std::string name = "plugin" + std::to_string(a_index);
		name.resize(a_length, 'a');
		return name;
	}

	Json MakeInventory(std::uint32_t a_revision, std::size_t a_count, std::size_t a_pluginLength)
	{
		Json items = Json::object();
		for (std::size_t index = 0; index < a_count; ++index) {
			const auto plugin = PluginName(index, a_pluginLength);
			char form[9]{};
			std::snprintf(form, sizeof(form), "%08X", static_cast<unsigned int>(index + 1));
			const auto key = plugin + ":" + form;
			items[key] = Json{
				{ "plugin", plugin },
				{ "localFormId", form },
				{ "count", 1u + static_cast<std::uint32_t>(index % 65535) }
			};
		}
		return Json{ { "initialized", true }, { "revision", a_revision }, { "items", items } };
	}

	Json HeaderJson(const Json& a_chunk)
	{
		return Json{
			{ "initialized", true },
			{ "revision", a_chunk.at("revision") },
			{ "itemCount", a_chunk.at("itemCount") },
			{ "chunkCount", a_chunk.at("chunkCount") }
		};
	}

	std::optional<Json> Reassemble(const std::vector<Json>& a_chunks)
	{
		const auto header = ParseInventoryTransferHeader(HeaderJson(a_chunks.front()));
		assert(header.has_value());
		InventoryTransferReceiver receiver;
		assert(receiver.Begin(*header));
		for (const auto& chunk : a_chunks) {
			Json inventory;
			const auto result = receiver.Accept(chunk, inventory);
			if (result == InventoryTransferReceiver::Result::Complete) return inventory;
			assert(result == InventoryTransferReceiver::Result::Accepted);
		}
		return std::nullopt;
	}

	void TestRoundTripSingleChunk()
	{
		const auto inventory = MakeInventory(7, 3, 16);
		std::string error;
		const auto chunks = BuildInventoryTransferChunks(inventory, error);
		assert(chunks.has_value());
		assert(chunks->size() == 1);
		assert((*chunks)[0].at("chunkCount") == 1u);
		assert((*chunks)[0].at("chunkIndex") == 0u);
		assert((*chunks)[0].at("lastChunk") == true);
		assert((*chunks)[0].at("itemCount") == 3u);
		assert((*chunks)[0].at("revision") == 7u);
		const auto rebuilt = Reassemble(*chunks);
		assert(rebuilt.has_value());
		assert(*rebuilt == inventory);
		assert(error.empty());
	}

	void TestEmptyInventoryStillTransfers()
	{
		const auto inventory = MakeInventory(0, 0, 16);
		std::string error;
		const auto chunks = BuildInventoryTransferChunks(inventory, error);
		assert(chunks.has_value());
		assert(chunks->size() == 1);
		assert((*chunks)[0].at("itemCount") == 0u);
		assert((*chunks)[0].at("lastChunk") == true);
		const auto rebuilt = Reassemble(*chunks);
		assert(rebuilt.has_value());
		assert(*rebuilt == inventory);
	}

	void TestChunkBoundariesAndOver64Kib()
	{
		const auto inventory = MakeInventory(9, 400, 120);
		assert(inventory.dump().size() > 64 * 1024);
		std::string error;
		const auto chunks = BuildInventoryTransferChunks(inventory, error);
		assert(chunks.has_value());
		assert(chunks->size() > 1);
		assert(chunks->size() <= kMaximumInventoryTransferChunks);
		std::size_t items = 0;
		std::uint32_t expectedIndex = 0;
		for (const auto& chunk : *chunks) {
			assert(chunk.dump().size() <= kMaximumInventoryTransferChunkBytes);
			assert(chunk.at("chunkIndex") == expectedIndex);
			assert(chunk.at("chunkCount") == static_cast<std::uint32_t>(chunks->size()));
			assert(chunk.at("lastChunk").get<bool>() == (expectedIndex + 1 == chunks->size()));
			assert(!chunk.at("items").empty());
			items += chunk.at("items").size();
			++expectedIndex;
		}
		assert(items == 400);
		const auto rebuilt = Reassemble(*chunks);
		assert(rebuilt.has_value());
		assert(*rebuilt == inventory);
	}

	void TestTotalByteCap()
	{
		const auto inventory = MakeInventory(1, 1800, 120);
		assert(inventory.dump().size() > kMaximumInventoryTransferBytes);
		std::string error;
		const auto chunks = BuildInventoryTransferChunks(inventory, error);
		assert(!chunks.has_value());
		assert(error.find("total byte limit") != std::string::npos);
	}

	void TestReceiverRejectsOutOfOrder()
	{
		const auto inventory = MakeInventory(3, 400, 120);
		std::string error;
		const auto chunks = BuildInventoryTransferChunks(inventory, error);
		assert(chunks.has_value() && chunks->size() > 1);
		const auto header = ParseInventoryTransferHeader(HeaderJson(chunks->front()));
		assert(header.has_value());
		InventoryTransferReceiver receiver;
		assert(receiver.Begin(*header));
		Json assembled;
		assert(receiver.Accept((*chunks)[1], assembled) == InventoryTransferReceiver::Result::Rejected);
		assert(!receiver.Active());
		Json forwarded;
		assert(receiver.Accept((*chunks)[0], forwarded) == InventoryTransferReceiver::Result::Rejected);
	}

	void TestReceiverRejectsMismatchAndDuplicates()
	{
		const auto inventory = MakeInventory(4, 3, 16);
		std::string error;
		const auto chunks = BuildInventoryTransferChunks(inventory, error);
		assert(chunks.has_value() && chunks->size() == 1);
		auto& chunk = (*chunks)[0];
		const auto header = ParseInventoryTransferHeader(HeaderJson(chunk));
		assert(header.has_value());
		{
			InventoryTransferReceiver receiver;
			assert(receiver.Begin(*header));
			auto wrongRevision = chunk;
			wrongRevision["revision"] = 5u;
			Json assembled;
			assert(receiver.Accept(wrongRevision, assembled) == InventoryTransferReceiver::Result::Rejected);
		}
		{
			InventoryTransferReceiver receiver;
			assert(receiver.Begin(*header));
			auto duplicate = chunk;
			auto item = duplicate.at("items").begin();
			duplicate["items"][item.key()] = item.value();
			duplicate["itemCount"] = 4u;
			Json assembled;
			assert(receiver.Accept(duplicate, assembled) == InventoryTransferReceiver::Result::Rejected);
		}
		{
			InventoryTransferReceiver receiver;
			assert(receiver.Begin(*header));
			auto wrongCount = chunk;
			wrongCount["itemCount"] = 4u;
			Json assembled;
			assert(receiver.Accept(wrongCount, assembled) == InventoryTransferReceiver::Result::Rejected);
		}
	}

	void TestHeaderBounds()
	{
		const auto inventory = MakeInventory(2, 2, 16);
		assert(!ParseInventoryTransferHeader(inventory).has_value());
		const auto validChunk = Json{
			{ "initialized", true }, { "revision", 2u }, { "itemCount", 2u }, { "chunkCount", 1u }
		};
		const auto header = ParseInventoryTransferHeader(validChunk);
		assert(header.has_value());
		assert(header->revision == 2 && header->itemCount == 2 && header->chunkCount == 1);
		auto zeroChunks = validChunk;
		zeroChunks["chunkCount"] = 0u;
		assert(!ParseInventoryTransferHeader(zeroChunks).has_value());
		auto tooManyChunks = validChunk;
		tooManyChunks["chunkCount"] = static_cast<std::uint32_t>(kMaximumInventoryTransferChunks + 1);
		assert(!ParseInventoryTransferHeader(tooManyChunks).has_value());
		InventoryTransferReceiver receiver;
		assert(!receiver.Begin(InventoryTransferHeader{ 0, 0, static_cast<std::uint32_t>(kMaximumInventoryTransferChunks + 1) }));
		assert(!receiver.Active());
	}

	void TestRejectsInvalidItem()
	{
		auto inventory = MakeInventory(1, 2, 16);
		const std::string invalidKey = "not-a-key";
		inventory["items"][invalidKey] = Json{ { "plugin", "not-a-key" }, { "localFormId", "00000001" }, { "count", 1u } };
		std::string error;
		assert(!BuildInventoryTransferChunks(inventory, error).has_value());
		assert(error.find("is invalid") != std::string::npos);
	}
}

int main()
{
	TestRoundTripSingleChunk();
	TestEmptyInventoryStillTransfers();
	TestChunkBoundariesAndOver64Kib();
	TestTotalByteCap();
	TestReceiverRejectsOutOfOrder();
	TestReceiverRejectsMismatchAndDuplicates();
	TestHeaderBounds();
	TestRejectsInvalidItem();
	return EXIT_SUCCESS;
}
