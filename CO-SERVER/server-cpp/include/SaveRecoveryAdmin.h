#pragma once

#include "AdminCommandDispatcher.h"
#include "SaveRecoveryApprovals.h"

#include <functional>
#include <string_view>

namespace CommonwealthOnline::Server
{
    class DurableStateFileStore;
    enum class RecoveryCharacterMatch { Match, Missing, Conflict };
    void InstallInventoryAuthorityHooks(AdminCommandHooks& a_hooks, DurableStateFileStore& a_store);
    void InstallSaveRecoveryHooks(AdminCommandHooks& a_hooks, SaveRecoveryApprovals& a_store,
        std::function<bool(std::string_view)> a_knownProfile, std::function<bool(std::string_view)> a_disconnected,
        std::function<RecoveryCharacterMatch(std::string_view, std::string_view)> a_matchesProfile);
    void InstallFirstJoinAbandonHook(AdminCommandHooks& a_hooks, std::function<bool(std::string_view)> a_knownProfile,
        std::function<bool(std::string_view)> a_disconnected, std::function<bool(std::string_view, std::uint32_t, bool)> a_abandon);
}
