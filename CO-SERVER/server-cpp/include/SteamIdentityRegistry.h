#pragma once

#include "SteamIdentityLink.h"

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
	struct SteamIdentityRecord
	{
		std::string profileId;
		std::string steamId;
		SteamIdSource source{ SteamIdSource::None };
		double firstSeen{};
		double lastSeen{};
		std::vector<std::string> nicknames;
	};

	enum class SteamIdentityFlagKind : std::uint8_t
	{
		SharedSteamId,
		MultipleSteamIds
	};

	[[nodiscard]] std::string_view SteamIdentityFlagKindName(SteamIdentityFlagKind a_kind) noexcept;

	struct SteamIdentityFlag
	{
		SteamIdentityFlagKind kind{ SteamIdentityFlagKind::SharedSteamId };
		std::string profileId;
		std::string steamId;
		std::string otherId;
		double raisedAt{};
	};

	struct SteamIdentityObservation
	{
		bool stored{};
		bool newLink{};
		std::vector<SteamIdentityFlag> raised;
	};

	// Verified Steam ID history per profile; a claim alone never reaches it.
	class SteamIdentityRegistry
	{
	public:
		static constexpr double kRetentionSeconds = 365.0 * 24.0 * 60.0 * 60.0;
		static constexpr std::size_t kMaximumNicknames = 10;
		static constexpr std::size_t kMaximumNicknameLength = 64;
		static constexpr std::size_t kMaximumRecords = 100000;
		static constexpr std::size_t kMaximumFlags = 10000;

		SteamIdentityRegistry() = default;
		explicit SteamIdentityRegistry(const nlohmann::json& a_stored);

		[[nodiscard]] SteamIdentityObservation Observe(
			std::string_view a_profileId,
			std::string_view a_steamId,
			SteamIdSource a_source,
			std::string_view a_nickname,
			double a_now);

		[[nodiscard]] std::vector<SteamIdentityRecord> ForSteamId(std::string_view a_steamId) const;
		[[nodiscard]] std::vector<SteamIdentityRecord> ForProfile(std::string_view a_profileId) const;
		[[nodiscard]] const std::vector<SteamIdentityFlag>& Flags() const noexcept { return _flags; }
		[[nodiscard]] std::size_t RecordCount() const noexcept { return _records.size(); }
		[[nodiscard]] bool IsFlagged(const SteamIdentityRecord& a_record) const;

		[[nodiscard]] std::size_t ClearFlags(std::string_view a_profileId, std::string_view a_steamId);

		[[nodiscard]] std::size_t Sweep(double a_now, const std::function<bool(const SteamIdentityRecord&)>& a_keep);

		[[nodiscard]] nlohmann::json ToJson() const;
		[[nodiscard]] static nlohmann::json RecordJson(const SteamIdentityRecord& a_record);
		[[nodiscard]] static nlohmann::json FlagJson(const SteamIdentityFlag& a_flag);

	private:
		bool RaiseFlag(SteamIdentityFlag a_flag, SteamIdentityObservation& a_observation);

		std::vector<SteamIdentityRecord> _records;
		std::vector<SteamIdentityFlag> _flags;
	};
}
