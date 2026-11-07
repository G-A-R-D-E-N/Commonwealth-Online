#include "SaveRecoveryAdmin.h"

#include "F4TCharacterSlots.h"

#include <exception>
#include <string>
#include <utility>

namespace CommonwealthOnline::Server
{
    void InstallSaveRecoveryHooks(AdminCommandHooks& a_hooks, SaveRecoveryApprovals& a_store,
        std::function<bool(std::string_view)> a_knownProfile, std::function<bool(std::string_view)> a_disconnected,
        std::function<RecoveryCharacterMatch(std::string_view, std::string_view)> a_matchesProfile)
    {
        a_hooks.saveRecoveryInspect = [&a_store, known = a_knownProfile](const nlohmann::json& a_request) -> AdminActionResult {
            if (!a_request.contains("profileId") || !a_request.at("profileId").is_string()) return { false, "A profileId is required.", nullptr };
            const auto profile = a_request.at("profileId").get<std::string>();
            if (!known || !known(profile)) return { false, "The profile is unknown.", nullptr };
            const auto approval = a_store.Get(profile);
            if (!approval) return { true, "No save recovery approval exists.", nullptr };
            return { true, "Save recovery approval.", nlohmann::json{ { "profileId", profile },
                { "characterId", approval->characterId }, { "saveFile", approval->save.saveFile },
                { "sha256", approval->save.sha256Hex }, { "note", approval->note },
                { "generation", approval->generation }, { "consumed", approval->consumed } } };
        };
        a_hooks.saveRecoveryResolve = [&a_store, known = std::move(a_knownProfile), disconnected = std::move(a_disconnected), matches = std::move(a_matchesProfile)](const nlohmann::json& a_request) -> AdminActionResult {
            for (const auto* field : { "profileId", "characterId", "saveFile", "sha256", "note", "confirm" }) {
                if (!a_request.contains(field) || !a_request.at(field).is_string()) return { false, "Recovery requires profileId, characterId, saveFile, sha256, note and confirm.", nullptr };
            }
            const auto confirm = a_request.at("confirm").get<std::string>();
            if (confirm != "save-and-inventory-verified" && confirm != "first-character-save-and-inventory-verified") return { false, "Explicit save and inventory confirmation is required.", nullptr };
            const auto profile = a_request.at("profileId").get<std::string>();
            if (!known || !known(profile)) return { false, "The profile is unknown.", nullptr };
            if (!disconnected || !disconnected(profile)) return { false, "Disconnect the profile before recovery.", nullptr };
            if (!matches) return { false, "Character verification is unavailable.", nullptr };
            const auto match = matches(profile, a_request.at("characterId").get_ref<const std::string&>());
            if (match == RecoveryCharacterMatch::Conflict) return { false, "The character conflicts with the profile.", nullptr };
            if (match == RecoveryCharacterMatch::Missing && confirm != "first-character-save-and-inventory-verified") return { false, "First-character recovery requires explicit first-character-save-and-inventory-verified confirmation.", nullptr };
            SaveRecoveryApproval approval;
            approval.characterId = a_request.at("characterId").get<std::string>();
            approval.save.saveFile = a_request.at("saveFile").get<std::string>();
            approval.save.sha256Hex = a_request.at("sha256").get<std::string>();
            approval.note = a_request.at("note").get<std::string>();
            try {
                if (!a_store.Approve(profile, approval)) return { false, "Recovery approval was invalid or conflicts with an existing approval.", nullptr };
            } catch (const std::exception&) {
                return { false, "Recovery approval could not be persisted.", nullptr };
            }
            return { true, "Manual save recovery approval recorded.", nlohmann::json{ { "profileId", profile },
                { "characterId", approval.characterId }, { "saveFile", approval.save.saveFile }, { "sha256", approval.save.sha256Hex } } };
        };
    }

    void InstallFirstJoinAbandonHook(AdminCommandHooks& a_hooks, std::function<bool(std::string_view)> a_knownProfile,
        std::function<bool(std::string_view)> a_disconnected, std::function<bool(std::string_view, std::uint32_t, bool)> a_abandon)
    {
        a_hooks.firstJoinAbandon = [known = std::move(a_knownProfile), disconnected = std::move(a_disconnected), abandon = std::move(a_abandon)](const nlohmann::json& a_request) -> AdminActionResult {
            if (!a_request.contains("profileId") || !a_request.at("profileId").is_string() || !a_request.contains("confirm") || !a_request.at("confirm").is_string()) return { false, "first-join-abandon requires profileId and confirm.", nullptr };
            const auto confirm = a_request.at("confirm").get<std::string>();
            const bool withoutSave = confirm == "discard-first-join-without-save";
            if (confirm != "discard-unbegun-first-join" && !withoutSave) return { false, "Explicit discard-unbegun-first-join or discard-first-join-without-save confirmation is required.", nullptr };
            const auto profile = a_request.at("profileId").get<std::string>();
            const auto slot = F4T::CharacterSlots::ParseSlotKey(profile);
            if (!slot) return { false, "profileId must be an account#slot profile.", nullptr };
            if (!known || !known(profile)) return { false, "The profile is unknown.", nullptr };
            if (!disconnected || !disconnected(profile)) return { false, "Disconnect the profile before abandoning its first character.", nullptr };
            if (!abandon || !abandon(slot->first, slot->second, withoutSave)) return { false, withoutSave
                ? "The slot was not discarded: its starting grant names a character with a recorded save, it is the active character, or it does not exist. No change was made."
                : "The slot is not an unbegun first character: its starting grant began, it has starting inventory, or it does not exist. If the player never got a save for it, use confirm discard-first-join-without-save. No change was made.", nullptr };
            return { true, "Unbegun first-join character discarded.", nlohmann::json{ { "profileId", profile } } };
        };
    }
}
