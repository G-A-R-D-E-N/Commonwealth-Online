#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace F4T::InventoryContainerSync
{
	struct Settled
	{
		bool committed{};
		std::uint32_t revision{};
		std::string reason;
	};

	void Activate(const std::string& a_sessionNonce, const std::string& a_serverKey, const std::string& a_profile);
	void Reset();
	[[nodiscard]] bool Busy();
	[[nodiscard]] std::optional<nlohmann::json> Pump(std::uint64_t a_nowMs, bool a_quiet, std::uint32_t a_revision);
	[[nodiscard]] std::optional<Settled> NoteResult(const nlohmann::json& a_packet);
	void Abandon();
}
