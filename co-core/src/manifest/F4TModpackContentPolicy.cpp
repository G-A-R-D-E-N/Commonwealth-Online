#include "F4TModpackContentPolicy.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::ModpackManifest
{
	namespace
	{
		constexpr std::array<std::string_view, 3> kPluginExtensions{ ".esp", ".esm", ".esl" };

		struct CompanionSuffix
		{
			std::string_view suffix;
			AutoInstallFileKind kind;
		};

		constexpr std::array<CompanionSuffix, 5> kCompanionSuffixes{ {
			{ " - Main.ba2", AutoInstallFileKind::mainArchive },
			{ " - Textures.ba2", AutoInstallFileKind::texturesArchive },
			{ " - Geometry.psg", AutoInstallFileKind::previsGeometry },
			{ " - Geometry.csg", AutoInstallFileKind::precombinedGeometry },
			{ ".cdx", AutoInstallFileKind::textureIndex },
		} };

		constexpr std::string_view kVoicesMarker = " - Voices_";
		constexpr std::string_view kVoicesExtension = ".ba2";

		constexpr std::array<std::string_view, 2> kOfficialStemPrefixes{ "Fallout4", "DLC" };
		constexpr std::string_view kCreationClubStemPrefix = "cc";
		constexpr std::string_view kCommonwealthOnlineStemPrefix = "CommonwealthOnline";

		constexpr std::array<std::string_view, 7> kReservedDeviceNames{ "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$" };

		[[nodiscard]] constexpr char LowerAscii(char a_character) noexcept
		{
			return a_character >= 'A' && a_character <= 'Z' ? static_cast<char>(a_character - 'A' + 'a') : a_character;
		}

		[[nodiscard]] bool EndsWithIgnoringAsciiCase(std::string_view a_text, std::string_view a_suffix) noexcept
		{
			return a_text.size() >= a_suffix.size() &&
				EqualsIgnoringAsciiCase(a_text.substr(a_text.size() - a_suffix.size()), a_suffix);
		}

		[[nodiscard]] bool IsAsciiDigit(char a_character) noexcept
		{
			return a_character >= '0' && a_character <= '9';
		}

		[[nodiscard]] bool IsAsciiLetter(char a_character) noexcept
		{
			const auto lower = LowerAscii(a_character);
			return lower >= 'a' && lower <= 'z';
		}

		[[nodiscard]] bool IsForbiddenCharacter(char a_character) noexcept
		{
			const auto byte = static_cast<unsigned char>(a_character);
			if (byte < 0x20 || byte > 0x7E) {
				return true;
			}
			switch (a_character) {
			case '<':
			case '>':
			case ':':
			case '"':
			case '|':
			case '?':
			case '*':
			case '~':
				return true;
			default:
				return false;
			}
		}

		[[nodiscard]] bool IsReservedDeviceStem(std::string_view a_stem) noexcept
		{
			for (const auto reserved : kReservedDeviceNames) {
				if (EqualsIgnoringAsciiCase(a_stem, reserved)) {
					return true;
				}
			}
			if (a_stem.size() == 4 && IsAsciiDigit(a_stem[3])) {
				const auto prefix = a_stem.substr(0, 3);
				return EqualsIgnoringAsciiCase(prefix, "COM") || EqualsIgnoringAsciiCase(prefix, "LPT");
			}
			return false;
		}

		[[nodiscard]] bool StartsWithIgnoringAsciiCase(std::string_view a_text, std::string_view a_prefix) noexcept
		{
			return a_text.size() >= a_prefix.size() && EqualsIgnoringAsciiCase(a_text.substr(0, a_prefix.size()), a_prefix);
		}

		[[nodiscard]] bool IsOfficialStem(std::string_view a_stem) noexcept
		{
			for (const auto prefix : kOfficialStemPrefixes) {
				if (StartsWithIgnoringAsciiCase(a_stem, prefix)) {
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] bool IsVoiceLanguage(std::string_view a_language) noexcept
		{
			if (a_language.size() < kMinimumVoiceLanguageBytes || a_language.size() > kMaximumVoiceLanguageBytes) {
				return false;
			}
			for (const auto character : a_language) {
				if (!IsAsciiLetter(character)) {
					return false;
				}
			}
			return true;
		}
	}

	bool EqualsIgnoringAsciiCase(std::string_view a_left, std::string_view a_right) noexcept
	{
		if (a_left.size() != a_right.size()) {
			return false;
		}
		for (std::size_t index = 0; index < a_left.size(); ++index) {
			if (LowerAscii(a_left[index]) != LowerAscii(a_right[index])) {
				return false;
			}
		}
		return true;
	}

	std::optional<AutoInstallFile> ClassifyAutoInstallName(std::string_view a_name)
	{
		for (const auto extension : kPluginExtensions) {
			if (EndsWithIgnoringAsciiCase(a_name, extension) && a_name.size() > extension.size()) {
				return AutoInstallFile{ AutoInstallFileKind::plugin, std::string(a_name.substr(0, a_name.size() - extension.size())), {} };
			}
		}
		for (const auto& companion : kCompanionSuffixes) {
			if (EndsWithIgnoringAsciiCase(a_name, companion.suffix) && a_name.size() > companion.suffix.size()) {
				return AutoInstallFile{ companion.kind, std::string(a_name.substr(0, a_name.size() - companion.suffix.size())), {} };
			}
		}
		if (EndsWithIgnoringAsciiCase(a_name, kVoicesExtension)) {
			const auto withoutExtension = a_name.substr(0, a_name.size() - kVoicesExtension.size());
			for (std::size_t position = withoutExtension.size(); position-- > 0;) {
				if (position + kVoicesMarker.size() <= withoutExtension.size() &&
					EqualsIgnoringAsciiCase(withoutExtension.substr(position, kVoicesMarker.size()), kVoicesMarker)) {
					const auto stem = withoutExtension.substr(0, position);
					const auto language = withoutExtension.substr(position + kVoicesMarker.size());
					if (!stem.empty() && IsVoiceLanguage(language)) {
						return AutoInstallFile{ AutoInstallFileKind::voicesArchive, std::string(stem), std::string(language) };
					}
					break;
				}
			}
		}
		return std::nullopt;
	}

	AutoInstallRefusal CheckAutoInstallName(std::string_view a_name)
	{
		if (a_name.empty()) {
			return AutoInstallRefusal::emptyName;
		}
		if (a_name.size() > kMaximumAutoInstallNameBytes) {
			return AutoInstallRefusal::nameTooLong;
		}
		for (const auto character : a_name) {
			if (character == '/' || character == '\\') {
				return AutoInstallRefusal::pathSeparator;
			}
		}
		for (const auto character : a_name) {
			if (IsForbiddenCharacter(character)) {
				return AutoInstallRefusal::forbiddenCharacter;
			}
		}
		if (a_name.front() == ' ' || a_name.front() == '.' || a_name.back() == ' ' || a_name.back() == '.') {
			return AutoInstallRefusal::leadingOrTrailingDotOrSpace;
		}
		const auto firstDot = a_name.find('.');
		auto deviceStem = a_name.substr(0, firstDot);
		while (!deviceStem.empty() && deviceStem.back() == ' ') {
			deviceStem.remove_suffix(1);
		}
		if (IsReservedDeviceStem(deviceStem)) {
			return AutoInstallRefusal::reservedDeviceName;
		}
		const auto classified = ClassifyAutoInstallName(a_name);
		if (!classified) {
			return AutoInstallRefusal::unsupportedType;
		}
		if (IsReservedDeviceStem(classified->pluginStem)) {
			return AutoInstallRefusal::reservedDeviceName;
		}
		if (IsOfficialStem(classified->pluginStem)) {
			return AutoInstallRefusal::officialContent;
		}
		if (StartsWithIgnoringAsciiCase(classified->pluginStem, kCreationClubStemPrefix)) {
			return AutoInstallRefusal::creationClub;
		}
		if (StartsWithIgnoringAsciiCase(classified->pluginStem, kCommonwealthOnlineStemPrefix)) {
			return AutoInstallRefusal::commonwealthOnlineContent;
		}
		return AutoInstallRefusal::none;
	}

	std::string_view DescribeAutoInstallRefusal(AutoInstallRefusal a_refusal) noexcept
	{
		switch (a_refusal) {
		case AutoInstallRefusal::none:
			return "allowed";
		case AutoInstallRefusal::emptyName:
			return "the file name is empty";
		case AutoInstallRefusal::nameTooLong:
			return "the file name is too long";
		case AutoInstallRefusal::pathSeparator:
			return "the file is not directly in Data";
		case AutoInstallRefusal::forbiddenCharacter:
			return "the file name has a character that is not allowed";
		case AutoInstallRefusal::leadingOrTrailingDotOrSpace:
			return "the file name starts or ends with a dot or a space";
		case AutoInstallRefusal::reservedDeviceName:
			return "the file name is a reserved Windows device name";
		case AutoInstallRefusal::unsupportedType:
			return "only plugins and their own archives can be installed automatically";
		case AutoInstallRefusal::officialContent:
			return "official game content is never downloaded";
		case AutoInstallRefusal::creationClub:
			return "Creation Club content is never downloaded";
		case AutoInstallRefusal::commonwealthOnlineContent:
			return "Commonwealth Online's own files are never downloaded from a server";
		}
		return "unknown";
	}

	std::string_view DescribeAutoInstallFileKind(AutoInstallFileKind a_kind) noexcept
	{
		switch (a_kind) {
		case AutoInstallFileKind::plugin:
			return "plugin";
		case AutoInstallFileKind::mainArchive:
			return "main archive";
		case AutoInstallFileKind::texturesArchive:
			return "textures archive";
		case AutoInstallFileKind::voicesArchive:
			return "voices archive";
		case AutoInstallFileKind::previsGeometry:
			return "previs geometry";
		case AutoInstallFileKind::precombinedGeometry:
			return "precombined geometry";
		case AutoInstallFileKind::textureIndex:
			return "texture index";
		}
		return "unknown";
	}
}
