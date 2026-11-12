#pragma once

#include <cstdint>
#include <optional>

namespace F4T::SurvivalPolicy
{
	struct Ticket
	{
		std::uint64_t connection{};
		std::uint64_t load{};
		std::uint64_t revision{};
		bool operator==(const Ticket&) const = default;
	};

	class State
	{
	public:
		void Reset() noexcept
		{
			_required.reset();
			_applied.reset();
			++_revision;
		}

		void Update(std::optional<bool> a_required) noexcept
		{
			if (_required != a_required) {
				_required = a_required;
				_applied.reset();
				++_revision;
			}
		}

		std::optional<bool> Required() const noexcept { return _required; }
		std::uint64_t Revision() const noexcept { return _revision; }
		void Invalidate() noexcept { _applied.reset(); ++_revision; }

		std::optional<Ticket> Prepare(std::uint64_t a_connection, std::uint64_t a_load, bool a_loaded) const noexcept
		{
			if (!_required.value_or(false) || !a_loaded || a_connection == 0 || a_load == 0) return std::nullopt;
			return Ticket{ a_connection, a_load, _revision };
		}

		bool Current(const Ticket& a_ticket, std::uint64_t a_connection, std::uint64_t a_load, bool a_loaded) const noexcept
		{
			return a_loaded && a_connection != 0 && a_load != 0 && _required.value_or(false) &&
				a_ticket == Ticket{ a_connection, a_load, _revision };
		}

		bool Complete(const Ticket& a_ticket, std::uint64_t a_connection, std::uint64_t a_load,
			bool a_loaded, bool a_applied) noexcept
		{
			if (!a_applied || !Current(a_ticket, a_connection, a_load, a_loaded)) return false;
			_applied = a_ticket;
			return true;
		}

		bool AllowsGameplay(std::uint64_t a_connection, std::uint64_t a_load, bool a_loaded) const noexcept
		{
			if (!a_loaded || a_connection == 0 || a_load == 0 || !_required) return false;
			if (!*_required) return true;
			return _applied && Current(*_applied, a_connection, a_load, a_loaded);
		}

	private:
		std::optional<bool> _required;
		std::optional<Ticket> _applied;
		std::uint64_t _revision{};
	};
}
