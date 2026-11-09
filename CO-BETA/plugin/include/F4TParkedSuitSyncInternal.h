#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace RE
{
	class TESObjectREFR;
}

namespace F4T::ParkedSuitSync::detail
{
	struct AppliedLock
	{
		std::uint32_t localId{};
		bool locked{};
	};

	struct PendingTransition
	{
		nlohmann::json packet;
		std::string op;
		std::uint32_t localId{};
		bool consoleSpawned{};
		bool nacked{};
		std::size_t attempts{};
		bool inFlight{};
		std::chrono::steady_clock::time_point nextSend{};
		std::string refusal;
		bool staleWorld{};
	};

	inline constexpr auto kRetryDelay = std::chrono::seconds{ 2 };
	inline constexpr std::size_t kMaximumPendingTransitions = 8;

	extern std::unordered_map<std::uint32_t, AppliedLock> g_appliedLocked;
	extern std::unordered_map<std::uint32_t, std::uint32_t> g_graduated;
	extern std::unordered_map<std::uint32_t, std::uint32_t> g_graduatedBeforeReconnect;
	extern std::deque<PendingTransition> g_pending;
	extern std::unordered_map<std::uint32_t, nlohmann::json> g_droppedParks;
	extern std::unordered_set<std::uint32_t> g_consoleParked;
	extern std::unordered_set<std::uint32_t> g_consoleUnsent;

	std::optional<std::uint32_t> ParseHexId(const nlohmann::json& a_object, std::string_view a_key);
	bool IsPowerArmorFrame(RE::TESObjectREFR& a_ref);
	bool FrameResolvesToBase(std::uint32_t a_localId, std::uint32_t a_frameBaseFormId);

	// Constraint: Enqueue, FlushPending and RetainParksLocked are defined in F4TParkedSuitSyncQueue.cpp; RetainParksLocked needs g_mutex held.
	void Enqueue(PendingTransition a_transition);
	void FlushPending();
	void RetainParksLocked();
	// Constraint: DiscardParksLocked needs g_mutex held; an in-flight park is kept marked staleWorld so its ack or nack cannot requeue it.
	void DiscardParksLocked();
	std::chrono::seconds NackDelay(const PendingTransition& a_transition);

	[[nodiscard]] inline bool IsDefinitiveRefusal(std::string_view a_reason)
	{
		return a_reason == "invalidParkRequest" || a_reason == "parkRefused" || a_reason == "notInFrame";
	}
}
