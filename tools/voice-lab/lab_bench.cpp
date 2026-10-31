#include "lab_bench.h"
#include "F4TVoiceCodec.h"
#include "lab_stats.h"
#include "lab_wav.h"

#include <opus/opus.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace
{
	using Clock = std::chrono::steady_clock;
	using Packet = std::vector<std::uint8_t>;
	using Pcm = std::vector<std::int16_t>;

	constexpr int kFrame40 = 1920;
	constexpr unsigned kLossSeed = 20261022U;
	constexpr double kCpuCriterionPercent = 5.0;
	constexpr double kMemoryCriterionBytes = 1048576.0;
	constexpr double kMeanPayloadLow = 54.0;
	constexpr double kMeanPayloadHigh = 66.0;
	constexpr double kHistogramBinBytes = 16.0;
	constexpr std::size_t kHistogramBins = 12;
	constexpr double kFullScale = 32768.0;
	constexpr double kSpeechThresholdDbfs = -45.0;

	struct Stream
	{
		bool ok{ false };
		std::vector<Packet> packets;
		std::vector<double> encodeUs;
		std::size_t dropped{ 0 };
	};

	struct LossReport
	{
		std::vector<double> fecSnrDb;
		std::vector<double> plcSnrDb;
	};

	double MicrosSince(Clock::time_point a_start)
	{
		return std::chrono::duration<double, std::micro>(Clock::now() - a_start).count();
	}

	std::vector<bool> SpeechFrames(const Pcm& a_speech, std::size_t a_frames, int a_frameSamples)
	{
		std::vector<bool> speech(a_frames, false);
		for (std::size_t f = 0; f < a_frames; ++f)
		{
			double energy = 0.0;
			for (int n = 0; n < a_frameSamples; ++n)
			{
				const double sample = a_speech[f * a_frameSamples + n];
				energy += sample * sample;
			}
			const double rms = std::sqrt(energy / a_frameSamples);
			speech[f] = 20.0 * std::log10(rms / kFullScale) >= kSpeechThresholdDbfs;
		}
		return speech;
	}

	std::vector<double> PayloadSizes(const std::vector<Packet>& a_packets, const std::vector<bool>& a_speech, bool a_speechOnly)
	{
		std::vector<double> sizes;
		for (std::size_t f = 0; f < a_packets.size(); ++f)
		{
			if (!a_packets[f].empty() && (!a_speechOnly || a_speech[f]))
			{
				sizes.push_back(static_cast<double>(a_packets[f].size()));
			}
		}
		return sizes;
	}

	template <typename EncodeFn>
	Stream RunEncode(const Pcm& a_speech, std::size_t a_frames, int a_frameSamples, EncodeFn a_encode)
	{
		Stream stream;
		std::vector<std::uint8_t> buffer(F4T::Voice::kMaxEncodedBytes);
		stream.packets.resize(a_frames);
		for (std::size_t f = 0; f < a_frames; ++f)
		{
			const Clock::time_point start = Clock::now();
			const int bytes = a_encode(&a_speech[f * a_frameSamples], buffer.data());
			stream.encodeUs.push_back(MicrosSince(start));
			if (bytes >= 1 && bytes <= F4T::Voice::kMaxEncodedBytes)
			{
				stream.packets[f].assign(buffer.begin(), buffer.begin() + bytes);
			}
			else
			{
				++stream.dropped;
			}
		}
		stream.ok = true;
		return stream;
	}

	Stream EncodeWrapper(const Pcm& a_speech, std::size_t a_frames, int a_complexity)
	{
		F4T::Voice::Encoder encoder;
		if (!encoder.Open(a_complexity))
		{
			return {};
		}
		return RunEncode(a_speech, a_frames, F4T::Voice::kFrameSamples, [&encoder](const std::int16_t* a_pcm, std::uint8_t* a_out) {
			return encoder.Encode(a_pcm, a_out).bytes;
		});
	}

	OpusEncoder* CreateRawEncoder(int a_complexity, int a_fec)
	{
		int error = OPUS_OK;
		OpusEncoder* encoder = opus_encoder_create(F4T::Voice::kSampleRate, F4T::Voice::kChannels, OPUS_APPLICATION_VOIP, &error);
		if (!encoder || error != OPUS_OK)
		{
			opus_encoder_destroy(encoder);
			return nullptr;
		}
		if (!F4T::Voice::ConfigureEncoder(encoder, a_complexity, a_fec))
		{
			opus_encoder_destroy(encoder);
			return nullptr;
		}
		return encoder;
	}

	Stream EncodeRaw(const Pcm& a_speech, std::size_t a_frames, int a_complexity, int a_fec, int a_frameSamples)
	{
		OpusEncoder* encoder = CreateRawEncoder(a_complexity, a_fec);
		if (!encoder)
		{
			return {};
		}
		const Stream stream = RunEncode(a_speech, a_frames, a_frameSamples, [encoder, a_frameSamples](const std::int16_t* a_pcm, std::uint8_t* a_out) {
			return opus_encode(encoder, a_pcm, a_frameSamples, a_out, F4T::Voice::kMaxEncodedBytes);
		});
		opus_encoder_destroy(encoder);
		return stream;
	}

	template <typename DecodeFn>
	void TimeDecodes(const std::vector<Packet>& a_packets, int a_frameSamples, DecodeFn a_decode, std::vector<double>& a_micros)
	{
		Pcm pcm(a_frameSamples);
		for (const Packet& packet : a_packets)
		{
			if (packet.empty())
			{
				continue;
			}
			const Clock::time_point start = Clock::now();
			a_decode(packet, pcm.data());
			a_micros.push_back(MicrosSince(start));
		}
	}

	std::vector<double> TimeWrapperDecodes(const std::vector<Packet>& a_packets, std::size_t a_decoders)
	{
		std::vector<double> micros;
		for (std::size_t d = 0; d < a_decoders; ++d)
		{
			F4T::Voice::Decoder decoder;
			if (!decoder.Open())
			{
				return {};
			}
			TimeDecodes(a_packets, F4T::Voice::kFrameSamples, [&decoder](const Packet& a_packet, std::int16_t* a_pcm) {
				decoder.Decode(a_packet.data(), static_cast<int>(a_packet.size()), a_pcm);
			}, micros);
		}
		return micros;
	}

	std::vector<double> TimeRawDecodes(const std::vector<Packet>& a_packets, std::size_t a_decoders, int a_frameSamples)
	{
		std::vector<double> micros;
		for (std::size_t d = 0; d < a_decoders; ++d)
		{
			int error = OPUS_OK;
			OpusDecoder* decoder = opus_decoder_create(F4T::Voice::kSampleRate, F4T::Voice::kChannels, &error);
			if (!decoder || error != OPUS_OK)
			{
				opus_decoder_destroy(decoder);
				return {};
			}
			TimeDecodes(a_packets, a_frameSamples, [decoder, a_frameSamples](const Packet& a_packet, std::int16_t* a_pcm) {
				opus_decode(decoder, a_packet.data(), static_cast<opus_int32>(a_packet.size()), a_pcm, a_frameSamples, 0);
			}, micros);
			opus_decoder_destroy(decoder);
		}
		return micros;
	}

	double SnrDb(const std::int16_t* a_reference, const std::int16_t* a_decoded)
	{
		double signal = 0.0;
		double error = 0.0;
		for (int n = 0; n < F4T::Voice::kFrameSamples; ++n)
		{
			const double reference = a_reference[n];
			const double difference = reference - a_decoded[n];
			signal += reference * reference;
			error += difference * difference;
		}
		return 10.0 * std::log10((signal + 1e-9) / (error + 1e-9));
	}

	LossReport SimulateLoss(const std::vector<Packet>& a_packets, double a_loss)
	{
		constexpr int kFrame = F4T::Voice::kFrameSamples;
		const std::size_t frames = a_packets.size();
		LossReport report;
		Pcm reference(frames * kFrame, 0);
		std::vector<bool> lost(frames, false);
		std::mt19937 random(kLossSeed);
		std::uniform_real_distribution<double> uniform(0.0, 1.0);

		F4T::Voice::Decoder referenceDecoder;
		if (!referenceDecoder.Open())
		{
			return report;
		}
		for (std::size_t f = 0; f < frames; ++f)
		{
			lost[f] = a_packets[f].empty() || uniform(random) < a_loss;
			if (!a_packets[f].empty())
			{
				referenceDecoder.Decode(a_packets[f].data(), static_cast<int>(a_packets[f].size()), &reference[f * kFrame]);
			}
		}

		F4T::Voice::Decoder decoder;
		if (!decoder.Open())
		{
			return report;
		}
		Pcm out(kFrame);
		for (std::size_t f = 0; f < frames; ++f)
		{
			if (lost[f])
			{
				const bool recoveredByNext = f + 1 < frames && !lost[f + 1];
				if (!recoveredByNext)
				{
					decoder.Conceal(out.data());
					if (!a_packets[f].empty())
					{
						report.plcSnrDb.push_back(SnrDb(&reference[f * kFrame], out.data()));
					}
				}
				continue;
			}
			if (f > 0 && lost[f - 1])
			{
				decoder.DecodeFec(a_packets[f].data(), static_cast<int>(a_packets[f].size()), out.data());
				if (!a_packets[f - 1].empty())
				{
					report.fecSnrDb.push_back(SnrDb(&reference[(f - 1) * kFrame], out.data()));
				}
			}
			decoder.Decode(a_packets[f].data(), static_cast<int>(a_packets[f].size()), out.data());
		}
		return report;
	}

	void PrintSnrLine(const char* a_label, const std::vector<double>& a_values)
	{
		std::printf("%s: %zu frames, mean SNR %.2f dB\n", a_label, a_values.size(), VoiceLab::Mean(a_values));
	}
}

