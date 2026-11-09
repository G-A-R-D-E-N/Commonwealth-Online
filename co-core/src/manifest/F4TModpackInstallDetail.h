#pragma once

#include "F4TContentManifest.h"
#include "F4TModpackInstall.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::ModpackInstall::Detail
{
	using ModpackManifest::EqualsIgnoringAsciiCase;

	inline constexpr int kFormatVersion = 1;
	inline constexpr std::size_t kMaximumLabelBytes = 256;
	inline constexpr std::size_t kMaximumJoinIdBytes = 128;
	inline constexpr std::size_t kMaximumLoadOrderNameBytes = 256;
	inline constexpr std::size_t kMaximumCreationClubListBytes = 256 * 1024;
	inline constexpr std::string_view kPluginsTxtBackupName = "plugins.txt";

	[[nodiscard]] inline bool IsPrintable(std::string_view a_text, std::size_t a_maximumBytes, bool a_allowEmpty)
	{
		if ((!a_allowEmpty && a_text.empty()) || a_text.size() > a_maximumBytes) {
			return false;
		}
		for (const auto character : a_text) {
			const auto byte = static_cast<unsigned char>(character);
			if (byte < 0x20 || byte == 0x7F) {
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] inline std::string Lower(std::string_view a_text)
	{
		std::string lowered{ a_text };
		for (auto& character : lowered) {
			if (character >= 'A' && character <= 'Z') {
				character = static_cast<char>(character - 'A' + 'a');
			}
		}
		return lowered;
	}

	[[nodiscard]] inline bool IsBackupFolderName(std::string_view a_name)
	{
		if (a_name.empty() || a_name.size() > 64) {
			return false;
		}
		for (const auto character : a_name) {
			const bool allowed = (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') || character == '-';
			if (!allowed) {
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] inline std::string_view StepName(OpStep a_step) noexcept
	{
		switch (a_step) {
		case OpStep::backedUp:
			return "backedUp";
		case OpStep::placed:
			return "placed";
		case OpStep::planned:
		default:
			return "planned";
		}
	}

	[[nodiscard]] inline std::optional<OpStep> ParseStep(std::string_view a_text) noexcept
	{
		if (a_text == "planned") {
			return OpStep::planned;
		}
		if (a_text == "backedUp") {
			return OpStep::backedUp;
		}
		if (a_text == "placed") {
			return OpStep::placed;
		}
		return std::nullopt;
	}

	[[nodiscard]] inline std::string_view StateName(JournalState a_state) noexcept
	{
		switch (a_state) {
		case JournalState::verifying:
			return "verifying";
		case JournalState::applied:
			return "applied";
		case JournalState::rolledBack:
			return "rolledBack";
		case JournalState::applying:
		default:
			return "applying";
		}
	}

	[[nodiscard]] inline std::optional<JournalState> ParseState(std::string_view a_text) noexcept
	{
		if (a_text == "applying") {
			return JournalState::applying;
		}
		if (a_text == "verifying") {
			return JournalState::verifying;
		}
		if (a_text == "applied") {
			return JournalState::applied;
		}
		if (a_text == "rolledBack") {
			return JournalState::rolledBack;
		}
		return std::nullopt;
	}

	[[nodiscard]] inline const std::string* StringField(const nlohmann::json& a_object, const char* a_key)
	{
		const auto found = a_object.find(a_key);
		if (found == a_object.end() || !found->is_string()) {
			return nullptr;
		}
		return &found->get_ref<const std::string&>();
	}

	[[nodiscard]] inline std::optional<bool> BoolField(const nlohmann::json& a_object, const char* a_key)
	{
		const auto found = a_object.find(a_key);
		if (found == a_object.end() || !found->is_boolean()) {
			return std::nullopt;
		}
		return found->get<bool>();
	}

	[[nodiscard]] inline bool HasVersion(const nlohmann::json& a_root)
	{
		const auto version = a_root.find("version");
		return version != a_root.end() && version->is_number_integer() && version->get<int>() == kFormatVersion;
	}

	[[nodiscard]] inline nlohmann::json EndpointObject(const ServerEndpoint& a_endpoint)
	{
		return nlohmann::json{
			{ "host", a_endpoint.host },
			{ "port", a_endpoint.port },
			{ "joinId", a_endpoint.joinId }
		};
	}

	[[nodiscard]] inline std::optional<ServerEndpoint> ParseEndpoint(const nlohmann::json& a_root)
	{
		const auto endpoint = a_root.find("endpoint");
		if (endpoint == a_root.end() || !endpoint->is_object()) {
			return std::nullopt;
		}
		const auto* host = StringField(*endpoint, "host");
		const auto* joinId = StringField(*endpoint, "joinId");
		const auto port = endpoint->find("port");
		if (host == nullptr || !IsPrintable(*host, kMaximumLabelBytes, true) ||
			joinId == nullptr || !IsPrintable(*joinId, kMaximumJoinIdBytes, true) ||
			port == endpoint->end() || !port->is_number_unsigned() || port->get<std::uint64_t>() > 65535) {
			return std::nullopt;
		}
		return ServerEndpoint{ *host, static_cast<std::uint16_t>(port->get<std::uint64_t>()), *joinId };
	}

	[[nodiscard]] inline std::optional<nlohmann::json> ParseObject(std::string_view a_text)
	{
		if (a_text.empty() || a_text.size() > kMaximumPlanBytes) {
			return std::nullopt;
		}
		auto root = nlohmann::json::parse(a_text, nullptr, false);
		if (root.is_discarded() || !root.is_object() || !HasVersion(root)) {
			return std::nullopt;
		}
		return root;
	}
}
