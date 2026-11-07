#pragma once

#include "F4TChatFactionTag.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::OverheadNamePolicy
{
	struct Settings
	{
		bool showNameplates{ true };
		float fadeStartDistance{ 1200.0f };
		float maxDistance{ 2000.0f };
		std::size_t maxLabels{ 8 };
	};

	struct Candidate
	{
		std::uint32_t playerId{};
		float distance{};
		std::string displayName;
		std::string factionTag;
		bool downed{};
		bool sleeping{};
	};

	struct Label
	{
		std::uint32_t playerId{};
		std::string text;
		float opacity{};
	};

	[[nodiscard]] inline std::string FormatLabel(std::string_view a_factionTag, std::string_view a_name)
	{
		if (a_name.empty()) {
			return {};
		}
		const auto tag = ChatFactionTag::Sanitize(a_factionTag);
		if (tag.empty()) {
			return std::string{ a_name };
		}
		return "[" + tag + "] " + std::string{ a_name };
	}

	[[nodiscard]] inline float OpacityFor(float a_distance, const Settings& a_settings)
	{
		if (!(a_distance >= 0.0f) || a_distance >= a_settings.maxDistance) {
			return 0.0f;
		}
		if (a_distance <= a_settings.fadeStartDistance) {
			return 1.0f;
		}
		const float span = a_settings.maxDistance - a_settings.fadeStartDistance;
		if (!(span > 0.0f)) {
			return 0.0f;
		}
		return 1.0f - (a_distance - a_settings.fadeStartDistance) / span;
	}

	[[nodiscard]] inline std::vector<Label> Select(std::vector<Candidate> a_candidates, const Settings& a_settings)
	{
		std::vector<Label> labels;
		if (!a_settings.showNameplates || a_settings.maxLabels == 0) {
			return labels;
		}
		std::erase_if(a_candidates, [&a_settings](const Candidate& a_candidate) {
			return a_candidate.downed || a_candidate.sleeping || a_candidate.displayName.empty() ||
				!(OpacityFor(a_candidate.distance, a_settings) > 0.0f);
		});
		std::sort(a_candidates.begin(), a_candidates.end(), [](const Candidate& a_left, const Candidate& a_right) {
			if (a_left.distance != a_right.distance) {
				return a_left.distance < a_right.distance;
			}
			return a_left.playerId < a_right.playerId;
		});
		for (const auto& candidate : a_candidates) {
			if (labels.size() >= a_settings.maxLabels) {
				break;
			}
			labels.push_back(Label{
				candidate.playerId,
				FormatLabel(candidate.factionTag, candidate.displayName),
				OpacityFor(candidate.distance, a_settings) });
		}
		return labels;
	}
}
