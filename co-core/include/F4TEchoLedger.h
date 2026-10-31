#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace F4T
{
	inline constexpr std::size_t MaximumEchoEntries = 256;
	inline constexpr std::uint64_t EchoLifetimeMilliseconds = 2000;

	class EchoLedger
	{
	public:
		void Expect(std::uint32_t a_containerId, std::uint32_t a_formId, std::uint32_t a_count, std::uint64_t a_nowMs);
		[[nodiscard]] bool Consume(std::uint32_t a_containerId, std::uint32_t a_formId, std::uint32_t a_count, std::uint64_t a_nowMs);
		void Expire(std::uint64_t a_nowMs);

	private:
		struct Entry
		{
			std::uint32_t containerId{};
			std::uint32_t formId{};
			std::uint32_t count{};
			std::uint64_t timestampMs{};
		};

		std::vector<Entry> _entries;
	};

	inline void EchoLedger::Expect(std::uint32_t a_containerId, std::uint32_t a_formId, std::uint32_t a_count, std::uint64_t a_nowMs)
	{
		if (a_count == 0) {
			return;
		}
		if (_entries.size() >= MaximumEchoEntries) {
			_entries.erase(_entries.begin());
		}
		_entries.push_back(Entry{ .containerId = a_containerId, .formId = a_formId, .count = a_count, .timestampMs = a_nowMs });
	}

	inline bool EchoLedger::Consume(std::uint32_t a_containerId, std::uint32_t a_formId, std::uint32_t a_count, std::uint64_t a_nowMs)
	{
		if (a_count == 0) {
			return false;
		}
		Expire(a_nowMs);
		std::uint32_t remaining = a_count;
		for (auto it = _entries.begin(); it != _entries.end() && remaining > 0;) {
			if (it->containerId == a_containerId && it->formId == a_formId) {
				const auto covered = (std::min)(it->count, remaining);
				it->count -= covered;
				remaining -= covered;
				if (it->count == 0) {
					it = _entries.erase(it);
					continue;
				}
			}
			++it;
		}
		return remaining == 0;
	}

	inline void EchoLedger::Expire(std::uint64_t a_nowMs)
	{
		std::erase_if(_entries, [a_nowMs](const Entry& a_entry) {
			return a_nowMs - a_entry.timestampMs > EchoLifetimeMilliseconds;
		});
	}
}
