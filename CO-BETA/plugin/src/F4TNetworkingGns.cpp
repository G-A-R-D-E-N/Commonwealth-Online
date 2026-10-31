#include "pch.h"

#include "F4TConnectionPolicy.h"

#include "F4TNetworking.h"
#include "F4TClientIdentity.h"
#include "F4TClientModpackProbe.h"
#include "F4TConnectionIdentityGate.h"
#include "F4TCrashForensics.h"
#include "F4TDebugLogging.h"
#include "F4TIntroQuestProbe.h"
#include "F4TJoinFlow.h"
#include "F4TLocalPlayerTransformSend.h"
#include "F4TNetworkingGnsInternal.h"
#include "F4TRemotePlayerState.h"
#include "F4TSessionLaunch.h"
#include "F4TServerProfileStore.h"
#include "F4TSoloCell.h"
#include "F4TSteamId.h"
#include "F4TSteamTunnelService.h"
#include "F4TUIClient.h"
#include "F4TValidationAttempt.h"

#include "RE/L/LoadingMenu.h"
#include "RE/M/MainMenu.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESWorldSpace.h"
#include "RE/U/UI.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <charconv>
#include <chrono>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>

#include "F4TGnsWinsockShim.h"

namespace
{
	constexpr auto kLocalServerPort = 7777;

	using ConnectFailure = F4T::Networking::ConnectFailure;
	using ConnectFailureKind = F4T::Networking::ConnectFailureKind;
	using ValidationAttempt = F4T::ValidationAttempt;

	thread_local std::shared_ptr<ValidationAttempt> g_activeValidationAttempt;
	std::mutex g_validationSocketMutex;
	std::unordered_map<SOCKET, std::weak_ptr<ValidationAttempt>> g_validationSockets;

	bool HasGameplayReadyParentCell(const RE::PlayerCharacter& a_player)
	{
		const auto* cell = a_player.GetParentCell();
		return cell && !F4T::SoloCell::IsSoloCell(*cell);
	}

	bool HasBlockingGameMenu()
	{
		const auto* ui = RE::UI::GetSingleton();
		return ui && (ui->GetMenuOpen<RE::LoadingMenu>() || ui->GetMenuOpen<RE::MainMenu>());
	}

	bool CanApplyPositionCorrection(const RE::PlayerCharacter& a_player)
	{
		return F4T::SessionLaunch::CanPublishGameplayTransform() &&
		       F4T::JoinFlow::CanPublishGameplayTransform() &&
		       !HasBlockingGameMenu() &&
		       HasGameplayReadyParentCell(a_player);
	}

	bool ShouldLogCorrectionGate()
	{
		static auto lastLog = std::chrono::steady_clock::time_point{};
		const auto now = std::chrono::steady_clock::now();
		if (lastLog.time_since_epoch().count() != 0 && now - lastLog < 2s) {
			return false;
		}
		lastLog = now;
		return true;
	}

	void DetachValidationAttempt(const std::shared_ptr<ValidationAttempt>& a_attempt)
	{
		const std::scoped_lock lock(g_validationSocketMutex);
		for (auto it = g_validationSockets.begin(); it != g_validationSockets.end();) {
			const auto attempt = it->second.lock();
			if (!attempt || attempt == a_attempt) {
				it = g_validationSockets.erase(it);
			} else {
				++it;
			}
		}
	}

	std::string FormatValidationFailure(const ConnectFailure& a_failure)
	{
		switch (a_failure.kind) {
		case ConnectFailureKind::LocalHandshakeFailure:
			return a_failure.detail.empty() ?
				"The client could not complete the session handshake." :
				a_failure.detail;
		case ConnectFailureKind::SessionRejected:
			if (!a_failure.code.empty() && !a_failure.detail.empty()) {
				return std::format("Server rejected the session ({}): {}", a_failure.code, a_failure.detail);
			}
			if (!a_failure.code.empty()) {
				return std::format("Server rejected the session ({}).", a_failure.code);
			}
			if (!a_failure.detail.empty()) {
				return std::format("Server rejected the session: {}", a_failure.detail);
			}
			return "Server rejected the session.";
		case ConnectFailureKind::AdmissionRefused:
		default:
			if (!a_failure.detail.empty()) {
				return std::format("Server refused the connection: {}", a_failure.detail);
			}
			if (a_failure.transportReason != 0) {
				return std::format("Server refused the connection (GNS reason {}).", a_failure.transportReason);
			}
			return "Server refused the connection.";
		}
	}

