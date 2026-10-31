#include "F4TDownedValidation.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <system_error>

namespace F4T::DownedValidation
{
	namespace
	{
		constexpr std::size_t kMaximumPointChars = 32;
		constexpr double kMaximumCheckpointCoordinate = 200000.0;

		std::optional<std::uint32_t> ReadPlayerId(const nlohmann::json& a_node)
		{
			if (a_node.is_boolean()) {
				return std::nullopt;
			}
			try {
				if (a_node.is_number_unsigned()) {
					const auto value = a_node.get<std::uint64_t>();
					if (value >= 1 && value <= (std::numeric_limits<std::uint32_t>::max)()) {
						return static_cast<std::uint32_t>(value);
					}
					return std::nullopt;
				}
				if (a_node.is_number_integer()) {
					const auto value = a_node.get<std::int64_t>();
					if (value >= 1 && static_cast<std::uint64_t>(value) <= (std::numeric_limits<std::uint32_t>::max)()) {
						return static_cast<std::uint32_t>(value);
					}
					return std::nullopt;
				}
				if (a_node.is_number_float()) {
					const auto value = a_node.get<double>();
					if (std::isfinite(value) && value == std::trunc(value) && value >= 1.0 &&
						value <= static_cast<double>((std::numeric_limits<std::uint32_t>::max)())) {
						return static_cast<std::uint32_t>(value);
					}
					return std::nullopt;
				}
				if (a_node.is_string()) {
					const auto text = a_node.get<std::string>();
					std::uint32_t value = 0;
					const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
					if (result.ec == std::errc{} && result.ptr == text.data() + text.size() && value >= 1) {
						return value;
					}
				}
			} catch (...) {
			}
			return std::nullopt;
		}

		std::optional<std::uint32_t> ReadCount(const nlohmann::json& a_node, std::uint32_t a_maximum)
		{
			if (a_node.is_boolean() || a_node.is_number_float() || a_node.is_string()) {
				return std::nullopt;
			}
			try {
				if (a_node.is_number_unsigned()) {
					const auto value = a_node.get<std::uint64_t>();
					if (value <= a_maximum) {
						return static_cast<std::uint32_t>(value);
					}
					return std::nullopt;
				}
				if (a_node.is_number_integer()) {
					const auto value = a_node.get<std::int64_t>();
					if (value >= 0 && static_cast<std::uint64_t>(value) <= a_maximum) {
						return static_cast<std::uint32_t>(value);
					}
				}
			} catch (...) {
			}
			return std::nullopt;
		}

		std::optional<double> ReadCoordinate(const nlohmann::json& a_node)
		{
			if (a_node.is_boolean()) {
				return std::nullopt;
			}
			try {
				if (a_node.is_number_float()) {
					const auto value = a_node.get<double>();
					if (std::isfinite(value) && std::abs(value) <= kMaximumCheckpointCoordinate) {
						return value;
					}
					return std::nullopt;
				}
				if (a_node.is_number_unsigned()) {
					return a_node.get<std::uint64_t>() <= kMaximumCheckpointCoordinate
						? std::optional<double>{ static_cast<double>(a_node.get<std::uint64_t>()) }
						: std::nullopt;
				}
				if (a_node.is_number_integer()) {
					const auto value = a_node.get<std::int64_t>();
					if (std::abs(value) <= kMaximumCheckpointCoordinate) {
						return static_cast<double>(value);
					}
				}
			} catch (...) {
			}
			return std::nullopt;
		}
	}

