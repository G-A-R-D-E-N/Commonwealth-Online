#pragma once

#include "F4TContentManifest.h"
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::DeathBagHistory
{
	inline constexpr std::size_t MaximumTransactionBytes = 256 * 1024;
	enum class DecodeStatus { Committed, Incomplete, Invalid, Corrupt };
	struct DecodedTransaction
	{
		DecodeStatus status{ DecodeStatus::Invalid };
		std::size_t consumed{};
		nlohmann::json payload;
	};

	[[nodiscard]] inline std::optional<std::string> EncodeTransaction(std::uint64_t a_sequence, const nlohmann::json& a_payload)
	{
		try {
			if (a_sequence == 0 || !a_payload.is_object()) return std::nullopt;
			const auto body = nlohmann::json{ { "sequence", a_sequence }, { "payload", a_payload } }.dump();
			if (body.size() > MaximumTransactionBytes) return std::nullopt;
			constexpr char digits[] = "0123456789abcdef";
			std::string length(8, '0');
			auto value = body.size();
			for (std::size_t i = length.size(); i > 0; --i) { length[i - 1] = digits[value & 0xf]; value >>= 4; }
			return length + ":" + body + ":" + F4T::ContentManifest::Sha256Hex(body) + "\n";
		} catch (...) { return std::nullopt; }
	}

	inline constexpr std::size_t FrameOverhead = 9 + 1 + 64 + 1;
	[[nodiscard]] inline std::optional<std::size_t> DeclaredFrameSize(std::string_view a_bytes)
	{
		if (a_bytes.size() < 9) return std::nullopt;
		std::size_t length = 0;
		for (std::size_t i = 0; i < 8; ++i) {
			const char ch = a_bytes[i];
			if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return std::nullopt;
			length = (length << 4) | static_cast<std::size_t>(ch <= '9' ? ch - '0' : ch - 'a' + 10);
		}
		if (a_bytes[8] != ':') return std::nullopt;
		return length + FrameOverhead;
	}

	[[nodiscard]] inline DecodedTransaction DecodeTransaction(std::string_view a_bytes, std::uint64_t a_expectedSequence)
	{
		try {
			if (a_expectedSequence == 0) return {};
			if (a_bytes.size() < 9) return { DecodeStatus::Incomplete, 0, {} };
			const auto declared = DeclaredFrameSize(a_bytes);
			if (!declared || *declared - FrameOverhead > MaximumTransactionBytes) return { DecodeStatus::Corrupt, 0, {} };
			const auto frameSize = *declared;
			const auto length = frameSize - FrameOverhead;
			if (a_bytes.size() < frameSize) return { DecodeStatus::Incomplete, 0, {} };
			if (a_bytes[9 + length] != ':' || a_bytes[frameSize - 1] != '\n') return { DecodeStatus::Corrupt, 0, {} };
			const auto body = a_bytes.substr(9, length);
			if (a_bytes.substr(10 + length, 64) != F4T::ContentManifest::Sha256Hex(body)) return { DecodeStatus::Corrupt, 0, {} };
			const auto document = nlohmann::json::parse(body, nullptr, false);
			if (!document.is_object() || document.size() != 2 || !document.contains("sequence") || !document.at("sequence").is_number_unsigned() ||
				document.at("sequence").get<std::uint64_t>() != a_expectedSequence || !document.contains("payload") || !document.at("payload").is_object()) return {};
			return { DecodeStatus::Committed, frameSize, document.at("payload") };
		} catch (...) { return {}; }
	}
}
