#include "F4TQuestNotices.h"

#include "F4TQuestProtocol.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>

namespace
{
	using F4T::QuestNotices::kMaximumNotices;
	using F4T::QuestNotices::Clear;
	using F4T::QuestNotices::FormatNotReachable;
	using F4T::QuestNotices::FormatCompletion;
	using F4T::QuestNotices::FormatReadyExhausted;
	using F4T::QuestNotices::Push;
	using F4T::QuestNotices::Revision;
	using F4T::QuestNotices::Snapshot;

	void TestFormatter()
	{
		assert(FormatCompletion("Out of Time", "Ana") == "Ana completed Out of Time");
		assert(FormatCompletion("Out of Time", "You") == "You completed Out of Time");
		assert(FormatCompletion("Out of Time", "") == "Out of Time completed");
	}

	void TestNotReachableFormatter()
	{
		using F4T::QuestProtocol::kNotReachableNotStartable;
		using F4T::QuestProtocol::kNotReachablePrerequisite;
		using F4T::QuestProtocol::kNotReachableUnknown;
		assert(
			FormatNotReachable(kNotReachablePrerequisite, "Out of Time") ==
			"That quest isn't available yet — finish Out of Time first");
		assert(
			FormatNotReachable(kNotReachablePrerequisite, "") ==
			"That quest isn't available yet — finish the previous story quest first");
		assert(
			FormatNotReachable(kNotReachableUnknown, "Out of Time") == "That quest isn't part of the synced story.");
		assert(FormatNotReachable(kNotReachableNotStartable, "") == "That quest can't be started yet.");
		assert(FormatNotReachable("stage_regression", "") == "The server refused that quest update.");
		assert(FormatNotReachable("bogus", "") == "The server refused that quest update.");
	}

	void TestReadyExhaustedFormatter()
	{
		assert(
			FormatReadyExhausted() ==
			"Quest sync stopped: the server kept refusing. Reconnect to retry.");
		Clear();
		Push(FormatReadyExhausted(), "12:03");
		assert(Snapshot().size() == 1);
		assert(Snapshot().front().text == FormatReadyExhausted());
	}

	void TestRingEvictsOldestNewestFirst()
	{
		Clear();
		for (std::size_t index = 0; index < kMaximumNotices + 4; ++index) {
			Push("n" + std::to_string(index), "12:00");
		}
		const auto notices = Snapshot();
		assert(notices.size() == kMaximumNotices);
		assert(notices.front().text == "n19");
		assert(notices.back().text == "n4");
		assert(notices.front().time == "12:00");
	}

	void TestRevisionIncrementsOnPushAndClear()
	{
		Clear();
		const auto cleared = Revision();
		Push("a", "12:01");
		const auto pushed = Revision();
		assert(pushed > cleared);
		Clear();
		assert(Revision() > pushed);
	}

	void TestClearEmpties()
	{
		Push("a", "12:02");
		Clear();
		assert(Snapshot().empty());
	}
}

int main()
{
	TestFormatter();
	TestNotReachableFormatter();
	TestReadyExhaustedFormatter();
	TestRingEvictsOldestNewestFirst();
	TestRevisionIncrementsOnPushAndClear();
	TestClearEmpties();
	return 0;
}
