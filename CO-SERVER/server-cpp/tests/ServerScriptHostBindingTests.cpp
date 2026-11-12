#include "ServerProtocol.h"
#include "ServerScriptHostBinding.h"
#include "ServerScriptResources.h"

#include "F4TGnsSnapshotEnvelope.h"
#include "F4TProfanityFilter.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Scripting = CommonwealthOnline::Server::Scripting;
using CommonwealthOnline::Server::OutboundFrame;
using CommonwealthOnline::Server::ProtocolHost;
using Json = nlohmann::json;

namespace
{
	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	Json DecodePacket(const OutboundFrame& a_frame)
	{
		std::span<const std::uint8_t> payload{ a_frame.payload };
		F4T::GnsSnapshotEnvelope::Header header{};
		if (F4T::GnsSnapshotEnvelope::Decode(a_frame.payload, header) == F4T::GnsSnapshotEnvelope::DecodeResult::Valid) {
			payload = payload.subspan(F4T::GnsSnapshotEnvelope::HeaderSize);
		}
		return Json::parse(std::string{ reinterpret_cast<const char*>(payload.data()), payload.size() });
	}

	std::vector<Json> Packets(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		std::vector<Json> packets;
		for (const auto& frame : a_frames) {
			if (frame.connectionId != a_connectionId || frame.payload.empty()) {
				continue;
			}
			auto packet = DecodePacket(frame);
			if (packet.value("type", std::string{}) == a_type) {
				packets.push_back(std::move(packet));
			}
		}
		return packets;
	}

	void JoinPlayer(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		(void)a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		const auto ready = a_host.HandleMessage(a_connectionId, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), a_monotonicSeconds, a_unixTimeSeconds + 0.1);
		assert(!Packets(ready, a_connectionId, "sessionReady").empty());
	}

	void JoinCapablePlayer(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		(void)a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		const auto ready = a_host.HandleMessage(
			a_connectionId,
			EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "state-bag-v1" }) } }),
			a_monotonicSeconds,
			a_unixTimeSeconds + 0.1);
		assert(!Packets(ready, a_connectionId, "sessionReady").empty());
	}

	std::vector<OutboundFrame> Chat(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_text, double a_monotonicSeconds)
	{
		return a_host.HandleMessage(a_connectionId, EncodeJson(Json{ { "type", "chatSend" }, { "channel", "global" }, { "text", a_text } }), a_monotonicSeconds, 100.0 + a_monotonicSeconds);
	}

	void WriteFile(const std::filesystem::path& a_path, std::string_view a_text)
	{
		std::filesystem::create_directories(a_path.parent_path());
		std::ofstream out(a_path, std::ios::binary | std::ios::trunc);
		out << a_text;
	}

	std::string StateOf(const Scripting::ResourceManager& a_manager, std::string_view a_name)
	{
		for (const auto& item : a_manager.List()) {
			if (item.at("name") == std::string{ a_name }) {
				return item.at("state").get<std::string>();
			}
		}
		return {};
	}

	bool IsServerMessage(const Json& a_packet, std::string_view a_text)
	{
		return a_packet.at("text") == std::string{ a_text } && a_packet.at("fromPlayerId") == 0xFFFFFFFFU && a_packet.at("fromName") == "Server";
	}

	std::uint32_t PlayerIdFor(const ProtocolHost& a_host, std::uint32_t a_connectionId)
	{
		for (const auto& player : a_host.ScriptPlayers()) {
			if (player.value("connectionId", std::uint32_t{}) == a_connectionId) {
				return player.at("id").get<std::uint32_t>();
			}
		}
		return 0;
	}

	void DumpFrames(const std::vector<OutboundFrame>& a_frames)
	{
		for (const auto& frame : a_frames) {
			try {
				const auto packet = DecodePacket(frame);
				const auto type = packet.value("type", std::string{});
				std::fprintf(stderr, "    frame conn %u type %s", frame.connectionId, type.c_str());
				if (packet.contains("text")) {
					std::fprintf(stderr, " text %s", packet.at("text").dump().c_str());
				}
				std::fprintf(stderr, "\n");
			}
			catch (...) {
				std::fprintf(stderr, "    undecodable frame conn %u\n", frame.connectionId);
			}
		}
	}
}

