#include "ServerScriptApi.h"

#include "F4TChatValidation.h"
#include "F4TPacketRateLimit.h"
#include "F4TProfanityFilter.h"

#include "quickjs.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server::Scripting
{
	namespace
	{
		using Json = nlohmann::json;

		std::vector<std::string> SplitWords(std::string_view a_text)
		{
			std::vector<std::string> words;
			while (!a_text.empty()) {
				const auto end = a_text.find(' ');
				if (end != 0) {
					words.emplace_back(a_text.substr(0, end));
				}
				a_text = end == std::string_view::npos ? std::string_view{} : a_text.substr(end + 1);
			}
			return words;
		}

		constexpr std::size_t kMaximumCommandNameBytes{ 32 };
		constexpr std::size_t kMaximumEventSubscriptions{ 64 };
		constexpr double kChatBucketCapacity{ 10.0 };
		constexpr double kChatBucketRefillPerSecond{ 10.0 };
		constexpr std::string_view kReservedCommandNames[]{
			"help", "admin", "resources", "ensure", "stop", "refresh", "approve"
		};

		double SteadySeconds()
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		bool Granted(const std::vector<std::string>& a_permissions, std::string_view a_name)
		{
			return std::find(a_permissions.begin(), a_permissions.end(), a_name) != a_permissions.end();
		}

		bool ValidCommandName(std::string_view a_name)
		{
			return !a_name.empty() && a_name.size() <= kMaximumCommandNameBytes && std::all_of(a_name.begin(), a_name.end(), [](char a_byte) {
				return std::isalnum(static_cast<unsigned char>(a_byte)) || a_byte == '-' || a_byte == '_';
			});
		}

		std::string LowerAscii(std::string_view a_text)
		{
			std::string lowered{ a_text };
			std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](char a_byte) {
				return static_cast<char>(std::tolower(static_cast<unsigned char>(a_byte)));
			});
			return lowered;
		}

		bool IsReservedCommandName(std::string_view a_name)
		{
			const auto lowered = LowerAscii(a_name);
			for (const auto reserved : kReservedCommandNames) {
				if (reserved == lowered) {
					return true;
				}
			}
			return false;
		}

		bool ReadAdminOnlyOption(JSContext* a_ctx, int a_argc, JSValueConst* a_argv, bool& a_adminOnly, bool& a_error)
		{
			a_error = false;
			if (a_argc < 3 || JS_IsUndefined(a_argv[2]) || JS_IsNull(a_argv[2])) {
				return true;
			}
			if (!JS_IsObject(a_argv[2])) {
				JS_ThrowTypeError(a_ctx, "CO.commands.register options must be an object");
				a_error = true;
				return false;
			}
			JSValue option = JS_GetPropertyStr(a_ctx, a_argv[2], "adminOnly");
			if (JS_IsException(option)) {
				a_error = true;
				return false;
			}
			const bool valid = JS_IsUndefined(option) || JS_IsBool(option);
			if (valid && JS_IsBool(option)) {
				a_adminOnly = JS_ToBool(a_ctx, option) > 0;
			}
			JS_FreeValue(a_ctx, option);
			if (!valid) {
				JS_ThrowTypeError(a_ctx, "CO.commands.register option adminOnly must be a boolean");
				a_error = true;
				return false;
			}
			return true;
		}

		std::optional<std::string> ChatText(std::string_view a_text)
		{
			const auto normalized = F4T::ChatValidation::NormalizeChatSend(Json{ { "channel", "global" }, { "text", std::string{ a_text } } });
			if (normalized.error) {
				return std::nullopt;
			}
			return F4T::ProfanityFilter::MaskBlockedWords(normalized.text, F4T::ProfanityFilter::Scope::Slurs);
		}

		JSValue ToValue(JSContext* a_ctx, const Json& a_json)
		{
			const auto text = a_json.dump(-1, ' ', false, Json::error_handler_t::replace);
			return JS_ParseJSON(a_ctx, text.data(), text.size(), "<co>");
		}

		std::optional<std::string> ReadText(JSContext* a_ctx, JSValueConst a_value)
		{
			std::size_t length{};
			const char* text = JS_ToCStringLen(a_ctx, &length, a_value);
			if (!text) {
				return std::nullopt;
			}
			std::string result{ text, length };
			JS_FreeCString(a_ctx, text);
			return result;
		}

		bool ReadPlayerId(JSContext* a_ctx, JSValueConst a_value, std::uint32_t& a_playerId)
		{
			double value{};
			if (JS_ToFloat64(a_ctx, &value, a_value) < 0) {
				return false;
			}
			a_playerId = value >= 1.0 && value <= 4294967295.0 && value == std::floor(value) ? static_cast<std::uint32_t>(value) : 0U;
			return true;
		}

		const std::string kViewHandlerKey{ "@view" };

		struct ApiState : std::enable_shared_from_this<ApiState>
		{
			JSContext* ctx{};
			ScriptRuntime* runtime{};
			ScriptHost host;
			ScriptCommandRegistry* commands{};
			ScriptViewRegistry* views{};
			std::map<std::string, JSValue, std::less<>> handlers;
			std::map<std::uint64_t, JSValue> events;
			F4T::PacketRateLimit::TokenBucket chatBucket{ kChatBucketCapacity, 0.0 };

			[[nodiscard]] bool TakeChatToken()
			{
				const auto now = host.nowMonotonicSeconds ? host.nowMonotonicSeconds() : SteadySeconds();
				return F4T::PacketRateLimit::TakeToken(chatBucket, now, kChatBucketRefillPerSecond, kChatBucketCapacity);
			}

			void Release()
			{
				commands->RemoveOwner(runtime);
				views->RemoveOwner(runtime);
				for (const auto& item : events) {
					host.unsubscribeEvent(item.first);
					JS_FreeValue(ctx, item.second);
				}
				events.clear();
				for (const auto& item : handlers) {
					JS_FreeValue(ctx, item.second);
				}
				handlers.clear();
				JS_SetContextOpaque(ctx, nullptr);
			}

			bool RemoveEvent(std::uint64_t a_token)
			{
				const auto found = events.find(a_token);
				if (found == events.end()) {
					return false;
				}
				host.unsubscribeEvent(a_token);
				JS_FreeValue(ctx, found->second);
				events.erase(found);
				return true;
			}

			bool RunEvent(std::uint64_t a_token, const Json& a_event)
			{
				const auto found = events.find(a_token);
				if (found == events.end()) {
					return true;
				}
				JSValue handler = JS_DupValue(ctx, found->second);
				JSValue argument = ToValue(ctx, a_event);
				JSValue result = JS_UNDEFINED;
				bool allow = true;
				if (runtime->Call(handler, 1, &argument, &result) && JS_IsBool(result)) {
					allow = JS_ToBool(ctx, result) != 0;
				}
				JS_FreeValue(ctx, result);
				JS_FreeValue(ctx, argument);
				JS_FreeValue(ctx, handler);
				return allow;
			}

			void RunViewMessage(std::uint32_t a_playerId, const Json& a_data)
			{
				const auto found = handlers.find(kViewHandlerKey);
				if (found == handlers.end() || !JS_IsFunction(ctx, found->second)) {
					return;
				}
				JSValue handler = JS_DupValue(ctx, found->second);
				JSValue argument = ToValue(ctx, Json{ { "playerId", a_playerId }, { "data", a_data } });
				runtime->Call(handler, 1, &argument);
				JS_FreeValue(ctx, argument);
				JS_FreeValue(ctx, handler);
			}

			void Invoke(std::string_view a_name, const std::vector<std::string>& a_args, std::uint32_t a_playerId)
			{
				const auto found = handlers.find(a_name);
				if (found == handlers.end()) {
					return;
				}
				JSContext* context = ctx;
				JSValue handler = JS_DupValue(context, found->second);
				JSValue argv[2] = { JS_NewArray(context), JS_NewUint32(context, a_playerId) };
				for (std::uint32_t index = 0; index < a_args.size(); ++index) {
					JS_SetPropertyUint32(context, argv[0], index, JS_NewStringLen(context, a_args[index].data(), a_args[index].size()));
				}
				runtime->Call(handler, 2, argv);
				JS_FreeValue(context, argv[0]);
				JS_FreeValue(context, handler);
			}
		};

		ApiState& State(JSContext* a_ctx)
		{
			return *static_cast<ApiState*>(JS_GetContextOpaque(a_ctx));
		}

		JSValue ListPlayers(JSContext* a_ctx, JSValueConst, int, JSValueConst*)
		{
			return ToValue(a_ctx, State(a_ctx).host.listPlayers());
		}

		JSValue GetPlayer(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			std::uint32_t playerId{};
			if (a_argc < 1) {
				return JS_NULL;
			}
			if (!ReadPlayerId(a_ctx, a_argv[0], playerId)) {
				return JS_EXCEPTION;
			}
			return ToValue(a_ctx, State(a_ctx).host.getPlayer(playerId));
		}

		JSValue SendChat(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 2) {
				return JS_ThrowTypeError(a_ctx, "CO.chat.send(playerId, text) needs a player id and text");
			}
			std::uint32_t playerId{};
			if (!ReadPlayerId(a_ctx, a_argv[0], playerId)) {
				return JS_EXCEPTION;
			}
			const auto text = ReadText(a_ctx, a_argv[1]);
			if (!text) {
				return JS_EXCEPTION;
			}
			const auto clean = ChatText(*text);
			ApiState& state = State(a_ctx);
			if (!clean || !state.TakeChatToken()) {
				return JS_NewBool(a_ctx, false);
			}
			return JS_NewBool(a_ctx, state.host.sendChat(playerId, *clean));
		}

		JSValue BroadcastChat(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 1) {
				return JS_ThrowTypeError(a_ctx, "CO.chat.broadcast(text) needs text");
			}
			const auto text = ReadText(a_ctx, a_argv[0]);
			if (!text) {
				return JS_EXCEPTION;
			}
			if (const auto clean = ChatText(*text)) {
				ApiState& state = State(a_ctx);
				if (state.TakeChatToken()) {
					state.host.broadcastChat(*clean);
				}
			}
			return JS_UNDEFINED;
		}

		JSValue RegisterCommand(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 2 || !JS_IsFunction(a_ctx, a_argv[1])) {
				return JS_ThrowTypeError(a_ctx, "CO.commands.register(name, handler) needs a name and a function");
			}
			const auto name = ReadText(a_ctx, a_argv[0]);
			if (!name) {
				return JS_EXCEPTION;
			}
			if (!ValidCommandName(*name)) {
				return JS_ThrowTypeError(a_ctx, "command name must be 1-32 letters, digits, '-' or '_'");
			}
			if (IsReservedCommandName(*name)) {
				return JS_ThrowTypeError(a_ctx, "command name is reserved");
			}
			bool adminOnly{};
			bool optionError{};
			if (!ReadAdminOnlyOption(a_ctx, a_argc, a_argv, adminOnly, optionError)) {
				return JS_EXCEPTION;
			}
			ApiState& state = State(a_ctx);
			if (!state.commands->Add(*name, state.runtime, [self = state.shared_from_this(), key = *name](const std::vector<std::string>& a_args, std::uint32_t a_playerId) {
					self->Invoke(key, a_args, a_playerId);
				}, adminOnly)) {
				return JS_ThrowTypeError(a_ctx, "command is already registered by another resource");
			}
			auto found = state.handlers.find(*name);
			if (found == state.handlers.end()) {
				found = state.handlers.emplace(*name, JS_UNDEFINED).first;
			}
			JS_FreeValue(a_ctx, found->second);
			found->second = JS_DupValue(a_ctx, a_argv[1]);
			return JS_UNDEFINED;
		}

		JSValue ReadWorld(JSContext* a_ctx, JSValueConst, int, JSValueConst*)
		{
			return ToValue(a_ctx, State(a_ctx).host.readWorld());
		}

		std::optional<Json> FromValue(JSContext* a_ctx, JSValueConst a_value)
		{
			JSValue text = JS_JSONStringify(a_ctx, a_value, JS_UNDEFINED, JS_UNDEFINED);
			if (JS_IsException(text)) {
				return std::nullopt;
			}
			if (!JS_IsString(text)) {
				JS_FreeValue(a_ctx, text);
				return Json{};
			}
			std::size_t length{};
			const char* raw = JS_ToCStringLen(a_ctx, &length, text);
			JS_FreeValue(a_ctx, text);
			if (!raw) {
				return std::nullopt;
			}
			auto parsed = Json::parse(std::string_view{ raw, length }, nullptr, false);
			JS_FreeCString(a_ctx, raw);
			if (parsed.is_discarded()) {
				return std::nullopt;
			}
			return parsed;
		}

		bool ReadId64(JSContext* a_ctx, JSValueConst a_value, std::uint64_t& a_id)
		{
			double value{};
			if (JS_ToFloat64(a_ctx, &value, a_value) < 0) {
				return false;
			}
			a_id = value >= 0.0 && value <= 9007199254740991.0 && value == std::floor(value) ? static_cast<std::uint64_t>(value) : 0U;
			return true;
		}

		JSValue Result(JSContext* a_ctx, std::string_view a_failure)
		{
			JSValue object = JS_NewObject(a_ctx);
			JS_SetPropertyStr(a_ctx, object, "ok", JS_NewBool(a_ctx, a_failure.empty()));
			if (!a_failure.empty()) {
				JS_SetPropertyStr(a_ctx, object, "error", JS_NewStringLen(a_ctx, a_failure.data(), a_failure.size()));
			}
			return object;
		}

		JSValue SubscribeEvent(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 2 || !JS_IsFunction(a_ctx, a_argv[1])) {
				return JS_ThrowTypeError(a_ctx, "CO.events.on(name, handler) needs a name and a function");
			}
			const auto name = ReadText(a_ctx, a_argv[0]);
			if (!name) {
				return JS_EXCEPTION;
			}
			const auto kind = ParseEventName(*name);
			if (!kind) {
				return JS_ThrowTypeError(a_ctx, "unknown event name");
			}
			if (*kind == ServerEventKind::StateBagChange) {
				return JS_ThrowTypeError(a_ctx, "use CO.state.onChange for state changes");
			}
			ApiState& state = State(a_ctx);
			if (state.events.size() >= kMaximumEventSubscriptions) {
				return JS_ThrowRangeError(a_ctx, "too many CO.events.on handlers");
			}
			auto slot = std::make_shared<std::uint64_t>(0);
			*slot = state.host.subscribeEvent(*kind, [self = state.shared_from_this(), slot](const Json& a_event) {
				return self->RunEvent(*slot, a_event);
			});
			if (*slot == 0) {
				return JS_NewInt64(a_ctx, 0);
			}
			state.events.emplace(*slot, JS_DupValue(a_ctx, a_argv[1]));
			return JS_NewInt64(a_ctx, static_cast<std::int64_t>(*slot));
		}

		JSValue UnsubscribeEvent(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 1) {
				return JS_ThrowTypeError(a_ctx, "CO.events.off(token) needs a token");
			}
			std::int64_t token{};
			if (JS_ToInt64(a_ctx, &token, a_argv[0]) < 0) {
				return JS_EXCEPTION;
			}
			return JS_NewBool(a_ctx, State(a_ctx).RemoveEvent(token > 0 ? static_cast<std::uint64_t>(token) : 0U));
		}

		std::string StatePrefix(const ApiState& a_state)
		{
			return a_state.runtime->GetName() + ".";
		}

		JSValue OnStateChange(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 1 || !JS_IsFunction(a_ctx, a_argv[0])) {
				return JS_ThrowTypeError(a_ctx, "CO.state.onChange(handler) needs a function");
			}
			ApiState& state = State(a_ctx);
			if (state.events.size() >= kMaximumEventSubscriptions) {
				return JS_ThrowRangeError(a_ctx, "too many CO.events.on handlers");
			}
			auto slot = std::make_shared<std::uint64_t>(0);
			*slot = state.host.subscribeEvent(ServerEventKind::StateBagChange, [self = state.shared_from_this(), slot, prefix = StatePrefix(state)](const Json& a_event) {
				const auto key = a_event.value("data", Json::object()).value("key", std::string{});
				if (!key.starts_with(prefix)) {
					return true;
				}
				Json change = a_event;
				change["data"]["key"] = key.substr(prefix.size());
				return self->RunEvent(*slot, change);
			});
			if (*slot == 0) {
				return JS_NewInt64(a_ctx, 0);
			}
			state.events.emplace(*slot, JS_DupValue(a_ctx, a_argv[0]));
			return JS_NewInt64(a_ctx, static_cast<std::int64_t>(*slot));
		}

		JSValue ReadState(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 1) {
				return JS_ThrowTypeError(a_ctx, "CO.state.get(bag) needs a bag name");
			}
			const auto bag = ReadText(a_ctx, a_argv[0]);
			if (!bag) {
				return JS_EXCEPTION;
			}
			const ApiState& state = State(a_ctx);
			const auto prefix = StatePrefix(state);
			const auto all = state.host.readState(*bag);
			Json owned = Json::object();
			if (all.is_object()) {
				for (auto item = all.begin(); item != all.end(); ++item) {
					if (item.key().starts_with(prefix)) {
						owned[item.key().substr(prefix.size())] = item.value();
					}
				}
			}
			return ToValue(a_ctx, owned);
		}

		JSValue WriteState(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 3) {
				return JS_ThrowTypeError(a_ctx, "CO.state.set(bag, key, value) needs three arguments");
			}
			const auto bag = ReadText(a_ctx, a_argv[0]);
			const auto key = ReadText(a_ctx, a_argv[1]);
			if (!bag || !key) {
				return JS_EXCEPTION;
			}
			const auto value = FromValue(a_ctx, a_argv[2]);
			if (!value) {
				return JS_ThrowTypeError(a_ctx, "the value is not JSON");
			}
			ApiState& state = State(a_ctx);
			const auto owned = StatePrefix(state) + *key;
			std::string failure;
			state.host.setState(*bag, owned, *value, failure);
			return Result(a_ctx, failure);
		}

		JSValue DeleteState(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 2) {
				return JS_ThrowTypeError(a_ctx, "CO.state.delete(bag, key) needs a bag name and a key");
			}
			const auto bag = ReadText(a_ctx, a_argv[0]);
			const auto key = ReadText(a_ctx, a_argv[1]);
			if (!bag || !key) {
				return JS_EXCEPTION;
			}
			ApiState& state = State(a_ctx);
			const auto owned = StatePrefix(state) + *key;
			std::string failure;
			state.host.setState(*bag, owned, Json{}, failure);
			return Result(a_ctx, failure);
		}

		JSValue ListFactions(JSContext* a_ctx, JSValueConst, int, JSValueConst*)
		{
			return ToValue(a_ctx, State(a_ctx).host.listFactions());
		}

		JSValue DisbandFaction(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 1) {
				return JS_ThrowTypeError(a_ctx, "CO.factions.disband(factionId) needs a faction id");
			}
			const auto faction = ReadText(a_ctx, a_argv[0]);
			if (!faction) {
				return JS_EXCEPTION;
			}
			std::string failure;
			State(a_ctx).host.disbandFaction(*faction, failure);
			return Result(a_ctx, failure);
		}

		JSValue PromoteFaction(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 2) {
				return JS_ThrowTypeError(a_ctx, "CO.factions.promote(factionId, profileId) needs two ids");
			}
			const auto faction = ReadText(a_ctx, a_argv[0]);
			const auto profile = ReadText(a_ctx, a_argv[1]);
			if (!faction || !profile) {
				return JS_EXCEPTION;
			}
			std::string failure;
			State(a_ctx).host.promoteFaction(*faction, *profile, failure);
			return Result(a_ctx, failure);
		}

		JSValue KickFaction(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 2) {
				return JS_ThrowTypeError(a_ctx, "CO.factions.kick(factionId, profileId) needs two ids");
			}
			const auto faction = ReadText(a_ctx, a_argv[0]);
			const auto profile = ReadText(a_ctx, a_argv[1]);
			if (!faction || !profile) {
				return JS_EXCEPTION;
			}
			std::string failure;
			State(a_ctx).host.kickFaction(*faction, *profile, failure);
			return Result(a_ctx, failure);
		}

		JSValue QuestStatus(JSContext* a_ctx, JSValueConst, int, JSValueConst*)
		{
			return ToValue(a_ctx, State(a_ctx).host.questStatus());
		}

		JSValue SetQuestMode(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 1) {
				return JS_ThrowTypeError(a_ctx, "CO.quests.mode(mode) needs a mode name");
			}
			const auto mode = ReadText(a_ctx, a_argv[0]);
			if (!mode) {
				return JS_EXCEPTION;
			}
			return Result(a_ctx, State(a_ctx).host.setQuestMode(*mode) ? std::string_view{} : "unknown quest mode");
		}

		JSValue SetQuestGate(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 3) {
				return JS_ThrowTypeError(a_ctx, "CO.quests.gate(quest, stage, release) needs three arguments");
			}
			const auto quest = ReadText(a_ctx, a_argv[0]);
			if (!quest) {
				return JS_EXCEPTION;
			}
			double stage{};
			if (JS_ToFloat64(a_ctx, &stage, a_argv[1]) < 0) {
				return JS_EXCEPTION;
			}
			if (stage < 0.0 || stage > 65535.0 || stage != std::floor(stage)) {
				return Result(a_ctx, "stage must be a whole number from 0 to 65535");
			}
			const bool release = JS_ToBool(a_ctx, a_argv[2]) != 0;
			return Result(a_ctx, State(a_ctx).host.setQuestGate(*quest, static_cast<std::uint16_t>(stage), release) ? std::string_view{} : "quest must be MOD:000000");
		}

		JSValue SetQuestSolo(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 3) {
				return JS_ThrowTypeError(a_ctx, "CO.quests.solo(profileId, quest, solo) needs three arguments");
			}
			const auto profile = ReadText(a_ctx, a_argv[0]);
			const auto quest = ReadText(a_ctx, a_argv[1]);
			if (!profile || !quest) {
				return JS_EXCEPTION;
			}
			const bool solo = JS_ToBool(a_ctx, a_argv[2]) != 0;
			return Result(a_ctx, State(a_ctx).host.setQuestSolo(*profile, *quest, solo) ? std::string_view{} : "quest must be MOD:000000");
		}

		JSValue SetSettlementOwner(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 2) {
				return JS_ThrowTypeError(a_ctx, "CO.settlements.setOwner(zoneId, profileId) needs two arguments");
			}
			std::uint64_t zone{};
			if (!ReadId64(a_ctx, a_argv[0], zone)) {
				return JS_EXCEPTION;
			}
			const auto profile = ReadText(a_ctx, a_argv[1]);
			if (!profile) {
				return JS_EXCEPTION;
			}
			if (zone == 0) {
				return Result(a_ctx, "zoneId must be a whole number from 1 upward");
			}
			std::string failure;
			State(a_ctx).host.setSettlementOwner(zone, *profile, failure);
			return Result(a_ctx, failure);
		}

		JSValue ReadPolicy(JSContext* a_ctx, JSValueConst, int, JSValueConst*)
		{
			return ToValue(a_ctx, State(a_ctx).host.readPolicy());
		}

		JSValue RevivePlayer(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 1) {
				return JS_ThrowTypeError(a_ctx, "CO.players.revive(playerId) needs a player id");
			}
			std::uint32_t playerId{};
			if (!ReadPlayerId(a_ctx, a_argv[0], playerId)) {
				return JS_EXCEPTION;
			}
			if (playerId == 0) {
				return Result(a_ctx, "playerId must be a whole number from 1 upward");
			}
			std::string failure;
			return Result(a_ctx, State(a_ctx).host.revivePlayer(playerId, failure) ? std::string_view{} : std::string_view{ failure });
		}

		JSValue KillPlayer(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 1) {
				return JS_ThrowTypeError(a_ctx, "CO.players.kill(playerId) needs a player id");
			}
			std::uint32_t playerId{};
			if (!ReadPlayerId(a_ctx, a_argv[0], playerId)) {
				return JS_EXCEPTION;
			}
			if (playerId == 0) {
				return Result(a_ctx, "playerId must be a whole number from 1 upward");
			}
			std::string failure;
			return Result(a_ctx, State(a_ctx).host.killPlayer(playerId, failure) ? std::string_view{} : std::string_view{ failure });
		}

		JSValue MessagePlayer(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 2) {
				return JS_ThrowTypeError(a_ctx, "CO.players.message(playerId, text) needs a player id and text");
			}
			std::uint32_t playerId{};
			if (!ReadPlayerId(a_ctx, a_argv[0], playerId)) {
				return JS_EXCEPTION;
			}
			const auto text = ReadText(a_ctx, a_argv[1]);
			if (!text) {
				return JS_EXCEPTION;
			}
			const auto clean = ChatText(*text);
			if (playerId == 0) {
				return Result(a_ctx, "playerId must be a whole number from 1 upward");
			}
			if (!clean) {
				return Result(a_ctx, "the text is empty or not valid chat text");
			}
			return Result(a_ctx, State(a_ctx).host.messagePlayer(playerId, *clean) ? std::string_view{} : "player is not connected");
		}

		JSValue SettlementOwner(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 1) {
				return JS_ThrowTypeError(a_ctx, "CO.settlements.get(zoneId) needs a zone id");
			}
			std::uint64_t zone{};
			if (!ReadId64(a_ctx, a_argv[0], zone)) {
				return JS_EXCEPTION;
			}
			if (zone == 0) {
				return JS_ThrowTypeError(a_ctx, "zoneId must be a whole number from 1 upward");
			}
			return ToValue(a_ctx, State(a_ctx).host.settlementOwner(zone));
		}

		JSValue SetWorldPolicy(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 1) {
				return JS_ThrowTypeError(a_ctx, "CO.world.set(policy) needs a policy object");
			}
			const auto policy = FromValue(a_ctx, a_argv[0]);
			if (!policy) {
				return JS_ThrowTypeError(a_ctx, "the policy is not JSON");
			}
			std::string failure;
			State(a_ctx).host.setWorldPolicy(*policy, failure);
			return Result(a_ctx, failure);
		}

		JSValue OnViewMessage(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 1 || !JS_IsFunction(a_ctx, a_argv[0])) {
				return JS_ThrowTypeError(a_ctx, "CO.views.onMessage(handler) needs a function");
			}
			ApiState& state = State(a_ctx);
			auto found = state.handlers.find(kViewHandlerKey);
			if (found == state.handlers.end()) {
				found = state.handlers.emplace(kViewHandlerKey, JS_UNDEFINED).first;
			}
			JS_FreeValue(a_ctx, found->second);
			found->second = JS_DupValue(a_ctx, a_argv[0]);
			return JS_UNDEFINED;
		}

		JSValue OpenView(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 2) {
				return JS_ThrowTypeError(a_ctx, "CO.views.open(playerId, view) needs a player id and a view path");
			}
			std::uint64_t player{};
			if (!ReadId64(a_ctx, a_argv[0], player)) {
				return JS_EXCEPTION;
			}
			if (player == 0 || player > 0xFFFFFFFFULL) {
				return Result(a_ctx, "playerId must be a whole number from 1 upward");
			}
			const auto view = ReadText(a_ctx, a_argv[1]);
			if (!view) {
				return JS_EXCEPTION;
			}
			ApiState& state = State(a_ctx);
			std::string failure;
			state.host.openView(state.runtime->GetName(), static_cast<std::uint32_t>(player), *view, failure);
			return Result(a_ctx, failure);
		}

		JSValue CloseView(JSContext* a_ctx, JSValueConst, int a_argc, JSValueConst* a_argv)
		{
			if (a_argc < 1) {
				return JS_ThrowTypeError(a_ctx, "CO.views.close(playerId) needs a player id");
			}
			std::uint64_t player{};
			if (!ReadId64(a_ctx, a_argv[0], player)) {
				return JS_EXCEPTION;
			}
			if (player == 0 || player > 0xFFFFFFFFULL) {
				return Result(a_ctx, "playerId must be a whole number from 1 upward");
			}
			ApiState& state = State(a_ctx);
			std::string failure;
			state.host.closeView(state.runtime->GetName(), static_cast<std::uint32_t>(player), failure);
			return Result(a_ctx, failure);
		}

		void InstallApi(ApiState& a_state, const std::vector<std::string>& a_permissions)
		{
			JSContext* ctx = a_state.ctx;
			JSValue global = JS_GetGlobalObject(ctx);
			JSValue co = JS_NewObject(ctx);
			if (Granted(a_permissions, "players.read") || Granted(a_permissions, "players.write")) {
				JSValue players = JS_NewObject(ctx);
				if (Granted(a_permissions, "players.read")) {
					JS_SetPropertyStr(ctx, players, "list", JS_NewCFunction(ctx, &ListPlayers, "list", 0));
					JS_SetPropertyStr(ctx, players, "get", JS_NewCFunction(ctx, &GetPlayer, "get", 1));
				}
				if (Granted(a_permissions, "players.write")) {
					JS_SetPropertyStr(ctx, players, "revive", JS_NewCFunction(ctx, &RevivePlayer, "revive", 1));
					JS_SetPropertyStr(ctx, players, "kill", JS_NewCFunction(ctx, &KillPlayer, "kill", 1));
					JS_SetPropertyStr(ctx, players, "message", JS_NewCFunction(ctx, &MessagePlayer, "message", 2));
				}
				JS_SetPropertyStr(ctx, co, "players", players);
			}
			if (Granted(a_permissions, "chat.send")) {
				JSValue chat = JS_NewObject(ctx);
				JS_SetPropertyStr(ctx, chat, "send", JS_NewCFunction(ctx, &SendChat, "send", 2));
				JS_SetPropertyStr(ctx, chat, "broadcast", JS_NewCFunction(ctx, &BroadcastChat, "broadcast", 1));
				JS_SetPropertyStr(ctx, co, "chat", chat);
			}
			if (Granted(a_permissions, "commands.register")) {
				JSValue commands = JS_NewObject(ctx);
				JS_SetPropertyStr(ctx, commands, "register", JS_NewCFunction(ctx, &RegisterCommand, "register", 2));
				JS_SetPropertyStr(ctx, co, "commands", commands);
			}
			if (Granted(a_permissions, "world.read") || Granted(a_permissions, "world.set")) {
				JSValue world = JS_NewObject(ctx);
				if (Granted(a_permissions, "world.read")) {
					JS_SetPropertyStr(ctx, world, "get", JS_NewCFunction(ctx, &ReadWorld, "get", 0));
				}
				if (Granted(a_permissions, "world.read")) {
					JS_SetPropertyStr(ctx, world, "policy", JS_NewCFunction(ctx, &ReadPolicy, "policy", 0));
				}
				if (Granted(a_permissions, "world.set")) {
					JS_SetPropertyStr(ctx, world, "set", JS_NewCFunction(ctx, &SetWorldPolicy, "set", 1));
				}
				JS_SetPropertyStr(ctx, co, "world", world);
			}
			if (Granted(a_permissions, "events.subscribe")) {
				JSValue events = JS_NewObject(ctx);
				JS_SetPropertyStr(ctx, events, "on", JS_NewCFunction(ctx, &SubscribeEvent, "on", 2));
				JS_SetPropertyStr(ctx, events, "off", JS_NewCFunction(ctx, &UnsubscribeEvent, "off", 1));
				JS_SetPropertyStr(ctx, co, "events", events);
			}
			if (Granted(a_permissions, "state.read") || Granted(a_permissions, "state.write")) {
				JSValue state = JS_NewObject(ctx);
				if (Granted(a_permissions, "state.read")) {
					JS_SetPropertyStr(ctx, state, "get", JS_NewCFunction(ctx, &ReadState, "get", 1));
					JS_SetPropertyStr(ctx, state, "onChange", JS_NewCFunction(ctx, &OnStateChange, "onChange", 1));
				}
				if (Granted(a_permissions, "state.write")) {
					JS_SetPropertyStr(ctx, state, "set", JS_NewCFunction(ctx, &WriteState, "set", 3));
					JS_SetPropertyStr(ctx, state, "delete", JS_NewCFunction(ctx, &DeleteState, "delete", 2));
				}
				JS_SetPropertyStr(ctx, co, "state", state);
			}
			if (Granted(a_permissions, "factions.read") || Granted(a_permissions, "factions.write")) {
				JSValue factions = JS_NewObject(ctx);
				if (Granted(a_permissions, "factions.read")) {
					JS_SetPropertyStr(ctx, factions, "list", JS_NewCFunction(ctx, &ListFactions, "list", 0));
				}
				if (Granted(a_permissions, "factions.write")) {
					JS_SetPropertyStr(ctx, factions, "disband", JS_NewCFunction(ctx, &DisbandFaction, "disband", 1));
					JS_SetPropertyStr(ctx, factions, "promote", JS_NewCFunction(ctx, &PromoteFaction, "promote", 2));
					JS_SetPropertyStr(ctx, factions, "kick", JS_NewCFunction(ctx, &KickFaction, "kick", 2));
				}
				JS_SetPropertyStr(ctx, co, "factions", factions);
			}
			if (Granted(a_permissions, "quests.read") || Granted(a_permissions, "quests.write")) {
				JSValue quests = JS_NewObject(ctx);
				if (Granted(a_permissions, "quests.read")) {
					JS_SetPropertyStr(ctx, quests, "status", JS_NewCFunction(ctx, &QuestStatus, "status", 0));
				}
				if (Granted(a_permissions, "quests.write")) {
					JS_SetPropertyStr(ctx, quests, "mode", JS_NewCFunction(ctx, &SetQuestMode, "mode", 1));
					JS_SetPropertyStr(ctx, quests, "gate", JS_NewCFunction(ctx, &SetQuestGate, "gate", 3));
					JS_SetPropertyStr(ctx, quests, "solo", JS_NewCFunction(ctx, &SetQuestSolo, "solo", 3));
				}
				JS_SetPropertyStr(ctx, co, "quests", quests);
			}
			if (Granted(a_permissions, "settlements.read") || Granted(a_permissions, "settlements.write")) {
				JSValue settlements = JS_NewObject(ctx);
				if (Granted(a_permissions, "settlements.read")) {
					JS_SetPropertyStr(ctx, settlements, "get", JS_NewCFunction(ctx, &SettlementOwner, "get", 1));
				}
				if (Granted(a_permissions, "settlements.write")) {
					JS_SetPropertyStr(ctx, settlements, "setOwner", JS_NewCFunction(ctx, &SetSettlementOwner, "setOwner", 2));
				}
				JS_SetPropertyStr(ctx, co, "settlements", settlements);
			}
			if (Granted(a_permissions, "views.message") || Granted(a_permissions, "views.open")) {
				JSValue views = JS_NewObject(ctx);
				if (Granted(a_permissions, "views.message")) {
					JS_SetPropertyStr(ctx, views, "onMessage", JS_NewCFunction(ctx, &OnViewMessage, "onMessage", 1));
				}
				if (Granted(a_permissions, "views.open")) {
					JS_SetPropertyStr(ctx, views, "open", JS_NewCFunction(ctx, &OpenView, "open", 2));
					JS_SetPropertyStr(ctx, views, "close", JS_NewCFunction(ctx, &CloseView, "close", 1));
				}
				JS_SetPropertyStr(ctx, co, "views", views);
			}
			JS_SetPropertyStr(ctx, global, "CO", co);
			JS_FreeValue(ctx, global);
		}
	}

	bool ScriptCommandRegistry::Add(std::string_view a_name, const void* a_owner, ScriptCommandRun a_run, bool a_adminOnly)
	{
		const auto found = _commands.find(a_name);
		if (found != _commands.end() && found->second.owner != a_owner) {
			return false;
		}
		_commands.insert_or_assign(std::string{ a_name }, Entry{ a_owner, std::move(a_run), a_adminOnly });
		return true;
	}

	void ScriptCommandRegistry::RemoveOwner(const void* a_owner)
	{
		std::erase_if(_commands, [a_owner](const auto& a_item) { return a_item.second.owner == a_owner; });
	}

	ScriptCommandResult ScriptCommandRegistry::Dispatch(std::string_view a_line, std::uint32_t a_playerId, bool a_isAdmin)
	{
		const auto split = a_line.find(' ');
		const auto found = _commands.find(a_line.substr(0, split));
		if (found == _commands.end()) {
			return ScriptCommandResult::NoMatch;
		}
		if (found->second.adminOnly && !a_isAdmin) {
			return ScriptCommandResult::Refused;
		}
		const auto run = found->second.run;
		run(SplitWords(split == std::string_view::npos ? std::string_view{} : a_line.substr(split + 1)), a_playerId);
		return ScriptCommandResult::Ran;
	}

	ScriptInstaller MakeScriptApiInstaller(ScriptHost a_host, ScriptCommandRegistry& a_commands, ScriptViewRegistry& a_views)
	{
		return [host = std::move(a_host), &a_commands, &a_views](ScriptRuntime& a_runtime, const std::vector<std::string>& a_permissions) {
			auto state = std::make_shared<ApiState>();
			state->ctx = a_runtime.Context();
			state->runtime = &a_runtime;
			state->host = host;
			state->commands = &a_commands;
			state->views = &a_views;
			JS_SetContextOpaque(state->ctx, state.get());
			a_runtime.OnStop([state] { state->Release(); });
			(void)a_views.Add(a_runtime.GetName(), &a_runtime, [state](std::uint32_t a_playerId, const Json& a_data) {
				state->RunViewMessage(a_playerId, a_data);
			});
			InstallApi(*state, a_permissions);
		};
	}
}
