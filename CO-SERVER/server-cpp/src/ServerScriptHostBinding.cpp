#include "ServerScriptHostBinding.h"

#include "F4TProfanityFilter.h"
#include "F4TQuestProtocol.h"
#include "F4TStateBagProtocol.h"
#include "F4TWorldPolicy.h"
#include "ServerEvents.h"
#include "ServerOutboundFrames.h"

#include "F4TWorldPolicy.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace CommonwealthOnline::Server::Scripting
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr std::uint32_t kScriptSenderPlayerId{ 0xFFFFFFFFU };

		double UnixNow()
		{
			return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
		}

		Json EventJson(const ServerEvent& a_event)
		{
			Json data = Json::object();
			if (a_event.packet != nullptr) data = *a_event.packet;
			return Json{
				{ "name", std::string{ EventName(a_event.kind) } },
				{ "playerId", a_event.playerId },
				{ "profileId", a_event.profileId },
				{ "data", std::move(data) }
			};
		}
	}

	ScriptHostBinding::ScriptHostBinding(ProtocolHost& a_protocol, const Json& a_world, ScriptFrameSender a_send)
		: _protocol{ a_protocol }, _world{ a_world }, _send{ std::move(a_send) }
	{
		_host.listPlayers = [this]() -> Json { return _protocol.ScriptPlayers(); };
		_host.getPlayer = [this](std::uint32_t a_playerId) -> Json {
			for (const auto& player : _protocol.ScriptPlayers()) {
				if (player.value("id", std::uint32_t{}) == a_playerId) return player;
			}
			return Json{};
		};
		_host.sendChat = [this](std::uint32_t a_playerId, std::string_view a_text) {
			const auto connectionId = ConnectionFor(a_playerId);
			if (!connectionId) return false;
			Emit({ ReliableFrame(*connectionId, ChatMessage(*connectionId, a_text)) });
			return true;
		};
		_host.broadcastChat = [this](std::string_view a_text) {
			std::vector<OutboundFrame> frames;
			const auto messageId = _protocol.AllocateChatMessageId();
			for (const auto& player : _protocol.ScriptPlayers()) {
				const auto connectionId = player.value("connectionId", std::uint32_t{});
				frames.push_back(ReliableFrame(connectionId, ChatMessage(connectionId, a_text, messageId)));
			}
			Emit(std::move(frames));
		};
		_host.readWorld = [this]() -> Json {
			Json world = _world.is_object() ? _world : Json::object();
			world["mode"] = std::string{ F4T::WorldPolicy::ModeName(_protocol.CurrentWorldPolicy().mode) };
			return world;
		};
		_host.nowMonotonicSeconds = []() -> double {
			return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		};
		_host.readPolicy = [this]() -> Json { return F4T::WorldPolicy::ToJsonV2(_protocol.CurrentWorldPolicy()); };
		_host.setWorldPolicy = [this](const Json& a_policy, std::string& a_failure) {
			const auto policy = F4T::WorldPolicy::ParsePolicy(a_policy);
			if (!policy) {
				a_failure = "the object is not a valid world policy";
				return false;
			}
			Emit(_protocol.SetWorldPolicy(*policy));
			return true;
		};
		_host.listFactions = [this]() -> Json { return _protocol.AdminListFactions(); };
		_host.disbandFaction = [this](std::string_view a_factionId, std::string& a_failure) {
			const auto frames = _protocol.AdminDisbandFaction(a_factionId, UnixNow(), a_failure);
			if (!a_failure.empty()) return false;
			Emit(frames);
			return true;
		};
		_host.promoteFaction = [this](std::string_view a_factionId, std::string_view a_profileId, std::string& a_failure) {
			const auto frames = _protocol.AdminPromoteFaction(a_factionId, a_profileId, UnixNow(), a_failure);
			if (!a_failure.empty()) return false;
			Emit(frames);
			return true;
		};
		_host.kickFaction = [this](std::string_view a_factionId, std::string_view a_profileId, std::string& a_failure) {
			const auto frames = _protocol.AdminKickFaction(a_factionId, a_profileId, UnixNow(), a_failure);
			if (!a_failure.empty()) return false;
			Emit(frames);
			return true;
		};
		_host.questStatus = [this]() -> Json { return _protocol.QuestStatus(); };
		_host.setQuestMode = [this](std::string_view a_mode) {
			const auto mode = F4T::QuestProtocol::ParseMode(a_mode);
			if (!mode) return false;
			Emit(_protocol.SetQuestMode(*mode, UnixNow()));
			return true;
		};
		_host.setQuestGate = [this](std::string_view a_quest, std::uint16_t a_stage, bool a_release) {
			const auto key = F4T::QuestProtocol::ParseFormKey(a_quest);
			if (!key) return false;
			Emit(_protocol.SetQuestGate(*key, a_stage, a_release, UnixNow()));
			return true;
		};
		_host.setQuestSolo = [this](std::string_view a_profileId, std::string_view a_quest, bool a_solo) {
			const auto key = F4T::QuestProtocol::ParseFormKey(a_quest);
			if (!key) return false;
			Emit(_protocol.SetQuestSolo(a_profileId, *key, a_solo, UnixNow()));
			return true;
		};
		_host.setSettlementOwner = [this](std::uint64_t a_zone, std::string_view a_owner, std::string& a_failure) {
			const auto frames = _protocol.SetSettlementOwner(a_zone, a_owner, a_failure, UnixNow());
			if (!a_failure.empty()) return false;
			Emit(frames);
			return true;
		};
		_host.readState = [this](std::string_view a_bag) -> Json {
			const auto bag = F4T::StateBagProtocol::ParseBag(a_bag);
			if (!bag) return Json::object();
			return _protocol.ReadStateBag(*bag);
		};
		_host.setState = [this](std::string_view a_bag, std::string_view a_key, const Json& a_value, std::string& a_failure) {
			const auto bag = F4T::StateBagProtocol::ParseBag(a_bag);
			if (!bag) {
				a_failure = "bag must be 'global' or 'player:<id>'";
				return false;
			}
			Emit(_protocol.SetStateBag(*bag, a_key, a_value, a_failure));
			return a_failure.empty();
		};
		_host.subscribeEvent = [this](ServerEventKind a_kind, ScriptEventFilter a_filter) {
			return _protocol.Events().Subscribe(a_kind, [filter = std::move(a_filter)](const ServerEvent& a_event) {
				return filter(EventJson(a_event));
			});
		};
		_host.unsubscribeEvent = [this](std::uint64_t a_token) { (void)_protocol.Events().Unsubscribe(a_token); };
		_host.openView = [this](std::string_view a_resource, std::uint32_t a_playerId, std::string_view a_view, std::string& a_failure) {
			Emit(_protocol.OpenResourceView(a_playerId, a_resource, a_view, a_failure));
			return a_failure.empty();
		};
		_host.closeView = [this](std::string_view a_resource, std::uint32_t a_playerId, std::string& a_failure) {
			const auto frames = _protocol.CloseResourceView(a_playerId, a_resource);
			if (frames.empty()) {
				a_failure = "player has no state-bag-v1 session";
				return false;
			}
			Emit(frames);
			return true;
		};
		_host.revivePlayer = [this](std::uint32_t a_playerId, std::string& a_failure) {
			const auto frames = _protocol.AdminRevivePlayer(a_playerId, UnixNow());
			if (frames.empty()) {
				a_failure = ConnectionFor(a_playerId) ? "nothing to do" : "player is not connected";
				return false;
			}
			Emit(frames);
			return true;
		};
		_host.killPlayer = [this](std::uint32_t a_playerId, std::string& a_failure) {
			const auto frames = _protocol.AdminKillPlayer(a_playerId);
			if (frames.empty()) {
				a_failure = ConnectionFor(a_playerId) ? "nothing to do" : "player is not connected";
				return false;
			}
			Emit(frames);
			return true;
		};
		_host.messagePlayer = [this](std::uint32_t a_playerId, std::string_view a_text) {
			const auto connectionId = ConnectionFor(a_playerId);
			if (!connectionId) return false;
			Emit({ ReliableFrame(*connectionId, ChatMessage(*connectionId, a_text)) });
			return true;
		};
		_host.settlementOwner = [this](std::uint64_t a_zone) -> Json { return _protocol.ScriptSettlementOwner(a_zone); };
		_protocol.SetChatCommandRouter([this](std::uint32_t a_playerId, std::string_view a_text) { return RouteChat(a_playerId, a_text); });
		_protocol.SetResourceMessageHandler([this](std::uint32_t a_playerId, std::string_view a_resource, const Json& a_data) {
			_views.Dispatch(a_resource, a_playerId, a_data);
		});
	}

	ScriptHostBinding::~ScriptHostBinding()
	{
		_protocol.SetChatCommandRouter({});
		_protocol.SetResourceMessageHandler({});
	}

	ScriptInstaller ScriptHostBinding::Installer()
	{
		return MakeScriptApiInstaller(_host, _commands, _views);
	}

	std::optional<std::vector<OutboundFrame>> ScriptHostBinding::RouteChat(std::uint32_t a_playerId, std::string_view a_text)
	{
		if (a_text.size() < 2 || a_text.front() != '/') return std::nullopt;
		struct ResetRouted
		{
			std::optional<std::vector<OutboundFrame>>& routed;
			~ResetRouted() { routed.reset(); }
		};
		_routed.emplace();
		const ResetRouted reset{ _routed };
		const auto result = _commands.Dispatch(a_text.substr(1), a_playerId, IsAdmin(a_playerId));
		if (result == ScriptCommandResult::Refused) {
			Notice(a_playerId, "That command requires an administrator.");
		}
		auto routed = std::exchange(_routed, std::nullopt);
		if (result == ScriptCommandResult::NoMatch) return std::nullopt;
		if (routed) AppendFrames(*routed, _protocol.TakeDeferredFrames());
		return routed;
	}

	void ScriptHostBinding::Emit(std::vector<OutboundFrame> a_frames)
	{
		if (_protocol.DeferWhileDispatching(a_frames)) return;
		if (_routed) {
			AppendFrames(*_routed, std::move(a_frames));
			return;
		}
		if (!a_frames.empty()) _send(a_frames);
		auto deferred = _protocol.TakeDeferredFrames();
		if (!deferred.empty()) _send(deferred);
	}

	void ScriptHostBinding::Notice(std::uint32_t a_playerId, std::string_view a_text)
	{
		if (const auto connectionId = ConnectionFor(a_playerId)) {
			Emit({ ReliableFrame(*connectionId, ChatMessage(*connectionId, a_text)) });
		}
	}

	bool ScriptHostBinding::IsAdmin(std::uint32_t a_playerId) const
	{
		const auto connectionId = ConnectionFor(a_playerId);
		return connectionId && _protocol.IsLiveAdmin(*connectionId);
	}

	std::optional<std::uint32_t> ScriptHostBinding::ConnectionFor(std::uint32_t a_playerId) const
	{
		for (const auto& player : _protocol.ScriptPlayers()) {
			if (player.value("id", std::uint32_t{}) == a_playerId) return player.value("connectionId", std::uint32_t{});
		}
		return std::nullopt;
	}

	Json ScriptHostBinding::ChatMessage(std::uint32_t a_connectionId, std::string_view a_text, std::optional<std::uint64_t> a_messageId)
	{
		const bool filtered = F4T::ProfanityFilter::ProfanityForced() || _protocol.ChatProfanityFilter(a_connectionId);
		const std::string text = filtered ? F4T::ProfanityFilter::MaskBlockedWords(a_text, F4T::ProfanityFilter::Scope::All) : std::string{ a_text };
		return Json{
			{ "type", "chatMessage" },
			{ "channel", "global" },
			{ "messageId", a_messageId ? *a_messageId : _protocol.AllocateChatMessageId() },
			{ "fromPlayerId", kScriptSenderPlayerId },
			{ "fromName", "Server" },
			{ "text", text },
			{ "serverTime", UnixNow() }
		};
	}
}
