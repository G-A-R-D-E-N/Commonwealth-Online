#include "pch.h"

#include "F4TDebugLogging.h"
#include "F4TNetworkingInternal.h"
#include "F4TSaveLineageSession.h"

#include <format>

namespace F4T::Networking::Detail
{
	void HandleSaveAttestationResultPacket(const Json& a_packet, std::string_view a_type)
	{
		const auto saveFile = ReadString(a_packet, "saveFile");
		const auto sha256 = ReadString(a_packet, "sha256");
		const auto safeSave = F4T::DebugLogging::SanitizeLogText(saveFile.value_or("<missing>"), 128);
		if (a_type == "saveAttestedRejected") {
			const auto code = ReadString(a_packet, "code");
			const auto reason = ReadString(a_packet, "reason");
			LogWarningWithLocalPlayerPrefix(std::format(
				"Server refused the save attestation for '{}' ({}): {}",
				safeSave,
				F4T::DebugLogging::SanitizeLogText(code.value_or("<missing>"), 64),
				F4T::DebugLogging::SanitizeLogText(reason.value_or("<none>"), 160)));
			F4T::SaveLineageSession::NoteFirstJoinAttestationResult(saveFile.value_or(""), sha256.value_or(""), false);
			return;
		}

		const auto recorded = a_packet.find("recorded");
		const bool isRecorded = recorded != a_packet.end() && recorded->is_boolean() && recorded->get<bool>();
		F4T::SaveLineageSession::NoteFirstJoinAttestationResult(saveFile.value_or(""), sha256.value_or(""), true);
		F4T::DebugLogging::Log(
			"SaveLineage",
			"server save attestation ack save='{}' recorded={}",
			safeSave,
			isRecorded ? "true" : "false");
	}
}
