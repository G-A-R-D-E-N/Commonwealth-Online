#include "pch.h"

#include "F4TDifficultyReportClient.h"

#include "F4TDifficultyReport.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "RE/P/PlayerCharacter.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <optional>

namespace F4T::DifficultyReportClient
{
	namespace
	{
		DifficultyReport::Cadence g_cadence;
		std::uint64_t g_cadenceConnection{};

		double NowSeconds()
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		std::optional<std::int32_t> ReadCurrentDifficulty()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->GetParentCell()) {
				return std::nullopt;
			}
			const auto value = static_cast<std::int32_t>(player->GetDifficultyLevel());
			if (!DifficultyEnforcement::IsValidDifficulty(value)) {
				return std::nullopt;
			}
			return value;
		}
	}

	void Tick(bool a_connected)
	{
		if (const auto difficulty = ReadCurrentDifficulty()) {
			DifficultyReport::SetLocalDifficulty(*difficulty);
		} else {
			DifficultyReport::ClearLocalDifficulty();
		}

		const auto connection = F4T::Networking::GetConnectionGeneration();
		if (connection != g_cadenceConnection) {
			g_cadence = {};
			g_cadenceConnection = connection;
		}

		const auto current = DifficultyReport::LocalDifficulty();
		if (!a_connected || !current || !DifficultyReport::ServerAcceptsReports()) {
			return;
		}
		const auto now = NowSeconds();
		if (!DifficultyReport::Due(g_cadence, *current, now)) {
			return;
		}

		const auto packet = nlohmann::json{ { "type", "difficultyReport" }, { "difficulty", *current } }.dump();
		if (!F4T::Networking::Detail::SendPacketBytes(packet)) {
			F4T::Networking::Detail::LogThrottledWarning(
				"difficulty_report_send_failed",
				"Could not report the game difficulty to the server.");
			return;
		}

		const bool logIt = !g_cadence.lastSent || *g_cadence.lastSent != *current;
		g_cadence.lastSent = *current;
		g_cadence.lastSentAt = now;
		if (logIt) {
			REX::INFO("[Difficulty] report sent difficulty={}", *current);
		}
	}

	void OnLoad()
	{
		DifficultyReport::ClearLocalDifficulty();
		g_cadence = {};
	}

	void OnDisconnect()
	{
		DifficultyReport::ClearLocalDifficulty();
		g_cadence = {};
	}
}
