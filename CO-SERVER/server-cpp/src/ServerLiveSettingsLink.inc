auto runningSettingsDocument = [&options] {
	auto values = F4T::ServerSettings::ReadEditable(options);
	values.passwordUpdate = values.serverPassword ? F4T::ServerSettings::PasswordUpdate::Set :
		F4T::ServerSettings::PasswordUpdate::Clear;
	return F4T::ServerSettings::ApplyEditable(nlohmann::json::object(), values);
};

steamIdentityLink.SetApplySettingsHandler([&](const nlohmann::json& a_document) {
	return serverLiveRelay.Submit(a_document, std::chrono::seconds(3));
});
steamIdentityLink.SetServerConfigProvider([&] {
	return nlohmann::json{
		{ "configPath", F4T::Utf8Path::Encode(baseDirectory / "commonwealth-server.json") },
		{ "document", runningSettingsDocument() }
	};
});
