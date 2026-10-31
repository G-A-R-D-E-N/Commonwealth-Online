#include "F4TDownedState.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace F4T::DownedState
{
	namespace
	{
		constexpr double kMaximumCheckpointCoordinate = 200000.0;

		std::string SynthesizedKey(std::uint32_t a_playerId)
		{
			return "#" + std::to_string(a_playerId);
		}

		std::string SanitizeReason(std::string_view a_reason)
		{
			std::string reason{ a_reason.substr(0, MaximumReasonChars) };
			return reason.empty() ? std::string{ "cancelled" } : reason;
		}
	}

	bool IsValidDownedSeconds(std::uint32_t a_seconds) noexcept
	{
		return a_seconds >= MinimumDownedSeconds && a_seconds <= MaximumDownedSeconds;
	}

	bool IsValidReviveChannelSeconds(double a_seconds) noexcept
	{
		return std::isfinite(a_seconds) && a_seconds >= 0.0 && a_seconds <= MaximumReviveChannelSeconds;
	}

	bool IsValidCheckpoint(const Checkpoint& a_checkpoint) noexcept
	{
		if (a_checkpoint.space == 0 || (a_checkpoint.space >> 24) == 0xFF) {
			return false;
		}
		for (const auto value : { a_checkpoint.x, a_checkpoint.y, a_checkpoint.z, a_checkpoint.rotZ }) {
			if (!std::isfinite(value) || std::abs(value) > kMaximumCheckpointCoordinate) {
				return false;
			}
		}
		return true;
	}

	bool IsActive(State a_state) noexcept
	{
		return a_state == State::Alive || a_state == State::Respawning;
	}

	bool IsValidRespawnPoint(std::string_view a_point) noexcept
	{
		return a_point == "sanctuary" || a_point == "lastbed" || a_point == "friend" || a_point == "checkpoint";
	}

	std::string_view ReviveVerdictReason(ReviveVerdict a_verdict) noexcept
	{
		switch (a_verdict) {
		case ReviveVerdict::Accepted: return "accepted";
		case ReviveVerdict::UnknownTarget: return "unknownTarget";
		case ReviveVerdict::TargetNotDowned: return "targetNotDowned";
		case ReviveVerdict::ReviverNotAlive: return "reviverNotAlive";
		case ReviveVerdict::SelfRevive: return "selfRevive";
		case ReviveVerdict::OutOfRange: return "outOfRange";
		case ReviveVerdict::RateLimited: return "rateLimited";
		case ReviveVerdict::AlreadyBeingRevived: return "alreadyBeingRevived";
		case ReviveVerdict::MissingStimpak: return "missingStimpak";
		}
		return "unknownTarget";
	}

	std::string_view RespawnVerdictReason(RespawnVerdict a_verdict) noexcept
	{
		switch (a_verdict) {
		case RespawnVerdict::Accepted: return "accepted";
		case RespawnVerdict::NotDead: return "notDead";
		case RespawnVerdict::InvalidPoint: return "invalidPoint";
		case RespawnVerdict::UnknownFriend: return "unknownFriend";
		case RespawnVerdict::NoCheckpoint: return "noCheckpoint";
		}
		return "notDead";
	}

	Tracker::Tracker(Config a_config) :
		_config(a_config)
	{}

	const Config& Tracker::GetConfig() const noexcept
	{
		return _config;
	}

	void Tracker::SetConfig(Config a_config) noexcept
	{
		_config = a_config;
	}

	std::size_t Tracker::TrackedCount() const noexcept
	{
		return _entries.size();
	}

	double Tracker::EffectiveDeadline(const Entry& a_entry) noexcept
	{
		if (a_entry.revivingPlayerId == 0) {
			return a_entry.downedDeadlineSeconds;
		}
		return (std::max)(a_entry.downedDeadlineSeconds, a_entry.reviveDeadlineSeconds);
	}

	void Tracker::EndChannel(Entry& a_entry) noexcept
	{
		a_entry.revivingPlayerId = 0;
		a_entry.reviveDeadlineSeconds = 0.0;
		a_entry.revivingStimpaksSpent = 0;
	}

	Tracker::Entry* Tracker::FindByPlayer(std::uint32_t a_playerId) noexcept
	{
		if (a_playerId == 0) {
			return nullptr;
		}
		const auto found = std::find_if(_entries.begin(), _entries.end(), [a_playerId](const auto& a_entry) {
			return a_entry.second.online && a_entry.second.playerId == a_playerId;
		});
		return found == _entries.end() ? nullptr : std::addressof(found->second);
	}

	const Tracker::Entry* Tracker::FindByPlayer(std::uint32_t a_playerId) const noexcept
	{
		if (a_playerId == 0) {
			return nullptr;
		}
		const auto found = std::find_if(_entries.begin(), _entries.end(), [a_playerId](const auto& a_entry) {
			return a_entry.second.online && a_entry.second.playerId == a_playerId;
		});
		return found == _entries.end() ? nullptr : std::addressof(found->second);
	}

	Tracker::Entry* Tracker::EnsureEntry(std::uint32_t a_playerId)
	{
		if (auto* existing = FindByPlayer(a_playerId)) {
			return existing;
		}
		if (!EvictOldestOffline()) {
			return nullptr;
		}
		const auto key = SynthesizedKey(a_playerId);
		auto& entry = _entries[key];
		entry = Entry{};
		entry.key = key;
		entry.playerId = a_playerId;
		entry.online = true;
		entry.state = State::Alive;
		entry.downedDeadlineSeconds = 0.0;
		entry.reviveRequests = F4T::PacketRateLimit::TokenBucket{};
		return std::addressof(entry);
	}

	bool Tracker::EvictOldestOffline()
	{
		if (_entries.size() < MaximumTrackedPlayers) {
			return true;
		}
		const auto found = std::find_if(_entries.begin(), _entries.end(), [](const auto& a_entry) {
			return !a_entry.second.online;
		});
		if (found == _entries.end()) {
			return false;
		}
		_entries.erase(found);
		return true;
	}

	void Tracker::EnterDowned(Entry& a_entry, double a_nowMonotonicSeconds)
	{
		a_entry.state = State::Downed;
		a_entry.downedDeadlineSeconds = a_nowMonotonicSeconds + static_cast<double>(_config.downedSeconds);
		a_entry.respawnDeadlineSeconds = 0.0;
		EndChannel(a_entry);
	}

	void Tracker::EnterRespawning(Entry& a_entry, double a_nowMonotonicSeconds)
	{
		a_entry.state = State::Respawning;
		a_entry.downedDeadlineSeconds = 0.0;
		a_entry.respawnDeadlineSeconds = a_nowMonotonicSeconds + _config.respawnGraceSeconds;
		EndChannel(a_entry);
	}

	void Tracker::KillByBleedOut(Entry& a_entry, std::vector<Event>& a_events)
	{
		if (a_entry.revivingPlayerId != 0) {
			a_events.push_back(Event{
				.kind = EventKind::ReviveCancelled,
				.playerId = a_entry.playerId,
				.actorPlayerId = a_entry.revivingPlayerId,
				.reason = "targetDied"
			});
			EndChannel(a_entry);
		}
		a_entry.state = State::Dead;
		a_entry.downedDeadlineSeconds = 0.0;
		if (!a_entry.online) {
			return;
		}
		if (_config.dropHeldOnBleedOut) {
			a_events.push_back(Event{
				.kind = EventKind::HeldItemDropped,
				.playerId = a_entry.playerId,
				.reason = "bledOut"
			});
		}
		a_events.push_back(Event{ .kind = EventKind::Died, .playerId = a_entry.playerId });
	}

	std::vector<Event> Tracker::CompleteChannel(Entry& a_entry, double a_nowMonotonicSeconds)
	{
		const auto reviverId = a_entry.revivingPlayerId;
		const auto stimpaksSpent = a_entry.revivingStimpaksSpent;
		EndChannel(a_entry);
		EnterRespawning(a_entry, a_nowMonotonicSeconds);
		return { Event{
			.kind = EventKind::Revived,
			.playerId = a_entry.playerId,
			.actorPlayerId = reviverId,
			.stimpaksSpent = stimpaksSpent,
			.healthFraction = _config.reviveHealthFraction
		} };
	}

	std::optional<State> Tracker::StateOf(std::uint32_t a_playerId) const noexcept
	{
		const auto* entry = FindByPlayer(a_playerId);
		return entry == nullptr ? std::nullopt : std::optional{ entry->state };
	}

	std::uint32_t Tracker::RemainingMs(std::uint32_t a_playerId, double a_nowMonotonicSeconds) const noexcept
	{
		const auto* entry = FindByPlayer(a_playerId);
		if (entry == nullptr || entry->state != State::Downed) {
			return 0;
		}
		const auto remainingSeconds = EffectiveDeadline(*entry) - a_nowMonotonicSeconds;
		if (!(remainingSeconds > 0.0)) {
			return 0;
		}
		const auto milliseconds = remainingSeconds * 1000.0;
		return milliseconds >= static_cast<double>((std::numeric_limits<std::uint32_t>::max)())
			? (std::numeric_limits<std::uint32_t>::max)()
			: static_cast<std::uint32_t>(milliseconds + 0.5);
	}

	std::uint32_t Tracker::ReviveChannelRemainingMs(std::uint32_t a_targetPlayerId, double a_nowMonotonicSeconds) const noexcept
	{
		const auto* entry = FindByPlayer(a_targetPlayerId);
		if (entry == nullptr || entry->state != State::Downed || entry->revivingPlayerId == 0) {
			return 0;
		}
		const auto remainingSeconds = entry->reviveDeadlineSeconds - a_nowMonotonicSeconds;
		if (!(remainingSeconds > 0.0)) {
			return 0;
		}
		const auto milliseconds = remainingSeconds * 1000.0;
		return milliseconds >= static_cast<double>((std::numeric_limits<std::uint32_t>::max)())
			? (std::numeric_limits<std::uint32_t>::max)()
			: static_cast<std::uint32_t>(milliseconds + 0.5);
	}

	std::vector<Event> Tracker::DownedSnapshot(double a_nowMonotonicSeconds) const
	{
		std::vector<Event> events;
		for (const auto& [key, entry] : _entries) {
			(void)key;
			if (entry.online && entry.state == State::Downed) {
				events.push_back(Event{
					.kind = EventKind::Downed,
					.playerId = entry.playerId,
					.actorPlayerId = entry.revivingPlayerId,
					.remainingMs = RemainingMs(entry.playerId, a_nowMonotonicSeconds),
					.channelMs = ReviveChannelRemainingMs(entry.playerId, a_nowMonotonicSeconds)
				});
			}
		}
		return events;
	}

	std::vector<Event> Tracker::Join(
		std::uint32_t a_playerId,
		std::string_view a_profileId,
		double a_nowMonotonicSeconds)
	{
		if (a_playerId == 0) {
			return {};
		}

		Entry* entry = nullptr;
		if (const auto byProfile = a_profileId.empty()
				? _entries.end()
				: _entries.find(std::string{ a_profileId });
			byProfile != _entries.end()) {
			entry = std::addressof(byProfile->second);
		} else if (const auto byPlayer = std::find_if(_entries.begin(), _entries.end(), [a_playerId](const auto& a_candidate) {
				return a_candidate.second.playerId == a_playerId;
			});
			byPlayer != _entries.end()) {
			entry = std::addressof(byPlayer->second);
		}

		if (entry == nullptr) {
			if (!EvictOldestOffline()) {
				return {};
			}
			const auto key = a_profileId.empty() ? SynthesizedKey(a_playerId) : std::string{ a_profileId };
			auto& created = _entries[key];
			created = Entry{};
			created.key = key;
			created.playerId = a_playerId;
			created.online = true;
			created.state = State::Alive;
			created.downedDeadlineSeconds = 0.0;
			created.reviveRequests = F4T::PacketRateLimit::TokenBucket{};
			return {};
		}

		entry->playerId = a_playerId;
		entry->online = true;
		entry->reviveRequests = F4T::PacketRateLimit::TokenBucket{};

		if (entry->state == State::Downed && a_nowMonotonicSeconds >= EffectiveDeadline(*entry)) {
			std::vector<Event> events;
			KillByBleedOut(*entry, events);
			return events;
		}
		if (entry->state == State::Downed) {
			return { Event{
				.kind = EventKind::Downed,
				.playerId = a_playerId,
				.actorPlayerId = entry->revivingPlayerId,
				.remainingMs = RemainingMs(a_playerId, a_nowMonotonicSeconds),
				.channelMs = ReviveChannelRemainingMs(a_playerId, a_nowMonotonicSeconds)
			} };
		}
		if (entry->state == State::Dead) {
			return { Event{ .kind = EventKind::Died, .playerId = a_playerId } };
		}
		return {};
	}

	void Tracker::Leave(std::uint32_t a_playerId)
	{
		// Constraint: a reviver who leaves must not leave the claim held.
		for (auto& [key, entry] : _entries) {
			(void)key;
			if (entry.revivingPlayerId == a_playerId) {
				EndChannel(entry);
			}
		}
		auto* entry = FindByPlayer(a_playerId);
		if (entry == nullptr) {
			return;
		}
		entry->online = false;
		entry->playerId = 0;
		if (entry->state == State::Alive || entry->state == State::Respawning) {
			_entries.erase(entry->key);
			return;
		}
		EndChannel(*entry);
	}

	std::vector<Event> Tracker::ObserveVitals(
		std::uint32_t a_playerId,
		bool a_isDowned,
		bool a_isDead,
		double a_nowMonotonicSeconds,
		bool a_carriesStateFlags)
	{
		if (a_playerId == 0) {
			return {};
		}
		auto* entry = EnsureEntry(a_playerId);
		if (entry == nullptr) {
			return {};
		}

		if (entry->state == State::Downed) {
			return {};
		}
		if (entry->state == State::Dead) {
			return {};
		}
		if (entry->state == State::Respawning) {
			if (a_nowMonotonicSeconds < entry->respawnDeadlineSeconds) {
				if (a_carriesStateFlags && !a_isDowned && !a_isDead) {
					entry->state = State::Alive;
					entry->respawnDeadlineSeconds = 0.0;
				}
				return {};
			}
			entry->state = State::Alive;
			entry->respawnDeadlineSeconds = 0.0;
		}

		if (a_isDowned) {
			EnterDowned(*entry, a_nowMonotonicSeconds);
			return { Event{
				.kind = EventKind::Downed,
				.playerId = a_playerId,
				.remainingMs = RemainingMs(a_playerId, a_nowMonotonicSeconds)
			} };
		}
		if (a_isDead) {
			entry->state = State::Dead;
			entry->downedDeadlineSeconds = 0.0;
			EndChannel(*entry);
			return { Event{ .kind = EventKind::Died, .playerId = a_playerId } };
		}
		return {};
	}

	std::vector<Event> Tracker::AdvanceChannels(double a_nowMonotonicSeconds)
	{
		std::vector<Event> events;
		for (auto& [key, entry] : _entries) {
			(void)key;
			if (entry.state != State::Downed || entry.revivingPlayerId == 0) {
				continue;
			}
			if (a_nowMonotonicSeconds < entry.reviveDeadlineSeconds) {
				continue;
			}
			const auto* reviver = FindByPlayer(entry.revivingPlayerId);
			if (reviver == nullptr || !IsActive(reviver->state)) {
				events.push_back(Event{
					.kind = EventKind::ReviveCancelled,
					.playerId = entry.playerId,
					.actorPlayerId = entry.revivingPlayerId,
					.reason = "reviverLeft"
				});
				EndChannel(entry);
				continue;
			}
			const auto completed = CompleteChannel(entry, a_nowMonotonicSeconds);
			events.insert(events.end(), completed.begin(), completed.end());
		}
		return events;
	}

	std::vector<Event> Tracker::Expire(double a_nowMonotonicSeconds)
	{
		std::vector<Event> events = AdvanceChannels(a_nowMonotonicSeconds);
		for (auto& [key, entry] : _entries) {
			(void)key;
			if (entry.state != State::Downed || a_nowMonotonicSeconds < EffectiveDeadline(entry)) {
				continue;
			}
			KillByBleedOut(entry, events);
		}
		return events;
	}

	ReviveOutcome Tracker::Revive(
		std::uint32_t a_reviverPlayerId,
		std::uint32_t a_targetPlayerId,
		std::optional<double> a_distanceMeters,
		double a_nowMonotonicSeconds,
		std::optional<std::uint32_t> a_stimpaksAvailable)
	{
		auto* target = FindByPlayer(a_targetPlayerId);
		if (target == nullptr) {
			return { ReviveVerdict::UnknownTarget, {} };
		}
		if (a_reviverPlayerId == a_targetPlayerId) {
			return { ReviveVerdict::SelfRevive, {} };
		}
		auto* reviver = FindByPlayer(a_reviverPlayerId);
		if (reviver == nullptr || !IsActive(reviver->state)) {
			return { ReviveVerdict::ReviverNotAlive, {} };
		}
		if (!F4T::PacketRateLimit::TakeToken(
				reviver->reviveRequests,
				a_nowMonotonicSeconds,
				ReviveRequestRefillPerSecond,
				ReviveRequestCapacity)) {
			return { ReviveVerdict::RateLimited, {} };
		}
		if (target->state != State::Downed || a_nowMonotonicSeconds >= EffectiveDeadline(*target)) {
			return { ReviveVerdict::TargetNotDowned, {} };
		}
		if (target->revivingPlayerId != 0) {
			return { ReviveVerdict::AlreadyBeingRevived, {} };
		}
		if (!a_distanceMeters || *a_distanceMeters > _config.reviveRangeMeters) {
			return { ReviveVerdict::OutOfRange, {} };
		}
		const bool stimpakReported = a_stimpaksAvailable.has_value();
		if (stimpakReported && *a_stimpaksAvailable < _config.stimpaksPerRevive) {
			return { ReviveVerdict::MissingStimpak, {} };
		}

		const auto channelSeconds = _config.reviveChannelSeconds > 0.0 ? _config.reviveChannelSeconds : 0.0;
		const auto stimpaksSpent = stimpakReported ? _config.stimpaksPerRevive : 0U;
		if (channelSeconds <= 0.0) {
			EnterRespawning(*target, a_nowMonotonicSeconds);
			return { ReviveVerdict::Accepted, { Event{
				.kind = EventKind::Revived,
				.playerId = a_targetPlayerId,
				.actorPlayerId = a_reviverPlayerId,
				.stimpaksSpent = stimpaksSpent,
				.healthFraction = _config.reviveHealthFraction
			} } };
		}

		target->revivingPlayerId = a_reviverPlayerId;
		target->reviveDeadlineSeconds = a_nowMonotonicSeconds + channelSeconds;
		target->revivingStimpaksSpent = stimpaksSpent;
		const auto channelMs = static_cast<std::uint32_t>(channelSeconds * 1000.0 + 0.5);
		return { ReviveVerdict::Accepted, { Event{
			.kind = EventKind::ReviveStarted,
			.playerId = a_targetPlayerId,
			.actorPlayerId = a_reviverPlayerId,
			.channelMs = channelMs
		} } };
	}

	std::vector<Event> Tracker::CancelRevive(
		std::uint32_t a_targetPlayerId,
		std::uint32_t a_reviverPlayerId,
		std::string_view a_reason,
		double a_nowMonotonicSeconds)
	{
		(void)a_nowMonotonicSeconds;
		auto* target = FindByPlayer(a_targetPlayerId);
		if (target == nullptr || target->state != State::Downed || target->revivingPlayerId == 0) {
			return {};
		}
		if (a_reviverPlayerId != 0 && a_reviverPlayerId != target->revivingPlayerId) {
			return {};
		}
		const auto reviverId = target->revivingPlayerId;
		EndChannel(*target);
		return { Event{
			.kind = EventKind::ReviveCancelled,
			.playerId = a_targetPlayerId,
			.actorPlayerId = reviverId,
			.reason = SanitizeReason(a_reason)
		} };
	}

	RespawnOutcome Tracker::Respawn(
		std::uint32_t a_playerId,
		std::string_view a_point,
		std::uint32_t a_friendPlayerId,
		double a_nowMonotonicSeconds)
	{
		auto* entry = FindByPlayer(a_playerId);
		if (entry == nullptr || entry->state != State::Dead) {
			return { RespawnVerdict::NotDead, {} };
		}
		if (!IsValidRespawnPoint(a_point)) {
			return { RespawnVerdict::InvalidPoint, {} };
		}
		if (a_point == "friend") {
			const auto* friendEntry = a_friendPlayerId == a_playerId ? nullptr : FindByPlayer(a_friendPlayerId);
			if (a_friendPlayerId == 0 || friendEntry == nullptr || !IsActive(friendEntry->state)) {
				return { RespawnVerdict::UnknownFriend, {} };
			}
		}
		if (a_point == "checkpoint" && !entry->checkpoint) {
			return { RespawnVerdict::NoCheckpoint, {} };
		}

		EnterRespawning(*entry, a_nowMonotonicSeconds);
		return { RespawnVerdict::Accepted, { Event{
			.kind = EventKind::RespawnGranted,
			.playerId = a_playerId,
			.point = std::string{ a_point },
			.friendPlayerId = a_point == "friend" ? a_friendPlayerId : 0U,
			.checkpoint = a_point == "checkpoint" ? entry->checkpoint : std::optional<Checkpoint>{}
		} } };
	}

	bool Tracker::RecordCheckpoint(std::uint32_t a_playerId, const Checkpoint& a_checkpoint)
	{
		if (!IsValidCheckpoint(a_checkpoint)) {
			return false;
		}
		auto* entry = FindByPlayer(a_playerId);
		if (entry == nullptr) {
			return false;
		}
		entry->checkpoint = a_checkpoint;
		return true;
	}

	std::optional<Checkpoint> Tracker::CheckpointOf(std::uint32_t a_playerId) const
	{
		const auto* entry = FindByPlayer(a_playerId);
		if (entry == nullptr) {
			return std::nullopt;
		}
		return entry->checkpoint;
	}

	std::vector<Event> Tracker::AdminRevive(std::uint32_t a_playerId, double a_nowMonotonicSeconds)
	{
		auto* entry = FindByPlayer(a_playerId);
		if (entry == nullptr || IsActive(entry->state)) {
			return {};
		}
		std::vector<Event> events{ Event{
			.kind = EventKind::Revived,
			.playerId = a_playerId,
			.healthFraction = _config.reviveHealthFraction
		} };
		const bool wasDead = entry->state == State::Dead;
		EnterRespawning(*entry, a_nowMonotonicSeconds);
		if (wasDead) {
			events.push_back(Event{
				.kind = EventKind::RespawnGranted,
				.playerId = a_playerId,
				.point = "lastbed"
			});
			return events;
		}
		return events;
	}

	std::vector<Event> Tracker::AdminKill(std::uint32_t a_playerId)
	{
		auto* entry = FindByPlayer(a_playerId);
		if (entry == nullptr || entry->state == State::Dead) {
			return {};
		}
		std::vector<Event> events;
		if (entry->revivingPlayerId != 0) {
			events.push_back(Event{
				.kind = EventKind::ReviveCancelled,
				.playerId = a_playerId,
				.actorPlayerId = entry->revivingPlayerId,
				.reason = "adminKill"
			});
		}
		entry->state = State::Dead;
		entry->downedDeadlineSeconds = 0.0;
		EndChannel(*entry);
		events.push_back(Event{ .kind = EventKind::Died, .playerId = a_playerId });
		return events;
	}
}
