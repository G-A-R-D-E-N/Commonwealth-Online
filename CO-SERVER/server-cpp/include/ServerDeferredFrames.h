#pragma once

#include "ServerProtocol.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server
{
	class DurableStateFileStore;

	inline constexpr double kHeldReplyLifetimeSeconds = 10.0;

	struct DeferredFrameEntry
	{
		OutboundFrame frame;
		bool identityReady{};
		std::uint64_t ticket{};
		std::uint64_t ticketBefore{};
		double expiresAtMonotonic{};
	};

	struct DeferredConnectionFrames
	{
		std::uint32_t connectionId{};
		std::vector<DeferredFrameEntry> frames;
	};

	enum class DeferredSweepVerdict : std::uint8_t
	{
		hold,
		deliver,
		abandon
	};

	// constraint: only the head frame decides, and a quarantined section abandons without waiting.
	[[nodiscard]] inline DeferredSweepVerdict DeferredVerdict(
		bool a_headDurable,
		bool a_headQuarantined,
		double a_nowMonotonic,
		double a_headExpiresAtMonotonic) noexcept
	{
		if (a_headQuarantined) {
			return DeferredSweepVerdict::abandon;
		}
		if (a_headDurable) {
			return DeferredSweepVerdict::deliver;
		}
		return a_nowMonotonic >= a_headExpiresAtMonotonic ? DeferredSweepVerdict::abandon : DeferredSweepVerdict::hold;
	}

	struct DeferredSweepResult
	{
		std::vector<DeferredFrameEntry> delivered;
		std::vector<DeferredFrameEntry> abandoned;
	};

	template <typename Durable, typename Quarantined>
	[[nodiscard]] inline DeferredSweepResult SweepDeferredFrames(
		DeferredConnectionFrames& a_entry,
		double a_nowMonotonic,
		Durable&& a_durable,
		Quarantined&& a_quarantined)
	{
		DeferredSweepResult result;
		while (!a_entry.frames.empty()) {
			const auto& head = a_entry.frames.front();
			const bool durable = head.ticket == 0 || a_durable(head.ticket);
			const bool quarantined = head.ticket != 0 && a_quarantined(head.ticketBefore, head.ticket);
			if (DeferredVerdict(durable, quarantined, a_nowMonotonic, head.expiresAtMonotonic) != DeferredSweepVerdict::deliver) {
				break;
			}
			result.delivered.push_back(std::move(a_entry.frames.front()));
			a_entry.frames.erase(a_entry.frames.begin());
		}
		if (!a_entry.frames.empty() &&
			DeferredVerdict(false, a_entry.frames.front().ticket != 0 && a_quarantined(a_entry.frames.front().ticketBefore, a_entry.frames.front().ticket), a_nowMonotonic, a_entry.frames.front().expiresAtMonotonic) ==
				DeferredSweepVerdict::abandon) {
			for (auto& pending : a_entry.frames) {
				result.abandoned.push_back(std::move(pending));
			}
			a_entry.frames.clear();
		}
		return result;
	}

	class DeferredFrameQueue final
	{
	public:
		void Park(
			std::uint32_t a_connectionId,
			OutboundFrame a_frame,
			bool a_identityReady,
			std::uint64_t a_ticket,
			double a_expiresAtMonotonic,
			std::uint64_t a_ticketBefore = 0)
		{
			DeferredFrameEntry entry{
				.frame = std::move(a_frame),
				.identityReady = a_identityReady,
				.ticket = a_ticket,
				.ticketBefore = a_ticketBefore,
				.expiresAtMonotonic = a_expiresAtMonotonic
			};
			for (auto& existing : _entries) {
				if (existing.connectionId == a_connectionId) {
					existing.frames.push_back(std::move(entry));
					return;
				}
			}
			DeferredConnectionFrames connection{ .connectionId = a_connectionId };
			connection.frames.push_back(std::move(entry));
			_entries.push_back(std::move(connection));
		}

		[[nodiscard]] bool Blocked(std::uint32_t a_connectionId) const
		{
			for (const auto& entry : _entries) {
				if (entry.connectionId == a_connectionId) {
					return true;
				}
			}
			return false;
		}

		void Erase(std::uint32_t a_connectionId)
		{
			for (auto entry = _entries.begin(); entry != _entries.end();) {
				if (entry->connectionId == a_connectionId) {
					entry = _entries.erase(entry);
				} else {
					++entry;
				}
			}
		}

		[[nodiscard]] std::vector<DeferredConnectionFrames>& Entries() noexcept { return _entries; }

		[[nodiscard]] bool Empty() const noexcept { return _entries.empty(); }

	private:
		std::vector<DeferredConnectionFrames> _entries;
	};

	struct DeferredFramePolicy
	{
		DeferredFrameQueue* queue{};
		DurableStateFileStore* store{};
		double expiresAtMonotonic{};
		std::uint32_t gateConnectionId{};
		std::uint64_t ticketBefore{};
		std::vector<std::pair<std::uint32_t, std::uint32_t>>* sessionReadyDelivered{};
	};
}
