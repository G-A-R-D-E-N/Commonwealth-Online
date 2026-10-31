#pragma once

#include "F4TPacketRateLimit.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::DownedState
{
	inline constexpr std::uint32_t DefaultDownedSeconds = 60;
	inline constexpr std::uint32_t MinimumDownedSeconds = 5;
	inline constexpr std::uint32_t MaximumDownedSeconds = 600;
	inline constexpr double DefaultReviveHealthFraction = 0.5;
	inline constexpr double DefaultReviveRangeMeters = 3.0;
	inline constexpr double DefaultReviveChannelSeconds = 3.0;
	inline constexpr double MaximumReviveChannelSeconds = 15.0;
	inline constexpr std::uint32_t StimpaksPerRevive = 1;
	inline constexpr std::uint32_t MaximumStimpaksReported = 99;
	inline constexpr std::size_t MaximumReasonChars = 32;
	inline constexpr double ReviveRequestCapacity = 2.0;
	inline constexpr double ReviveRequestRefillPerSecond = 1.0;
	inline constexpr double BroadcastIntervalSeconds = 1.0;
	inline constexpr double kRespawnGraceSeconds = 8.0;
	inline constexpr std::size_t MaximumTrackedPlayers = 256;

	enum class State : std::uint8_t
	{
		Alive,
		Downed,
		Dead,
		Respawning
	};

	enum class ReviveVerdict : std::uint8_t
	{
		Accepted,
		UnknownTarget,
		TargetNotDowned,
		ReviverNotAlive,
		SelfRevive,
		OutOfRange,
		RateLimited,
		AlreadyBeingRevived,
		MissingStimpak
	};

	enum class RespawnVerdict : std::uint8_t
	{
		Accepted,
		NotDead,
		InvalidPoint,
		UnknownFriend,
		NoCheckpoint
	};

	enum class EventKind : std::uint8_t
	{
		Downed,
		Revived,
		Died,
		RespawnGranted,
		ReviveStarted,
		ReviveCancelled,
		HeldItemDropped
	};

	struct Checkpoint
	{
		bool interior{};
		std::uint32_t space{};
		double x{};
		double y{};
		double z{};
		double rotZ{};
	};

	struct Event
	{
		EventKind kind{ EventKind::Downed };
		std::uint32_t playerId{};
		std::uint32_t actorPlayerId{};
		std::uint32_t remainingMs{};
		std::uint32_t channelMs{};
		std::uint32_t stimpaksSpent{};
		double healthFraction{};
		std::string point;
		std::string reason;
		std::uint32_t friendPlayerId{};
		std::optional<Checkpoint> checkpoint;
	};

	struct Config
	{
		std::uint32_t downedSeconds{ DefaultDownedSeconds };
		double reviveHealthFraction{ DefaultReviveHealthFraction };
		double reviveRangeMeters{ DefaultReviveRangeMeters };
		double reviveChannelSeconds{ DefaultReviveChannelSeconds };
		double respawnGraceSeconds{ kRespawnGraceSeconds };
		std::uint32_t stimpaksPerRevive{ StimpaksPerRevive };
		bool dropHeldOnBleedOut{ true };
	};

	struct ReviveOutcome
	{
		ReviveVerdict verdict{ ReviveVerdict::UnknownTarget };
		std::vector<Event> events;
	};

	struct RespawnOutcome
	{
		RespawnVerdict verdict{ RespawnVerdict::NotDead };
		std::vector<Event> events;
	};

	[[nodiscard]] bool IsValidDownedSeconds(std::uint32_t a_seconds) noexcept;
	[[nodiscard]] bool IsValidReviveChannelSeconds(double a_seconds) noexcept;
	[[nodiscard]] bool IsValidCheckpoint(const Checkpoint& a_checkpoint) noexcept;
	[[nodiscard]] bool IsActive(State a_state) noexcept;
	[[nodiscard]] bool IsValidRespawnPoint(std::string_view a_point) noexcept;
	[[nodiscard]] std::string_view ReviveVerdictReason(ReviveVerdict a_verdict) noexcept;
	[[nodiscard]] std::string_view RespawnVerdictReason(RespawnVerdict a_verdict) noexcept;

	class Tracker
	{
	public:
		explicit Tracker(Config a_config = {});

		[[nodiscard]] const Config& GetConfig() const noexcept;
		void SetConfig(Config a_config) noexcept;

		std::vector<Event> Join(std::uint32_t a_playerId, std::string_view a_profileId, double a_nowMonotonicSeconds);
		void Leave(std::uint32_t a_playerId);

		[[nodiscard]] std::optional<State> StateOf(std::uint32_t a_playerId) const noexcept;
		[[nodiscard]] std::uint32_t RemainingMs(std::uint32_t a_playerId, double a_nowMonotonicSeconds) const noexcept;
		[[nodiscard]] std::uint32_t ReviveChannelRemainingMs(std::uint32_t a_targetPlayerId, double a_nowMonotonicSeconds) const noexcept;
		[[nodiscard]] std::vector<Event> DownedSnapshot(double a_nowMonotonicSeconds) const;
		[[nodiscard]] std::size_t TrackedCount() const noexcept;

		std::vector<Event> ObserveVitals(
			std::uint32_t a_playerId,
			bool a_isDowned,
			bool a_isDead,
			double a_nowMonotonicSeconds,
			bool a_carriesStateFlags = true);
		std::vector<Event> Expire(double a_nowMonotonicSeconds);

		// Constraint: a channel of 0 s revives inside this call.
		[[nodiscard]] ReviveOutcome Revive(
			std::uint32_t a_reviverPlayerId,
			std::uint32_t a_targetPlayerId,
			std::optional<double> a_distanceMeters,
			double a_nowMonotonicSeconds,
			std::optional<std::uint32_t> a_stimpaksAvailable = std::nullopt);
		std::vector<Event> CancelRevive(
			std::uint32_t a_targetPlayerId,
			std::uint32_t a_reviverPlayerId,
			std::string_view a_reason,
			double a_nowMonotonicSeconds);
		std::vector<Event> AdvanceChannels(double a_nowMonotonicSeconds);

		[[nodiscard]] RespawnOutcome Respawn(
			std::uint32_t a_playerId,
			std::string_view a_point,
			std::uint32_t a_friendPlayerId,
			double a_nowMonotonicSeconds);

		bool RecordCheckpoint(std::uint32_t a_playerId, const Checkpoint& a_checkpoint);
		[[nodiscard]] std::optional<Checkpoint> CheckpointOf(std::uint32_t a_playerId) const;

		std::vector<Event> AdminRevive(std::uint32_t a_playerId, double a_nowMonotonicSeconds);
		std::vector<Event> AdminKill(std::uint32_t a_playerId);

	private:
		struct Entry
		{
			std::string key;
			std::uint32_t playerId{};
			bool online{};
			State state{ State::Alive };
			double downedDeadlineSeconds{};
			double respawnDeadlineSeconds{};
			std::uint32_t revivingPlayerId{};
			double reviveDeadlineSeconds{};
			std::uint32_t revivingStimpaksSpent{};
			std::optional<Checkpoint> checkpoint;
			F4T::PacketRateLimit::TokenBucket reviveRequests;
		};

		[[nodiscard]] static double EffectiveDeadline(const Entry& a_entry) noexcept;
		static void EndChannel(Entry& a_entry) noexcept;
		void KillByBleedOut(Entry& a_entry, std::vector<Event>& a_events);
		std::vector<Event> CompleteChannel(Entry& a_entry, double a_nowMonotonicSeconds);

		[[nodiscard]] Entry* FindByPlayer(std::uint32_t a_playerId) noexcept;
		[[nodiscard]] const Entry* FindByPlayer(std::uint32_t a_playerId) const noexcept;
		[[nodiscard]] Entry* EnsureEntry(std::uint32_t a_playerId);
		[[nodiscard]] bool EvictOldestOffline();
		void EnterDowned(Entry& a_entry, double a_nowMonotonicSeconds);
		void EnterRespawning(Entry& a_entry, double a_nowMonotonicSeconds);

		Config _config;
		std::map<std::string, Entry> _entries;
	};
}
