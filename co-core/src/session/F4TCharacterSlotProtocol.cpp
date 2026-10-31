#include "F4TCharacterSlotProtocol.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::CharacterSlotProtocol
{
	namespace
	{
		using Json = nlohmann::json;

		[[nodiscard]] bool IsNameChar(char a_ch) noexcept
		{
			const auto byte = static_cast<unsigned char>(a_ch);
			if (byte < 0x20 || byte == 0x7F) {
				return false;
			}
			return a_ch != '#';
		}
	}

	std::optional<std::uint32_t> NormalizeSlot(const Json& a_packet)
	{
		const auto found = a_packet.find("slot");
		if (found == a_packet.end() || !found->is_number_unsigned()) {
			return std::nullopt;
		}
		// Constraint: read wide; an out-of-range slot is invalid input, never a throw.
		const auto slot = found->get<std::uint64_t>();
		if (slot > std::numeric_limits<std::uint32_t>::max()) {
			return std::nullopt;
		}
		return static_cast<std::uint32_t>(slot);
	}

	std::optional<Create> NormalizeCreate(const Json& a_packet)
	{
		const auto slot = NormalizeSlot(a_packet);
		const auto found = a_packet.find("name");
		if (!slot || found == a_packet.end() || !found->is_string()) {
			return std::nullopt;
		}
		const auto name = NormalizeName(found->get_ref<const std::string&>());
		if (!name) {
			return std::nullopt;
		}
		return Create{ *slot, *name };
	}

	std::optional<SlotDelete> NormalizeDelete(const Json& a_packet)
	{
		const auto slot = NormalizeSlot(a_packet);
		if (!slot) {
			return std::nullopt;
		}
		const auto found = a_packet.find("confirmToken");
		if (found == a_packet.end()) {
			return SlotDelete{ *slot, {}, false };
		}
		if (!found->is_string() || found->get_ref<const std::string&>().empty()) {
			return std::nullopt;
		}
		return SlotDelete{ *slot, found->get<std::string>(), true };
	}

	std::optional<std::string> NormalizeName(std::string_view a_name)
	{
		if (a_name.size() < kMinimumSlotNameLength || a_name.size() > kMaximumSlotNameLength) {
			return std::nullopt;
		}
		if (a_name.front() == ' ' || a_name.back() == ' ') {
			return std::nullopt;
		}
		for (const char ch : a_name) {
			if (!IsNameChar(ch)) {
				return std::nullopt;
			}
		}
		return std::string{ a_name };
	}

	bool NamesEqual(std::string_view a_left, std::string_view a_right) noexcept
	{
		if (a_left.size() != a_right.size()) {
			return false;
		}
		for (std::size_t i = 0; i < a_left.size(); ++i) {
			if (std::tolower(static_cast<unsigned char>(a_left[i])) != std::tolower(static_cast<unsigned char>(a_right[i]))) {
				return false;
			}
		}
		return true;
	}

	Json MakeResult(std::string_view a_op, bool a_ok, std::string_view a_reason, std::uint32_t a_slot, bool a_hasSlot)
	{
		Json result{ { "type", "characterResult" }, { "op", std::string{ a_op } }, { "ok", a_ok } };
		if (!a_ok) {
			result["reason"] = std::string{ a_reason };
		}
		if (a_hasSlot) {
			result["slot"] = a_slot;
		}
		return result;
	}

	Json MakeDeleteChallenge(std::uint32_t a_slot, std::string_view a_confirmToken, std::uint32_t a_expiresInSeconds)
	{
		return Json{
			{ "type", "characterDeleteChallenge" },
			{ "slot", a_slot },
			{ "confirmToken", std::string{ a_confirmToken } },
			{ "expiresInSeconds", a_expiresInSeconds }
		};
	}
}
