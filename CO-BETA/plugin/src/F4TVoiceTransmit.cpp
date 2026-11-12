#include "F4TVoiceTransmit.h"

#include "F4TVoiceWire.h"

#include <windows.h>

#include <algorithm>
#include <utility>

namespace F4T::Voice
{
	namespace
	{
		bool Held(const KeyDownFn& a_keyDown, std::uint8_t a_vk)
		{
			return a_vk != 0 && a_keyDown(a_vk);
		}

		constexpr std::uint8_t kPadKeyFirst = 0xC3;
		constexpr std::uint8_t kPadKeyLast = 0xD2;

		struct PadState
		{
			WORD          buttons{};
			BYTE          leftTrigger{};
			BYTE          rightTrigger{};
		};
		struct XInputStateRaw
		{
			DWORD  packet;
			PadState pad;
			SHORT  axes[4];
		};
		using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XInputStateRaw*);

		constexpr WORD kPadMasks[] = { 0x1000, 0x2000, 0x4000, 0x8000, 0x0200, 0x0100, 0, 0, 0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080 };
		constexpr BYTE kTriggerThreshold = 30;

		bool PadKeyDown(std::uint8_t a_vk)
		{
			static XInputGetStateFn getState = [] {
				const auto module = LoadLibraryExW(L"xinput1_4.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
				return module ? reinterpret_cast<XInputGetStateFn>(GetProcAddress(module, "XInputGetState")) : nullptr;
			}();
			static int         padIndex = -1;
			static ULONGLONG   nextScan = 0;
			static ULONGLONG   lastRead = 0;
			static PadState    last{};
			if (!getState) {
				return false;
			}
			const auto now = GetTickCount64();
			if (now - lastRead >= 4) {
				lastRead = now;
				XInputStateRaw raw{};
				bool           ok = padIndex >= 0 && getState(static_cast<DWORD>(padIndex), &raw) == ERROR_SUCCESS;
				if (!ok) {
					padIndex = -1;
					if (now >= nextScan) {
						nextScan = now + 1000;
						for (int i = 0; i < 4 && !ok; ++i) {
							if (getState(static_cast<DWORD>(i), &raw) == ERROR_SUCCESS) {
								padIndex = i;
								ok = true;
							}
						}
					}
				}
				last = ok ? raw.pad : PadState{};
			}
			const auto index = a_vk - kPadKeyFirst;
			if (index == 6) {
				return last.leftTrigger > kTriggerThreshold;
			}
			if (index == 7) {
				return last.rightTrigger > kTriggerThreshold;
			}
			return (last.buttons & kPadMasks[index]) != 0;
		}

		void SamplesThunk(void* a_user, const std::int16_t* a_mono, std::uint32_t a_frames)
		{
			static_cast<Transmitter*>(a_user)->OnSamples(a_mono, a_frames);
		}

		void TickThunk(void* a_user)
		{
			static_cast<Transmitter*>(a_user)->OnTick();
		}

		void ErrorThunk(void* a_user, long)
		{
			static_cast<Transmitter*>(a_user)->OnDeviceError();
		}

		void ReopenThunk(void* a_user)
		{
			static_cast<Transmitter*>(a_user)->OnReopenAttempt();
		}
	}

	bool IsVoiceKeyDown(std::uint8_t a_vk)
	{
		if (a_vk >= kPadKeyFirst && a_vk <= kPadKeyLast) {
			return PadKeyDown(a_vk);
		}
		return (GetAsyncKeyState(a_vk) & 0x8000) != 0;
	}

	Transmitter::Transmitter(TransmitDeps a_deps) : m_deps(std::move(a_deps)), m_health(m_deps.clock())
	{
		EnsureEncoder();
	}

	Transmitter::~Transmitter()
	{
		Stop();
	}

	bool Transmitter::Start()
	{
		{
			std::scoped_lock lock(m_mutex);
			m_health = MicHealth(m_deps.clock());
		}
		return m_capture.Start(SamplesThunk, TickThunk, ErrorThunk, ReopenThunk, this);
	}