int RunBench(const BenchOptions& a_options)
{
	Pcm corpus;
	if (!LoadCorpus(a_options.wavPath, corpus))
	{
		std::fprintf(stderr, "voice-lab: cannot read 48 kHz mono 16-bit PCM WAV '%s'\n", a_options.wavPath.c_str());
		return 1;
	}

	const bool twentyMs = a_options.frameMs == 20;
	const int frameSamples = twentyMs ? F4T::Voice::kFrameSamples : kFrame40;
	const std::size_t frames = static_cast<std::size_t>(a_options.minutes * 60.0 * F4T::Voice::kSampleRate) / frameSamples;
	const std::size_t decoders = static_cast<std::size_t>(a_options.decoders);
	if (corpus.size() < frames * frameSamples)
	{
		std::fprintf(stderr, "voice-lab: corpus has %zu samples but the bench needs %zu (the corpus is never repeated)\n",
			corpus.size(), frames * frameSamples);
		return 1;
	}
	corpus.resize(frames * frameSamples);
	const double audioUs = static_cast<double>(frames) * a_options.frameMs * 1000.0;

	const Stream stream = twentyMs
		? EncodeWrapper(corpus, frames, a_options.complexity)
		: EncodeRaw(corpus, frames, a_options.complexity, 1, frameSamples);
	if (!stream.ok)
	{
		std::fprintf(stderr, "voice-lab: encoder setup failed\n");
		return 1;
	}
	const std::vector<double> decodeUs = twentyMs
		? TimeWrapperDecodes(stream.packets, decoders)
		: TimeRawDecodes(stream.packets, decoders, frameSamples);
	if (decodeUs.empty())
	{
		std::fprintf(stderr, "voice-lab: decoder setup failed\n");
		return 1;
	}

	const std::vector<bool> isSpeech = SpeechFrames(corpus, frames, frameSamples);
	const std::vector<double> payloads = PayloadSizes(stream.packets, isSpeech, false);
	const double payloadMean = VoiceLab::Mean(payloads);
	const double payloadMax = VoiceLab::Max(payloads);
	const double payloadP99 = VoiceLab::Percentile(payloads, 0.99);
	const std::vector<double> speechPayloads = PayloadSizes(stream.packets, isSpeech, true);
	const double speechMean = VoiceLab::Mean(speechPayloads);
	const double speechMax = VoiceLab::Max(speechPayloads);
	const double speechP99 = VoiceLab::Percentile(speechPayloads, 0.99);

	const int encoderSize = opus_encoder_get_size(1);
	const int decoderSize = opus_decoder_get_size(1);
	const double memoryBytes = static_cast<double>(encoderSize) + static_cast<double>(decoders) * decoderSize;
	const double encodeTotal = VoiceLab::Sum(stream.encodeUs);
	const double decodeTotal = VoiceLab::Sum(decodeUs);
	const double cpuPercent = 100.0 * (encodeTotal + decodeTotal) / audioUs;

	std::printf("voice-lab bench: frame %d ms (%d samples), complexity %d, decoders %zu, loss %.3f\n",
		a_options.frameMs, frameSamples, a_options.complexity, decoders, a_options.loss);
	std::printf("audio %.1f s, frames %zu, cap failures (dropped ticks) %zu\n", audioUs / 1e6, frames, stream.dropped);
	std::printf("opus_encoder_get_size(1) = %d bytes\n", encoderSize);
	std::printf("opus_decoder_get_size(1) = %d bytes\n", decoderSize);
	std::printf("memory 1 encoder + %zu decoders = %.0f bytes\n", decoders, memoryBytes);
	std::printf("payload all frames: mean %.2f max %.0f p99 %.0f count %zu\n",
		payloadMean, payloadMax, payloadP99, payloads.size());
	std::printf("payload speech frames (input >= -45 dBFS): mean %.2f max %.0f p99 %.0f count %zu\n",
		speechMean, speechMax, speechP99, speechPayloads.size());
	const std::vector<std::size_t> histogram = VoiceLab::Histogram(payloads, kHistogramBinBytes, kHistogramBins);
	for (std::size_t bin = 0; bin < histogram.size(); ++bin)
	{
		std::printf("  payload %3.0f-%3.0f B: %zu\n", bin * kHistogramBinBytes, (bin + 1) * kHistogramBinBytes - 1.0, histogram[bin]);
	}
	std::printf("encode us per frame: mean %.1f p99 %.1f\n",
		VoiceLab::Mean(stream.encodeUs), VoiceLab::Percentile(stream.encodeUs, 0.99));
	std::printf("decode us per call: mean %.1f p99 %.1f over %zu calls\n",
		VoiceLab::Mean(decodeUs), VoiceLab::Percentile(decodeUs, 0.99), decodeUs.size());
	std::printf("cpu percent of one core (1 encoder + %zu decoders): %.2f\n", decoders, cpuPercent);

	if (twentyMs)
	{
		const Stream fecOff = EncodeRaw(corpus, frames, a_options.complexity, 0, frameSamples);
		const std::vector<double> fecOffPayloads = PayloadSizes(fecOff.packets, isSpeech, false);
		const std::vector<double> fecOffSpeech = PayloadSizes(fecOff.packets, isSpeech, true);
		std::printf("FEC on mean payload all frames %.2f B, FEC off %.2f B; speech frames FEC on %.2f B, FEC off %.2f B\n",
			payloadMean, VoiceLab::Mean(fecOffPayloads), speechMean, VoiceLab::Mean(fecOffSpeech));
		const LossReport report = SimulateLoss(stream.packets, a_options.loss);
		PrintSnrLine("FEC recovered frames", report.fecSnrDb);
		PrintSnrLine("PLC concealed frames", report.plcSnrDb);
	}
	else
	{
		std::printf("FEC on/off and loss SNR are measured at 20 ms only\n");
	}

	VoiceLab::PrintCriterion("cpu <= 5% of one core", cpuPercent <= kCpuCriterionPercent);
	VoiceLab::PrintCriterion("memory 1 encoder + N decoders < 1 MB", memoryBytes < kMemoryCriterionBytes);
	if (twentyMs)
	{
		VoiceLab::PrintCriterion("mean payload 54-66 B (speech frames)", speechMean >= kMeanPayloadLow && speechMean <= kMeanPayloadHigh);
		VoiceLab::PrintCriterion("every tick encoded to 1-160 B", stream.dropped == 0);
	}
	return 0;
}
