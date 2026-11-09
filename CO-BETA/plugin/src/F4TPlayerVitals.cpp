#include "pch.h"

#include "F4TPlayerVitals.h"

#include "F4TDebugLogging.h"
#include "F4TDownedGlue.h"
#include "F4TJoinFlow.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TRemotePlayerState.h"
#include "F4TSessionLaunch.h"

#include "RE/A/ActorValue.h"
#include "RE/P/PlayerCharacter.h"

#include <chrono>
#include <optional>

namespace
{
	F4T::PlayerVitals::PublishState g_publishState;

	double NowSeconds()
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	std::optional<F4T::PlayerVitals::Vitals> ReadLocalVitals()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* actorValues = RE::ActorValue::GetSingleton();
		if (!player || !actorValues || !actorValues->health) {
			return std::nullopt;
		}
		const auto& health = *actorValues->health;
		const auto percent = F4T::PlayerVitals::ComputeHealthPercent(
			player->GetActorValue(health),
			player->GetPermanentActorValue(health));
		if (!percent) {
			return std::nullopt;
		}
		F4T::PlayerVitals::Vitals vitals{};
		vitals.healthPercent = *percent;
		vitals.isDowned = F4T::Downed::IsLocalPlayerDowned();
		vitals.isDead = F4T::Downed::IsLocalPlayerDead();
		vitals.level = F4T::PlayerVitals::NormalizeLevel(static_cast<std::int32_t>(player->GetLevel()));
		return vitals;
	}
}

namespace F4T::PlayerVitals
{
	bool SendVitalsPacket(const Vitals& a_vitals)
	{
		if (!Networking::IsConnectedToServer()) {
			return false;
		}
		const auto encoded = Encode(a_vitals);
		if (!encoded) {
			return false;
		}
		const nlohmann::json packet{
			{ "type", "playerState" },
			{ "vitals", *encoded }
		};
		if (!Networking::Detail::SendPacketBytes(packet.dump())) {
			Networking::Detail::LogThrottledWarning(
				"vitals_send_failed",
				"Could not send Commonwealth Online playerState vitals packet.");
			return false;
		}
		return true;
	}

	void PublishTick()
	{
		if (!Networking::IsConnectedToServer() ||
			!SessionLaunch::CanPublishGameplayTransform() ||
			!JoinFlow::CanPublishGameplayTransform()) {
			return;
		}
		const auto vitals = ReadLocalVitals();
		if (!vitals || !ShouldPublish(g_publishState, *vitals, NowSeconds())) {
			return;
		}
		(void)SendVitalsPacket(*vitals);
	}

	bool ApplyRemoteVitalsFromPacket(std::uint32_t a_playerId, const nlohmann::json& a_packet)
	{
		const auto found = a_packet.find("vitals");
		if (found == a_packet.end()) {
			return false;
		}
		const auto vitals = Decode(*found);
		if (!vitals) {
			Networking::Detail::LogThrottledWarning(
				"vitals_invalid",
				"Ignoring Commonwealth Online playerState vitals with out-of-range values.");
			return false;
		}
		return RemotePlayerState::SetRemotePlayerVitals(a_playerId, *vitals);
	}
}
