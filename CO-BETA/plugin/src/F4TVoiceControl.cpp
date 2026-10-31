#include "F4TVoiceControl.h"

#include "F4TVoiceCodec.h"
#include "F4TVoiceWire.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace F4T::Voice
{
	namespace
	{
		constexpr double kSendIntervalSeconds = 1.0;
		constexpr int kFrameMs = kFrameSamples * 1000 / kSampleRate;
	}

	void ControlSync::Negotiate(bool a_serverHasVoice) noexcept
	{
		m_negotiated = true;
		m_serverHasVoice = a_serverHasVoice;
	}

	void ControlSync::Reset() noexcept
	{
		m_revision = 1;
		m_sentRevision = 0;
		m_ackedRevision = 0;
		m_lastSendAt = 0.0;
		m_sentOnce = false;
		m_negotiated = false;
		m_serverHasVoice = false;
	}

	void ControlSync::SetDesired(ControlState a_state)
	{
		auto& blocked = a_state.blockedPlayerIds;
		std::sort(blocked.begin(), blocked.end());
		blocked.erase(std::unique(blocked.begin(), blocked.end()), blocked.end());
		if (blocked.size() > kMaxBlockedPlayers) {
			blocked.resize(kMaxBlockedPlayers);
		}
		if (a_state == m_desired) {
			return;
		}
		m_desired = std::move(a_state);
		++m_revision;
	}

	std::optional<std::string> ControlSync::Poll(double a_now)
	{
		if (!m_serverHasVoice || m_ackedRevision == m_revision) {
			return std::nullopt;
		}
		if (m_sentOnce && a_now - m_lastSendAt < kSendIntervalSeconds) {
			return std::nullopt;
		}

		m_sentOnce = true;
		m_lastSendAt = a_now;
		m_sentRevision = m_revision;
		const nlohmann::json snapshot{
			{ "type", "voiceState" },
			{ "revision", m_revision },
			{ "profile", kVoiceProfileOpus48kMono20ms },
			{ "rate", kSampleRate },
			{ "channels", kChannels },
			{ "frameMs", kFrameMs },
			{ "canSend", m_desired.canSend },
			{ "canPlay", m_desired.canPlay },
			{ "deafened", m_desired.deafened },
			{ "blockedPlayerIds", m_desired.blockedPlayerIds }
		};
		return snapshot.dump();
	}

	void ControlSync::OnAck(std::uint32_t a_revision) noexcept
	{
		if (a_revision == m_sentRevision) {
			m_ackedRevision = a_revision;
		}
	}

	bool ControlSync::CanSendVoice() const noexcept
	{
		return m_serverHasVoice && m_desired.canSend && m_ackedRevision == m_revision;
	}

	bool ControlSync::CanReceiveVoice() const noexcept
	{
		return m_serverHasVoice;
	}

	bool ControlSync::ServerNoVoice() const noexcept
	{
		return m_negotiated && !m_serverHasVoice;
	}

	bool ControlSync::Pending() const noexcept
	{
		return m_negotiated && m_serverHasVoice && m_ackedRevision != m_revision;
	}
}