	void Transmitter::Stop()
	{
		m_capture.Stop();
		m_transmitting.store(false);
		m_inBurst = false;
		m_pendingCount = 0;
	}

	void Transmitter::Configure(const TransmitConfig& a_config)
	{
		std::scoped_lock lock(m_mutex);
		m_config = a_config;
	}

	void Transmitter::SetCanSend(bool a_canSend) noexcept
	{
		m_canSend.store(a_canSend);
	}

	void Transmitter::SetInputAllowed(bool a_allowed) noexcept
	{
		m_inputAllowed.store(a_allowed);
	}

	void Transmitter::ResetSession() noexcept
	{
		m_sessionReset.store(true);
	}

	void Transmitter::RequestReopen()
	{
		m_capture.RequestReopen();
	}

	TransmitStatus Transmitter::Status() const
	{
		const double now = m_deps.clock();
		const bool transmitting = m_transmitting.load();
		TransmitStatus status{};
		{
			std::scoped_lock lock(m_mutex);
			status.inputs.micFailed = m_health.Failed(now);
			status.inputs.micRecovering = m_health.Recovering();
			status.inputs.micSilentHint = m_health.SilentHint(now);
			status.inputs.idle = m_health.Idle(now, transmitting);
			status.inputs.micMuted = m_config.micMuted;
		}
		status.inputs.transmitting = transmitting && !status.inputs.micFailed;
		status.inputs.codecFailed = m_codecFailed.load();
		status.levelDbfs = m_levelDbfs.load();
		status.sent = m_sent.load();
		status.dropped = m_dropped.load();
		status.codecFailures = m_codecFailures.load();
		return status;
	}

	void Transmitter::OnSamples(const std::int16_t* a_mono, std::uint32_t a_frames)
	{
		for (std::uint32_t i = 0; i < a_frames; ++i) {
			m_pending[m_pendingCount] = a_mono[i];
			++m_pendingCount;
			if (m_pendingCount == VoiceFrameSamples) {
				m_pendingCount = 0;
				ProcessFrame(m_pending.data());
			}
		}
	}

	void Transmitter::OnTick()
	{
		PollPtt(Snapshot());
	}

	void Transmitter::PollPtt(const TransmitConfig& a_config)
	{
		PttInput input{};
		input.inputAllowed = m_inputAllowed.load();
		input.keyDown = Held(m_deps.keyDown, a_config.pttVk);
		input.whisperDown = input.inputAllowed && Held(m_deps.keyDown, a_config.whisperVk);
		input.shoutDown = input.inputAllowed && Held(m_deps.keyDown, a_config.shoutVk);
		m_pttHeld = input.keyDown && input.inputAllowed;
		m_ptt.Poll(input, m_deps.clock());
	}

	void Transmitter::ResetBurstState(const TransmitConfig& a_config)
	{
		m_inBurst = false;
		m_gate = OpenMicGate(m_gateThresholdDbfs);
		m_ptt = PttState{};
		PollPtt(a_config);
	}

	void Transmitter::OnDeviceError()
	{
		m_transmitting.store(false);
		std::scoped_lock lock(m_mutex);
		m_health.OnDeviceError();
	}

	void Transmitter::OnReopenAttempt()
	{
		m_pendingCount = 0;
		ResetBurstState(Snapshot());
		if (m_codecFailed.load()) {
			m_encoderOpen = false;
		}
		EnsureEncoder();
		std::scoped_lock lock(m_mutex);
		m_health.OnReopenAttempt();
	}

	TransmitConfig Transmitter::Snapshot() const
	{
		std::scoped_lock lock(m_mutex);
		return m_config;
	}

	void Transmitter::EnsureEncoder()
	{
		if (!m_encoderOpen) {
			m_encoderOpen = m_encoder.Open();
			m_codecFailed.store(!m_encoderOpen);
		}
	}

