#include "lab_loop.h"
#include "F4TVoiceWasapiCapture.h"
#include "F4TVoiceWasapiRender.h"
#include "lab_stats.h"
#include "lab_wav.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace VoiceLab::Loop
{
	namespace
	{
		constexpr std::uint64_t kFrame = F4T::Voice::kFrameSamples;
		constexpr std::uint64_t kQuietSamples = F4T::Voice::kSampleRate / 2;
		constexpr std::size_t kMinClicks = 10;
		constexpr float kFullScale = 32768.0f;
		constexpr double kSampleRateHz = F4T::Voice::kSampleRate;

		double QpcSeconds()
		{
			LARGE_INTEGER frequency{};
			LARGE_INTEGER count{};
			QueryPerformanceFrequency(&frequency);
			QueryPerformanceCounter(&count);
			return static_cast<double>(count.QuadPart) / static_cast<double>(frequency.QuadPart);
		}

		double Now(const Pipeline& a_pipeline)
		{
			return a_pipeline.options->simulate ? a_pipeline.simSeconds : QpcSeconds();
		}

		std::uint64_t MarkPosition(const Mark& a_mark)
		{
			return a_mark.frame * kFrame + a_mark.index;
		}

		void DetectClick(Pipeline& a_pipeline, std::int16_t a_sample, double a_now)
		{
			if (std::abs(static_cast<int>(a_sample)) <= a_pipeline.thresholdAbs)
			{
				++a_pipeline.quietSamples;
				return;
			}
			if (a_pipeline.quietSamples >= kQuietSamples && a_pipeline.marksCount < kMaxClicks)
			{
				Mark& mark = a_pipeline.marks[a_pipeline.marksCount];
				mark.frame = a_pipeline.frames;
				mark.index = a_pipeline.pendingCount;
				mark.captureSec = a_now;
				++a_pipeline.marksCount;
				a_pipeline.marksPublished.store(a_pipeline.marksCount, std::memory_order_release);
			}
			a_pipeline.quietSamples = 0;
		}

		void PushFrame(Pipeline& a_pipeline, const std::int16_t* a_pcm)
		{
			const std::uint64_t write = a_pipeline.writeIndex.load(std::memory_order_relaxed);
			const std::uint64_t read = a_pipeline.readIndex.load(std::memory_order_acquire);
			if (write + kFrame - read > kRingCapacity)
			{
				++a_pipeline.overflowFrames;
				return;
			}
			for (std::uint64_t i = 0; i < kFrame; ++i)
			{
				a_pipeline.ring[(write + i) % kRingCapacity] = a_pcm[i];
			}
			a_pipeline.writeIndex.store(write + kFrame, std::memory_order_release);
		}

		void ProcessFrame(Pipeline& a_pipeline)
		{
			std::uint8_t packet[F4T::Voice::kMaxEncodedBytes]{};
			std::int16_t pcm[F4T::Voice::kFrameSamples]{};
			const F4T::Voice::EncodeResult encoded = a_pipeline.encoder.Encode(a_pipeline.pending, packet);
			int samples = 0;
			if (encoded.sendable)
			{
				const auto bytes = static_cast<std::uint32_t>(encoded.bytes);
				++a_pipeline.encoded;
				a_pipeline.payloadSum += encoded.bytes;
				if (bytes > a_pipeline.payloadMax)
				{
					a_pipeline.payloadMax = bytes;
				}
				samples = a_pipeline.decoder.Decode(packet, encoded.bytes, pcm);
			}
			if (samples == F4T::Voice::kFrameSamples)
			{
				++a_pipeline.decoded;
			}
			else
			{
				++a_pipeline.concealed;
				a_pipeline.decoder.Conceal(pcm);
			}
			PushFrame(a_pipeline, pcm);
			++a_pipeline.frames;
		}

		void OnTick(void*) {}

		void OnError(void* a_user, long a_hresult)
		{
			Pipeline& pipeline = *static_cast<Pipeline*>(a_user);
			pipeline.hresult.store(a_hresult);
			pipeline.failed.store(true);
		}

		void OnReopen(void*) {}

		bool ParseOptions(int a_argc, char** a_argv, Options& a_options)
		{
			for (int i = 2; i + 1 < a_argc; i += 2)
			{
				const std::string flag = a_argv[i];
				const char* value = a_argv[i + 1];
				if (flag == "--simulate")
				{
					a_options.simulate = true;
					a_options.wavPath = value;
				}
				else if (flag == "--seconds")
				{
					a_options.seconds = std::atof(value);
				}
				else if (flag == "--complexity")
				{
					a_options.complexity = std::atoi(value);
				}
				else if (flag == "--threshold-dbfs")
				{
					a_options.thresholdDbfs = std::atof(value);
				}
				else
				{
					return false;
				}
			}
			return (a_argc - 2) % 2 == 0 && a_options.seconds > 0.0 && a_options.complexity >= 0 &&
				a_options.complexity <= 10 && a_options.thresholdDbfs < 0.0 &&
				(!a_options.simulate || !a_options.wavPath.empty());
		}

		int RunDevice(Pipeline& a_pipeline, const Options& a_options)
		{
			F4T::Voice::WasapiRender render;
			F4T::Voice::WasapiCapture capture;
			if (!render.Start(OnFill, OnError, &a_pipeline))
			{
				std::fprintf(stderr, "voice-lab: render stream did not start\n");
				return 1;
			}
			if (!capture.Start(OnSamples, OnTick, OnError, OnReopen, &a_pipeline))
			{
				std::fprintf(stderr, "voice-lab: capture stream did not start\n");
				render.Stop();
				return 1;
			}
			const double start = QpcSeconds();
			while (!a_pipeline.failed.load() && QpcSeconds() - start < a_options.seconds)
			{
				Sleep(20);
			}
			capture.Stop();
			render.Stop();
			return PrintReport(a_pipeline, a_options) ? 0 : 1;
		}
	}

	bool Prepare(Pipeline& a_pipeline, const Options& a_options)
	{
		a_pipeline.options = &a_options;
		a_pipeline.thresholdAbs = kFullScale * std::pow(10.0, a_options.thresholdDbfs / 20.0);
		a_pipeline.marks.assign(kMaxClicks, Mark{});
		return a_pipeline.encoder.Open(a_options.complexity) && a_pipeline.decoder.Open();
	}

	void OnSamples(void* a_user, const std::int16_t* a_mono, std::uint32_t a_frames)
	{
		Pipeline& pipeline = *static_cast<Pipeline*>(a_user);
		const double now = Now(pipeline);
		for (std::uint32_t i = 0; i < a_frames; ++i)
		{
			if (pipeline.options->latency)
			{
				DetectClick(pipeline, a_mono[i], now);
			}
			pipeline.pending[pipeline.pendingCount++] = a_mono[i];
			if (pipeline.pendingCount == F4T::Voice::kFrameSamples)
			{
				ProcessFrame(pipeline);
				pipeline.pendingCount = 0;
			}
		}
	}

	void OnFill(void* a_user, float* a_stereo, std::uint32_t a_frames)
	{
		Pipeline& pipeline = *static_cast<Pipeline*>(a_user);
		const double now = Now(pipeline);
		const std::uint64_t written = pipeline.writeIndex.load(std::memory_order_acquire);
		const std::size_t published = pipeline.marksPublished.load(std::memory_order_acquire);
		std::uint64_t read = pipeline.readIndex.load(std::memory_order_relaxed);
		for (std::uint32_t i = 0; i < a_frames; ++i)
		{
			float value = 0.0f;
			if (read < written)
			{
				if (pipeline.nextMark < published && MarkPosition(pipeline.marks[pipeline.nextMark]) == read)
				{
					pipeline.marks[pipeline.nextMark].renderSec = now + static_cast<double>(i) / kSampleRateHz;
					++pipeline.nextMark;
				}
				value = static_cast<float>(pipeline.ring[read % kRingCapacity]) / kFullScale;
				++read;
			}
			else
			{
				++pipeline.underflowFrames;
			}
			a_stereo[2 * i] = value;
			a_stereo[2 * i + 1] = value;
		}
		pipeline.readIndex.store(read, std::memory_order_release);
	}

	bool PrintReport(const Pipeline& a_pipeline, const Options& a_options)
	{
		const char* mode = a_options.simulate ? "simulated" : "device";
		const bool failed = a_pipeline.failed.load();
		if (failed)
		{
			std::printf("onError hresult 0x%08lX\n", static_cast<unsigned long>(a_pipeline.hresult.load()));
		}
		else
		{
			std::printf("onError: none\n");
		}
		if (!a_options.latency)
		{
			std::printf("voice-lab loop (%s): %.1f s, complexity %d\n", mode, a_options.seconds, a_options.complexity);
			std::printf("frames captured %llu, encoded %llu, decoded %llu, concealed %llu\n", a_pipeline.frames,
				a_pipeline.encoded, a_pipeline.decoded, a_pipeline.concealed);
			const double mean = a_pipeline.encoded == 0 ? 0.0 : a_pipeline.payloadSum / static_cast<double>(a_pipeline.encoded);
			std::printf("payload bytes: mean %.2f max %u over %llu encoded frames\n", mean, a_pipeline.payloadMax,
				a_pipeline.encoded);
			std::printf("render underflow frames %llu, ring overflow frames %llu\n", a_pipeline.underflowFrames,
				a_pipeline.overflowFrames);
			return !failed;
		}

		std::vector<double> latencies;
		for (std::size_t i = 0; i < a_pipeline.marksCount; ++i)
		{
			const Mark& mark = a_pipeline.marks[i];
			if (mark.renderSec >= 0.0)
			{
				latencies.push_back((mark.renderSec - mark.captureSec) * 1000.0);
			}
		}
		const double p95 = VoiceLab::Percentile(latencies, 0.95);
		std::printf("voice-lab latency (%s): %.1f s, threshold %.1f dBFS\n", mode, a_options.seconds, a_options.thresholdDbfs);
		std::printf("clicks detected %zu, rendered %zu, ring overflow frames %llu\n", a_pipeline.marksCount, latencies.size(),
			a_pipeline.overflowFrames);
		std::printf("latency ms: count %zu p50 %.2f p95 %.2f max %.2f\n", latencies.size(), VoiceLab::Percentile(latencies, 0.50),
			p95, VoiceLab::Max(latencies));
		std::printf("measurement excludes device latency outside the shared-mode buffers; criterion adds 20 ms (two 10 ms buffers)\n");
		const bool pass = latencies.size() >= kMinClicks && p95 + 20.0 <= 160.0 && a_pipeline.overflowFrames == 0;
		VoiceLab::PrintCriterion("latency p95 + 20 ms <= 160 ms with no ring overflow", pass);
		return !failed && pass;
	}

	namespace
	{
		constexpr std::size_t kTickSamples = 480;
		constexpr double kTickMs = 10.0;
		constexpr double kFrameMs = static_cast<double>(kFrame) * 1000.0 / kSampleRateHz;
		constexpr std::size_t kClickSamples = 96;
		constexpr std::size_t kClickSpacing = 48000;
		constexpr std::size_t kOnsetSpread = 2 * kTickSamples;
		constexpr std::size_t kOnsetStep = 97;
		constexpr std::size_t kQuietBefore = 24000;
		constexpr std::size_t kTailSamples = 9600;
		constexpr std::size_t kWarmupTicks = 4;
		constexpr double kToleranceMs = 1.0;
		constexpr std::int16_t kClickHigh = 32767;
		constexpr std::int16_t kClickLow = -32768;

		void InjectClicks(std::vector<std::int16_t>& a_pcm, std::vector<std::uint64_t>& a_onsets)
		{
			std::int16_t* base = a_pcm.data();
			for (std::size_t k = 1; k * kClickSpacing + kClickSamples + kTailSamples <= a_pcm.size(); ++k)
			{
				const std::size_t onset = k * kClickSpacing + (k * kOnsetStep) % kOnsetSpread;
				std::fill(base + onset - kQuietBefore, base + onset, std::int16_t{ 0 });
				for (std::size_t j = 0; j < kClickSamples; ++j)
				{
					base[onset + j] = (j % 2 == 0) ? kClickHigh : kClickLow;
				}
				a_onsets.push_back(onset);
			}
		}

		double SimulatedDelayMs(std::uint64_t a_onset)
		{
			return kFrameMs + static_cast<double>(a_onset % kTickSamples) * 1000.0 / kSampleRateHz;
		}

		bool CheckLoop(std::uint64_t a_underflowAfterWarmup)
		{
			const bool pass = a_underflowAfterWarmup == 0;
			std::printf("simulated render underflow frames after 40 ms: %llu\n", a_underflowAfterWarmup);
			std::printf("simulated loop check (0 underflow after 40 ms): %s\n", pass ? "PASS" : "FAIL");
			return pass;
		}

		bool CheckLatency(const Pipeline& a_pipeline, const std::vector<std::uint64_t>& a_onsets)
		{
			double lowMs = (std::numeric_limits<double>::max)();
			double highMs = 0.0;
			for (const std::uint64_t onset : a_onsets)
			{
				lowMs = (std::min)(lowMs, SimulatedDelayMs(onset));
				highMs = (std::max)(highMs, SimulatedDelayMs(onset));
			}
			bool matched = !a_onsets.empty() && a_pipeline.marksCount == a_onsets.size();
			double worstMs = 0.0;
			for (std::size_t i = 0; matched && i < a_onsets.size(); ++i)
			{
				const Mark& mark = a_pipeline.marks[i];
				const std::uint64_t position = MarkPosition(mark);
				matched = mark.renderSec >= 0.0 && position == a_onsets[i];
				if (matched)
				{
					const double measured = (mark.renderSec - mark.captureSec) * 1000.0;
					worstMs = (std::max)(worstMs, std::fabs(measured - SimulatedDelayMs(a_onsets[i])));
				}
			}
			const bool pass = matched && worstMs <= kToleranceMs;
			std::printf("simulated pipeline delay per click: %.2f to %.2f ms (one 20 ms frame of accumulation plus the onset offset inside its 10 ms block)\n",
				lowMs, highMs);
			std::printf("simulated clicks injected %zu, detected %zu, positions matched %s, max |measured - simulated| %.4f ms\n",
				a_onsets.size(), a_pipeline.marksCount, matched ? "yes" : "no", worstMs);
			std::printf("simulated latency check (every click within 1 ms): %s\n", pass ? "PASS" : "FAIL");
			return pass;
		}
	}

	int RunSimulated(Pipeline& a_pipeline, const Options& a_options)
	{
		std::vector<std::int16_t> pcm;
		if (!LoadCorpus(a_options.wavPath, pcm))
		{
			std::fprintf(stderr, "voice-lab: cannot read 48 kHz mono 16-bit PCM WAV '%s'\n", a_options.wavPath.c_str());
			return 1;
		}
		const auto ticks = static_cast<std::size_t>(a_options.seconds * 1000.0 / kTickMs);
		const std::size_t total = ticks * kTickSamples;
		if (pcm.size() < total)
		{
			std::fprintf(stderr, "voice-lab: corpus has %zu samples but the simulation needs %zu\n", pcm.size(), total);
			return 1;
		}
		pcm.resize(total);
		std::vector<std::uint64_t> onsets;
		InjectClicks(pcm, onsets);

		std::vector<float> stereo(2 * kTickSamples);
		std::uint64_t underflowAtWarmup = 0;
		for (std::size_t n = 0; n < ticks; ++n)
		{
			a_pipeline.simSeconds = static_cast<double>(n) * kTickMs / 1000.0;
			OnSamples(&a_pipeline, pcm.data() + n * kTickSamples, kTickSamples);
			a_pipeline.simSeconds = static_cast<double>(n + 1) * kTickMs / 1000.0;
			OnFill(&a_pipeline, stereo.data(), kTickSamples);
			if (n + 1 == kWarmupTicks)
			{
				underflowAtWarmup = a_pipeline.underflowFrames;
			}
		}

		const bool reported = PrintReport(a_pipeline, a_options);
		const bool clean = !a_pipeline.failed.load() && a_pipeline.overflowFrames == 0 && reported;
		const bool checked = a_options.latency ? CheckLatency(a_pipeline, onsets)
			: CheckLoop(a_pipeline.underflowFrames - underflowAtWarmup);
		return clean && checked ? 0 : 1;
	}
}

int RunLoop(int a_argc, char** a_argv, bool a_latency)
{
	VoiceLab::Loop::Options options;
	options.latency = a_latency;
	if (!VoiceLab::Loop::ParseOptions(a_argc, a_argv, options))
	{
		std::fprintf(stderr, "voice-lab: bad arguments for %s\n", a_argv[1]);
		return 2;
	}
	const auto pipeline = std::make_unique<VoiceLab::Loop::Pipeline>();
	if (!VoiceLab::Loop::Prepare(*pipeline, options))
	{
		std::fprintf(stderr, "voice-lab: encoder or decoder setup failed\n");
		return 1;
	}
	if (options.simulate)
	{
		return VoiceLab::Loop::RunSimulated(*pipeline, options);
	}
	return VoiceLab::Loop::RunDevice(*pipeline, options);
}