	ReviveRequest NormalizeReviveRequest(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return { RequestError::Malformed, 0, std::nullopt };
		}
		const auto target = a_packet.find("target");
		if (target == a_packet.end() || target->is_null()) {
			return { RequestError::Malformed, 0, std::nullopt };
		}
		const auto targetPlayerId = ReadPlayerId(*target);
		if (!targetPlayerId) {
			return { RequestError::InvalidTarget, 0, std::nullopt };
		}
		std::optional<std::uint32_t> stimpaks;
		if (const auto found = a_packet.find("stimpaks"); found != a_packet.end() && !found->is_null()) {
			stimpaks = ReadCount(*found, DownedState::MaximumStimpaksReported);
			if (!stimpaks) {
				return { RequestError::InvalidStimpaks, 0, std::nullopt };
			}
		}
		return { std::nullopt, *targetPlayerId, stimpaks };
	}

	ReviveCancelRequest NormalizeReviveCancelRequest(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return { RequestError::Malformed, 0, {} };
		}
		const auto target = a_packet.find("target");
		if (target == a_packet.end() || target->is_null()) {
			return { RequestError::Malformed, 0, {} };
		}
		const auto targetPlayerId = ReadPlayerId(*target);
		if (!targetPlayerId) {
			return { RequestError::InvalidTarget, 0, {} };
		}
		if (const auto found = a_packet.find("reason"); found != a_packet.end() && !found->is_null()) {
			if (!found->is_string()) {
				return { RequestError::InvalidReason, 0, {} };
			}
			const auto reason = found->get<std::string>();
			if (reason.empty() || reason.size() > DownedState::MaximumReasonChars) {
				return { RequestError::InvalidReason, 0, {} };
			}
			return { std::nullopt, *targetPlayerId, reason };
		}
		return { std::nullopt, *targetPlayerId, {} };
	}

	RespawnRequest NormalizeRespawnRequest(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return { RequestError::Malformed, {}, 0 };
		}
		const auto point = a_packet.find("point");
		if (point == a_packet.end() || !point->is_string()) {
			return { RequestError::Malformed, {}, 0 };
		}
		const auto pointText = point->get<std::string>();
		if (pointText.empty() || pointText.size() > kMaximumPointChars) {
			return { RequestError::InvalidPoint, {}, 0 };
		}
		if (const auto friendId = a_packet.find("friendId"); friendId != a_packet.end() && !friendId->is_null()) {
			const auto friendPlayerId = ReadPlayerId(*friendId);
			if (!friendPlayerId) {
				return { RequestError::InvalidFriend, {}, 0 };
			}
			return { std::nullopt, pointText, *friendPlayerId };
		}
		return { std::nullopt, pointText, 0 };
	}

	CheckpointRequest NormalizeCheckpointRequest(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return { RequestError::Malformed, {} };
		}
		const auto interior = a_packet.find("interior");
		if (interior == a_packet.end() || !interior->is_boolean()) {
			return { RequestError::Malformed, {} };
		}
		const auto space = a_packet.find("space");
		if (space == a_packet.end() || space->is_null()) {
			return { RequestError::Malformed, {} };
		}
		const auto parsedSpace = ReadPlayerId(*space);
		if (!parsedSpace || (*parsedSpace >> 24) == 0xFF) {
			return { RequestError::InvalidSpace, {} };
		}
		DownedState::Checkpoint checkpoint;
		checkpoint.interior = interior->get<bool>();
		checkpoint.space = *parsedSpace;
		const auto x = a_packet.find("x");
		const auto y = a_packet.find("y");
		const auto z = a_packet.find("z");
		if (x == a_packet.end() || y == a_packet.end() || z == a_packet.end()) {
			return { RequestError::Malformed, {} };
		}
		const auto parsedX = ReadCoordinate(*x);
		const auto parsedY = ReadCoordinate(*y);
		const auto parsedZ = ReadCoordinate(*z);
		const auto rotZ = a_packet.find("rotZ");
		const auto parsedRotZ = rotZ == a_packet.end() || rotZ->is_null() ? std::optional<double>{ 0.0 } : ReadCoordinate(*rotZ);
		if (!parsedX || !parsedY || !parsedZ || !parsedRotZ) {
			return { RequestError::InvalidPosition, {} };
		}
		checkpoint.x = *parsedX;
		checkpoint.y = *parsedY;
		checkpoint.z = *parsedZ;
		checkpoint.rotZ = *parsedRotZ;
		if (!DownedState::IsValidCheckpoint(checkpoint)) {
			return { RequestError::InvalidPosition, {} };
		}
		return { std::nullopt, checkpoint };
	}

	bool CarriesVitals(const nlohmann::json& a_packet) noexcept
	{
		if (!a_packet.is_object()) {
			return false;
		}
		const auto found = a_packet.find("vitals");
		return found != a_packet.end() && found->is_object();
	}

	Vitals ReadVitals(const nlohmann::json& a_packet) noexcept
	{
		Vitals vitals;
		if (!a_packet.is_object()) {
			return vitals;
		}
		const auto found = a_packet.find("vitals");
		if (found == a_packet.end() || !found->is_object()) {
			return vitals;
		}
		if (const auto downed = found->find("isDowned"); downed != found->end() && downed->is_boolean()) {
			vitals.isDowned = downed->get<bool>();
			vitals.hasStateFlags = true;
		}
		if (const auto dead = found->find("isDead"); dead != found->end() && dead->is_boolean()) {
			vitals.isDead = dead->get<bool>();
			vitals.hasStateFlags = true;
		}
		return vitals;
	}
}
