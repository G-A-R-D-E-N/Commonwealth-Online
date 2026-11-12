#pragma once
#include "F4TDeathBagWire.h"
#include <functional>
#include <nlohmann/json.hpp>
#include <string_view>
namespace RE { class Actor; }
namespace F4T::DeathBagGrant
{
	enum class Outcome { Rejected, Duplicate, Attempted };
	[[nodiscard]] Outcome ApplyHealth(RE::Actor& a_actor, const F4T::DeathBagWire::HealthGrant& a_grant);
	void NoteReconciliation(const nlohmann::json& a_packet);
	void NotePlaceFailedAck(const nlohmann::json& a_packet);
	bool TryHandlePacket(std::string_view a_type, const nlohmann::json& a_packet);
	void AwaitSave(const F4T::DeathBagWire::HealthGrant& a_grant);
	void RetryFresh(const F4T::DeathBagWire::HealthGrant& a_grant);
	[[nodiscard]] bool TrackWithdrawal(const std::string& a_deathId, const std::string& a_operationId);
	void ForgetWithdrawal(const std::string& a_deathId, const std::string& a_operationId);
	void OnLoad();
	void ApplyReconciliation(const std::function<bool(const nlohmann::json&, std::uint64_t)>& a_sendAttempt);
}
