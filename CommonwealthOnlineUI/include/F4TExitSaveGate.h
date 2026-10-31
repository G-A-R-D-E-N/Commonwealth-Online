#pragma once

#include <cstdint>
#include <mutex>

namespace F4T::ExitSave
{
	inline constexpr std::uint64_t kTimeoutMs = 10000;
	inline constexpr std::uint64_t kPostSaveGraceMs = 1500;

	class Gate
	{
	public:
		void Arm(std::uint64_t a_nowMs)
		{
			const std::scoped_lock lock(_mutex);
			_armed = true;
			_postSaveSeen = false;
			_dueMs = a_nowMs + kTimeoutMs;
		}

		void OnPostSave(std::uint64_t a_nowMs)
		{
			const std::scoped_lock lock(_mutex);
			if (_armed && !_postSaveSeen) {
				_postSaveSeen = true;
				_dueMs = a_nowMs + kPostSaveGraceMs;
			}
		}

		[[nodiscard]] bool TakeDisconnect(std::uint64_t a_nowMs)
		{
			const std::scoped_lock lock(_mutex);
			if (!_armed || a_nowMs < _dueMs) {
				return false;
			}
			_armed = false;
			return true;
		}

		[[nodiscard]] bool PostSaveSeen()
		{
			const std::scoped_lock lock(_mutex);
			return _postSaveSeen;
		}

	private:
		std::mutex    _mutex;
		bool          _armed{ false };
		bool          _postSaveSeen{ false };
		std::uint64_t _dueMs{ 0 };
	};
}