int main()
{
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto root = std::filesystem::temp_directory_path() / ("co-script-host-tests-" + std::to_string(stamp));
	std::filesystem::remove_all(root);
	WriteFile(root / "cmds" / "co-resource.json", R"({"name":"cmds","version":"1.0.0","server":["index.js"],"permissions":["commands.register","chat.send","players.read","world.read"],"errorBehavior":"restart"})");
	WriteFile(root / "cmds" / "index.js",
		"const p = CO.players.list()[0];"
		"CO.chat.broadcast('loaded ' + CO.players.list().length + ' ready ' + p.ready + ' profile ' + (p.profileId || '-') + ' faction ' + (p.factionId === null ? 'none' : p.factionId) + ' steam ' + typeof p.steamId);"
		"CO.commands.register('ping', (args, playerId) => CO.chat.send(playerId, 'pong ' + args.join(',')));"
		"CO.commands.register('secret', (args, playerId) => CO.chat.send(playerId, 'secret ' + args.join(',')), { adminOnly: true });"
		"CO.commands.register('world', (args, playerId) => CO.chat.send(playerId, 'mode ' + CO.world.get().mode));"
		"CO.commands.register('say', (args) => CO.chat.broadcast(args.join(' ')));"
		"CO.commands.register('spin', () => { for (;;) {} });");

	{
		ProtocolHost host{ "Script Host Binding", {}, { "admin-profile" } };
		host.SetChatEnabled(true);
		JoinPlayer(host, 10, 1.0, 100.0);
		JoinPlayer(host, 20, 1.1, 100.1);
		const Json world{ { "timeHHmm", "1200" } };
		std::vector<OutboundFrame> sent;
		Scripting::ScriptHostBinding binding{ host, world, [&sent, &host](std::vector<OutboundFrame>& a_frames) {
				sent.insert(sent.end(), a_frames.begin(), a_frames.end());
				for (auto deferred = host.TakeDeferredFrames(); !deferred.empty(); deferred = host.TakeDeferredFrames()) {
					sent.insert(sent.end(), deferred.begin(), deferred.end());
				}
			} };
		Scripting::ResourceManager manager{ root, Scripting::Limits{}, {}, binding.Installer() };

		manager.Tick(0.0);
		const auto approveError = manager.Approve("cmds");
		assert(approveError.empty());
		assert(StateOf(manager, "cmds") == "running");
		const auto loadedTo10 = Packets(sent, 10, "chatMessage");
		const auto loadedTo20 = Packets(sent, 20, "chatMessage");
		assert(loadedTo10.size() == 1 && IsServerMessage(loadedTo10.front(), "loaded 2 ready false profile - faction none steam undefined"));
		assert(loadedTo20.size() == 1 && IsServerMessage(loadedTo20.front(), "loaded 2 ready false profile - faction none steam undefined"));
		const auto loadedId = loadedTo10.front().at("messageId").get<std::uint64_t>();
		assert(loadedId == 1);
		assert(loadedTo20.front().at("messageId") == loadedId);
		sent.clear();

		const auto reply = Chat(host, 10, "/ping a b", 2.0);
		const auto pong = Packets(reply, 10, "chatMessage");
		assert(pong.size() == 1 && IsServerMessage(pong.front(), "pong a,b"));
		assert(Packets(reply, 20, "chatMessage").empty());
		assert(sent.empty());

		const auto worldReply = Chat(host, 10, "/world", 3.0);
		const auto worldPackets = Packets(worldReply, 10, "chatMessage");
		assert(worldPackets.size() == 1 && IsServerMessage(worldPackets.front(), "mode coop-story"));
		assert(Packets(worldReply, 20, "chatMessage").empty());

		const auto spun = Chat(host, 10, "/spin", 4.0);
		assert(Packets(spun, 20, "chatMessage").empty());

		const auto relayed = Chat(host, 10, "/ping c", 6.0);
		const auto relayedTo20 = Packets(relayed, 20, "chatMessage");
		assert(relayedTo20.size() == 1 && relayedTo20.front().at("text") == "/ping c");
		assert(relayedTo20.front().at("messageId").get<std::uint64_t>() > loadedId);

		manager.Tick(7.0);
		assert(StateOf(manager, "cmds") == "running");
		assert(Packets(sent, 10, "chatMessage").size() == 1);
		sent.clear();

		const auto again = Chat(host, 10, "/ping d", 8.0);
		const auto pongAgain = Packets(again, 10, "chatMessage");
		assert(pongAgain.size() == 1 && IsServerMessage(pongAgain.front(), "pong d"));
		assert(sent.empty());

		const auto refused = Chat(host, 10, "/secret x", 20.0);
		const auto refusedNotice = Packets(refused, 10, "chatMessage");
		assert(refusedNotice.size() == 1 && IsServerMessage(refusedNotice.front(), "That command requires an administrator."));
		assert(Packets(refused, 20, "chatMessage").empty());

		host.BindSessionProfileVerified(10, "admin-profile", true, "");
		const auto allowed = Chat(host, 10, "/secret x", 22.0);
		const auto secret = Packets(allowed, 10, "chatMessage");
		assert(secret.size() == 1 && IsServerMessage(secret.front(), "secret x"));
		assert(Packets(allowed, 20, "chatMessage").empty());
		assert(sent.empty());

		assert(F4T::ProfanityFilter::LoadWords("[slurs]\n*nigger\n[profanity]\n+fuck\n"));
		host.HandleMessage(20, EncodeJson(Json{ { "type", "chatHistoryRequest" }, { "profanityFilter", true } }), 30.0, 130.0);
		const auto said = Chat(host, 10, "/say what the fuck", 26.0);
		const auto saidTo10 = Packets(said, 10, "chatMessage");
		const auto saidTo20 = Packets(said, 20, "chatMessage");
		assert(saidTo10.size() == 1 && saidTo10.front().at("text") == "what the fuck");
		assert(saidTo20.size() == 1 && saidTo20.front().at("text") == "what the ****");
		assert(saidTo10.front().at("messageId") == saidTo20.front().at("messageId"));
	}

	{
		ProtocolHost host{ "S4" };
		host.SetChatEnabled(true);
		JoinCapablePlayer(host, 10, 1.0, 100.0);
		JoinCapablePlayer(host, 20, 1.1, 100.1);
		const Json world{ { "timeHHmm", "1200" } };
		std::vector<OutboundFrame> sent;
		Scripting::ScriptHostBinding binding{ host, world, [&sent, &host](std::vector<OutboundFrame>& a_frames) {
				sent.insert(sent.end(), a_frames.begin(), a_frames.end());
				for (auto deferred = host.TakeDeferredFrames(); !deferred.empty(); deferred = host.TakeDeferredFrames()) {
					sent.insert(sent.end(), deferred.begin(), deferred.end());
				}
			} };
		const auto s4Root = root / "s4root";
		WriteFile(
			s4Root / "s4" / "co-resource.json",
			R"({"name":"s4","version":"1.0.0","server":["index.js"],"permissions":["events.subscribe","state.read","state.write","commands.register","chat.send","views.message"]})");
		WriteFile(
			s4Root / "s4" / "index.js",
			"CO.events.on('chat', (e) => e.data.text !== 'cursed');"
			"CO.commands.register('bump', () => { const next = (CO.state.get('global').wave || 0) + 1; CO.state.set('global', 'wave', next); CO.chat.broadcast('wave ' + next); });"
			"CO.views.onMessage((m) => CO.chat.send(m.playerId, 'view ' + m.data.action));");
		Scripting::ResourceManager manager{ s4Root, Scripting::Limits{}, {}, binding.Installer() };
		manager.Tick(0.0);
		assert(manager.Approve("s4").empty());
		assert(StateOf(manager, "s4") == "running");
		sent.clear();

		const auto allowed = Chat(host, 10, "hello", 10.0);
		assert(Packets(allowed, 20, "chatMessage").size() == 1);
		const auto refused = Chat(host, 10, "cursed", 12.0);
		assert(Packets(refused, 20, "chatMessage").empty());

		const auto bumped = Chat(host, 10, "/bump", 14.0);
		const auto wave = Packets(bumped, 10, "chatMessage");
		assert(wave.size() == 1 && IsServerMessage(wave.front(), "wave 1"));
		const auto bumpedAgain = Chat(host, 10, "/bump", 16.0);
		const auto waveAgain = Packets(bumpedAgain, 10, "chatMessage");
		assert(waveAgain.size() == 1 && IsServerMessage(waveAgain.front(), "wave 2"));

		sent.clear();
		const auto scriptPlayerId = PlayerIdFor(host, 10);
		std::string openFailure;
		assert(host.OpenResourceView(scriptPlayerId, "s4", "web/menu.html", openFailure).size() == 1);
		sent.clear();
		const auto view = host.HandleMessage(
			10,
			EncodeJson(Json{ { "type", "resourceMessage" }, { "resource", "s4" }, { "data", Json{ { "action", "press" } } } }),
			18.0,
			118.0);
		assert(Packets(view, 10, "chatMessage").empty());
		const auto viewReply = Packets(sent, 10, "chatMessage");
		if (viewReply.size() != 1 || !IsServerMessage(viewReply.front(), "view press")) {
			std::fprintf(
				stderr,
				"S4 view reply: expected 1 chatMessage on connection 10, got %zu (sent %zu frames), scriptPlayerId %u\n",
				viewReply.size(),
				sent.size(),
				scriptPlayerId);
			DumpFrames(sent);
			for (const auto& player : host.ScriptPlayers()) {
				std::fprintf(stderr, "    player conn %u id %u\n", player.value("connectionId", std::uint32_t{}), player.value("id", std::uint32_t{}));
			}
		}
		assert(viewReply.size() == 1 && IsServerMessage(viewReply.front(), "view press"));
		sent.clear();
		const auto unopened = host.HandleMessage(
			10,
			EncodeJson(Json{ { "type", "resourceMessage" }, { "resource", "closed" }, { "data", Json{ { "action", "press" } } } }),
			20.0,
			120.0);
		assert(Packets(unopened, 10, "chatMessage").empty());
		assert(Packets(sent, 10, "chatMessage").empty());
	}

	{
		ProtocolHost host{ "S4Order" };
		host.SetChatEnabled(true);
		JoinCapablePlayer(host, 10, 1.0, 100.0);
		const Json world{ { "timeHHmm", "1200" } };
		std::vector<OutboundFrame> sent;
		Scripting::ScriptHostBinding binding{ host, world, [&sent, &host](std::vector<OutboundFrame>& a_frames) {
				sent.insert(sent.end(), a_frames.begin(), a_frames.end());
				for (auto deferred = host.TakeDeferredFrames(); !deferred.empty(); deferred = host.TakeDeferredFrames()) {
					sent.insert(sent.end(), deferred.begin(), deferred.end());
				}
			} };
		const auto orderRoot = root / "order";
		WriteFile(
			orderRoot / "ord" / "co-resource.json",
			R"({"name":"ord","version":"1.0.0","server":["index.js"],"permissions":["events.subscribe","state.read","state.write","commands.register"]})");
		WriteFile(
			orderRoot / "ord" / "index.js",
			"CO.state.onChange((e) => { if (e.data.key === 'form' && e.data.value === 'v1') CO.state.set('global', 'form', 'v2'); });"
			"CO.commands.register('form', () => CO.state.set('global', 'form', 'v1'));");
		Scripting::ResourceManager manager{ orderRoot, Scripting::Limits{}, {}, binding.Installer() };
		manager.Tick(0.0);
		assert(manager.Approve("ord").empty());
		assert(StateOf(manager, "ord") == "running");
		sent.clear();

		const auto commandFrames = Chat(host, 10, "/form", 10.0);
		const auto changes = Packets(commandFrames, 10, "stateBag");
		if (changes.size() != 2) {
			std::fprintf(stderr, "S4Order state bag: expected 2 stateBag frames on connection 10, got %zu (sent %zu frames)\n", changes.size(), sent.size());
			DumpFrames(commandFrames);
			DumpFrames(sent);
		}
		assert(changes.size() == 2);
		assert(changes.at(0).at("value") == "v1");
		assert(changes.at(1).at("value") == "v2");
		assert(host.TakeDeferredFrames().empty());
		assert(host.ReadStateBag(F4T::StateBagProtocol::BagId{}).at("ord.form") == "v2");
		assert(sent.empty());
	}

	{
		ProtocolHost host{ "Players" };
		host.SetChatEnabled(true);
		JoinPlayer(host, 10, 1.0, 100.0);
		JoinPlayer(host, 20, 1.1, 100.1);
		const Json world{ { "timeHHmm", "1200" } };
		std::vector<OutboundFrame> sent;
		Scripting::ScriptHostBinding binding{ host, world, [&sent, &host](std::vector<OutboundFrame>& a_frames) {
				sent.insert(sent.end(), a_frames.begin(), a_frames.end());
				for (auto deferred = host.TakeDeferredFrames(); !deferred.empty(); deferred = host.TakeDeferredFrames()) {
					sent.insert(sent.end(), deferred.begin(), deferred.end());
				}
			} };
		const auto playerIdOf = [&host](std::uint32_t a_connectionId) {
			for (const auto& player : host.ScriptPlayers()) {
				if (player.value("connectionId", std::uint32_t{}) == a_connectionId) return player.at("id").get<std::uint32_t>();
			}
			return std::uint32_t{};
		};
		const auto alive = std::to_string(playerIdOf(10));
		const auto playersRoot = root / "players";
		WriteFile(
			playersRoot / "adm" / "co-resource.json",
			R"({"name":"adm","version":"1.0.0","server":["index.js"],"permissions":["players.write","chat.send"]})");
		WriteFile(
			playersRoot / "adm" / "index.js",
			"const f = (r) => r.ok ? 'ok' : r.error;"
			"CO.chat.broadcast('revive-alive ' + f(CO.players.revive(" + alive + ")));"
			"CO.chat.broadcast('revive-missing ' + f(CO.players.revive(999)));"
			"CO.chat.broadcast('kill-first ' + f(CO.players.kill(" + alive + ")));"
			"CO.chat.broadcast('kill-again ' + f(CO.players.kill(" + alive + ")));"
			"CO.chat.broadcast('kill-missing ' + f(CO.players.kill(999)));");
		Scripting::ResourceManager manager{ playersRoot, Scripting::Limits{}, {}, binding.Installer() };
		manager.Tick(0.0);
		assert(manager.Approve("adm").empty());
		assert(StateOf(manager, "adm") == "running");
		std::vector<std::string> texts;
		for (const auto& packet : Packets(sent, 10, "chatMessage")) {
			texts.push_back(packet.at("text").get<std::string>());
		}
		assert((texts == std::vector<std::string>{
					"revive-alive nothing to do",
					"revive-missing player is not connected",
					"kill-first ok",
					"kill-again nothing to do",
					"kill-missing player is not connected" }));
	}

	std::filesystem::remove_all(root);
	return 0;
}
