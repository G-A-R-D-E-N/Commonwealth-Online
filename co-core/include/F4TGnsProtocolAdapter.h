#pragma once

#include "F4TClientCompatibility.h"
#include "F4TModpackManifest.h"
#include "F4TSaveProfileLogic.h"
#include "F4TWorldPolicy.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::GnsProtocolAdapter
{
	using Json = nlohmann::json;

	enum class Delivery : std::uint32_t
	{
		UnreliableSequenced = 0,
		ReliableOrdered = 1
	};

	struct OutboundMessage
	{
		std::string payload;
		Delivery delivery{ Delivery::ReliableOrdered };
	};

	struct ReadyIdentity
	{
		std::string profileId;
		std::string resumeToken;
		double resumeExpiresAt{};
		bool multiplayerSaveAdmin{ false };
		std::optional<std::string> profileSecret;
	};

	struct ModpackChunksInfo
	{
		std::uint64_t totalBytes{};
		std::uint32_t chunkCount{};
		std::string sha256;
	};

	struct SessionRejection
	{
		std::string code;
		std::string reason;
		std::optional<ModpackManifest::Manifest> requiredModpack;
		std::string requiredModpackFingerprint;
		std::string requiredModpackName;
		std::optional<ModpackChunksInfo> requiredModpackChunks;
	};

	using ReadyIdentityCommitter = std::function<bool(const ReadyIdentity&, std::string&)>;
	using FormIdTranslator = std::function<std::uint32_t(std::uint32_t, std::uint32_t)>;

	struct InboundResult
	{
		std::vector<std::string> legacyMessages;
		std::vector<OutboundMessage> controlMessages;
		bool protocolViolation{ false };
		bool localHandshakeFailure{ false };
		std::string error;
		std::optional<ReadyIdentity> readyIdentity;
		std::optional<SessionRejection> rejection;
	};

	struct ClientIdentity
	{
		std::string profileId;
		ClientCompatibility::Metadata metadata;
		std::optional<std::string> resumeToken;
		std::optional<std::string> serverPassword;
		std::optional<std::string> modpackFingerprint;
		std::optional<F4T::SaveProfileLogic::AttestedSave> saveAttestation;
		std::optional<std::string> profileSecret;
		std::optional<std::string> steamId;
		std::optional<std::string> steamTicket;
	};

	struct NpcAuthorityAssignment
	{
		std::uint32_t playerId{};
		std::uint64_t epoch{};
	};

	class Session
	{
	public:
		Session();
		~Session();
		Session(Session&&) noexcept;
		Session& operator=(Session&&) noexcept;
		Session(const Session&) = delete;
		Session& operator=(const Session&) = delete;

		void Reset();
		void SetClientIdentity(ClientIdentity a_identity, ReadyIdentityCommitter a_committer);
		void SetFormIdTranslator(FormIdTranslator a_translator);
		bool IsReady() const noexcept;
		std::vector<OutboundMessage> AdaptOutbound(std::string_view a_legacyPayload);
		InboundResult AdaptInbound(std::string_view a_jsonPayload);
		std::optional<NpcAuthorityAssignment> GetNpcAuthority(
			std::string_view a_cellId,
			std::string_view a_worldspaceId) const;
		std::optional<F4T::WorldPolicy::Policy> GetWorldPolicy() const;

	private:
		class Impl;
		std::unique_ptr<Impl> _impl;
	};
}
