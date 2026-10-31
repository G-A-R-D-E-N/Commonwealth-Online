#include "F4TVoiceReceive.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace F4T::Voice
{
	static_assert(VoiceSlotCount == MixerSpeakerCount);

	namespace
	{
		constexpr std::size_t ReceiveCrossfadeSamples = MixerFrameSamples / 2U;

		std::size_t SlotIndex(const VoiceSlots& a_slots, std::uint16_t a_speakerId) noexcept
		{
			for (std::size_t i = 0; i < VoiceSlotCount; ++i) {
				if (a_slots.slots[i].used && a_slots.slots[i].speakerId == a_speakerId) {
					return i;
				}
			}
			return VoiceSlotCount;
		}

		const ReceiveSpeakerPose* FindPose(const ReceivePoses& a_poses, std::uint16_t a_speakerId) noexcept
		{
			for (const auto& pose : a_poses.speakers) {
				if (pose.used && pose.speakerId == a_speakerId) {
					return &pose;
				}
			}
			return nullptr;
		}

		StereoGain Spatialise(const ReceivePoses& a_poses, std::uint16_t a_speakerId, Tier a_tier) noexcept
		{
			const ReceiveSpeakerPose* pose = FindPose(a_poses, a_speakerId);
			if (pose == nullptr) {
				return ConstantPowerGains(0.0);
			}
			const double distance = std::hypot(pose->x - a_poses.listenerX, pose->y - a_poses.listenerY, pose->z - a_poses.listenerZ);
			const StereoGain pan = ConstantPowerGains(
				SpeakerPan(a_poses.listenerX, a_poses.listenerY, a_poses.listenerHeading, pose->x, pose->y));
			const double gain = DistanceGain(distance, TierRange(a_tier));
			return { pan.left * gain, pan.right * gain };
		}

		void FadeOut(MixerSpeaker& a_speaker) noexcept
		{
			const std::int16_t held = a_speaker.pcm[MixerFrameSamples - 1];
			a_speaker.gain = {};
			std::fill_n(a_speaker.pcm.begin(), MixerFrameSamples, held);
		}

		void FadeOutTail(MixerSpeaker& a_speaker) noexcept
		{
			const int held = a_speaker.pcm[MixerFrameSamples - 1];
			for (std::size_t n = 0; n < MixerFrameSamples; ++n) {
				const int remaining = static_cast<int>(MixerFrameSamples - 1U - n);
				a_speaker.pcm[n] = static_cast<std::int16_t>(held * remaining / static_cast<int>(MixerFrameSamples));
			}
		}

		void Crossfade(std::int16_t a_held, std::int16_t* a_pcm) noexcept
		{
			constexpr int span = static_cast<int>(ReceiveCrossfadeSamples);
			for (std::size_t n = 0; n < ReceiveCrossfadeSamples; ++n) {
				const int from = static_cast<int>(n);
				a_pcm[n] = static_cast<std::int16_t>((a_held * (span - from) + a_pcm[n] * from) / span);
			}
		}

		void OnFill(void* a_user, float* a_interleavedStereo, std::uint32_t a_frames)
		{
			static_cast<Receiver*>(a_user)->Fill(a_interleavedStereo, a_frames);
		}

		void OnRenderError(void*, long) noexcept
		{
		}
	}

	bool ReceiveRing::Push(const ReceiveEntry& a_entry) noexcept
	{
		const auto tail = m_tail.load(std::memory_order_relaxed);
		if (tail - m_head.load(std::memory_order_acquire) >= ReceiveRingSlots) {
			m_dropped.fetch_add(1U, std::memory_order_relaxed);
			return false;
		}
		m_entries[tail % ReceiveRingSlots] = a_entry;
		m_tail.store(tail + 1U, std::memory_order_release);
		return true;
	}

	bool ReceiveRing::Pop(ReceiveEntry& a_out) noexcept
	{
		const auto head = m_head.load(std::memory_order_relaxed);
		if (head == m_tail.load(std::memory_order_acquire)) {
			return false;
		}
		a_out = m_entries[head % ReceiveRingSlots];
		m_head.store(head + 1U, std::memory_order_release);
		return true;
	}

	std::uint64_t ReceiveRing::Dropped() const noexcept
	{
		return m_dropped.load(std::memory_order_relaxed);
	}

	Receiver::Receiver(std::function<double()> a_clock) : m_clock(std::move(a_clock))
	{
		for (auto& volume : m_volumes) {
			volume.store(static_cast<std::uint8_t>(ReceiveDefaultVolume));
		}
	}

	Receiver::~Receiver()
	{
		Stop();
	}

	bool Receiver::Open()
	{
		bool opened = true;
		for (auto& decoder : m_decoders) {
			opened = decoder.Open() && opened;
		}
		return opened;
	}

	bool Receiver::Start()
	{
		return m_render.Start(&OnFill, &OnRenderError, this);
	}

	void Receiver::Stop()
	{
		m_render.Stop();
	}

	void Receiver::RequestReopen()
	{
		m_render.RequestReopen();
	}

	bool Receiver::OutputFailed() const
	{
		return m_render.OutputFailed();
	}

	bool Receiver::OnPacket(const VoicePacket& a_packet, double a_arrivalSeconds) noexcept
	{
		const std::size_t size = a_packet.payload.size();
		if (size == 0 || size > kVoiceMaxPayloadBytes || m_deafened.load() || IsMuted(a_packet.header.speakerId)) {
			return false;
		}
		ReceiveEntry entry{};
		entry.generation = m_generation.load();
		entry.sequence = a_packet.header.sequence;
		entry.arrivalSeconds = a_arrivalSeconds;
		entry.speakerId = a_packet.header.speakerId;
		entry.tier = a_packet.header.tier;
		entry.burstStart = a_packet.header.burstStart;
		entry.burstEnd = a_packet.header.burstEnd;
		entry.size = size;
		std::copy(a_packet.payload.begin(), a_packet.payload.end(), entry.payload.begin());
		return m_ring.Push(entry);
	}

	void Receiver::ResetSession() noexcept
	{
		m_generation.fetch_add(1U);
	}

	void Receiver::SetDeafened(bool a_deafened) noexcept
	{
		m_deafened.store(a_deafened);
	}

	void Receiver::SetSpeakerMuted(std::uint16_t a_speakerId, bool a_muted)
	{
		const auto bit = std::uint64_t{ 1 } << (a_speakerId % 64U);
		auto& word = m_muted[a_speakerId / 64U];
		if (a_muted) {
			word.fetch_or(bit);
			RemoveSpeaker(a_speakerId);
		} else {
			word.fetch_and(~bit);
		}
	}

	void Receiver::SetSpeakerVolume(std::uint16_t a_speakerId, int a_volume) noexcept
	{
		m_volumes[a_speakerId].store(static_cast<std::uint8_t>(std::clamp(a_volume, 0, 100)));
	}

	void Receiver::SetMasterVolume(int a_volume) noexcept
	{
		m_masterVolume.store(a_volume);
	}

	void Receiver::SetPositions(const ListenerPose& a_listener, std::span<const SpeakerPosition> a_speakers)
	{
		std::lock_guard lock(m_poseMutex);
		m_poses = {};
		m_poses.listenerX = a_listener.x;
		m_poses.listenerY = a_listener.y;
		m_poses.listenerZ = a_listener.z;
		m_poses.listenerHeading = a_listener.angleZ;
		const std::size_t count = (std::min)(a_speakers.size(), m_poses.speakers.size());
		for (std::size_t i = 0; i < count; ++i) {
			m_poses.speakers[i] = { a_speakers[i].playerId, a_speakers[i].x, a_speakers[i].y, a_speakers[i].z, true };
		}
	}

	void Receiver::RemoveSpeaker(std::uint16_t a_speakerId)
	{
		std::lock_guard lock(m_poseMutex);
		m_removals[a_speakerId / 64U] |= std::uint64_t{ 1 } << (a_speakerId % 64U);
	}

	std::vector<std::uint16_t> Receiver::Speakers() const
	{
		std::lock_guard lock(m_speakingMutex);
		return std::vector<std::uint16_t>(m_speaking.data(), m_speaking.data() + m_speakingCount);
	}

	ReceiveStats Receiver::Stats() const noexcept
	{
		return { m_played.load(), m_recovered.load(), m_concealed.load(), m_ring.Dropped() };
	}

	void Receiver::Fill(float* a_interleavedStereo, std::uint32_t a_frames) noexcept
	{
		if (m_deafened.load()) {
			std::fill_n(a_interleavedStereo, 2U * static_cast<std::size_t>(a_frames), 0.0F);
			Silence();
			return;
		}
		std::size_t written = 0;
		while (written < a_frames) {
			if (m_mixPosition == MixerFrameSamples) {
				Tick();
				m_mixPosition = 0;
			}
			const std::size_t count = std::min<std::size_t>(a_frames - written, MixerFrameSamples - m_mixPosition);
			std::copy_n(m_out.data() + 2 * m_mixPosition, 2 * count, a_interleavedStereo + 2 * written);
			m_mixPosition += count;
			written += count;
		}
	}

	void Receiver::Tick() noexcept
	{
		const double now = m_clock();
		AdoptGeneration(m_generation.load());
		RefreshPoses();
		Drain();
		Expire(m_slots, now);
		for (std::size_t i = 0; i < VoiceSlotCount; ++i) {
			MixerSpeaker& speaker = m_speakers[i];
			if (!m_slots.slots[i].used) {
				FadeOut(speaker);
				m_realAudio[i] = false;
				continue;
			}
			const std::uint16_t speakerId = m_slots.slots[i].speakerId;
			const bool muted = IsMuted(speakerId);
			const Decision decision = Next(m_slots.slots[i].jitter, now);
			const bool audible = decision.kind == DecisionKind::Play || decision.kind == DecisionKind::Fec || decision.kind == DecisionKind::Conceal;
			if (audible) {
				DecodeSlot(i, decision);
				speaker.gain = muted ? StereoGain{} : Spatialise(m_activePoses, speakerId, m_tiers[i]);
			} else if (decision.kind == DecisionKind::Silence) {
				FadeOutTail(speaker);
			} else {
				FadeOut(speaker);
			}
			speaker.volume = m_volumes[speakerId].load();
			m_realAudio[i] = !muted && audible && decision.kind != DecisionKind::Conceal;
		}
		MixFrame(m_speakers, m_masterVolume.load(), m_out);
		PublishSpeaking();
	}

	void Receiver::Silence() noexcept
	{
		ReceiveEntry entry{};
		while (m_ring.Pop(entry)) {
		}
		ResetState();
		m_speakers = {};
		m_mixPosition = MixerFrameSamples;
		PublishSpeaking();
	}

	void Receiver::AdoptGeneration(std::uint32_t a_generation) noexcept
	{
		if (a_generation != m_seenGeneration) {
			ResetState();
			m_seenGeneration = a_generation;
		}
	}

	void Receiver::ResetState() noexcept
	{
		F4T::Voice::ResetSession(m_slots);
		m_tiers = {};
		m_realAudio = {};
	}

	void Receiver::RefreshPoses() noexcept
	{
		std::unique_lock lock(m_poseMutex, std::try_to_lock);
		if (!lock.owns_lock()) {
			return;
		}
		m_activePoses = m_poses;
		for (auto& slot : m_slots.slots) {
			if (slot.used && ((m_removals[slot.speakerId / 64U] >> (slot.speakerId % 64U)) & 1U) != 0U) {
				Remove(m_slots, slot.speakerId);
			}
		}
		m_removals = {};
	}

	void Receiver::Drain() noexcept
	{
		ReceiveEntry entry{};
		while (m_ring.Pop(entry)) {
			if (entry.generation != m_generation.load()) {
				continue;
			}
			AdoptGeneration(entry.generation);
			if (IsMuted(entry.speakerId)) {
				continue;
			}
			const std::span<const std::byte> payload(entry.payload.data(), entry.size);
			if (!Accept(m_slots, entry.speakerId, entry.sequence, payload, entry.burstStart, entry.burstEnd, entry.arrivalSeconds)) {
				continue;
			}
			const std::size_t index = SlotIndex(m_slots, entry.speakerId);
			if (index < VoiceSlotCount) {
				m_tiers[index] = entry.tier;
			}
		}
	}

	void Receiver::DecodeSlot(std::size_t a_index, const Decision& a_decision) noexcept
	{
		Decoder& decoder = m_decoders[a_index];
		MixerSpeaker& speaker = m_speakers[a_index];
		std::int16_t* pcm = speaker.pcm.data();
		const std::int16_t held = pcm[MixerFrameSamples - 1];
		const bool audible = speaker.applied.left != 0.0 || speaker.applied.right != 0.0;
		if (a_decision.resetDecoder) {
			decoder.Reset();
		}
		const auto* data = reinterpret_cast<const std::uint8_t*>(a_decision.payload.data());
		const int size = static_cast<int>(a_decision.payload.size());
		int samples = 0;
		switch (a_decision.kind) {
		case DecisionKind::Play:
			samples = decoder.Decode(data, size, pcm);
			m_played.fetch_add(1U);
			break;
		case DecisionKind::Fec:
			samples = decoder.DecodeFec(data, size, pcm);
			m_recovered.fetch_add(1U);
			break;
		case DecisionKind::Conceal:
			samples = decoder.Conceal(pcm);
			m_concealed.fetch_add(1U);
			break;
		default:
			break;
		}
		if (samples != kFrameSamples) {
			std::fill_n(pcm, MixerFrameSamples, std::int16_t{ 0 });
		}
		if (a_decision.resetDecoder && audible) {
			Crossfade(held, pcm);
		}
	}

	void Receiver::PublishSpeaking() noexcept
	{
		std::unique_lock lock(m_speakingMutex, std::try_to_lock);
		if (!lock.owns_lock()) {
			return;
		}
		m_speakingCount = 0;
		for (std::size_t i = 0; i < VoiceSlotCount; ++i) {
			if (m_slots.slots[i].used && m_realAudio[i]) {
				m_speaking[m_speakingCount] = m_slots.slots[i].speakerId;
				m_speakingCount += 1U;
			}
		}
	}

	bool Receiver::IsMuted(std::uint16_t a_speakerId) const noexcept
	{
		return ((m_muted[a_speakerId / 64U].load() >> (a_speakerId % 64U)) & 1U) != 0U;
	}
}
