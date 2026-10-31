%:pragma once

%:include <array>
%:include <cstddef>
%:include <cstdint>
%:include <utility>

%:include "F4TGnsSnapshotEnvelope.h"
%:include "F4TSnapshotDeltaModel.h"

namespace F4T::Replication
{
	class SnapshotReceiver
	{
	public:
		bool AcceptBaseline(SnapshotState a_baseline, std::uint32_t a_snapSeq, Channel a_channel = Channel::Unreliable)
		{
			if (a_snapSeq == 0 || !IsValidChannel(a_channel)) {
				return false;
			}
			auto& stream = m_streams[static_cast<std::size_t>(a_channel)];
			if (stream.hasState && !GnsSnapshotEnvelope::IsNewer(a_snapSeq, stream.lastSnapshotSequence)) {
				return false;
			}
			stream.baseline = std::move(a_baseline);
			stream.baselineSequence = a_snapSeq;
			stream.lastSnapshotSequence = a_snapSeq;
			stream.view = stream.baseline;
			stream.hasState = true;
			return true;
		}

		bool AcceptDelta(
			const SnapshotDelta& a_delta,
			std::uint32_t a_snapSeq,
			std::uint32_t a_baseSeq,
			Channel a_channel = Channel::Unreliable)
		{
			if (a_snapSeq == 0 || a_baseSeq == 0 || !IsValidChannel(a_channel)) {
				return false;
			}
			auto& stream = m_streams[static_cast<std::size_t>(a_channel)];
			if (!stream.hasState || a_baseSeq != stream.baselineSequence ||
				!GnsSnapshotEnvelope::IsNewer(a_snapSeq, stream.lastSnapshotSequence)) {
				return false;
			}
			stream.view = ApplyDelta(stream.baseline, a_delta);
			stream.lastSnapshotSequence = a_snapSeq;
			return true;
		}

		[[nodiscard]] const SnapshotState& CurrentView(Channel a_channel = Channel::Unreliable) const noexcept { return Stream(a_channel).view; }
		[[nodiscard]] const SnapshotState& Baseline(Channel a_channel = Channel::Unreliable) const noexcept { return Stream(a_channel).baseline; }
		[[nodiscard]] std::uint32_t BaselineSequence(Channel a_channel = Channel::Unreliable) const noexcept { return Stream(a_channel).baselineSequence; }
		[[nodiscard]] std::uint32_t LastSnapshotSequence(Channel a_channel = Channel::Unreliable) const noexcept { return Stream(a_channel).lastSnapshotSequence; }
		[[nodiscard]] bool HasState(Channel a_channel = Channel::Unreliable) const noexcept { return Stream(a_channel).hasState; }

	private:
		struct StreamState
		{
			SnapshotState baseline;
			SnapshotState view;
			std::uint32_t baselineSequence{ 0 };
			std::uint32_t lastSnapshotSequence{ 0 };
			bool hasState{ false };
		};

		[[nodiscard]] static const StreamState& EmptyStream() noexcept
		{
			static const StreamState empty{};
			return empty;
		}

		[[nodiscard]] const StreamState& Stream(Channel a_channel) const noexcept
		{
			if (!IsValidChannel(a_channel)) {
				return EmptyStream();
			}

			return m_streams[static_cast<std::size_t>(a_channel)];
		}

		std::array<StreamState, 2> m_streams;
	};
}