	std::string TrimAsciiWhitespace(std::string_view a_text)
	{
		while (!a_text.empty() &&
		       (a_text.front() == ' ' || a_text.front() == '\t' || a_text.front() == '\r' || a_text.front() == '\n')) {
			a_text.remove_prefix(1);
		}
		while (!a_text.empty() &&
		       (a_text.back() == ' ' || a_text.back() == '\t' || a_text.back() == '\r' || a_text.back() == '\n')) {
			a_text.remove_suffix(1);
		}
		return std::string{ a_text };
	}

	bool NormalizeConnectEndpoint(std::string& a_host, std::uint16_t& a_port)
	{
		a_host = TrimAsciiWhitespace(a_host);
		return F4T::ConnectionPolicy::NormalizeConnectEndpoint(a_host, a_port);
	}

	std::optional<float> ReadCorrectionFloat(const nlohmann::json& a_packet, const char* a_field)
	{
		const auto found = a_packet.find(a_field);
		if (found == a_packet.end() || !found->is_number()) {
			return std::nullopt;
		}
		try {
			const auto value = found->get<double>();
			if (!std::isfinite(value) || value < -(std::numeric_limits<float>::max)() ||
				value > (std::numeric_limits<float>::max)()) {
				return std::nullopt;
			}
			return static_cast<float>(value);
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<std::uint32_t> ReadCorrectionFormId(const nlohmann::json& a_packet, const char* a_field)
	{
		const auto found = a_packet.find(a_field);
		if (found == a_packet.end() || found->is_null() || !found->is_string()) {
			return std::nullopt;
		}
		const auto text = found->get<std::string>();
		if (text.empty() || text.size() > 8) {
			return std::nullopt;
		}
		std::uint32_t value = 0;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
		if (error != std::errc{} || end != text.data() + text.size()) {
			return std::nullopt;
		}
		return value;
	}

	bool PlayerMatchesCorrectionScope(
		RE::PlayerCharacter& a_player,
		const std::optional<std::uint32_t>& a_cellId,
		const std::optional<std::uint32_t>& a_worldspaceId)
	{
		const auto* cell = a_player.GetParentCell();
		if (a_cellId && (!cell || cell->GetFormID() != *a_cellId)) {
			return false;
		}
		if (a_worldspaceId) {
			const RE::TESWorldSpace* worldspace = nullptr;
			if (cell && cell->IsExterior()) {
				worldspace = cell->worldSpace;
			}
			if (!worldspace) {
				worldspace = a_player.cachedWorldspace;
			}
			if (!worldspace || worldspace->GetFormID() != *a_worldspaceId) {
				return false;
			}
		}
		return true;
	}

	void QueuePositionCorrection(
		float a_x,
		float a_y,
		float a_z,
		float a_angleZ,
		std::optional<std::uint32_t> a_cellId,
		std::optional<std::uint32_t> a_worldspaceId)
	{
		const auto* tasks = F4SE::GetTaskInterface();
		if (!tasks) {
			REX::WARN("Commonwealth Online position correction dropped: F4SE task interface unavailable.");
			return;
		}

		tasks->AddTask([a_x, a_y, a_z, a_angleZ, a_cellId, a_worldspaceId]() {
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			if (!CanApplyPositionCorrection(*player)) {
				if (ShouldLogCorrectionGate()) {
					F4T::DebugLogging::Log(
						"Networking",
						"ignored server position correction before gameplay readiness, while loading, or outside a gameplay-ready cell");
				}
				return;
			}
			if (!PlayerMatchesCorrectionScope(*player, a_cellId, a_worldspaceId)) {
				if (ShouldLogCorrectionGate()) {
					F4T::DebugLogging::Log(
						"Networking",
						"ignored stale position correction for a different play space");
				}
				return;
			}

			// source:CO-BETA/plugin/lib/commonlibf4/include/RE/T/TESForm.h
			F4T::CrashForensics::Record(F4T::MutationRing::MutationSite::kPositionCorrection, player->GetFormID(), reinterpret_cast<std::uintptr_t>(player), a_cellId.value_or(0));
			player->SetPosition(RE::NiPoint3{ a_x, a_y, a_z }, true);
			player->SetAngleOnReference(RE::NiPoint3{ 0.0F, 0.0F, a_angleZ });
			player->SetHeading(a_angleZ);
			F4T::DebugLogging::Log(
				"Networking",
				"applied server position correction pos=({:.1f},{:.1f},{:.1f}) angleZ={:.3f}",
				a_x,
				a_y,
				a_z,
				a_angleZ);
		});
	}

	bool ConsumePositionCorrection(const char* a_buffer, int a_length)
	{
		if (!a_buffer || a_length <= 0) {
			return false;
		}
		std::string_view text{ a_buffer, static_cast<std::size_t>(a_length) };
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
			text.remove_suffix(1);
		}
		if (text.empty() || text.front() != '{') {
			return false;
		}

		const auto packet = nlohmann::json::parse(text, nullptr, false);
		if (packet.is_discarded() || !packet.is_object()) {
			return false;
		}
		const auto type = packet.find("type");
		if (type == packet.end() || !type->is_string() || type->get_ref<const std::string&>() != "positionCorrection") {
			return false;
		}
		F4T::IntroQuestProbe::LogServerPositionCorrection(text);

		const auto x = ReadCorrectionFloat(packet, "x");
		const auto y = ReadCorrectionFloat(packet, "y");
		const auto z = ReadCorrectionFloat(packet, "z");
		const auto angleZ = ReadCorrectionFloat(packet, "angleZ");
		if (!x || !y || !z || !angleZ) {
			REX::WARN("Commonwealth Online rejected malformed server position correction.");
			return true;
		}

		QueuePositionCorrection(
			*x,
			*y,
			*z,
			*angleZ,
			ReadCorrectionFormId(packet, "cellId"),
			ReadCorrectionFormId(packet, "worldspaceId"));
		return true;
	}
}

namespace F4T::NetworkingGnsInternal
{
	SOCKET SocketForValidation(int a_addressFamily, int a_type, int a_protocol) noexcept
	{
		const auto socketValue = F4T::GnsWinsockShim::Socket(a_addressFamily, a_type, a_protocol);
		if (socketValue != INVALID_SOCKET && g_activeValidationAttempt) {
			const std::scoped_lock lock(g_validationSocketMutex);
			g_validationSockets[socketValue] = g_activeValidationAttempt;
		}
		return socketValue;
	}

