#pragma once

#include <cstdint>
#include <string_view>

namespace F4T::QuestAssistCapture
{
	void SetOriginating(bool a_originating);

	[[nodiscard]] bool CreditTargetMatches(
		std::string_view a_questKey,
		std::uint32_t a_resolvedQuestFormId,
		std::uint32_t a_objectiveId,
		std::string_view a_targetKey);

	void NoteSentNpcHit(
		std::uint32_t a_sequence,
		std::uint32_t a_attackSequence,
		std::uint32_t a_sourceFormId,
		std::uint64_t a_nowMilliseconds);

	void Process(std::uint64_t a_nowMilliseconds);
	void Clear();
}
