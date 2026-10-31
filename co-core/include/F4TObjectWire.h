#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ObjectWire
{
	inline constexpr std::size_t kMaximumOperationIdChars = 64;
	inline constexpr std::size_t kMaximumPluginChars = 128;
	inline constexpr std::size_t kMaximumReasonChars = 64;
	inline constexpr std::size_t kMaximumSnapshotObjects = 128;
	inline constexpr std::uint32_t kMaximumLockLevel = 8;

	enum class Kind : std::uint8_t
	{
		Door,
		Activator
	};

	enum class Action : std::uint8_t
	{
		Open,
		Close,
		Toggle,
		Activate
	};

	struct ObjectKey
	{
		std::string plugin;
		std::uint32_t localId{};

		friend bool operator==(const ObjectKey&, const ObjectKey&) = default;

		[[nodiscard]] friend bool operator<(const ObjectKey& a_lhs, const ObjectKey& a_rhs) noexcept
		{
			if (a_lhs.plugin != a_rhs.plugin) {
				return a_lhs.plugin < a_rhs.plugin;
			}
			return a_lhs.localId < a_rhs.localId;
		}
	};

	struct ObjectPosition
	{
		std::string cellId;
		std::string worldspaceId;
		double x{};
		double y{};
		double z{};
	};

	struct ObjectState
	{
		ObjectKey object;
		Kind kind{ Kind::Door };
		bool open{};
		bool hasLock{};
		bool locked{};
		std::uint32_t lockLevel{};
		std::uint32_t revision{};
		ObjectPosition position;
	};

	[[nodiscard]] std::string_view KindName(Kind a_kind) noexcept;
	[[nodiscard]] std::string_view ActionName(Action a_action) noexcept;
	[[nodiscard]] std::optional<Kind> ParseKind(std::string_view a_kind) noexcept;
	[[nodiscard]] std::optional<Action> ParseAction(std::string_view a_action) noexcept;

	[[nodiscard]] std::optional<ObjectKey> ParseObjectKey(const nlohmann::json& a_object);
	[[nodiscard]] std::optional<ObjectPosition> ParsePosition(const nlohmann::json& a_position);

	[[nodiscard]] std::optional<nlohmann::json> NormalizeActivateRequest(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeLockRequest(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeObjectState(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeObjectStateSnapshot(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeObjectStateRejected(const nlohmann::json& a_packet);

	[[nodiscard]] nlohmann::json BuildObjectState(const ObjectState& a_state);
	[[nodiscard]] nlohmann::json BuildStateSnapshotPacket(const std::vector<ObjectState>& a_states);
}
