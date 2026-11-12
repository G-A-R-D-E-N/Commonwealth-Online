#pragma once

#include "F4TObjectWire.h"
#include "F4TParkedSuitBreach.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ParkedSuit
{
	inline constexpr std::string_view kCapabilityName = "parked-suit-v1";
	inline constexpr std::string_view kPacketType = "parkedSuit";
	inline constexpr std::size_t kMaximumPerOwner = 4;
	inline constexpr std::size_t kMaximumTotal = 256;
	inline constexpr std::size_t kMaximumPendingReleases = 1024;
	inline constexpr std::size_t kMaximumPieceKeys = 8;

	struct ParkRequest
	{
		std::string localId;
		nlohmann::json powerArmor;
		F4T::ObjectWire::ObjectPosition position;
		double angleZ{};
		std::vector<std::string> pieceKeys;
		bool console{};
	};

	struct PendingRelease
	{
		std::string playerKey;
		std::string suitId;
		std::string localId;
	};

	struct HackRequest
	{
		std::string suitId;
		std::vector<Breach::Pick> picks;
	};

	struct AdoptRequest
	{
		std::string suitId;
		std::string localId;
	};

	struct Record
	{
		std::string ownerKey;
		std::string suitId;
		std::string localId;
		std::string localOwnerKey;
		nlohmann::json powerArmor;
		F4T::ObjectWire::ObjectPosition position;
		double angleZ{};
		std::vector<std::string> pieceKeys;
		bool hacked{};
		double ownerLastSeenUnix{};
	};

	enum class ParkOutcome : std::uint8_t
	{
		Stored,
		Replaced,
		OwnerLimit,
		TableFull,
		NotOwner
	};

	enum class HackOutcome : std::uint8_t
	{
		Hacked,
		Missing,
		SameOwner,
		OwnerLimit
	};

	enum class AdoptOutcome : std::uint8_t
	{
		Adopted,
		Missing,
		NotOwner,
		Duplicate
	};

	struct PieceAuditResult
	{
		bool allPresent{};
		bool expired{};
		std::vector<std::string> missing;
	};

	[[nodiscard]] std::optional<std::string> NormalizeSuitId(const nlohmann::json& a_value);
	[[nodiscard]] std::optional<std::uint32_t> NormalizeNonce(const nlohmann::json& a_value);
	[[nodiscard]] std::optional<ParkRequest> NormalizeParkRequest(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<std::string> NormalizeRemoveRequest(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<std::string> NormalizeBreachStartRequest(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<HackRequest> NormalizeHackRequest(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<AdoptRequest> NormalizeAdoptRequest(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<std::string> NormalizeBreachFailedRequest(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<std::string> NormalizeHackAlert(const nlohmann::json& a_packet);
	[[nodiscard]] bool IsWriteRequest(const nlohmann::json& a_packet);
	[[nodiscard]] nlohmann::json BuildParkPacket(const Record& a_record);
	[[nodiscard]] nlohmann::json BuildRemovePacket(std::string_view a_ownerKey, std::string_view a_suitId);
	[[nodiscard]] nlohmann::json BuildReleasePacket(std::string_view a_suitId, std::string_view a_localId);
	[[nodiscard]] nlohmann::json BuildBreachChallengePacket(std::string_view a_suitId, std::uint32_t a_seed, std::size_t a_length);
	[[nodiscard]] nlohmann::json BuildAckPacket(const nlohmann::json& a_request, bool a_ok, std::string_view a_reason = {});
	[[nodiscard]] nlohmann::json BuildHackResultPacket(std::string_view a_suitId, bool a_hacked);
	[[nodiscard]] nlohmann::json BuildHackAlertPacket(std::string_view a_suitId, bool a_failed);
	[[nodiscard]] nlohmann::json BuildBreachFailedPacket(std::string_view a_suitId);
	[[nodiscard]] PieceAuditResult EvaluatePieceAudit(const std::vector<std::string>& a_keys, const nlohmann::json& a_inventory, double a_now, double a_deadline);

	class Table
	{
	public:
		[[nodiscard]] ParkOutcome Park(Record a_record);
		[[nodiscard]] std::optional<std::string> Remove(std::string_view a_ownerKey, std::string_view a_localId);
		[[nodiscard]] const Record* Find(std::string_view a_suitId) const;
		[[nodiscard]] const Record* FindByLocal(std::string_view a_localOwnerKey, std::string_view a_localId) const;
		[[nodiscard]] HackOutcome Reassign(std::string_view a_suitId, std::string a_newOwnerKey);
		[[nodiscard]] AdoptOutcome Adopt(std::string_view a_suitId, std::string_view a_requesterKey, std::string_view a_localId, std::string* a_previousLocalOwnerKey);
		[[nodiscard]] std::vector<PendingRelease> TakeReleases(std::string_view a_playerKey);
		[[nodiscard]] bool ClearRelease(std::string_view a_playerKey, std::string_view a_localId);
		[[nodiscard]] std::vector<Record> SweepOffline(double a_nowUnix, double a_retentionSeconds, const std::set<std::string>& a_onlineOwnerKeys);
		[[nodiscard]] std::optional<Record> EvictOldestOffline(const std::set<std::string>& a_onlineOwnerKeys);
		[[nodiscard]] const std::vector<Record>& Entries() const noexcept { return m_records; }
		[[nodiscard]] nlohmann::json ToJson() const;
		void LoadFromJson(const nlohmann::json& a_records);

	private:
		[[nodiscard]] std::string NextSuitId();

		std::vector<Record> m_records;
		std::vector<PendingRelease> m_releases;
		std::uint32_t m_nextSuitNumber{ 1 };
	};
}
