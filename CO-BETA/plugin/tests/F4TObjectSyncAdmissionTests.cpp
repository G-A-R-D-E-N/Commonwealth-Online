#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
#include "F4TFormIdentity.h"
#include "F4TObjectState.h"
#include "object-sync-constants.h"

inline int t_activations{};
inline int t_lockWrites{};

namespace RE {
enum class BSEventNotifyControl : std::int32_t { kContinue, kStop };
template <class Event> class BSTEventSource;
template <class Event> class BSTEventSink {
public:
    virtual ~BSTEventSink() = default;
    virtual BSEventNotifyControl ProcessEvent(const Event& a_event, BSTEventSource<Event>* a_source) = 0;
};
template <class Event> class BSTEventSource {
public:
    void RegisterSink(BSTEventSink<Event>* a_sink) { sinks.push_back(a_sink); }
    std::vector<BSTEventSink<Event>*> sinks;
};

template <class T> class NiPointer {
public:
    using element_type = T;
    NiPointer(T* a_ptr = nullptr) : _ptr(a_ptr) {}
    element_type* get() const noexcept { return _ptr; }
    element_type* operator->() const noexcept { return _ptr; }
    explicit operator bool() const noexcept { return _ptr != nullptr; }
private:
    T* _ptr;
};

template <class T> class BSTSmartPointer {
public:
    T* get() const noexcept { return _ptr; }
    T* _ptr{};
};

struct NiPoint3 { float x{ 0.0F }; float y{ 0.0F }; float z{ 0.0F }; };

enum class ENUM_FORM_ID : std::int32_t { kACTI, kDOOR, kTERM };

enum class EXTRA_DATA_TYPE : std::int32_t { kTeleport };

struct BSExtraData {};
struct ExtraDataList {
    BSExtraData* GetByType(EXTRA_DATA_TYPE a_type) const noexcept { return a_type == EXTRA_DATA_TYPE::kTeleport && teleport ? &marker : nullptr; }
    bool teleport{};
    mutable BSExtraData marker;
};

class TESObjectREFR;
enum class LOCK_LEVEL : std::int32_t { kNone };
class REFR_LOCK {
public:
    enum class Flags : std::uint8_t { kLocked = 1 };
    LOCK_LEVEL GetLockLevel(TESObjectREFR*) { return static_cast<LOCK_LEVEL>(level); }
    void SetLocked(bool a_locked)
    {
        flags = a_locked ? static_cast<std::uint8_t>(flags | 1) : static_cast<std::uint8_t>(flags & ~1);
        ++t_lockWrites;
    }
    std::uint8_t flags{};
    std::int32_t level{};
};

class TESForm;
inline std::map<std::uint32_t, TESForm*>& World()
{
    static std::map<std::uint32_t, TESForm*> world;
    return world;
}
class TESForm {
public:
    virtual ~TESForm() = default;
    std::uint32_t GetFormID() const noexcept { return recordId; }
    ENUM_FORM_ID GetFormType() const noexcept { return formType; }
    template <class T> static T* GetFormByID(std::uint32_t a_id)
    {
        const auto found = World().find(a_id);
        return found == World().end() ? nullptr : static_cast<T*>(found->second);
    }
    std::uint32_t recordId{};
    ENUM_FORM_ID formType{ ENUM_FORM_ID::kACTI };
};
class TESBoundObject : public TESForm {};
class TESWorldSpace : public TESForm {};
class TESObjectCELL : public TESForm {
public:
    TESWorldSpace* worldSpace{};
};

class TESObjectREFR : public TESForm {
public:
    TESBoundObject* GetObjectReference() const noexcept { return objectReference; }
    TESObjectCELL* GetParentCell() const noexcept { return parentCell; }
    NiPoint3 GetPosition() const noexcept { return location; }
    REFR_LOCK* GetLock() { return hasLock ? &lock : nullptr; }
    bool ActivateRef(TESObjectREFR*, TESBoundObject*, std::int32_t, bool, bool, bool)
    {
        ++t_activations;
        if (formType == ENUM_FORM_ID::kDOOR) {
            open = !open;
        }
        return true;
    }
    TESBoundObject* objectReference{};
    BSTSmartPointer<ExtraDataList> extraList;
    TESObjectCELL* parentCell{};
    NiPoint3 location;
    ExtraDataList extras;
    REFR_LOCK lock;
    bool hasLock{};
    bool open{};
};

class PlayerCharacter : public TESObjectREFR {
public:
    static PlayerCharacter* GetSingleton();
};
inline PlayerCharacter t_playerObject;
inline PlayerCharacter* t_player = &t_playerObject;
inline PlayerCharacter* PlayerCharacter::GetSingleton() { return t_player; }

class BGSOpenCloseForm {
public:
    enum class OPEN_STATE : std::int32_t { kNone = 0, kOpen = 1, kOpening = 2, kClosed = 3, kClosing = 4 };
    static OPEN_STATE GetOpenState(const TESObjectREFR* a_ref) { return a_ref->open ? OPEN_STATE::kOpen : OPEN_STATE::kClosed; }
};

class TESFile {
public:
    std::string_view GetFilename() const noexcept { return filename; }
    std::string filename{ "Test.esp" };
};

class TESDataHandler {
public:
    static TESDataHandler* GetSingleton()
    {
        static TESDataHandler handler;
        return &handler;
    }
    template <class T> T* LookupForm(std::uint32_t a_rawId, std::string_view a_modName)
    {
        return a_modName == "Test.esp" ? TESForm::GetFormByID<T>((std::uint32_t{ 1 } << 24) | a_rawId) : nullptr;
    }
    const TESFile* LookupLoadedModByIndex(std::uint8_t a_index) { return a_index == 1 ? &file : nullptr; }
    const TESFile* LookupLoadedLightModByIndex(std::uint16_t) { return nullptr; }
    TESFile file;
};

class TESActivateEvent {
public:
    static BSTEventSource<TESActivateEvent>* GetEventSource()
    {
        static BSTEventSource<TESActivateEvent> source;
        return &source;
    }
    NiPointer<TESObjectREFR> objectActivated;
    NiPointer<TESObjectREFR> actionRef;
};
}

