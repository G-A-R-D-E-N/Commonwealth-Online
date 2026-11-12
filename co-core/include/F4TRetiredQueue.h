#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <optional>
#include <utility>

namespace F4T
{
	template <typename Key, typename Value, std::size_t Capacity>
	class RetiredQueue
	{
		static_assert(Capacity > 0);

	public:
		void Push(const Key& a_key, Value a_value)
		{
			std::erase_if(m_entries, [&](const Entry& a_entry) { return a_entry.key == a_key; });
			if (m_entries.size() >= Capacity) {
				m_entries.pop_front();
			}
			m_entries.push_back(Entry{ a_key, std::move(a_value) });
		}

		[[nodiscard]] std::optional<Value> Take(const Key& a_key)
		{
			const auto found = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& a_entry) { return a_entry.key == a_key; });
			if (found == m_entries.end()) {
				return std::nullopt;
			}
			Value value = std::move(found->value);
			m_entries.erase(found);
			return value;
		}

		void Clear()
		{
			m_entries.clear();
		}

	private:
		struct Entry
		{
			Key key{};
			Value value{};
		};

		std::deque<Entry> m_entries;
	};
}
