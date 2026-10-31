#include "AdminCommandDispatcher.h"
#include "DurableStateFileStore.h"
#include "IdentityGate.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::AdminActionResult;
	using CommonwealthOnline::Server::AdminCommandDispatcher;
	using CommonwealthOnline::Server::AdminCommandHooks;
	using CommonwealthOnline::Server::Delivery;
	using CommonwealthOnline::Server::DurableStateFileStore;
	using CommonwealthOnline::Server::IdentityGate;
	using CommonwealthOnline::Server::OutboundFrame;
	using F4T::Identity::TokenHash;

	void Check(bool a_condition)
	{
		assert(a_condition);
		if (!a_condition) {
			std::abort();
		}
	}

	class FakeTokenService final : public F4T::Identity::TokenService
	{
	public:
		std::string CreateToken() override
		{
			return "recovery-token-" + std::to_string(++counter);
		}

		TokenHash HashToken(std::string_view a_token) override
		{
			TokenHash hash{};
			std::uint32_t state = 2166136261u;
			for (const auto ch : a_token) {
				state ^= static_cast<unsigned char>(ch);
				state *= 16777619u;
			}
			for (std::size_t index = 0; index < hash.size(); ++index) {
				state ^= static_cast<std::uint32_t>(index + 1);
				state *= 16777619u;
				hash[index] = static_cast<std::uint8_t>((state >> ((index % 4) * 8)) & 0xffu);
			}
			return hash;
		}

		std::uint32_t counter{};
	};

	std::filesystem::path UniqueDirectory()
	{
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		return std::filesystem::temp_directory_path() / ("co-identity-recovery-" + std::to_string(stamp));
	}

	std::vector<std::uint8_t> Encode(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	Json Decode(const OutboundFrame& a_frame)
	{
		return Json::parse(std::string{
			reinterpret_cast<const char*>(a_frame.payload.data()),
			a_frame.payload.size()
		});
	}

	Json Hello(std::string_view a_profileId, std::string_view a_resumeToken = {})
	{
		Json hello{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "clientVersion", "1.2.0" },
			{ "falloutRuntimeVersion", "1.11.240" },
			{ "profileId", a_profileId },
			{ "contentManifestFingerprint", std::string(64, 'a') },
			{ "capabilities", Json::array({ "interest-v1", "player-state-v1", "save-attestation-v1" }) }
		};
		if (!a_resumeToken.empty()) {
			hello["resumeToken"] = a_resumeToken;
		}
		return hello;
	}

	OutboundFrame Ready(std::uint32_t a_connectionId)
	{
		return OutboundFrame{
			a_connectionId,
			Delivery::ReliableOrdered,
			Encode(Json{ { "type", "sessionReady" }, { "protocolVersion", 2 } })
		};
	}

	std::string ResumeToken(IdentityGate& a_gate, std::uint32_t a_connectionId)
	{
		auto ready = Ready(a_connectionId);
		Check(a_gate.EnrichSessionReady(ready));
		return Decode(ready).at("resumeToken").get<std::string>();
	}

	AdminCommandDispatcher ResetDispatcher(IdentityGate& a_gate)
	{
		return AdminCommandDispatcher(AdminCommandHooks{
			.resetIdentity = [&a_gate](std::string_view a_profileId) {
				std::string reason;
				const auto reset = a_gate.TryResetResumeCredential(a_profileId, reason);
				return AdminActionResult{ reset, reason, reset ? Json{ { "profileId", a_profileId } } : Json(nullptr) };
			}
		});
	}

	void AssertDurablePlayerState(const DurableStateFileStore& a_store, std::string_view a_profileId)
	{
		const auto state = a_store.GetPlayerState(a_profileId);
		Check(state.has_value());
		Check(state->at("characterName") == "Recovery Survivor");
		Check(state->at("appearance").at("version") == 4);
		Check(state->at("equippedItems").size() == 2);
	}
}

