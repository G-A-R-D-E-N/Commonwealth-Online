#include "lab_wav.h"

#include "F4TVoiceCodec.h"

#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>

namespace
{
	std::uint32_t ReadLe32(const std::vector<unsigned char>& a_bytes, std::size_t a_at)
	{
		return static_cast<std::uint32_t>(a_bytes[a_at]) | (static_cast<std::uint32_t>(a_bytes[a_at + 1]) << 8) |
			(static_cast<std::uint32_t>(a_bytes[a_at + 2]) << 16) | (static_cast<std::uint32_t>(a_bytes[a_at + 3]) << 24);
	}

	std::uint16_t ReadLe16(const std::vector<unsigned char>& a_bytes, std::size_t a_at)
	{
		return static_cast<std::uint16_t>(a_bytes[a_at] | (a_bytes[a_at + 1] << 8));
	}
}

bool LoadCorpus(const std::string& a_path, std::vector<std::int16_t>& a_pcm)
{
	std::ifstream file(a_path, std::ios::binary);
	if (!file)
	{
		return false;
	}
	const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
	{
		return false;
	}
	bool formatOk = false;
	std::size_t position = 12;
	while (position + 8 <= bytes.size())
	{
		const std::size_t size = ReadLe32(bytes, position + 4);
		const std::size_t body = position + 8;
		if (std::memcmp(&bytes[position], "fmt ", 4) == 0 && body + 16 <= bytes.size())
		{
			formatOk = ReadLe16(bytes, body) == 1 && ReadLe16(bytes, body + 2) == 1 &&
				ReadLe32(bytes, body + 4) == static_cast<std::uint32_t>(F4T::Voice::kSampleRate) && ReadLe16(bytes, body + 14) == 16;
		}
		else if (std::memcmp(&bytes[position], "data", 4) == 0)
		{
			if (!formatOk)
			{
				return false;
			}
			const std::size_t available = (size < bytes.size() - body ? size : bytes.size() - body) / 2;
			a_pcm.resize(available);
			for (std::size_t i = 0; i < available; ++i)
			{
				a_pcm[i] = static_cast<std::int16_t>(ReadLe16(bytes, body + 2 * i));
			}
			return !a_pcm.empty();
		}
		position = body + size + (size & 1U);
	}
	return false;
}
