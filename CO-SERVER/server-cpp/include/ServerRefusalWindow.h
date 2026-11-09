#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CommonwealthOnline::Server::RuntimeSupport
{
	// constraint: one refusing peer must not be able to grow the log without bound.
	inline constexpr std::size_t kMaximumRefusalPeers = 256;
	inline constexpr double kRefusalWindowSeconds = 10.0;

	struct RefusalWindow
	{
		double openedAtMonotonic{};
		std::uint32_t suppressed{};
	};

	struct RefusalWindows
	{
		std::unordered_map<std::string, RefusalWindow> byPeer;
	};

	struct RefusalTally
	{
		std::string peer;
		std::uint32_t suppressed{};
	};

	struct RefusalReport
	{
		bool writeLine{};
		std::vector<RefusalTally> closed;
	};

	[[nodiscard]] inline std::vector<RefusalTally> CloseRefusalWindows(RefusalWindows& a_windows, double a_nowMonotonic)
	{
		std::vector<RefusalTally> closed;
		for (auto window = a_windows.byPeer.begin(); window != a_windows.byPeer.end();) {
			if (a_nowMonotonic - window->second.openedAtMonotonic >= kRefusalWindowSeconds) {
				closed.push_back({ window->first, window->second.suppressed });
				window = a_windows.byPeer.erase(window);
			} else {
				++window;
			}
		}
		return closed;
	}

	[[nodiscard]] inline RefusalReport NoteRefusal(
		RefusalWindows& a_windows,
		std::string_view a_peer,
		double a_nowMonotonic)
	{
		RefusalReport report;
		const auto found = a_windows.byPeer.find(std::string{ a_peer });
		if (found == a_windows.byPeer.end()) {
			if (a_windows.byPeer.size() >= kMaximumRefusalPeers) {
				report.closed = CloseRefusalWindows(a_windows, a_nowMonotonic);
				if (a_windows.byPeer.size() >= kMaximumRefusalPeers) {
					auto oldest = a_windows.byPeer.begin();
					for (auto window = a_windows.byPeer.begin(); window != a_windows.byPeer.end(); ++window) {
						if (window->second.openedAtMonotonic < oldest->second.openedAtMonotonic) {
							oldest = window;
						}
					}
					report.closed.push_back({ oldest->first, oldest->second.suppressed });
					a_windows.byPeer.erase(oldest);
				}
			}
			a_windows.byPeer.emplace(std::string{ a_peer }, RefusalWindow{ .openedAtMonotonic = a_nowMonotonic });
			report.writeLine = true;
			return report;
		}
		if (a_nowMonotonic - found->second.openedAtMonotonic >= kRefusalWindowSeconds) {
			report.closed.push_back({ found->first, found->second.suppressed });
			found->second = RefusalWindow{ .openedAtMonotonic = a_nowMonotonic };
			report.writeLine = true;
			return report;
		}
		++found->second.suppressed;
		return report;
	}
}
