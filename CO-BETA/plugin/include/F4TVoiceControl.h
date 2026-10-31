#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace F4T::Voice
{
	inline constexpr std::size_t kMaxBlockedPlayers = 15;

	struct ControlState
	{
		bool canSend{ true };
		bool canPlay{ true };
		bool deafened{};
		std::vector<std::uint32_t> blockedPlayerIds{};

		bool operator==(const ControlState&) const = default;
	};

	class ControlSync
	{
	public:
		void Negotiate(bool a_serverHasVoice) noexcept;
		void Reset() noexcept;
		void SetDesired(ControlState a_state);
		[[nodiscard]] std::optional<std::string> Poll(double a_now);
		void OnAck(std::uint32_t a_revision) noexcept;
		[[nodiscard]] bool CanSendVoice() const noexcept;
		[[nodiscard]] bool CanReceiveVoice() const noexcept;
		[[nodiscard]] bool ServerNoVoice() const noexcept;
		[[nodiscard]] bool Pending() const noexcept;

	private:
		ControlState m_desired{};
		std::uint32_t m_revision{ 1 };
		std::uint32_t m_sentRevision{};
		std::uint32_t m_ackedRevision{};
		double m_lastSendAt{};
		bool m_sentOnce{};
		bool m_negotiated{};
		bool m_serverHasVoice{};
	};
}
