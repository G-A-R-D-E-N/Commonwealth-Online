#include "lab_tload_wire.h"

#include "F4TGnsSnapshotEnvelope.h"
#include "F4TVoiceWire.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <span>

namespace VoiceLab::Tload
{
	namespace
	{
		constexpr std::size_t kFingerprintBytes = 64;
		constexpr std::size_t kVoicePayloadBytes = 60;
		constexpr std::byte kVoicePayloadFill{ 0x5A };
		constexpr char kCellId[] = "00012345";
		constexpr char kWorldspaceId[] = "0000003C";
		constexpr std::size_t kMaximumNumberBytes = 64;

		std::string_view AfterKey(std::string_view a_json, std::string_view a_key)
		{
			const std::string needle = "\"" + std::string{ a_key } + "\":";
			const auto at = a_json.find(needle);
			return at == std::string_view::npos ? std::string_view{} : a_json.substr(at + needle.size());
		}
	}

	std::string HelloJson(const std::string& a_profileId, bool a_voice)
	{
		std::string capabilities = "\"component-snapshot-channels-v1\",\"component-snapshot-v1\",\"inventory-stable-v1\",\"object-state-v1\",\"save-attestation-v1\",\"session-lifecycle-v1\",\"world-policy-v1\"";
		if (a_voice) {
			capabilities += ",\"voice-v1\"";
		}
		return "{\"type\":\"hello\",\"protocolVersion\":2,\"clientVersion\":\"voice-tload-1\",\"falloutRuntimeVersion\":\"1.11.240\",\"profileId\":\"" +
			a_profileId + "\",\"contentManifestFingerprint\":\"" + std::string(kFingerprintBytes, 'c') + "\",\"capabilities\":[" + capabilities + "]}";
	}

	std::string KeepAliveJson(const std::string& a_resumeToken)
	{
		return "{\"type\":\"keepAlive\",\"sessionReadyAck\":true,\"resumeTokenAck\":\"" + a_resumeToken + "\"}";
	}

	std::string VoiceStateJson()
	{
		return "{\"type\":\"voiceState\",\"revision\":1,\"profile\":0,\"rate\":48000,\"channels\":1,\"frameMs\":20,"
			"\"canSend\":true,\"canPlay\":true,\"deafened\":false,\"blockedPlayerIds\":[]}";
	}

	std::string ChatJson(const std::string& a_text)
	{
		return "{\"type\":\"chatSend\",\"channel\":\"global\",\"text\":\"" + a_text + "\"}";
	}

	std::string TransformJson(double a_x, double a_y, double a_clientTime)
	{
		std::array<char, 640> buffer{};
		std::snprintf(buffer.data(), buffer.size(),
			"{\"type\":\"transform\",\"x\":%.6f,\"y\":%.6f,\"z\":0.0,\"angleZ\":0.0,\"movementType\":\"normal\",\"clientTime\":%.17g,"
			"\"isMoving\":true,\"isSprinting\":false,\"isSneaking\":false,\"isJumping\":false,\"isCrouching\":false,\"weaponDrawn\":false,"
			"\"movementSpeed\":4.5,\"animationGraphSpeed\":4.5,\"animationDirection\":0.0,\"aimPitch\":0.0,\"turnDelta\":0.0,"
			"\"characterName\":\"tload\",\"cellId\":\"%s\",\"worldspaceId\":\"%s\"}",
			a_x, a_y, a_clientTime, kCellId, kWorldspaceId);
		return buffer.data();
	}

	std::vector<std::uint8_t> TransformEnvelope(const std::string& a_json, std::uint32_t& a_sequence)
	{
		a_sequence = F4T::GnsSnapshotEnvelope::NextSequence(a_sequence);
		std::vector<std::uint8_t> bytes(F4T::GnsSnapshotEnvelope::HeaderSize + a_json.size());
		(void)F4T::GnsSnapshotEnvelope::WriteHeader(bytes, F4T::GnsSnapshotEnvelope::Family::Transform, a_sequence);
		std::memcpy(bytes.data() + F4T::GnsSnapshotEnvelope::HeaderSize, a_json.data(), a_json.size());
		return bytes;
	}

	std::vector<std::uint8_t> VoiceDatagram(std::uint32_t& a_sequence, bool a_burstStart)
	{
		const std::vector<std::byte> payload(kVoicePayloadBytes, kVoicePayloadFill);
		F4T::Voice::VoiceHeader header{};
		header.tier = F4T::Voice::Tier::Normal;
		header.burstStart = a_burstStart;
		a_sequence = F4T::Voice::NextTick(a_sequence);
		header.sequence = a_sequence;
		std::vector<std::uint8_t> datagram(F4T::Voice::kVoiceMaxDatagramBytes);
		datagram.resize(F4T::Voice::EncodeVoicePacket(header, payload, std::as_writable_bytes(std::span{ datagram })));
		return datagram;
	}

	bool FindNumber(std::string_view a_json, std::string_view a_key, double& a_value)
	{
		const auto rest = AfterKey(a_json, a_key).substr(0, kMaximumNumberBytes);
		const auto result = std::from_chars(rest.data(), rest.data() + rest.size(), a_value);
		return result.ec == std::errc{};
	}

	bool FindString(std::string_view a_json, std::string_view a_key, std::string& a_value)
	{
		auto rest = AfterKey(a_json, a_key);
		if (rest.empty() || rest.front() != '"') {
			return false;
		}
		rest.remove_prefix(1);
		const auto end = rest.find('"');
		if (end == std::string_view::npos) {
			return false;
		}
		a_value.assign(rest.substr(0, end));
		return true;
	}
}
