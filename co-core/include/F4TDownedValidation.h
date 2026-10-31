#pragma once

#include "F4TDownedState.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace F4T::DownedValidation
{
	enum class RequestError : std::uint8_t
	{
		Malformed,
		InvalidTarget,
		InvalidPoint,
		InvalidFriend,
		InvalidStimpaks,
		InvalidSpace,
		InvalidPosition,
		InvalidReason
	};

	struct ReviveRequest
	{
		std::optional<RequestError> error;
		std::uint32_t targetPlayerId{};
		std::optional<std::uint32_t> stimpaksAvailable;
	};

	struct ReviveCancelRequest
	{
		std::optional<RequestError> error;
		std::uint32_t targetPlayerId{};
		std::string reason;
	};

	struct RespawnRequest
	{
		std::optional<RequestError> error;
		std::string point;
		std::uint32_t friendPlayerId{};
	};

	struct CheckpointRequest
	{
		std::optional<RequestError> error;
		DownedState::Checkpoint checkpoint;
	};

	struct Vitals
	{
		bool isDowned{};
		bool isDead{};
		bool hasStateFlags{};
	};

	[[nodiscard]] ReviveRequest NormalizeReviveRequest(const nlohmann::json& a_packet);
	[[nodiscard]] ReviveCancelRequest NormalizeReviveCancelRequest(const nlohmann::json& a_packet);
	[[nodiscard]] RespawnRequest NormalizeRespawnRequest(const nlohmann::json& a_packet);
	[[nodiscard]] CheckpointRequest NormalizeCheckpointRequest(const nlohmann::json& a_packet);
	[[nodiscard]] bool CarriesVitals(const nlohmann::json& a_packet) noexcept;
	[[nodiscard]] Vitals ReadVitals(const nlohmann::json& a_packet) noexcept;
}
