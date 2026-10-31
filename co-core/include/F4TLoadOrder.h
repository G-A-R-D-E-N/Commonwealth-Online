#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace F4T::LoadOrder
{
	inline constexpr std::uint8_t kFormatVersion = 1;
	inline constexpr std::string_view kPacketType = "pluginList";

	inline constexpr std::size_t kMaximumFullPlugins = 254;
	inline constexpr std::size_t kMaximumLightPlugins = 4096;
	inline constexpr std::size_t kMaximumPluginNameBytes = 255;
	inline constexpr std::size_t kMaximumPayloadBytes = 40 * 1024;
	inline constexpr std::size_t kMaximumDifferenceNames = 3;
	inline constexpr std::size_t kMaximumDifferenceNameBytes = 64;

	enum class PluginKind : std::uint8_t
	{
		full = 0,
		light = 1
	};

	struct Plugin
	{
		PluginKind kind{ PluginKind::full };
		std::uint16_t index{};
		std::string name;
	};

	struct PluginList
	{
		std::vector<Plugin> full;
		std::vector<Plugin> light;
	};

	[[nodiscard]] bool IsValidPluginList(const PluginList& a_list) noexcept;
	[[nodiscard]] std::optional<std::string> EncodePluginList(const PluginList& a_list);
	[[nodiscard]] std::optional<PluginList> DecodePluginList(std::string_view a_base64);
	[[nodiscard]] std::optional<std::string> BuildPluginListPacket(const PluginList& a_list);

	[[nodiscard]] constexpr std::array<std::int16_t, 256> MissingFullSlots() noexcept
	{
		std::array<std::int16_t, 256> slots{};
		for (auto& slot : slots) {
			slot = -1;
		}
		return slots;
	}

	struct TranslationTable
	{
		bool identical{ true };
		bool reordered{};
		std::array<std::int16_t, 256> full = MissingFullSlots();
		std::vector<std::int16_t> light;
		std::vector<std::string> missingLocal;
		std::vector<std::string> missingRemote;
		std::string differences;

		[[nodiscard]] std::uint32_t ToLocal(std::uint32_t a_formId) const noexcept;
	};

	[[nodiscard]] TranslationTable BuildTranslationTable(const PluginList& a_local, const PluginList& a_remote);

	class Registry
	{
	public:
		void SetLocalList(PluginList a_list);
		void SetRemoteList(std::uint32_t a_playerId, PluginList a_list);
		[[nodiscard]] bool HasLocalList() const;
		[[nodiscard]] bool Known(std::uint32_t a_playerId) const;
		[[nodiscard]] bool Identical(std::uint32_t a_playerId) const;
		[[nodiscard]] std::uint32_t ToLocal(std::uint32_t a_playerId, std::uint32_t a_formId) const;
		[[nodiscard]] std::string Differences(std::uint32_t a_playerId) const;
		[[nodiscard]] std::uint64_t Dropped() const noexcept;
		void Forget(std::uint32_t a_playerId);
		void Reset();

	private:
		void RebuildLocked();

		mutable std::mutex _mutex;
		std::optional<PluginList> _local;
		std::unordered_map<std::uint32_t, PluginList> _remoteLists;
		std::unordered_map<std::uint32_t, TranslationTable> _tables;
		mutable std::atomic<std::uint64_t> _dropped{ 0 };
	};
}
