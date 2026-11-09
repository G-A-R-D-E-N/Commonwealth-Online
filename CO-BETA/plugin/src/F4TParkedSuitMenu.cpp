#include "pch.h"

#include "F4TParkedSuitSync.h"

#include "F4TParkedSuitInternal.h"
#include "F4TParkedSuitUi.h"

#include "F4TRemotePlayerState.h"
#include "F4TSocialNameplate.h"

#include "RE/B/BGSLocation.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESFullName.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESWorldSpace.h"

#include "RE/P/PlayerCharacter.h"

#include <algorithm>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::ParkedSuitSync
{
	using namespace detail;

	namespace
	{
		std::optional<nlohmann::json> NormalizePicks(const nlohmann::json& a_packet)
		{
			const auto found = a_packet.find("picks");
			if (found == a_packet.end() || !found->is_array() || found->size() > kBreachMaximumPicks) {
				return std::nullopt;
			}
			auto picks = nlohmann::json::array();
			for (const auto& pick : *found) {
				if (!pick.is_array() || pick.size() != 2) {
					return std::nullopt;
				}
				const auto row = AsInteger(pick[0], 0, kBreachGridSize - 1);
				const auto col = AsInteger(pick[1], 0, kBreachGridSize - 1);
				if (!row || !col) {
					return std::nullopt;
				}
				picks.push_back(nlohmann::json::array({ *row, *col }));
			}
			return picks;
		}

		std::string PlaceName(std::optional<std::uint32_t> a_cellId, std::optional<std::uint32_t> a_worldspaceId)
		{
			if (a_cellId) {
				if (const auto* cell = RE::TESForm::GetFormByID<RE::TESObjectCELL>(*a_cellId)) {
					if (const auto cellName = RE::TESFullName::GetFullName(*cell); !cellName.empty()) {
						return std::string{ cellName };
					}
					if (const auto* location = cell->GetLocation()) {
						if (const auto locationName = RE::TESFullName::GetFullName(*location); !locationName.empty()) {
							return std::string{ locationName };
						}
					}
				}
			}
			if (a_worldspaceId) {
				if (const auto* space = RE::TESForm::GetFormByID<RE::TESWorldSpace>(*a_worldspaceId)) {
					if (const auto spaceName = RE::TESFullName::GetFullName(*space); !spaceName.empty()) {
						return std::string{ spaceName };
					}
				}
			}
			return {};
		}

		std::string OwnerName(std::string_view a_ownerKey)
		{
			for (const auto& remote : F4T::RemotePlayerState::GetRemotePlayerSnapshot()) {
				if (remote.playerKey != a_ownerKey) {
					continue;
				}
				if (auto shown = F4T::SocialNameplate::ResolveDisplayName(remote.displayName, remote.characterName); !shown.empty()) {
					return shown;
				}
			}
			return "Owner unknown";
		}

		std::size_t LockLength(std::string_view a_ownerKey, const Placed& a_placed)
		{
			constexpr float kAttendedDistanceSquared = 4096.0F * 4096.0F;
			for (const auto& remote : F4T::RemotePlayerState::GetRemotePlayerSnapshot()) {
				if (remote.playerKey != a_ownerKey) {
					continue;
				}
				const auto dx = remote.x - a_placed.x;
				const auto dy = remote.y - a_placed.y;
				const auto dz = remote.z - a_placed.z;
				return dx * dx + dy * dy + dz * dz <= kAttendedDistanceSquared ? 4 : 3;
			}
			return 2;
		}

		std::string LocationText(const Placed& a_placed)
		{
			if (auto place = PlaceName(a_placed.cellId, a_placed.worldspaceId); !place.empty()) {
				return place;
			}
			return std::format("X {:.0f}  Y {:.0f}  Z {:.0f}", a_placed.x, a_placed.y, a_placed.z);
		}

		std::vector<std::pair<std::uint32_t, Placed>> OrderedByDistance()
		{
			std::vector<std::pair<std::uint32_t, Placed>> ordered;
			{
				const std::scoped_lock lock(g_mutex);
				ordered.reserve(g_owners.size());
				for (const auto& [refFormId, placed] : g_owners) {
					ordered.emplace_back(refFormId, placed);
				}
			}
			const auto* player = RE::PlayerCharacter::GetSingleton();
			float originX{};
			float originY{};
			float originZ{};
			if (player) {
				const auto origin = player->GetPosition();
				originX = origin.x;
				originY = origin.y;
				originZ = origin.z;
			}
			const auto squared = [originX, originY, originZ](const Placed& a_placed) {
				const auto dx = a_placed.x - originX;
				const auto dy = a_placed.y - originY;
				const auto dz = a_placed.z - originZ;
				return dx * dx + dy * dy + dz * dz;
			};
			std::sort(ordered.begin(), ordered.end(), [&squared](const auto& a_left, const auto& a_right) {
				const auto left = squared(a_left.second);
				const auto right = squared(a_right.second);
				if (left != right) {
					return left < right;
				}
				return a_left.first < a_right.first;
			});
			return ordered;
		}
	}

	std::string BuildUiStateJson()
	{
		const auto hackResults = [] {
			const std::scoped_lock lock(g_mutex);
			return g_hackResults;
		}();
		const auto challenge = [] {
			const std::scoped_lock lock(g_mutex);
			return g_challenge;
		}();
		nlohmann::json suits = nlohmann::json::array();
		const auto target = [] {
			const std::scoped_lock lock(g_mutex);
			return std::make_pair(g_target, g_targetSequence);
		}();
		for (const auto& [suitId, placed] : OrderedByDistance()) {
			const auto mine = placed.mine;
			if (!mine && target.first != suitId) {
				continue;
			}
			nlohmann::json suit{
				{ "suitId", Hex(suitId) },
				{ "owner", mine ? std::string{ "You" } : OwnerName(placed.ownerKey) },
				{ "location", LocationText(placed) },
				{ "targetLength", LockLength(placed.ownerKey, placed) },
				{ "mine", mine },
				{ "locked", !mine }
			};
			if (const auto found = hackResults.find(suitId); found != hackResults.end()) {
				suit["result"] = found->second ? "Broken in." : "You already own this suit.";
				suit["outcome"] = found->second ? "broken" : "owned";
			}
			if (challenge && challenge->suitId == suitId) {
				suit["challenge"] = nlohmann::json{ { "suitId", Hex(suitId) }, { "seed", challenge->seed }, { "length", challenge->length } };
			}
			suits.push_back(std::move(suit));
		}
		nlohmann::json state{ { "open", true }, { "suits", suits } };
		if (target.first) {
			state["target"] = Hex(*target.first);
			state["targetSequence"] = target.second;
		}
		return state.dump();
	}

	void HandleUiAction(std::string_view a_actionJson)
	{
		if (a_actionJson.size() > 1024) {
			return;
		}
		const auto parsed = nlohmann::json::parse(a_actionJson, nullptr, false);
		if (!parsed.is_object()) {
			return;
		}
		const auto op = parsed.value("op", std::string{});
		if (op == "close" || op == "clearTarget") {
			{
				const std::scoped_lock lock(g_mutex);
				g_target.reset();
			}
			F4T::ParkedSuitUi::NotifyStateChanged();
			return;
		}
		const auto suitId = ReadSuitId(parsed);
		if (!suitId) {
			return;
		}
		if (op == "remove") {
			RemoveOwnSuit(*suitId);
			return;
		}
		if (op == "breachStart") {
			{
				const std::scoped_lock lock(g_mutex);
				g_challenge.reset();
			}
			SendDirect(nlohmann::json{ { "type", "parkedSuit" }, { "action", "breachStart" }, { "suitId", Hex(*suitId) } });
			return;
		}
		if (op == "breachFailed") {
			{
				const std::scoped_lock lock(g_mutex);
				ForgetChallenge(*suitId);
			}
			SendDirect(nlohmann::json{ { "type", "parkedSuit" }, { "action", "breachFailed" }, { "suitId", Hex(*suitId) } });
			return;
		}
		if (op != "hack") {
			return;
		}
		const auto picks = NormalizePicks(parsed);
		if (!picks) {
			return;
		}
		{
			const std::scoped_lock lock(g_mutex);
			g_hackResults.erase(*suitId);
		}
		F4T::ParkedSuitUi::NotifyStateChanged();
		SendDirect(nlohmann::json{ { "type", "parkedSuit" }, { "action", "hack" }, { "suitId", Hex(*suitId) }, { "picks", *picks } });
	}
}
