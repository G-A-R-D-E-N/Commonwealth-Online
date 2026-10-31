#pragma once

#include "F4TGnsSnapshotEnvelope.h"
#include "PersistenceGate.h"
#include "ServerProtocol.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace FactionTestSupport
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::DurableStateFileStore;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::PersistenceGate;
	using CommonwealthOnline::Server::ProtocolHost;

	inline std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	inline Json DecodePacket(const OutboundFrame& a_frame)
	{
		return Json::parse(std::string{ reinterpret_cast<const char*>(a_frame.payload.data()), a_frame.payload.size() });
	}

	inline const OutboundFrame* FindFrame(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && DecodePacket(frame).value("type", std::string{}) == std::string{ a_type }) {
				return &frame;
			}
		}
		return nullptr;
	}

	inline Json RequirePacket(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		const auto* frame = FindFrame(a_frames, a_connectionId, a_type);
		assert(frame != nullptr);
		return DecodePacket(*frame);
	}

	inline bool HasPacket(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		return FindFrame(a_frames, a_connectionId, a_type) != nullptr;
	}

	inline void AssertResult(
		const std::vector<OutboundFrame>& a_frames,
		std::uint32_t a_connectionId,
		std::string_view a_op,
		bool a_ok,
		std::string_view a_reason = {})
	{
		const auto result = RequirePacket(a_frames, a_connectionId, "factionResult");
		assert(result.at("op") == std::string{ a_op });
		assert(result.at("ok") == a_ok);
		assert(result.value("reason", std::string{}) == std::string{ a_reason });
	}

	inline std::vector<OutboundFrame> Send(ProtocolHost& a_host, std::uint32_t a_connectionId, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		return a_host.HandleMessage(a_connectionId, EncodeJson(a_packet), a_monotonicSeconds, a_unixTimeSeconds);
	}

	inline Json Create(std::string_view a_name, std::string_view a_tag = {})
	{
		Json packet{ { "type", "factionCreate" }, { "name", std::string{ a_name } } };
		if (!a_tag.empty()) packet["tag"] = std::string{ a_tag };
		return packet;
	}

	inline Json Invite(std::uint32_t a_targetPlayerId)
	{
		return Json{ { "type", "factionInvite" }, { "targetPlayerId", a_targetPlayerId } };
	}

	inline Json Respond(const std::string& a_inviteId, bool a_accept)
	{
		return Json{ { "type", "factionInviteResponse" }, { "inviteId", a_inviteId }, { "accept", a_accept } };
	}

	inline Json Leave()
	{
		return Json{ { "type", "factionLeave" } };
	}

	inline Json Kick(const std::string& a_targetProfileId)
	{
		return Json{ { "type", "factionKick" }, { "targetProfileId", a_targetProfileId } };
	}

	inline Json Disband()
	{
		return Json{ { "type", "factionDisband" } };
	}

	inline Json Promote(const std::string& a_targetProfileId)
	{
		return Json{ { "type", "factionPromote" }, { "targetProfileId", a_targetProfileId } };
	}

	inline Json SetRank(const std::string& a_targetProfileId, const std::string& a_rank)
	{
		return Json{ { "type", "factionSetRank" }, { "targetProfileId", a_targetProfileId }, { "rank", a_rank } };
	}

	inline Json SetProfile(std::string_view a_description, std::string_view a_color)
	{
		return Json{ { "type", "factionSetProfile" }, { "description", std::string{ a_description } }, { "color", std::string{ a_color } } };
	}

	inline Json Stored(std::string_view a_name, std::string_view a_tag, std::string_view a_leader, Json a_members, Json a_memberNames)
	{
		return Json{
			{ "name", std::string{ a_name } },
			{ "tag", std::string{ a_tag } },
			{ "leaderProfileId", std::string{ a_leader } },
			{ "members", std::move(a_members) },
			{ "memberNames", std::move(a_memberNames) },
			{ "createdAt", 1.0 }
		};
	}

	struct Joined
	{
		std::uint32_t playerId{};
		std::vector<OutboundFrame> hello;
	};

	inline Joined Join(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_profileId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		a_host.BindSessionProfile(a_connectionId, a_profileId);
		Joined joined;
		joined.playerId = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		joined.hello = a_host.HandleMessage(a_connectionId, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1" }) } }), a_monotonicSeconds, a_unixTimeSeconds + 0.1);
		assert(HasPacket(joined.hello, a_connectionId, "sessionReady"));
		return joined;
	}

	inline std::string InviteIdFrom(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_targetConnectionId)
	{
		return RequirePacket(a_frames, a_targetConnectionId, "factionInviteReceived").at("inviteId").get<std::string>();
	}

	using F4T::GnsSnapshotEnvelope::Family;

	inline std::vector<std::uint8_t> EncodeSnapshot(const Json& a_packet, Family a_family, std::uint32_t a_sequence)
	{
		const auto json = a_packet.dump();
		std::vector<std::uint8_t> output(F4T::GnsSnapshotEnvelope::HeaderSize + json.size());
		assert(F4T::GnsSnapshotEnvelope::WriteHeader(std::span<std::uint8_t>{ output }, a_family, a_sequence));
		std::copy(json.begin(), json.end(), output.begin() + static_cast<std::ptrdiff_t>(F4T::GnsSnapshotEnvelope::HeaderSize));
		return output;
	}

	inline Json Transform(double a_x)
	{
		return Json{
			{ "type", "transform" },
			{ "cell" "Id", "CAFE" },
			{ "worldspace" "Id", "F00D" },
			{ "x", a_x },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "movementType", "spawn" }
		};
	}

	inline Json RangedPlayerState(std::uint32_t a_actionSequence)
	{
		return Json{
			{ "type", "playerState" },
			{ "equippedItems", Json::array({ Json{ { "slot", "rightHand" }, { "form" "Id", "1234" } } }) },
			{ "actionEvents", Json::array({ Json{ { "sequence", a_actionSequence }, { "type", 3 }, { "eventName", "fireSingle" } } }) }
		};
	}

	inline Json RangedHit(std::uint32_t a_targetPlayerId, std::uint32_t a_sequence)
	{
		return Json{
			{ "type", "combatHit" },
			{ "targetPlayerId", a_targetPlayerId },
			{ "sequence", a_sequence },
			{ "attackSequence", 10 },
			{ "damage", 25.0 },
			{ "weaponForm" "Id", "1234" }
		};
	}

	inline void ArmRangedAttacker(ProtocolHost& a_host, std::uint32_t a_attackerConnectionId, std::uint32_t a_victimConnectionId)
	{
		a_host.HandleMessage(a_attackerConnectionId, EncodeSnapshot(Transform(0.0), Family::Transform, 1), 2.0, 101.0);
		a_host.HandleMessage(a_victimConnectionId, EncodeSnapshot(Transform(100.0), Family::Transform, 1), 2.1, 101.1);
		Send(a_host, a_attackerConnectionId, RangedPlayerState(10), 2.2, 101.2);
	}

	inline std::vector<OutboundFrame> SendRangedHit(ProtocolHost& a_host, std::uint32_t a_attackerConnectionId, std::uint32_t a_targetPlayerId, std::uint32_t a_sequence, double a_monotonicSeconds)
	{
		return Send(a_host, a_attackerConnectionId, RangedHit(a_targetPlayerId, a_sequence), a_monotonicSeconds, a_monotonicSeconds + 99.0);
	}

	inline void FormFaction(ProtocolHost& a_host, std::uint32_t a_leaderConnectionId, std::uint32_t a_memberConnectionId, std::uint32_t a_memberPlayerId, std::string_view a_name)
	{
		AssertResult(Send(a_host, a_leaderConnectionId, Create(a_name), 2.0, 101.0), a_leaderConnectionId, "create", true);
		const auto invite = Send(a_host, a_leaderConnectionId, Invite(a_memberPlayerId), 2.1, 101.1);
		AssertResult(invite, a_leaderConnectionId, "invite", true);
		AssertResult(Send(a_host, a_memberConnectionId, Respond(InviteIdFrom(invite, a_memberConnectionId), true), 2.2, 101.2), a_memberConnectionId, "inviteResponse", true);
	}

	inline std::filesystem::path TempDirectory(std::string_view a_suffix)
	{
		auto directory = std::filesystem::temp_directory_path() /
			("co-server-factions-" + std::string{ a_suffix } + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		return directory;
	}
}
