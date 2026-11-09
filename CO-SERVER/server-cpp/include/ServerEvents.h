#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
	enum class ServerEventKind : std::uint8_t
	{
		QuestReport,
		Chat,
		InventoryChange,
		FactionAction,
		SettlementAction,
		PlayerReady,
		PlayerDeath,
		PlayerJoined,
		PlayerLeft,
		StateBagChange
	};

	struct ServerEvent
	{
		ServerEventKind kind{};
		std::uint32_t connectionId{};
		std::uint32_t playerId{};
		std::string profileId;
		const nlohmann::json* packet{};
	};

	inline std::string_view EventName(ServerEventKind a_kind) noexcept
	{
		switch (a_kind) {
		case ServerEventKind::QuestReport:
			return "questReport";
		case ServerEventKind::Chat:
			return "chat";
		case ServerEventKind::InventoryChange:
			return "inventoryChange";
		case ServerEventKind::FactionAction:
			return "factionAction";
		case ServerEventKind::SettlementAction:
			return "settlementAction";
		case ServerEventKind::PlayerReady:
			return "playerReady";
		case ServerEventKind::PlayerDeath:
			return "playerDeath";
		case ServerEventKind::PlayerJoined:
			return "playerJoined";
		case ServerEventKind::PlayerLeft:
			return "playerLeft";
		case ServerEventKind::StateBagChange:
			return "stateBagChange";
		}
		return {};
	}

	inline std::optional<ServerEventKind> ParseEventName(std::string_view a_name) noexcept
	{
		for (std::uint8_t raw = 0; raw <= static_cast<std::uint8_t>(ServerEventKind::StateBagChange); ++raw) {
			const auto kind = static_cast<ServerEventKind>(raw);
			if (EventName(kind) == a_name) return kind;
		}
		return std::nullopt;
	}

	class ServerEvents
	{
	public:
		using Handler = std::function<bool(const ServerEvent&)>;
		using Token = std::uint64_t;

		Token Subscribe(ServerEventKind a_kind, Handler a_handler, int a_priority = 0);
		bool Unsubscribe(Token a_token);
		[[nodiscard]] bool Allow(const ServerEvent& a_event);
		void Notify(const ServerEvent& a_event);
		[[nodiscard]] bool Dispatching() const noexcept;

	private:
		struct Subscription
		{
			Token token{};
			ServerEventKind kind{};
			int priority{};
			Handler handler;
		};

		[[nodiscard]] std::vector<Subscription> Matching(ServerEventKind a_kind) const;

		std::vector<Subscription> subscriptions;
		Token nextToken{ 1 };
		std::size_t dispatchDepth{};
	};
}
