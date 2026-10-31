#pragma once

#include "F4TObjectWire.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace F4T::ObjectState
{
	inline constexpr std::size_t kMaximumObjects = 4096;
	inline constexpr std::size_t kMaximumObjectsPerPlayer = 256;
	inline constexpr std::size_t kMaximumReplayEntries = 4096;
	inline constexpr std::size_t kMaximumApplyGuardEntries = 64;
	inline constexpr double kMaximumPositionDrift = 64.0;
	inline constexpr double kApplyGuardLifetimeSeconds = 5.0;

	enum class Outcome : std::uint8_t
	{
		Applied,
		RejectedCapacity,
		RejectedUnknownObject,
		RejectedKindMismatch,
		RejectedPositionDrift,
		RejectedActionNotApplicable,
		RejectedOperationConflict,
		RejectedPlayerCapacity
	};

	struct IntentResult
	{
		Outcome outcome{ Outcome::RejectedCapacity };
		bool replayed{};
		F4T::ObjectWire::ObjectState state;
	};

	class ObjectStateTable
	{
	public:
		explicit ObjectStateTable(std::size_t a_maximumObjectsPerPlayer = kMaximumObjects);

		[[nodiscard]] IntentResult ApplyActivate(
			std::uint32_t a_playerId,
			const std::string& a_operationId,
			const F4T::ObjectWire::ObjectKey& a_object,
			F4T::ObjectWire::Kind a_kind,
			F4T::ObjectWire::Action a_action,
			const F4T::ObjectWire::ObjectPosition& a_position);

		[[nodiscard]] IntentResult ApplyLock(
			std::uint32_t a_playerId,
			const std::string& a_operationId,
			const F4T::ObjectWire::ObjectKey& a_object,
			bool a_locked,
			std::optional<std::uint32_t> a_lockLevel,
			const F4T::ObjectWire::ObjectPosition& a_position);

		[[nodiscard]] const F4T::ObjectWire::ObjectState* Find(const F4T::ObjectWire::ObjectKey& a_object) const noexcept;
		[[nodiscard]] bool IsEmpty() const noexcept;
		[[nodiscard]] std::size_t Size() const noexcept;
		[[nodiscard]] std::vector<F4T::ObjectWire::ObjectState> Snapshot(std::size_t a_maximum) const;

	private:
		enum class Request : std::uint8_t
		{
			Activate,
			Lock
		};

		struct ReplayRecord
		{
			std::uint32_t playerId{};
			std::string operationId;
			Request request{ Request::Activate };
			F4T::ObjectWire::ObjectKey object;
			F4T::ObjectWire::Action action{ F4T::ObjectWire::Action::Open };
			bool locked{};
			std::optional<std::uint32_t> lockLevel;
			IntentResult result;
			std::uint64_t sequence{};
		};

		using ReplayKey = std::pair<std::uint32_t, std::string>;

		[[nodiscard]] std::optional<IntentResult> LookUpReplay(const ReplayRecord& a_probe) const;
		void Remember(ReplayRecord a_record);

		std::size_t _maximumObjectsPerPlayer{ kMaximumObjects };
		std::map<std::uint32_t, std::size_t> _objectsCreatedByPlayer;
		std::map<F4T::ObjectWire::ObjectKey, F4T::ObjectWire::ObjectState> _objects;
		std::map<ReplayKey, ReplayRecord> _replays;
		std::deque<std::pair<std::uint64_t, ReplayKey>> _replayOrder;
		std::uint64_t _nextSequence{ 1 };
	};

	class ApplyGuard
	{
	public:
		[[nodiscard]] bool Record(const F4T::ObjectWire::ObjectState& a_expected, double a_nowSeconds);
		[[nodiscard]] bool Consume(const F4T::ObjectWire::ObjectState& a_observed, double a_nowSeconds);
		void Expire(double a_nowSeconds) noexcept;
		[[nodiscard]] std::size_t Size() const noexcept;

	private:
		struct Entry
		{
			F4T::ObjectWire::ObjectState expected;
			double expiresAtSeconds{};
		};

		std::deque<Entry> _entries;
	};
}
