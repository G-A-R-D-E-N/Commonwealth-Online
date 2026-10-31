#pragma once

#include "F4TVoiceCodec.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace VoiceLab::Loop
{
	inline constexpr std::uint64_t kRingCapacity = 16384;
	inline constexpr std::size_t kMaxClicks = 4096;

	struct Options
	{
		bool latency{};
		bool simulate{};
		std::string wavPath;
		double seconds{ 60.0 };
		int complexity{ 5 };
		double thresholdDbfs{ -30.0 };
	};

	struct Mark
	{
		std::uint64_t frame{};
		std::uint32_t index{};
		double captureSec{};
		double renderSec{ -1.0 };
	};

	struct Pipeline
	{
		const Options* options{};
		double simSeconds{};
		double thresholdAbs{};
		F4T::Voice::Encoder encoder;
		F4T::Voice::Decoder decoder;
		std::int16_t pending[F4T::Voice::kFrameSamples]{};
		std::uint32_t pendingCount{};
		std::uint64_t quietSamples{};
		std::uint64_t frames{};
		std::uint64_t encoded{};
		std::uint64_t decoded{};
		std::uint64_t concealed{};
		std::uint64_t overflowFrames{};
		double payloadSum{};
		std::uint32_t payloadMax{};
		std::vector<Mark> marks;
		std::size_t marksCount{};
		std::atomic<std::size_t> marksPublished{};
		std::size_t nextMark{};
		std::int16_t ring[kRingCapacity]{};
		std::atomic<std::uint64_t> writeIndex{};
		std::atomic<std::uint64_t> readIndex{};
		std::uint64_t underflowFrames{};
		std::atomic<bool> failed{};
		std::atomic<long> hresult{};
	};

	bool Prepare(Pipeline& a_pipeline, const Options& a_options);
	void OnSamples(void* a_user, const std::int16_t* a_mono, std::uint32_t a_frames);
	void OnFill(void* a_user, float* a_stereo, std::uint32_t a_frames);
	bool PrintReport(const Pipeline& a_pipeline, const Options& a_options);
	int RunSimulated(Pipeline& a_pipeline, const Options& a_options);
}

int RunLoop(int a_argc, char** a_argv, bool a_latency);
