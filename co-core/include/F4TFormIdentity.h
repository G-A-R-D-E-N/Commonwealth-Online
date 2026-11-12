#pragma once

#include <cstdint>
#include <string>

namespace F4T::FormIdentity
{
	inline constexpr std::uint8_t LightFileMarker = 0xFE;
	inline constexpr std::uint8_t RuntimeFileMarker = 0xFF;
	inline constexpr std::uint32_t MaximumRegularLocalId = 0x00FFFFFF;
	inline constexpr std::uint32_t MaximumLightFileIndex = 0x00000FFF;
	inline constexpr std::uint32_t MaximumLightLocalId = 0x00000FFF;

	enum class Kind : std::uint8_t
	{
		Regular,
		Light,
		Runtime
	};

	struct Split
	{
		Kind kind{ Kind::Runtime };
		std::uint32_t fileIndex{};
		std::uint32_t localId{};
	};

	struct RecordIdentity
	{
		std::string pluginName;
		std::uint32_t localId{};
	};

	[[nodiscard]] Split SplitFormId(std::uint32_t a_formId) noexcept;
	[[nodiscard]] bool IsValidRecordIdentity(const RecordIdentity& a_identity, Kind a_kind) noexcept;
}
