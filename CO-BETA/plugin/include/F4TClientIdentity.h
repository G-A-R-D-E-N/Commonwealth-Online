#pragma once

#include "F4TClientCompatibility.h"
#include "F4TGnsProtocolAdapter.h"
#include "F4TSaveProfileLogic.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

// Client-side identity resolution for the Protocol V2 hello (#48). The stable
// profileId and the resume credential come from the crash-safe per-server
// profile store; the content manifest fingerprint comes from the per-release
// packaged manifest file (CommonwealthOnlineClient.json, staged next to the
// DLLs) so the value attached on the wire can never drift from what was
// released - it is not a hardcoded constant anywhere in the client.
namespace F4T::ClientIdentity
{
	struct ConnectionIdentity
	{
		// Store key of the server being joined (host:port, normalized).
		std::string serverKey;
		// Durable server/profile identity, generated once and persisted before
		// the first hello so a retry reconnects under the same profile.
		std::string profileId;
		// Server-issued credential from the previous accepted session, if the
		// store holds one.
		std::optional<std::string> resumeToken;
		// The canonical per-target metadata completed with the packaged content
		// fingerprint. A connection is not prepared when the packaged manifest is
		// missing or invalid because the server requires Protocol V2 identity.
		std::optional<F4T::ClientCompatibility::Metadata> metadata;
		// Held in memory only for the next connect; never written to the profile store.
		std::optional<std::string> serverPassword;
		std::optional<F4T::SaveProfileLogic::AttestedSave> saveAttestation;
		std::optional<std::string> profileSecret;
		std::optional<std::string> modpackFingerprint;
		std::optional<std::string> steamId;
		std::optional<std::string> steamTicket;
	};

	// Reads CommonwealthOnlineClient.json from the DLL's own directory and
	// completes the compile-time target's canonical metadata with the packaged
	// content manifest fingerprint. Returns std::nullopt on any missing or
	// malformed component (fail closed - never emit a fingerprint the release
	// pipeline did not produce).
	[[nodiscard]] std::optional<F4T::ClientCompatibility::Metadata> LoadPackagedMetadata();

	[[nodiscard]] std::optional<ConnectionIdentity> PrepareConnectionIdentity(
		const std::string& a_host,
		std::uint16_t a_port,
		std::string& a_error,
		const std::optional<std::string>& a_serverPassword = std::nullopt,
		const std::string& a_joinId = {});

	// The join flow holds the prepared identity for the next GNS connect to
	// consume (one client connection at a time). The slot is overwritten on the
	// next prepare and cleared when consumed.
	void HoldConnectionIdentity(ConnectionIdentity a_identity);
	void ClearConnectionIdentity();
	[[nodiscard]] std::optional<ConnectionIdentity> TakeConnectionIdentity();

	[[nodiscard]] bool CommitReadyIdentity(
		const std::string& a_serverKey,
		const F4T::GnsProtocolAdapter::ReadyIdentity& a_identity,
		std::string& a_error);

	void SetConnectedServerAdmin(bool a_isAdmin);
	[[nodiscard]] bool IsConnectedServerAdmin();

	[[nodiscard]] std::string ConnectedProfileId();
	void ClearConnectedProfileId();

#ifdef F4T_CLIENT_IDENTITY_TESTING
	void SetManifestPathForTesting(const std::filesystem::path& a_path);
	void ResetManifestPathForTesting();
#endif
}
