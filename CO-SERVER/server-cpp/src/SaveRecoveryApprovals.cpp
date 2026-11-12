#include "SaveRecoveryApprovals.h"

#include "F4TDeathBag.h"
#include "F4TDurableState.h"

#include <ctime>
#include <fstream>
#include <iostream>
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

        struct UnusableStore : std::runtime_error
        {
            using std::runtime_error::runtime_error;
        };

        struct StoreContents
        {
            nlohmann::json root;
            nlohmann::json approvals;
            bool migrated{};
        };

        StoreContents ReadUsableStore(const std::filesystem::path& a_path, const std::string& a_serverInstanceId,
            const DurableWritePipeline::Clock& a_clock)
        {
            if (std::filesystem::file_size(a_path) > kMaximumStoreBytes) throw UnusableStore("it is larger than the 8 MiB limit");
            std::ifstream input(a_path, std::ios::binary);
            if (!input.is_open()) throw std::runtime_error("Cannot read save recovery approvals.");
            nlohmann::json parsed;
            try {
                parsed = nlohmann::json::parse(input);
            } catch (const std::exception&) {
                throw UnusableStore("it is not valid JSON");
            }
            StoreContents store;
            try {
                auto loaded = F4T::DurableState::LoadAndMigrate(std::move(parsed), a_clock());
                store.migrated = F4T::DeathBag::MigrateLoaded(loaded.root, a_clock()) || loaded.migrated;
                store.root = std::move(loaded.root);
            } catch (const std::exception& exception) {
                throw UnusableStore(std::string{ "it is not a usable durable state: " } + exception.what());
            }
            if (store.root.at("serverInstanceId") != a_serverInstanceId) throw UnusableStore("it belongs to another server");
            if (!store.root.contains("saveRecoveryApprovals")) throw UnusableStore("the approval section is missing");
            store.approvals = store.root.at("saveRecoveryApprovals");
            if (!store.approvals.is_object() || store.approvals.size() > kMaximumApprovals) throw UnusableStore("the approval section is invalid");
            try {
                for (const auto& entry : store.approvals.items()) {
                    if (!ValidApproval(entry.key(), ParseApproval(entry.value()))) throw std::invalid_argument("approval rejected");
                }
            } catch (const std::exception&) {
                throw UnusableStore("an approval record is invalid");
            }
            return store;
        }

        void Quarantine(const std::filesystem::path& a_path, std::string_view a_reason, double a_now)
        {
            const auto seconds = static_cast<std::time_t>(a_now);
            std::tm utc{};
#if defined(_WIN32)
            (void)gmtime_s(&utc, &seconds);
#else
            (void)gmtime_r(&seconds, &utc);
#endif
            char stamp[17]{};
            (void)std::strftime(stamp, sizeof stamp, "%Y%m%dT%H%M%SZ", &utc);
            const std::string suffix = std::string{ ".quarantine." } + stamp;
            auto target = a_path;
            target += suffix;
            for (std::size_t n = 2; std::filesystem::exists(target); ++n) {
                target = a_path;
                target += suffix + "." + std::to_string(n);
            }
            std::filesystem::rename(a_path, target);
            std::cerr << "Save recovery approvals are unusable (" << a_reason << "); moved to " << target.string() << "." << std::endl;
        }
    }

    SaveRecoveryApprovals::SaveRecoveryApprovals(std::filesystem::path a_path, std::string a_serverInstanceId,
        DurableWritePipeline::Clock a_clock) :
        _approvals(nlohmann::json::object()), _pipeline(a_path, a_clock)
    {
        auto backup = a_path;
        backup += ".bak";
        if (std::filesystem::exists(a_path)) {
            try {
                auto store = ReadUsableStore(a_path, a_serverInstanceId, a_clock);
                _approvals = std::move(store.approvals);
                if (store.migrated) {
                    _pipeline.PersistDocument(store.root);
                } else {
                    _pipeline.SetDocument(store.root);
                }
                return;
            } catch (const UnusableStore& exception) {
                _approvals = nlohmann::json::object();
                Quarantine(a_path, exception.what(), a_clock());
                if (std::filesystem::exists(backup)) {
                    try {
                        auto recovered = ReadUsableStore(backup, a_serverInstanceId, a_clock);
                        for (auto& entry : recovered.approvals) entry["consumed"] = true;
                        recovered.root["saveRecoveryApprovals"] = recovered.approvals;
                        _approvals = std::move(recovered.approvals);
                        _pipeline.PersistDocument(recovered.root);
                        std::cerr << "Save recovery approvals were restored from '" << backup.string()
                            << "'; every restored approval is marked consumed and must be approved again." << std::endl;
                        return;
                    } catch (const UnusableStore& backupError) {
                        _approvals = nlohmann::json::object();
                        std::cerr << "Save recovery approvals backup '" << backup.string() << "' is unusable (" << backupError.what() << ")." << std::endl;
                    }
                }
                std::cerr << "Save recovery approvals are starting empty." << std::endl;
            }
        } else if (std::filesystem::exists(backup)) {
            throw std::runtime_error("Save recovery approval store is missing; manual recovery required.");
        }
        auto root = F4T::DurableState::NewRoot(a_serverInstanceId, a_clock());
        root["saveRecoveryApprovals"] = _approvals;
        _pipeline.PersistDocument(root);
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
