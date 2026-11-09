#include "F4TDurableState.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::DurableState
{
	namespace
	{
		using Json = nlohmann::json;

		Json* SurvivalProfile(Json& a_root, std::string_view a_profileId)
		{
			auto& profiles = a_root.at("profiles");
			const auto found = profiles.find(std::string{ a_profileId });
			return found == profiles.end() ? nullptr : std::addressof(*found);
		}

		const Json* SurvivalProfile(const Json& a_root, std::string_view a_profileId)
		{
			const auto& profiles = a_root.at("profiles");
			const auto found = profiles.find(std::string{ a_profileId });
			return found == profiles.end() ? nullptr : std::addressof(*found);
		}

		std::optional<std::uint32_t> InventoryItemCount(const Json& a_root, std::string_view a_profileId, const std::string& a_itemKey)
		{
			const auto* profile = SurvivalProfile(a_root, a_profileId);
			if (profile == nullptr || !profile->contains("inventory")) {
				return std::nullopt;
			}
			const auto& items = profile->at("inventory").at("items");
			const auto found = items.find(a_itemKey);
			if (found == items.end()) {
				return 0u;
			}
			if (!found->at("count").is_number_unsigned()) {
				return std::nullopt;
			}
			return found->at("count").get<std::uint32_t>();
		}
	}

	F4T::SurvivalNeeds::Record GetSurvivalNeeds(const nlohmann::json& a_root, std::string_view a_profileId)
	{
		const auto* profile = SurvivalProfile(a_root, a_profileId);
		if (profile == nullptr || !profile->contains("survivalNeeds")) {
			return {};
		}
		const auto& stored = profile->at("survivalNeeds");
		try {
			F4T::SurvivalNeeds::Record record;
			record.state = F4T::SurvivalNeeds::State{ stored.at("hunger").get<double>(), stored.at("thirst").get<double>(), stored.at("sleep").get<double>() };
			if (!F4T::SurvivalNeeds::Valid(record.state) || !stored.at("revision").is_number_unsigned() || !stored.at("consumed").is_array()) {
				return {};
			}
			record.revision = stored.at("revision").get<std::uint64_t>();
			if (const auto claims = stored.find("claims"); claims != stored.end()) {
				if (!claims->is_array() || claims->size() > F4T::SurvivalNeeds::kMaximumOutstandingClaims) {
					return {};
				}
				for (const auto& claim : *claims) {
					if (!claim.at("baseCount").is_number_unsigned() || !claim.at("deadline").is_number()) {
						return {};
					}
					const auto deadline = claim.at("deadline").get<double>();
					if (!std::isfinite(deadline) || deadline < 0.0) {
						return {};
					}
					record.claims.push_back(F4T::SurvivalNeeds::Claim{ claim.at("operationId").get<std::string>(), claim.at("kind").get<std::string>(),
						claim.at("itemKey").get<std::string>(), claim.at("baseCount").get<std::uint32_t>(), deadline });
				}
			}
			for (const auto& entry : stored.at("consumed")) {
				const auto status = entry.at("status").get<std::string>();
				if (status != "applied" && status != "refused") {
					return {};
				}
				record.consumed.emplace_back(entry.at("operationId").get<std::string>(),
					status == "applied" ? F4T::SurvivalNeeds::Status::Applied : F4T::SurvivalNeeds::Status::Refused);
			}
			if (record.consumed.size() > F4T::SurvivalNeeds::kMaximumConsumedOperations) {
				return {};
			}
			return record;
		} catch (const std::exception&) {
			return {};
		}
	}

	void PutSurvivalNeeds(nlohmann::json& a_root, std::string_view a_profileId, F4T::SurvivalNeeds::Record a_record, double a_updatedAt)
	{
		auto* profile = SurvivalProfile(a_root, a_profileId);
		if (profile == nullptr) {
			(a_root.at("profiles"))[std::string{ a_profileId }] = Json{ { "updatedAt", a_updatedAt } };
			profile = SurvivalProfile(a_root, a_profileId);
		}
		Json stored{ { "hunger", a_record.state.hunger }, { "thirst", a_record.state.thirst }, { "sleep", a_record.state.sleep },
			{ "revision", a_record.revision + 1 }, { "claims", Json::array() } };
		for (const auto& claim : a_record.claims) {
			stored["claims"].push_back(Json{ { "operationId", claim.operationId }, { "kind", claim.kind },
				{ "itemKey", claim.itemKey }, { "baseCount", claim.baseCount }, { "deadline", claim.deadline } });
		}
		Json consumed = Json::array();
		for (const auto& [operationId, status] : a_record.consumed) {
			consumed.push_back(Json{ { "operationId", operationId },
				{ "status", status == F4T::SurvivalNeeds::Status::Applied ? "applied" : "refused" } });
		}
		stored["consumed"] = std::move(consumed);
		(*profile)["survivalNeeds"] = std::move(stored);
		(*profile)["updatedAt"] = a_updatedAt;
	}

	F4T::SurvivalNeeds::Admission BeginSurvivalClaim(nlohmann::json& a_root, std::string_view a_profileId,
		F4T::SurvivalNeeds::Claim a_claim, std::uint32_t a_creditedDrops, double a_updatedAt)
	{
		auto record = GetSurvivalNeeds(a_root, a_profileId);
		if (F4T::SurvivalNeeds::FindConsumed(record, a_claim.operationId)) {
			return F4T::SurvivalNeeds::Admission::Replayed;
		}
		const bool outstanding = std::any_of(record.claims.begin(), record.claims.end(),
			[&](const F4T::SurvivalNeeds::Claim& a_active) { return a_active.operationId == a_claim.operationId; });
		if (outstanding) {
			return F4T::SurvivalNeeds::Admission::Pending;
		}
		if (record.claims.size() >= F4T::SurvivalNeeds::kMaximumOutstandingClaims) {
			return F4T::SurvivalNeeds::Admission::Busy;
		}
		const auto count = InventoryItemCount(a_root, a_profileId, a_claim.itemKey);
		if (!count || (*count == 0 && a_creditedDrops == 0)) {
			return F4T::SurvivalNeeds::Admission::Missing;
		}
		a_claim.baseCount = *count + a_creditedDrops;
		a_claim.deadline = a_updatedAt + F4T::SurvivalNeeds::kProofDeadlineSeconds;
		record.claims.push_back(std::move(a_claim));
		PutSurvivalNeeds(a_root, a_profileId, std::move(record), a_updatedAt);
		return F4T::SurvivalNeeds::Admission::Started;
	}

	F4T::SurvivalNeeds::Settled SettleSurvivalNeeds(nlohmann::json& a_root, std::string_view a_profileId, double a_now)
	{
		auto record = GetSurvivalNeeds(a_root, a_profileId);
		F4T::SurvivalNeeds::SettleCounts counts;
		for (const auto& claim : record.claims) {
			counts.try_emplace(claim.itemKey, InventoryItemCount(a_root, a_profileId, claim.itemKey));
		}
		auto settled = F4T::SurvivalNeeds::Settle(record, counts, a_now);
		if (!settled.empty()) {
			PutSurvivalNeeds(a_root, a_profileId, std::move(record), a_now);
		}
		return settled;
	}
}