namespace F4T::Networking {
inline std::uint64_t t_connection{ 1 };
inline std::vector<nlohmann::json> t_activateSends;
inline std::vector<nlohmann::json> t_lockSends;
inline int t_warnings{};
std::uint64_t GetConnectionGeneration() { return t_connection; }
void SendObjectActivateRequest(const nlohmann::json& a_request) { t_activateSends.push_back(a_request); }
void SendObjectLockRequest(const nlohmann::json& a_request) { t_lockSends.push_back(a_request); }
}
namespace F4T::Networking::Detail {
void LogThrottledWarning(const char*, const char*) { ++F4T::Networking::t_warnings; }
}
namespace F4T::SessionLaunch {
inline bool t_canPublish{ true };
bool CanPublishGameplayTransform() { return t_canPublish; }
}
namespace F4T::JoinFlow {
inline bool t_canPublish{ true };
bool CanPublishGameplayTransform() { return t_canPublish; }
}
namespace F4T::ReplicationRuntime {
inline std::uint64_t t_session{ 1 };
std::uint64_t CurrentGeneration() noexcept { return t_session; }
}
namespace F4T::SurvivalPolicy {
struct ClientStub {
    std::optional<std::uint64_t> ready{ 7 };
    std::optional<std::uint64_t> revision{ 7 };
    std::optional<std::uint64_t> ReadyRevision(std::uint64_t, std::uint64_t) { return ready; }
    std::optional<std::uint64_t> Revision(std::uint64_t) { return revision; }
} g_clientState;
}
namespace F4T::ObjectWire {
std::string_view KindName(Kind a_kind) noexcept
{
    return a_kind == Kind::Door ? "door" : a_kind == Kind::Terminal ? "terminal" : "activator";
}
std::optional<ObjectKey> ParseObjectKey(const nlohmann::json& a_object)
{
    if (!a_object.contains("plugin") || !a_object.contains("localId")) {
        return std::nullopt;
    }
    return ObjectKey{ a_object.at("plugin").get<std::string>(), a_object.at("localId").get<std::uint32_t>() };
}
nlohmann::json BuildObjectState(const ObjectState& a_state)
{
    return nlohmann::json{ { "object", nlohmann::json{ { "plugin", a_state.object.plugin }, { "localId", a_state.object.localId } } } };
}
}
namespace F4T::FormIdentity {
Split SplitFormId(std::uint32_t a_value) noexcept
{
    return { Kind::Regular, a_value >> 24, a_value & MaximumRegularLocalId };
}
bool IsValidRecordIdentity(const RecordIdentity& a_identity, Kind) noexcept { return !a_identity.pluginName.empty(); }
}

#include "object-sync-body.h"

