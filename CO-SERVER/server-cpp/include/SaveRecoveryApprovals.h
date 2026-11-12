#pragma once

#include "DurableWritePipeline.h"
#include "F4TSaveProfileLogic.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace CommonwealthOnline::Server
{
    struct SaveRecoveryApproval
    {
        std::string characterId;
        F4T::SaveProfileLogic::AttestedSave save;
        std::string note;
        std::uint64_t generation{};
        bool consumed{};
    };

    class SaveRecoveryApprovals
    {
    public:
        SaveRecoveryApprovals(std::filesystem::path a_path, std::string a_serverInstanceId,
            DurableWritePipeline::Clock a_clock);
        bool Approve(std::string_view a_profileId, const SaveRecoveryApproval& a_approval);
        std::optional<SaveRecoveryApproval> Get(std::string_view a_profileId) const;
        bool Consume(std::string_view a_profileId, const SaveRecoveryApproval& a_expected);

    private:
        bool Persist(nlohmann::json a_updated);
        mutable std::mutex _mutex;
        nlohmann::json _approvals;
        DurableWritePipeline _pipeline;
    };
}
