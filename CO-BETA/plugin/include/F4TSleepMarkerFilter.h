#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <memory>
#include <unordered_map>
#include <algorithm>

namespace F4T::SleepMarker
{
	using Registry = std::unordered_map<const void*, std::shared_ptr<const void>>;
	inline Registry::node_type ExtractRegistration(Registry& a_registry, const std::shared_ptr<const void>& a_token)
	{
		if (!a_token) return {};
		const auto found = std::find_if(a_registry.begin(), a_registry.end(),
			[&](const auto& a_entry) { return a_entry.second == a_token; });
		return found != a_registry.end() ? a_registry.extract(found) : Registry::node_type{};
	}

	struct Selection
	{
		const void* actor{};
		const void* bed{};
		std::uint32_t marker{};

		bool Reject(const void* a_actor, const void* a_bed, std::uint32_t a_marker) const
		{
			return actor && bed && actor == a_actor && bed == a_bed && marker != a_marker;
		}
	};

	class CollectionScope
	{
	public:
		CollectionScope(Selection& a_current, Selection a_next) : current(a_current), previous(a_current) { current = a_next; }
		~CollectionScope() { current = previous; }
		CollectionScope(const CollectionScope&) = delete;
		CollectionScope& operator=(const CollectionScope&) = delete;
	private:
		Selection& current;
		Selection previous;
	};

	inline bool MatchesCall(std::span<const std::uint8_t> a_bytes, std::uintptr_t a_site, std::uintptr_t a_target)
	{
		if (a_bytes.size() < 5 || a_bytes[0] != 0xE8) return false;
		std::int32_t displacement{};
		std::memcpy(&displacement, a_bytes.data() + 1, sizeof(displacement));
		return a_site + 5 + static_cast<std::intptr_t>(displacement) == a_target;
	}
}
