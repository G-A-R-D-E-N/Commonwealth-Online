#include "pch.h"

#include "F4TSurvivalNeedsClient.h"

#include "F4TInventoryDropGate.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TSurvivalLogPolicy.h"
#include "F4TSurvivalPolicyClient.h"

#include "CommonwealthOnlineUI_API.h"
#include "RE/T/TESForm.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace
{
	using NeedsJson = nlohmann::json;

	constexpr std::size_t kMaximumItemKeyChars = 160;
	constexpr std::size_t kMaximumTrackedClaims = 16;
	constexpr std::size_t kMaximumPendingResults = 8;
	constexpr std::int32_t kNoResult = 0;
	constexpr std::int32_t kFoodApplied = 1;
	constexpr std::int32_t kDrinkApplied = 2;
	constexpr std::int32_t kRefused = 3;

	struct Needs
	{
		double hunger{};
		double thirst{};
		double sleep{};
	};

	struct SentClaim
	{
		std::string operationId;
		std::string kind;
	};

	std::mutex g_mutex;
	Needs g_needs;
	std::vector<SentClaim> g_sentClaims;
	std::deque<std::int32_t> g_results;
	std::vector<NeedsJson> g_pendingSends;
	std::atomic<std::uint64_t> g_operationCounter{};
	const auto g_sessionNonce = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
	std::atomic<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI24*> g_hudUi{ nullptr };
	F4T::SurvivalLogPolicy::Throttle g_needsLogThrottle{ 30.0 };

	double SteadySeconds()
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	void PushHud(bool a_enabled, const Needs& a_needs)
	{
		const auto ui = g_hudUi.load();
		if (!ui || !ui->PushNeedsState) {
			return;
		}
		const NeedsJson state{ { "enabled", a_enabled }, { "hunger", a_needs.hunger }, { "thirst", a_needs.thirst }, { "sleep", a_needs.sleep } };
		const auto payload = state.dump();
		ui->PushNeedsState(payload.c_str());
	}

	std::optional<double> ReadUnitInterval(const NeedsJson& a_packet, const char* a_field)
	{
		const auto value = F4T::Networking::Detail::ReadDouble(a_packet, a_field);
		if (!value || *value < 0.0 || *value > 1.0) {
			return std::nullopt;
		}
		return value;
	}

	void HandleNeedsUpdate(const NeedsJson& a_packet)
	{
		const auto hunger = ReadUnitInterval(a_packet, "hunger");
		const auto thirst = ReadUnitInterval(a_packet, "thirst");
		const auto sleep = ReadUnitInterval(a_packet, "sleep");
		if (!hunger || !thirst || !sleep) {
			return;
		}
		bool due{};
		{
			const std::scoped_lock lock(g_mutex);
			g_needs = Needs{ *hunger, *thirst, *sleep };
			due = g_needsLogThrottle.Due(SteadySeconds());
		}
		if (due) {
			REX::INFO("[SurvivalNeeds] needsUpdate hunger={:.3f} thirst={:.3f} sleep={:.3f}", *hunger, *thirst, *sleep);
		}
		F4T::SurvivalNeedsClient::PushHudState();
	}

	void PushResult(std::int32_t a_result)
	{
		if (g_results.size() >= kMaximumPendingResults) {
			g_results.pop_front();
		}
		g_results.push_back(a_result);
	}

	void HandleConsumeResult(const NeedsJson& a_packet)
	{
		const auto operationId = F4T::Networking::Detail::ReadString(a_packet, "operationId");
		const auto status = F4T::Networking::Detail::ReadString(a_packet, "status");
		if (!operationId || !status || (*status != "applied" && *status != "refused" && *status != "duplicate")) {
			return;
		}
		const auto outcome = *status == "duplicate" ? F4T::Networking::Detail::ReadString(a_packet, "result") : status;
		const bool settled = outcome && (*outcome == "applied" || *outcome == "refused");
		const auto shown = outcome.value_or(*status);
		const std::scoped_lock lock(g_mutex);
		const auto sent = std::find_if(g_sentClaims.begin(), g_sentClaims.end(), [&](const SentClaim& a_claim) {
			return a_claim.operationId == *operationId;
		});
		if (sent == g_sentClaims.end()) {
			REX::INFO("[SurvivalNeeds] consume result op={} status={} claim=unknown", *operationId, shown);
			if (settled && *outcome == "refused") {
				PushResult(kRefused);
			}
			return;
		}
		REX::INFO("[SurvivalNeeds] consume result op={} kind={} status={}", *operationId, sent->kind, shown);
		if (settled) {
			PushResult(*outcome == "refused" ? kRefused : (sent->kind == "food" ? kFoodApplied : kDrinkApplied));
		}
		g_sentClaims.erase(sent);
	}

	std::string BeginClaim(const std::string& a_kind)
	{
		auto operationId = std::format("{:016X}-{}", g_sessionNonce, ++g_operationCounter);
		const std::scoped_lock lock(g_mutex);
		g_sentClaims.push_back(SentClaim{ operationId, a_kind });
		if (g_sentClaims.size() > kMaximumTrackedClaims) {
			g_sentClaims.erase(g_sentClaims.begin());
		}
		return operationId;
	}

	void PapyrusReportConsume(std::monostate, RE::TESForm* a_item, std::string a_kind)
	{
		if (!a_item || (a_kind != "food" && a_kind != "drink")) {
			return;
		}
		static F4T::SurvivalLogPolicy::Throttle consumeRefusedLog{ 10.0 };
		if (!F4T::SurvivalPolicy::NeedsEnabled() || !F4T::Networking::IsConnectedToServer()) {
			if (consumeRefusedLog.Due(SteadySeconds())) REX::INFO("[SurvivalNeeds] consume refused locally kind={} needsEnabled={} connected={}", a_kind,
				F4T::SurvivalPolicy::NeedsEnabled(), F4T::Networking::IsConnectedToServer());
			return;
		}
		const auto itemFormId = F4T::InventoryDropGate::TranslateItemKey(a_item);
		if (!itemFormId || itemFormId->size() > kMaximumItemKeyChars) {
			if (consumeRefusedLog.Due(SteadySeconds())) REX::INFO("[SurvivalNeeds] consume refused locally kind={} reason=itemKey", a_kind);
			return;
		}
		const auto operationId = BeginClaim(a_kind);
		const NeedsJson packet{ { "type", "needsConsume" }, { "operationId", operationId }, { "itemFormId", *itemFormId }, { "kind", a_kind } };
		const std::scoped_lock lock(g_mutex);
		g_pendingSends.push_back(packet);
	}

	float PapyrusNeedValue(double Needs::*a_member)
	{
		if (!F4T::SurvivalPolicy::NeedsEnabled()) {
			return 0.0f;
		}
		const std::scoped_lock lock(g_mutex);
		return static_cast<float>(g_needs.*a_member);
	}

	float PapyrusGetHunger(std::monostate)
	{
		return PapyrusNeedValue(&Needs::hunger);
	}

	float PapyrusGetThirst(std::monostate)
	{
		return PapyrusNeedValue(&Needs::thirst);
	}

	float PapyrusGetSleep(std::monostate)
	{
		return PapyrusNeedValue(&Needs::sleep);
	}

	std::int32_t PapyrusTakeConsumeResult(std::monostate)
	{
		const std::scoped_lock lock(g_mutex);
		if (g_results.empty()) {
			return kNoResult;
		}
		const auto result = g_results.front();
		g_results.pop_front();
		return result;
	}
}

