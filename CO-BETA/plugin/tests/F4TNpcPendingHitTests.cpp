#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <format>
#include <mutex>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
namespace Detail { using Clock = std::chrono::steady_clock; }
namespace RE { struct PlayerCharacter { static PlayerCharacter* GetSingleton() { static PlayerCharacter player; return &player; } }; }
namespace RE { struct BGSSaveLoadManager { static BGSSaveLoadManager* GetSingleton() { static BGSSaveLoadManager manager; return &manager; } const char* mostRecentSaveGame{}; std::uint64_t lastSaveTime{}; }; }
namespace F4T::NpcCoop {
struct NpcHitClaim { struct { int npcId{}; } identity; int senderPlayerId{}; };
}
namespace F4T::Networking {
std::uint64_t connection = 1;
bool connected = true;
std::uint64_t GetConnectionGeneration() { return connection; }
bool IsConnectedToServer() { return connected; }
}
namespace F4T::ReplicationRuntime {
constexpr std::uint64_t kNoSession = 0;
std::uint64_t session = 1;
std::uint64_t CurrentGeneration() { return session; }
}
namespace F4T::SurvivalPolicy {
struct ClientState {
    std::uint64_t revision = 1;
    bool known = true;
    bool ready = true;
    bool ambiguous = false;
    std::optional<std::uint64_t> Revision(std::uint64_t connection) { return known && connection == Networking::connection ? std::optional{revision} : std::nullopt; }
    std::optional<std::uint64_t> ReadyRevision(std::uint64_t connection, std::uint64_t session) { return ready && session == ReplicationRuntime::session && session != 0 ? Revision(connection) : std::nullopt; }
    bool LoadAmbiguous() { return ambiguous; }
    std::uint64_t LoadEpoch() { return 1; }
} g_clientState;
}
namespace NpcCoop = F4T::NpcCoop;
namespace NpcSyncPolicy {
enum class HitRoute { kApply, kRetry, kDrop };
bool KeepPendingHit(HitRoute, std::chrono::milliseconds age, std::chrono::seconds maxAge) { return age <= maxAge; }
}
namespace REX { template <class... Args> void WARN(const char*, Args&&...) {} template <class... Args> void INFO(const char*, Args&&...) {} }
#include "pending-hit-constants.h"
#include "pending-hit-struct.h"
std::mutex g_pendingMutex;
std::deque<PendingNpcHit> g_pendingHits;
std::atomic<std::uint64_t> g_capacityDroppedNpcHits{};
std::atomic<std::uint64_t> g_staleDroppedNpcHits{};
std::size_t appliedTotal = 0;
std::vector<int> appliedIds;
bool retry = false;
NpcSyncPolicy::HitRoute ApplyNpcHit(const NpcCoop::NpcHitClaim& claim, RE::PlayerCharacter&) {
    if (retry) return NpcSyncPolicy::HitRoute::kRetry;
    ++appliedTotal;
    appliedIds.push_back(claim.identity.npcId);
    return NpcSyncPolicy::HitRoute::kApply;
}
#include "pending-hit-functions.h"
namespace F4T::Networking {
bool operationAvailable = true;
bool TryRunForConnectionGeneration(std::uint64_t generation, const std::function<void()>& work) {
    if (!operationAvailable || !connected || generation != connection) return false;
    work();
    return true;
}
}
namespace F4T::NpcSync {
inline bool SourceMatchesActor(std::uint32_t sourceFormId, std::uint32_t actorFormId) { return sourceFormId == actorFormId; }
int puppetWork = 0;
int lootWork = 0;
int hostWork = 0;
bool isHost = true;
void Update(const std::function<void(const std::function<void()>&)>& runGameplay) {
    runGameplay([] { ++puppetWork; ApplyPendingNpcHits(); });
    ++lootWork;
    if (isHost) runGameplay([] { ++hostWork; });
}
}
namespace F4T::SurvivalEnforcer {
bool ready = true;
bool invalidateAfterWork = false;
bool Tick(std::uint64_t, std::uint64_t, bool loaded, const std::function<void()>& work) {
    if (!ready || !loaded) return false;
    work();
    return !invalidateAfterWork;
}
}
struct Runtime { std::uint64_t CurrentGeneration() const { return F4T::ReplicationRuntime::session; } } g_runtime;
std::uint64_t CurrentGeneration() { return F4T::ReplicationRuntime::session; }
bool worldLoaded = true;
bool IsInWorld() { return worldLoaded; }
bool IsSurvivalWorldReady() { return worldLoaded; }
void RunHealth(bool connected) {
#include "runtime-health-call.h"
}
namespace F4T::NpcCombatCapture {
struct PendingDamage {
    struct { struct { int npcId{}; } identity; int targetPlayerId{}, sequence{}; float damage{}; int weaponFormId{}; } claim;
    std::uint64_t sessionGeneration{}, connectionGeneration{}, policyRevision{};
};
std::vector<PendingDamage> pending;
std::vector<PendingDamage> GetAndClearPendingNpcDamage() { return std::exchange(pending, {}); }
}
namespace F4T::RangedCombatCapture {
enum class HitKind { kPlayer, kNpc };
struct Hit {
    int targetPlayerId{}, sequence{}, attackSequence{}; float damage{}; int weaponFormId{};
    std::uint64_t sessionGeneration{}, connectionGeneration{}, policyRevision{};
    HitKind kind{};
    struct { int npcId{}; } npc;
};
std::vector<Hit> pending;
std::vector<Hit> GetAndClearPendingCombatHits() { return std::exchange(pending, {}); }
}
namespace F4T::DebugLogging { template <typename... Args> void Log(const char*, const char*, Args&&...) {} }
namespace F4T::Networking {
int sent = 0;
std::uint64_t sentConnection = 0;
template <typename Identity> bool SendNpcDamagePacket(const Identity&, int, int, float, int, std::uint64_t connection) { ++sent; sentConnection = connection; return true; }
template <typename... Args> bool SendNpcHitPacket(Args&&...) { ++sent; return true; }
template <typename... Args> bool SendCombatHitPacket(Args&&...) { ++sent; return true; }
}
namespace F4T::Networking {
std::vector<std::string> animations;
bool SendPlayerAnimEventPacket(const std::string& name) { animations.push_back(name); return true; }
}
std::mutex g_outboundMutex;
#include "animation-event.h"
namespace F4T::ShotReplay {
struct Direction { double x{}, y{}, z{}; };
std::optional<Direction> NormalizeDirection(const Direction& direction) { return direction; }
}
namespace F4T::Networking {
int aimsSent{};
bool SendProjectileAimPacket(double, double, double) { ++aimsSent; return true; }
}
namespace AimTest {
constexpr std::size_t kMaximumQueued = 64;
std::mutex g_aimOutboundMutex;
#include "projectile-aim.h"
}
namespace F4T::FurnitureSync {
enum class Action { Enter, Exit };
const char* ActionName(Action action) { return action == Action::Enter ? "enter" : "exit"; }
}
namespace F4T::LocalPlayerTransformSend { bool ready = true; bool HasSentTransformThisConnection() { return ready; } }
namespace F4T::Networking {
std::vector<std::string> seats;
bool seatSendSucceeds = true;
bool SendFurnitureUsePacket(const char* action) { if (!seatSendSucceeds) return false; seats.emplace_back(action); return true; }
}
bool g_sentSeated = false;
std::uint64_t g_sentGeneration = 0;
bool localSeated = false;
bool LocalPlayerSeated() { return localSeated; }
#include "furniture-publish.h"
namespace F4T::SessionWorldState { bool IsGameplayWorldReady(const RE::PlayerCharacter&) { return worldLoaded; } }
namespace F4T::SessionSaveGuard {
enum class ArmOwner { kFirstJoin };
bool exiting = false;
bool IsSessionExitPending() { return exiting; }
int disarms = 0;
void DisarmInternalSave(const char*) { ++disarms; }
}
namespace F4T::SessionSaveQueue {
int arms = 0;
bool StartQueuedSave(F4T::SessionSaveGuard::ArmOwner, const char*, std::uint64_t) { ++arms; return true; }
void StopQueue(F4T::SessionSaveGuard::ArmOwner) { --arms; }
}
bool g_profileSaveQueued = false;
#include "F4TFirstJoinAttempt.h"
std::optional<F4T::FirstJoinAttempt::Token> g_firstJoinAttempt;
#include "first-join-save.h"
namespace F4T::SessionLaunch {
enum class LaunchOutcome { Failed, Pending };
using JoinCompletion = std::function<void(bool, std::string)>;
}
namespace F4T::ServerProfileStore {
bool holdSucceeds = false;
int holds = 0;
std::optional<std::uint32_t> heldSlot;
std::optional<std::uint32_t> ConnectedCharacterSlot() { return 2; }
bool RecordFirstJoinHold(const std::string&, std::uint64_t, const std::string&, const std::string&, std::optional<std::uint32_t> slot) { ++holds; heldSlot = slot; return holdSucceeds; }
int stages = 0;
bool StageFirstJoinRecovery(const std::string&, const std::string&, const std::string&, const std::string&,
    const std::string&, const std::string&, std::uint64_t) { ++stages; return true; }
}
namespace F4T::SaveProfileLogic { bool EqualsIgnoreCase(const std::string& a, const std::string& b) { return a == b; } }
namespace F4T::SaveLineageSession {
std::optional<std::string> testHash = "matching-hash";
std::optional<std::string> HashSaveForRecovery(std::string_view) { return testHash; }
#include "stage-first-join.h"
}
namespace F4T::SessionCharacterSetupFlow {
bool startSucceeds = false;
int starts = 0;
bool StartVanillaNewGame() { ++starts; return startSucceeds; }
void ShowReturnToMenuAlert() {}
}
namespace F4T::JoinFlow { void EndForFirstTimeJoin() {} }
namespace FirstJoinStartTest {
namespace ServerProfileStore = F4T::ServerProfileStore;
bool g_newGameMessageSeen{};
std::string g_activeProfileServerKey;
F4T::SessionLaunch::JoinCompletion g_pendingJoinCompletion;
int queued = 0;
void QueueFirstTimeJoin(const std::string&) { ++queued; }
#include "first-join-start.h"
}
namespace F4T::SaveProfileLogic {
struct AttestedSave { std::string saveFile, sha256Hex; std::uint64_t attestedAt{}; };
}
namespace F4T::SaveLineageStore {
F4T::SaveProfileLogic::AttestedSave recorded;
bool persist = true;
bool RecordAttestedSave(const std::string&, const F4T::SaveProfileLogic::AttestedSave& save) { recorded = save; return persist; }
}
namespace SaveAttestationTest {
std::string announcedHash;
bool announce = true;
int announcements = 0;
int reads = 0;
std::optional<std::string> HashSaveFile(const std::string&) { ++reads; return "later-file-hash"; }
void LogWarning(const std::string&) {}
bool AnnounceSaveAttestation(const std::string&, const F4T::SaveProfileLogic::AttestedSave& save, std::optional<std::uint64_t> = std::nullopt) { ++announcements; announcedHash = save.sha256Hex; return announce; }
#include "save-attestation.h"
}
namespace LootAdmissionTest {
struct Identity { std::uint32_t npcId{}, sourceFormId{}, authorityEpoch{}; };
struct Request { Identity identity; std::uint32_t takerPlayerId{}; std::string operationId; std::uint32_t itemId{}, requested{}, expiresInMs{}; };
struct Queued { Request request; std::uint64_t receivedMs{}; };
struct Result { bool accepted{}; std::string operationId; std::uint32_t count{}; std::string reason; };
struct GrantAwaitingAck { Identity identity; std::uint32_t itemId{}, count{}, authorityPlayerId{}; std::uint64_t grantedMs{}; };
std::map<std::pair<std::uint32_t, std::string>, GrantAwaitingAck> g_grantsAwaitingAck;
constexpr std::size_t kMaximumGrantsAwaitingAck = 64;
std::uint64_t nowMilliseconds = 1000;
std::uint64_t NowMilliseconds() { return nowMilliseconds; }
namespace NpcLootSync {
#include "loot-expiry.h"
}
struct Scope { std::uint64_t epoch{}; };
struct Container { std::uint32_t GetFormID() const { return 8; } } lootContainer;
struct Cache {
    std::map<std::pair<std::uint32_t, std::string>, Result> results;
    std::optional<Result> Find(std::uint32_t taker, const std::string& id) { auto it = results.find({taker, id}); return it == results.end() ? std::nullopt : std::optional{it->second}; }
    void Remember(std::uint32_t taker, const std::string& id, const Result& result) { results[{taker, id}] = result; }
} g_results;
std::vector<Result> replies;
int mutations = 0;
void EnqueueOutboundTakeResult(Result result) { replies.push_back(std::move(result)); }
Result MakeTakeResult(const std::string& id, std::uint32_t, std::uint32_t, std::uint32_t, bool accepted, const char* reason, std::uint32_t) { return { accepted, id, 0, reason }; }
Container* FindLootContainer(std::uint32_t, const Scope&) { return &lootContainer; }
bool IsLootableContainer(Container&) { return true; }
bool IsContainerInScope(Container&, const Scope&) { return true; }
Result GrantTake(Container&, const Identity&, std::uint32_t, const std::string& id, std::uint32_t, std::uint32_t, std::uint32_t) { ++mutations; return { true, id, 0, "" }; }
void Process(const std::vector<Request>& a_requests, const Scope& a_scope, const std::function<void(const std::function<void()>&)>& a_runGameplay, std::uint64_t receivedMs = 1000) {
    const std::optional<std::uint32_t> localPlayerId = 7;
    std::vector<Queued> requests;
    for (const auto& request : a_requests) requests.push_back({ request, receivedMs });
#include "loot-request-loop.h"
}
}
struct Coordinator { bool Admits(std::uint64_t generation) const { return generation != 0 && generation == F4T::ReplicationRuntime::session; } };
#include "publish-hits.h"
namespace COMMONWEALTH_ONLINE_UI_API {
struct AlertRequest { const char* id{}; const char* category{}; const char* title{}; const char* message{}; const char* tone{}; int durationMs{}; };
}
namespace F4T::UIClient {
int notices = 0;
int dismissals = 0;
std::string lastMessage;
bool ShowAlert(COMMONWEALTH_ONLINE_UI_API::AlertRequest* request, void*, void*, void*, int) { ++notices; lastMessage = request->message; return true; }
bool DismissAlert(const char*) { ++dismissals; return true; }
struct API { decltype(&ShowAlert) ShowAlert = F4T::UIClient::ShowAlert; decltype(&DismissAlert) DismissAlert = F4T::UIClient::DismissAlert; } api;
API* GetCachedUI() { return &api; }
}
std::uint64_t g_lastNotified{};
bool g_lastLoadAmbiguous{};
bool g_dismissPending{};
std::atomic<std::uint64_t> g_failureConnection{};
std::atomic<std::uint64_t> g_failureEpoch{};
#include "survival-notice.h"
int main() {
    using namespace F4T;
    {
        using namespace SaveAttestationTest;
        assert(AttestFirstJoinSave("server", "save", "verified-hash"));
        assert(reads == 0 && SaveLineageStore::recorded.sha256Hex == "verified-hash" && announcedHash == "verified-hash");
        assert(RecordActiveSave("server", "save"));
        assert(reads == 1 && SaveLineageStore::recorded.sha256Hex == "later-file-hash" && announcedHash == "later-file-hash");
        SaveLineageStore::persist = false;
        const auto beforeFailure = announcements;
        assert(!AttestFirstJoinSave("server", "save", "verified-hash"));
        assert(announcements == beforeFailure);
        SaveLineageStore::persist = true;
        announce = false;
        assert(!AttestFirstJoinSave("server", "save", "verified-hash"));
        assert(announcements == beforeFailure + 1);
        announce = true;
    }
    {
        using namespace LootAdmissionTest;
        const std::vector<Request> requests{ { { 3, 8, 1 }, 5, "operation", 9, 1, 5000 } };
        Process(requests, Scope{1}, [](const auto&) {});
        assert(mutations == 0 && replies.size() == 1 && !replies.back().accepted);
        Process(requests, Scope{1}, [](const auto& work) { work(); });
        assert(mutations == 0 && replies.size() == 2 && !replies.back().accepted);
        auto next = requests;
        next.front().operationId = "next";
        Process(next, Scope{1}, [](const auto& work) { work(); });
        assert(mutations == 1 && replies.size() == 3 && replies.back().accepted);
        Process(next, Scope{1}, [](const auto& work) { work(); });
        assert(mutations == 1 && replies.size() == 4 && replies.back().accepted);
        auto expiry = requests;
        expiry.front().operationId = "expiry-boundary";
        nowMilliseconds = 3500;
        Process(expiry, Scope{1}, [](const auto& work) { work(); });
        assert(mutations == 2 && replies.back().accepted);
        nowMilliseconds = 3501;
        int callbacks = 0;
        Process(expiry, Scope{1}, [&](const auto& work) { ++callbacks; work(); });
        assert(mutations == 2 && callbacks == 0 && replies.back().accepted);
        expiry.front().operationId = "expired";
        Process(expiry, Scope{1}, [&](const auto& work) { ++callbacks; work(); });
        assert(mutations == 2 && callbacks == 0 && !replies.back().accepted && replies.back().reason == "expired");
        nowMilliseconds = 1000;
        Process(expiry, Scope{1}, [&](const auto& work) { ++callbacks; work(); });
        assert(mutations == 2 && callbacks == 0 && !replies.back().accepted && replies.back().reason == "expired");
        expiry.front().operationId = "zero-expiry";
        expiry.front().expiresInMs = 0;
        Process(expiry, Scope{1}, [&](const auto& work) { ++callbacks; work(); });
        assert(mutations == 2 && callbacks == 0 && !replies.back().accepted && replies.back().reason == "expired");
    }
    SurvivalEnforcer::ready = false;
    assert(!QueueProfileSave() && SessionSaveQueue::arms == 0);
    SurvivalEnforcer::ready = true;
    Networking::operationAvailable = false;
    assert(!QueueProfileSave() && SessionSaveQueue::arms == 0);
    Networking::operationAvailable = true;
    SessionSaveGuard::exiting = true;
    assert(QueueProfileSave() == false && SessionSaveQueue::arms == 0);
    SessionSaveGuard::exiting = false;
    SurvivalEnforcer::invalidateAfterWork = true;
    assert(!QueueProfileSave() && SessionSaveQueue::arms == 0 && !g_profileSaveQueued && SessionSaveGuard::disarms == 1);
    SurvivalEnforcer::invalidateAfterWork = false;
    assert(QueueProfileSave() == true && SessionSaveQueue::arms == 1 && g_profileSaveQueued);
    {
        using namespace FirstJoinStartTest;
        std::string error;
        g_activeProfileServerKey = "server";
        assert(StartFirstTimeJoin("server", {}, error) == F4T::SessionLaunch::LaunchOutcome::Failed);
        assert(F4T::ServerProfileStore::holds == 1 && F4T::SessionCharacterSetupFlow::starts == 0 && queued == 0);
        assert(g_activeProfileServerKey.empty() && !error.empty());
        F4T::ServerProfileStore::holdSucceeds = true;
        F4T::SessionCharacterSetupFlow::startSucceeds = true;
        assert(StartFirstTimeJoin("server", {}, error) == F4T::SessionLaunch::LaunchOutcome::Pending);
        assert(F4T::ServerProfileStore::holds == 2 && F4T::SessionCharacterSetupFlow::starts == 1 && queued == 1);
        assert(F4T::ServerProfileStore::heldSlot == 2u);
    }
    {
        using F4T::SaveLineageSession::StageApprovedFirstJoin;
        const auto stage = [](std::uint64_t connection, const std::string& selected, const std::string& hash) {
            return StageApprovedFirstJoin(connection, "server", "world", selected, "profile#1", "character", "save", hash, 1);
        };
        assert(!stage(2, "profile#1", "matching-hash"));
        assert(!stage(1, "profile#2", "matching-hash"));
        assert(!stage(1, "profile#1", "wrong-hash"));
        F4T::Networking::operationAvailable = false;
        assert(!stage(1, "profile#1", "matching-hash"));
        assert(F4T::ServerProfileStore::stages == 0);
        F4T::Networking::operationAvailable = true;
        assert(stage(1, "profile#1", "matching-hash"));
        assert(F4T::ServerProfileStore::stages == 1);
    }
    g_outbound.push_back({ "valid", 1, 1, 1 });
    g_outbound.push_back({ "old connection", 2, 1, 1 });
    g_outbound.push_back({ "old session", 1, 2, 1 });
    g_outbound.push_back({ "old policy", 1, 1, 2 });
    FlushOutbound();
    assert(Networking::animations == std::vector<std::string>{ "valid" } && g_outbound.empty());
    Networking::connected = false;
    g_outbound.push_back({ "disconnected", 1, 1, 1 });
    FlushOutbound();
    assert(Networking::animations.size() == 1 && g_outbound.empty());
    Networking::connected = true;
    AimTest::QueueAimForSend({1, 0, 0});
    assert(AimTest::g_aimOutbound.size() == 1);
    AimTest::FlushOutbound();
    assert(Networking::aimsSent == 1 && AimTest::g_aimOutbound.empty());
    AimTest::QueueAimForSend({1, 0, 0});
    ++SurvivalPolicy::g_clientState.revision;
    AimTest::FlushOutbound();
    assert(Networking::aimsSent == 1 && AimTest::g_aimOutbound.empty());
    SurvivalPolicy::g_clientState.ready = false;
    AimTest::QueueAimForSend({1, 0, 0});
    assert(AimTest::g_aimOutbound.empty());
    SurvivalPolicy::g_clientState.ready = true;
    AimTest::QueueAimForSend({1, 0, 0});
    AimTest::DiscardOutbound();
    assert(AimTest::g_aimOutbound.empty());
    SurvivalPolicy::g_clientState.revision = 1;
    localSeated = true;
    PublishLocal();
    PublishLocal();
    assert(Networking::seats == std::vector<std::string>{ "enter" });
    localSeated = false;
    PublishLocal();
    assert((Networking::seats == std::vector<std::string>{ "enter", "exit" }));
    Networking::connected = false;
    PublishLocal();
    Networking::connection = 2;
    Networking::connected = true;
    localSeated = true;
    PublishLocal();
    assert(Networking::seats.size() == 3 && Networking::seats.back() == "enter");
    Networking::connection = 3;
    LocalPlayerTransformSend::ready = false;
    PublishLocal();
    assert(Networking::seats.size() == 3);
    LocalPlayerTransformSend::ready = true;
    Networking::seatSendSucceeds = false;
    PublishLocal();
    assert(Networking::seats.size() == 3);
    Networking::seatSendSucceeds = true;
    PublishLocal();
    assert(Networking::seats.size() == 4 && Networking::seats.back() == "enter");
    Networking::connection = 1;
    assert(EnqueueNpcHit({}));
    ApplyPendingNpcHits();
    assert(appliedTotal == 1);
    assert(EnqueueNpcHit({}));
    ++ReplicationRuntime::session;
    ApplyPendingNpcHits();
    assert(appliedTotal == 1 && g_pendingHits.empty());
    assert(EnqueueNpcHit({}));
    ++SurvivalPolicy::g_clientState.revision;
    ApplyPendingNpcHits();
    assert(appliedTotal == 1 && g_pendingHits.empty());
    assert(EnqueueNpcHit({}));
    ++Networking::connection;
    ApplyPendingNpcHits();
    assert(appliedTotal == 1 && g_pendingHits.empty());
    assert(EnqueueNpcHit({}));
    retry = true;
    ApplyPendingNpcHits();
    assert(g_pendingHits.size() == 1);
    retry = false;
    ApplyPendingNpcHits();
    assert(appliedTotal == 2 && g_pendingHits.empty());
    assert(EnqueueNpcHit({}));
    retry = true;
    ApplyPendingNpcHits();
    ++Networking::connection;
    retry = false;
    ApplyPendingNpcHits();
    assert(appliedTotal == 2 && g_pendingHits.empty());
    assert(EnqueueNpcHit({}));
    g_pendingHits.front().receivedAt -= kMaximumNpcHitAge + 1s;
    ApplyPendingNpcHits();
    assert(appliedTotal == 2 && g_pendingHits.empty());
    appliedIds.clear();
    for (std::size_t i = 0; i < kMaximumPendingNpcHits; ++i) {
        NpcCoop::NpcHitClaim claim{};
        claim.identity.npcId = static_cast<int>(i);
        assert(EnqueueNpcHit(claim));
    }
    assert(!EnqueueNpcHit({}));
    assert(g_capacityDroppedNpcHits == 1);
    ApplyPendingNpcHits();
    assert(appliedTotal == 2 + kMaximumNpcHitsPerTick);
    assert(g_pendingHits.size() == kMaximumPendingNpcHits - kMaximumNpcHitsPerTick);
    assert(EnqueueNpcHit({}));
    ApplyPendingNpcHits();
    for (std::size_t i = 0; i < 2 * kMaximumNpcHitsPerTick; ++i) assert(appliedIds[i] == static_cast<int>(i));
    g_pendingHits.clear();
    Networking::connected = false;
    assert(!EnqueueNpcHit({}));
    Networking::connected = true;
    ReplicationRuntime::session = 0;
    assert(!EnqueueNpcHit({}));
    ReplicationRuntime::session = 1;
    SurvivalPolicy::g_clientState.known = false;
    assert(!EnqueueNpcHit({}));
    SurvivalPolicy::g_clientState.known = true;
    SurvivalPolicy::g_clientState.ready = false;
    assert(!EnqueueNpcHit({}));
    SurvivalPolicy::g_clientState.ready = true;
    const auto previousTotal = appliedTotal;
    assert(EnqueueNpcHit({}));
    SurvivalEnforcer::ready = false;
    RunHealth(true);
    assert(appliedTotal == previousTotal && g_pendingHits.size() == 1 && NpcSync::puppetWork == 0 && NpcSync::hostWork == 0 && NpcSync::lootWork == 1);
    SurvivalEnforcer::ready = true;
    worldLoaded = false;
    RunHealth(true);
    assert(appliedTotal == previousTotal && g_pendingHits.size() == 1);
    worldLoaded = true;
    Networking::operationAvailable = false;
    RunHealth(true);
    assert(appliedTotal == previousTotal && g_pendingHits.size() == 1);
    Networking::operationAvailable = true;
    RunHealth(true);
    assert(appliedTotal == previousTotal + 1 && g_pendingHits.empty() && NpcSync::puppetWork == 1 && NpcSync::hostWork == 1 && NpcSync::lootWork == 4);
    NpcSync::isHost = false;
    RunHealth(true);
    assert(NpcSync::puppetWork == 2 && NpcSync::hostWork == 1 && NpcSync::lootWork == 5);
    Coordinator coordinator;
    NpcCombatCapture::PendingDamage damage{};
    damage.sessionGeneration = ReplicationRuntime::session;
    damage.connectionGeneration = Networking::connection;
    damage.policyRevision = SurvivalPolicy::g_clientState.revision;
    RangedCombatCapture::Hit hit{};
    hit.sessionGeneration = damage.sessionGeneration;
    hit.connectionGeneration = damage.connectionGeneration;
    hit.policyRevision = damage.policyRevision;
    NpcCombatCapture::pending.push_back(damage);
    RangedCombatCapture::pending.push_back(hit);
    hit.kind = RangedCombatCapture::HitKind::kNpc;
    RangedCombatCapture::pending.push_back(hit);
    SendPendingCombatHits(coordinator);
    assert(Networking::sent == 3 && Networking::sentConnection == damage.connectionGeneration);
    NpcCombatCapture::pending.push_back(damage);
    RangedCombatCapture::pending.push_back(hit);
    ++SurvivalPolicy::g_clientState.revision;
    SendPendingCombatHits(coordinator);
    assert(Networking::sent == 3);
    damage.policyRevision = SurvivalPolicy::g_clientState.revision;
    hit.policyRevision = damage.policyRevision;
    NpcCombatCapture::pending.push_back(damage);
    RangedCombatCapture::pending.push_back(hit);
    ++Networking::connection;
    SendPendingCombatHits(coordinator);
    assert(Networking::sent == 3);
    damage.connectionGeneration = Networking::connection;
    hit.connectionGeneration = damage.connectionGeneration;
    NpcCombatCapture::pending.push_back(damage);
    RangedCombatCapture::pending.push_back(hit);
    ++ReplicationRuntime::session;
    SendPendingCombatHits(coordinator);
    assert(Networking::sent == 3);
    NotifyUnavailable(Networking::connection);
    assert(UIClient::notices == 0);
    g_failureConnection = Networking::connection;
    g_failureEpoch = 2;
    NotifyUnavailable(Networking::connection);
    assert(UIClient::notices == 0);
    g_failureEpoch = 1;
    NotifyUnavailable(Networking::connection);
    assert(UIClient::notices == 1 && UIClient::lastMessage.find("Very Hard") != std::string::npos);
    NotifyUnavailable(Networking::connection);
    assert(UIClient::notices == 1);
    SurvivalPolicy::g_clientState.ambiguous = true;
    NotifyUnavailable(Networking::connection);
    assert(UIClient::notices == 2 && UIClient::lastMessage.find("before restarting") != std::string::npos);
    UIClient::api.DismissAlert = nullptr;
    ClearNotice();
    assert(g_dismissPending && UIClient::dismissals == 0);
    NotifyUnavailable(Networking::connection);
    assert(UIClient::notices == 3 && !g_dismissPending);
    UIClient::api.DismissAlert = UIClient::DismissAlert;
    ClearNotice();
    assert(UIClient::dismissals == 1);
    NotifyUnavailable(Networking::connection);
    UIClient::api.DismissAlert = nullptr;
    ClearNotice();
    UIClient::api.DismissAlert = UIClient::DismissAlert;
    ClearNotice();
    assert(UIClient::dismissals == 2 && !g_dismissPending && g_lastNotified == 0);
}
