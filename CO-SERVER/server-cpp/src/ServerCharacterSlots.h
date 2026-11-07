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
		};

		struct HelloOutcome
		{
			std::string account;
			std::string persistenceProfile;
			std::string questProfile;
			Json readyBlock = Json::object();
		};

		bool Attach(PersistenceGate* a_persistence, const Config& a_config);
		[[nodiscard]] bool Enabled() const noexcept { return persistence != nullptr; }

		HelloOutcome OnHello(
			std::uint32_t a_connectionId,
			std::string_view a_sessionProfileId,
			std::string_view a_resumeProfile,
			const Json& a_helloPacket,
			double a_monotonicSeconds,
			double a_unixTimeSeconds);

		void ReleaseConnection(std::uint32_t a_connectionId);

		[[nodiscard]] std::vector<std::string> TakeErasedProfiles();
		struct AbandonedConnection
		{
			std::uint32_t connectionId{};
			std::string profileKey;
		};

		[[nodiscard]] std::optional<AbandonedConnection> TakeAbandonedConnection();
		[[nodiscard]] bool AbandonForAdmin(std::string_view a_account, std::uint32_t a_slot, double a_unixTimeSeconds, const SessionMatches& a_sessionMatches, bool a_withoutSave = false);
		[[nodiscard]] std::vector<std::string> PendingQuestErase() const;
		[[nodiscard]] bool ClearPendingQuestErase(const std::vector<std::string>& a_questProfiles);

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
		void ReconcileRecords(std::string_view a_account, double a_unixTimeSeconds, const SessionMatches& a_sessionMatches);
		bool EraseSlot(std::string_view a_account, std::uint32_t a_slot, bool a_keepConfirmation = false);
		void ClearAbandonConfirmation(std::string_view a_account);
		[[nodiscard]] std::optional<Selection> ActiveSelection(
			std::string_view a_account,
			const SessionMatches& a_sessionMatches) const;
		[[nodiscard]] bool IsLiveSlot(std::string_view a_account, std::uint32_t a_slot) const;
		[[nodiscard]] bool HasRemainingSlotState(std::string_view a_account, std::uint32_t a_exceptSlot) const;
		[[nodiscard]] std::optional<std::uint32_t> LastSelectedLiveSlot(std::string_view a_account) const;
		[[nodiscard]] std::optional<std::uint32_t> HelloSlot(std::string_view a_account, std::string_view a_resumeProfile, bool a_slotAware);
		[[nodiscard]] std::optional<std::uint32_t> ResumedSlot(std::string_view a_account, std::string_view a_resumeProfile) const;
		[[nodiscard]] std::map<std::uint32_t, Json> SlotRecords(std::string_view a_account) const;
		[[nodiscard]] std::vector<OutboundFrame> Refuse(std::uint32_t a_connectionId, std::string_view a_op, std::string_view a_reason, bool a_hasSlot, std::uint32_t a_slot) const;
		[[nodiscard]] std::vector<OutboundFrame> AbandonBound(std::uint32_t a_connectionId, std::string_view a_sessionProfileId, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds, F4T::PacketRateLimit::TokenBucket& a_bucket);
		[[nodiscard]] std::vector<OutboundFrame> AbandonUnstarted(std::uint32_t a_connectionId, std::string_view a_account, std::string_view a_op, double a_unixTimeSeconds);
		[[nodiscard]] std::string SlotName(std::string_view a_account, std::uint32_t a_slot, const Json& a_record) const;
		[[nodiscard]] bool NameTaken(std::string_view a_account, std::string_view a_name, std::uint32_t a_exceptSlot) const;
		[[nodiscard]] bool PersistRecord(std::string_view a_account, std::uint32_t a_slot, const Json& a_record) const;
		void LogEvent(std::string_view a_account, std::uint32_t a_slot, std::string_view a_event, std::string_view a_detail) const;

		PersistenceGate* persistence{};
		std::optional<AbandonedConnection> abandonedConnection;
		std::vector<std::string> erasedProfiles;
		OpenSslTokenService tokens;
		std::size_t slotCount{ 3 };
		std::map<std::string, Selection, std::less<>> activeSelections;
		std::map<std::string, PendingDelete, std::less<>> pendingDeletes;
		std::map<std::string, PendingSelection, std::less<>> pendingSelections;
	};
}
