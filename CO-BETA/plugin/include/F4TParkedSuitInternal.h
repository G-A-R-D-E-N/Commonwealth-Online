#pragma once

#include "F4TNetworking.h"
#include "F4TParkedSuit.h"
#include "F4TPowerArmorSync.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace F4T::ParkedSuitSync::detail
{
	inline constexpr std::int64_t kBreachGridSize = 6;
	inline constexpr std::size_t kBreachMaximumPicks = 5;
	inline constexpr std::int64_t kBreachMinimumLength = 2;
	inline constexpr std::int64_t kBreachMaximumLength = 4;

	struct Placed
	{
		std::string ownerKey;
		std::optional<std::uint32_t> localId;
		std::optional<std::uint32_t> cellId;
		std::optional<std::uint32_t> worldspaceId;
		float x{};
		float y{};
		float z{};
		bool mine{};
		std::uint32_t frameBaseFormId{};
		F4T::PowerArmorSync::PowerArmorState powerArmor;
		float angleZ{};
	};

	struct Challenge
	{
		std::uint32_t suitId{};
		std::uint32_t seed{};
		std::uint32_t length{};
	};

	// Constraint: g_owners, g_hackResults, g_challenge and g_adopting are shared with the receive thread under g_mutex.
	inline std::mutex g_mutex;
	inline std::unordered_map<std::uint32_t, Placed> g_owners;
	inline std::unordered_map<std::uint32_t, bool> g_hackResults;
	inline std::optional<Challenge> g_challenge;
	inline std::optional<std::uint32_t> g_target;
	inline std::uint32_t g_targetSequence{};
	inline std::unordered_map<std::uint32_t, bool> g_adopting;

	inline std::optional<std::int64_t> AsInteger(const nlohmann::json& a_value, std::int64_t a_minimum, std::int64_t a_maximum)
	{
		if (!a_value.is_number_integer()) {
			return std::nullopt;
		}
		const auto value = a_value.get<std::int64_t>();
		if (value < a_minimum || value > a_maximum) {
			return std::nullopt;
		}
		return value;
	}

	inline void SendDirect(const nlohmann::json& a_packet)
	{
		if (F4T::Networking::IsConnectedToServer()) {
			(void)F4T::Networking::SendParkedSuitPacket(a_packet);
		}
	}

	inline std::string Hex(std::uint32_t a_value)
	{
		return std::format("{:08X}", a_value);
	}

	inline std::optional<std::uint32_t> ReadSuitId(const nlohmann::json& a_packet)
	{
		const auto found = a_packet.find("suitId");
		if (found == a_packet.end() || !found->is_string()) {
			return std::nullopt;
		}
		const auto normalized = F4T::ParkedSuit::NormalizeSuitId(*found);
		if (!normalized) {
			return std::nullopt;
		}
		std::uint32_t parsed = 0;
		const auto result = std::from_chars(normalized->data(), normalized->data() + normalized->size(), parsed, 16);
		if (result.ec != std::errc{}) {
			return std::nullopt;
		}
		return parsed;
	}

	inline void ForgetChallenge(std::uint32_t a_suitId)
	{
		if (g_challenge && g_challenge->suitId == a_suitId) {
			g_challenge.reset();
		}
	}

	[[nodiscard]] nlohmann::json WithPieceKeys(nlohmann::json a_packet, const std::vector<F4T::PowerArmorSync::Piece>& a_pieces);
}
