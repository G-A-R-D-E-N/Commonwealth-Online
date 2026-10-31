#pragma once

#include <cstdint>
#include <optional>

struct OpusEncoder;
struct OpusDecoder;

namespace F4T::Voice
{
	inline constexpr int kSampleRate = 48000;
	inline constexpr int kChannels = 1;
	inline constexpr int kFrameSamples = 960;
	inline constexpr int kMaxEncodedBytes = 160;
	inline constexpr int kCodecFailureStreak = 3;

	struct EncodeResult
	{
		int bytes{};
		bool sendable{};
		bool codecFailed{};
	};

	bool ConfigureEncoder(OpusEncoder* a_encoder, int a_complexity, int a_inbandFec);

	class Encoder
	{
	public:
		Encoder() = default;
		~Encoder();
		Encoder(const Encoder&) = delete;
		Encoder& operator=(const Encoder&) = delete;

		bool Open(int a_complexity = 5);
		bool Reset();
		EncodeResult Encode(const std::int16_t* a_pcm, std::uint8_t* a_out);
		std::uint32_t DroppedTicks() const { return m_droppedTicks; }

	private:
		OpusEncoder* m_encoder{ nullptr };
		int m_failureStreak{ 0 };
		std::uint32_t m_droppedTicks{ 0 };
	};

	class Decoder
	{
	public:
		Decoder() = default;
		~Decoder();
		Decoder(const Decoder&) = delete;
		Decoder& operator=(const Decoder&) = delete;

		bool Open();
		bool Reset();
		int Decode(const std::uint8_t* a_data, int a_len, std::int16_t* a_pcm);
		int DecodeFec(const std::uint8_t* a_nextData, int a_len, std::int16_t* a_pcm);
		int Conceal(std::int16_t* a_pcm);

	private:
		OpusDecoder* m_decoder{ nullptr };
	};

#ifdef F4T_VOICE_CODEC_TESTING
	namespace Testing
	{
		struct Faults
		{
			bool failCreate{};
			bool failCtl{};
			std::optional<int> encodeReturn{};
		};

		Faults& MutableFaults();
	}
#endif
}
