// SPDX-License-Identifier: GPL-3.0-or-later
// Design informed by FathersFallout (FatherLamb), GPL-3.0 with additional terms; used with the author's permission.
#include "F4TSteamTunnel.h"

#include "F4TTransportEndpoint.h"

#include <algorithm>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace F4T::Transport
{
	bool SteamApi::IsComplete() const
	{
		return getSteamId && initRelay && createListenSocketP2P && connectP2P && acceptConnection &&
			closeConnection && closeListenSocket && sendMessage && receiveMessages &&
			pollConnectionEvents && runCallbacks;
	}

	bool UdpSocketApi::IsComplete() const
	{
		return openLoopback && close && sendTo && receive;
	}

	std::string_view DescribeTunnelError(TunnelError a_error)
	{
		switch (a_error) {
		case TunnelError::None: return "ok";
		case TunnelError::InvalidArgument: return "invalid argument";
		case TunnelError::SteamUnavailable: return "the Steam networking accessors are not available";
		case TunnelError::RelayNotReady: return "the Steam relay network is not ready";
		case TunnelError::ListenSocketFailed: return "the Steam P2P listen socket could not be opened";
		case TunnelError::LoopbackSocketFailed: return "the loopback socket could not be opened";
		case TunnelError::ConnectionFailed: return "the Steam P2P connection could not be started";
		case TunnelError::NotRunning: return "the tunnel is not running";
		}
		return "unknown tunnel error";
	}

	std::string_view DescribeTunnelState(TunnelState a_state)
	{
		switch (a_state) {
		case TunnelState::Closed: return "closed";
		case TunnelState::Connecting: return "still connecting";
		case TunnelState::Connected: return "connected";
		case TunnelState::Failed: return "failed";
		}
		return "unknown";
	}

	std::string_view DescribeConnectionEnd(int a_endReason)
	{
		switch (a_endReason) {
		case kConnectionEndNotPermitted: return "That host only accepts Steam friends.";
		case kConnectionEndServerFull: return "That host is full.";
		case kConnectionEndLocalSocketFailed: return "The host could not open a relay port for you.";
		case kConnectionEndIdentityUnlinked: return "The host could not confirm your Steam identity.";
		case kConnectionEndFriendsOnly: return "That host only accepts Steam friends.";
		case kConnectionEndFriendCheckUnavailable: return "The host's Steam could not check whether you are friends. Try again in a moment.";
		case kConnectionEndUndeliverable: return SteamAdmission::GuestMessage(a_endReason);
		default: return "The Steam link ended before the host answered.";
		}
	}

	std::string_view DescribeTunnelEventKind(TunnelEventKind a_kind)
	{
		switch (a_kind) {
		case TunnelEventKind::TunnelStarted: return "tunnel started";
		case TunnelEventKind::TunnelStopped: return "tunnel stopped";
		case TunnelEventKind::LinkStateChanged: return "link state changed";
		case TunnelEventKind::PeerAccepted: return "peer accepted";
		case TunnelEventKind::PeerRefused: return "peer refused";
		case TunnelEventKind::PeerDropped: return "peer dropped";
		case TunnelEventKind::IdentityBound: return "identity bound";
		case TunnelEventKind::IdentityBindFailed: return "identity bind failed";
		case TunnelEventKind::FirstDatagramForwarded: return "first datagram forwarded";
		}
		return "unknown tunnel event";
	}

	std::string FormatTunnelEvent(const TunnelEvent& a_event)
	{
		std::string text{ DescribeTunnelEventKind(a_event.kind) };
		if (a_event.connection != kInvalidSteamConnection) {
			text += " connection=";
			text += std::to_string(a_event.connection);
		}
		if (a_event.peerSteamId != 0) {
			text += " peer=";
			text += std::to_string(a_event.peerSteamId);
		}
		if (a_event.port != 0) {
			text += " port=";
			text += std::to_string(a_event.port);
		}
		if (a_event.kind == TunnelEventKind::LinkStateChanged) {
			text += " state=";
			text += DescribeTunnelState(a_event.state);
		}
		if (a_event.code != 0) {
			text += " code=";
			text += std::to_string(a_event.code);
			text += " reason=";
			text += DescribeConnectionEnd(a_event.code);
		}
		if (!a_event.detail.empty()) {
			text += " detail=";
			text += a_event.detail;
		}
		return text;
	}

	SteamAdmissionPolicy MakeFriendsOnlyPolicy(
		std::function<bool(std::uint64_t)> a_isFriend,
		std::vector<std::uint64_t> a_allowSet)
	{
		return [isFriend = std::move(a_isFriend), allowSet = std::move(a_allowSet)](std::uint64_t a_peerSteamId) {
			if (a_peerSteamId == 0) return false;
			if (std::find(allowSet.begin(), allowSet.end(), a_peerSteamId) != allowSet.end()) return true;
			return isFriend && isFriend(a_peerSteamId);
		};
	}

	std::optional<std::uint64_t> ReadSteamHostId(std::string_view a_text)
	{
		const auto endpoint = ParseEndpoint(a_text);
		if (!endpoint || endpoint->kind != EndpointKind::SteamId) return std::nullopt;
		return endpoint->steamId64;
	}

	std::string FormatLoopbackEndpoint(std::uint16_t a_port)
	{
		return FormatEndpoint(TransportEndpoint{ EndpointKind::DirectIpv4, std::string{ kLoopbackHost }, a_port, 0 });
	}
}
