#include "ServerAdminMenu.h"
#include "ServerProtocolImpl.h"
#include "F4TSaveProfileLogic.h"
#include <filesystem>
#include <fstream>
#include <iostream>
namespace CommonwealthOnline::Server
{
namespace
{
using Json = nlohmann::json;
bool IsSafeToken(std::string_view v, std::size_t max)
{
    if (v.empty() || v.size() > max) return false;
    for (unsigned char c : v) { if (c < 0x20 || c == 0x7f) return false; }
    return true;
}
std::string SanitizeAuditField(std::string_view v)
{
    std::string out;
    out.reserve(v.size());
    for (unsigned char c : v) {
        if (c < 0x20 || c == 0x7f) continue;
        out.push_back(static_cast<char>(c));
    }
    return out;
}
bool AdminAccountMatches(const std::vector<std::string> &a_entries, std::string_view a_profileId)
{
    if (a_profileId.empty()) return false;
    for (const auto &entry : a_entries) {
        if (F4T::SaveProfileLogic::EqualsIgnoreCase(entry, a_profileId)) return true;
    }
    return false;
}
std::string TargetOf(const NormalizedAdminAction& a)
{
    std::string target;
    const auto t = a.params.find("targetProfileId");
    if (t != a.params.end() && t->is_string()) target = t->get<std::string>();
    else if (const auto p = a.params.find("playerId"); p != a.params.end() && p->is_number_unsigned()) target = std::to_string(p->get<std::uint32_t>());
    if (target.size() > AdminMenuService::kMaxTargetBytes) target.resize(AdminMenuService::kMaxTargetBytes);
    return SanitizeAuditField(target);
}
}
AdminAuditLog::AdminAuditLog(std::string a_path, std::size_t a_maxBytes, int a_retain, Clock a_clock)
    : _path(std::move(a_path)), _maxBytes(a_maxBytes), _retain(a_retain), _clock(std::move(a_clock)) {}
void AdminAuditLog::RotateIfNeeded()
{
    if (_path.empty()) return;
    try {
        std::error_code ec;
        const auto size = std::filesystem::file_size(_path, ec);
        if (ec || size < _maxBytes) return;
        for (int i = _retain - 1; i >= 1; --i) {
            const auto from = _path + "." + std::to_string(i);
            const auto to = _path + "." + std::to_string(i + 1);
            std::error_code ec2;
            if (std::filesystem::exists(from, ec2)) std::filesystem::rename(from, to, ec2);
        }
        std::error_code ec3;
        std::filesystem::rename(_path, _path + ".1", ec3);
    } catch (...) {}
}
void AdminAuditLog::SetPath(std::string a_path)
{
    _path = std::move(a_path);
}
void AdminAuditLog::SetClock(Clock a_clock)
{
    _clock = std::move(a_clock);
}
void AdminAuditLog::Log(std::string_view a_profileId, std::uint32_t a_playerId, std::string_view a_action, std::string_view a_target, bool a_ok, std::string_view a_reason)
{
    const auto profile = SanitizeAuditField(a_profileId);
    const auto action = SanitizeAuditField(a_action);
    const auto target = SanitizeAuditField(a_target);
    const auto reason = SanitizeAuditField(a_reason);
    Json entry{ {"profileId", profile}, {"playerId", a_playerId}, {"action", action}, {"target", target}, {"ok", a_ok}, {"reason", reason} };
    double now = _clock ? _clock() : 0.0;
    entry["time"] = now;
    _entries.push_back(entry);
    while (_entries.size() > AdminMenuService::kMaxEntries) _entries.pop_front();
    if (a_ok) ++_accepted; else ++_rejected;
    std::cout << "[admin-menu] profile=" << profile << " player=" << a_playerId << " action=" << action << " target=" << target << " ok=" << (a_ok ? 1 : 0) << " reason=" << reason << std::endl;
    if (_path.empty()) return;
    try {
        RotateIfNeeded();
        std::ofstream out(_path, std::ios::app);
        out << entry.dump() << std::endl;
    } catch (...) {}
}
bool AdminMenuService::IsKnownAction(std::string_view a_action) noexcept
{
    return a_action == "ping" || a_action == "players";
}
std::optional<NormalizedAdminAction> AdminMenuService::Normalize(const Json& a_packet)
{
    const auto actionIt = a_packet.find("action");
    if (actionIt == a_packet.end() || !actionIt->is_string()) return std::nullopt;
    const auto action = actionIt->get<std::string>();
    if (!IsSafeToken(action, kMaxActionBytes)) return std::nullopt;
    std::string requestId;
    if (const auto r = a_packet.find("requestId"); r != a_packet.end()) {
        if (!r->is_string()) return std::nullopt;
        requestId = r->get<std::string>();
        if (!requestId.empty() && !IsSafeToken(requestId, kMaxRequestIdBytes)) return std::nullopt;
    }
    NormalizedAdminAction out{ action, requestId, Json::object() };
    for (auto it = a_packet.begin(); it != a_packet.end(); ++it) {
        if (it.key() == "type" || it.key() == "action" || it.key() == "requestId") continue;
        out.params[it.key()] = it.value();
    }
    return out;
}
Json AdminMenuService::MakeState(double a_unixTimeSeconds)
{
    return Json{ {"type", "adminState"}, {"isAdmin", true}, {"serverTime", a_unixTimeSeconds}, {"actions", Json::array({"ping", "players"})}, {"menus", Json::array({"players"})} };
}
Json AdminMenuService::MakeResult(const NormalizedAdminAction& a_action, bool a_ok, std::string a_message, Json a_data)
{
    Json result{ {"type", "adminResult"}, {"action", a_action.action}, {"ok", a_ok}, {"message", std::move(a_message)} };
    if (!a_action.requestId.empty()) result["requestId"] = a_action.requestId;
    result["data"] = a_data.is_null() ? Json(nullptr) : std::move(a_data);
    return result;
}
std::optional<std::vector<OutboundFrame>> AdminMenuService::Dispatch(
    std::string_view a_type, const AdminMenuPeer& a_self, F4T::PacketRateLimit::TokenBucket& a_bucket,
    bool a_isLiveAdmin, bool& a_notAdminLogged, const std::vector<AdminMenuPeer>& a_peers, const Json& a_packet,
    double a_monotonicSeconds, double a_unixTimeSeconds, AdminAuditLog& a_audit, ProtocolStats& a_stats)
{
    if (a_type != "adminAction") return std::nullopt;
    if (!F4T::PacketRateLimit::TakeToken(a_bucket, a_monotonicSeconds, kRefillPerSecond, kBurst)) {
        ++a_stats.packetsRejected;
        ++a_stats.adminRejected;
        if (!a_isLiveAdmin) return std::vector<OutboundFrame>{};
        a_audit.Log(a_self.profileId, a_self.playerId, "adminAction", {}, false, "rate-limited");
        return std::vector<OutboundFrame>{};
    }
    if (!a_isLiveAdmin) {
        ++a_stats.packetsRejected;
        ++a_stats.adminRejected;
        if (!a_notAdminLogged) {
            a_audit.Log(a_self.profileId, a_self.playerId, "adminAction", {}, false, "not-admin");
            a_notAdminLogged = true;
        }
        return std::vector<OutboundFrame>{};
    }
    const auto normalized = Normalize(a_packet);
    if (!normalized) {
        ++a_stats.packetsRejected;
        ++a_stats.adminRejected;
        a_audit.Log(a_self.profileId, a_self.playerId, "adminAction", {}, false, "malformed");
        return std::vector<OutboundFrame>{ ReliableFrame(a_self.connectionId, MakeResult({"adminAction", {}, {}}, false, "Malformed admin action.", nullptr)) };
    }
    if (!IsKnownAction(normalized->action)) {
        ++a_stats.packetsRejected;
        ++a_stats.adminRejected;
        a_audit.Log(a_self.profileId, a_self.playerId, normalized->action, TargetOf(*normalized), false, "unknown-action");
        return std::vector<OutboundFrame>{ ReliableFrame(a_self.connectionId, MakeResult(*normalized, false, "Unknown admin action.", nullptr)) };
    }
    if (normalized->action == "ping") {
        a_audit.Log(a_self.profileId, a_self.playerId, "ping", {}, true, "ok");
        return std::vector<OutboundFrame>{ ReliableFrame(a_self.connectionId, MakeResult(*normalized, true, "pong", Json{{"pong", true}})) };
    }
    Json players = Json::array();
    for (const auto& peer : a_peers) players.push_back(Json{ {"playerId", peer.playerId}, {"profileId", peer.profileId}, {"name", peer.name} });
    a_audit.Log(a_self.profileId, a_self.playerId, "players", {}, true, "ok");
    (void)a_unixTimeSeconds;
    return std::vector<OutboundFrame>{ ReliableFrame(a_self.connectionId, MakeResult(*normalized, true, "players", Json{{"players", std::move(players)}})) };
}

namespace
{
std::string AdminDisplayName(const SessionTypes::Session& s)
{
    if (s.lastDurablePlayerState) {
        const auto f = s.lastDurablePlayerState->find("characterName");
        if (f != s.lastDurablePlayerState->end() && f->is_string() && !f->get_ref<const std::string&>().empty())
            return f->get_ref<const std::string&>();
    }
    return "Unknown";
}
}

bool ProtocolHost::IsLiveAdmin(std::uint32_t a_connectionId) const noexcept
{
    const auto found = _impl->sessions.find(a_connectionId);
    if (found == _impl->sessions.end()) return false;
    return _impl->IsLiveAdminSession(found->second);
}

bool ProtocolHost::Impl::IsLiveAdminSession(const Session& a_session) const
{
    if (!a_session.active || !a_session.adminSecretVerified) return false;
    return AdminAccountMatches(adminProfiles, a_session.profileId);
}

void ProtocolHost::SetSessionAdminVerified(std::uint32_t a_connectionId, bool a_verified)
{
    if (const auto found = _impl->sessions.find(a_connectionId); found != _impl->sessions.end())
        found->second.adminSecretVerified = a_verified;
}

void ProtocolHost::SetAdminProfiles(std::vector<std::string> a_adminProfiles)
{
    _impl->adminProfiles = std::move(a_adminProfiles);
}

void ProtocolHost::SetAdminAuditPath(std::string a_path)
{
    _impl->adminMenu.audit.SetPath(std::move(a_path));
}
void ProtocolHost::SetAdminAuditClock(std::function<double()> a_clock)
{
    _impl->adminMenu.audit.SetClock(std::move(a_clock));
}

std::vector<OutboundFrame> ProtocolHost::AdminStateFrame(std::uint32_t a_connectionId, double a_unixTimeSeconds) const
{
    if (!IsLiveAdmin(a_connectionId)) return {};
    return { ReliableFrame(a_connectionId, AdminMenuService::MakeState(a_unixTimeSeconds)) };
}

std::optional<std::vector<OutboundFrame>> ProtocolHost::Impl::DispatchAdminPacket(
    const std::string& a_type, std::uint32_t a_connectionId, Session& a_session,
    const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds)
{
    if (a_type != "adminAction") return std::nullopt;
    AdminMenuPeer self{ a_connectionId, a_session.playerId, a_session.profileId, AdminDisplayName(a_session) };
    std::vector<AdminMenuPeer> peers;
    for (const auto& [connectionId, session] : sessions) {
        if (!session.active) continue;
        peers.push_back({ connectionId, session.playerId, session.profileId, AdminDisplayName(session) });
    }
    const bool liveAdmin = IsLiveAdminSession(a_session);
    return adminMenu.Dispatch(a_type, self, a_session.adminRequests, liveAdmin, a_session.adminRejectLogged, peers, a_packet, a_monotonicSeconds, a_unixTimeSeconds, adminMenu.audit, stats);
}

void ProtocolHost::Impl::BindSessionProfileVerified(std::uint32_t a_connectionId, std::string_view a_profileId, bool a_verified, std::string_view a_steamId)
{
    if (const auto found = sessions.find(a_connectionId); found != sessions.end()) {
        found->second.profileId = std::string{ a_profileId };
        found->second.adminSecretVerified = a_verified;
        found->second.steamId = std::string{ a_steamId };
    }
}

void ProtocolHost::Impl::BindSessionSteamId(std::uint32_t a_connectionId, std::string_view a_steamId)
{
    if (const auto found = sessions.find(a_connectionId); found != sessions.end()) {
        found->second.steamId = std::string{ a_steamId };
    }
}
}
