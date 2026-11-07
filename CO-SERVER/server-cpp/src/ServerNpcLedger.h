#pragma once

#include "F4TNpcAuthority.h"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace CommonwealthOnline::Server::NpcLedger
{
	using Scope = F4T::NpcAuthority::Scope;

	inline std::string LedgerSourceKey(std::uint32_t a_authorityEpoch, std::uint32_t a_npcId, const std::string& a_sourceFormId)
	{
		if (a_sourceFormId != "00000000") {
			return a_sourceFormId;
		}
		return "R" + std::to_string(a_authorityEpoch) + ":" + std::to_string(a_npcId);
	}

	class Ledger
	{
	public:
		struct NpcObservation
		{
			std::uint32_t npcId{};
			std::string sourceFormId;
			double x{};
			double y{};
			double z{};
			bool dead{};
		};

		void ObserveSnapshot(const Scope& a_scope, std::uint32_t a_epoch, std::span<const NpcObservation> a_npcs, double a_monotonicSeconds);
		[[nodiscard]] bool Quarantined(const Scope& a_scope, const std::string& a_sourceFormId, double a_monotonicSeconds) const;
		[[nodiscard]] bool KillClaimed(std::string_view a_claimKey, const Scope& a_scope, const std::string& a_sourceFormId, double a_monotonicSeconds) const;
		void RecordKill(std::string_view a_claimKey, const Scope& a_scope, const std::string& a_sourceFormId, double a_monotonicSeconds);
		void ForgetScope(const Scope& a_scope);

	private:
		using NpcKey = std::pair<Scope, std::string>;
		using KillKey = std::pair<std::string, NpcKey>;

		struct Tombstone
		{
			std::optional<double> deadSince;
			double quarantinedUntil{};
			double touched{};
		};

		struct Position
		{
			std::string sourceFormId;
			double x{};
			double y{};
			double z{};
		};

		struct Continuity
		{
			std::uint32_t epoch{};
			double touched{};
			std::map<std::uint32_t, Position> npcs;
		};

		struct KillClaim
		{
			double touched{};
		};

		void Quarantine(const NpcKey& a_key, double a_untilMonotonicSeconds, double a_monotonicSeconds);

		std::map<NpcKey, Tombstone> tombstones;
		std::map<Scope, Continuity> continuity;
		std::map<KillKey, KillClaim> kills;
	};
}
