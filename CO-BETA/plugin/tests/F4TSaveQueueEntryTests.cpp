#include "F4TSessionSaveGuardPolicy.h"

#include <cassert>
#include <chrono>
#include <mutex>
#include <optional>
#include <string_view>

namespace F4T::SessionSaveGuard {
bool exiting = true;
int arms = 0;
bool IsSessionExitPending() { return exiting; }
void ArmInternalSave(std::string_view, ArmOwner) { ++arms; }
std::string_view OwnerName(ArmOwner) { return "save"; }
}
namespace F4T::PowerArmorSync {
struct Gate { int cancels{}; void CancelPendingSave() { ++cancels; } } gate;
Gate& GetFrameSaveGate() { return gate; }
}
namespace QueueTest {
using namespace F4T::SessionSaveGuard;
namespace SessionSaveGuard = F4T::SessionSaveGuard;
struct QueueState { bool wanted{}; unsigned stablePolls{}, attempts{}; std::optional<std::chrono::steady_clock::time_point> queuedAt; };
struct Entry { bool result{}; unsigned count{}; };
std::mutex g_queueMutex;
QueueState g_quitQueue, g_firstJoinQueue;
Entry g_firstJoinEntry;
QueueState& StateFor(ArmOwner owner) { return owner == ArmOwner::kQuit ? g_quitQueue : g_firstJoinQueue; }
template <typename... Args> void Trace(const char*, Args&&...) {}
#include "save-queue-note.h"
}
int main() {
    using namespace QueueTest;
    g_firstJoinQueue = {true, 3, 1, std::chrono::steady_clock::now()};
    NoteEntered(ArmOwner::kFirstJoin, false);
    assert(!g_firstJoinQueue.wanted && !g_firstJoinQueue.queuedAt && g_firstJoinQueue.stablePolls == 0);
    assert(!g_firstJoinEntry.result && g_firstJoinEntry.count == 0);
    assert(F4T::SessionSaveGuard::arms == 0 && F4T::PowerArmorSync::gate.cancels == 1);
    g_quitQueue = {true, 3, 1, std::chrono::steady_clock::now()};
    NoteEntered(ArmOwner::kQuit, false);
    assert(g_quitQueue.wanted && !g_quitQueue.queuedAt && F4T::SessionSaveGuard::arms == 1);
    g_quitQueue = {true, 3, kMaxQueueAttempts, std::chrono::steady_clock::now()};
    NoteEntered(ArmOwner::kQuit, false);
    assert(!g_quitQueue.wanted && F4T::SessionSaveGuard::arms == 2);
    g_firstJoinQueue = {true, 3, 1, std::chrono::steady_clock::now()};
    NoteEntered(ArmOwner::kFirstJoin, true);
    assert(!g_firstJoinQueue.wanted && g_firstJoinEntry.result && g_firstJoinEntry.count == 1);
}
