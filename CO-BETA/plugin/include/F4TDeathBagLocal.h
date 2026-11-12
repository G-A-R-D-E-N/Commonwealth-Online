#pragma once
#include "F4TDeathBagLocalPlan.h"
#include "RE/B/BSPointerHandle.h"
#include "RE/N/NiPoint3.h"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace RE { class Actor; class TESBoundObject; class TESObjectREFR; }
namespace F4T::DeathBagLocal
{
	struct Manifest
	{
		nlohmann::json stacks = nlohmann::json::array();
		nlohmann::json retained = nlohmann::json::array();
	};
	[[nodiscard]] std::optional<Manifest> Snapshot(RE::Actor& a_player, const std::string& a_deathId);
	struct PlaceResult
	{
		RE::ObjectRefHandle bag;
		bool restored{ true };
		nlohmann::json remaining = nlohmann::json::array();
	};
	[[nodiscard]] PlaceResult Place(RE::Actor& a_player, const std::string& a_deathId);
	[[nodiscard]] RE::ObjectRefHandle SpawnView(RE::Actor& a_player, const RE::NiPoint3& a_location);
	[[nodiscard]] bool FillView(RE::TESObjectREFR& a_bag, const nlohmann::json& a_stacks);
	[[nodiscard]] std::uint64_t ReturnToMirror(RE::Actor& a_player, RE::TESObjectREFR& a_bag, RE::TESBoundObject& a_object, std::uint32_t a_count, const std::vector<nlohmann::json>& a_candidates);
	[[nodiscard]] std::uint64_t ReturnFromMirror(RE::Actor& a_player, RE::TESObjectREFR& a_bag, RE::TESBoundObject& a_object, std::uint32_t a_count, const nlohmann::json& a_metadata);
	void Abandon(const std::string& a_deathId);
	void AbandonPending();
	[[nodiscard]] nlohmann::json PendingStacks(const std::string& a_deathId);
	[[nodiscard]] std::optional<nlohmann::json> CarriedStacks(RE::Actor& a_player);
	[[nodiscard]] bool IsBagReference(RE::TESObjectREFR& a_reference);
	bool ReturnAll(RE::Actor& a_player, RE::TESObjectREFR& a_bag, nlohmann::json& a_remaining);
}
