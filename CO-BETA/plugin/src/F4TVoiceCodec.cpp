#include "F4TVoiceCodec.h"

#include <opus/opus.h>

#include <limits>

namespace F4T::Voice
{
#ifdef F4T_VOICE_CODEC_TESTING
	namespace Testing
	{
		Faults& MutableFaults()
		{
			static Faults faults;
			return faults;
		}
	}
#endif

	namespace
	{
		constexpr int kBitrate = 24000;
		constexpr int kPacketLossPercent = 10;
		constexpr int kInbandFec = 1;

		bool CtlOk(int a_result)
		{
#ifdef F4T_VOICE_CODEC_TESTING
			if (Testing::MutableFaults().failCtl)
			{
				return false;
			}
#endif
			return a_result == OPUS_OK;
		}

		OpusEncoder* CreateEncoder(int& a_error)
		{
#ifdef F4T_VOICE_CODEC_TESTING
			if (Testing::MutableFaults().failCreate)
			{
				a_error = OPUS_ALLOC_FAIL;
				return nullptr;
			}
#endif
			return opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_VOIP, &a_error);
		}

		OpusDecoder* CreateDecoder(int& a_error)
		{
#ifdef F4T_VOICE_CODEC_TESTING
			if (Testing::MutableFaults().failCreate)
			{
				a_error = OPUS_ALLOC_FAIL;
				return nullptr;
			}
#endif
			return opus_decoder_create(kSampleRate, kChannels, &a_error);
		}

		int EncodeFrame(OpusEncoder* a_encoder, const std::int16_t* a_pcm, std::uint8_t* a_out)
		{
#ifdef F4T_VOICE_CODEC_TESTING
			if (Testing::MutableFaults().encodeReturn)
			{
				return *Testing::MutableFaults().encodeReturn;
			}
#endif
			return opus_encode(a_encoder, a_pcm, kFrameSamples, a_out, kMaxEncodedBytes);
		}
	}

	bool ConfigureEncoder(OpusEncoder* a_encoder, int a_complexity, int a_inbandFec)
	{
		return CtlOk(opus_encoder_ctl(a_encoder, OPUS_SET_BITRATE(kBitrate))) &&
			CtlOk(opus_encoder_ctl(a_encoder, OPUS_SET_VBR(1))) &&
			CtlOk(opus_encoder_ctl(a_encoder, OPUS_SET_VBR_CONSTRAINT(1))) &&
			CtlOk(opus_encoder_ctl(a_encoder, OPUS_SET_COMPLEXITY(a_complexity))) &&
			CtlOk(opus_encoder_ctl(a_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE))) &&
			CtlOk(opus_encoder_ctl(a_encoder, OPUS_SET_INBAND_FEC(a_inbandFec))) &&
			CtlOk(opus_encoder_ctl(a_encoder, OPUS_SET_PACKET_LOSS_PERC(kPacketLossPercent))) &&
			CtlOk(opus_encoder_ctl(a_encoder, OPUS_SET_DTX(0)));
	}

	Encoder::~Encoder()
	{
		opus_encoder_destroy(m_encoder);
	}

	bool Encoder::Open(int a_complexity)
	{
		opus_encoder_destroy(m_encoder);
		m_encoder = nullptr;
		m_failureStreak = 0;
		m_droppedTicks = 0;

		int error = OPUS_OK;
		OpusEncoder* encoder = CreateEncoder(error);
		if (!encoder || error != OPUS_OK || !ConfigureEncoder(encoder, a_complexity, kInbandFec))
		{
			opus_encoder_destroy(encoder);
			return false;
		}

		m_encoder = encoder;
		return true;
	}

	bool Encoder::Reset()
	{
		return m_encoder && CtlOk(opus_encoder_ctl(m_encoder, OPUS_RESET_STATE));
	}

	EncodeResult Encoder::Encode(const std::int16_t* a_pcm, std::uint8_t* a_out)
	{
		if (!m_encoder)
		{
			return { 0, false, true };
		}

		const int bytes = EncodeFrame(m_encoder, a_pcm, a_out);
		const bool sendable = bytes >= 1 && bytes <= kMaxEncodedBytes;
		if (sendable)
		{
			m_failureStreak = 0;
		}
		else
		{
			if (m_failureStreak < kCodecFailureStreak)
			{
				++m_failureStreak;
			}
			if (m_droppedTicks < std::numeric_limits<std::uint32_t>::max())
			{
				++m_droppedTicks;
			}
		}
		return { bytes, sendable, m_failureStreak >= kCodecFailureStreak };
	}

	Decoder::~Decoder()
	{
		opus_decoder_destroy(m_decoder);
	}

	bool Decoder::Open()
	{
		opus_decoder_destroy(m_decoder);
		int error = OPUS_OK;
		m_decoder = CreateDecoder(error);
		return m_decoder && error == OPUS_OK;
	}

	bool Decoder::Reset()
	{
		return m_decoder && opus_decoder_ctl(m_decoder, OPUS_RESET_STATE) == OPUS_OK;
	}

	int Decoder::Decode(const std::uint8_t* a_data, int a_len, std::int16_t* a_pcm)
	{
		return m_decoder ? opus_decode(m_decoder, a_data, a_len, a_pcm, kFrameSamples, 0) : OPUS_INVALID_STATE;
	}

	int Decoder::DecodeFec(const std::uint8_t* a_nextData, int a_len, std::int16_t* a_pcm)
	{
		return m_decoder ? opus_decode(m_decoder, a_nextData, a_len, a_pcm, kFrameSamples, 1) : OPUS_INVALID_STATE;
	}

	int Decoder::Conceal(std::int16_t* a_pcm)
	{
		return m_decoder ? opus_decode(m_decoder, nullptr, 0, a_pcm, kFrameSamples, 0) : OPUS_INVALID_STATE;
	}
}
