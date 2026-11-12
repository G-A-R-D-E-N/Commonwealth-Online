#include "DurableStateFileStore.h"
#include "SaveRecoveryApprovals.h"
#include "SaveRecoveryAdmin.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

int main()
{
    using namespace CommonwealthOnline::Server;
    const auto directory = std::filesystem::temp_directory_path() /
        ("co-save-recovery-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto path = directory / "approvals.json";
    const std::string serverId(32, 'a');
    const SaveRecoveryApproval approval{ "AAAA1111", { "Save9_AAAA1111_Nomad_Commonwealth_20260814000101_1_2", std::string(64, 'b'), 0 }, "inspected inventory" };
    {
        SaveRecoveryApprovals store(path, serverId, [] { return 1000.0; });
        assert(!store.Get("profile-a"));
        assert(!store.Approve("", approval));
        auto wrong = approval;
        wrong.characterId = "BBBB2222";
        assert(!store.Approve("profile-a", wrong));
        wrong = approval;
        wrong.note = "invalid\ncontrol";
        assert(!store.Approve("profile-a", wrong));
        wrong = approval;
        wrong.save.sha256Hex = "bad";
        assert(!store.Approve("profile-a", wrong));
        assert(store.Approve("profile-a", approval));
        assert(store.Approve("profile-a", approval));
        wrong = approval;
        wrong.save.sha256Hex = std::string(64, 'c');
        assert(!store.Approve("profile-a", wrong));
        assert(!store.Get("profile-b"));
        AdminCommandHooks hooks;
        bool connected = true;
        InstallSaveRecoveryHooks(hooks, store, [](std::string_view id) { return id == "profile-admin-test" || id == "profile-new-test"; },
            [&](std::string_view) { return !connected; },
            [](std::string_view id, std::string_view character) {
                if (id == "profile-new-test") return RecoveryCharacterMatch::Missing;
                return character == "AAAA1111" ? RecoveryCharacterMatch::Match : RecoveryCharacterMatch::Conflict;
            });
        nlohmann::json request{ { "profileId", "profile-admin-test" }, { "characterId", approval.characterId },
            { "saveFile", approval.save.saveFile }, { "sha256", approval.save.sha256Hex },
            { "note", approval.note }, { "confirm", "save-and-inventory-verified" } };
        assert(!hooks.saveRecoveryResolve(request).ok);
        connected = false;
        auto unknown = request;
        unknown["profileId"] = "unknown";
        assert(!hooks.saveRecoveryResolve(unknown).ok);
        auto unconfirmed = request;
        unconfirmed["confirm"] = "yes";
        assert(!hooks.saveRecoveryResolve(unconfirmed).ok);
        auto wrongCharacter = request;
        wrongCharacter["characterId"] = "BBBB2222";
        wrongCharacter["saveFile"] = "Save9_BBBB2222_Nomad_Commonwealth_20260814000101_1_2";
        assert(!hooks.saveRecoveryResolve(wrongCharacter).ok);
        wrongCharacter["confirm"] = "first-character-save-and-inventory-verified";
        assert(!hooks.saveRecoveryResolve(wrongCharacter).ok);
        auto firstCharacter = request;
        firstCharacter["profileId"] = "profile-new-test";
        assert(!hooks.saveRecoveryResolve(firstCharacter).ok);
        firstCharacter["confirm"] = "first-character-save-and-inventory-verified";
        assert(hooks.saveRecoveryResolve(firstCharacter).ok);
        assert(hooks.saveRecoveryResolve(request).ok);
        assert(hooks.saveRecoveryInspect(request).data.at("sha256") == approval.save.sha256Hex);
        bool first = false;
        bool second = false;
        std::thread one([&] { first = store.Approve("profile-b", approval); });
        std::thread two([&] { second = store.Approve("profile-c", approval); });
        one.join();
        two.join();
        assert(first && second);
    }
    {
        SaveRecoveryApprovals restarted(path, serverId, [] { return 1001.0; });
        const auto actual = restarted.Get("profile-a");
        assert(actual && actual->characterId == approval.characterId && actual->save.saveFile == approval.save.saveFile &&
            actual->save.sha256Hex == approval.save.sha256Hex && actual->note == approval.note);
        assert(restarted.Get("profile-b") && restarted.Get("profile-c"));
        const auto concurrent = *restarted.Get("profile-b");
        bool consumedOne = false;
        bool consumedTwo = false;
        std::thread one([&] { consumedOne = restarted.Consume("profile-b", concurrent); });
        std::thread two([&] { consumedTwo = restarted.Consume("profile-b", concurrent); });
        one.join();
        two.join();
        assert(consumedOne != consumedTwo);
        auto expected = *actual;
        assert(expected.generation == 1 && !expected.consumed);
        auto wrong = expected;
        wrong.save.sha256Hex = std::string(64, 'c');
        assert(!restarted.Consume("profile-a", wrong));
        assert(restarted.Consume("profile-a", expected));
        assert(!restarted.Consume("profile-a", expected));
    }
    {
        SaveRecoveryApprovals restarted(path, serverId, [] { return 1002.0; });
        const auto consumed = restarted.Get("profile-a");
        assert(consumed && consumed->consumed && consumed->generation == 1);
        assert(restarted.Approve("profile-a", approval));
        const auto renewed = restarted.Get("profile-a");
        assert(renewed && !renewed->consumed && renewed->generation == 2);
        assert(!restarted.Consume("profile-a", *consumed));
        assert(restarted.Consume("profile-a", *renewed));
    }
    {
        SaveRecoveryApprovals restarted(path, serverId, [] { return 1003.0; });
        const auto consumed = restarted.Get("profile-a");
        assert(consumed && consumed->consumed && consumed->generation == 2);
        assert(!restarted.Consume("profile-a", *consumed));
    }
    const auto quarantinesOf = [](const std::filesystem::path& a_file) {
        std::vector<std::filesystem::path> found;
        const auto prefix = a_file.filename().string() + ".quarantine.";
        for (const auto& entry : std::filesystem::directory_iterator(a_file.parent_path())) {
            if (entry.path().filename().string().rfind(prefix, 0) == 0) found.push_back(entry.path());
        }
        return found;
    };
    const auto readText = [](const std::filesystem::path& a_file) {
        std::ifstream input(a_file, std::ios::binary);
        return std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    };
    const auto writeText = [](const std::filesystem::path& a_file, const std::string& a_text) {
        std::ofstream output(a_file, std::ios::binary | std::ios::trunc);
        output << a_text;
    };
    const auto mainBefore = readText(path);
    {
        SaveRecoveryApprovals wrongServer(path, std::string(32, 'c'), [] { return 1002.0; });
        assert(!wrongServer.Get("profile-a"));
    }
    {
        const auto moved = quarantinesOf(path);
        assert(moved.size() == 1 && readText(moved.front()) == mainBefore);
    }
    const auto scenario = [&](const std::string& a_name) {
        const auto dir = directory / "quarantine" / a_name;
        std::filesystem::create_directories(dir);
        return dir / "approvals.json";
    };
    const auto seeded = [&](const std::filesystem::path& a_file) {
        {
            SaveRecoveryApprovals owner(a_file, serverId, [] { return 1000.0; });
            assert(owner.Approve("profile-a", approval));
        }
        return readText(a_file);
    };
    const auto quarantineCase = [&](const std::string& a_name, const std::string& a_bad) {
        const auto file = scenario(a_name);
        writeText(file, a_bad);
        {
            SaveRecoveryApprovals store(file, serverId, [] { return 1002.0; });
            assert(!store.Get("profile-a"));
        }
        const auto found = quarantinesOf(file);
        assert(found.size() == 1 && readText(found.front()) == a_bad);
        SaveRecoveryApprovals restarted(file, serverId, [] { return 1003.0; });
        assert(!restarted.Get("profile-a") && quarantinesOf(file).size() == 1);
        assert(restarted.Approve("profile-a", approval));
    };
    quarantineCase("corrupt", "invalid");
    const auto seedText = seeded(scenario("seed"));
    {
        const auto file = scenario("foreign");
        writeText(file, seedText);
        {
            SaveRecoveryApprovals foreign(file, std::string(32, 'c'), [] { return 1002.0; });
            assert(!foreign.Get("profile-a"));
        }
        const auto found = quarantinesOf(file);
        assert(found.size() == 1 && readText(found.front()) == seedText);
        SaveRecoveryApprovals again(file, std::string(32, 'c'), [] { return 1003.0; });
        assert(!again.Get("profile-a") && quarantinesOf(file).size() == 1);
    }
    auto missingSection = nlohmann::json::parse(seedText);
    missingSection.erase("saveRecoveryApprovals");
    quarantineCase("missing-section", missingSection.dump(2));
    auto invalidRecord = nlohmann::json::parse(seedText);
    invalidRecord["saveRecoveryApprovals"]["profile-a"]["note"] = nullptr;
    quarantineCase("invalid-approval", invalidRecord.dump(2));
    auto newerSchema = nlohmann::json::parse(seedText);
    newerSchema["schemaVersion"] = 7;
    quarantineCase("newer-schema", newerSchema.dump(2));
    {
        const auto file = scenario("collision");
        writeText(file, "first");
        {
            SaveRecoveryApprovals store(file, serverId, [] { return 1000.0; });
            assert(!store.Get("profile-a"));
        }
        writeText(file, "second");
        {
            SaveRecoveryApprovals store(file, serverId, [] { return 1000.0; });
            assert(!store.Get("profile-a"));
        }
        const auto found = quarantinesOf(file);
        assert(found.size() == 2);
        bool first = false;
        bool second = false;
        for (const auto& item : found) {
            first = first || readText(item) == "first";
            second = second || readText(item) == "second";
        }
        assert(first && second);
    }
    {
        const auto upgrade = directory / "upgrade";
        std::filesystem::create_directories(upgrade);
        const auto durablePath = upgrade / "durable-state.json";
        const auto upgradePath = std::filesystem::path{ durablePath.string() + ".save-recovery" };
        const auto clock = [] { return 1000.0; };
        const auto instance = [&] { return serverId; };
        {
            DurableStateFileStore store(durablePath, clock, instance);
            assert(store.UpdatePlayerState("upgrade-profile", nlohmann::json{ { "type", "playerState" }, { "characterName", "Upgraded" } }));
            SaveRecoveryApprovals approvals(upgradePath, serverId, clock);
            assert(approvals.Approve("upgrade-profile", approval));
        }
        const auto downgrade = [](const std::filesystem::path& a_file) {
            std::ifstream input(a_file);
            auto root = nlohmann::json::parse(input);
            input.close();
            root["schemaVersion"] = 5;
            std::ofstream output(a_file, std::ios::trunc);
            output << root.dump(2);
            return root;
        };
        downgrade(durablePath);
        const auto schema5 = downgrade(upgradePath);
        for (int start = 0; start < 2; ++start) {
            DurableStateFileStore store(durablePath, clock, instance);
            assert(store.GetPlayerState("upgrade-profile")->at("characterName") == "Upgraded");
            SaveRecoveryApprovals approvals(upgradePath, serverId, clock);
            const auto actual = approvals.Get("upgrade-profile");
            assert(actual && actual->characterId == approval.characterId && actual->save.saveFile == approval.save.saveFile &&
                actual->save.sha256Hex == approval.save.sha256Hex && actual->note == approval.note && !actual->consumed);
        }
        std::ifstream recovered(upgradePath);
        assert(nlohmann::json::parse(recovered).at("schemaVersion") == 6);
        std::ifstream backup(std::filesystem::path{ upgradePath.string() + ".bak" });
        assert(nlohmann::json::parse(backup) == schema5);
    }
    std::filesystem::remove_all(directory);
}
