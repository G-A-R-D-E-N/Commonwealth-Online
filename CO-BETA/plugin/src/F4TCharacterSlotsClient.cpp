#include "pch.h"

#include "F4TCharacterSlotsClient.h"

#include "F4TCharacterSlotProtocol.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TSecretWipe.h"
#include "F4TUIClient.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace F4T::CharacterSlotsClient
{
	using namespace F4T::Networking::Detail;
	using Json = nlohmann::json;

	namespace
	{
		constexpr std::size_t kMaximumActionJsonBytes = 1024;
		constexpr std::size_t kMaximumSlotNameBytes = 128;
		constexpr std::size_t kMaximumNoticeBytes = 512;
		constexpr std::size_t kMaximumTokenBytes = 128;
		constexpr std::size_t kMaximumStoredSlots = 16;
		constexpr std::size_t kStateOutputBudgetBytes = 4 * 1024;
		constexpr std::uint32_t kMaximumSaneSlot = 255;
		constexpr double kSendTokensPerSecond = 3.0;
		constexpr double kSendBurstTokens = 4.0;

		struct SlotEntry
		{
			std::uint32_t slot{};
			std::string name;
			double createdAt{};
			double lastPlayedAt{};
			bool hasCreatedAt{};
			bool hasLastPlayedAt{};
			bool empty{};
			bool deleted{};
			std::optional<double> deletedAt;
			std::optional<double> restorableUntil;
		};

		struct PendingDelete
		{
			std::uint32_t slot{};
			std::string token;
			std::chrono::steady_clock::time_point receivedAt{};
			std::uint32_t expiresInSeconds{};
		};

		struct LastResult
		{
			std::string op;
			bool ok{};
			std::string reason;
			std::uint32_t slot{};
			bool hasSlot{};
		};

		struct DeferredLaunch
		{
			std::uint64_t sequence{};
			F4T::SessionLaunch::JoinRequest request;
			std::function<void()> resume;
			std::function<void(bool, std::string_view)> cancel;
		};

		std::mutex g_mutex;
		bool g_supported{};
		bool g_legacy{};
		std::string g_notice;
		std::vector<SlotEntry> g_slots;
		std::optional<std::uint32_t> g_selectedSlot;
		bool g_selectionDone{};
		std::optional<PendingDelete> g_pendingDelete;
		std::optional<LastResult> g_lastResult;
		std::optional<DeferredLaunch> g_deferredLaunch;
		std::uint64_t g_launchSequence{};
		bool g_reconnecting{};
		std::thread* g_reconnectWorker = nullptr;
		double g_sendTokens = kSendBurstTokens;
		std::chrono::steady_clock::time_point g_lastSendRefill{};

		bool IsPrintableToken(std::string_view a_value) noexcept
		{
			if (a_value.empty() || a_value.size() > kMaximumTokenBytes) {
				return false;
			}
			for (const char ch : a_value) {
				const auto byte = static_cast<unsigned char>(ch);
				if (byte <= 0x20 || byte == 0x7F) {
					return false;
				}
			}
			return true;
		}

		std::optional<std::uint32_t> ReadSlotNumber(const Json& a_packet)
		{
			const auto found = a_packet.find("slot");
			if (found == a_packet.end() || !found->is_number_unsigned()) {
				return std::nullopt;
			}
			const auto slot = found->get<std::uint32_t>();
			return slot <= kMaximumSaneSlot ? std::optional<std::uint32_t>{ slot } : std::nullopt;
		}

		std::optional<double> ReadTimestamp(const Json& a_entry, const char* a_key)
		{
			const auto found = a_entry.find(a_key);
			if (found == a_entry.end() || !found->is_number()) {
				return std::nullopt;
			}
			return found->get<double>();
		}

		bool ReadSlotEntry(const Json& a_entry, SlotEntry& a_out)
		{
			if (!a_entry.is_object()) {
				return false;
			}
			const auto slot = ReadSlotNumber(a_entry);
			if (!slot) {
				return false;
			}
			a_out.slot = *slot;
			a_out.empty = a_entry.value("empty", false);
			a_out.deleted = a_entry.value("deleted", false);
			if (a_out.empty) {
				a_out.name.clear();
				if (a_out.deleted) {
					a_out.deletedAt = ReadTimestamp(a_entry, "deletedAt");
					a_out.restorableUntil = ReadTimestamp(a_entry, "restorableUntil");
				}
				return true;
			}
			const auto name = a_entry.find("name");
			if (name == a_entry.end() || !name->is_string() ||
				name->get_ref<const std::string&>().size() > kMaximumSlotNameBytes) {
				return false;
			}
			a_out.name = name->get<std::string>();
			if (const auto created = a_entry.find("createdAt"); created != a_entry.end() && created->is_number()) {
				a_out.createdAt = created->get<double>();
				a_out.hasCreatedAt = true;
			}
			if (const auto played = a_entry.find("lastPlayedAt"); played != a_entry.end() && played->is_number()) {
				a_out.lastPlayedAt = played->get<double>();
				a_out.hasLastPlayedAt = true;
			}
			return true;
		}

		void QueueGameTask(std::function<void()> a_task)
		{
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				LogWarningWithLocalPlayerPrefix("Cannot queue character slot work: task interface unavailable.");
				return;
			}
			taskInterface->AddTask(std::move(a_task));
		}

		void QueueStateChanged()
		{
			QueueGameTask([]() { F4T::UIClient::NotifyFactionStateChanged(); });
		}

		bool PaceSend()
		{
			const auto now = std::chrono::steady_clock::now();
			std::scoped_lock lock(g_mutex);
			const double elapsed = std::chrono::duration<double>(now - g_lastSendRefill).count();
			g_sendTokens = (std::min)(kSendBurstTokens, g_sendTokens + elapsed * kSendTokensPerSecond);
			g_lastSendRefill = now;
			if (g_sendTokens < 1.0) {
				return false;
			}
			g_sendTokens -= 1.0;
			return true;
		}

		void QueuePacket(Json a_packet)
		{
			QueueGameTask([packet = std::move(a_packet)]() {
				if (!F4T::Networking::IsConnectedToServer()) {
					LogThrottledWarning("slot_send_offline", "Dropped a character slot packet: not connected to a server.");
					return;
				}
				if (!SendPacketBytes(packet.dump())) {
					LogThrottledWarning("slot_send_failed", "Could not send Commonwealth Online character slot packet.");
				}
			});
		}

		void SendSlotPacket(Json a_packet, bool a_clearPendingDelete)
		{
			{
				std::scoped_lock lock(g_mutex);
				if (a_clearPendingDelete) {
					g_pendingDelete.reset();
				}
			}
			QueueStateChanged();
			if (!PaceSend()) {
				LogThrottledWarning("slot_send_paced", "Dropping a character slot packet under the client pace limit.");
				return;
			}
			QueuePacket(std::move(a_packet));
		}

		void StoreResult(std::string a_op, bool a_ok, const std::string& a_reason, std::optional<std::uint32_t> a_slot)
		{
			std::scoped_lock lock(g_mutex);
			LastResult result{ std::move(a_op), a_ok, a_ok ? std::string{} : a_reason, a_slot.value_or(0), a_slot.has_value() };
			g_lastResult = std::move(result);
		}

		void HandleListState(const Json& a_packet)
		{
			const auto slots = a_packet.find("slots");
			if (slots == a_packet.end() || !slots->is_array() || slots->size() > kMaximumStoredSlots) {
				LogThrottledWarning("slot_state_malformed", "Ignoring malformed characterListState packet.");
				return;
			}
			std::vector<SlotEntry> parsed;
			for (const auto& entry : *slots) {
				SlotEntry record;
				if (!ReadSlotEntry(entry, record)) {
					LogThrottledWarning("slot_state_entry", "Ignoring malformed characterListState entry.");
					return;
				}
				parsed.push_back(std::move(record));
			}
			{
				std::scoped_lock lock(g_mutex);
				g_slots = std::move(parsed);
			}
			QueueStateChanged();
		}

		void WipeServerPassword(std::optional<std::string>& a_password)
		{
			if (a_password) {
				F4T::Secret::Wipe(*a_password);
			}
		}

		void ResumeDeferredLaunch()
		{
			std::optional<DeferredLaunch> deferred;
			{
				std::scoped_lock lock(g_mutex);
				deferred = std::move(g_deferredLaunch);
				g_deferredLaunch.reset();
			}
			if (!deferred) {
				return;
			}
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				LogWarningWithLocalPlayerPrefix("Cannot resume deferred session launch: task interface unavailable.");
				std::scoped_lock lock(g_mutex);
				g_selectionDone = false;
				g_selectedSlot.reset();
				if (!g_deferredLaunch) {
					g_deferredLaunch = std::move(deferred);
				}
				return;
			}
			taskInterface->AddTask([resume = std::move(deferred->resume)]() { resume(); });
			WipeServerPassword(deferred->request.serverPassword);
		}

		void FinishSelectionReconnect(std::uint64_t a_sequence, std::uint64_t a_generation, bool a_connected, const std::string& a_error)
		{
			bool owned = false;
			{
				std::scoped_lock lock(g_mutex);
				g_reconnecting = false;
				owned = g_deferredLaunch && g_deferredLaunch->sequence == a_sequence;
			}
			QueueStateChanged();
			const bool superseded = F4T::Networking::GetConnectionGeneration() != a_generation;
			if (!owned) {
				if (a_connected && !superseded) {
					F4T::Networking::DisconnectFromLocalServer();
				}
				return;
			}
			if (!a_connected || superseded) {
				const std::string reason = a_error.empty() ? "Could not reconnect to the server to finish character selection." : a_error;
				CancelDeferredLaunch(reason);
				return;
			}
			if (NeedsSelection()) {
				CancelDeferredLaunch("Character selection did not take effect. Select a character again.");
				return;
			}
			ResumeDeferredLaunch();
		}

		void StartSelectionReconnect()
		{
			F4T::SessionLaunch::JoinRequest request;
			std::uint64_t sequence{};
			{
				std::scoped_lock lock(g_mutex);
				if (!g_deferredLaunch || g_reconnecting) {
					return;
				}
				request = g_deferredLaunch->request;
				sequence = g_deferredLaunch->sequence;
				g_reconnecting = true;
			}
			if (g_reconnectWorker) {
				g_reconnectWorker->join();
				delete g_reconnectWorker;
				g_reconnectWorker = nullptr;
			}
			try {
				g_reconnectWorker = new std::thread([request = std::move(request), sequence]() mutable {
					F4T::Networking::DisconnectFromLocalServer();
					std::string error;
					const bool connected = F4T::Networking::ConnectAndValidateServer(
						request.host.c_str(), request.port, error, nullptr, request.serverPassword, request.id);
					WipeServerPassword(request.serverPassword);
					const auto generation = F4T::Networking::GetConnectionGeneration();
					QueueGameTask([sequence, generation, connected, error = std::move(error)]() {
						FinishSelectionReconnect(sequence, generation, connected, error);
					});
				});
			} catch (const std::exception&) {
				QueueGameTask([sequence]() {
					FinishSelectionReconnect(sequence, F4T::Networking::GetConnectionGeneration(), false, "Could not start the character selection reconnect.");
				});
			}
		}

		void HandleResult(const Json& a_packet)
		{
			const auto op = ReadString(a_packet, "op");
			const auto ok = a_packet.find("ok");
			if (!op || ok == a_packet.end() || !ok->is_boolean()) {
				LogThrottledWarning("slot_result_malformed", "Ignoring malformed characterResult packet.");
				return;
			}
			const auto slot = ReadSlotNumber(a_packet);
			const auto reason = ReadString(a_packet, "reason").value_or("");
			const bool succeeded = ok->get<bool>();
			StoreResult(std::string{ *op }, succeeded, reason, slot);
			if (*op == "delete") {
				std::scoped_lock lock(g_mutex);
				g_pendingDelete.reset();
			}
			if (!succeeded && *op == "hello") {
				const std::string message = reason.empty() ? std::string{ "Character selection was refused." } : "Character selection refused: " + reason + ".";
				QueueGameTask([message]() {
					if (CancelDeferredLaunch(message)) {
						F4T::UIClient::ClearJoinInProgress();
					}
				});
			}
			if (succeeded && *op == "select") {
				const auto reconnect = a_packet.find("needsReconnect");
				const bool needsReconnect = reconnect == a_packet.end() || !reconnect->is_boolean() || reconnect->get<bool>();
				bool reconnectJoin{};
				{
					std::scoped_lock lock(g_mutex);
					if (slot) {
						g_selectedSlot = *slot;
					}
					reconnectJoin = needsReconnect && g_deferredLaunch.has_value();
					g_selectionDone = !reconnectJoin;
				}
				if (reconnectJoin) {
					StartSelectionReconnect();
				} else if (needsReconnect) {
					QueueGameTask([]() { F4T::UIClient::PushConnectionStatus("Character chosen. Reconnect to the server to play as this character."); });
				} else {
					ResumeDeferredLaunch();
				}
			}
			QueueStateChanged();
		}

		void HandleChallenge(const Json& a_packet)
		{
			const auto slot = ReadSlotNumber(a_packet);
			const auto token = ReadString(a_packet, "confirmToken");
			const auto expires = a_packet.find("expiresInSeconds");
			if (!slot || !token || !IsPrintableToken(*token) || expires == a_packet.end() || !expires->is_number_unsigned()) {
				LogThrottledWarning("slot_challenge_malformed", "Ignoring malformed characterDeleteChallenge packet.");
				return;
			}
			{
				std::scoped_lock lock(g_mutex);
				g_pendingDelete = PendingDelete{ *slot, std::string{ *token }, std::chrono::steady_clock::now(), expires->get<std::uint32_t>() };
				g_lastResult.reset();
			}
			QueueStateChanged();
		}

		bool SendActionPacket(std::string_view a_op, std::optional<std::uint32_t> a_slot, const std::string& a_extra, bool a_clearPending)
		{
			Json packet{ { "type", std::string{ a_op } } };
			if (a_slot) {
				packet["slot"] = *a_slot;
			}
			if (!a_extra.empty()) {
				const auto extra = Json::parse(a_extra, nullptr, false);
				if (!extra.is_object()) {
					return false;
				}
				for (auto it = extra.begin(); it != extra.end(); ++it) {
					packet[it.key()] = it.value();
				}
			}
			{
				std::scoped_lock lock(g_mutex);
				if (a_op != "characterList") {
					g_lastResult.reset();
				}
			}
			SendSlotPacket(std::move(packet), a_clearPending);
			return true;
		}
	}

	void NoteWelcome(const Json& a_packet)
	{
		const auto found = a_packet.find("characterSlots");
		bool supported = false;
		bool legacy = false;
		bool bound = false;
		std::string notice;
		std::optional<std::uint32_t> selected;
		if (found != a_packet.end() && found->is_object()) {
			supported = found->value("supported", false);
			legacy = found->value("legacy", false);
			bound = found->value("bound", false);
			if (const auto text = found->find("notice"); text != found->end() && text->is_string() &&
				text->get_ref<const std::string&>().size() <= kMaximumNoticeBytes) {
				notice = text->get<std::string>();
			}
			if (const auto current = found->find("selected"); current != found->end() && current->is_number_unsigned() &&
				current->get<std::uint32_t>() <= kMaximumSaneSlot) {
				selected = current->get<std::uint32_t>();
			}
		}
		{
			std::scoped_lock lock(g_mutex);
			g_supported = supported;
			g_legacy = legacy;
			g_notice = std::move(notice);
			g_slots.clear();
			g_selectedSlot = selected;
			g_selectionDone = bound;
			g_pendingDelete.reset();
			g_lastResult.reset();
		}
		if (NeedsSelection()) {
			SendActionPacket("characterList", std::nullopt, {}, true);
		}
		QueueStateChanged();
	}

	bool TryHandlePacket(std::string_view a_type, const Json& a_packet)
	{
		if (a_type == "characterListState") {
			HandleListState(a_packet);
		} else if (a_type == "characterResult") {
			HandleResult(a_packet);
		} else if (a_type == "characterDeleteChallenge") {
			HandleChallenge(a_packet);
		} else {
			return false;
		}
		return true;
	}

	void Reset()
	{
		{
			std::scoped_lock lock(g_mutex);
			g_supported = false;
			g_legacy = false;
			g_notice.clear();
			g_slots.clear();
			g_selectedSlot.reset();
			g_selectionDone = false;
			g_pendingDelete.reset();
			g_lastResult.reset();
			g_sendTokens = kSendBurstTokens;
			g_lastSendRefill = {};
		}
		QueueStateChanged();
	}

	bool HandleAction(std::string_view a_actionJson)
	{
		if (a_actionJson.empty() || a_actionJson.size() > kMaximumActionJsonBytes) {
			return false;
		}
		const auto action = Json::parse(std::string{ a_actionJson }, nullptr, false);
		if (!action.is_object()) {
			return false;
		}
		const auto op = action.value("op", "");
		if (op != "slotList" && op != "slotCreate" && op != "slotSelect" && op != "slotDelete" && op != "slotRestore" &&
			op != "slotDeleteCancel") {
			return false;
		}
		if (op != "slotList" && op != "slotDeleteCancel" && IsReconnecting()) {
			LogThrottledWarning("slot_action_reconnecting", "Ignoring a character slot action while the selection reconnect is in progress.");
			return true;
		}
		if (op == "slotList") {
			return SendActionPacket("characterList", std::nullopt, {}, true);
		}
		if (op == "slotDeleteCancel") {
			{
				std::scoped_lock lock(g_mutex);
				g_pendingDelete.reset();
			}
			QueueStateChanged();
			return true;
		}
		const auto slot = ReadSlotNumber(action);
		if (!slot) {
			LogThrottledWarning("slot_action_slot", "Ignoring a character slot action with an invalid slot.");
			return true;
		}
		if (op == "slotSelect") {
			return SendActionPacket("characterSelect", slot, {}, true);
		}
		if (op == "slotRestore") {
			return SendActionPacket("characterRestore", slot, {}, true);
		}
		if (op == "slotCreate") {
			const auto name = ReadString(action, "name");
			if (!name || !F4T::CharacterSlotProtocol::NormalizeName(*name)) {
				LogThrottledWarning("slot_action_name", "Ignoring a character slot create with an invalid name.");
				return true;
			}
			Json extra{ { "name", std::string{ *name } } };
			return SendActionPacket("characterCreate", slot, extra.dump(), true);
		}
		const auto token = ReadString(action, "token");
		if (token && !IsPrintableToken(*token)) {
			LogThrottledWarning("slot_action_token", "Ignoring a character slot delete with an invalid token.");
			return true;
		}
		Json extra;
		if (token) {
			extra["confirmToken"] = std::string{ *token };
		}
		return SendActionPacket("characterDelete", slot, token ? extra.dump() : std::string{}, false);
	}

	std::string WithCharacterSlots(std::string a_stateJson)
	{
		try {
			auto state = Json::parse(a_stateJson);
			if (!state.is_object()) {
				return a_stateJson;
			}
			Json block{ { "supported", false }, { "needsSelection", false } };
			{
				std::scoped_lock lock(g_mutex);
				if (g_reconnecting) {
					block["loading"] = true;
				}
				if (!g_supported) {
					state["characterSlots"] = std::move(block);
					return state.dump();
				}
				block["supported"] = true;
				block["needsSelection"] = g_supported && !g_legacy && !g_selectionDone && !g_reconnecting;
				if (g_legacy) {
					block["legacy"] = true;
				}
				if (!g_notice.empty()) {
					block["notice"] = g_notice;
				}
				if (g_selectedSlot) {
					block["selectedSlot"] = *g_selectedSlot;
				}
				Json list = Json::array();
				for (const auto& entry : g_slots) {
					if (entry.empty) {
						Json item{ { "slot", entry.slot }, { "empty", true } };
						if (entry.deleted) {
							item["deleted"] = true;
							if (entry.deletedAt) {
								item["deletedAt"] = *entry.deletedAt;
							}
							if (entry.restorableUntil) {
								item["restorableUntil"] = *entry.restorableUntil;
							}
						}
						list.push_back(std::move(item));
					} else {
						Json item{ { "slot", entry.slot }, { "name", entry.name } };
						if (entry.hasCreatedAt) {
							item["createdAt"] = entry.createdAt;
						}
						if (entry.hasLastPlayedAt) {
							item["lastPlayedAt"] = entry.lastPlayedAt;
						}
						list.push_back(std::move(item));
					}
				}
				block["slots"] = std::move(list);
				if (g_pendingDelete) {
					block["pendingDelete"] = Json{ { "slot", g_pendingDelete->slot }, { "token", g_pendingDelete->token }, { "expiresInSeconds", g_pendingDelete->expiresInSeconds } };
				}
				if (g_lastResult) {
					Json result{ { "op", g_lastResult->op }, { "ok", g_lastResult->ok } };
					if (!g_lastResult->ok) {
						result["reason"] = g_lastResult->reason;
					}
					if (g_lastResult->hasSlot) {
						result["slot"] = g_lastResult->slot;
					}
					block["lastResult"] = std::move(result);
				}
			}
			if (block.dump().size() > kStateOutputBudgetBytes) {
				block.erase("slots");
				block.erase("pendingDelete");
				block.erase("lastResult");
				block["truncated"] = true;
			}
			state["characterSlots"] = std::move(block);
			return state.dump();
		} catch (...) {
			LogThrottledWarning("slot_state_dump", "Could not merge character slots into the faction state JSON.");
			return a_stateJson;
		}
	}

	bool NeedsSelection()
	{
		std::scoped_lock lock(g_mutex);
		return g_supported && !g_legacy && !g_selectionDone;
	}

	bool IsReconnecting()
	{
		std::scoped_lock lock(g_mutex);
		return g_reconnecting;
	}

	void DeferLaunch(const F4T::SessionLaunch::JoinRequest& a_request, std::function<void()> a_resume, std::function<void(bool, std::string_view)> a_cancel)
	{
		std::scoped_lock lock(g_mutex);
		g_deferredLaunch = DeferredLaunch{ ++g_launchSequence, a_request, std::move(a_resume), std::move(a_cancel) };
	}

	bool CancelDeferredLaunch(std::string_view a_reason)
	{
		std::optional<DeferredLaunch> deferred;
		{
			std::scoped_lock lock(g_mutex);
			deferred = std::move(g_deferredLaunch);
			g_deferredLaunch.reset();
		}
		if (!deferred) {
			return false;
		}
		if (deferred->cancel) {
			deferred->cancel(false, a_reason);
		}
		WipeServerPassword(deferred->request.serverPassword);
		return true;
	}
}
