#pragma once
// Single-constraint: in-game admin transport is server-verified only; see docs/code-notes.
#include "ServerOutboundFrames.h"
#include "ServerProtocol.h"
#include "ServerSessionTypes.h"
#include "F4TPacketRateLimit.h"
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
struct AdminMenuPeer
{
    std::uint32_t connectionId{};
    std::uint32_t playerId{};
    std::string profileId;
    std::string name;
};

struct NormalizedAdminAction
{
    std::string action;
    std::string requestId;
    nlohmann::json params{ nlohmann::json::object() };
};

class AdminAuditLog
{
public:
    using Clock = std::function<double()>;
    explicit AdminAuditLog(std::string a_path = {}, std::size_t a_maxBytes = 1024 * 1024, int a_retain = 5, Clock a_clock = {});
    void Log(std::string_view a_profileId, std::uint32_t a_playerId, std::string_view a_action, std::string_view a_target, bool a_ok, std::string_view a_reason);
    void SetPath(std::string a_path);
    void SetClock(Clock a_clock);
    [[nodiscard]] std::size_t RejectedCount() const noexcept { return _rejected; }
    [[nodiscard]] std::size_t AcceptedCount() const noexcept { return _accepted; }
    [[nodiscard]] const std::deque<nlohmann::json>& Entries() const noexcept { return _entries; }
private:
    void RotateIfNeeded();
    std::string _path;
    std::size_t _maxBytes{};
    int _retain{};
    Clock _clock;
    std::size_t _rejected{};
    std::size_t _accepted{};
    std::deque<nlohmann::json> _entries;
};

class AdminMenuService
{
public:
    using Json = nlohmann::json;
    static constexpr double kRefillPerSecond = 2.0;
    static constexpr double kBurst = 8.0;
    static constexpr std::size_t kMaxActionBytes = 256;
    static constexpr std::size_t kMaxRequestIdBytes = 128;
    static constexpr std::size_t kMaxEntries = 1024;
    static constexpr std::size_t kMaxTargetBytes = 256;
    [[nodiscard]] static bool IsKnownAction(std::string_view a_action) noexcept;
    [[nodiscard]] static std::optional<NormalizedAdminAction> Normalize(const Json& a_packet);
    [[nodiscard]] static Json MakeState(double a_unixTimeSeconds);
    [[nodiscard]] static Json MakeResult(const NormalizedAdminAction& a_action, bool a_ok, std::string a_message, Json a_data = nullptr);
    std::optional<std::vector<OutboundFrame>> Dispatch(
        std::string_view a_type,
        const AdminMenuPeer& a_self,
        F4T::PacketRateLimit::TokenBucket& a_bucket,
        bool a_isLiveAdmin,
        bool& a_notAdminLogged,
        const std::vector<AdminMenuPeer>& a_peers,
        const Json& a_packet,
        double a_monotonicSeconds,
        double a_unixTimeSeconds,
        AdminAuditLog& a_audit,
        ProtocolStats& a_stats);
    AdminAuditLog audit;
};
}
