#include "ServerScriptApi.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Scripting = CommonwealthOnline::Server::Scripting;
using Json = nlohmann::json;

namespace
{
	using Lines = std::vector<std::string>;

	bool Contains(const Lines& a_lines, std::string_view a_text)
	{
		return std::find(a_lines.begin(), a_lines.end(), a_text) != a_lines.end();
	}

	bool AnyStartsWith(const Lines& a_lines, std::string_view a_prefix)
	{
		return std::any_of(a_lines.begin(), a_lines.end(), [a_prefix](const std::string& a_line) { return a_line.starts_with(a_prefix); });
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

	Scripting::LogSink Capture(Lines& a_lines)
	{
		return [&a_lines](std::string_view a_line) { a_lines.emplace_back(a_line); };
	}

	struct FakeServer
	{
		Lines chats;
		Lines calls;
		Lines viewCalls;
		Json state = Json::object();
		Scripting::ScriptEventFilter filter;
		CommonwealthOnline::Server::ServerEventKind kind{};
	};

	Scripting::ScriptHost MakeHost(FakeServer& a_fake, std::function<double()> a_clock = {})
	{
		return Scripting::ScriptHost{
			.listPlayers = [] { return Json::array({ Json{ { "id", 7 }, { "name", "Alpha" } } }); },
			.getPlayer = [](std::uint32_t a_playerId) { return a_playerId == 7 ? Json{ { "id", 7 }, { "name", "Alpha" } } : Json{}; },
			.sendChat = [&a_fake](std::uint32_t a_playerId, std::string_view a_text) {
				a_fake.chats.push_back(std::to_string(a_playerId) + ":" + std::string{ a_text });
				return a_playerId == 7;
			},
			.broadcastChat = [&a_fake](std::string_view a_text) { a_fake.chats.push_back("*:" + std::string{ a_text }); },
			.readWorld = [] { return Json{ { "timeHHmm", "1200" } }; },
			.readPolicy = [] { return Json{ { "type", "worldPolicy" }, { "mode", "coop" }, { "version", 3 } }; },
			.setWorldPolicy = [&a_fake](const Json& a_policy, std::string& a_failure) {
				if (!a_policy.is_object() || !a_policy.contains("doorsShared")) {
					a_failure = "the object is not a valid world policy";
					return false;
				}
				a_fake.calls.push_back("world.set");
				return true;
			},
			.listFactions = [] { return Json::array({ Json{ { "id", "f1" } } }); },
			.disbandFaction = [&a_fake](std::string_view a_factionId, std::string& a_failure) {
				if (a_factionId == "nope") {
					a_failure = "no such faction";
					return false;
				}
				a_fake.calls.push_back("disband " + std::string{ a_factionId });
				return true;
			},
			.promoteFaction = [&a_fake](std::string_view a_factionId, std::string_view a_profileId, std::string&) {
				a_fake.calls.push_back("promote " + std::string{ a_factionId } + " " + std::string{ a_profileId });
				return true;
			},
			.kickFaction = [&a_fake](std::string_view a_factionId, std::string_view a_profileId, std::string&) {
				a_fake.calls.push_back("kick " + std::string{ a_factionId } + " " + std::string{ a_profileId });
				return true;
			},
			.questStatus = [] { return Json{ { "mode", "coop" } }; },
			.setQuestMode = [&a_fake](std::string_view a_mode) {
				if (a_mode != "coop" && a_mode != "solo") return false;
				a_fake.calls.push_back("mode " + std::string{ a_mode });
				return true;
			},
			.setQuestGate = [&a_fake](std::string_view a_quest, std::uint16_t a_stage, bool a_release) {
				a_fake.calls.push_back("gate " + std::string{ a_quest } + " " + std::to_string(a_stage) + (a_release ? " open" : " shut"));
				return true;
			},
			.setQuestSolo = [&a_fake](std::string_view a_profileId, std::string_view a_quest, bool a_solo) {
				a_fake.calls.push_back("solo " + std::string{ a_profileId } + " " + std::string{ a_quest } + (a_solo ? " on" : " off"));
				return true;
			},
			.setSettlementOwner = [&a_fake](std::uint64_t a_zone, std::string_view a_owner, std::string& a_failure) {
				if (a_owner.empty()) {
					a_failure = "owner must not be empty";
					return false;
				}
				a_fake.calls.push_back("owner " + std::to_string(a_zone) + " " + std::string{ a_owner });
				return true;
			},
			.readState = [&a_fake](std::string_view a_bag) {
				return a_bag == "global" ? a_fake.state : Json{};
			},
			.setState = [&a_fake](std::string_view a_bag, std::string_view a_key, const Json& a_value, std::string& a_failure) {
				const auto digits = a_bag.starts_with("player:") ? a_bag.substr(7) : std::string_view{};
				const auto bagKnown = a_bag == "global" || (!digits.empty() && std::all_of(digits.begin(), digits.end(), [](char a_digit) { return a_digit >= '0' && a_digit <= '9'; }));
				if (!bagKnown) {
					a_failure = "bag must be 'global' or 'player:<id>'";
					return false;
				}
				if (a_key.empty()) {
					a_failure = "invalid key";
					return false;
				}
				if (a_value.is_null()) {
					a_fake.state.erase(std::string{ a_key });
					a_fake.calls.push_back("delete " + std::string{ a_bag } + " " + std::string{ a_key });
					return true;
				}
				a_fake.state[std::string{ a_key }] = a_value;
				a_fake.calls.push_back("set " + std::string{ a_bag } + " " + std::string{ a_key });
				return true;
			},
			.subscribeEvent = [&a_fake](CommonwealthOnline::Server::ServerEventKind a_kind, Scripting::ScriptEventFilter a_filter) -> std::uint64_t {
				a_fake.kind = a_kind;
				a_fake.filter = std::move(a_filter);
				return a_fake.filter ? 9 : 0;
			},
			.unsubscribeEvent = [&a_fake](std::uint64_t a_token) {
				if (a_token == 9) a_fake.filter = {};
			},
			.openView = [&a_fake](std::string_view a_resource, std::uint32_t a_playerId, std::string_view a_view, std::string& a_failure) {
				if (a_view.find("..") != std::string_view::npos) {
					a_failure = "invalid view path";
					return false;
				}
				a_fake.viewCalls.push_back("open " + std::string{ a_resource } + " " + std::to_string(a_playerId) + " " + std::string{ a_view });
				return true;
			},
			.closeView = [&a_fake](std::string_view a_resource, std::uint32_t a_playerId, std::string& a_failure) {
				if (a_playerId != 9) {
					a_failure = "player has no state-bag-v1 session";
					return false;
				}
				a_fake.viewCalls.push_back("close " + std::string{ a_resource } + " " + std::to_string(a_playerId));
				return true;
			},
			.revivePlayer = [&a_fake](std::uint32_t a_playerId, std::string& a_failure) {
				if (a_playerId != 7) {
					a_failure = "player is not connected";
					return false;
				}
				a_fake.calls.push_back("revive " + std::to_string(a_playerId));
				return true;
			},
			.killPlayer = [&a_fake](std::uint32_t a_playerId, std::string& a_failure) {
				if (a_playerId != 7) {
					a_failure = "player is not connected";
					return false;
				}
				a_fake.calls.push_back("kill " + std::to_string(a_playerId));
				return true;
			},
			.messagePlayer = [&a_fake](std::uint32_t a_playerId, std::string_view a_text) {
				if (a_playerId != 7) return false;
				a_fake.chats.push_back("pm " + std::to_string(a_playerId) + ":" + std::string{ a_text });
				return true;
			},
			.settlementOwner = [&a_fake](std::uint64_t a_zone) {
				a_fake.calls.push_back("ownerof " + std::to_string(a_zone));
				return Json{ { "buildZoneId", a_zone }, { "ownerProfileId", a_zone == 9 ? "p1" : "" } };
			},
			.nowMonotonicSeconds = std::move(a_clock)
		};
	}

	const Lines kAllPermissions{
		"players.read",
		"players.write",
		"chat.send",
		"commands.register",
		"world.read",
		"world.set",
		"events.subscribe",
		"state.read",
		"state.write",
		"factions.read",
		"factions.write",
		"quests.read",
		"quests.write",
		"settlements.read",
		"settlements.write",
		"views.open",
		"views.message"
	};
}

int main()
{
	{
		Lines lines;
		FakeServer fake;
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		Scripting::ScriptRuntime bare{ "bare", Scripting::Limits{}, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views)(bare, {});
		const bool loaded = bare.Load(
			"console.log(typeof CO.players, typeof CO.chat, typeof CO.commands, typeof CO.world, typeof CO.events, typeof CO.state, typeof CO.factions, typeof CO.quests, typeof CO.settlements, typeof CO.views)",
			"bare.js",
			0.0);
		assert(loaded);
		assert(Contains(lines, "[script:bare] undefined undefined undefined undefined undefined undefined undefined undefined undefined undefined"));
	}

	{
		Lines lines;
		FakeServer fake;
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		Scripting::ScriptRuntime sandbox{ "sandbox", Scripting::Limits{}, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views)(sandbox, {});
		const bool loaded = sandbox.Load("console.log(Object.keys(CO).length, typeof setTimeout)", "sandbox.js", 0.0);
		assert(loaded);
		assert(Contains(lines, "[script:sandbox] 0 function"));
	}

	{
		Lines lines;
		FakeServer fake;
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		Scripting::ScriptRuntime api{ "api", Scripting::Limits{}, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views)(api, kAllPermissions);
		const bool loaded = api.Load(
			"console.log('count', CO.players.list().length);"
			"console.log('name', CO.players.get(7).name, CO.players.get(8));"
			"console.log('world', CO.world.get().timeHHmm);"
			"console.log('send', CO.chat.send(7, 'hello'), CO.chat.send(8, 'hello'));"
			"console.log('long', CO.chat.send(7, 'x'.repeat(300)));"
			"CO.chat.broadcast('all hands');",
			"api.js",
			0.0);
		assert(loaded);
		assert(Contains(lines, "[script:api] count 1"));
		assert(Contains(lines, "[script:api] name Alpha null"));
		assert(Contains(lines, "[script:api] world 1200"));
		assert(Contains(lines, "[script:api] send true false"));
		assert(Contains(lines, "[script:api] long false"));
		assert(fake.chats == (Lines{ "7:hello", "8:hello", "*:all hands" }));
	}

	{
		Lines lines;
		FakeServer fake;
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		Scripting::ScriptRuntime api{ "verbs", Scripting::Limits{}, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views)(api, kAllPermissions);
		const bool loaded = api.Load(
			"console.log('factions', CO.factions.list().length);"
			"console.log('disband', CO.factions.disband('f1').ok, CO.factions.disband('nope').error);"
			"console.log('members', CO.factions.promote('f1', 'p1').ok, CO.factions.kick('f1', 'p2').ok);"
			"console.log('quest', CO.quests.status().mode);"
			"console.log('mode', CO.quests.mode('coop').ok, CO.quests.mode('wild').error);"
			"console.log('gate', CO.quests.gate('MOD:000001', 10, true).ok, CO.quests.gate('MOD:000001', 70000, true).error);"
			"console.log('solo', CO.quests.solo('p1', 'MOD:000001', true).ok);"
			"console.log('owner', CO.settlements.setOwner(4321, 'p1').ok, CO.settlements.setOwner(0, 'p1').error);"
			"console.log('current', CO.world.policy().mode, CO.world.policy().version);"
			"console.log('policy', CO.world.set({ doorsShared: true }).ok, CO.world.set({ nope: 1 }).error);",
			"verbs.js",
			0.0);
		assert(loaded);
		assert(Contains(lines, "[script:verbs] factions 1"));
		assert(Contains(lines, "[script:verbs] disband true no such faction"));
		assert(Contains(lines, "[script:verbs] members true true"));
		assert(Contains(lines, "[script:verbs] quest coop"));
		assert(Contains(lines, "[script:verbs] mode true unknown quest mode"));
		assert(Contains(lines, "[script:verbs] gate true stage must be a whole number from 0 to 65535"));
		assert(Contains(lines, "[script:verbs] solo true"));
		assert(Contains(lines, "[script:verbs] owner true zoneId must be a whole number from 1 upward"));
		assert(Contains(lines, "[script:verbs] current coop 3"));
		assert(Contains(lines, "[script:verbs] policy true the object is not a valid world policy"));
		assert(fake.calls == (Lines{
			"disband f1",
			"promote f1 p1",
			"kick f1 p2",
			"mode coop",
			"gate MOD:000001 10 open",
			"solo p1 MOD:000001 on",
			"owner 4321 p1",
			"world.set" }));
	}

	{
		Lines lines;
		FakeServer fake;
		fake.state = Json{ { "state.count", 1 }, { "other.count", 9 } };
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		Scripting::ScriptRuntime api{ "state", Scripting::Limits{}, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views)(api, kAllPermissions);
		const bool loaded = api.Load(
			"console.log('before', CO.state.get('global').count);"
			"console.log('foreign', typeof CO.state.get('global').other);"
			"console.log('set', CO.state.set('global', 'count', 2).ok);"
			"console.log('after', CO.state.get('global').count);"
			"console.log('remove', CO.state.delete('player:7', 'count').ok);"
			"console.log('bad', CO.state.set('nonsense', 'a', 1).error);",
			"state.js",
			0.0);
		assert(loaded);
		assert(Contains(lines, "[script:state] before 1"));
		assert(Contains(lines, "[script:state] foreign undefined"));
		assert(Contains(lines, "[script:state] set true"));
		assert(Contains(lines, "[script:state] after 2"));
		assert(Contains(lines, "[script:state] bad bag must be 'global' or 'player:<id>'"));
		assert(!fake.state.contains("state.count"));
		assert(fake.state.at("other.count") == 9);
	}

	{
		Lines lines;
		FakeServer fake;
		fake.state = Json{ { "beta.secret", 7 } };
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		Scripting::ScriptRuntime alpha{ "alpha", Scripting::Limits{}, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views)(alpha, { "state.read", "state.write" });
		const bool loaded = alpha.Load(
			"console.log('read', typeof CO.state.get('global').secret);"
			"console.log('overwrite', CO.state.set('global', 'secret', 1).ok);"
			"console.log('own', CO.state.get('global').secret);"
			"console.log('roundtrip', CO.state.set('global', 'note', 'mine').ok, CO.state.get('global').note);"
			"console.log('delete', CO.state.delete('global', 'note').ok);"
			"console.log('gone', typeof CO.state.get('global').note);",
			"alpha.js",
			0.0);
		assert(loaded);
		assert(Contains(lines, "[script:alpha] read undefined"));
		assert(Contains(lines, "[script:alpha] overwrite true"));
		assert(Contains(lines, "[script:alpha] own 1"));
		assert(Contains(lines, "[script:alpha] roundtrip true mine"));
		assert(Contains(lines, "[script:alpha] delete true"));
		assert(Contains(lines, "[script:alpha] gone undefined"));
		assert(fake.state.at("beta.secret") == 7);
		assert(!fake.state.contains("secret"));
		assert(fake.state.at("alpha.secret") == 1);
		assert(!fake.state.contains("alpha.note"));
	}

	{
		Lines lines;
		FakeServer fake;
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		Scripting::ScriptRuntime api{ "evt", Scripting::Limits{}, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views)(api, kAllPermissions);
		const bool loaded = api.Load(
			"const token = CO.events.on('chat', (e) => { console.log('evt', e.name, e.playerId, e.data.text); return e.data.text !== 'block'; });"
			"console.log('token', token);",
			"evt.js",
			0.0);
		assert(loaded);
		assert(Contains(lines, "[script:evt] token 9"));
		assert(fake.filter);
		const Json event{ { "name", "chat" }, { "playerId", 7 }, { "profileId", "p1" }, { "data", Json{ { "text", "hi" } } } };
		assert(fake.filter(event));
		assert(Contains(lines, "[script:evt] evt chat 7 hi"));
		auto blocked = event;
		blocked["data"]["text"] = "block";
		assert(!fake.filter(blocked));
		api.Stop();
		assert(!fake.filter);
	}

	{
		Lines lines;
		FakeServer fake;
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		Scripting::ScriptRuntime api{ "evt", Scripting::Limits{}, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views)(api, kAllPermissions);
		const bool loaded = api.Load(
			"try { CO.events.on('stateBagChange', () => true); console.log('subscribed'); } catch (e) { console.log('refused', e.message); }",
			"evt.js",
			0.0);
		assert(loaded);
		assert(Contains(lines, "[script:evt] refused use CO.state.onChange for state changes"));
		assert(!fake.filter);
		api.Stop();
	}

	{
		Lines lines;
		FakeServer fake;
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		Scripting::ScriptRuntime api{ "bag", Scripting::Limits{}, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views)(api, kAllPermissions);
		const bool loaded = api.Load(
			"const token = CO.state.onChange((e) => console.log('changed', e.name, e.data.key));"
			"console.log('token', token);",
			"bag.js",
			0.0);
		assert(loaded);
		assert(Contains(lines, "[script:bag] token 9"));
		assert(fake.kind == CommonwealthOnline::Server::ServerEventKind::StateBagChange);
		const Json event{ { "name", "stateBagChange" }, { "playerId", 0 }, { "profileId", "" }, { "data", Json{ { "bag", "global" }, { "key", "bag.motd" }, { "value", "hi" } } } };
		assert(fake.filter(event));
		assert(Contains(lines, "[script:bag] changed stateBagChange motd"));
		const Json other{ { "name", "stateBagChange" }, { "playerId", 0 }, { "profileId", "" }, { "data", Json{ { "bag", "global" }, { "key", "beta.motd" }, { "value", "leak" } } } };
		assert(fake.filter(other));
		assert(!Contains(lines, "[script:bag] changed stateBagChange beta.motd"));
		assert(!Contains(lines, "[script:bag] changed stateBagChange bag.motd"));
		api.Stop();
		assert(!fake.filter);
	}

	{
		Lines lines;
		FakeServer fake;
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		Scripting::ScriptRuntime api{ "view", Scripting::Limits{}, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views)(api, kAllPermissions);
		const bool loaded = api.Load(
			"CO.views.onMessage((m) => console.log('view', m.playerId, m.data.action));",
			"view.js",
			0.0);
		assert(loaded);
		assert(views.Dispatch("view", 7, Json{ { "action", "press" } }));
		assert(Contains(lines, "[script:view] view 7 press"));
		assert(!views.Dispatch("other", 7, Json::object()));
		api.Stop();
		assert(!views.Dispatch("view", 7, Json{ { "action", "press" } }));
	}

	{
		Lines lines;
		FakeServer fake;
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		const Scripting::Limits limits{ 16U * 1024U * 1024U, std::chrono::milliseconds{ 20 } };
		Scripting::ScriptRuntime api{ "spinner", limits, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views)(api, kAllPermissions);
		const bool loaded = api.Load("CO.views.onMessage(() => { while (true) {} });", "spinner.js", 0.0);
		assert(loaded);
		assert(views.Dispatch("spinner", 7, Json::object()));
		assert(api.GetFault() == Scripting::Fault::CpuBudget);
		assert(!views.Dispatch("spinner", 7, Json::object()));
	}

	{
		Lines lines;
		FakeServer fake;
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		Scripting::ScriptRuntime api{ "show", Scripting::Limits{}, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views)(api, kAllPermissions);
		const bool loaded = api.Load(
			"console.log('open', CO.views.open(7, 'board.html').ok, CO.views.open(0, 'board.html').error);"
			"console.log('bad', CO.views.open(7, '../board.html').error);"
			"console.log('close', CO.views.close(9).ok, CO.views.close(8).error);",
			"show.js",
			0.0);
		assert(loaded);
		assert(Contains(lines, "[script:show] open true playerId must be a whole number from 1 upward"));
		assert(Contains(lines, "[script:show] bad invalid view path"));
		assert(Contains(lines, "[script:show] close true player has no state-bag-v1 session"));
		assert(fake.viewCalls == (Lines{ "open show 7 board.html", "close show 9" }));
	}

	{
		Lines lines;
		FakeServer fake;
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		Scripting::ScriptRuntime api{ "acts", Scripting::Limits{}, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views)(api, kAllPermissions);
		const bool loaded = api.Load(
			"console.log('revive', CO.players.revive(7).ok, CO.players.revive(8).error);"
			"console.log('kill', CO.players.kill(7).ok, CO.players.kill(0).error);"
			"console.log('message', CO.players.message(7, 'hi').ok, CO.players.message(8, 'hi').error);"
			"console.log('owner', CO.settlements.get(9).ownerProfileId, CO.settlements.get(5).buildZoneId);"
			"try { CO.settlements.get(0); } catch (e) { console.log('badzone', e.message); }",
			"acts.js",
			0.0);
		assert(loaded);
		assert(Contains(lines, "[script:acts] revive true player is not connected"));
		assert(Contains(lines, "[script:acts] kill true playerId must be a whole number from 1 upward"));
		assert(Contains(lines, "[script:acts] message true player is not connected"));
		assert(Contains(lines, "[script:acts] owner p1 5"));
		assert(Contains(lines, "[script:acts] badzone zoneId must be a whole number from 1 upward"));
		assert(fake.calls == (Lines{ "revive 7", "kill 7", "ownerof 9", "ownerof 5" }));
		assert(fake.chats == (Lines{ "pm 7:hi" }));
	}

	{
		Lines lines;
		FakeServer fake;
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		Scripting::ScriptRuntime api{ "wonly", Scripting::Limits{}, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views)(api, { "players.write", "settlements.write" });
		const bool loaded = api.Load(
			"console.log('players', typeof CO.players.list, typeof CO.players.revive, typeof CO.players.message);"
			"console.log('settlements', typeof CO.settlements.get, typeof CO.settlements.setOwner);",
			"wonly.js",
			0.0);
		assert(loaded);
		assert(Contains(lines, "[script:wonly] players undefined function function"));
		assert(Contains(lines, "[script:wonly] settlements undefined function"));
	}

	{
		Lines lines;
		FakeServer fake;
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		Scripting::ScriptRuntime api{ "partial", Scripting::Limits{}, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views)(api, { "state.read" });
		const bool loaded = api.Load(
			"console.log('gate', typeof CO.state.get, typeof CO.state.set, typeof CO.events, typeof CO.factions, typeof CO.settlements);"
			"console.log('policy', typeof (CO.world || {}).set);",
			"partial.js",
			0.0);
		assert(loaded);
		assert(Contains(lines, "[script:partial] gate function undefined undefined undefined undefined"));
		assert(Contains(lines, "[script:partial] policy undefined"));
	}

	{
		Lines lines;
		FakeServer fake;
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		Scripting::ScriptRuntime api{ "cmd", Scripting::Limits{}, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views)(api, kAllPermissions);
		const bool loaded = api.Load(
			"CO.commands.register('echo', (args, playerId) => console.log('echo', args.join('|'), playerId));"
			"try { CO.commands.register('bad name', () => {}); } catch (e) { console.log('rejected', e.message); }",
			"cmd.js",
			0.0);
		assert(loaded);
		assert(Contains(lines, "[script:cmd] rejected command name must be 1-32 letters, digits, '-' or '_'"));
		const bool echoed = commands.Dispatch("echo one  two", 7, false) == Scripting::ScriptCommandResult::Ran;
		assert(echoed);
		assert(Contains(lines, "[script:cmd] echo one|two 7"));
		const bool missed = commands.Dispatch("missing", 7, false) == Scripting::ScriptCommandResult::NoMatch;
		assert(missed);
		api.Stop();
		const bool stale = commands.Dispatch("echo one", 7, false) == Scripting::ScriptCommandResult::NoMatch;
		assert(stale);
	}

	{
		Lines lines;
		FakeServer fake;
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		Scripting::ScriptRuntime api{ "gate", Scripting::Limits{}, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views)(api, kAllPermissions);
		const bool loaded = api.Load(
			"CO.commands.register('secret', (args, playerId) => console.log('ran', playerId), { adminOnly: true });"
			"CO.commands.register('open', (args, playerId) => console.log('open', playerId));"
			"for (const name of ['help', 'ADMIN', 'resources', 'Ensure', 'stop', 'refresh', 'approve']) {"
			"  try { CO.commands.register(name, () => {}); console.log('allowed', name); } catch (e) { console.log('reserved', name, e.message); }"
			"}"
			"try { CO.commands.register('opts', () => {}, 3); } catch (e) { console.log('options', e.message); }"
			"try { CO.commands.register('opts2', () => {}, { adminOnly: 'yes' }); } catch (e) { console.log('flag', e.message); }",
			"gate.js",
			0.0);
		assert(loaded);
		for (const auto name : { "help", "ADMIN", "resources", "Ensure", "stop", "refresh", "approve" }) {
			assert(Contains(lines, std::string{ "[script:gate] reserved " } + name + " command name is reserved"));
		}
		assert(Contains(lines, "[script:gate] options CO.commands.register options must be an object"));
		assert(Contains(lines, "[script:gate] flag CO.commands.register option adminOnly must be a boolean"));
		assert(!AnyStartsWith(lines, "[script:gate] allowed"));

		const bool refused = commands.Dispatch("secret", 7, false) == Scripting::ScriptCommandResult::Refused;
		assert(refused);
		assert(!Contains(lines, "[script:gate] ran 7"));
		const bool ran = commands.Dispatch("secret", 7, true) == Scripting::ScriptCommandResult::Ran;
		assert(ran);
		assert(Contains(lines, "[script:gate] ran 7"));
		const bool open = commands.Dispatch("open", 8, false) == Scripting::ScriptCommandResult::Ran;
		assert(open);
		assert(Contains(lines, "[script:gate] open 8"));
	}

	{
		Lines lines;
		FakeServer fake;
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		Scripting::ScriptRuntime first{ "first", Scripting::Limits{}, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views)(first, kAllPermissions);
		const bool firstLoaded = first.Load("CO.commands.register('ping', () => {});", "first.js", 0.0);
		assert(firstLoaded);
		Scripting::ScriptRuntime second{ "second", Scripting::Limits{}, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views)(second, kAllPermissions);
		const bool secondLoaded = second.Load("try { CO.commands.register('ping', () => {}); } catch (e) { console.log('taken', e.message); }", "second.js", 0.0);
		assert(secondLoaded);
		assert(Contains(lines, "[script:second] taken command is already registered by another resource"));
	}

	{
		Lines lines;
		FakeServer fake;
		double now = 10.0;
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		Scripting::ScriptRuntime api{ "rl", Scripting::Limits{}, Capture(lines) };
		Scripting::MakeScriptApiInstaller(MakeHost(fake, [&now] { return now; }), commands, views)(api, kAllPermissions);
		const bool loaded = api.Load(
			"let sent = 0; for (let i = 0; i < 25; ++i) { if (CO.chat.send(7, 'm' + i)) sent++; } console.log('sent', sent);"
			"CO.commands.register('again', () => { let n = 0; for (let i = 0; i < 25; ++i) { if (CO.chat.send(7, 'n' + i)) n++; } CO.chat.broadcast('dropped'); console.log('again', n); });",
			"rl.js",
			0.0);
		assert(loaded);
		assert(Contains(lines, "[script:rl] sent 10"));
		assert(fake.chats.size() == 10);
		now += 0.5;
		assert(commands.Dispatch("again", 7, false) == Scripting::ScriptCommandResult::Ran);
		assert(Contains(lines, "[script:rl] again 5"));
		assert(fake.chats.size() == 15);
	}

	{
		Lines lines;
		FakeServer fake;
		Scripting::ScriptCommandRegistry commands;
		Scripting::ScriptViewRegistry views;
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		const auto root = std::filesystem::temp_directory_path() / ("co-script-api-tests-" + std::to_string(stamp));
		std::filesystem::remove_all(root);
		WriteFile(root / "cmdres" / "co-resource.json", R"({"name":"cmdres","version":"1.0.0","server":["dist/index.js"],"permissions":["commands.register","chat.send"]})");
		WriteFile(root / "cmdres" / "dist" / "index.js", "CO.commands.register('ping', (args, playerId) => CO.chat.send(playerId, 'pong'));");

		Scripting::ResourceManager manager{ root, Scripting::Limits{}, Capture(lines), Scripting::MakeScriptApiInstaller(MakeHost(fake), commands, views) };
		manager.Tick(0.0);
		assert(StateOf(manager, "cmdres") == "pending");
		const bool beforeApprove = commands.Dispatch("ping", 7, false) == Scripting::ScriptCommandResult::Ran;
		assert(!beforeApprove);
		const auto approveError = manager.Approve("cmdres");
		assert(approveError.empty());
		assert(StateOf(manager, "cmdres") == "running");
		const bool ran = commands.Dispatch("ping", 7, false) == Scripting::ScriptCommandResult::Ran;
		assert(ran);
		assert(fake.chats == (Lines{ "7:pong" }));
		const auto stopError = manager.Stop("cmdres");
		assert(stopError.empty());
		const bool afterStop = commands.Dispatch("ping", 7, false) == Scripting::ScriptCommandResult::Ran;
		assert(!afterStop);

		std::filesystem::remove_all(root);
	}

	return 0;
}
