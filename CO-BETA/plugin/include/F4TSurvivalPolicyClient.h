#pragma once

#include "F4TSurvivalPolicyState.h"

#include <cstdint>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

namespace F4T::SurvivalPolicy
{
	class ClientState
	{
	public:
		void Welcome(std::uint64_t a_connection, std::optional<bool> a_required)
		{
			const std::scoped_lock lock(_mutex);
			if (a_connection == 0 || a_connection < _connection) return;
			ClearReady();
			_connection = a_connection;
			_state.Reset();
			_state.Update(a_required);
		}

		void Update(std::uint64_t a_connection, std::optional<bool> a_required)
		{
			const std::scoped_lock lock(_mutex);
			if (a_connection != 0 && a_connection == _connection) {
				if (_state.Required() != a_required) ClearReady();
				_state.Update(a_required);
			}
		}

		std::optional<bool> Required(std::uint64_t a_connection)
		{
			const std::scoped_lock lock(_mutex);
			if (a_connection == 0 || a_connection != _connection) return std::nullopt;
			return _state.Required();
		}

		std::optional<std::uint64_t> Revision(std::uint64_t a_connection)
		{
			const std::scoped_lock lock(_mutex);
			if (_loading || a_connection == 0 || a_connection != _connection || !_state.Required()) return std::nullopt;
			return _state.Revision();
		}

		std::optional<std::uint64_t> ReadyRevision(std::uint64_t a_connection, std::uint64_t a_load)
		{
			const auto ready = _publishedReady.load(std::memory_order_acquire);
			if (!ready || a_connection == 0 || a_load == 0 || ready->connection != a_connection || ready->load != a_load) return std::nullopt;
			return ready->revision;
		}

		std::uint64_t BeginLoad()
		{
			const std::scoped_lock lock(_mutex);
			ClearReady();
			if (_loading && !_loadFailed && _loadEpoch != 0) _loadAmbiguous = true;
			_loadFailed = false;
			_loading = true;
			_state.Invalidate();
			return ++_loadEpoch;
		}

		void BeginTrackedLoad()
		{
			const std::scoped_lock lock(_mutex);
			_pendingLoad = BeginLoad();
		}

		std::uint64_t TakePendingLoad()
		{
			const std::scoped_lock lock(_mutex);
			const auto token = _pendingLoad;
			_pendingLoad = 0;
			return token;
		}

		void UntrackedLoad()
		{
			const std::scoped_lock lock(_mutex);
			ClearReady();
			_loading = true;
			_loadAmbiguous = true;
			_state.Invalidate();
		}

		bool CompleteLoad(std::uint64_t a_epoch, bool a_succeeded = true)
		{
			const std::scoped_lock lock(_mutex);
			if (!_loading || _loadAmbiguous || _loadFailed || a_epoch == 0 || a_epoch != _loadEpoch) return false;
			if (!a_succeeded) { ClearReady(); _loadFailed = true; return false; }
			ClearReady();
			_loading = false;
			_state.Invalidate();
			return true;
		}

		bool LoadAmbiguous()
		{
			const std::scoped_lock lock(_mutex);
			return _loadAmbiguous;
		}

		std::uint64_t LoadEpoch()
		{
			const std::scoped_lock lock(_mutex);
			return _loadEpoch;
		}

		template <typename Apply, typename Work>
		bool Enforce(std::uint64_t a_connection, std::uint64_t a_load, bool a_loaded, Apply&& a_apply, Work&& a_work)
		{
			const std::scoped_lock lock(_mutex);
			if (_loading || a_connection == 0 || a_connection != _connection || !a_loaded || a_load == 0) {
				if (_ready) { ClearReady(); _state.Invalidate(); }
				return false;
			}
			if (_state.Required() != false) {
				const auto ticket = _state.Prepare(a_connection, a_load, a_loaded);
				if (!ticket || !a_apply() || !_state.Complete(*ticket, a_connection, a_load, a_loaded, true)) {
					if (_ready) { ClearReady(); _state.Invalidate(); }
					return false;
				}
			} else if (!a_apply()) {
				if (_ready) { ClearReady(); _state.Invalidate(); }
				return false;
			}
			const auto revision = _state.Revision();
			const Ticket ticket{ a_connection, a_load, revision };
			if (!_ready || *_ready != ticket) {
				ClearReady();
				_publishedReady.store(std::make_shared<const Ticket>(ticket), std::memory_order_release);
				_ready = ticket;
			}
			a_work();
			if (_loading || _connection != a_connection || _state.Revision() != revision || !_ready || *_ready != ticket) { ClearReady(); return false; }
			return true;
		}

	private:
		void ClearReady()
		{
			_publishedReady.store({}, std::memory_order_release);
			_ready.reset();
		}

		std::recursive_mutex _mutex;
		std::atomic<std::shared_ptr<const Ticket>> _publishedReady;
		std::uint64_t _connection{};
		std::uint64_t _loadEpoch{};
		std::uint64_t _pendingLoad{};
		bool _loading{ true };
		bool _loadAmbiguous{};
		bool _loadFailed{};
		std::optional<Ticket> _ready;
		State _state;
	};

	inline ClientState g_clientState;
	inline std::atomic<bool> g_needsEnabled{};
	inline bool NeedsEnabled() noexcept { return g_needsEnabled.load(); }
}