namespace F4T::SurvivalNeedsClient
{
	void Reset()
	{
		const std::scoped_lock lock(g_mutex);
		g_needs = Needs{};
		g_sentClaims.clear();
		g_results.clear();
		g_pendingSends.clear();
	}

	void FlushPendingSends()
	{
		std::vector<NeedsJson> packets;
		{
			const std::scoped_lock lock(g_mutex);
			packets.swap(g_pendingSends);
		}
		for (const auto& packet : packets) {
			const auto operationId = packet["operationId"].get<std::string>();
			if (!F4T::Networking::Detail::SendPacketBytes(packet.dump())) {
				REX::INFO("[SurvivalNeeds] could not send needsConsume op={}", operationId);
			} else {
				REX::INFO("[SurvivalNeeds] consume sent op={} kind={}", operationId, packet["kind"].get<std::string>());
			}
		}
	}

	bool TryHandlePacket(std::string_view a_type, const NeedsJson& a_packet)
	{
		if (a_type == "needsUpdate") {
			HandleNeedsUpdate(a_packet);
			return true;
		}
		if (a_type == "needsConsumeResult") {
			HandleConsumeResult(a_packet);
			return true;
		}
		return false;
	}

	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm) {
			return false;
		}
		a_vm->BindNativeMethod("CommonwealthOnlineNeeds", "GetHunger", PapyrusGetHunger, false, false);
		a_vm->BindNativeMethod("CommonwealthOnlineNeeds", "GetThirst", PapyrusGetThirst, false, false);
		a_vm->BindNativeMethod("CommonwealthOnlineNeeds", "GetSleep", PapyrusGetSleep, false, false);
		a_vm->BindNativeMethod("CommonwealthOnlineNeeds", "TakeConsumeResult", PapyrusTakeConsumeResult, false, false);
		a_vm->BindNativeMethod("CommonwealthOnlineNeeds", "ReportConsume", PapyrusReportConsume, false, false);
		return true;
	}

	void RegisterHud()
	{
		g_hudUi.store(COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI24>());
	}

	void PushHudState()
	{
		Needs needs;
		{
			const std::scoped_lock lock(g_mutex);
			needs = g_needs;
		}
		PushHud(F4T::SurvivalPolicy::NeedsEnabled(), needs);
	}

	void ClearHud()
	{
		PushHud(false, Needs{});
	}
}
