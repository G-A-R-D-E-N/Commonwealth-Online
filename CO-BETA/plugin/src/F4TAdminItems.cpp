#include "pch.h"

#include "F4TAdminItems.h"
#include "F4TAdminItemQueue.h"

#include "F4TConsoleGuard.h"
#include "F4TDebugLogging.h"
#include "F4TNetworking.h"

#include "RE/B/BGSInventoryItem.h"
#include "RE/B/BGSInventoryList.h"
#include "RE/L/LoadingMenu.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESBoundObject.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESFullName.h"
#include "RE/T/TESObjectREFR.h"
#include "RE/U/UI.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace F4T::AdminItems
{
	namespace
	{
		enum class Op : std::uint8_t
		{
			kGive,
			kTake,
			kSpawn,
			kInventory
		};

		struct PendingRequest
		{
			Op            op{};
			std::uint32_t itemId{};
			std::uint32_t count{};
			std::uint32_t opId{};
		};

		constexpr std::uint32_t kMaxGiveTakeCount = 9999;
		constexpr std::uint32_t kMaxSpawnCount = 10;
		constexpr std::size_t kMaxInventoryItems = 512;
		constexpr std::size_t kMaxNameLength = 64;
		constexpr std::size_t kInventoryItemBudget = 60 * 1024;
		constexpr std::size_t kItemFixedBytes = 64;
		constexpr std::size_t kNameEscapeBytes = 6;

		PendingQueue<PendingRequest> g_pendingRequests;

		std::optional<Op> ParseOp(std::string_view a_op)
		{
			if (a_op == "give_item") {
				return Op::kGive;
			}
			if (a_op == "take_item") {
				return Op::kTake;
			}
			if (a_op == "spawn_object") {
				return Op::kSpawn;
			}
			return std::nullopt;
		}

		std::optional<std::uint32_t> ParseItemId(std::string_view a_itemId)
		{
			if (a_itemId.size() != 8) {
				return std::nullopt;
			}

			std::uint32_t value = 0;
			const auto* end = a_itemId.data() + a_itemId.size();
			const auto result = std::from_chars(a_itemId.data(), end, value, 16);
			if (result.ec != std::errc{} || result.ptr != end || value == 0) {
				return std::nullopt;
			}
			return value;
		}

		bool IsValidCount(Op a_op, std::uint32_t a_count)
		{
			const auto maxCount = a_op == Op::kSpawn ? kMaxSpawnCount : kMaxGiveTakeCount;
			return a_count >= 1 && a_count <= maxCount;
		}

		std::size_t PrintableUtf8Length(std::string_view a_text, std::size_t a_index)
		{
			const auto lead = static_cast<unsigned char>(a_text[a_index]);
			if (lead < 0x80) {
				return lead >= 0x20 && lead != 0x7F ? 1 : 0;
			}

			std::size_t length = 0;
			int low = 0x80;
			int high = 0xBF;
			if (lead >= 0xC2 && lead <= 0xDF) {
				length = 2;
			} else if (lead >= 0xE0 && lead <= 0xEF) {
				length = 3;
				low = lead == 0xE0 ? 0xA0 : low;
				high = lead == 0xED ? 0x9F : high;
			} else if (lead >= 0xF0 && lead <= 0xF4) {
				length = 4;
				low = lead == 0xF0 ? 0x90 : low;
				high = lead == 0xF4 ? 0x8F : high;
			} else {
				return 0;
			}

			if (a_index + length > a_text.size()) {
				return 0;
			}
			const auto second = static_cast<unsigned char>(a_text[a_index + 1]);
			if (second < low || second > high) {
				return 0;
			}
			for (std::size_t offset = 2; offset < length; ++offset) {
				const auto next = static_cast<unsigned char>(a_text[a_index + offset]);
				if (next < 0x80 || next > 0xBF) {
					return 0;
				}
			}
			return length;
		}

		std::string SanitizeName(std::string_view a_name)
		{
			std::string out;
			std::size_t index = 0;
			while (index < a_name.size()) {
				const auto length = PrintableUtf8Length(a_name, index);
				const auto piece = length == 0 ? std::string_view{ "?" } : a_name.substr(index, length);
				if (out.size() + piece.size() > kMaxNameLength) {
					break;
				}
				out.append(piece);
				index += length == 0 ? 1 : length;
			}
			return out;
		}

		void ApplyInventoryReport(RE::PlayerCharacter& a_player, std::uint32_t a_opId, std::uint64_t a_connectionGeneration)
		{
			std::vector<F4T::Networking::AdminInventoryEntry> items;
			bool truncated = false;
			std::size_t budget = kInventoryItemBudget;
			if (a_player.inventoryList) {
				for (const auto& entry : a_player.inventoryList->data) {
					const auto count = entry.GetCount();
					if (!entry.object || count == 0) {
						continue;
					}

					auto name = SanitizeName(RE::TESFullName::GetFullName(*entry.object));
					const auto cost = kItemFixedBytes + kNameEscapeBytes * name.size();
					if (items.size() >= kMaxInventoryItems || cost > budget) {
						truncated = true;
						break;
					}

					budget -= cost;
					items.push_back({ entry.object->GetFormID(), std::move(name), count });
				}
			}

			F4T::Networking::SendInventoryReport(a_opId, truncated, items, a_connectionGeneration);
		}

		void ApplyItemOp(RE::PlayerCharacter& a_player, const PendingRequest& a_request, std::uint64_t a_connectionGeneration)
		{
			auto* form = RE::TESForm::GetFormByID(a_request.itemId);
			if (!form) {
				F4T::Networking::SendAdminItemResult(a_request.opId, false, 0, "form not found", a_connectionGeneration);
				return;
			}

			auto* object = form->As<RE::TESBoundObject>();
			if (!object) {
				F4T::Networking::SendAdminItemResult(a_request.opId, false, 0, "form is not a bound object", a_connectionGeneration);
				return;
			}

			switch (a_request.op) {
			case Op::kGive:
				a_player.AddObjectToContainer(
					object,
					RE::BSTSmartPointer<RE::ExtraDataList>{},
					static_cast<std::int32_t>(a_request.count),
					nullptr,
					RE::ITEM_REMOVE_REASON::kNone);
				F4T::Networking::SendAdminItemResult(a_request.opId, true, a_request.count, "", a_connectionGeneration);
				return;
			case Op::kTake: {
				const auto applied = (std::min)(a_request.count, a_player.GetInventoryObjectCount(object));
				if (applied == 0) {
					F4T::Networking::SendAdminItemResult(a_request.opId, false, 0, "not in inventory", a_connectionGeneration);
					return;
				}

				RE::TESObjectREFR::RemoveItemData removal{ object, static_cast<std::int32_t>(applied) };
				a_player.RemoveItem(removal);
				F4T::Networking::SendAdminItemResult(a_request.opId, true, applied, "", a_connectionGeneration);
				return;
			}
			case Op::kSpawn: {
				const auto* ui = RE::UI::GetSingleton();
				if (ui && ui->GetMenuOpen<RE::LoadingMenu>()) {
					F4T::Networking::SendAdminItemResult(a_request.opId, false, 0, "loading menu open", a_connectionGeneration);
					return;
				}

				const auto command = std::format("player.placeatme {:08X} {}", a_request.itemId, a_request.count);
				F4T::ConsoleGuard::ExecutePluginCommand(command.c_str());
				F4T::Networking::SendAdminItemResult(a_request.opId, true, a_request.count, "", a_connectionGeneration);
				return;
			}
			case Op::kInventory:
				return;
			}
		}

		bool IsStillCurrent(std::uint64_t a_connectionGeneration)
		{
			const auto connected = F4T::Networking::IsConnectedToServer();
			const auto currentGeneration = F4T::Networking::GetConnectionGeneration();
			return StillCurrent(a_connectionGeneration, currentGeneration, connected);
		}

		void ApplyRequest(const PendingRequest& a_request, std::uint64_t a_connectionGeneration)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				F4T::Networking::SendAdminItemResult(a_request.opId, false, 0, "player unavailable", a_connectionGeneration);
				return;
			}

			if (a_request.op == Op::kInventory) {
				ApplyInventoryReport(*player, a_request.opId, a_connectionGeneration);
				return;
			}
			ApplyItemOp(*player, a_request, a_connectionGeneration);
		}
	}

	QueueResult QueueItemOp(std::string_view a_op, std::string_view a_itemId, std::uint32_t a_count, std::uint32_t a_opId)
	{
		const auto op = ParseOp(a_op);
		const auto itemId = ParseItemId(a_itemId);
		if (!op || !itemId || !IsValidCount(*op, a_count)) {
			return QueueResult::kInvalid;
		}

		return g_pendingRequests.Enqueue(
			PendingRequest{ *op, *itemId, a_count, a_opId },
			F4T::Networking::GetConnectionGeneration());
	}

	QueueResult QueueInventoryRequest(std::uint32_t a_opId)
	{
		return g_pendingRequests.Enqueue(
			PendingRequest{ Op::kInventory, 0, 0, a_opId },
			F4T::Networking::GetConnectionGeneration());
	}

	void ApplyPendingAdminItems()
	{
		const auto connected = F4T::Networking::IsConnectedToServer();
		const auto connectionGeneration = F4T::Networking::GetConnectionGeneration();
		auto drained = g_pendingRequests.Drain(connectionGeneration, connected);
		if (drained.discarded > 0) {
			F4T::DebugLogging::Log("AdminItems", "dropped {} stale admin request(s)", drained.discarded);
		}

		for (const auto& request : drained.admitted) {
			if (!IsStillCurrent(connectionGeneration)) {
				F4T::DebugLogging::Log("AdminItems", "connection changed mid-batch; dropped remaining admin request(s)");
				break;
			}
			ApplyRequest(request, connectionGeneration);
		}
	}
}
