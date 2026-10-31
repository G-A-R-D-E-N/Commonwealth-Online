#include "F4TGnsProtocolAdapter.h"
#include "co_gns_client_bridge.h"

#include <dlfcn.h>

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	using ClientIdentity = F4T::GnsProtocolAdapter::ClientIdentity;
	using Metadata = F4T::ClientCompatibility::Metadata;
	using ReadyIdentity = F4T::GnsProtocolAdapter::ReadyIdentity;
	using Rejection = F4T::GnsProtocolAdapter::SessionRejection;
	using Session = F4T::GnsProtocolAdapter::Session;

	inline constexpr std::string_view kRequiredCapability = "acceptance-required";

	struct BridgeApi
	{
		void* module{};
		decltype(&co_gns_client_create) create{};
		decltype(&co_gns_client_destroy) destroy{};
		decltype(&co_gns_client_poll) poll{};
		decltype(&co_gns_client_send) send{};
		decltype(&co_gns_client_disconnect) disconnect{};
	};

	struct Endpoint
	{
		const BridgeApi& api;
		std::string host;
		std::uint16_t port{};
	};

	struct HandshakeOutcome
	{
		std::optional<ReadyIdentity> ready;
		std::optional<Rejection> rejection;
	};

	template <class T>
	bool Resolve(void* a_module, const char* a_name, T& a_output)
	{
		a_output = reinterpret_cast<T>(dlsym(a_module, a_name));
		return a_output != nullptr;
	}

	bool LoadBridge(std::string_view a_path, BridgeApi& a_api, std::string& a_error)
	{
		const std::string path{ a_path };
		a_api.module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
		if (a_api.module == nullptr) {
			const char* loaderError = dlerror();
			a_error = loaderError != nullptr ? loaderError : "dlopen failed";
			return false;
		}
		if (!Resolve(a_api.module, "co_gns_client_create", a_api.create) ||
			!Resolve(a_api.module, "co_gns_client_destroy", a_api.destroy) ||
			!Resolve(a_api.module, "co_gns_client_poll", a_api.poll) ||
			!Resolve(a_api.module, "co_gns_client_send", a_api.send) ||
			!Resolve(a_api.module, "co_gns_client_disconnect", a_api.disconnect)) {
			a_error = "client bridge is missing a required co_gns_client_* export";
			dlclose(a_api.module);
			a_api = {};
			return false;
		}
		return true;
	}

	bool ParsePort(std::string_view a_value, std::uint16_t& a_port)
	{
		std::uint32_t parsed = 0;
		const auto result = std::from_chars(a_value.data(), a_value.data() + a_value.size(), parsed);
		if (result.ec != std::errc{} || result.ptr != a_value.data() + a_value.size() || parsed == 0 || parsed > 65535) {
			return false;
		}
		a_port = static_cast<std::uint16_t>(parsed);
		return true;
	}

	std::optional<Metadata> MetadataFor(std::string_view a_target)
	{
		Metadata metadata;
		metadata.clientVersion = F4T::ClientCompatibility::kClientVersion;
		metadata.contentManifestFingerprint = std::string(64, 'a');
		if (a_target == "current") {
			metadata.buildTarget = F4T::ClientCompatibility::BuildTarget::Current;
			metadata.falloutRuntimeVersion = F4T::ClientCompatibility::kCurrentRuntimeVersion;
			return metadata;
		}
		if (a_target == "og") {
			metadata.buildTarget = F4T::ClientCompatibility::BuildTarget::OldGen;
			metadata.falloutRuntimeVersion = F4T::ClientCompatibility::kOldGenRuntimeVersion;
			return metadata;
		}
		if (a_target == "ng") {
			metadata.buildTarget = F4T::ClientCompatibility::BuildTarget::NG;
			metadata.falloutRuntimeVersion = F4T::ClientCompatibility::kNgRuntimeVersion;
			return metadata;
		}
		return std::nullopt;
	}

	bool SendAdapterMessages(
		const BridgeApi& a_api,
		co_gns_client_handle a_client,
		const std::vector<F4T::GnsProtocolAdapter::OutboundMessage>& a_messages)
	{
		for (const auto& message : a_messages) {
			const auto result = a_api.send(
				a_client,
				message.payload.data(),
				static_cast<std::uint32_t>(message.payload.size()),
				static_cast<std::uint32_t>(message.delivery));
			if (result != CO_GNS_SEND_SENT) {
				std::cerr << "client bridge send failed with result " << result << '\n';
				return false;
			}
		}
		return true;
	}

	void Require(bool a_condition, const std::string& a_message)
	{
		if (!a_condition) {
			throw std::runtime_error(a_message);
		}
	}

	ClientIdentity MakeIdentity(
		std::string a_profileId,
		const Metadata& a_metadata,
		std::optional<std::string> a_resumeToken = std::nullopt)
	{
		ClientIdentity identity;
		identity.profileId = std::move(a_profileId);
		identity.metadata = a_metadata;
		identity.resumeToken = std::move(a_resumeToken);
		return identity;
	}

	ReadyIdentity RequireReady(const HandshakeOutcome& a_outcome, std::string_view a_step)
	{
		Require(
			a_outcome.ready.has_value(),
			std::string{ a_step } + ": expected sessionReady but got rejection " +
				(a_outcome.rejection ? a_outcome.rejection->code : std::string{ "none" }));
		return *a_outcome.ready;
	}

	void RequireRejection(const HandshakeOutcome& a_outcome, std::string_view a_step, std::string_view a_code)
	{
		Require(
			a_outcome.rejection.has_value(),
			std::string{ a_step } + ": expected rejection " + std::string{ a_code } + " but the session reached sessionReady");
		Require(
			a_outcome.rejection->code == a_code,
			std::string{ a_step } + ": expected rejection " + std::string{ a_code } + " but got " +
				a_outcome.rejection->code + ": " + a_outcome.rejection->reason);
	}

	double UnixSecondsNow()
	{
		return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

	class BridgeClient
	{
	public:
		explicit BridgeClient(const Endpoint& a_endpoint) :
			_api(a_endpoint.api)
		{
			std::array<char, 512> bridgeError{};
			if (_api.create(a_endpoint.host.c_str(), a_endpoint.port, &_handle, bridgeError.data(), bridgeError.size()) == 0 || _handle == nullptr) {
				throw std::runtime_error(std::string{ "GNS client create failed: " } + bridgeError.data());
			}
		}

		~BridgeClient()
		{
			if (_handle != nullptr) {
				if (_api.disconnect(_handle, 0, "acceptance complete") != 0) {
					// The last destroy kills the shared GNS runtime, so the close must reach the socket first.
					std::this_thread::sleep_for(std::chrono::milliseconds(75));
				}
				_api.destroy(_handle);
			}
		}

		BridgeClient(const BridgeClient&) = delete;
		BridgeClient& operator=(const BridgeClient&) = delete;

		HandshakeOutcome Handshake(ClientIdentity a_identity)
		{
			Session session;
			std::optional<ReadyIdentity> committed;
			session.SetClientIdentity(
				std::move(a_identity),
				[&committed](const ReadyIdentity& a_ready, std::string&) {
					committed = a_ready;
					return true;
				});

			HandshakeOutcome outcome;
			std::array<char, 64 * 1024> payload{};
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (std::chrono::steady_clock::now() < deadline && !session.IsReady()) {
				co_gns_event event{};
				const auto result = _api.poll(_handle, &event, payload.data(), static_cast<std::uint32_t>(payload.size()));
				Require(result >= 0, "client bridge poll failed");
				if (result == 0) {
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
					continue;
				}
				if (event.type == CO_GNS_EVENT_DISCONNECTED || event.type == CO_GNS_EVENT_OVERSIZE_MESSAGE) {
					throw std::runtime_error(std::string{ "client bridge disconnected before sessionReady: " } + event.debug);
				}
				if (event.type != CO_GNS_EVENT_MESSAGE) {
					continue;
				}
				Require(event.payload_size != 0 && event.payload_size <= payload.size(), "client bridge returned invalid message size");

				const auto inbound = session.AdaptInbound(std::string_view{ payload.data(), event.payload_size });
				Require(!inbound.protocolViolation && !inbound.localHandshakeFailure, "client adapter handshake failed: " + inbound.error);
				if (inbound.rejection) {
					outcome.rejection = inbound.rejection;
					return outcome;
				}
				Require(SendAdapterMessages(_api, _handle, inbound.controlMessages), "client bridge send failed during handshake");
			}

			Require(session.IsReady(), "client adapter did not reach sessionReady");
			Require(
				committed && !committed->resumeToken.empty() && committed->resumeExpiresAt > 0.0,
				"sessionReady did not commit a valid resume credential");
			const auto keepAlive = session.AdaptOutbound("{\"type\":\"keepAlive\"}");
			Require(
				keepAlive.size() == 1 && SendAdapterMessages(_api, _handle, keepAlive),
				"post-ready adapter output did not traverse the client bridge");
			Drain(std::chrono::milliseconds(300));
			outcome.ready = committed;
			return outcome;
		}

	private:
		void Drain(std::chrono::milliseconds a_duration)
		{
			std::array<char, 64 * 1024> payload{};
			const auto until = std::chrono::steady_clock::now() + a_duration;
			while (std::chrono::steady_clock::now() < until) {
				co_gns_event event{};
				const auto result = _api.poll(_handle, &event, payload.data(), static_cast<std::uint32_t>(payload.size()));
				Require(result >= 0, "client bridge poll failed");
				if (event.type == CO_GNS_EVENT_DISCONNECTED || event.type == CO_GNS_EVENT_OVERSIZE_MESSAGE) {
					throw std::runtime_error(std::string{ "client disconnected after sessionReady: " } + event.debug);
				}
				if (result == 0) {
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}
		}

		const BridgeApi& _api;
		co_gns_client_handle _handle{};
	};

	void RunRotationLane(const Endpoint& a_endpoint, const std::string& profile, const Metadata& a_metadata)
	{
		ReadyIdentity first;
		{
			BridgeClient client{ a_endpoint };
			first = RequireReady(client.Handshake(MakeIdentity(profile, a_metadata)), "fresh session");
		}
		ReadyIdentity second;
		{
			BridgeClient client{ a_endpoint };
			second = RequireReady(client.Handshake(MakeIdentity(profile, a_metadata, first.resumeToken)), "reconnect with first token");
			Require(second.profileId == profile, "reconnect returned a different profile");
			Require(second.resumeToken != first.resumeToken, "reconnect did not rotate the resume token");
		}
		{
			BridgeClient client{ a_endpoint };
			const auto third = RequireReady(client.Handshake(MakeIdentity(profile, a_metadata, second.resumeToken)), "normal reconnect with rotated token");
			Require(third.resumeToken != second.resumeToken, "normal reconnect did not rotate the resume token");
		}
		{
			BridgeClient client{ a_endpoint };
			RequireRejection(client.Handshake(MakeIdentity(profile, a_metadata, first.resumeToken)), "retired first token", "invalid_resume");
		}
	}

	void RunInvalidLane(const Endpoint& a_endpoint, const std::string& a_knownProfile, const Metadata& a_metadata)
	{
		{
			BridgeClient client{ a_endpoint };
			RequireRejection(
				client.Handshake(MakeIdentity(a_knownProfile + "-unknown", a_metadata, std::string{ "forged-resume-token" })),
				"forged token for unknown profile",
				"invalid_resume");
		}
		BridgeClient client{ a_endpoint };
		RequireRejection(
			client.Handshake(MakeIdentity(a_knownProfile, a_metadata, std::string{ "forged-resume-token" })),
			"forged token for known profile",
			"invalid_resume");
	}

	void RunDuplicateLane(const Endpoint& a_endpoint, std::string_view a_target, const Metadata& a_metadata)
	{
		const auto profile = "adapter-acceptance-duplicate-" + std::string{ a_target };
		BridgeClient holder{ a_endpoint };
		RequireReady(holder.Handshake(MakeIdentity(profile, a_metadata)), "duplicate holder session");
		BridgeClient intruder{ a_endpoint };
		RequireRejection(
			intruder.Handshake(MakeIdentity(profile, a_metadata)),
			"second session for a connected profile without its token",
			"duplicate_identity");
	}

	void RunRequirementsLane(const Endpoint& a_endpoint, const Metadata& a_metadata)
	{
		Metadata base = a_metadata;
		base.capabilities = { std::string{ kRequiredCapability } };
		{
			BridgeClient client{ a_endpoint };
			RequireReady(client.Handshake(MakeIdentity("adapter-acceptance-requirements-matching", base)), "matching requirements");
		}

		const auto expectRejection = [&a_endpoint](std::string_view a_label, const Metadata& a_identityMetadata, std::string_view a_code) {
			BridgeClient client{ a_endpoint };
			RequireRejection(
				client.Handshake(MakeIdentity("adapter-acceptance-requirements-" + std::string{ a_label }, a_identityMetadata)),
				a_label,
				a_code);
		};

		Metadata clientVersion = base;
		clientVersion.clientVersion = "0.0.0-acceptance";
		expectRejection("client_version", clientVersion, "client_version_mismatch");

		Metadata runtime = base;
		runtime.falloutRuntimeVersion = std::string{ F4T::ClientCompatibility::kOgRuntimeVersion };
		expectRejection("runtime", runtime, "runtime_mismatch");

		Metadata content = base;
		content.contentManifestFingerprint = std::string(64, 'b');
		expectRejection("content", content, "content_mismatch");

		Metadata capability = base;
		capability.capabilities.clear();
		expectRejection("capability", capability, "capability_mismatch");
	}

	void RunExpiredLane(const Endpoint& a_endpoint, const Metadata& a_metadata)
	{
		const auto profile = std::string{ "adapter-acceptance-expired" };
		ReadyIdentity issued;
		{
			BridgeClient client{ a_endpoint };
			issued = RequireReady(client.Handshake(MakeIdentity(profile, a_metadata)), "expiry fresh session");
		}
		Require(issued.resumeExpiresAt - UnixSecondsNow() < 5, "resume lifetime is longer than the expiry lane allows");
		while (UnixSecondsNow() <= issued.resumeExpiresAt + 0.1) {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		BridgeClient client{ a_endpoint };
		RequireRejection(
			client.Handshake(MakeIdentity(profile, a_metadata, issued.resumeToken)),
			"reconnect after resume lifetime",
			"resume_expired");
	}

	void RunAdmissionLane(const Endpoint& a_endpoint, const Metadata& a_metadata)
	{
		BridgeClient holder{ a_endpoint };
		RequireReady(holder.Handshake(MakeIdentity("adapter-acceptance-admission-holder", a_metadata)), "admission holder session");
		BridgeClient late{ a_endpoint };
		RequireRejection(
			late.Handshake(MakeIdentity("adapter-acceptance-admission-late", a_metadata)),
			"new profile while the server is at max_players",
			"server_full");
	}

	template <class Body>
	void RunLane(std::string_view a_name, Body&& a_body)
	{
		try {
			a_body();
		} catch (const std::exception& exception) {
			throw std::runtime_error(std::string{ a_name } + ": " + exception.what());
		}
		std::cout << "lane " << a_name << " passed\n";
	}

	void RunDefaultScenario(const Endpoint& a_endpoint, std::string_view a_target, const Metadata& a_metadata)
	{
		const auto rotationProfile = "adapter-acceptance-rotation-" + std::string{ a_target };
		RunLane("1 resume-token rotation and normal reconnect", [&] { RunRotationLane(a_endpoint, rotationProfile, a_metadata); });
		RunLane("2 invalid and duplicate identity", [&] {
			RunInvalidLane(a_endpoint, rotationProfile, a_metadata);
			RunDuplicateLane(a_endpoint, a_target, a_metadata);
		});
	}

	void RunRequirementsScenario(const Endpoint& a_endpoint, const Metadata& a_metadata)
	{
		RunLane("3 client, runtime, content and capability mismatch", [&] { RunRequirementsLane(a_endpoint, a_metadata); });
	}

	void RunCapacityScenario(const Endpoint& a_endpoint, const Metadata& a_metadata)
	{
		RunLane("2 expired resume credential", [&] { RunExpiredLane(a_endpoint, a_metadata); });
		RunLane("4 server_full admission rejection", [&] { RunAdmissionLane(a_endpoint, a_metadata); });
	}
}

int main(int argc, char** argv)
{
	if (argc != 5 && argc != 6) {
		std::cerr << "Usage: CommonwealthOnlineClientProtocolAcceptance BRIDGE HOST PORT current|ng|og [default|requirements|capacity]\n";
		return 2;
	}

	std::uint16_t port = 0;
	if (!ParsePort(argv[3], port)) {
		std::cerr << "Invalid port\n";
		return 2;
	}
	const auto metadata = MetadataFor(argv[4]);
	if (!metadata) {
		std::cerr << "Target must be current, ng, or og\n";
		return 2;
	}
	const std::string_view scenario = argc == 6 ? std::string_view{ argv[5] } : std::string_view{ "default" };
	if (scenario != "default" && scenario != "requirements" && scenario != "capacity") {
		std::cerr << "Scenario must be default, requirements, or capacity\n";
		return 2;
	}

	BridgeApi api;
	std::string error;
	if (!LoadBridge(argv[1], api, error)) {
		std::cerr << "Failed to load CO-BETA client bridge: " << error << '\n';
		return 1;
	}

	int exitCode = 1;
	try {
		const Endpoint endpoint{ api, argv[2], port };
		if (scenario == "default") {
			RunDefaultScenario(endpoint, argv[4], *metadata);
		} else if (scenario == "requirements") {
			RunRequirementsScenario(endpoint, *metadata);
		} else {
			RunCapacityScenario(endpoint, *metadata);
		}
		std::cout << "CO-BETA adapter live acceptance passed for " << argv[4]
		          << " scenario " << scenario << " runtime " << metadata->falloutRuntimeVersion << '\n';
		exitCode = 0;
	} catch (const std::exception& exception) {
		std::cerr << "CO-BETA adapter live acceptance failed for " << argv[4] << " scenario " << scenario
		          << ": " << exception.what() << '\n';
	}

	dlclose(api.module);
	return exitCode;
}
