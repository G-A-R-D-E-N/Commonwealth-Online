#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace F4T::CombatTicketStore
{
	inline constexpr auto kRelaunchAmbiguity = std::chrono::seconds{ 2 };

	class ShotScope
	{
	public:
		explicit ShotScope(std::uint64_t a_generation) noexcept;
		~ShotScope();
		ShotScope(const ShotScope&) = delete;
		ShotScope& operator=(const ShotScope&) = delete;

		static ShotScope* Current() noexcept;
		std::uint64_t Generation() const noexcept { return _generation; }
		std::uint64_t Shot() const noexcept { return _shot; }
		void MarkLaunched() noexcept { _launched = true; }
		bool HasLaunch() const noexcept { return _launched; }

	private:
		static thread_local ShotScope* _current;
		ShotScope* _previous{};
		std::uint64_t _generation{};
		std::uint64_t _shot{};
		bool _launched{};
	};

	class Store
	{
	public:
		static constexpr std::size_t kCapacity = 256;

		bool SetSession(std::uint64_t a_generation);
		bool TrackShot(std::uint64_t a_generation, std::uint64_t a_shot);
		bool CommitShot(std::uint64_t a_generation, std::uint64_t a_shot, std::uint32_t a_sequence);
		std::uint32_t Sequence(std::uint64_t a_generation, std::uint64_t a_shot);
		bool RecordLaunch(std::uint64_t a_generation, std::uint64_t a_shot, std::uint32_t a_handle, std::chrono::steady_clock::time_point a_at);
		std::uint64_t ShotForHit(std::uint64_t a_generation, std::uint32_t a_handle, std::chrono::steady_clock::time_point a_hitAt);

	private:
		struct ShotEntry
		{
			std::uint64_t shot{};
			std::uint64_t generation{};
			std::uint32_t sequence{};
		};

		struct LaunchEntry
		{
			std::uint64_t generation{};
			std::uint64_t shot{};
			std::uint32_t handle{};
			std::chrono::steady_clock::time_point at{};
		};

		std::mutex _mutex;
		std::array<ShotEntry, kCapacity> _shots{};
		std::array<LaunchEntry, kCapacity> _launches{};
		std::size_t _nextLaunch{};
		std::uint64_t _activeGeneration{};
		std::uint64_t _highestGeneration{};
	};
}
