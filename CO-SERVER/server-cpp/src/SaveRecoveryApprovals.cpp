#include "SaveRecoveryApprovals.h"

#include "F4TDurableState.h"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace CommonwealthOnline::Server
{
    namespace
    {
        constexpr std::size_t kMaximumApprovals = 1024;
        constexpr std::uintmax_t kMaximumStoreBytes = 8 * 1024 * 1024;

        bool CleanName(std::string_view a_value, std::size_t a_max)
        {
            if (a_value.empty() || a_value.size() > a_max) return false;
            for (const unsigned char ch : a_value) {
                if (ch < 32 || ch == 127 || ch == '/' || ch == '\\' || ch == ':') return false;
            }
            return a_value != "." && a_value != "..";
        }

        bool ValidApproval(std::string_view a_profileId, const SaveRecoveryApproval& a_approval)
        {
            if (!CleanName(a_profileId, 256) || !CleanName(a_approval.save.saveFile, 256) ||
                a_approval.note.empty() || a_approval.note.size() > 512) return false;
            for (const unsigned char ch : a_approval.note) if (ch < 32 || ch == 127) return false;
            const auto character = F4T::SaveProfileLogic::ExtractCharacterId(a_approval.save.saveFile);
            if (!character || !F4T::SaveProfileLogic::CharacterIdsEqual(*character, a_approval.characterId)) return false;
            try {
                F4T::DurableState::ValidateSaveAttestation(a_approval.save);
                return true;
            } catch (const std::exception&) {
                return false;
            }
        }

        SaveRecoveryApproval ParseApproval(const nlohmann::json& a_record)
        {
            if (!a_record.is_object() || a_record.size() != 6 || !a_record.at("generation").is_number_unsigned() ||
                a_record.at("generation").get<std::uint64_t>() == 0 || !a_record.at("consumed").is_boolean())
                throw std::invalid_argument("Invalid save recovery approval.");
            SaveRecoveryApproval approval;
            approval.characterId = a_record.at("characterId").get<std::string>();
            approval.save.saveFile = a_record.at("saveFile").get<std::string>();
            approval.save.sha256Hex = a_record.at("sha256").get<std::string>();
            approval.note = a_record.at("note").get<std::string>();
            approval.generation = a_record.at("generation").get<std::uint64_t>();
            approval.consumed = a_record.at("consumed").get<bool>();
            return approval;
        }
    }

    SaveRecoveryApprovals::SaveRecoveryApprovals(std::filesystem::path a_path, std::string a_serverInstanceId,
        DurableWritePipeline::Clock a_clock) :
        _approvals(nlohmann::json::object()), _pipeline(a_path, a_clock)
    {
        auto backup = a_path;
        backup += ".bak";
        nlohmann::json root;
        if (std::filesystem::exists(a_path)) {
            if (std::filesystem::file_size(a_path) > kMaximumStoreBytes) throw std::runtime_error("Save recovery approval store is too large.");
            std::ifstream input(a_path, std::ios::binary);
            if (!input.is_open()) throw std::runtime_error("Cannot read save recovery approvals.");
            input >> root;
            F4T::DurableState::ValidateRoot(root);
            if (root.at("serverInstanceId") != a_serverInstanceId) throw std::runtime_error("Save recovery approvals belong to another server.");
            if (!root.contains("saveRecoveryApprovals")) throw std::runtime_error("Save recovery approval section is missing.");
            _approvals = root.at("saveRecoveryApprovals");
            if (!_approvals.is_object() || _approvals.size() > kMaximumApprovals) throw std::runtime_error("Invalid save recovery approvals.");
            for (const auto& entry : _approvals.items()) {
                const auto approval = ParseApproval(entry.value());
                if (!ValidApproval(entry.key(), approval)) throw std::runtime_error("Invalid save recovery approval.");
            }
            _pipeline.SetDocument(root);
        } else {
            if (std::filesystem::exists(backup)) throw std::runtime_error("Save recovery approval store is missing; manual recovery required.");
            root = F4T::DurableState::NewRoot(a_serverInstanceId, a_clock());
            root["saveRecoveryApprovals"] = _approvals;
            _pipeline.PersistDocument(root);
        }
    }

    bool SaveRecoveryApprovals::Persist(nlohmann::json a_updated)
    {
        const auto ticket = _pipeline.Post({ DurableSectionDelta{ "saveRecoveryApprovals", a_updated } }, true);
        if (!_pipeline.Durable(ticket) || _pipeline.TicketQuarantined(ticket)) return false;
        _approvals = std::move(a_updated);
        return true;
    }

    bool SaveRecoveryApprovals::Approve(std::string_view a_profileId, const SaveRecoveryApproval& a_approval)
    {
        if (!ValidApproval(a_profileId, a_approval)) return false;
        const std::scoped_lock lock(_mutex);
        auto updated = _approvals;
        const auto key = std::string{ a_profileId };
        std::uint64_t generation = 1;
        if (updated.contains(key)) {
            const auto previous = ParseApproval(updated.at(key));
            if (!previous.consumed) {
                return previous.characterId == a_approval.characterId &&
                    previous.save.saveFile == a_approval.save.saveFile &&
                    previous.save.sha256Hex == a_approval.save.sha256Hex && previous.note == a_approval.note;
            }
            if (previous.generation == (std::numeric_limits<std::uint64_t>::max)()) return false;
            generation = previous.generation + 1;
        } else if (updated.size() >= kMaximumApprovals) {
            return false;
        }
        updated[key] = nlohmann::json{ { "characterId", a_approval.characterId }, { "saveFile", a_approval.save.saveFile },
            { "sha256", a_approval.save.sha256Hex }, { "note", a_approval.note }, { "generation", generation }, { "consumed", false } };
        return Persist(std::move(updated));
    }

    bool SaveRecoveryApprovals::Consume(std::string_view a_profileId, const SaveRecoveryApproval& a_expected)
    {
        const std::scoped_lock lock(_mutex);
        const auto found = _approvals.find(std::string{ a_profileId });
        if (found == _approvals.end()) return false;
        const auto current = ParseApproval(*found);
        if (current.consumed || current.generation != a_expected.generation ||
            current.characterId != a_expected.characterId || current.save.saveFile != a_expected.save.saveFile ||
            current.save.sha256Hex != a_expected.save.sha256Hex) return false;
        auto updated = _approvals;
        updated[std::string{ a_profileId }]["consumed"] = true;
        return Persist(std::move(updated));
    }

    std::optional<SaveRecoveryApproval> SaveRecoveryApprovals::Get(std::string_view a_profileId) const
    {
        const std::scoped_lock lock(_mutex);
        const auto found = _approvals.find(std::string{ a_profileId });
        return found == _approvals.end() ? std::nullopt : std::optional<SaveRecoveryApproval>{ ParseApproval(*found) };
    }
}