int main()
{
	using namespace CommonwealthOnline::Server;

	const auto directory = UniqueDirectory();
	std::filesystem::create_directories(directory);
	double now = 1000.0;
	FakeTokenService tokens;
	const std::string profileId = "profile-recovery";
	const std::string aliasProfileId = "profile-recovery-alias";
	const auto statePath = directory / "durable-state.json";

	F4T::Identity::Requirements requirements{
		.clientVersion = std::string{ "1.2.0" },
		.falloutRuntimeVersion = std::string{ "1.11.240" },
		.contentManifestFingerprint = std::string(64, 'a'),
		.capabilities = { "interest-v1", "player-state-v1" },
		.resumeLifetimeSeconds = 100.0
	};

	DurableStateFileStore store(
		statePath,
		[&] { return now; },
		[] { return "0123456789abcdef0123456789abcdef"; });
	Check(store.UpdatePlayerState(profileId, Json{
		{ "type", "playerState" },
		{ "characterName", "Recovery Survivor" },
		{ "appearance", Json{ { "version", 4 }, { "raceFormId", "00013746" } } },
		{ "equippedItems", Json::array({
			Json{ { "slot", "rightHand" }, { "formId", "0001F66A" } },
			Json{ { "slot", "armor0" }, { "formId", "00023432" } }
		}) }
	}));

	IdentityGate gate(requirements, tokens, &store);
	auto dispatcher = ResetDispatcher(gate);

	Check(gate.InspectInbound(1, Encode(Hello(profileId)), now).forward);
	Check(gate.IsValidated(1));
	const auto tokenA = ResumeToken(gate, 1);
	Check(gate.CommitReady(1));
	gate.ConnectionClosed(1);
	Check(store.GetResumeCredential(profileId).has_value());

	const auto pendingDecision = gate.InspectInbound(2, Encode(Hello(profileId, tokenA)), now + 1.0);
	Check(pendingDecision.forward);
	const auto tokenB = ResumeToken(gate, 2);
	Check(tokenB != tokenA);
	const auto pendingReset = dispatcher.Dispatch(Json{ { "cmd", "resume_reset" }, { "profileId", profileId } });
	Check(!pendingReset.value("ok", true));
	gate.CommitReady(2);
	gate.ConnectionClosed(2);

	const auto activeDecision = gate.InspectInbound(3, Encode(Hello(profileId, tokenB)), now + 2.0);
	Check(activeDecision.forward);
	const auto tokenBeforeActiveReset = ResumeToken(gate, 3);
	Check(gate.CommitReady(3));
	const auto activeReset = dispatcher.Dispatch(Json{ { "cmd", "identity_reset" }, { "profileId", profileId } });
	Check(!activeReset.value("ok", true));
	gate.ConnectionClosed(3);

	const std::string clientResumeToken = tokenA;
	const auto staleDecision = gate.InspectInbound(4, Encode(Hello(profileId, clientResumeToken)), now + 3.0);
	Check(!staleDecision.forward);
	Check(staleDecision.code == "invalid_resume");
	Check(!gate.IsValidated(4));

	const auto reset = dispatcher.Dispatch(Json{ { "cmd", "identity_reset" }, { "profileId", profileId } });
	Check(reset.value("ok", false));
	const auto revokedDecision = gate.InspectInbound(10, Encode(Hello(profileId, tokenBeforeActiveReset)), now + 3.5);
	Check(!revokedDecision.forward);
	Check(revokedDecision.code == "invalid_resume");
	Check(!store.GetResumeCredential(profileId).has_value());
	AssertDurablePlayerState(store, profileId);

	DurableStateFileStore restarted(
		statePath,
		[&] { return now; },
		[] { return "fedcba9876543210fedcba9876543210"; });
	Check(!restarted.GetResumeCredential(profileId).has_value());
	AssertDurablePlayerState(restarted, profileId);

	Check(gate.InspectInbound(5, Encode(Hello(profileId)), now + 4.0).forward);
	const auto tokenC = ResumeToken(gate, 5);
	Check(tokenC != tokenA && tokenC != tokenB && tokenC != tokenBeforeActiveReset);
	Check(gate.CommitReady(5));
	gate.ConnectionClosed(5);

	const auto staleAfterReset = gate.InspectInbound(6, Encode(Hello(profileId, tokenA)), now + 5.0);
	Check(!staleAfterReset.forward);
	Check(staleAfterReset.code == "invalid_resume");
	const auto reconnect = gate.InspectInbound(7, Encode(Hello(profileId, tokenC)), now + 6.0);
	Check(reconnect.forward);
	const auto reconnectToken = ResumeToken(gate, 7);
	Check(reconnectToken != tokenC);
	Check(gate.CommitReady(7));
	gate.ConnectionClosed(7);

	Check(gate.InspectInbound(8, Encode(Hello(aliasProfileId)), now + 7.0).forward);
	const auto aliasToken = ResumeToken(gate, 8);
	Check(gate.CommitReady(8));
	gate.ConnectionClosed(8);
	const auto aliasReset = dispatcher.Dispatch(Json{ { "cmd", "resume_reset" }, { "profile_id", aliasProfileId } });
	Check(aliasReset.value("ok", false));
	Check(!store.GetResumeCredential(aliasProfileId).has_value());
	Check(gate.InspectInbound(9, Encode(Hello(aliasProfileId)), now + 8.0).forward);
	const auto aliasFreshToken = ResumeToken(gate, 9);
	Check(aliasFreshToken != aliasToken);
	Check(gate.CommitReady(9));
	gate.ConnectionClosed(9);

	std::error_code ignored;
	std::filesystem::remove_all(directory, ignored);
	return 0;
}
