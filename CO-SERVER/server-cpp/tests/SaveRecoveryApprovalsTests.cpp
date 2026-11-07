#include "SaveRecoveryApprovals.h"
#include "SaveRecoveryAdmin.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

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
    bool rejected = false;
    try { SaveRecoveryApprovals wrongServer(path, std::string(32, 'c'), [] { return 1002.0; }); }
    catch (const std::exception&) { rejected = true; }
    assert(rejected);
    nlohmann::json root;
    { std::ifstream input(path); root = nlohmann::json::parse(input); }
    {
        auto missingSection = root;
        missingSection.erase("saveRecoveryApprovals");
        std::ofstream output(path, std::ios::trunc);
        output << missingSection;
    }
    rejected = false;
    try { SaveRecoveryApprovals missing(path, serverId, [] { return 1002.0; }); }
    catch (const std::exception&) { rejected = true; }
    assert(rejected);
    {
        root["saveRecoveryApprovals"]["profile-a"]["note"] = nullptr;
        std::ofstream output(path, std::ios::trunc);
        output << root;
    }
    rejected = false;
    try { SaveRecoveryApprovals malformed(path, serverId, [] { return 1002.0; }); }
    catch (const std::exception&) { rejected = true; }
    assert(rejected);
    { std::ofstream output(path, std::ios::trunc); output << "invalid"; }
    rejected = false;
    try { SaveRecoveryApprovals corrupt(path, serverId, [] { return 1002.0; }); }
    catch (const std::exception&) { rejected = true; }
    assert(rejected);
    std::filesystem::remove_all(directory);
}