namespace {
using Json = nlohmann::json;
namespace Sync = F4T::ObjectSync;
using Runner = std::function<void(const std::function<void()>&)>;
constexpr std::uint32_t kBase = std::uint32_t{ 1 } << 24;
std::deque<RE::TESObjectREFR> g_store;
RE::TESBoundObject g_doorBase;
RE::TESBoundObject g_activatorBase;
RE::TESObjectCELL g_cell;
RE::TESWorldSpace g_worldSpace;
std::uint64_t g_now = 10000;
const Runner deny = [](const std::function<void()>&) {};
const Runner admit = [](const std::function<void()>& a_work) { a_work(); };

void Step(const Runner& a_runner)
{
    g_now += 2000;
    Sync::Tick(g_now, a_runner);
}

void ClearSends()
{
    F4T::Networking::t_activateSends.clear();
    F4T::Networking::t_lockSends.clear();
}

void Begin()
{
    Sync::Reset();
    g_store.clear();
    RE::World().clear();
    g_doorBase.formType = RE::ENUM_FORM_ID::kDOOR;
    g_activatorBase.formType = RE::ENUM_FORM_ID::kACTI;
    g_cell.recordId = 1000;
    g_cell.worldSpace = &g_worldSpace;
    g_worldSpace.recordId = 2000;
    RE::t_player = &RE::t_playerObject;
    RE::t_playerObject.recordId = 3000;
    t_activations = 0;
    t_lockWrites = 0;
    F4T::Networking::t_connection = 1;
    F4T::Networking::t_warnings = 0;
    F4T::ReplicationRuntime::t_session = 1;
    F4T::SessionLaunch::t_canPublish = true;
    F4T::JoinFlow::t_canPublish = true;
    F4T::SurvivalPolicy::g_clientState.ready = 7;
    F4T::SurvivalPolicy::g_clientState.revision = 7;
    ClearSends();
    Sync::SetSharedDoors(true);
    Sync::NoteWelcome(Json{ { "capabilities", Json::array({ "object-state-v1", "object-state-v2" }) } });
}

RE::TESObjectREFR& AddRef(std::uint32_t a_local, RE::ENUM_FORM_ID a_type, bool a_open = false, bool a_hasLock = false, bool a_locked = false)
{
    auto& ref = g_store.emplace_back();
    ref.recordId = kBase | a_local;
    ref.formType = a_type;
    ref.objectReference = a_type == RE::ENUM_FORM_ID::kDOOR ? &g_doorBase : &g_activatorBase;
    ref.extraList._ptr = &ref.extras;
    ref.parentCell = &g_cell;
    ref.open = a_open;
    ref.hasLock = a_hasLock;
    ref.lock.flags = a_locked ? 1 : 0;
    RE::World()[ref.recordId] = &ref;
    return ref;
}

Json Key(std::uint32_t a_local)
{
    return Json{ { "plugin", "Test.esp" }, { "localId", a_local } };
}

Json DoorState(std::uint32_t a_local, std::uint32_t a_revision, bool a_open)
{
    return Json{ { "type", "objectState" }, { "object", Key(a_local) }, { "revision", a_revision }, { "kind", "door" }, { "open", a_open } };
}

Json ActivatorState(std::uint32_t a_local, std::uint32_t a_revision)
{
    return Json{ { "type", "objectState" }, { "object", Key(a_local) }, { "revision", a_revision }, { "kind", "activator" } };
}

void Fire(RE::TESObjectREFR& a_ref)
{
    const RE::TESActivateEvent event{ RE::NiPointer<RE::TESObjectREFR>(&a_ref), RE::NiPointer<RE::TESObjectREFR>(RE::t_player) };
    Sync::g_sink.ProcessEvent(event, nullptr);
}

bool NothingWritten()
{
    return t_activations == 0 && t_lockWrites == 0 && F4T::Networking::t_activateSends.empty() && F4T::Networking::t_lockSends.empty();
}

void DeniedTicksKeepState()
{
    Begin();
    auto& door = AddRef(1, RE::ENUM_FORM_ID::kDOOR);
    Sync::EnqueuePacket(DoorState(1, 1, true));
    for (int tick = 0; tick < 100; ++tick) {
        Step(deny);
    }
    assert(Sync::g_inbound.empty());
    assert(Sync::g_denied);
    assert(Sync::g_retries.size() == 1);
    const auto& held = Sync::g_retries.at(door.recordId);
    assert(held.attempts == 0 && held.expected.open && held.expected.revision == 1);
    assert(!door.open && NothingWritten());
}

void DeniedPacketsAreNeverDropped()
{
    Begin();
    for (std::uint32_t index = 0; index < Sync::kMaximumQueue; ++index) {
        AddRef(100 + index, RE::ENUM_FORM_ID::kDOOR);
        Sync::EnqueuePacket(DoorState(100 + index, 1, true));
        Step(deny);
        assert(Sync::g_dropped == 0 && Sync::g_inbound.empty());
    }
    assert(Sync::g_retries.size() == Sync::kMaximumQueue);
    for (std::uint32_t index = 0; index <= Sync::kMaximumQueue; ++index) {
        AddRef(300 + index, RE::ENUM_FORM_ID::kDOOR);
        Sync::EnqueuePacket(DoorState(300 + index, 1, true));
    }
    assert(Sync::g_inbound.size() == Sync::kMaximumQueue && Sync::g_dropped == 1);
}

void SameRefLatestWins()
{
    Begin();
    auto& door = AddRef(1, RE::ENUM_FORM_ID::kDOOR);
    Sync::EnqueuePacket(DoorState(1, 1, true));
    Sync::EnqueuePacket(DoorState(1, 2, false));
    Step(deny);
    assert(Sync::g_retries.size() == 1);
    assert(Sync::g_retries.at(door.recordId).expected.revision == 2 && !Sync::g_retries.at(door.recordId).expected.open);
    Sync::EnqueuePacket(DoorState(1, 1, true));
    Step(deny);
    assert(Sync::g_retries.at(door.recordId).expected.revision == 2);
    Step(admit);
    assert(NothingWritten() && Sync::g_retries.empty());
}

void ActivationsSaturateWithParity()
{
    for (std::uint32_t total : { 180u, 181u }) {
        Begin();
        auto& activator = AddRef(1, RE::ENUM_FORM_ID::kACTI);
        std::uint32_t revision = 0;
        while (revision < total) {
            for (std::uint32_t batch = 0; batch < 60 && revision < total; ++batch) {
                Sync::EnqueuePacket(ActivatorState(1, ++revision));
            }
            Step(deny);
        }
        const auto held = Sync::g_retries.at(activator.recordId).activations;
        assert(held < Sync::kMaximumQueue && (held & 1u) == (total & 1u) && held > 0);
        Step(admit);
        assert(t_activations == static_cast<int>(held) && Sync::g_retries.empty());
    }
}

void ObjectCapacityRejectsExtraRef()
{
    Begin();
    const std::uint32_t total = static_cast<std::uint32_t>(F4T::ObjectState::kMaximumObjects) + 1;
    for (std::uint32_t index = 0; index < total; ++index) {
        AddRef(1000 + index, RE::ENUM_FORM_ID::kACTI);
        Sync::EnqueuePacket(ActivatorState(1000 + index, 1));
        if ((index + 1) % Sync::kMaximumQueue == 0) {
            Step(deny);
        }
    }
    Step(deny);
    assert(Sync::g_dropped == 0);
    assert(Sync::g_revisions.size() == F4T::ObjectState::kMaximumObjects);
    assert(Sync::g_retries.size() == F4T::ObjectState::kMaximumObjects);
    assert(!Sync::g_retries.contains(kBase | (1000 + total - 1)));
}

void ReleaseReobservesLiveState()
{
    Begin();
    auto& matching = AddRef(1, RE::ENUM_FORM_ID::kDOOR);
    auto& mismatching = AddRef(2, RE::ENUM_FORM_ID::kDOOR);
    Sync::EnqueuePacket(DoorState(1, 1, true));
    Sync::EnqueuePacket(DoorState(2, 1, true));
    Step(deny);
    assert(Sync::g_retries.size() == 2 && t_activations == 0);
    matching.open = true;
    Step(admit);
    assert(t_activations == 1 && matching.open && mismatching.open);
    assert(Sync::g_retries.empty() && !Sync::g_denied);
    assert(F4T::Networking::t_activateSends.empty() && F4T::Networking::t_lockSends.empty());
}

void SinkNeedsReadyRevision()
{
    Begin();
    auto& door = AddRef(1, RE::ENUM_FORM_ID::kDOOR, true);
    F4T::SurvivalPolicy::g_clientState.ready.reset();
    Fire(door);
    assert(Sync::g_captures.empty());
    F4T::SurvivalPolicy::g_clientState.ready = 7;
    Fire(door);
    assert(Sync::g_captures.size() == 1);
    const auto& capture = Sync::g_captures.front();
    assert(capture.localActor && capture.connection == 1 && capture.session == 1 && capture.revision == 7);
    Step(deny);
    assert(Sync::g_captures.empty() && Sync::g_denied);
    Fire(door);
    assert(Sync::g_captures.empty());
}

void StaleCapturesAreDropped()
{
    for (int variant = 0; variant < 4; ++variant) {
        Begin();
        auto& door = AddRef(1, RE::ENUM_FORM_ID::kDOOR, true);
        Fire(door);
        if (variant == 1) {
            F4T::SurvivalPolicy::g_clientState.revision = 8;
        } else if (variant == 2) {
            F4T::ReplicationRuntime::t_session = 2;
        } else if (variant == 3) {
            F4T::Networking::t_connection = 2;
        }
        Step(admit);
        if (variant == 0) {
            assert(F4T::Networking::t_activateSends.size() == 1 && F4T::Networking::t_lockSends.empty());
            assert(F4T::Networking::t_activateSends.front().at("action") == "open");
            assert(Sync::g_tracked.size() == 1);
            Step(admit);
            assert(F4T::Networking::t_activateSends.size() == 1);
        } else {
            assert(NothingWritten() && Sync::g_tracked.empty() && Sync::g_captures.empty());
        }
    }
}

void PublishGatesBlockOutbound()
{
    for (int variant = 0; variant < 2; ++variant) {
        Begin();
        auto& door = AddRef(1, RE::ENUM_FORM_ID::kDOOR, true);
        Fire(door);
        if (variant == 0) {
            F4T::SessionLaunch::t_canPublish = false;
        } else {
            F4T::JoinFlow::t_canPublish = false;
        }
        Step(admit);
        assert(F4T::Networking::t_activateSends.empty() && F4T::Networking::t_lockSends.empty());
    }
}

void DeniedThenResumedSendsNothingStale()
{
    Begin();
    auto& door = AddRef(1, RE::ENUM_FORM_ID::kDOOR, true);
    Fire(door);
    Step(deny);
    Step(admit);
    assert(NothingWritten() && Sync::g_tracked.empty() && !Sync::g_denied);
}

void LockEditsDuringDenialAreNotSent()
{
    Begin();
    std::vector<RE::TESObjectREFR*> refs;
    for (std::uint32_t index = 0; index < Sync::kMaximumTrackedRefs; ++index) {
        refs.push_back(&AddRef(1 + index, RE::ENUM_FORM_ID::kDOOR, true, true, false));
        Fire(*refs.back());
    }
    Step(admit);
    assert(Sync::g_tracked.size() == Sync::kMaximumTrackedRefs);
    assert(F4T::Networking::t_activateSends.size() == Sync::kMaximumTrackedRefs);
    assert(F4T::Networking::t_lockSends.size() == Sync::kMaximumTrackedRefs);
    ClearSends();
    Step(deny);
    Step(deny);
    refs[0]->lock.flags = 1;
    RE::World().erase(refs[1]->recordId);
    Step(admit);
    assert(F4T::Networking::t_lockSends.empty() && F4T::Networking::t_activateSends.empty());
    assert(Sync::g_tracked.size() == Sync::kMaximumTrackedRefs && !Sync::g_denied);
    refs[2]->lock.flags = 1;
    Step(admit);
    assert(F4T::Networking::t_lockSends.size() == 1);
    assert(F4T::Networking::t_lockSends.front().at("locked") == true);
    assert(Sync::g_tracked.size() == Sync::kMaximumTrackedRefs);
}

void ResetClearsHeldAndDenied()
{
    Begin();
    AddRef(1, RE::ENUM_FORM_ID::kDOOR);
    Sync::EnqueuePacket(DoorState(1, 1, true));
    Step(deny);
    assert(!Sync::g_retries.empty() && Sync::g_denied);
    Sync::Reset();
    assert(Sync::g_retries.empty() && !Sync::g_denied && Sync::g_inbound.empty() && Sync::g_captures.empty());
    assert(Sync::g_revisions.empty() && Sync::g_tracked.empty() && Sync::g_dropped == 0);
}
}

int main()
{
    DeniedTicksKeepState();
    DeniedPacketsAreNeverDropped();
    SameRefLatestWins();
    ActivationsSaturateWithParity();
    ObjectCapacityRejectsExtraRef();
    ReleaseReobservesLiveState();
    SinkNeedsReadyRevision();
    StaleCapturesAreDropped();
    PublishGatesBlockOutbound();
    DeniedThenResumedSendsNothingStale();
    LockEditsDuringDenialAreNotSent();
    ResetClearsHeldAndDenied();
}
