#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace F4T::NpcAuthority
{
	inline constexpr std::size_t MaximumTrackedEpochScopes = 8192;

	struct Scope
	{
		std::string cellId;
		std::string worldspaceId;

		auto operator<=>(const Scope&) const = default;
	};

	struct PlayerScope
	{
		std::uint32_t playerId{};
		Scope scope;
	};

	struct Assignment
	{
		Scope scope;
		std::uint32_t playerId{};
		std::uint32_t epoch{};
	};

	struct Change
	{
		Scope scope;
		std::uint32_t previousPlayerId{};
		std::uint32_t playerId{};
		std::uint32_t epoch{};
	};

	class Manager
	{
	public:
		void Clear() noexcept;
		[[nodiscard]] std::optional<Assignment> Get(const Scope& a_scope) const;
		[[nodiscard]] std::vector<Assignment> Assignments() const;
		[[nodiscard]] bool Authorize(std::uint32_t a_playerId, const Scope& a_scope, std::uint32_t a_epoch) const;
		[[nodiscard]] std::vector<Change> Reconcile(std::span<const PlayerScope> a_players);

		[[nodiscard]] std::size_t TrackedEpochScopes() const noexcept;

	private:
		struct EpochEntry
		{
			Scope scope;
			std::uint32_t epoch{};
			std::uint64_t lastTouched{};
		};

		void ForgetColdEpochs();

		std::vector<Assignment> _assignments;
		std::vector<EpochEntry> _lastEpochs;
		std::uint64_t _touchCounter{};
	};
}
