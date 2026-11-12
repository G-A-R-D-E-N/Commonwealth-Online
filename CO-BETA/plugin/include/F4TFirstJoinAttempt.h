#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

namespace F4T::FirstJoinAttempt
{
	struct Scope
	{
		std::uint64_t connection{};
		std::uint64_t session{};
		std::uint64_t loadEpoch{};
		std::uint64_t policyRevision{};
	};

	struct Token
	{
		std::uint64_t id{};
		Scope scope{};
		std::string baselineMostRecentSave;
		std::uint64_t baselineLastSaveTime{};
	};

	struct Entry
	{
		std::uint64_t id{};
		bool result{};
		std::uint32_t count{};
	};

	inline std::uint64_t NextId()
	{
		static std::atomic<std::uint64_t> lastId{ 0 };
		return ++lastId;
	}

	inline bool SameScope(const Scope& a_left, const Scope& a_right)
	{
		return a_left.connection == a_right.connection && a_left.session == a_right.session &&
		       a_left.loadEpoch == a_right.loadEpoch && a_left.policyRevision == a_right.policyRevision;
	}

	inline bool CallbackAccepted(
		const Token& a_token,
		const Scope& a_current,
		bool a_callbackSeen,
		bool a_setupSave,
		const std::string& a_saveName,
		const std::string& a_characterId)
	{
		return !a_callbackSeen && !a_setupSave && !a_saveName.empty() && !a_characterId.empty() &&
		       a_saveName != a_token.baselineMostRecentSave && SameScope(a_token.scope, a_current);
	}

	inline bool EntryAccepted(const Token& a_token, const Entry& a_entry)
	{
		return a_entry.id == a_token.id && a_entry.result && a_entry.count == 1;
	}

	inline bool EntryPending(const Token& a_token, const Entry& a_entry)
	{
		return a_entry.id == a_token.id && a_entry.count == 0;
	}

	inline bool SettleExpired(std::chrono::steady_clock::time_point a_deadline, std::chrono::steady_clock::time_point a_now)
	{
		return a_now >= a_deadline;
	}

	inline bool ShouldRecordOnboardingResume(bool a_pendingBind, bool a_pastPodExit, bool a_setupSave, const std::string& a_saveName)
	{
		return a_pendingBind && a_pastPodExit && a_setupSave && !a_saveName.empty();
	}

	inline bool ShouldResumeOnboarding(bool a_onMainMenu, bool a_hasGameplaySave, bool a_hasResumeSave)
	{
		return a_onMainMenu && !a_hasGameplaySave && a_hasResumeSave;
	}
}
