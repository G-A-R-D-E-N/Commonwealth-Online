#pragma once

#include "F4TDownedState.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::Downed
{
	inline constexpr std::string_view kDownedStateType = "downedState";
	inline constexpr std::string_view kRevivedType = "revived";
	inline constexpr std::string_view kDiedType = "died";
	inline constexpr std::string_view kPlayerDiedType = "playerDied";
	inline constexpr std::string_view kRespawnGrantType = "respawnGrant";
	inline constexpr std::string_view kReviveRequestType = "reviveRequest";
	inline constexpr std::string_view kRespawnRequestType = "respawnRequest";
	inline constexpr std::string_view kCheckpointType = "checkpoint";
	inline constexpr std::string_view kReviveStartedType = "reviveStarted";
	inline constexpr std::string_view kReviveCancelledType = "reviveCancelled";
	inline constexpr std::string_view kHeldItemDroppedType = "heldItemDropped";
	inline constexpr std::string_view kReviveCancelType = "reviveCancel";

	struct DownedStateMessage
	{
		std::uint32_t playerId{};
		bool downed{ false };
		std::int32_t remainingMs{};
	};

	struct RevivedMessage
	{
		std::uint32_t byPlayerId{};
		double healthFraction{ 1.0 };
	};

	struct PlayerDiedMessage
	{
		std::uint32_t playerId{};
	};

	struct RespawnGrantMessage
	{
		RespawnPoint point{ RespawnPoint::kSanctuary };
		std::optional<std::uint32_t> friendId;
		std::optional<Checkpoint> checkpoint;
	};

	struct ReviveStartedMessage
	{
		std::uint32_t playerId{};
		std::uint32_t byPlayerId{};
		std::uint32_t channelMs{};
	};

	struct ReviveCancelledMessage
	{
		std::uint32_t playerId{};
		std::uint32_t byPlayerId{};
		std::string reason;
	};

	enum class EventKind
	{
		kDownedState,
		kRevived,
		kDied,
		kPlayerDied,
		kRespawnGrant,
		kRespawnChoice,
		kReviveStarted,
		kReviveCancelled,
		kHeldItemDropped
	};

	struct DownedEvent
	{
		EventKind kind{ EventKind::kDied };
		std::uint32_t playerId{};
		bool downed{ false };
		std::int32_t remainingMs{};
		double healthFraction{ 1.0 };
		RespawnPoint point{ RespawnPoint::kSanctuary };
		std::optional<std::uint32_t> friendId;
		std::optional<Checkpoint> checkpoint;
		std::uint32_t byPlayerId{};
		std::uint32_t channelMs{};
		std::string reason;
	};

	enum class DecodeStatus
	{
		kUnknown,
		kMalformed,
		kHandled
	};

	inline constexpr double kMinimumRevivedHealthFraction = 0.05;

	[[nodiscard]] inline double ClampRevivedHealthFraction(double a_fraction)
	{
		if (!std::isfinite(a_fraction)) {
			return kMinimumRevivedHealthFraction;
		}
		return (std::clamp)(a_fraction, kMinimumRevivedHealthFraction, 1.0);
	}

	class IDownedTransport
	{
	public:
		virtual ~IDownedTransport() = default;
		virtual bool Send(std::string_view a_packet) = 0;
	};

	void SetDownedTransport(IDownedTransport* a_transport);
	[[nodiscard]] bool SendReviveRequest(
		std::uint32_t a_targetPlayerId,
		std::optional<std::uint32_t> a_stimpaks = std::nullopt);
	[[nodiscard]] bool SendReviveCancel(std::uint32_t a_targetPlayerId, std::string_view a_reason);
	[[nodiscard]] bool SendRespawnRequest(RespawnPoint a_point, std::optional<std::uint32_t> a_friendId);
	[[nodiscard]] bool SendCheckpointReport(const Checkpoint& a_checkpoint);

	[[nodiscard]] inline std::optional<nlohmann::json> EncodeReviveRequest(
		std::uint32_t a_targetPlayerId,
		std::optional<std::uint32_t> a_stimpaks = std::nullopt)
	{
		if (a_targetPlayerId == 0) {
			return std::nullopt;
		}
		nlohmann::json request{
			{ "type", kReviveRequestType },
			{ "target", a_targetPlayerId }
		};
		if (a_stimpaks) {
			if (*a_stimpaks > kMaximumStimpaksReported) {
				return std::nullopt;
			}
			request["stimpaks"] = *a_stimpaks;
		}
		return request;
	}

	[[nodiscard]] inline std::optional<nlohmann::json> EncodeReviveCancel(
		std::uint32_t a_targetPlayerId,
		std::string_view a_reason)
	{
		if (a_targetPlayerId == 0 || a_reason.empty() || a_reason.size() > kMaximumReasonChars) {
			return std::nullopt;
		}
		return nlohmann::json{
			{ "type", kReviveCancelType },
			{ "target", a_targetPlayerId },
			{ "reason", a_reason }
		};
	}

	[[nodiscard]] inline std::optional<nlohmann::json> EncodeRespawnRequest(
		RespawnPoint a_point,
		std::optional<std::uint32_t> a_friendId)
	{
		nlohmann::json request{
			{ "type", kRespawnRequestType },
			{ "point", GetRespawnPointName(a_point) }
		};
		if (a_point == RespawnPoint::kFriend) {
			if (!a_friendId || *a_friendId == 0) {
				return std::nullopt;
			}
			request["friendId"] = *a_friendId;
		}
		return request;
	}

	[[nodiscard]] inline std::optional<nlohmann::json> EncodeCheckpointReport(const Checkpoint& a_checkpoint)
	{
		if (!IsValidCheckpoint(a_checkpoint)) {
			return std::nullopt;
		}
		return nlohmann::json{
			{ "type", kCheckpointType },
			{ "interior", a_checkpoint.interior },
			{ "space", a_checkpoint.space },
			{ "x", a_checkpoint.x },
			{ "y", a_checkpoint.y },
			{ "z", a_checkpoint.z },
			{ "rotZ", a_checkpoint.rotZ }
		};
	}

	namespace Detail
	{
		[[nodiscard]] inline std::optional<std::uint32_t> ReadPlayerId(
			const nlohmann::json& a_packet,
			std::string_view a_key)
		{
			const auto found = a_packet.find(std::string{ a_key });
			if (found == a_packet.end() || !found->is_number_unsigned()) {
				return std::nullopt;
			}
			const auto value = found->get<std::uint64_t>();
			if (value == 0 || value > 0xFFFFFFFFULL) {
				return std::nullopt;
			}
			return static_cast<std::uint32_t>(value);
		}

		[[nodiscard]] inline std::optional<RespawnPoint> ReadRespawnPoint(const nlohmann::json& a_packet)
		{
			const auto found = a_packet.find("point");
			if (found == a_packet.end() || !found->is_string()) {
				return std::nullopt;
			}
			return ParseRespawnPoint(found->get<std::string>());
		}

		[[nodiscard]] inline bool HasType(const nlohmann::json& a_packet, std::string_view a_type)
		{
			const auto found = a_packet.find("type");
			return found != a_packet.end() && found->is_string() && found->get<std::string>() == a_type;
		}

		[[nodiscard]] inline std::optional<float> ReadCoordinate(
			const nlohmann::json& a_packet,
			std::string_view a_key)
		{
			const auto found = a_packet.find(std::string{ a_key });
			if (found == a_packet.end() || !found->is_number()) {
				return std::nullopt;
			}
			const auto value = found->get<double>();
			if (!std::isfinite(value) || std::fabs(value) > static_cast<double>(kMaximumCheckpointCoordinate)) {
				return std::nullopt;
			}
			return static_cast<float>(value);
		}
	}

	[[nodiscard]] inline std::optional<Checkpoint> DecodeCheckpointObject(const nlohmann::json& a_packet)
	{
		const auto found = a_packet.find("checkpoint");
		if (found == a_packet.end() || !found->is_object()) {
			return std::nullopt;
		}
		const auto interior = found->find("interior");
		if (interior == found->end() || !interior->is_boolean()) {
			return std::nullopt;
		}
		const auto space = Detail::ReadPlayerId(*found, "space");
		const auto x = Detail::ReadCoordinate(*found, "x");
		const auto y = Detail::ReadCoordinate(*found, "y");
		const auto z = Detail::ReadCoordinate(*found, "z");
		const auto rotZField = found->find("rotZ");
		const auto rotZ = rotZField == found->end() || rotZField->is_null() ?
			std::optional<float>{ 0.0F } :
			Detail::ReadCoordinate(*found, "rotZ");
		if (!space || !x || !y || !z || !rotZ) {
			return std::nullopt;
		}
		const Checkpoint checkpoint{ interior->get<bool>(), *space, *x, *y, *z, *rotZ };
		if (!IsValidCheckpoint(checkpoint)) {
			return std::nullopt;
		}
		return checkpoint;
	}

	[[nodiscard]] inline std::optional<DownedStateMessage> DecodeDownedState(const nlohmann::json& a_packet)
	{
		const auto playerId = Detail::ReadPlayerId(a_packet, "playerId");
		const auto downed = a_packet.find("downed");
		const auto remaining = a_packet.find("remainingMs");
		if (!playerId ||
			downed == a_packet.end() || !downed->is_boolean() ||
			remaining == a_packet.end() || !remaining->is_number_integer()) {
			return std::nullopt;
		}
		const auto remainingMs = remaining->get<std::int64_t>();
		if (remainingMs < 0 || remainingMs > kMaximumServerRemainingMs) {
			return std::nullopt;
		}
		return DownedStateMessage{
			*playerId,
			downed->get<bool>(),
			static_cast<std::int32_t>(remainingMs)
		};
	}

	[[nodiscard]] inline std::optional<RevivedMessage> DecodeRevived(const nlohmann::json& a_packet)
	{
		const auto byPlayerId = a_packet.find("byPlayerId");
		const auto fraction = a_packet.find("healthFraction");
		if (byPlayerId == a_packet.end() || !byPlayerId->is_number_unsigned() ||
			fraction == a_packet.end() || !fraction->is_number()) {
			return std::nullopt;
		}
		const auto byId = byPlayerId->get<std::uint64_t>();
		const auto healthFraction = fraction->get<double>();
		if (byId > 0xFFFFFFFFULL || !std::isfinite(healthFraction) || healthFraction < 0.0 || healthFraction > 1.0) {
			return std::nullopt;
		}
		return RevivedMessage{ static_cast<std::uint32_t>(byId), ClampRevivedHealthFraction(healthFraction) };
	}

	[[nodiscard]] inline bool IsDiedPacket(const nlohmann::json& a_packet)
	{
		return Detail::HasType(a_packet, kDiedType);
	}

	[[nodiscard]] inline std::optional<PlayerDiedMessage> DecodePlayerDied(const nlohmann::json& a_packet)
	{
		const auto playerId = Detail::ReadPlayerId(a_packet, "playerId");
		if (!playerId) {
			return std::nullopt;
		}
		return PlayerDiedMessage{ *playerId };
	}

	[[nodiscard]] inline std::optional<RespawnGrantMessage> DecodeRespawnGrant(const nlohmann::json& a_packet)
	{
		const auto point = Detail::ReadRespawnPoint(a_packet);
		if (!point) {
			return std::nullopt;
		}
		if (*point == RespawnPoint::kCheckpoint) {
			const auto checkpoint = DecodeCheckpointObject(a_packet);
			if (!checkpoint) {
				return std::nullopt;
			}
			return RespawnGrantMessage{ *point, std::nullopt, checkpoint };
		}
		if (*point != RespawnPoint::kFriend) {
			return RespawnGrantMessage{ *point, std::nullopt, std::nullopt };
		}
		const auto friendId = Detail::ReadPlayerId(a_packet, "friendId");
		if (!friendId) {
			return std::nullopt;
		}
		return RespawnGrantMessage{ *point, friendId, std::nullopt };
	}

	[[nodiscard]] inline std::optional<ReviveStartedMessage> DecodeReviveStarted(const nlohmann::json& a_packet)
	{
		const auto playerId = Detail::ReadPlayerId(a_packet, "playerId");
		const auto byPlayerId = Detail::ReadPlayerId(a_packet, "byPlayerId");
		const auto channel = a_packet.find("channelMs");
		if (!playerId || !byPlayerId || channel == a_packet.end() || !channel->is_number_integer()) {
			return std::nullopt;
		}
		const auto channelMs = channel->get<std::int64_t>();
		if (channelMs < 0 || channelMs > static_cast<std::int64_t>(kMaximumReviveChannelMs)) {
			return std::nullopt;
		}
		return ReviveStartedMessage{ *playerId, *byPlayerId, static_cast<std::uint32_t>(channelMs) };
	}

	[[nodiscard]] inline std::optional<ReviveCancelledMessage> DecodeReviveCancelled(const nlohmann::json& a_packet)
	{
		const auto playerId = Detail::ReadPlayerId(a_packet, "playerId");
		const auto byPlayerId = Detail::ReadPlayerId(a_packet, "byPlayerId");
		if (!playerId || !byPlayerId) {
			return std::nullopt;
		}
		std::string reason;
		if (const auto found = a_packet.find("reason"); found != a_packet.end() && !found->is_null()) {
			if (!found->is_string()) {
				return std::nullopt;
			}
			reason = found->get<std::string>();
			if (reason.size() > kMaximumReasonChars) {
				return std::nullopt;
			}
		}
		return ReviveCancelledMessage{ *playerId, *byPlayerId, std::move(reason) };
	}

	[[nodiscard]] inline std::optional<std::string> DecodeHeldItemDropped(const nlohmann::json& a_packet)
	{
		std::string reason;
		if (const auto found = a_packet.find("reason"); found != a_packet.end() && !found->is_null()) {
			if (!found->is_string()) {
				return std::nullopt;
			}
			reason = found->get<std::string>();
			if (reason.size() > kMaximumReasonChars) {
				return std::nullopt;
			}
		}
		return reason;
	}

	[[nodiscard]] inline DecodeStatus TryDecodeDownedEvent(
		std::string_view a_type,
		const nlohmann::json& a_packet,
		DownedEvent& a_event)
	{
		if (a_type == kDownedStateType) {
			const auto message = DecodeDownedState(a_packet);
			if (!message) {
				return DecodeStatus::kMalformed;
			}
			a_event = DownedEvent{
				EventKind::kDownedState,
				message->playerId,
				message->downed,
				message->remainingMs,
				1.0,
				RespawnPoint::kSanctuary,
				std::nullopt
			};
			return DecodeStatus::kHandled;
		}
		if (a_type == kRevivedType) {
			const auto message = DecodeRevived(a_packet);
			if (!message) {
				return DecodeStatus::kMalformed;
			}
			a_event = DownedEvent{
				EventKind::kRevived,
				message->byPlayerId,
				false,
				0,
				message->healthFraction,
				RespawnPoint::kSanctuary,
				std::nullopt
			};
			return DecodeStatus::kHandled;
		}
		if (a_type == kDiedType) {
			a_event = DownedEvent{ EventKind::kDied, 0, false, 0, 1.0, RespawnPoint::kSanctuary, std::nullopt };
			return DecodeStatus::kHandled;
		}
		if (a_type == kPlayerDiedType) {
			const auto message = DecodePlayerDied(a_packet);
			if (!message) {
				return DecodeStatus::kMalformed;
			}
			a_event = DownedEvent{
				EventKind::kPlayerDied,
				message->playerId,
				false,
				0,
				1.0,
				RespawnPoint::kSanctuary,
				std::nullopt
			};
			return DecodeStatus::kHandled;
		}
		if (a_type == kRespawnGrantType) {
			const auto message = DecodeRespawnGrant(a_packet);
			if (!message) {
				return DecodeStatus::kMalformed;
			}
			a_event = DownedEvent{
				EventKind::kRespawnGrant,
				0,
				false,
				0,
				1.0,
				message->point,
				message->friendId,
				message->checkpoint
			};
			return DecodeStatus::kHandled;
		}
		if (a_type == kReviveStartedType) {
			const auto message = DecodeReviveStarted(a_packet);
			if (!message) {
				return DecodeStatus::kMalformed;
			}
			a_event = DownedEvent{};
			a_event.kind = EventKind::kReviveStarted;
			a_event.playerId = message->playerId;
			a_event.byPlayerId = message->byPlayerId;
			a_event.channelMs = message->channelMs;
			return DecodeStatus::kHandled;
		}
		if (a_type == kReviveCancelledType) {
			const auto message = DecodeReviveCancelled(a_packet);
			if (!message) {
				return DecodeStatus::kMalformed;
			}
			a_event = DownedEvent{};
			a_event.kind = EventKind::kReviveCancelled;
			a_event.playerId = message->playerId;
			a_event.byPlayerId = message->byPlayerId;
			a_event.reason = std::move(message->reason);
			return DecodeStatus::kHandled;
		}
		if (a_type == kHeldItemDroppedType) {
			const auto reason = DecodeHeldItemDropped(a_packet);
			if (!reason) {
				return DecodeStatus::kMalformed;
			}
			a_event = DownedEvent{};
			a_event.kind = EventKind::kHeldItemDropped;
			a_event.reason = *reason;
			return DecodeStatus::kHandled;
		}
		return DecodeStatus::kUnknown;
	}
}
