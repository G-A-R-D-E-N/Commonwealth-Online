#pragma once

#include "OpenSslTokenService.h"
#include "ServerOutboundFrames.h"

#include "F4TCharacterSlotProtocol.h"
#include "F4TPacketRateLimit.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
	class PersistenceGate;
	class DurableStateFileStore;

	class CharacterSlotService
	{
	public:
		using Json = nlohmann::json;
		using SessionMatches = std::function<bool(std::uint32_t a_connectionId, std::string_view a_account)>;

		struct Config
		{
			std::size_t slotCount{ 3 };
			std::uint32_t retentionDays{ 7 };
		};

		struct HelloOutcome
		{
			std::string account;
			std::string persistenceProfile;
			Json readyBlock = Json::object();
		};

		bool Attach(PersistenceGate* a_persistence, const Config& a_config);
		[[nodiscard]] bool Enabled() const noexcept { return persistence != nullptr; }

		HelloOutcome OnHello(
			std::uint32_t a_connectionId,
			std::string_view a_sessionProfileId,
			const Json& a_helloPacket,
			double a_monotonicSeconds,
			double a_unixTimeSeconds,
			const SessionMatches& a_sessionMatches);

		void ReleaseConnection(std::uint32_t a_connectionId);

		[[nodiscard]] std::vector<std::string> TakeErasedProfiles();

		Json ReadyBlock(
			std::string_view a_account,
			std::uint32_t a_connectionId,
			bool a_slotAware,
			double a_unixTimeSeconds);

		std::optional<std::vector<OutboundFrame>> Dispatch(
			std::string_view a_type,
			std::uint32_t a_connectionId,
			std::string_view a_sessionProfileId,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds,
			const SessionMatches& a_sessionMatches,
			F4T::PacketRateLimit::TokenBucket& a_bucket);

	private:
		struct Selection
		{
			std::uint32_t connectionId{};
			std::uint32_t slot{};
		};

		struct PendingDelete
		{
			std::string account;
			std::uint32_t slot{};
			std::uint32_t connectionId{};
			double expiresMonotonic{};
		};

		struct PendingSelection
		{
			std::uint32_t slot{};
			double expiresMonotonic{};
		};

		[[nodiscard]] std::optional<std::string> AccountOf(std::string_view a_sessionProfileId) const;
		void PurgeExpired(std::string_view a_account, double a_unixTimeSeconds, const SessionMatches& a_sessionMatches);
		[[nodiscard]] std::optional<Selection> ActiveSelection(
			std::string_view a_account,
			const SessionMatches& a_sessionMatches) const;
		[[nodiscard]] bool IsLiveSlot(std::string_view a_account, std::uint32_t a_slot) const;
		[[nodiscard]] bool HasRemainingSlotState(std::string_view a_account, std::uint32_t a_exceptSlot) const;
		[[nodiscard]] std::optional<std::uint32_t> LastSelectedLiveSlot(std::string_view a_account) const;
		[[nodiscard]] std::optional<std::uint32_t> HelloSlot(std::string_view a_account, bool a_slotAware);
		[[nodiscard]] std::map<std::uint32_t, Json> SlotRecords(std::string_view a_account) const;
		[[nodiscard]] std::vector<OutboundFrame> Refuse(std::uint32_t a_connectionId, std::string_view a_op, std::string_view a_reason, bool a_hasSlot, std::uint32_t a_slot) const;
		[[nodiscard]] std::string SlotName(std::string_view a_account, std::uint32_t a_slot, const Json& a_record) const;
		[[nodiscard]] bool NameTaken(std::string_view a_account, std::string_view a_name, std::uint32_t a_exceptSlot) const;
		[[nodiscard]] bool PersistRecord(std::string_view a_account, std::uint32_t a_slot, const Json& a_record) const;
		void LogEvent(std::string_view a_account, std::uint32_t a_slot, std::string_view a_event, std::string_view a_detail) const;

		PersistenceGate* persistence{};
		std::vector<std::string> erasedProfiles;
		OpenSslTokenService tokens;
		std::size_t slotCount{ 3 };
		double retentionSeconds{ 7.0 * 86400.0 };
		std::map<std::string, Selection, std::less<>> activeSelections;
		std::map<std::string, PendingDelete, std::less<>> pendingDeletes;
		std::map<std::string, PendingSelection, std::less<>> pendingSelections;
	};
}
