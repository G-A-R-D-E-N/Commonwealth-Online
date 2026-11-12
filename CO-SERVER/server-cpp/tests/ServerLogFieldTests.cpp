#include "ServerLogSanitizer.h"
#include "ServerRefusalWindow.h"

#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using CommonwealthOnline::Server::RuntimeSupport::CloseRefusalWindows;
	using CommonwealthOnline::Server::RuntimeSupport::kMaximumRefusalPeers;
	using CommonwealthOnline::Server::RuntimeSupport::kRefusalWindowSeconds;
	using CommonwealthOnline::Server::RuntimeSupport::NoteRefusal;
	using CommonwealthOnline::Server::RuntimeSupport::RefusalWindows;

	void FirstRefusalIsPrintedAndLaterOnesAreCounted()
	{
		RefusalWindows windows;
		const auto first = NoteRefusal(windows, "203.0.113.9", 100.0);
		assert(first.writeLine);
		assert(first.closed.empty());
		for (int repeat = 0; repeat < 4; ++repeat) {
			const auto later = NoteRefusal(windows, "203.0.113.9", 100.5 + repeat);
			assert(!later.writeLine);
			assert(later.closed.empty());
		}
		// a peer that stops refusing must still have its count printed when the window closes
		const auto closed = CloseRefusalWindows(windows, 100.0 + kRefusalWindowSeconds);
		assert(closed.size() == 1);
		assert(closed.front().peer == "203.0.113.9");
		assert(closed.front().suppressed == 4);
		assert(windows.byPeer.empty());
	}

	void ARefusalWindowReopensAfterItsWindow()
	{
		RefusalWindows windows;
		assert(NoteRefusal(windows, "peer", 300.0).writeLine);
		for (int repeat = 0; repeat < 3; ++repeat) {
			assert(!NoteRefusal(windows, "peer", 301.0).writeLine);
		}
		const auto reopened = NoteRefusal(windows, "peer", 300.0 + kRefusalWindowSeconds);
		assert(reopened.writeLine);
		assert(reopened.closed.size() == 1);
		assert(reopened.closed.front().suppressed == 3);
		assert(windows.byPeer.at("peer").suppressed == 0);
	}

	void RefusalWindowsNeverGrowPastThePeerLimit()
	{
		RefusalWindows windows;
		for (std::size_t peer = 0; peer < kMaximumRefusalPeers; ++peer) {
			assert(NoteRefusal(windows, "10.0.0." + std::to_string(peer), 200.0).writeLine);
		}
		assert(windows.byPeer.size() == kMaximumRefusalPeers);
		const auto overflow = NoteRefusal(windows, "10.1.0.1", 200.5);
		assert(overflow.writeLine);
		assert(windows.byPeer.size() == kMaximumRefusalPeers);
		assert(windows.byPeer.contains("10.1.0.1"));
	}
}

int main()
{
	using CommonwealthOnline::Server::RuntimeSupport::SanitizeLogField;

	assert(SanitizeLogField("") == "<none>");
	assert(SanitizeLogField("Banned: repeat offence") == "Banned: repeat offence");

	const std::string forged = "peer\nConnection 7 ended by server: player=Admin";
	const auto line = SanitizeLogField(forged);
	assert(line.find('\n') == std::string::npos);
	assert(line == "peer Connection 7 ended by server: player=Admin");

	assert(SanitizeLogField("tab\there\r\nnext").find_first_of("\t\r\n") == std::string::npos);
	assert(SanitizeLogField(std::string{ "delete\x7Fhere" }) == "delete here");
	assert(SanitizeLogField(std::string{ "J\xC3\xBC" "rgen" }).size() == 7);

	FirstRefusalIsPrintedAndLaterOnesAreCounted();
	ARefusalWindowReopensAfterItsWindow();
	RefusalWindowsNeverGrowPastThePeerLimit();

	return 0;
}
