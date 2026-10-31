#include "lab_loss.h"

#include "F4TVoiceCodec.h"
#include "F4TVoiceJitter.h"
#include "F4TVoiceWire.h"
#include "lab_relay.h"
#include "lab_wav.h"

#include <psapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace VoiceLab::Loss
{
	namespace
	{
		using Pcm = std::vector<std::int16_t>;

		constexpr std::uint16_t kRelayPort = 47110;
		constexpr std::uint16_t kReceiverPort = 47111;
		constexpr int kTickMs = 20;
		constexpr int kTicksPerSecond = 1000 / kTickMs;
		constexpr int kStartLeadMs = 200;
		constexpr int kComplexity = 5;
		constexpr int kSpeakerId = 1;
		constexpr std::uint32_t kFirstSequence = 1;
		constexpr std::size_t kReceiveBytes = 512;

		constexpr Profile kProfiles[] = {
			{ 'a', { 5.0, 0, 0, 0.0 }, true, false, false, false },
			{ 'b', { 15.0, 0, 0, 0.0 }, false, true, false, false },
			{ 'c', { 0.0, 0, 100, 0.0 }, false, false, true, false },
			{ 'd', { 0.0, 150, 20, 0.0 }, false, false, false, true },
			{ 'e', { 5.0, 0, 100, 0.0 }, true, false, true, false },
		};

		const Profile* FindProfile(char a_letter)
		{
			for (const Profile& profile : kProfiles)
			{
				if (profile.letter == a_letter)
				{
					return &profile;
				}
			}
			return nullptr;
		}

		bool ParseLoss(int a_argc, char** a_argv, LossOptions& a_options)
		{
			if (a_argc < 3 || (a_argc - 3) % 2 != 0)
			{
				return false;
			}
			a_options.wavPath = a_argv[2];
			for (int i = 3; i < a_argc; i += 2)
			{
				const std::string flag = a_argv[i];
				const char* value = a_argv[i + 1];
				if (flag == "--profile" && std::strlen(value) == 1)
				{
					a_options.profile = value[0];
				}
				else if (flag == "--seconds")
				{
					a_options.seconds = std::atoi(value);
				}
				else if (flag == "--warmup")
				{
					a_options.warmupSeconds = std::atoi(value);
				}
				else if (flag == "--seed")
				{
					a_options.seed = static_cast<std::uint32_t>(std::strtoul(value, nullptr, 10));
				}
				else
				{
					return false;
				}
			}
			return FindProfile(a_options.profile) != nullptr && a_options.seconds >= 1 && a_options.warmupSeconds >= 0 &&
				a_options.warmupSeconds < a_options.seconds;
		}

		MemorySample SampleMemory()
		{
			PROCESS_MEMORY_COUNTERS_EX counters{};
			counters.cb = static_cast<DWORD>(sizeof(counters));
			if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
			{
				return {};
			}
			return MemorySample{ counters.PrivateUsage, counters.WorkingSetSize };
		}

		void RunSender(Session& a_session, SOCKET a_socket, SenderTotals& a_totals)
		{
			F4T::Voice::Encoder encoder;
			a_totals.encoderOk = encoder.Open(kComplexity);
			if (!a_totals.encoderOk)
			{
				return;
			}
			const sockaddr_in relay = VoiceLab::LoopbackAddress(kRelayPort);
			std::array<std::uint8_t, F4T::Voice::kMaxEncodedBytes> coded{};
			std::array<std::byte, F4T::Voice::kVoiceMaxDatagramBytes> datagram{};
			std::uint32_t sequence = kFirstSequence;
			bool first = true;
			for (int tick = 0; tick < a_session.ticks; ++tick)
			{
				std::this_thread::sleep_until(a_session.start + std::chrono::milliseconds(tick * kTickMs));
				const std::int16_t* frame = a_session.pcm->data() + (static_cast<std::size_t>(tick) % a_session.frames) * F4T::Voice::kFrameSamples;
				const F4T::Voice::EncodeResult result = encoder.Encode(frame, coded.data());
				a_session.sendNs[static_cast<std::size_t>(tick)].store(Clock::now().time_since_epoch().count(), std::memory_order_relaxed);
				if (result.sendable)
				{
					F4T::Voice::VoiceHeader header{};
					header.tier = F4T::Voice::Tier::Normal;
					header.burstStart = first;
					header.sequence = sequence;
					const auto payload = std::span<const std::byte>(reinterpret_cast<const std::byte*>(coded.data()), static_cast<std::size_t>(result.bytes));
					const std::size_t size = F4T::Voice::EncodeVoicePacket(header, payload, datagram);
					first = false;
					const int sent = ::sendto(a_socket, reinterpret_cast<const char*>(datagram.data()), static_cast<int>(size), 0, reinterpret_cast<const sockaddr*>(&relay), static_cast<int>(sizeof(relay)));
					if (size != 0 && sent == static_cast<int>(size))
					{
						++a_totals.sent;
					}
					else
					{
						++a_totals.sendFailed;
					}
				}
				else
				{
					++a_totals.codecDropped;
				}
				sequence = F4T::Voice::NextTick(sequence);
			}
		}

		class Receiver
		{
		public:
			Receiver(const Session& a_session, SOCKET a_socket, ReceiverTotals& a_totals) :
				m_session(a_session),
				m_socket(a_socket),
				m_totals(a_totals)
			{
			}

			void Run()
			{
				m_totals.decoderOk = m_decoder.Open();
				if (!m_totals.decoderOk)
				{
					return;
				}
				int tick = 0;
				while (tick < m_session.ticks)
				{
					const auto deadline = m_session.start + std::chrono::milliseconds(tick * kTickMs);
					const auto now = Clock::now();
					if (now >= deadline)
					{
						Drain();
						PlayoutTick(tick, Clock::now());
						++tick;
					}
					else if (WaitReadable(deadline - now))
					{
						Drain();
					}
				}
				m_totals.underrunsFinal = m_underrunBase + m_lastUnderruns;
				m_totals.bufferedFinal = BufferedFrames();
			}

		private:
			double Seconds(Clock::time_point a_time) const
			{
				return std::chrono::duration<double>(a_time - m_session.start).count();
			}

			bool WaitReadable(Clock::duration a_wait)
			{
				const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(a_wait).count();
				timeval timeout{};
				timeout.tv_sec = static_cast<long>(micros / 1000000);
				timeout.tv_usec = static_cast<long>(micros % 1000000);
				fd_set readable{};
				FD_SET(m_socket, &readable);
				return ::select(0, &readable, nullptr, nullptr, &timeout) > 0;
			}

			void Drain()
			{
				for (;;)
				{
					const int size = ::recv(m_socket, m_buffer.data(), static_cast<int>(m_buffer.size()), 0);
					if (size == SOCKET_ERROR)
					{
						return;
					}
					Datagram(std::span<const std::byte>(reinterpret_cast<const std::byte*>(m_buffer.data()), static_cast<std::size_t>(size)), Clock::now());
				}
			}

			std::size_t BufferedFrames() const
			{
				for (const auto& slot : m_slots.slots)
				{
					if (slot.used && slot.speakerId == kSpeakerId)
					{
						return slot.jitter.count;
					}
				}
				return 0;
			}

			void Datagram(std::span<const std::byte> a_datagram, Clock::time_point a_now)
			{
				++m_totals.datagrams;
				F4T::Voice::VoicePacket packet{};
				if (F4T::Voice::DecodeVoicePacket(a_datagram, F4T::Voice::VoiceDirection::ClientToServer, packet) != F4T::Voice::VoiceDecodeResult::Ok)
				{
					++m_totals.malformed;
					return;
				}
				const std::uint32_t sequence = packet.header.sequence;
				if (F4T::Voice::Check(m_window, sequence) != F4T::Voice::SequenceVerdict::Accept)
				{
					++m_totals.windowRejected;
					return;
				}
				const bool full = BufferedFrames() == F4T::Voice::JitterMaxFrames;
				if (!F4T::Voice::Accept(m_slots, kSpeakerId, sequence, packet.payload, packet.header.burstStart, packet.header.burstEnd, Seconds(a_now)))
				{
					++m_totals.jitterRejected;
					return;
				}
				F4T::Voice::Commit(m_window, sequence);
				++m_totals.accepted;
				if (full)
				{
					++m_totals.overflowDiscards;
				}
			}

			void PlayoutTick(int a_tick, Clock::time_point a_now)
			{
				const double nowSeconds = Seconds(a_now);
				const std::size_t buffered = BufferedFrames();
				F4T::Voice::Expire(m_slots, nowSeconds);
				m_totals.expiredFrames += buffered - BufferedFrames();
				F4T::Voice::VoiceSlot& slot = F4T::Voice::Admit(m_slots, kSpeakerId, nowSeconds);
				if (slot.admittedSeconds != m_admittedSeconds)
				{
					m_underrunBase += m_lastUnderruns;
					m_lastUnderruns = 0;
					m_admittedSeconds = slot.admittedSeconds;
				}
				if (a_tick == m_session.warmupTicks)
				{
					m_totals.underrunsAtWarmup = m_underrunBase + m_lastUnderruns;
					m_totals.memoryWarmup = SampleMemory();
				}
				const std::uint32_t drainsBefore = slot.jitter.drainEvents;
				const std::size_t framesBefore = slot.jitter.count;
				const F4T::Voice::Decision decision = F4T::Voice::Next(slot.jitter, nowSeconds);
				m_totals.drainEvents += slot.jitter.drainEvents - drainsBefore;
				m_totals.drainedFrames += framesBefore - slot.jitter.count - (decision.kind == F4T::Voice::DecisionKind::Play ? 1U : 0U);
				m_lastUnderruns = slot.jitter.underrunEvents;
				if (decision.resetDecoder)
				{
					m_decoder.Reset();
				}
				const auto* bytes = reinterpret_cast<const std::uint8_t*>(decision.payload.data());
				const int size = static_cast<int>(decision.payload.size());
				switch (decision.kind)
				{
				case F4T::Voice::DecisionKind::Play:
					CountDecode(m_decoder.Decode(bytes, size, m_pcm.data()));
					++m_totals.played;
					RecordLatency(decision.sequence, a_now);
					break;
				case F4T::Voice::DecisionKind::Fec:
					CountDecode(m_decoder.DecodeFec(bytes, size, m_pcm.data()));
					++m_totals.fec;
					RecordLatency(decision.sequence, a_now);
					break;
				case F4T::Voice::DecisionKind::Conceal:
					CountDecode(m_decoder.Conceal(m_pcm.data()));
					++m_totals.conceal;
					break;
				case F4T::Voice::DecisionKind::Silence:
					m_pcm.fill(0);
					++m_totals.silence;
					break;
				default:
					++m_totals.noneYet;
					break;
				}
				if (a_tick >= m_session.warmupTicks)
				{
					RecordDepth(slot.jitter.depthFrames);
					RecordLag(slot.jitter);
				}
			}

			void CountDecode(int a_samples)
			{
				if (a_samples != F4T::Voice::kFrameSamples)
				{
					++m_totals.decodeErrors;
				}
			}

			void RecordDepth(std::uint32_t a_depthFrames)
			{
				const int depth = static_cast<int>(a_depthFrames) * kTickMs;
				m_totals.depthMinMs = (std::min)(m_totals.depthMinMs, depth);
				m_totals.depthMaxMs = (std::max)(m_totals.depthMaxMs, depth);
				m_totals.depthFinalMs = depth;
			}

			void RecordLag(const F4T::Voice::JitterBuffer& a_jitter)
			{
				if (!a_jitter.playing)
				{
					return;
				}
				const std::uint32_t ticks = a_jitter.count == 0 ? 0U : F4T::Voice::TickDistance(a_jitter.cursor, a_jitter.frames[a_jitter.count - 1U].sequence);
				m_totals.lagMs.push_back(static_cast<double>(ticks) * kTickMs);
			}

			void RecordLatency(std::uint32_t a_sequence, Clock::time_point a_now)
			{
				const std::uint32_t index = F4T::Voice::TickDistance(kFirstSequence, a_sequence);
				if (index >= static_cast<std::uint32_t>(m_session.ticks))
				{
					return;
				}
				const auto sent = Clock::time_point(Clock::duration(m_session.sendNs[index].load(std::memory_order_relaxed)));
				m_totals.latencyMs.push_back(std::chrono::duration<double, std::milli>(a_now - sent).count());
			}

			const Session& m_session;
			SOCKET m_socket;
			ReceiverTotals& m_totals;
			F4T::Voice::Decoder m_decoder{};
			F4T::Voice::SequenceWindow m_window{};
			F4T::Voice::VoiceSlots m_slots{};
			std::uint64_t m_underrunBase{};
			std::uint64_t m_lastUnderruns{};
			double m_admittedSeconds{ -1.0 };
			std::array<std::int16_t, F4T::Voice::kFrameSamples> m_pcm{};
			std::array<char, kReceiveBytes> m_buffer{};
		};

		void CloseSockets(SOCKET a_first, SOCKET a_second)
		{
			if (a_first != INVALID_SOCKET)
			{
				::closesocket(a_first);
			}
			if (a_second != INVALID_SOCKET)
			{
				::closesocket(a_second);
			}
			VoiceLab::StopNetworking();
		}

		std::uint64_t DrainLate(SOCKET a_socket)
		{
			std::array<char, kReceiveBytes> buffer{};
			std::uint64_t count{};
			while (::recv(a_socket, buffer.data(), static_cast<int>(buffer.size()), 0) != SOCKET_ERROR)
			{
				++count;
			}
			return count;
		}
	}

	int RunLoss(int a_argc, char** a_argv)
	{
		LossOptions options;
		if (!ParseLoss(a_argc, a_argv, options))
		{
			std::fprintf(stderr, "voice-lab loss: bad arguments (usage: run voice-lab with no arguments)\n");
			return 2;
		}
		const Profile& profile = *FindProfile(options.profile);
		Pcm pcm;
		if (!LoadCorpus(options.wavPath, pcm) || pcm.size() < F4T::Voice::kFrameSamples)
		{
			std::fprintf(stderr, "voice-lab: cannot read 48 kHz mono 16-bit PCM WAV '%s' with at least one frame\n", options.wavPath.c_str());
			return 2;
		}
		if (!VoiceLab::StartNetworking())
		{
			return 1;
		}
		VoiceLab::Relay relay(VoiceLab::RelayConfig{ kRelayPort, kReceiverPort, profile.impairment, VoiceLab::Impairment{}, options.seed });
		const SOCKET receiverSocket = VoiceLab::OpenLoopbackUdp(kReceiverPort);
		const SOCKET senderSocket = VoiceLab::OpenLoopbackUdp(0);
		if (receiverSocket == INVALID_SOCKET || senderSocket == INVALID_SOCKET || !relay.Start())
		{
			std::fprintf(stderr, "voice-lab loss: cannot open the loopback sockets or start the relay (ports %u and %u)\n", kRelayPort, kReceiverPort);
			CloseSockets(receiverSocket, senderSocket);
			return 1;
		}

		Session session;
		session.start = Clock::now() + std::chrono::milliseconds(kStartLeadMs);
		session.ticks = options.seconds * kTicksPerSecond;
		session.warmupTicks = options.warmupSeconds * kTicksPerSecond;
		session.pcm = &pcm;
		session.frames = pcm.size() / F4T::Voice::kFrameSamples;
		session.sendNs = std::vector<std::atomic<Clock::rep>>(static_cast<std::size_t>(session.ticks));
		SenderTotals sender;
		ReceiverTotals receiver;
		receiver.latencyMs.reserve(static_cast<std::size_t>(session.ticks));
		receiver.lagMs.reserve(static_cast<std::size_t>(session.ticks));

		std::thread senderThread([&session, senderSocket, &sender] { RunSender(session, senderSocket, sender); });
		std::thread receiverThread([&session, receiverSocket, &receiver] { Receiver(session, receiverSocket, receiver).Run(); });
		senderThread.join();
		receiverThread.join();
		relay.Stop();
		receiver.lateDatagrams = DrainLate(receiverSocket);
		const VoiceLab::RelayCounters relayCounters = relay.Counters();
		const MemorySample memoryEnd = SampleMemory();
		CloseSockets(receiverSocket, senderSocket);

		if (!sender.encoderOk || !receiver.decoderOk)
		{
			std::fprintf(stderr, "voice-lab loss: codec open failed (encoder %d decoder %d)\n", sender.encoderOk ? 1 : 0, receiver.decoderOk ? 1 : 0);
			return 1;
		}
		return Summarize(options, profile, session, sender, receiver, relayCounters, memoryEnd);
	}
}
