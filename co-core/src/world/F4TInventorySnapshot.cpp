#include "F4TInventorySnapshot.h"

#include "F4TContentManifest.h"
#include "F4TDropLedger.h"
#include "F4TDurableState.h"
#include "F4TFormIdentity.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace F4T::InventorySnapshot
{
	using Json = nlohmann::json;

	namespace
	{
		std::optional<std::uint32_t> ReadRevision(const Json& a_object, const char* a_key)
		{
			if (!a_object.contains(a_key) || !a_object.at(a_key).is_number_unsigned()) return std::nullopt;
			const auto value = a_object.at(a_key).get<std::uint64_t>();
			if (value >= (std::numeric_limits<std::uint32_t>::max)()) return std::nullopt;
			return static_cast<std::uint32_t>(value);
		}

		Json Committed(const std::string& a_operationId, std::uint32_t a_revision)
		{
			return Json{ { "type", std::string{ kResultType } }, { "operationId", a_operationId },
				{ "status", "committed" }, { "revision", a_revision } };
		}

		Json Rejected(const std::string& a_operationId, const char* a_reason, std::optional<std::uint32_t> a_revision)
		{
			Json packet{ { "type", std::string{ kResultType } }, { "operationId", a_operationId },
				{ "status", "rejected" }, { "reason", a_reason } };
			if (a_revision) packet["revision"] = *a_revision;
			return packet;
		}

		Json Logged(const Json& a_request)
		{
			return Json{ { "type", std::string{ kRequestType } }, { "operationId", a_request.at("operationId") },
				{ "baseRevision", a_request.at("baseRevision") },
				{ "itemsDigest", F4T::ContentManifest::Sha256Hex(a_request.at("items").dump()) } };
		}
	}

	std::optional<Json> NormalizeItems(const Json& a_wireItems)
	{
		try {
			return F4T::DurableState::NormalizeStartingInventory(a_wireItems);
		} catch (const std::invalid_argument&) {
			return std::nullopt;
		}
	}

	std::optional<Json> NormalizeSnapshotItems(const Json& a_wireItems)
	{
		auto items = NormalizeItems(a_wireItems);
		if (!items) return std::nullopt;
		for (const auto& item : *items) {
			if (item.at("count").get<std::uint32_t>() > kMaximumItemCount ||
				std::stoul(item.at("localFormId").get<std::string>(), nullptr, 16) > F4T::FormIdentity::MaximumRegularLocalId) {
				return std::nullopt;
			}
		}
		return items;
	}

	std::optional<Part> NormalizePart(const Json& a_packet)
	{
		if (!a_packet.is_object() || a_packet.size() != 6 || a_packet.value("type", std::string{}) != kRequestType ||
			!a_packet.contains("operationId") || !a_packet.at("operationId").is_string()) {
			return std::nullopt;
		}
		const auto operationId = a_packet.at("operationId").get<std::string>();
		const auto baseRevision = ReadRevision(a_packet, "baseRevision");
		const auto part = ReadRevision(a_packet, "part");
		const auto parts = ReadRevision(a_packet, "parts");
		if (operationId.empty() || operationId.size() > F4T::Drops::kMaximumOperationIdChars || !baseRevision || !part || !parts ||
			*parts == 0 || *parts > kMaximumParts || *part >= *parts || !a_packet.contains("items") || !a_packet.at("items").is_array() ||
			a_packet.at("items").size() > kMaximumPartItems || !NormalizeSnapshotItems(a_packet.at("items"))) {
			return std::nullopt;
		}
		return Part{ operationId, *baseRevision, *part, *parts, a_packet.at("items") };
	}

	std::optional<Json> NormalizeRequest(const std::string& a_operationId, std::uint32_t a_baseRevision, const Json& a_wireItems)
	{
		if (a_operationId.empty() || a_operationId.size() > F4T::Drops::kMaximumOperationIdChars) return std::nullopt;
		const auto items = NormalizeSnapshotItems(a_wireItems);
		if (!items) return std::nullopt;
		return Json{ { "type", std::string{ kRequestType } }, { "operationId", a_operationId },
			{ "baseRevision", a_baseRevision }, { "items", *items } };
	}

	bool IsRequest(const Json& a_request)
	{
		return a_request.is_object() && a_request.value("type", std::string{}) == kRequestType;
	}

	GainPolicy DefaultGainPolicy(GainMode a_mode)
	{
		GainPolicy policy;
		policy.mode = a_mode;
		policy.buckets = {
			{ "units", BucketLimit{ 50000, 1000000, 20000 } },
			{ std::string{ kCapsItemKey }, BucketLimit{ 250000, 1000000, 100000 } },
			{ "newKeys", BucketLimit{ 256, 2048, 128 } },
		};
		return policy;
	}

	std::optional<GainMode> ParseGainMode(std::string_view a_text)
	{
		if (a_text == "off") return GainMode::Off;
		if (a_text == "warn") return GainMode::Warn;
		if (a_text == "enforce") return GainMode::Enforce;
		return std::nullopt;
	}

	GainVerdict EvaluateGain(const Json& a_before, const Json& a_after, bool a_onboarding, const GainPolicy& a_policy,
		const GainBuckets& a_buckets, double a_now)
	{
		GainVerdict verdict{ .next = a_buckets };
		if (a_policy.mode == GainMode::Off) return verdict;
		std::map<std::string, std::uint64_t> charges;
		for (const auto& [key, item] : a_after.items()) {
			const auto after = item.at("count").get<std::uint64_t>();
			const auto found = a_before.find(key);
			const auto before = found == a_before.end() ? std::uint64_t{} : found->at("count").get<std::uint64_t>();
			if (after <= before) continue;
			if (after > a_policy.maxStack && !verdict.limited) {
				verdict.limited = true;
				verdict.detail = "stackCeiling " + key + " " + std::to_string(after);
			}
			charges[a_policy.buckets.contains(key) ? key : std::string{ "units" }] += after - before;
			if (before == 0) ++charges["newKeys"];
		}
		for (const auto& [name, charge] : charges) {
			const auto limit = a_policy.buckets.find(name);
			if (limit == a_policy.buckets.end()) continue;
			const auto burst = static_cast<double>(a_onboarding ? limit->second.onboardingBurst : limit->second.burst);
			const auto stored = a_buckets.find(name);
			auto bucket = stored == a_buckets.end() ? Bucket{ burst, a_now } : stored->second;
			const auto elapsed = (std::max)(0.0, a_now - bucket.at);
			bucket.tokens = (std::min)(burst, bucket.tokens + elapsed * static_cast<double>(limit->second.refillPerMinute) / 60.0);
			bucket.at = a_now;
			const auto need = static_cast<double>(charge);
			if (bucket.tokens < need) {
				if (!verdict.limited) {
					verdict.detail = "budget " + name + " need " + std::to_string(charge) + " have " +
						std::to_string(static_cast<std::uint64_t>(bucket.tokens));
				}
				verdict.limited = true;
			}
			bucket.tokens = (std::max)(0.0, bucket.tokens - need);
			verdict.next[name] = bucket;
		}
		return verdict;
	}

	Outcome Reconcile(Json& a_root, const std::string& a_profileId, const Json& a_request, const GainCheck& a_gain)
	{
		const auto operationId = a_request.at("operationId").get<std::string>();
		const auto revisionOf = [](const Json& a_profile) { return a_profile.at("inventory").at("revision").get<std::uint32_t>(); };
		if (a_root.at("inventoryAuthorityBlocked").get<bool>()) {
			return { Rejected(operationId, "inventoryAuthorityUnavailable", std::nullopt), false };
		}
		auto& profiles = a_root.at("profiles");
		const auto profile = profiles.find(a_profileId);
		if (profile == profiles.end() || !profile->contains("inventory")) {
			return { Rejected(operationId, "inventoryAuthorityUnavailable", std::nullopt), false };
		}
		if (F4T::DurableState::PendingStartingGrant(a_root, a_profileId)) {
			return { Rejected(operationId, "startingInventoryPending", std::nullopt), false };
		}
		const auto revision = revisionOf(*profile);
		const auto logged = Logged(a_request);
		for (const auto& operation : profile->at("inventoryOperations")) {
			if (operation.at("request").at("operationId") != operationId) continue;
			if (operation.at("request") == logged) return { operation.at("result"), false };
			return { Rejected(operationId, "operationMismatch", revision), false };
		}
		if (a_request.at("baseRevision").get<std::uint32_t>() != revision) {
			return { Rejected(operationId, "staleRevision", revision), false };
		}
		auto& inventory = profile->at("inventory");
		const auto& items = a_request.at("items");
		if (items == inventory.at("items")) return { Committed(operationId, revision), false };
		if (revision == (std::numeric_limits<std::uint32_t>::max)()) {
			return { Rejected(operationId, "capacityExceeded", revision), false };
		}
		if (a_gain && a_gain(inventory.at("items"), items, revision)) {
			return { Rejected(operationId, kGainLimitedReason.data(), revision), false };
		}
		Json candidate{ { "initialized", true }, { "revision", revision + 1 }, { "items", items } };
		try {
			F4T::DurableState::ValidateInventory(candidate);
		} catch (const std::invalid_argument&) {
			return { Rejected(operationId, "capacityExceeded", revision), false };
		}
		auto result = Committed(operationId, revision + 1);
		auto& history = profile->at("inventoryOperations");
		if (history.size() >= F4T::Drops::kMaximumOperationHistory) history.erase(history.begin());
		history.push_back(Json{ { "request", logged }, { "result", result } });
		inventory = std::move(candidate);
		return { std::move(result), true };
	}

	std::uint32_t ValidateLoggedOperation(const Json& a_operation, std::set<std::string>& a_seen,
		std::uint32_t a_expectedRevision, std::uint32_t a_revision)
	{
		const auto& request = a_operation.at("request");
		const auto& result = a_operation.at("result");
		if (!IsRequest(request) || request.size() != 4 || !request.at("operationId").is_string() ||
			!request.contains("itemsDigest") || !request.at("itemsDigest").is_string() ||
			!F4T::ContentManifest::IsValidFingerprint(request.at("itemsDigest").get_ref<const std::string&>())) {
			throw std::invalid_argument("Invalid inventory snapshot operation.");
		}
		const auto operationId = request.at("operationId").get<std::string>();
		const auto baseRevision = ReadRevision(request, "baseRevision");
		if (operationId.empty() || operationId.size() > F4T::Drops::kMaximumOperationIdChars || !baseRevision ||
			!a_seen.insert(operationId).second || *baseRevision + 1 != a_expectedRevision || a_expectedRevision > a_revision) {
			throw std::invalid_argument("Invalid inventory snapshot revision.");
		}
		if (result != Committed(operationId, *baseRevision + 1)) {
			throw std::invalid_argument("Invalid inventory snapshot result.");
		}
		return a_expectedRevision;
	}
}
