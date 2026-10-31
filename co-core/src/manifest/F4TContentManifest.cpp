#include "F4TContentManifest.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace F4T::ContentManifest
{
	namespace
	{
		constexpr std::array<std::uint32_t, 64> kRoundConstants{
			0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL,
			0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
			0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
			0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
			0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
			0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
			0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL,
			0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
			0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
			0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
			0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL,
			0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
			0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
			0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
			0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
			0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
		};

		constexpr std::array<std::uint32_t, 8> kInitialHash{
			0x6a09e667UL, 0xbb67ae85UL, 0x3c6ef372UL, 0xa54ff53aUL,
			0x510e527fUL, 0x9b05688cUL, 0x1f83d9abUL, 0x5be0cd19UL
		};

		std::uint32_t RotateRight(std::uint32_t a_value, unsigned a_bits) noexcept
		{
			return (a_value >> a_bits) | (a_value << (32u - a_bits));
		}

		class Hasher
		{
		public:
			void Update(std::string_view a_bytes)
			{
				for (const unsigned char byte : a_bytes) {
					_buffer[_buffered++] = byte;
					if (_buffered == _buffer.size()) {
						ProcessBlock();
						_buffered = 0;
					}
				}
				_totalBytes += a_bytes.size();
			}

			std::array<std::uint8_t, 32> Digest()
			{
				const auto bitLength = _totalBytes * 8;
				_buffer[_buffered++] = 0x80;
				if (_buffered > 56) {
					std::fill(_buffer.begin() + static_cast<std::ptrdiff_t>(_buffered), _buffer.end(), std::uint8_t{ 0 });
					ProcessBlock();
					_buffered = 0;
				}
				std::fill(_buffer.begin() + static_cast<std::ptrdiff_t>(_buffered), _buffer.begin() + 56, std::uint8_t{ 0 });
				for (unsigned index = 0; index < 8; ++index) {
					_buffer[56 + index] = static_cast<std::uint8_t>(bitLength >> (56 - 8 * index));
				}
				ProcessBlock();

				std::array<std::uint8_t, 32> digest{};
				for (std::size_t index = 0; index < _hash.size(); ++index) {
					digest[index * 4] = static_cast<std::uint8_t>(_hash[index] >> 24);
					digest[index * 4 + 1] = static_cast<std::uint8_t>(_hash[index] >> 16);
					digest[index * 4 + 2] = static_cast<std::uint8_t>(_hash[index] >> 8);
					digest[index * 4 + 3] = static_cast<std::uint8_t>(_hash[index]);
				}
				return digest;
			}

		private:
			void ProcessBlock()
			{
				std::array<std::uint32_t, 64> words{};
				for (std::size_t index = 0; index < 16; ++index) {
					words[index] =
						(static_cast<std::uint32_t>(_buffer[index * 4]) << 24) |
						(static_cast<std::uint32_t>(_buffer[index * 4 + 1]) << 16) |
						(static_cast<std::uint32_t>(_buffer[index * 4 + 2]) << 8) |
						static_cast<std::uint32_t>(_buffer[index * 4 + 3]);
				}
				for (std::size_t index = 16; index < words.size(); ++index) {
					const auto sigma0 = RotateRight(words[index - 15], 7) ^ RotateRight(words[index - 15], 18) ^ (words[index - 15] >> 3);
					const auto sigma1 = RotateRight(words[index - 2], 17) ^ RotateRight(words[index - 2], 19) ^ (words[index - 2] >> 10);
					words[index] = words[index - 16] + sigma0 + words[index - 7] + sigma1;
				}

				auto a = _hash[0];
				auto b = _hash[1];
				auto c = _hash[2];
				auto d = _hash[3];
				auto e = _hash[4];
				auto f = _hash[5];
				auto g = _hash[6];
				auto h = _hash[7];
				for (std::size_t index = 0; index < words.size(); ++index) {
					const auto bigSigma1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
					const auto choose = (e & f) ^ ((~e) & g);
					const auto temp1 = h + bigSigma1 + choose + kRoundConstants[index] + words[index];
					const auto bigSigma0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
					const auto majority = (a & b) ^ (a & c) ^ (b & c);
					const auto temp2 = bigSigma0 + majority;
					h = g;
					g = f;
					f = e;
					e = d + temp1;
					d = c;
					c = b;
					b = a;
					a = temp1 + temp2;
				}

				_hash[0] += a;
				_hash[1] += b;
				_hash[2] += c;
				_hash[3] += d;
				_hash[4] += e;
				_hash[5] += f;
				_hash[6] += g;
				_hash[7] += h;
			}

			std::array<std::uint32_t, 8> _hash{ kInitialHash };
			std::array<std::uint8_t, 64> _buffer{};
			std::size_t _buffered{};
			std::uint64_t _totalBytes{};
		};

		std::string ToLowerHex(std::span<const std::uint8_t> a_bytes)
		{
			constexpr char kHex[] = "0123456789abcdef";
			std::string result;
			result.reserve(a_bytes.size() * 2);
			for (const auto byte : a_bytes) {
				result.push_back(kHex[byte >> 4]);
				result.push_back(kHex[byte & 0x0F]);
			}
			return result;
		}

		bool IsLowerHex64(std::string_view a_value) noexcept
		{
			if (a_value.size() != 64) {
				return false;
			}
			for (const char ch : a_value) {
				if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
					return false;
				}
			}
			return true;
		}

		std::optional<std::string> NormalizeRelativePath(std::string_view a_path)
		{
			if (a_path.empty() || a_path.size() > 512) {
				return std::nullopt;
			}
			std::string normalized{ a_path };
			for (auto& ch : normalized) {
				if (ch == '\\') {
					ch = '/';
				}
			}

			std::size_t begin = 0;
			while (begin + 2 <= normalized.size() && normalized[begin] == '.' && normalized[begin + 1] == '/') {
				begin += 2;
			}
			const auto view = std::string_view{ normalized }.substr(begin);
			if (view.empty()) {
				return std::nullopt;
			}

			std::size_t cursor = 0;
			while (cursor <= view.size()) {
				const auto next = view.find('/', cursor);
				const auto segment = view.substr(
					cursor,
					next == std::string_view::npos ? std::string_view::npos : next - cursor);
				if (segment.empty() || segment == "." || segment == "..") {
					return std::nullopt;
				}
				for (const unsigned char ch : segment) {
					if (ch < 0x20 || ch == 0x7f || ch == ':') {
						return std::nullopt;
					}
				}
				if (next == std::string_view::npos) {
					break;
				}
				cursor = next + 1;
			}
			return std::string{ view };
		}
	}

	std::string Sha256Hex(std::string_view a_bytes)
	{
		Hasher hasher;
		hasher.Update(a_bytes);
		return ToLowerHex(hasher.Digest());
	}

	struct Sha256Stream::State
	{
		Hasher hasher;
	};

	Sha256Stream::Sha256Stream() :
		state(std::make_unique<State>())
	{}

	Sha256Stream::~Sha256Stream() = default;
	Sha256Stream::Sha256Stream(Sha256Stream&&) noexcept = default;
	Sha256Stream& Sha256Stream::operator=(Sha256Stream&&) noexcept = default;

	void Sha256Stream::Update(std::string_view a_bytes)
	{
		state->hasher.Update(a_bytes);
	}

	std::string Sha256Stream::FinishHex()
	{
		auto hex = ToLowerHex(state->hasher.Digest());
		state = std::make_unique<State>();
		return hex;
	}

	std::optional<std::string> Sha256HexFile(const std::filesystem::path& a_path)
	{
		std::ifstream stream(a_path, std::ios::binary);
		if (!stream) {
			return std::nullopt;
		}
		Hasher hasher;
		std::array<char, 1 << 16> buffer{};
		while (stream) {
			stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
			const auto readCount = stream.gcount();
			if (readCount > 0) {
				hasher.Update(std::string_view{ buffer.data(), static_cast<std::size_t>(readCount) });
			}
		}
		if (!stream.eof()) {
			return std::nullopt;
		}
		return ToLowerHex(hasher.Digest());
	}

	bool IsValidFingerprint(std::string_view a_sha256Hex) noexcept
	{
		return IsLowerHex64(a_sha256Hex);
	}

	bool IsValidEntry(const Entry& a_entry)
	{
		return IsValidFingerprint(a_entry.sha256Hex) && NormalizeRelativePath(a_entry.relativePath).has_value();
	}

	std::optional<std::string> ComputeFingerprint(std::span<const Entry> a_entries)
	{
		if (a_entries.empty()) {
			return std::nullopt;
		}

		struct NormalizedEntry
		{
			std::string path;
			std::string sha256Hex;
		};

		std::vector<NormalizedEntry> normalized;
		normalized.reserve(a_entries.size());
		std::unordered_set<std::string> seenPaths;
		for (const auto& entry : a_entries) {
			auto path = NormalizeRelativePath(entry.relativePath);
			if (!path || !IsLowerHex64(entry.sha256Hex) || !seenPaths.insert(*path).second) {
				return std::nullopt;
			}
			normalized.push_back(NormalizedEntry{ std::move(*path), entry.sha256Hex });
		}

		std::sort(normalized.begin(), normalized.end(), [](const NormalizedEntry& a_lhs, const NormalizedEntry& a_rhs) {
			return a_lhs.path < a_rhs.path;
		});

		std::string canonical;
		for (const auto& entry : normalized) {
			canonical += entry.path;
			canonical += '\t';
			canonical += entry.sha256Hex;
			canonical += '\n';
		}
		return Sha256Hex(canonical);
	}
}