	int CloseSocketForValidation(SOCKET a_socket) noexcept
	{
		std::shared_ptr<ValidationAttempt> attempt;
		{
			const std::scoped_lock lock(g_validationSocketMutex);
			const auto found = g_validationSockets.find(a_socket);
			if (found != g_validationSockets.end()) {
				attempt = found->second.lock();
				g_validationSockets.erase(found);
			}
		}
		if (auto failure = F4T::GnsWinsockShim::TakeConnectFailure(a_socket)) {
			if (attempt) {
				attempt->Publish(std::move(*failure));
			}
		}
		return F4T::GnsWinsockShim::CloseSocket(a_socket);
	}

	int RecvWithPositionCorrection(SOCKET a_socket, char* a_buffer, int a_length, int a_flags) noexcept
	{
		for (;;) {
			const auto received = F4T::GnsWinsockShim::Recv(a_socket, a_buffer, a_length, a_flags);
			if (received <= 0 || !ConsumePositionCorrection(a_buffer, received)) {
				return received;
			}
		}
	}
}

namespace F4T::Networking
{
	bool SendTransformPacket(
		const F4T::ReplicationRuntime::PlayerTransform& a_transform,
		const F4T::ReplicationRuntime::PlayerLocation& a_location,
		const F4T::ReplicationRuntime::PlayerMovementState& a_movement,
		const char* a_movementType,
		float a_animationGraphSpeed,
		std::span<const F4T::RemotePlayerState::RemoteEquippedItem> a_equippedItems,
		const F4T::RemotePlayerState::RemoteAppearance* a_appearance,
		std::span<const F4T::RemotePlayerState::RemoteActionEvent> a_actionEvents,
		const char* a_characterName,
		const F4T::PowerArmorSync::PowerArmorState* a_powerArmor)
	{
		static bool suppressionLogged = false;
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player ||
			!F4T::SessionLaunch::CanPublishGameplayTransform() ||
			!F4T::JoinFlow::CanPublishGameplayTransform() ||
			HasBlockingGameMenu() ||
			!HasGameplayReadyParentCell(*player)) {
			if (!suppressionLogged) {
				F4T::DebugLogging::Log(
					"Networking",
					"suppressed local transform while loading, onboarding, or outside a gameplay-ready cell");
				suppressionLogged = true;
			}
			return false;
		}

