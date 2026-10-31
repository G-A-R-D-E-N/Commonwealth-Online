#include "F4TQuestNotices.h"

#include "F4TQuestProtocol.h"

#include <deque>
#include <mutex>

namespace F4T::QuestNotices
{
	namespace
	{
		std::mutex g_mutex;
		std::deque<Notice> g_notices;
		std::uint64_t g_revision{ 0 };
	}

	std::string FormatCompletion(std::string_view a_questName, std::string_view a_completer)
	{
		if (a_completer.empty()) {
			return std::string(a_questName) + " completed";
		}
		return std::string(a_completer) + " completed " + std::string(a_questName);
	}

	std::string FormatNotReady(std::string_view a_reason)
	{
		if (a_reason == F4T::QuestProtocol::kUpdateRequiredReason) {
			return "Update the mod to sync quests.";
		}
		if (a_reason == F4T::QuestProtocol::kNotReadyReason) {
			return "Quest sync is not ready yet; the update was not sent.";
		}
		return "The server rejected a quest update.";
	}

	std::string FormatNotReachable(std::string_view a_reason, std::string_view a_previousLabel)
	{
		// Reason codes; must stay equal to co-core/include/F4TQuestProtocol.h (kNotReachable*).
		if (a_reason == F4T::QuestProtocol::kNotReachablePrerequisite) {
			if (!a_previousLabel.empty()) {
				return "That quest isn't available yet — finish " + std::string(a_previousLabel) + " first";
			}
			return "That quest isn't available yet — finish the previous story quest first";
		}
		if (a_reason == F4T::QuestProtocol::kNotReachableUnknown) {
			return "That quest isn't part of the synced story.";
		}
		if (a_reason == F4T::QuestProtocol::kNotReachableNotStartable) {
			return "That quest can't be started yet.";
		}
		return "The server refused that quest update.";
	}

	std::string FormatReadyExhausted()
	{
		return "Quest sync stopped: the server kept refusing. Reconnect to retry.";
	}

	void Push(std::string_view a_text, std::string_view a_time)
	{
		const std::scoped_lock lock(g_mutex);
		g_notices.push_back(Notice{ std::string(a_text), std::string(a_time) });
		if (g_notices.size() > kMaximumNotices) {
			g_notices.pop_front();
		}
		++g_revision;
	}

	std::vector<Notice> Snapshot()
	{
		const std::scoped_lock lock(g_mutex);
		return std::vector<Notice>(g_notices.rbegin(), g_notices.rend());
	}

	std::uint64_t Revision()
	{
		const std::scoped_lock lock(g_mutex);
		return g_revision;
	}

	void Clear()
	{
		const std::scoped_lock lock(g_mutex);
		g_notices.clear();
		++g_revision;
	}
}
