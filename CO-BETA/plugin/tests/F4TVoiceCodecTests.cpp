#include "F4TVoiceCodec.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{
	using F4T::Voice::Decoder;
	using F4T::Voice::Encoder;
	using F4T::Voice::EncodeResult;
	using F4T::Voice::kFrameSamples;
	using F4T::Voice::kMaxEncodedBytes;
	using F4T::Voice::kSampleRate;
	using Pcm = std::vector<std::int16_t>;
	using Payload = std::vector<std::uint8_t>;

	constexpr double kPi = 3.14159265358979323846;
	constexpr std::size_t kAlignmentWarmupSamples = 24000;
	constexpr std::size_t kMaxAlignmentLag = 960;
	constexpr double kSnrFloorDb = 2.0;
	constexpr double kFecSnrFloorDb = 3.0;
	constexpr double kPlcSnrFloorDb = 0.5;

	std::int16_t ToPcm(double a_value)
	{
		return static_cast<std::int16_t>(std::lround(std::clamp(a_value, -32768.0, 32767.0)));
	}

	Pcm MakeSpeechLike(std::size_t a_frames)
	{
		Pcm out(a_frames * kFrameSamples);
		double phase = 0.0;
		for (std::size_t n = 0; n < out.size(); ++n)
		{
			const double t = static_cast<double>(n) / kSampleRate;
			const double pitch = 120.0 + 40.0 * std::sin(2.0 * kPi * 0.7 * t);
			phase += 2.0 * kPi * pitch / kSampleRate;
			const double envelope = 0.55 + 0.45 * std::sin(2.0 * kPi * 3.0 * t);
			double harmonics = 0.0;
			for (int h = 1; h <= 16; ++h)
			{
				harmonics += std::sin(h * phase) / h;
			}
			out[n] = ToPcm(envelope * 5000.0 * harmonics);
		}
		return out;
	}

	Pcm MakeNoise(std::size_t a_frames)
	{
		Pcm out(a_frames * kFrameSamples);
		std::uint32_t state = 0x12345678U;
		for (auto& sample : out)
		{
			state = state * 1664525U + 1013904223U;
			sample = static_cast<std::int16_t>(static_cast<int>(state >> 16) - 32768);
		}
		return out;
	}

	Pcm MakeTransients(std::size_t a_frames)
	{
		Pcm out(a_frames * kFrameSamples, 0);
		for (std::size_t n = 0; n < out.size(); ++n)
		{
			const std::size_t position = n % 4800;
			if (position < 200)
			{
				out[n] = (position % 2 == 0) ? 32000 : -32000;
			}
		}
		return out;
	}

	Pcm MakeSilence(std::size_t a_frames)
	{
		return Pcm(a_frames * kFrameSamples, 0);
	}

	Payload Encode(Encoder& a_encoder, const Pcm& a_pcm, std::size_t a_frame, EncodeResult& a_result)
	{
		Payload buffer(kMaxEncodedBytes);
		a_result = a_encoder.Encode(&a_pcm[a_frame * kFrameSamples], buffer.data());
		buffer.resize(a_result.sendable ? static_cast<std::size_t>(a_result.bytes) : 0);
		return buffer;
	}

	void RoundTripSignal(const Pcm& a_pcm)
	{
		Encoder encoder;
		Decoder decoder;
		const bool encoderOpen = encoder.Open();
		const bool decoderOpen = decoder.Open();
		assert(encoderOpen && decoderOpen);

		Pcm out(kFrameSamples);
		const std::size_t frames = a_pcm.size() / kFrameSamples;
		for (std::size_t f = 0; f < frames; ++f)
		{
			EncodeResult result{};
			const Payload packet = Encode(encoder, a_pcm, f, result);
			assert(result.sendable);
			assert(!result.codecFailed);
			assert(result.bytes >= 1 && result.bytes <= kMaxEncodedBytes);
			assert(static_cast<std::size_t>(result.bytes) == packet.size());
			const int produced = decoder.Decode(packet.data(), result.bytes, out.data());
			assert(produced == kFrameSamples);
		}
	}

	void FecAndConcealProduceFrames()
	{
		Encoder encoder;
		Decoder decoder;
		const bool encoderOpen = encoder.Open();
		const bool decoderOpen = decoder.Open();
		assert(encoderOpen && decoderOpen);

		const Pcm pcm = MakeSpeechLike(10);
		std::vector<Payload> packets;
		for (std::size_t f = 0; f < 10; ++f)
		{
			EncodeResult result{};
			packets.push_back(Encode(encoder, pcm, f, result));
			assert(result.sendable);
		}

		Pcm out(kFrameSamples);
		for (std::size_t f = 0; f < 4; ++f)
		{
			const int decoded = decoder.Decode(packets[f].data(), static_cast<int>(packets[f].size()), out.data());
			assert(decoded == kFrameSamples);
		}
		const int fecRecovered = decoder.DecodeFec(packets[5].data(), static_cast<int>(packets[5].size()), out.data());
		assert(fecRecovered == kFrameSamples);
		const int afterFec = decoder.Decode(packets[5].data(), static_cast<int>(packets[5].size()), out.data());
		assert(afterFec == kFrameSamples);
		const int firstConceal = decoder.Conceal(out.data());
		assert(firstConceal == kFrameSamples);
		const int secondConceal = decoder.Conceal(out.data());
		assert(secondConceal == kFrameSamples);
		const int afterConceal = decoder.Decode(packets[8].data(), static_cast<int>(packets[8].size()), out.data());
		assert(afterConceal == kFrameSamples);

		Decoder fresh;
		const bool freshOpen = fresh.Open();
		assert(freshOpen);
		const int freshConceal = fresh.Conceal(out.data());
		assert(freshConceal == kFrameSamples);
	}

	void AddFrameError(const std::int16_t* a_reference, const std::int16_t* a_decoded, double& a_signal, double& a_error)
	{
		for (int n = 0; n < kFrameSamples; ++n)
		{
			const double reference = a_reference[n];
			const double difference = reference - a_decoded[n];
			a_signal += reference * reference;
			a_error += difference * difference;
		}
	}

	double SnrFromTotals(double a_signal, double a_error)
	{
		return 10.0 * std::log10(a_signal / std::max(a_error, 1.0e-9));
	}

	void FecAndPlcRecoverContent()
	{
		const std::size_t frames = 200;
		const Pcm pcm = MakeSpeechLike(frames);
		Encoder encoder;
		Decoder reference;
		const bool encoderOpen = encoder.Open();
		const bool referenceOpen = reference.Open();
		assert(encoderOpen && referenceOpen);

		std::vector<Payload> packets;
		for (std::size_t f = 0; f < frames; ++f)
		{
			EncodeResult result{};
			packets.push_back(Encode(encoder, pcm, f, result));
			assert(result.sendable);
		}

		Pcm clean(frames * kFrameSamples);
		for (std::size_t f = 0; f < frames; ++f)
		{
			const int produced = reference.Decode(packets[f].data(), static_cast<int>(packets[f].size()), &clean[f * kFrameSamples]);
			assert(produced == kFrameSamples);
		}

		const auto lost = [](std::size_t a_frame) { return a_frame % 10 == 5 || a_frame % 10 == 8 || a_frame % 10 == 9; };
		Decoder decoder;
		const bool decoderOpen = decoder.Open();
		assert(decoderOpen);
		Pcm out(kFrameSamples);
		double fecSignal = 0.0;
		double fecError = 0.0;
		double plcSignal = 0.0;
		double plcError = 0.0;
		for (std::size_t f = 0; f < frames; ++f)
		{
			if (lost(f))
			{
				if (f + 1 < frames && !lost(f + 1))
				{
					continue;
				}
				const int concealed = decoder.Conceal(out.data());
				assert(concealed == kFrameSamples);
				AddFrameError(&clean[f * kFrameSamples], out.data(), plcSignal, plcError);
				continue;
			}
			if (f > 0 && lost(f - 1))
			{
				const int recovered = decoder.DecodeFec(packets[f].data(), static_cast<int>(packets[f].size()), out.data());
				assert(recovered == kFrameSamples);
				AddFrameError(&clean[(f - 1) * kFrameSamples], out.data(), fecSignal, fecError);
			}
			const int produced = decoder.Decode(packets[f].data(), static_cast<int>(packets[f].size()), out.data());
			assert(produced == kFrameSamples);
		}

		const double fecSnr = SnrFromTotals(fecSignal, fecError);
		const double plcSnr = SnrFromTotals(plcSignal, plcError);
		std::printf("MEASURE_FEC_SNR %.3f dB floor %.1f dB\n", fecSnr, kFecSnrFloorDb);
		std::printf("MEASURE_PLC_SNR %.3f dB floor %.1f dB\n", plcSnr, kPlcSnrFloorDb);
		assert(fecSnr >= kFecSnrFloorDb);
		assert(plcSnr >= kPlcSnrFloorDb);
	}

	void EncoderResetRestoresInitialState()
	{
		const Pcm pcm = MakeSpeechLike(20);
		Encoder encoder;
		const bool opened = encoder.Open();
		assert(opened);

		std::vector<Payload> first;
		for (std::size_t f = 0; f < 10; ++f)
		{
			EncodeResult result{};
			first.push_back(Encode(encoder, pcm, f, result));
		}
		for (std::size_t f = 10; f < 20; ++f)
		{
			EncodeResult result{};
			Encode(encoder, pcm, f, result);
		}

		const bool reset = encoder.Reset();
		assert(reset);
		bool identical = true;
		for (std::size_t f = 0; f < 10; ++f)
		{
			EncodeResult result{};
			identical = identical && Encode(encoder, pcm, f, result) == first[f];
		}
		assert(identical);
	}

	void DecoderResetRestoresInitialState()
	{
		const Pcm pcm = MakeSpeechLike(10);
		Encoder encoder;
		Decoder decoder;
		const bool encoderOpen = encoder.Open();
		const bool decoderOpen = decoder.Open();
		assert(encoderOpen && decoderOpen);

		std::vector<Payload> packets;
		for (std::size_t f = 0; f < 10; ++f)
		{
			EncodeResult result{};
			packets.push_back(Encode(encoder, pcm, f, result));
		}

		auto decodeAll = [&decoder, &packets]() {
			Pcm all;
			Pcm out(kFrameSamples);
			for (const auto& packet : packets)
			{
				const int produced = decoder.Decode(packet.data(), static_cast<int>(packet.size()), out.data());
				assert(produced == kFrameSamples);
				all.insert(all.end(), out.begin(), out.end());
			}
			return all;
		};

		const Pcm firstRun = decodeAll();
		const bool reset = decoder.Reset();
		assert(reset);
		const Pcm secondRun = decodeAll();
		assert(secondRun == firstRun);
	}

	void FaultsAreCleared()
	{
		F4T::Voice::Testing::MutableFaults() = {};
	}

	void CreateFailureReportsCodecFailedImmediately()
	{
		auto& faults = F4T::Voice::Testing::MutableFaults();
		faults.failCreate = true;

		Encoder encoder;
		const bool opened = encoder.Open();
		assert(!opened);

		const Pcm pcm = MakeSpeechLike(1);
		EncodeResult result{};
		Encode(encoder, pcm, 0, result);
		assert(!result.sendable);
		assert(result.codecFailed);
		assert(result.bytes == 0);

		Decoder decoder;
		assert(!decoder.Open());

		faults.failCreate = false;
		const bool reopened = encoder.Open();
		assert(reopened);
		Encode(encoder, pcm, 0, result);
		assert(result.sendable);
		assert(!result.codecFailed);
	}

	void CtlFailureFailsSetup()
	{
		auto& faults = F4T::Voice::Testing::MutableFaults();
		faults.failCtl = true;
		Encoder encoder;
		const bool opened = encoder.Open();
		assert(!opened);
		faults.failCtl = false;

		Encoder realCtlFailure;
		const bool badComplexity = realCtlFailure.Open(11);
		assert(!badComplexity);
		const bool goodComplexity = realCtlFailure.Open(9);
		assert(goodComplexity);
	}

	void InjectedEncodeReturnsDropTicks()
	{
		auto& faults = F4T::Voice::Testing::MutableFaults();
		Encoder encoder;
		const bool opened = encoder.Open();
		assert(opened);
		const Pcm pcm = MakeSpeechLike(16);

		const int injected[] = { -1, 0, kMaxEncodedBytes + 1 };
		EncodeResult result{};
		for (std::size_t i = 0; i < 3; ++i)
		{
			faults.encodeReturn = injected[i];
			const Payload packet = Encode(encoder, pcm, i, result);
			assert(result.bytes == injected[i]);
			assert(!result.sendable);
			assert(packet.empty());
			assert(result.codecFailed == (i == 2));
		}
		assert(encoder.DroppedTicks() == 3U);

		faults.encodeReturn.reset();
		const Payload recovered = Encode(encoder, pcm, 3, result);
		assert(result.sendable);
		assert(!result.codecFailed);
		assert(!recovered.empty());
		assert(encoder.DroppedTicks() == 3U);
	}

	void SuccessClearsFailureStreak()
	{
		auto& faults = F4T::Voice::Testing::MutableFaults();
		Encoder encoder;
		const bool opened = encoder.Open();
		assert(opened);
		const Pcm pcm = MakeSpeechLike(16);
		EncodeResult result{};

		faults.encodeReturn = 0;
		Encode(encoder, pcm, 0, result);
		Encode(encoder, pcm, 1, result);
		assert(!result.codecFailed);

		faults.encodeReturn.reset();
		Encode(encoder, pcm, 2, result);
		assert(result.sendable);

		faults.encodeReturn = 0;
		Encode(encoder, pcm, 3, result);
		Encode(encoder, pcm, 4, result);
		assert(!result.codecFailed);
		Encode(encoder, pcm, 5, result);
		assert(result.codecFailed);

		faults.encodeReturn.reset();
		Encode(encoder, pcm, 6, result);
		assert(result.sendable);
		assert(!result.codecFailed);
	}

	double AlignedSnrDb(const Pcm& a_reference, const Pcm& a_decoded)
	{
		const std::size_t from = kAlignmentWarmupSamples;
		const std::size_t count = a_reference.size() - from - kMaxAlignmentLag;
		double bestCorrelation = -1.0e300;
		std::size_t bestLag = 0;
		for (std::size_t lag = 0; lag < kMaxAlignmentLag; ++lag)
		{
			double correlation = 0.0;
			for (std::size_t n = 0; n < count; ++n)
			{
				correlation += static_cast<double>(a_reference[from + n]) * a_decoded[from + n + lag];
			}
			if (correlation > bestCorrelation)
			{
				bestCorrelation = correlation;
				bestLag = lag;
			}
		}

		double signal = 0.0;
		double error = 0.0;
		for (std::size_t n = 0; n < count; ++n)
		{
			const double reference = a_reference[from + n];
			const double difference = reference - a_decoded[from + n + bestLag];
			signal += reference * reference;
			error += difference * difference;
		}
		return 10.0 * std::log10(signal / std::max(error, 1.0e-9));
	}

	void AlignedSnrMeetsFloor()
	{
		const std::size_t frames = 200;
		const Pcm pcm = MakeSpeechLike(frames);
		Encoder encoder;
		Decoder decoder;
		const bool encoderOpen = encoder.Open();
		const bool decoderOpen = decoder.Open();
		assert(encoderOpen && decoderOpen);

		Pcm decoded;
		Pcm out(kFrameSamples);
		for (std::size_t f = 0; f < frames; ++f)
		{
			EncodeResult result{};
			const Payload packet = Encode(encoder, pcm, f, result);
			assert(result.sendable);
			const int produced = decoder.Decode(packet.data(), result.bytes, out.data());
			assert(produced == kFrameSamples);
			decoded.insert(decoded.end(), out.begin(), out.end());
		}

		const double snr = AlignedSnrDb(pcm, decoded);
		std::printf("MEASURE_SNR %.3f dB floor %.1f dB\n", snr, kSnrFloorDb);
		assert(snr >= kSnrFloorDb);
	}
}

int main()
{
	const std::size_t frames = 200;
	RoundTripSignal(MakeSpeechLike(frames));
	RoundTripSignal(MakeSilence(frames));
	RoundTripSignal(MakeTransients(frames));
	RoundTripSignal(MakeNoise(frames));
	FecAndConcealProduceFrames();
	FecAndPlcRecoverContent();
	EncoderResetRestoresInitialState();
	DecoderResetRestoresInitialState();

	FaultsAreCleared();
	CreateFailureReportsCodecFailedImmediately();
	FaultsAreCleared();
	CtlFailureFailsSetup();
	FaultsAreCleared();
	InjectedEncodeReturnsDropTicks();
	FaultsAreCleared();
	SuccessClearsFailureStreak();
	FaultsAreCleared();

	AlignedSnrMeetsFloor();
	return 0;
}
