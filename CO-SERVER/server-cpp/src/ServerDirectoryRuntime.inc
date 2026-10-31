	LanDiscoveryService discovery{
		options.serverName,
		options.serverDescription,
		localPort,
		options.maximumPlayers,
		activePlayers,
		options.serverPassword.has_value(),
		options.lanDiscoveryPort
	};
	if (options.enableLanDiscovery) {
		std::string discoveryError;
		if (!discovery.Start(discoveryError)) {
			std::cerr << "Failed to start LAN discovery: " << discoveryError << '\n';
			admin.Stop();
			co_gns_server_destroy(server);
			return 1;
		}
	}

	const bool listensOnIpv6 = F4T::ConnectionPolicy::Ipv6Prefix64(options.bindHost).has_value();
	std::unique_ptr<CommonwealthOnline::Server::DirectoryRegistrar> directory;
	if (options.publicListing) {
		directory = std::make_unique<CommonwealthOnline::Server::DirectoryRegistrar>(
			options.directoryUrl,
			CommonwealthOnline::Server::DirectoryListing{
				.name = options.serverName,
				.description = options.serverDescription,
				.port = localPort,
				.maximumPlayers = options.maximumPlayers,
				.clientVersion = options.requiredClientVersion,
				.falloutRuntime = options.requiredFalloutRuntimeVersion,
				.contentManifestFingerprint = options.requiredContentManifestFingerprint,
				.capabilities = options.requiredCapabilities,
				.passworded = options.serverPassword.has_value(),
				.modpackFingerprint = options.requiredModpackFingerprint,
				.listensOnIpv6 = listensOnIpv6 },
			activePlayers,
			CommonwealthOnline::Server::CreatePlatformDirectorySender());
		directory->Start();
	}

	std::cout << "CommonwealthOnline.Server C++ listening on " << options.bindHost << ':' << localPort << " (GNS)" << std::endl;
	if (options.enableLanDiscovery) {
		std::cout << "LAN discovery listening on UDP port " << discovery.LocalPort() << std::endl;
	} else {
		std::cout << "LAN discovery disabled" << std::endl;
	}
	if (directory) {
		std::cout << CommonwealthOnline::Server::PublicListingStartupNotice(localPort, options.directoryUrl, listensOnIpv6) << std::endl;
	} else {
		std::cout << "Public listing disabled" << std::endl;
	}
