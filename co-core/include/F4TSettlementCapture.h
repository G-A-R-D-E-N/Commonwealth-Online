#pragma once

#include "F4TSettlementAccess.h"
#include "F4TWorkshopTransactions.h"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::SettlementCapture
{
	struct Policy
	{
		double holdSeconds{ 60.0 };
		double heartbeatTimeoutSeconds{ 10.0 };
		double maximumPositionAgeSeconds{ 5.0 };
		double defaultRadius{ 800.0 };
		double maximumRadius{ 6000.0 };
		std::size_t maximumAttempts{ 64 };
	};

	enum class Op : std::uint8_t
	{
		Start,
		Continue,
		Cancel
	};

	enum class Status : std::uint8_t
	{
		Started,
		Holding,
		Contested,
		Captured,
		Cancelled,
		InvalidRequest,
		UnknownBuildZone,
		AlreadyOwner,
		NoAttempt,
		Busy,
		NotInRange,
		StalePosition,
		NotAlive,
		Downed,
		TimedOut,
		RateLimited,
		SettlementLimit,
		PersistenceUnavailable
	};

	enum class Standing : std::uint8_t
	{
		Eligible,
		Stale,
		OutOfRange,
		NotAlive,
		Downed
	};

	struct Workbench
	{
		Workshop::BuildZoneId buildZoneId{};
		double x{};
		double y{};
		double z{};
		double radius{};
	};

	struct Presence
	{
		std::string profileId;
		bool positionKnown{};
		double positionAgeSeconds{};
		double x{};
		double y{};
		double z{};
		bool vitalsKnown{};
		bool alive{};
		bool downed{};
	};

	struct Reply
	{
		Status status{ Status::InvalidRequest };
		double elapsedSeconds{};
		bool attemptEnded{};
	};

	struct Event
	{
		Workshop::BuildZoneId buildZoneId{};
		std::string capturerProfileId;
		Status status{ Status::InvalidRequest };
		double elapsedSeconds{};
	};

	struct Request
	{
		Workshop::BuildZoneId buildZoneId{};
		Op op{ Op::Start };
	};

	[[nodiscard]] Workbench WorkbenchOf(const Workshop::BuildZonePolicy& a_policy, const Policy& a_captureRules) noexcept;
	[[nodiscard]] Standing Classify(const Workbench& a_workbench, const Presence& a_presence, const Policy& a_policy) noexcept;
	[[nodiscard]] bool ValidPolicy(const Policy& a_policy) noexcept;
	[[nodiscard]] Status StatusOf(Standing a_standing) noexcept;
	[[nodiscard]] std::string_view StatusName(Status a_status) noexcept;
	[[nodiscard]] std::optional<Request> NormalizeRequest(const nlohmann::json& a_packet);
	struct DecodedState
	{
		Workshop::BuildZoneId buildZoneId{};
		std::string status;
		std::string capturerProfileId;
		std::string ownerProfileId;
		bool raid{};
		double elapsedSeconds{};
		double holdSeconds{};
		bool hasCapturePoint{};
		double workbenchX{};
		double workbenchY{};
		double workbenchZ{};
		double captureRadius{};
	};

	[[nodiscard]] nlohmann::json MakeState(
		const Workbench& a_workbench,
		std::string_view a_capturerProfileId,
		std::string_view a_ownerProfileId,
		Status a_status,
		double a_elapsedSeconds,
		double a_holdSeconds);
	[[nodiscard]] std::optional<DecodedState> DecodeState(const nlohmann::json& a_packet);
	[[nodiscard]] nlohmann::json MakeReject(Workshop::BuildZoneId a_buildZoneId, Status a_status);
	[[nodiscard]] nlohmann::json MakeZones(const std::vector<Workbench>& a_workbenches);
	[[nodiscard]] std::optional<std::vector<Workbench>> DecodeZones(const nlohmann::json& a_packet);

	class Tracker
	{
	public:
		explicit Tracker(Policy a_policy = {}) noexcept :
			m_policy(a_policy)
		{}

		[[nodiscard]] const Policy& Rules() const noexcept { return m_policy; }
		[[nodiscard]] std::set<Workshop::BuildZoneId> ActiveZones() const;
		[[nodiscard]] std::vector<std::string> CapturersForZone(Workshop::BuildZoneId a_buildZoneId) const;
		[[nodiscard]] std::size_t AttemptCount() const noexcept { return m_attempts.size(); }
		Reply Handle(
			const Request& a_request,
			const Workbench& a_workbench,
			std::string_view a_ownerProfileId,
			std::string_view a_requesterProfileId,
			const std::vector<Presence>& a_presences,
			double a_nowSeconds);
		std::vector<Event> Update(
			const Workbench& a_workbench,
			std::string_view a_ownerProfileId,
			const std::vector<Presence>& a_presences,
			double a_nowSeconds);
		void DropZone(Workshop::BuildZoneId a_buildZoneId);

	private:
		struct Attempt
		{
			Workshop::BuildZoneId buildZoneId{};
			double lastHeartbeat{};
			double holdStart{};
			bool contested{};
		};

		[[nodiscard]] Standing StandingOf(const Workbench& a_workbench, std::string_view a_profileId, const std::vector<Presence>& a_presences) const noexcept;
		[[nodiscard]] bool Contested(const Workbench& a_workbench, std::string_view a_profileId, const std::vector<Presence>& a_presences) const noexcept;
		[[nodiscard]] bool HeartbeatExpired(const Attempt& a_attempt, double a_nowSeconds) const noexcept;
		static bool RefreshContest(Attempt& a_attempt, bool a_contested, double a_nowSeconds) noexcept;

		Policy m_policy;
		std::map<std::string, Attempt> m_attempts;
	};
}
