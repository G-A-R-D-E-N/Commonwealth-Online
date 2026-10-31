#include "ServerProtocol.h"
#include "ServerAdminMenu.h"
#include "F4TGnsSnapshotEnvelope.h"
#include <nlohmann/json.hpp>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
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
}
int main()
{
    using CommonwealthOnline::Server::ProtocolHost;
    using CommonwealthOnline::Server::AdminMenuService;
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
        int answered = 0, empty = 0;
        for (int i = 0; i < 20; ++i) {
            const auto out = host.HandleMessage(61, EncodeJson(Json{{"type", "adminAction"}, {"action", "ping"}}), 6001.0, 6001.0);
            if (HasType(out, 61, "adminResult")) ++answered;
            else ++empty;
        }
        assert(answered > 0 && empty > 0);
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
        assert(HasType(caseReady, 95, "adminState"));
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
    return 0;
}