	bool Transmitter::BeginBurst()
	{
		m_needBurstStart = true;
		if (m_encoderOpen && m_encoder.Reset()) {
			return true;
		}
		m_encoderOpen = false;
		m_codecFailed.store(true);
		return false;
	}

	void Transmitter::ProcessFrame(const std::int16_t* a_frame)
	{
		const double now = m_deps.clock();
		const TransmitConfig config = Snapshot();
		const auto frame = std::span<const std::int16_t, VoiceFrameSamples>(a_frame, VoiceFrameSamples);
		const FrameLevel level = FrameLevelDbfs(frame);
		if (static_cast<double>(config.openMicThresholdDbfs) != m_gateThresholdDbfs) {
			m_gateThresholdDbfs = config.openMicThresholdDbfs;
			m_gate = OpenMicGate(m_gateThresholdDbfs);
		}
		if (m_sessionReset.exchange(false)) {
			m_sequence = 0;
			ResetBurstState(config);
		}
		m_sequence = NextTick(m_sequence);
		std::copy_backward(m_recentSequences.begin(), m_recentSequences.end() - 1, m_recentSequences.end());
		m_recentSequences.front() = m_sequence;

		const bool allowed = config.voiceChat && !config.micMuted && m_canSend.load();
		const bool pttLive = !config.pushToTalk || m_inputAllowed.load();
		const GateTick gate = m_gate.Tick(frame);
		if (!allowed) {
			m_gate = OpenMicGate(m_gateThresholdDbfs);
		}
		const PttTick ptt = m_ptt.Tick(now);
		const bool send = config.pushToTalk ? ptt.transmit : gate.send;
		// A blocked input ends an open burst on its first frame, so burstEnd goes out there.
		const bool burstEnd = (config.pushToTalk ? ptt.burstEnd : gate.burstEnd) || (m_inBurst && !pttLive);
		const std::size_t preRoll = config.pushToTalk ? 0 : gate.preRoll;
		const bool wanted = send && allowed && (pttLive || m_inBurst);
		const bool started = wanted && !m_inBurst;
		const bool active = wanted && (!started || BeginBurst());
		if (wanted && !active) {
			++m_dropped;
			++m_codecFailures;
		}
		m_inBurst = active && !burstEnd;
		const std::uint64_t sentBefore = m_sent.load();
		if (active && started) {
			for (std::size_t back = preRoll; back > 0; --back) {
				SendFrame(m_gate.Frame(back), m_recentSequences[back], ptt.tier, false);
			}
		}
		if (active) {
			SendFrame(a_frame, m_sequence, ptt.tier, burstEnd);
		}
		m_transmitting.store(m_sent.load() != sentBefore);
		m_levelDbfs.store((std::max)(level.rmsDbfs, TransmitLevelFloorDbfs));

		std::scoped_lock lock(m_mutex);
		m_health.OnCallback(now, level.peakDbfs, m_pttHeld);
	}

	void Transmitter::SendFrame(const std::int16_t* a_pcm, std::uint32_t a_sequence, Tier a_tier, bool a_burstEnd)
	{
		std::array<std::uint8_t, kMaxEncodedBytes> payload{};
		const EncodeResult encoded = m_encoder.Encode(a_pcm, payload.data());
		m_codecFailed.store(encoded.codecFailed);
		if (!encoded.sendable) {
			++m_dropped;
			++m_codecFailures;
			return;
		}
		VoiceHeader header{};
		header.tier = a_tier;
		header.burstStart = m_needBurstStart;
		header.burstEnd = a_burstEnd;
		header.sequence = a_sequence;
		std::array<std::byte, kVoiceMaxDatagramBytes> datagram{};
		const auto body = std::as_bytes(std::span<const std::uint8_t>(payload.data(), static_cast<std::size_t>(encoded.bytes)));
		const std::size_t size = EncodeVoicePacket(header, body, datagram);
		if (size == 0 || !m_deps.send(std::span<const std::byte>(datagram.data(), size))) {
			++m_dropped;
			return;
		}
		m_needBurstStart = false;
		++m_sent;
	}
}
