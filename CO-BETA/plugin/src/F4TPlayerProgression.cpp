#include "pch.h"

#include "F4TPlayerProgression.h"

#include "F4TJoinFlow.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TSessionLaunch.h"

#include "RE/A/ActorValue.h"
#include "RE/B/BGSPerk.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESDataHandler.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <format>
#include <optional>
#include <utility>

namespace F4T::PlayerProgression
{
	namespace
	{
		constexpr double kSampleIntervalSeconds = 1.0;
		std::optional<Progression> g_lastPublished;
		std::optional<double> g_nextSampleSeconds;

		double NowSeconds()
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		// TODO-verify: SPECIAL, experience and perk values are unconfirmed in game.
		std::optional<nlohmann::json> Encode(const Progression& a_progression)
		{
			nlohmann::json special = nlohmann::json::object();
			for (std::size_t index = 0; index < a_progression.special.size(); ++index) {
				special[ProgressionValidation::kSpecialFields[index]] = a_progression.special[index];
			}
			nlohmann::json perks = nlohmann::json::array();
			for (const auto& perk : a_progression.perks) {
				perks.push_back(nlohmann::json{ { "formId", perk.formId }, { "rank", perk.rank } });
			}
			return ProgressionValidation::NormalizeProgression(nlohmann::json{
				{ "special", std::move(special) },
				{ "level", a_progression.level },
				{ "experience", a_progression.experience },
				{ "perks", std::move(perks) }
			});
		}

		std::optional<Progression> ReadLocalProgression()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* actorValues = RE::ActorValue::GetSingleton();
			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			if (!player || !actorValues || !dataHandler || !actorValues->experience) {
				return std::nullopt;
			}
			const std::array specialValues{
				actorValues->strength,
				actorValues->perception,
				actorValues->endurance,
				actorValues->charisma,
				actorValues->intelligence,
				actorValues->agility,
				actorValues->luck
			};
			Progression progression{};
			for (std::size_t index = 0; index < specialValues.size(); ++index) {
				if (!specialValues[index]) {
					return std::nullopt;
				}
				const auto value = player->GetPermanentActorValue(*specialValues[index]);
				if (!std::isfinite(value)) {
					return std::nullopt;
				}
				progression.special[index] = static_cast<std::int32_t>(std::clamp<std::int64_t>(
					std::llround(value),
					ProgressionValidation::kMinimumSpecialValue,
					ProgressionValidation::kMaximumSpecialValue));
			}
			const auto experience = player->GetActorValue(*actorValues->experience);
			if (!std::isfinite(experience)) {
				return std::nullopt;
			}
			progression.level = player->GetLevel();
			progression.experience = std::clamp<std::int64_t>(std::llround(experience), 0, ProgressionValidation::kMaximumExperience);
			for (auto* perk : dataHandler->GetFormArray<RE::BGSPerk>()) {
				if (!perk) {
					continue;
				}
				const auto rank = player->GetPerkRank(perk);
				if (rank > 0) {
					progression.perks.push_back(PerkRank{ std::format("{:08X}", perk->GetFormID()), static_cast<std::int32_t>(rank) });
				}
			}
			return progression;
		}

		bool SendProgressionPacket(const Progression& a_progression)
		{
			if (!Networking::IsConnectedToServer()) {
				return false;
			}
			const auto encoded = Encode(a_progression);
			if (!encoded) {
				Networking::Detail::LogThrottledWarning(
					"progression_invalid",
					"Commonwealth Online progression was not sent: the sample is outside the supported limits.");
				return false;
			}
			const nlohmann::json packet{
				{ "type", "playerState" },
				{ "progression", *encoded }
			};
			if (!Networking::Detail::SendPacketBytes(packet.dump())) {
				Networking::Detail::LogThrottledWarning(
					"progression_send_failed",
					"Could not send Commonwealth Online playerState progression packet.");
				return false;
			}
			return true;
		}
	}

	void HandleRejectedPacket(const nlohmann::json& a_packet)
	{
		const auto found = a_packet.find("reason");
		const auto reason = found != a_packet.end() && found->is_string() ? found->get<std::string>() : std::string{ "unspecified" };
		Networking::Detail::LogThrottledWarning(
			"progression_rejected",
			std::string("Server rejected the Commonwealth Online progression report: ").append(reason.substr(0, 64)));
	}

	void PublishTick()
	{
		if (!Networking::IsConnectedToServer()) {
			g_lastPublished.reset();
			return;
		}
		if (!SessionLaunch::CanPublishGameplayTransform() || !JoinFlow::CanPublishGameplayTransform()) {
			return;
		}
		const auto now = NowSeconds();
		if (g_nextSampleSeconds && now < *g_nextSampleSeconds) {
			return;
		}
		g_nextSampleSeconds = now + kSampleIntervalSeconds;
		const auto progression = ReadLocalProgression();
		if (!progression || (g_lastPublished && *g_lastPublished == *progression)) {
			return;
		}
		if (SendProgressionPacket(*progression)) {
			g_lastPublished = *progression;
		}
	}
}