		const auto sent = F4T::LocalPlayerTransformSend::Send(
			a_transform,
			a_location,
			a_movement,
			a_movementType,
			a_animationGraphSpeed,
			a_equippedItems,
			a_appearance,
			a_actionEvents,
			a_characterName,
			a_powerArmor);
		if (sent && suppressionLogged) {
			F4T::DebugLogging::Log(
				"Networking",
				"first gameplay transform after suppression cell={:08X} world={:08X} pos=({:.1f},{:.1f},{:.1f})",
				a_location.hasCellId ? a_location.cellId : 0U,
				a_location.hasWorldspaceId ? a_location.worldspaceId : 0U,
				a_transform.x,
				a_transform.y,
				a_transform.z);
			suppressionLogged = false;
		}
		return sent;
	}

	bool ConnectAndValidateServerOnce(
		const char* a_host,
		std::uint16_t a_port,
		std::string& a_error,
		std::optional<ConnectFailure>* a_failureOut,
		const std::optional<std::string>& a_serverPassword,
		const std::optional<std::string>& a_modpackFingerprint,
		const std::string& a_joinId)
	{
		a_error.clear();
		if (a_failureOut) {
			a_failureOut->reset();
		}
		if (!a_host || a_host[0] == '\0') {
			a_error = "Server address is required.";
			return false;
		}

		auto host = std::string{ a_host };
		auto port = a_port > 0 ? a_port : static_cast<std::uint16_t>(kLocalServerPort);
		if (!NormalizeConnectEndpoint(host, port)) {
			a_error = "Server address is required.";
			return false;
		}

		auto attempt = std::make_shared<ValidationAttempt>();
		bool identityPrepared = false;
		F4T::JoinFlow::BeginConnecting();
		const bool connected = F4T::ConnectionIdentityGate::PrepareHoldConnect(
			[] { F4T::ClientIdentity::ClearConnectionIdentity(); },
			[&](std::string& a_identityError) {
				return F4T::ClientIdentity::PrepareConnectionIdentity(host, port, a_identityError, a_serverPassword, a_joinId);
			},
			[&](auto a_identity) {
				a_identity.modpackFingerprint = a_modpackFingerprint;
				if (const auto steamId = F4T::Steam::MySteamId(); F4T::SteamId::IsIndividual(steamId)) {
					a_identity.steamId = std::to_string(steamId);
					a_identity.steamTicket = F4T::Steam::CreateAuthTicketHex();
				}
				F4T::ClientIdentity::HoldConnectionIdentity(std::move(a_identity));
				identityPrepared = true;
			},
			[&](std::string& a_connectError) {
				F4T::GnsWinsockShim::TakeCurrentThreadConnectFailure();
				g_activeValidationAttempt = attempt;
				const auto result = ConnectAndValidateServerLegacy(host.c_str(), port, a_connectError);
				g_activeValidationAttempt.reset();
				return result;
			},
			a_error);
		g_activeValidationAttempt.reset();
		if (!connected && !identityPrepared && a_error.empty()) {
			a_error = "Failed to prepare client identity.";
		}

		if (connected) {
			DetachValidationAttempt(attempt);
			F4T::GnsWinsockShim::TakeCurrentThreadConnectFailure();
			F4T::JoinFlow::NotifyAuthenticated(false);
			return true;
		}

		F4T::ClientIdentity::ClearConnectionIdentity();
		auto failure = attempt->Take();
		if (!failure) {
			failure = F4T::GnsWinsockShim::TakeCurrentThreadConnectFailure();
		}
		if (failure) {
			a_error = FormatValidationFailure(*failure);
			if (a_failureOut) {
				*a_failureOut = std::move(*failure);
			}
		}
		return false;
	}

