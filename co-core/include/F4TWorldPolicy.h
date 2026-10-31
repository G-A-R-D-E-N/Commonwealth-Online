#pragma once

#include "F4TReceiveFrame.h"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::WorldPolicy
{
	inline constexpr std::size_t kMaximumPolicyFileBytes = 8 * 1024 * 1024;
	inline constexpr std::size_t kMaximumClassEntries = 65536;
	inline constexpr std::size_t kMaximumPluginNameBytes = 260;
	inline constexpr std::size_t kMaximumGameNameBytes = 260;
	inline constexpr std::uint32_t kMaximumDataVersion = 0x00FFFFFF;
	inline constexpr std::uint32_t kMaximumLocalId = 0x00FFFFFF;
	inline constexpr std::size_t kFormKeyDigits = 6;

	// One worldPolicy frame must stay inside the GNS reliable frame limit (F4TReceiveFrame.h DefaultMaximumFrameBytes).
	inline constexpr std::size_t kMaximumDisableEntriesPerMessage = 128;
	static_assert(
		kMaximumDisableEntriesPerMessage * (kFormKeyDigits + kMaximumPluginNameBytes + 8) <
			F4T::ReceiveFrame::DefaultMaximumFrameBytes,
		"worldPolicy chunk exceeds the reliable frame budget");

	// constraint: reassembly must never retain more than the two source classes can hold
	inline constexpr std::size_t kMaximumTotalDisableEntries = 2 * kMaximumClassEntries;
	inline constexpr std::size_t kMaximumMessageParts =
		kMaximumTotalDisableEntries / kMaximumDisableEntriesPerMessage;
	inline constexpr std::string_view kPartialMessageType = "worldPolicyPart";
	inline constexpr std::string_view kCapabilityName = "world-policy-v1";

	enum class ClassId : std::uint8_t
	{
		Story,
		Companion,
		Vendor,
		Town,
		Settler,
		Enemy,
		Animal,
		Count
	};

	inline constexpr std::size_t kClassCount = static_cast<std::size_t>(ClassId::Count);

	[[nodiscard]] std::string_view ClassName(ClassId a_class) noexcept;
	[[nodiscard]] std::optional<ClassId> ParseClass(std::string_view a_name) noexcept;

	struct FormKey
	{
		std::string plugin;
		std::uint32_t localId{};

		[[nodiscard]] bool operator==(const FormKey&) const noexcept = default;
	};

	[[nodiscard]] std::optional<FormKey> ParseFormKey(std::string_view a_text);
	[[nodiscard]] std::string FormatFormKey(const FormKey& a_key);

	struct Data
	{
		std::uint32_t version{};
		std::string game;
		std::array<std::vector<FormKey>, kClassCount> classes;
		std::vector<FormKey> doors;
		bool sample{};

		[[nodiscard]] std::size_t EntryCount() const noexcept;
	};

	[[nodiscard]] std::optional<Data> ParseData(std::string_view a_bytes);
	[[nodiscard]] std::optional<Data> LoadData(const std::filesystem::path& a_path);

	enum class Mode : std::uint8_t
	{
		CoopStory,
		FreeRoam
	};

	[[nodiscard]] std::string_view ModeName(Mode a_mode) noexcept;
	[[nodiscard]] std::optional<Mode> ParseMode(std::string_view a_name) noexcept;
	[[nodiscard]] bool CanServeMode(const Data& a_data, Mode a_mode) noexcept;
	[[nodiscard]] std::string UnservableModeMessage(std::string_view a_dataPath, Mode a_mode);
	[[nodiscard]] std::string UnservableModeError(const std::filesystem::path& a_dataPath, Mode a_mode);

	enum class Doors : std::uint8_t
	{
		Shared,
		Vanilla
	};

	[[nodiscard]] std::string_view DoorsName(Doors a_doors) noexcept;

	enum class Vats : std::uint8_t
	{
		Disabled,
		RealTime,
		Vanilla
	};

	[[nodiscard]] std::string_view VatsName(Vats a_vats) noexcept;
	[[nodiscard]] std::optional<Vats> ParseVats(std::string_view a_name) noexcept;
	[[nodiscard]] Vats ServerDefaultVats() noexcept;
	void SetServerDefaultVats(Vats a_vats) noexcept;

	struct Policy
	{
		Mode mode{ Mode::CoopStory };
		std::uint32_t version{};
		std::vector<FormKey> disable;
		Doors doors{ Doors::Shared };
		bool suppressQuests{};
		Vats vats{ Vats::Disabled };

		[[nodiscard]] bool operator==(const Policy&) const noexcept = default;
	};

	[[nodiscard]] Policy DerivePolicy(const Data& a_data, Mode a_mode, bool a_doorsShared);
	[[nodiscard]] Policy DerivePolicy(const Data& a_data, Mode a_mode, bool a_doorsShared, Vats a_vats);

	struct Packet
	{
		Policy policy;
		std::size_t part{};
		std::size_t parts{ 1 };
	};

	[[nodiscard]] std::vector<nlohmann::json> EncodeMessages(const Policy& a_policy);
	[[nodiscard]] std::optional<Packet> DecodeMessage(const nlohmann::json& a_packet);
	[[nodiscard]] nlohmann::json ToJson(const Policy& a_policy);
	[[nodiscard]] std::optional<Policy> ParsePolicy(const nlohmann::json& a_packet);

	class Reassembler
	{
	public:
		[[nodiscard]] std::optional<Policy> Apply(const nlohmann::json& a_packet);
		void Reset() noexcept;

	private:
		Policy m_base;
		std::vector<FormKey> m_disable;
		std::size_t m_parts{};
		std::size_t m_received{};
	};
}
