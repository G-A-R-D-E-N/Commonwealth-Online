#include "F4TCrashReport.h"
#include "F4THookThreadTable.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#define CHECK(expr)                                                                 \
	do {                                                                            \
		if (!(expr)) {                                                              \
			std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expr); \
			std::abort();                                                           \
		}                                                                           \
	} while (false)

namespace
{
	F4T::HookThread::Row Find(std::uint16_t a_hookId, std::uint32_t a_threadId)
	{
		F4T::HookThread::Row rows[F4T::HookThread::kCapacity]{};
		const auto count = F4T::HookThread::Snapshot(rows, F4T::HookThread::kCapacity);
		for (std::size_t index = 0; index < count; ++index) {
			if (rows[index].hookId == a_hookId && rows[index].threadId == a_threadId) {
				return rows[index];
			}
		}
		return {};
	}
}

int main()
{
	using F4T::HookThread::Id;
	F4T::HookThread::Reset();

	const auto mainThread = F4T::HookThread::CurrentThreadId();
	F4T::HookThread::Note(Id::ExecuteCommand);
	F4T::HookThread::Note(Id::ExecuteCommand);
	F4T::HookThread::Note(Id::CompileAndRun);

	std::uint32_t otherThread = 0;
	std::thread worker([&otherThread] {
		otherThread = F4T::HookThread::CurrentThreadId();
		F4T::HookThread::Note(Id::ExecuteCommand);
	});
	worker.join();
	CHECK(otherThread != 0);
	CHECK(otherThread != mainThread);

	const auto execute = Find(static_cast<std::uint16_t>(Id::ExecuteCommand), mainThread);
	const auto compile = Find(static_cast<std::uint16_t>(Id::CompileAndRun), mainThread);
	const auto other = Find(static_cast<std::uint16_t>(Id::ExecuteCommand), otherThread);
	CHECK(execute.count == 2);
	CHECK(compile.count == 1);
	CHECK(other.count == 1);

	char report[512]{};
	CHECK(F4T::HookThread::TakeReport(1000, 60000, report, sizeof(report)));
	CHECK(std::strstr(report, "hook 1 thread ") != nullptr);
	CHECK(std::strstr(report, "hook 2 thread ") != nullptr);
	CHECK(!F4T::HookThread::TakeReport(2000, 60000, report, sizeof(report)));
	CHECK(F4T::HookThread::TakeReport(61000, 60000, report, sizeof(report)));
	CHECK(std::strstr(report, "hook 1 thread ") != nullptr);
	CHECK(!F4T::HookThread::TakeReport(61000, F4T::HookThread::kReportRepeatIntervalMs, report, sizeof(report)));

	char crash[8192]{};
	F4T::CrashReport::Data data{};
	data.year = 2026;
	data.month = 10;
	data.day = 25;
	const auto crashLength = F4T::CrashReport::Format(data, crash, sizeof(crash));
	CHECK(crashLength > 0);
	CHECK(std::strstr(crash, "[CO CRASH] hook=1 thread=") != nullptr);
	CHECK(std::strstr(crash, "[CO CRASH] ===== end crash report =====\r\n") != nullptr);
	const auto end = std::strstr(crash, "[CO CRASH] ===== end crash report =====\r\n");
	const auto hookLine = std::strstr(crash, "[CO CRASH] hook=1 thread=");
	CHECK(hookLine != nullptr && end != nullptr && hookLine < end);

	F4T::HookThread::Reset();
	for (std::uint32_t threadId = 1; threadId <= F4T::HookThread::kCapacity; ++threadId) {
		F4T::HookThread::NoteOnThread(Id::DropObject, threadId);
	}
	F4T::HookThread::NoteOnThread(Id::DropObject, static_cast<std::uint32_t>(F4T::HookThread::kCapacity + 1));
	F4T::HookThread::NoteOnThread(Id::DropObject, 1);
	CHECK(Find(static_cast<std::uint16_t>(Id::DropObject), 1).count == 2);
	CHECK(Find(static_cast<std::uint16_t>(Id::DropObject), static_cast<std::uint32_t>(F4T::HookThread::kCapacity + 1)).count == 0);

	std::printf(
		"hook thread contract: execute=%u compile=%u otherThread=%u repeat=1 dropped=1 crash=1\n",
		execute.count,
		compile.count,
		other.count);
	return 0;
}
