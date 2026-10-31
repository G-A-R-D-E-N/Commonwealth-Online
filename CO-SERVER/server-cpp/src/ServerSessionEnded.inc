	void SendSessionEnded(
		co_gns_server_handle a_server,
		std::uint32_t a_connectionId,
		std::string_view a_code,
		std::string_view a_reason,
		double a_unixTimeSeconds,
		const nlohmann::json& a_requiredModpack,
		std::string_view a_modpackFingerprint,
		const nlohmann::json& a_modpackChunks,
		std::string_view a_modpackName)
	{
		nlohmann::json packet{
			{ "type", "sessionEnded" },
			{ "code", a_code },
			{ "reason", a_reason },
			{ "serverTime", a_unixTimeSeconds }
		};
		if (!a_requiredModpack.is_null()) {
			packet["requiredModpack"] = a_requiredModpack;
		}
		if (!a_modpackFingerprint.empty()) {
			packet["requiredModpackFingerprint"] = a_modpackFingerprint;
		}
		if (!a_modpackChunks.is_null()) {
			packet["requiredModpackChunks"] = a_modpackChunks;
		}
		if (!a_modpackName.empty()) {
			packet["requiredModpackName"] = a_modpackName;
		}
		(void)SendReliableJson(a_server, a_connectionId, packet);
	}