	bool ConnectAndValidateServer(
		const char* a_host,
		std::uint16_t a_port,
		std::string& a_error,
		std::optional<ConnectFailure>* a_failureOut,
		const std::optional<std::string>& a_serverPassword,
		const std::string& a_joinId)
	{
		std::optional<ConnectFailure> failure;
		auto endpointHost = std::string{ a_host ? a_host : "" };
		auto endpointPort = a_port > 0 ? a_port : static_cast<std::uint16_t>(kLocalServerPort);
		const bool endpointValid = !endpointHost.empty() && NormalizeConnectEndpoint(endpointHost, endpointPort);
		const auto serverKey = endpointValid ? F4T::ServerProfileStore::ProfileServerKey(endpointHost, endpointPort, a_joinId) : std::string{};
		const auto cachedFingerprint = endpointValid ? F4T::ClientModpackProbe::CachedFingerprint(serverKey) : std::optional<std::string>{};
		auto connected = ConnectAndValidateServerOnce(a_host, a_port, a_error, &failure, a_serverPassword, cachedFingerprint, a_joinId);
		const bool staleCacheRejected = cachedFingerprint && !connected && failure &&
			failure->kind == ConnectFailureKind::SessionRejected && failure->code == "modpack_mismatch" &&
			failure->matchingModpackFingerprint.empty();
		if (staleCacheRejected) {
			F4T::ClientModpackProbe::Forget(serverKey);
		}
		if (!connected && failure && !failure->matchingModpackFingerprint.empty()) {
			const auto matching = failure->matchingModpackFingerprint;
			failure.reset();
			connected = ConnectAndValidateServerOnce(a_host, a_port, a_error, &failure, a_serverPassword, matching, a_joinId);
		}
		if (!connected && failure && failure->code == "duplicate_identity") {
			constexpr auto kRetryInterval = std::chrono::seconds{ 3 };
			constexpr auto kRetryBudget = std::chrono::seconds{ 20 };
			const auto generation = F4T::Networking::GetConnectionGeneration();
			F4T::UIClient::PushConnectionStatus("Reconnecting...");
			const auto retryDeadline = std::chrono::steady_clock::now() + kRetryBudget;
			while (!connected && std::chrono::steady_clock::now() < retryDeadline &&
				F4T::Networking::GetConnectionGeneration() == generation) {
				std::this_thread::sleep_for(kRetryInterval);
				if (F4T::Networking::GetConnectionGeneration() != generation) {
					break;
				}
				failure.reset();
				a_error.clear();
				connected = ConnectAndValidateServerOnce(a_host, a_port, a_error, &failure, a_serverPassword, cachedFingerprint, a_joinId);
			}
			F4T::UIClient::PushConnectionStatus();
		}
		if (!connected) {
			F4T::JoinFlow::EndSessionJoin(false, a_error);
		}
		if (a_failureOut) {
			*a_failureOut = std::move(failure);
		}
		return connected;
	}

	bool ConnectAndValidateServer(const char* a_host, std::uint16_t a_port, std::string& a_error)
	{
		return ConnectAndValidateServer(a_host, a_port, a_error, nullptr, std::nullopt);
	}

	bool ConnectToServer(const char* a_host, std::uint16_t a_port)
	{
		std::string error;
		return ConnectAndValidateServer(a_host, a_port, error);
	}

	bool ConnectToLocalServer(std::string& a_error)
	{
		return ConnectAndValidateServer("127.0.0.1", 7777, a_error);
	}

	bool ConnectToLocalServer()
	{
		std::string error;
		return ConnectToLocalServer(error);
	}

	std::optional<NpcAuthorityAssignment> GetNpcAuthorityForScope(
		std::string_view a_cellId,
		std::string_view a_worldspaceId)
	{
		const auto authority = F4T::GnsWinsockShim::GetNpcAuthorityForScope(a_cellId, a_worldspaceId);
		if (!authority) {
			return std::nullopt;
		}
		return NpcAuthorityAssignment{ authority->playerId, authority->epoch };
	}
}
