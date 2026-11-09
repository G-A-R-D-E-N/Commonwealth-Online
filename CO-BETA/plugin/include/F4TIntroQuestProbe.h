#pragma once

#include <cstdint>
#include <string_view>

namespace F4T::IntroQuestProbe
{
	void BeginFirstJoinTrace();
	void EndFirstJoinTrace();
	void Tick();
	void LogNpcDecision(std::string_view a_decision, std::uint32_t a_sourceFormId, std::uint32_t a_baseFormId);
	void LogServerPositionCorrection(std::string_view a_payload);
}
