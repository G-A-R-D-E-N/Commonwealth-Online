#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace VoiceLab::Tload
{
	std::string HelloJson(const std::string& a_profileId, bool a_voice);
	std::string KeepAliveJson(const std::string& a_resumeToken);
	std::string VoiceStateJson();
	std::string ChatJson(const std::string& a_text);
	std::string TransformJson(double a_x, double a_y, double a_clientTime);
	std::vector<std::uint8_t> TransformEnvelope(const std::string& a_json, std::uint32_t& a_sequence);
	std::vector<std::uint8_t> VoiceDatagram(std::uint32_t& a_sequence, bool a_burstStart);
	bool FindNumber(std::string_view a_json, std::string_view a_key, double& a_value);
	bool FindString(std::string_view a_json, std::string_view a_key, std::string& a_value);
}
