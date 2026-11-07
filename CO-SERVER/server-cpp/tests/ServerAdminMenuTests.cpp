#include "ServerProtocol.h"
#include "ServerAdminMenu.h"
#include "ServerAdminMenuDrain.h"
#include "AdminCommandDispatcher.h"
#include "F4TGnsSnapshotEnvelope.h"
#include <nlohmann/json.hpp>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace
{
using Json = nlohmann::json;
using CommonwealthOnline::Server::OutboundFrame;
using F4T::GnsSnapshotEnvelope::DecodeResult;
std::vector<std::uint8_t> EncodeJson(const Json& p) { const auto t = p.dump(); return { t.begin(), t.end() }; }
Json DecodePacket(const OutboundFrame& f)
{
    F4T::GnsSnapshotEnvelope::Header h{};
    const auto d = F4T::GnsSnapshotEnvelope::Decode(f.payload, h);
    std::span<const std::uint8_t> payload{ f.payload };
    if (d == DecodeResult::Valid) payload = payload.subspan(F4T::GnsSnapshotEnvelope::HeaderSize);
    else assert(d == DecodeResult::NotEnvelope);
    return Json::parse(std::string{ reinterpret_cast<const char*>(payload.data()), payload.size() });
}
bool HasType(const std::vector<OutboundFrame>& frames, std::uint32_t c, std::string_view t)
{
    for (const auto& f : frames) {
        if (f.connectionId != c || f.payload.empty()) continue;
        if (DecodePacket(f).value("type", std::string{}) == t) return true;
    }
    return false;
}
Json FindResult(const std::vector<OutboundFrame>& frames, std::uint32_t c)
{
    for (const auto& f : frames) {
        if (f.connectionId != c || f.payload.empty()) continue;
        const auto p = DecodePacket(f);
        if (p.value("type", std::string{}) == "adminResult") return p;
    }
    assert(false && "adminResult missing");
    return {};
}
std::uint32_t HelloReady(CommonwealthOnline::Server::ProtocolHost& host, std::uint32_t conn, std::string_view profile, bool verified, double t)
{
    const auto welcome = host.HandleConnected(conn, t);
    const auto player = DecodePacket(welcome[0]).at("playerId").get<std::uint32_t>();
    host.BindSessionProfile(conn, profile);
    host.SetSessionAdminVerified(conn, verified);
    const auto ready = host.HandleMessage(conn, EncodeJson(Json{{"type", "hello"}, {"protocolVersion", 2}}), t + 0.1, t + 0.2);
    assert(HasType(ready, conn, "sessionReady"));
    return player;
}
std::optional<std::vector<OutboundFrame>> Gate(CommonwealthOnline::Server::AdminMenuService& a_svc, const Json& a_packet, double a_t)
{
    const CommonwealthOnline::Server::AdminMenuPeer self{ 1, 1, "co-admin", "Admin" };
    CommonwealthOnline::Server::ProtocolStats stats;
    F4T::PacketRateLimit::TokenBucket bucket;
    bool logged = false;
    return a_svc.Dispatch("adminAction", self, bucket, true, logged, {}, a_packet, a_t, a_t, a_svc.audit, stats);
}
Json ReadLastAudit(const std::filesystem::path& a_path)
{
    std::ifstream in(a_path);
    return Json::parse(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()));
}
std::vector<Json> ReadAuditLines(const std::filesystem::path& a_path)
{
    std::ifstream in(a_path);
    std::vector<Json> lines;
    for (std::string line; std::getline(in, line);) lines.push_back(Json::parse(line));
    return lines;
}
}
int main()
{
    using CommonwealthOnline::Server::ProtocolHost;
    using CommonwealthOnline::Server::AdminMenuService;
    using CommonwealthOnline::Server::AdminCommandDispatcher;
    using CommonwealthOnline::Server::AdminCommandHooks;
    using CommonwealthOnline::Server::AdminActionResult;
    using CommonwealthOnline::Server::NormalizedAdminAction;
    using CommonwealthOnline::Server::DrainAdminMenuActions;
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        HelloReady(host, 11, "co-player", false, 1000.0);
        assert(!HasType(host.AdminStateFrame(11, 1000.5), 11, "adminState"));
        const auto before = host.Stats().adminRejected;
        const auto rejBefore = host.Stats().packetsRejected;
        const auto out = host.HandleMessage(11, EncodeJson(Json{{"type", "adminAction"}, {"action", "ping"}}), 1001.0, 1001.0);
        assert(!HasType(out, 11, "adminResult"));
        assert(host.Stats().adminRejected == before + 1);
        assert(host.Stats().packetsRejected == rejBefore + 1);
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        HelloReady(host, 21, "co-admin", true, 2000.0);
        assert(HasType(host.AdminStateFrame(21, 2000.5), 21, "adminState"));
        const auto ping = host.HandleMessage(21, EncodeJson(Json{{"type", "adminAction"}, {"action", "ping"}, {"requestId", "r1"}}), 2001.0, 2001.0);
        const auto pr = FindResult(ping, 21);
        assert(pr.at("ok") == true && pr.at("requestId") == "r1");
        const auto players = host.HandleMessage(21, EncodeJson(Json{{"type", "adminAction"}, {"action", "players"}}), 2002.0, 2002.0);
        const auto pl = FindResult(players, 21);
        assert(pl.at("ok") == true && pl.at("data").contains("players"));
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        HelloReady(host, 31, "co-admin", false, 3000.0);
        assert(!HasType(host.AdminStateFrame(31, 3000.5), 31, "adminState"));
        const auto out = host.HandleMessage(31, EncodeJson(Json{{"type", "adminAction"}, {"action", "ping"}}), 3001.0, 3001.0);
        assert(!HasType(out, 31, "adminResult"));
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        HelloReady(host, 41, "co-admin", true, 4000.0);
        assert(HasType(host.AdminStateFrame(41, 4000.5), 41, "adminState"));
        host.SetAdminProfiles({});
        assert(!HasType(host.AdminStateFrame(41, 4001.0), 41, "adminState"));
        const auto out = host.HandleMessage(41, EncodeJson(Json{{"type", "adminAction"}, {"action", "ping"}}), 4002.0, 4002.0);
        assert(!HasType(out, 41, "adminResult"));
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        HelloReady(host, 51, "co-admin", true, 5000.0);
        const auto unknown = host.HandleMessage(51, EncodeJson(Json{{"type", "adminAction"}, {"action", "shutdown-everything"}}), 5001.0, 5001.0);
        assert(FindResult(unknown, 51).at("ok") == false);
        const auto malformed = host.HandleMessage(51, EncodeJson(Json{{"type", "adminAction"}}), 5002.0, 5002.0);
        assert(FindResult(malformed, 51).at("ok") == false);
        const auto huge = host.HandleMessage(51, EncodeJson(Json{{"type", "adminAction"}, {"action", std::string(500, 'x')}}), 5003.0, 5003.0);
        assert(FindResult(huge, 51).at("ok") == false);
        const auto badId = host.HandleMessage(51, EncodeJson(Json{{"type", "adminAction"}, {"action", "ping"}, {"requestId", 7}}), 5004.0, 5004.0);
        assert(FindResult(badId, 51).at("ok") == false);
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        HelloReady(host, 61, "co-admin", true, 6000.0);
        int accepted = 0, limited = 0;
        for (int i = 0; i < 20; ++i) {
            const auto out = host.HandleMessage(61, EncodeJson(Json{{"type", "adminAction"}, {"action", "ping"}}), 6001.0, 6001.0);
            assert(HasType(out, 61, "adminResult"));
            if (FindResult(out, 61).at("ok") == true) ++accepted;
            else ++limited;
        }
        assert(accepted > 0 && limited > 0);
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        host.HandleConnected(81, 8000.0);
        host.BindSessionProfile(81, "co-admin");
        host.SetSessionAdminVerified(81, true);
        const auto ready = host.HandleMessage(81, EncodeJson(Json{{"type", "hello"}, {"protocolVersion", 2}}), 8000.1, 8000.2);
        assert(HasType(ready, 81, "sessionReady"));
        assert(HasType(ready, 81, "adminState"));
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        host.HandleConnected(82, 8010.0);
        host.BindSessionProfile(82, "co-player");
        host.SetSessionAdminVerified(82, false);
        const auto ready = host.HandleMessage(82, EncodeJson(Json{{"type", "hello"}, {"protocolVersion", 2}}), 8010.1, 8010.2);
        assert(HasType(ready, 82, "sessionReady"));
        assert(!HasType(ready, 82, "adminState"));
    }
    {
        AdminMenuService svc;
        CommonwealthOnline::Server::AdminMenuPeer self{ 71, 7, "co-admin", "Admin" };
        CommonwealthOnline::Server::ProtocolStats stats;
        F4T::PacketRateLimit::TokenBucket okBucket;
        bool okLogged = false;
        const auto ok = svc.Dispatch("adminAction", self, okBucket, true, okLogged, {}, Json{{"type", "adminAction"}, {"action", "ping"}}, 7001.0, 7001.0, svc.audit, stats);
        assert(ok && HasType(*ok, 71, "adminResult"));
        F4T::PacketRateLimit::TokenBucket deniedBucket;
        bool deniedLogged = false;
        const auto denied = svc.Dispatch("adminAction", self, deniedBucket, false, deniedLogged, {}, Json{{"type", "adminAction"}, {"action", "ping"}}, 7002.0, 7002.0, svc.audit, stats);
        assert(denied && denied->empty());
        assert(deniedLogged);
        assert(svc.audit.AcceptedCount() == 1 && svc.audit.RejectedCount() == 1);
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        host.HandleConnected(96, 9020.0);
        host.BindSessionProfile(96, "co-admin");
        host.SetSessionAdminVerified(96, true);
        assert(HasType(host.HandleMessage(96, EncodeJson(Json{{"type", "hello"}, {"protocolVersion", 2}}), 9020.1, 9020.2), 96, "adminState"));
        host.HandleDisconnected(96, 9021.0);
        host.HandleConnected(96, 9022.0);
        host.BindSessionProfile(96, "co-player");
        host.SetSessionAdminVerified(96, false);
        const auto ready = host.HandleMessage(96, EncodeJson(Json{{"type", "hello"}, {"protocolVersion", 2}}), 9022.1, 9022.2);
        assert(HasType(ready, 96, "sessionReady"));
        assert(!HasType(ready, 96, "adminState"));
        const auto out = host.HandleMessage(96, EncodeJson(Json{{"type", "adminAction"}, {"action", "ping"}}), 9023.0, 9023.0);
        assert(!HasType(out, 96, "adminResult"));
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        host.HandleConnected(94, 9010.0);
        host.BindSessionProfile(94, "co-admin#2");
        host.SetSessionAdminVerified(94, true);
        const auto slotReady = host.HandleMessage(94, EncodeJson(Json{{"type", "hello"}, {"protocolVersion", 2}}), 9010.1, 9010.2);
        assert(HasType(slotReady, 94, "sessionReady"));
        assert(!HasType(slotReady, 94, "adminState"));
        const auto slotOut = host.HandleMessage(94, EncodeJson(Json{{"type", "adminAction"}, {"action", "ping"}}), 9011.0, 9011.0);
        assert(!HasType(slotOut, 94, "adminResult"));
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        host.HandleConnected(95, 9012.0);
        host.BindSessionProfile(95, "CO-ADMIN");
        host.SetSessionAdminVerified(95, true);
        const auto caseReady = host.HandleMessage(95, EncodeJson(Json{{"type", "hello"}, {"protocolVersion", 2}}), 9012.1, 9012.2);
        assert(HasType(caseReady, 95, "sessionReady"));
        assert(!HasType(caseReady, 95, "adminState"));
        const auto caseAction = host.HandleMessage(95, EncodeJson(Json{{"type", "adminAction"}, {"action", "ping"}}), 9013.0, 9013.0);
        assert(!HasType(caseAction, 95, "adminResult"));
    }
    {
        AdminMenuService probe;
        CommonwealthOnline::Server::AdminMenuPeer hostile{ 93, 9, std::string("evil") + std::string(1, char(10)) + "INJECTED" + std::string(1, char(1)), "x" };
        CommonwealthOnline::Server::ProtocolStats stats;
        F4T::PacketRateLimit::TokenBucket bucket;
        bool logged = false;
        const auto out = probe.Dispatch("adminAction", hostile, bucket, true, logged, {}, Json{{"type", "adminAction"}, {"action", "ping"}}, 9001.0, 9001.0, probe.audit, stats);
        assert(out && HasType(*out, 93, "adminResult"));
        const auto stored = probe.audit.Entries().back().at("profileId").get<std::string>();
        assert(stored.find(char(10)) == std::string::npos);
        assert(stored.find(char(1)) == std::string::npos);
        assert(stored == "evilINJECTED");
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        HelloReady(host, 97, "co-admin", true, 9030.0);
        HelloReady(host, 98, "co-player", false, 9030.5);
        const auto players = host.HandleMessage(97, EncodeJson(Json{{"type", "adminAction"}, {"action", "players"}}), 9031.0, 9031.0);
        const auto pl = FindResult(players, 97);
        assert(pl.at("ok") == true);
        for (const auto& entry : pl.at("data").at("players")) {
            assert(entry.is_object() && entry.size() == 3u);
            assert(entry.contains("playerId") && entry.contains("profileId") && entry.contains("name"));
        }
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        host.HandleConnected(101, 9100.0);
        host.BindSessionProfile(101, "co-admin");
        host.SetSessionAdminVerified(101, true);
        assert(!HasType(host.AdminStateFrame(101, 9100.5), 101, "adminState"));
        const auto out = host.HandleMessage(101, EncodeJson(Json{{"type", "adminAction"}, {"action", "ping"}}), 9101.0, 9101.0);
        assert(!HasType(out, 101, "adminResult"));
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        HelloReady(host, 102, "co-admin", true, 9200.0);
        host.HandleConnected(103, 9200.5);
        host.BindSessionProfile(103, "co-player");
        host.SetSessionAdminVerified(103, false);
        const auto ready = host.HandleMessage(103, EncodeJson(Json{{"type", "hello"}, {"protocolVersion", 2}}), 9200.6, 9200.7);
        assert(HasType(ready, 103, "sessionReady"));
        assert(!HasType(ready, 103, "adminState"));
        const auto out = host.HandleMessage(103, EncodeJson(Json{{"type", "adminAction"}, {"action", "ping"}}), 9201.0, 9201.0);
        assert(!HasType(out, 103, "adminResult"));
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        HelloReady(host, 104, "co-admin", true, 9300.0);
        const std::string evilId = std::string("r1") + char(10) + "forged: ok=true";
        const auto out = host.HandleMessage(104, EncodeJson(Json{{"type", "adminAction"}, {"action", "ping"}, {"requestId", evilId}}), 9301.0, 9301.0);
        assert(FindResult(out, 104).at("ok") == false);
    }
    {
        AdminMenuService svc;
        for (int i = 0; i < 1100; ++i) svc.audit.Log("p", 1, "ping", {}, true, "ok");
        assert(svc.audit.Entries().size() == 1024u);
        assert(svc.audit.AcceptedCount() == 1100);
    }
    {
        AdminMenuService svc;
        svc.audit.SetClock([] { return 1234.5; });
        CommonwealthOnline::Server::AdminMenuPeer self{ 105, 5, "co-admin", "Admin" };
        CommonwealthOnline::Server::ProtocolStats stats;
        F4T::PacketRateLimit::TokenBucket bucket;
        bool logged = false;
        const auto out = svc.Dispatch("adminAction", self, bucket, true, logged, {}, Json{{"type", "adminAction"}, {"action", "ping"}}, 9302.0, 9302.0, svc.audit, stats);
        assert(out && HasType(*out, 105, "adminResult"));
        assert(svc.audit.Entries().back().at("time").get<double>() == 1234.5);
    }
    {
        AdminMenuService svc;
        CommonwealthOnline::Server::AdminMenuPeer hostile{ 106, 6, "intruder", "Evil" };
        CommonwealthOnline::Server::ProtocolStats stats;
        F4T::PacketRateLimit::TokenBucket bucket;
        bool logged = false;
        for (int i = 0; i < 20; ++i) {
            const auto out = svc.Dispatch("adminAction", hostile, bucket, false, logged, {}, Json{{"type", "adminAction"}, {"action", "ping"}}, 9303.0, 9303.0, svc.audit, stats);
            assert(out && out->empty());
        }
        assert(stats.adminRejected == 20);
        assert(stats.packetsRejected == 20);
        assert(logged);
        assert(svc.audit.Entries().size() == 1);
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        const auto path = (std::filesystem::temp_directory_path() / "co-admin-audit-107.log").string();
        std::remove(path.c_str());
        host.SetAdminAuditPath(path);
        host.SetAdminAuditClock([] { return 4321.0; });
        HelloReady(host, 107, "co-admin", true, 9400.0);
        const auto ping = host.HandleMessage(107, EncodeJson(Json{{"type", "adminAction"}, {"action", "ping"}}), 9401.0, 9401.0);
        assert(HasType(ping, 107, "adminResult"));
        std::ifstream in(path);
        assert(in.good());
        const auto logged = Json::parse(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()));
        assert(logged.at("action") == "ping" && logged.at("ok") == true);
        assert(logged.at("time").get<double>() == 4321.0);
        in.close();
        std::remove(path.c_str());
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        HelloReady(host, 111, "co-admin", true, 10000.0);
        const auto state = DecodePacket(host.AdminStateFrame(111, 10000.5).at(0));
        const std::vector<std::string> expected{ "ping", "players", "status", "clients", "bans", "kick", "ban", "unban", "profile_unban", "admin_list", "admin_grant", "admin_revoke", "revive", "kill", "voice_mute", "voice_unmute", "world_time", "world_weather", "world_mode", "factions", "faction_disband", "faction_promote", "faction_kick", "resources", "ensure", "stop", "refresh", "approve", "quest_mode", "map_public", "reports" };
        assert(state.at("actions") == Json(expected));
        assert(state.at("menus") == Json::array({ "players", "moderation", "world", "factions", "resources", "reports" }));
    }
    {
        ProtocolHost host{ "Admin Menu", "", {} };
        bool listed = true;
        host.SetAdminLookup([&listed](std::string_view a_profileId) { return listed && a_profileId == "co-admin"; });
        HelloReady(host, 111, "co-admin", true, 10000.0);
        auto frames = host.AdminStateFramesForProfile("co-admin", 10001.0);
        assert(frames.size() == 1);
        auto state = DecodePacket(frames.at(0));
        assert(state.at("isAdmin").get<bool>());
        assert(state.at("actions").size() == 31);
        listed = false;
        frames = host.AdminStateFramesForProfile("co-admin", 10002.0);
        assert(frames.size() == 1);
        state = DecodePacket(frames.at(0));
        assert(!state.at("isAdmin").get<bool>());
        assert(state.at("actions").empty() && state.at("menus").empty());
        assert(host.AdminStateFrame(111, 10003.0).empty());
        assert(host.AdminStateFramesForProfile("someone-else", 10004.0).empty());
    }
    {
        AdminMenuService svc;
        const std::vector<std::string> queued{ "status", "clients", "bans", "kick", "ban", "unban", "profile_unban", "admin_list", "admin_grant", "admin_revoke", "revive", "kill", "voice_mute", "voice_unmute", "world_time", "world_weather", "world_mode", "factions", "faction_disband", "faction_promote", "faction_kick", "resources", "ensure", "stop", "refresh", "approve", "quest_mode", "map_public", "reports" };
        double t = 12000.0;
        for (const auto& action : queued) {
            t += 1.0;
            const auto out = Gate(svc, Json{ { "type", "adminAction" }, { "action", action } }, t);
            assert(out && out->empty());
        }
        assert(svc.TakePending().size() == queued.size());
    }
    {
        AdminMenuService svc;
        const std::vector<Json> rejected{
            Json{ { "action", "shutdown" } },
            Json{ { "action", "identity_reset" }, { "profileId", "co-player" } },
            Json{ { "action", "steam_ban" }, { "playerId", 7 } },
            Json{ { "action", "steam_lookup" }, { "profileId", "co-player" } },
            Json{ { "action", "ban" }, { "ip", "1.2.3.4" } },
            Json{ { "action", "ban" }, { "playerId", 7 }, { "ip", "1.2.3.4" } },
        };
        double t = 12100.0;
        for (auto packet : rejected) {
            t += 1.0;
            packet["type"] = "adminAction";
            const auto out = Gate(svc, packet, t);
            assert(out && FindResult(*out, 1).at("message") == "Unknown admin action.");
        }
        assert(svc.TakePending().empty());
    }
    {
        AdminMenuService svc;
        const auto ping = Gate(svc, Json{ { "type", "adminAction" }, { "action", "ping" }, { "junk", 1 } }, 12200.0);
        assert(ping && FindResult(*ping, 1).at("ok") == true);
        const auto kick = Gate(svc, Json{ { "type", "adminAction" }, { "action", "kick" }, { "playerId", 7 }, { "reason", "" }, { "ip", "1.2.3.4" }, { "junk", true } }, 12201.0);
        assert(kick && kick->empty());
        const auto pending = svc.TakePending();
        assert(pending.size() == 1 && AdminMenuService::DispatchRequest(pending[0].action) == (Json{ { "cmd", "kick" }, { "playerId", 7 }, { "reason", "" } }));
    }
    {
        AdminMenuService svc;
        for (std::size_t i = 0; i < AdminMenuService::kMaxPendingActions; ++i) {
            const auto out = Gate(svc, Json{ { "type", "adminAction" }, { "action", "kick" }, { "playerId", 7 } }, 13000.0 + static_cast<double>(i));
            assert(out && out->empty());
        }
        const auto busy = Gate(svc, Json{ { "type", "adminAction" }, { "action", "kick" }, { "playerId", 7 } }, 13100.0);
        assert(busy && FindResult(*busy, 1).at("ok") == false);
        assert(FindResult(*busy, 1).at("message") == "Too many admin actions are waiting. Try again in a moment.");
        assert(svc.audit.Entries().back().at("reason") == "busy");
        assert(svc.TakePending().size() == AdminMenuService::kMaxPendingActions);
    }
    {
        const auto request = AdminMenuService::DispatchRequest(NormalizedAdminAction{ "ban", "", Json{ { "playerId", 9 }, { "reason", "x" }, { "ip", "1.2.3.4" } } });
        assert(request.is_null());
        const auto banRequest = AdminMenuService::DispatchRequest(NormalizedAdminAction{ "ban", "", Json{ { "playerId", 9 }, { "reason", "x" } } });
        assert(banRequest == (Json{ { "cmd", "ban" }, { "playerId", 9 }, { "reason", "x" } }));
        const auto unbanRequest = AdminMenuService::DispatchRequest(NormalizedAdminAction{ "unban", "", Json{ { "ip", "1.2.3.4" }, { "playerId", 3 } } });
        assert(unbanRequest == (Json{ { "cmd", "unban" }, { "ip", "1.2.3.4" } }));
        assert(AdminMenuService::DispatchRequest(NormalizedAdminAction{ "shutdown", "", Json::object() }).is_null());
        assert(AdminMenuService::DispatchRequest(NormalizedAdminAction{ "map_public", "", Json{ { "enabled", false }, { "junk", 1 } } }) == (Json{ { "cmd", "map_public" }, { "enabled", false } }));
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        HelloReady(host, 161, "co-player", false, 14000.0);
        const auto out = host.HandleMessage(161, EncodeJson(Json{ { "type", "adminAction" }, { "action", "kick" }, { "playerId", 7 } }), 14001.0, 14001.0);
        assert(!HasType(out, 161, "adminResult"));
        assert(host.TakePendingAdminActions().empty());
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        const auto player = HelloReady(host, 171, "co-admin", true, 15000.0);
        const auto out = host.HandleMessage(171, EncodeJson(Json{ { "type", "adminAction" }, { "action", "kick" }, { "requestId", "k1" }, { "playerId", 7 }, { "reason", "spam" } }), 15001.0, 15001.0);
        assert(out.empty());
        const auto pending = host.TakePendingAdminActions();
        assert(pending.size() == 1u && pending[0].connectionId == 171u && pending[0].playerId == player);
        assert(pending[0].profileId == "co-admin" && pending[0].action.action == "kick" && pending[0].action.requestId == "k1");
        assert(host.TakePendingAdminActions().empty());
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        HelloReady(host, 181, "co-admin", true, 16000.0);
        std::optional<std::uint32_t> mutedPlayer;
        std::string mutedProfile;
        bool muted = false;
        std::optional<double> seconds;
        std::string time;
        std::uint32_t banned = 0;
        std::string banReason;
        bool ipBanCalled = false;
        std::string unbannedIp;
        AdminCommandHooks hooks;
        hooks.voiceMute = [&](std::optional<std::uint32_t> a_playerId, std::string_view a_profileId, bool a_muted, std::optional<double> a_seconds) {
            mutedPlayer = a_playerId;
            mutedProfile = std::string{ a_profileId };
            muted = a_muted;
            seconds = a_seconds;
            return AdminActionResult{ true, "muted", nullptr };
        };
        hooks.setWorldTime = [&](std::string_view a_hhmm) {
            time = std::string{ a_hhmm };
            return AdminActionResult{ true, "time", nullptr };
        };
        hooks.banPlayer = [&](std::uint32_t a_playerId, std::string_view a_reason) {
            banned = a_playerId;
            banReason = std::string{ a_reason };
            return AdminActionResult{ true, "banned", nullptr };
        };
        hooks.banIp = [&](std::string_view, std::string_view) {
            ipBanCalled = true;
            return AdminActionResult{ true, "ip", nullptr };
        };
        hooks.unban = [&](std::string_view a_ip) {
            unbannedIp = std::string{ a_ip };
            return AdminActionResult{ true, "unbanned", nullptr };
        };
        AdminCommandDispatcher dispatcher{ hooks };
        host.HandleMessage(181, EncodeJson(Json{ { "type", "adminAction" }, { "action", "unban" }, { "ip", "1.2.3.4" }, { "reason", "" } }), 16004.0, 16004.0);
        host.HandleMessage(181, EncodeJson(Json{ { "type", "adminAction" }, { "action", "voice_mute" }, { "playerId", 9 }, { "seconds", 30 } }), 16001.0, 16001.0);
        host.HandleMessage(181, EncodeJson(Json{ { "type", "adminAction" }, { "action", "world_time" }, { "hhmm", "1200" } }), 16002.0, 16002.0);
        host.HandleMessage(181, EncodeJson(Json{ { "type", "adminAction" }, { "action", "ban" }, { "playerId", 9 }, { "reason", "x" } }), 16003.0, 16003.0);
        std::vector<OutboundFrame> sent;
        DrainAdminMenuActions(host, dispatcher, [&](std::vector<OutboundFrame>& a_frames) { sent.insert(sent.end(), a_frames.begin(), a_frames.end()); });
        assert(mutedPlayer && *mutedPlayer == 9u && mutedProfile.empty() && muted && seconds && *seconds == 30.0);
        assert(time == "1200");
        assert(banned == 9u && banReason == "x" && !ipBanCalled);
        assert(unbannedIp == "1.2.3.4");
        assert(sent.size() == 4);
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        HelloReady(host, 191, "co-admin", true, 17000.0);
        const auto path = (std::filesystem::temp_directory_path() / "co-admin-audit-191.log").string();
        std::remove(path.c_str());
        host.SetAdminAuditPath(path);
        AdminCommandHooks hooks;
        hooks.kick = [](std::uint32_t, std::string_view) { return AdminActionResult{ false, "no such player", nullptr }; };
        AdminCommandDispatcher dispatcher{ hooks };
        host.HandleMessage(191, EncodeJson(Json{ { "type", "adminAction" }, { "action", "kick" }, { "requestId", "f1" }, { "playerId", 7 } }), 17001.0, 17001.0);
        std::vector<OutboundFrame> sent;
        DrainAdminMenuActions(host, dispatcher, [&](std::vector<OutboundFrame>& a_frames) { sent.insert(sent.end(), a_frames.begin(), a_frames.end()); });
        const auto result = FindResult(sent, 191);
        assert(result.at("ok") == false && result.at("message") == "no such player" && result.at("requestId") == "f1");
        const auto logged = ReadLastAudit(path);
        assert(logged.at("action") == "kick" && logged.at("ok") == false && logged.at("reason") == "no such player");
        std::remove(path.c_str());
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        HelloReady(host, 231, "co-admin", true, 21000.0);
        const auto path = (std::filesystem::temp_directory_path() / "co-admin-audit-231.log").string();
        std::remove(path.c_str());
        host.SetAdminAuditPath(path);
        host.HandleMessage(231, EncodeJson(Json{ { "type", "adminAction" }, { "action", "unban" }, { "ip", "1.2.3.4" } }), 21001.0, 21001.0);
        AdminCommandHooks hooks;
        hooks.unban = [](std::string_view) { return AdminActionResult{ true, "unbanned", nullptr }; };
        AdminCommandDispatcher dispatcher{ hooks };
        DrainAdminMenuActions(host, dispatcher, [](std::vector<OutboundFrame>&) {});
        const auto logged = ReadLastAudit(path);
        assert(logged.at("action") == "unban" && logged.at("target") == "1.2.3.4" && logged.at("ok") == true);
        std::remove(path.c_str());
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        HelloReady(host, 201, "co-admin", true, 18000.0);
        const auto path = (std::filesystem::temp_directory_path() / "co-admin-audit-201.log").string();
        std::remove(path.c_str());
        host.SetAdminAuditPath(path);
        host.HandleMessage(201, EncodeJson(Json{ { "type", "adminAction" }, { "action", "kick" }, { "playerId", 7 } }), 18001.0, 18001.0);
        host.SetAdminProfiles({});
        bool called = false;
        AdminCommandHooks hooks;
        hooks.kick = [&](std::uint32_t, std::string_view) {
            called = true;
            return AdminActionResult{ true, "kicked", nullptr };
        };
        AdminCommandDispatcher dispatcher{ hooks };
        int sends = 0;
        DrainAdminMenuActions(host, dispatcher, [&](std::vector<OutboundFrame>&) { ++sends; });
        assert(!called && sends == 0);
        assert(host.TakePendingAdminActions().empty());
        const auto logged = ReadLastAudit(path);
        assert(logged.at("ok") == false && logged.at("reason") == "not-admin");
        std::remove(path.c_str());
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin", "co-other" } };
        HelloReady(host, 211, "co-admin", true, 19000.0);
        host.HandleMessage(211, EncodeJson(Json{ { "type", "adminAction" }, { "action", "kick" }, { "playerId", 7 } }), 19001.0, 19001.0);
        host.HandleDisconnected(211, 19002.0);
        HelloReady(host, 211, "co-other", true, 19003.0);
        bool called = false;
        AdminCommandHooks hooks;
        hooks.kick = [&](std::uint32_t, std::string_view) {
            called = true;
            return AdminActionResult{ true, "kicked", nullptr };
        };
        AdminCommandDispatcher dispatcher{ hooks };
        int sends = 0;
        DrainAdminMenuActions(host, dispatcher, [&](std::vector<OutboundFrame>&) { ++sends; });
        assert(!called && sends == 0);
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        HelloReady(host, 221, "co-admin", true, 20000.0);
        host.HandleMessage(221, EncodeJson(Json{ { "type", "adminAction" }, { "action", "kick" }, { "requestId", "k2" }, { "playerId", 7 }, { "reason", "spam" } }), 20001.0, 20001.0);
        AdminCommandHooks hooks;
        std::uint32_t kicked = 0;
        std::string reason;
        hooks.kick = [&](std::uint32_t a_playerId, std::string_view a_reason) {
            kicked = a_playerId;
            reason = std::string{ a_reason };
            return AdminActionResult{ true, "kicked", Json{ { "playerId", a_playerId } } };
        };
        AdminCommandDispatcher dispatcher{ hooks };
        std::vector<OutboundFrame> sent;
        DrainAdminMenuActions(host, dispatcher, [&](std::vector<OutboundFrame>& a_frames) { sent.insert(sent.end(), a_frames.begin(), a_frames.end()); });
        assert(kicked == 7u && reason == "spam");
        const auto result = FindResult(sent, 221);
        assert(result.at("ok") == true && result.at("action") == "kick" && result.at("requestId") == "k2");
        assert(result.at("message") == "kicked" && (result.at("data") == Json{ { "playerId", 7 } }));
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        HelloReady(host, 261, "co-admin", true, 22500.0);
        const auto path = (std::filesystem::temp_directory_path() / "co-admin-audit-261.log").string();
        std::remove(path.c_str());
        host.SetAdminAuditPath(path);
        host.HandleMessage(261, EncodeJson(Json{ { "type", "adminAction" }, { "action", "kick" }, { "requestId", "x1" }, { "playerId", 7 } }), 22501.0, 22501.0);
        host.HandleMessage(261, EncodeJson(Json{ { "type", "adminAction" }, { "action", "world_time" }, { "requestId", "w1" }, { "hhmm", "1200" } }), 22502.0, 22502.0);
        std::string time;
        AdminCommandHooks hooks;
        hooks.kick = [](std::uint32_t, std::string_view) -> AdminActionResult { throw std::runtime_error("boom"); };
        hooks.setWorldTime = [&](std::string_view a_hhmm) {
            time = std::string{ a_hhmm };
            return AdminActionResult{ true, "time", nullptr };
        };
        AdminCommandDispatcher dispatcher{ hooks };
        std::vector<OutboundFrame> sent;
        DrainAdminMenuActions(host, dispatcher, [&](std::vector<OutboundFrame>& a_frames) { sent.insert(sent.end(), a_frames.begin(), a_frames.end()); });
        assert(time == "1200");
        assert(sent.size() == 2);
        const auto kick = FindResult(sent, 261);
        assert(kick.at("ok") == false && kick.at("message") == "The server could not run that action. Check the server log." && kick.at("requestId") == "x1");
        const auto lines = ReadAuditLines(path);
        assert(lines.size() == 2);
        assert(lines[0].at("action") == "kick" && lines[0].at("ok") == false && lines[0].at("reason") == "exception: boom");
        assert(lines[1].at("action") == "world_time" && lines[1].at("ok") == true);
        std::remove(path.c_str());
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        HelloReady(host, 271, "co-admin", true, 23000.0);
        const auto path = (std::filesystem::temp_directory_path() / "co-admin-audit-271.log").string();
        std::remove(path.c_str());
        host.SetAdminAuditPath(path);
        const std::vector<Json> queued{
            Json{ { "type", "adminAction" }, { "action", "profile_unban" }, { "profileId", "co-player" } },
            Json{ { "type", "adminAction" }, { "action", "faction_disband" }, { "factionId", "f1" } },
            Json{ { "type", "adminAction" }, { "action", "approve" }, { "name", "stage" } },
            Json{ { "type", "adminAction" }, { "action", "world_time" }, { "hhmm", "1200" } },
            Json{ { "type", "adminAction" }, { "action", "voice_mute" }, { "profileId", "co-player" }, { "seconds", 30 } },
        };
        double t = 23001.0;
        for (const auto& packet : queued) {
            host.HandleMessage(271, EncodeJson(packet), t, t);
            t += 1.0;
        }
        AdminCommandHooks hooks;
        AdminCommandDispatcher dispatcher{ hooks };
        DrainAdminMenuActions(host, dispatcher, [](std::vector<OutboundFrame>&) {});
        const auto lines = ReadAuditLines(path);
        assert(lines.size() == 5);
        assert(lines[0].at("target") == "co-player" && lines[1].at("target") == "f1" && lines[2].at("target") == "stage");
        assert(lines[3].at("target") == "1200" && lines[4].at("target") == "co-player");
        std::remove(path.c_str());
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        const auto first = HelloReady(host, 281, "co-admin", true, 24000.0);
        host.HandleMessage(281, EncodeJson(Json{ { "type", "adminAction" }, { "action", "kick" }, { "playerId", 7 } }), 24001.0, 24001.0);
        host.HandleDisconnected(281, 24002.0);
        const auto second = HelloReady(host, 281, "co-admin", true, 24003.0);
        assert(second != first);
        bool called = false;
        AdminCommandHooks hooks;
        hooks.kick = [&](std::uint32_t, std::string_view) {
            called = true;
            return AdminActionResult{ true, "kicked", nullptr };
        };
        AdminCommandDispatcher dispatcher{ hooks };
        int sends = 0;
        DrainAdminMenuActions(host, dispatcher, [&](std::vector<OutboundFrame>&) { ++sends; });
        assert(!called && sends == 0);
    }
    {
        ProtocolHost host{ "Admin Menu", "", { "co-admin" } };
        HelloReady(host, 291, "co-admin", true, 25000.0);
        host.HandleMessage(291, EncodeJson(Json{ { "type", "adminAction" }, { "action", "kick" }, { "requestId", "t1" }, { "playerId", 7 } }), 25001.0, 25001.0);
        const auto pending = host.TakePendingAdminActions();
        assert(pending.size() == 1u);
        const auto frames = host.CompleteAdminAction(pending.at(0), Json{ { "ok", "yes" }, { "message", 5 }, { "data", 3 } });
        const auto result = FindResult(frames, 291);
        assert(result.at("ok") == false && result.at("message") == "" && result.at("data") == 3);
    }
    {
        AdminMenuService svc;
        CommonwealthOnline::Server::AdminMenuPeer self{ 301, 30, "co-admin", "Admin" };
        CommonwealthOnline::Server::ProtocolStats stats;
        F4T::PacketRateLimit::TokenBucket bucket;
        bool logged = false;
        for (int i = 0; i < static_cast<int>(AdminMenuService::kBurst); ++i) {
            const auto out = svc.Dispatch("adminAction", self, bucket, true, logged, {}, Json{{"type", "adminAction"}, {"action", "ping"}}, 26000.0, 26000.0, svc.audit, stats);
            assert(out && FindResult(*out, 301).at("ok") == true);
        }
        const auto limited = svc.Dispatch("adminAction", self, bucket, true, logged, {}, Json{{"type", "adminAction"}, {"action", "ping"}, {"requestId", "r9"}}, 26000.0, 26000.0, svc.audit, stats);
        assert(limited);
        const auto result = FindResult(*limited, 301);
        assert(result.at("ok") == false && result.at("requestId") == "r9");
        assert(result.at("message").get<std::string>().find("Too many") != std::string::npos);
        assert(stats.adminRejected == 1);
    }
    return 0;
}
