#pragma once

#include <nlohmann/json.hpp>
#include <cstddef>
#include <optional>
#include <string>

namespace F4T::InventoryGrant
{
	RE::TESBoundObject* ResolveItem(const std::string& a_key);
	bool CheckLaunchSave(const std::optional<std::string>& a_save, bool& a_approved);
	void NoteStartingGrant(const nlohmann::json& a_grant);
	void NoteRecovery(const nlohmann::json& a_packet);
	void NoteRecoveryWelcome(const nlohmann::json& a_packet);
	void NoteVerification(const nlohmann::json& a_packet);
	void OnPreLoad(const char* a_name, std::size_t a_length);
	void OnPostLoad(bool a_succeeded);
	void OnNewGame();
	void Tick();
}
