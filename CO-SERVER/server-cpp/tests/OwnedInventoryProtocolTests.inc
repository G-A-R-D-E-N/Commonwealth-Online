	void TestOwnedInventoryProtocol()
	{
		using namespace F4T::Drops;
		using namespace F4T::Replication;
		const auto directory = std::filesystem::temp_directory_path() / ("co-owned-protocol-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		const auto path = directory / "state.json";
		const auto backup = std::filesystem::path{ path.string() + ".bak" };
		try {
			DurableStateFileStore store(path, [] { return 1000.0; }, [] { return "0123456789abcdef0123456789abcdef"; });
			inspectedStore = &store;
			OpenSslTokenService tokens;
			IdentityGate identity({}, tokens, &store);
			PersistenceGate persistence(store, Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5u } } }));
			ProtocolHost host;
			assert(host.EnableInventoryTransactions(persistence));
			assert(!host.EnableInventoryTransactions(persistence));
			ComponentRegistry otherDefinitions;
			assert(otherDefinitions.RegisterDefinition(Family::Transform, AuthorityClass::Server, Channel::Unreliable));
			assert(!host.SetComponentDefinitions(*otherDefinitions.Freeze()));
			assert(!host.SetDirectComponentPublicationContract({}));
			const auto move = [&](std::uint32_t a_connection, double a_time, const char* a_cell = "CAFE", const char* a_world = "F00D", double a_x = 1.0, const char* a_type = "teleport") {
				return host.HandleMessage(a_connection, JsonBytes(Json{ { "type", "transform" }, { "cellId", a_cell }, { "worldspaceId", a_world },
					{ "x", a_x }, { "y", 2.0 }, { "z", 3.0 }, { "angleZ", 0.0 }, { "movementType", a_type } }), a_time, 1000.0);
			};
			const auto join = [&](std::uint32_t a_connection, bool a_stable, bool a_components = true, bool a_channels = true) {
				const auto welcome = host.HandleConnected(a_connection, 1000.0);
				assert(Json::parse(welcome.front().payload).at("capabilities").get<std::vector<std::string>>().back() == "inventory-stable-v1");
				auto caps = Json::array();
				caps.push_back("save-attestation-v1");
				if (a_components) caps.push_back("component-snapshot-v1");
				if (a_channels) caps.push_back("component-snapshot-channels-v1");
				if (a_stable) caps.push_back("inventory-stable-v1");
				const auto hello = JsonBytes(Json{ { "type", "hello" }, { "protocolVersion", 2u }, { "clientVersion", "1.2.0" },
					{ "falloutRuntimeVersion", "1.11.240" }, { "profileId", "profile-" + std::to_string(a_connection) },
					{ "contentManifestFingerprint", std::string(64, 'a') }, { "capabilities", caps } });
				assert(identity.InspectInbound(a_connection, hello, 1000.0).forward);
				return host.HandleMessage(a_connection, hello, 1.0, 1000.0);
			};
			const Json drop{ { "type", "inventoryDropRequest" }, { "operationId", "first" }, { "expectedRevision", 0u }, { "itemKey", "test.esm:00000123" }, { "count", 2u } };
			const auto request = [&](std::uint32_t a_connection, const Json& a_packet, double a_time) { return host.HandleMessage(a_connection, JsonBytes(a_packet), a_time, 1000.0); };
			const auto result = [](const auto& a_frames) {
				assert(!a_frames.empty());
				return Json::parse(a_frames.front().payload);
			};
			auto ready = join(11, true);
			(void)move(11, 2.0, "CAFE", "F00D", 1, "spawn");
			assert(result(request(11, drop, 2.1)).at("reason") == "inventoryAuthorityUnavailable");
			sendResult = CO_GNS_SEND_BACKPRESSURE;
			SendFrames(nullptr, host, ready, &identity, &persistence, 2.2, 1000, nullptr);
			assert(result(request(11, drop, 2.3)).at("reason") == "inventoryAuthorityUnavailable");
			assert(store.GetInventory("profile-11")->at("revision") == 0);
			sendResult = CO_GNS_SEND_SENT;
			SendFrames(nullptr, host, ready, &identity, &persistence, 2.4, 1000, nullptr);
			for (const auto connection : { 12u, 13u, 14u }) {
				ready = join(connection, connection != 13);
				SendFrames(nullptr, host, ready, &identity, &persistence, 2.5, 1000, nullptr);
				(void)move(connection, 2.6, connection == 14 ? "BEEF" : "CAFE", connection == 14 ? "DEAD" : "F00D", 1, "spawn");
			}
			assert(result(request(13, drop, 2.7)).at("reason") == "inventoryAuthorityUnavailable");
			for (const auto connection : { 16u, 17u }) {
				ready = join(connection, true, connection != 17, connection != 16);
				SendFrames(nullptr, host, ready, &identity, &persistence, 2.7, 1000, nullptr);
				(void)move(connection, 2.7, "CAFE", "F00D", 1, "spawn");
				assert(result(request(connection, drop, 2.7)).at("reason") == "inventoryAuthorityUnavailable");
				assert(store.GetInventory("profile-" + std::to_string(connection))->at("revision") == 0);
			}
			std::filesystem::remove(backup);
			std::filesystem::create_directories(backup / "occupied");
			assert(result(request(11, drop, 2.8)).at("reason") == "inventoryPersistenceUnavailable");
			assert(store.GetInventory("profile-11")->at("revision") == 0 && store.GetDropSnapshot()->empty());
			std::filesystem::remove_all(backup);
			const auto projectionsBeforeDrop = host.Stats().inventoryProjections;
			auto frames = request(11, drop, 3.0);
			const auto committed = result(frames);
			assert(committed.at("status") == "committed" && committed.at("revision") == 1);
			assert(host.Stats().inventoryProjections == projectionsBeforeDrop + 1);
			assert(store.GetInventory("profile-11")->at("items").at("test.esm:00000123").at("count") == 3);
			assert(ReliableTo(frames, 12).size() == 1 && ReliableTo(frames, 13).empty() && ReliableTo(frames, 14).empty());
			SnapshotReceiver receiver;
			sendResult = CO_GNS_SEND_BACKPRESSURE;
			SendFrames(nullptr, host, frames, nullptr, &persistence, 3.1, 1000, nullptr);
			frames = host.RetryComponentSnapshots();
			assert(frames.size() == 2);
			sendResult = CO_GNS_SEND_SENT;
			SendFrames(nullptr, host, frames, nullptr, &persistence, 3.2, 1000, nullptr);
			AcceptComponent(receiver, ReliableTo(frames, 12).front());
			assert(receiver.CurrentView() == *store.GetDropSnapshot());
			frames = move(12, 4.0, "BEEF", "DEAD");
			assert(ReliableTo(frames, 12).size() == 1);
			sendResult = CO_GNS_SEND_BACKPRESSURE;
			SendFrames(nullptr, host, frames, nullptr, &persistence, 4.1, 1000, nullptr);
			frames = host.RetryComponentSnapshots();
			assert(ReadComponent(ReliableTo(frames, 12).front()).baseline.empty());
			SendFrames(nullptr, host, frames, nullptr, &persistence, 4.2, 1000, nullptr);
			frames = move(12, 5.0);
			const auto visibleAgain = ReliableTo(frames, 12);
			assert(visibleAgain.size() == 1 && ReadComponent(visibleAgain.front()).baseline == *store.GetDropSnapshot());
			sendResult = CO_GNS_SEND_SENT;
			SendFrames(nullptr, host, frames, nullptr, &persistence, 5.1, 1000, nullptr);
			AcceptComponent(receiver, visibleAgain.front());
			assert(receiver.CurrentView() == *store.GetDropSnapshot());
			assert(result(request(11, drop, 20.0)) == committed);
			auto second = drop;
			second["operationId"] = "second";
			second["expectedRevision"] = 1u;
			assert(result(request(11, second, 20.1)).at("reason") == "outOfScope");
			const auto rejectedBefore = host.Stats().movementRejected;
			(void)move(11, 20.2, "CAFE", "F00D", 9999999.0, "normal");
			assert(host.Stats().movementRejected == rejectedBefore + 1);
			assert(result(request(11, second, 20.3)).at("reason") == "outOfScope");
			(void)move(11, 21.0);
			auto forged = second;
			forged["profileId"] = "profile-12";
			assert(result(request(11, forged, 21.1)).at("reason") == "invalidRequest");
			(void)host.HandleDisconnected(11, 1001);
			persistence.ConnectionClosed(11);
			assert(store.GetDropSnapshot()->size() == 1);
			ready = join(15, true);
			SendFrames(nullptr, host, ready, &identity, &persistence, 22.0, 1000, nullptr);
			frames = move(15, 22.1, "CAFE", "F00D", 1, "spawn");
			const auto late = ReliableTo(frames, 15);
			assert(late.size() == 1 && ReadComponent(late.front()).baseline == *store.GetDropSnapshot());
			(void)move(12, 23.0);
			const Json pickup{ { "type", "inventoryPickupRequest" }, { "operationId", "pickup" }, { "expectedRevision", 0u }, { "entityId", committed.at("entityId") } };
			frames = request(12, pickup, 23.1);
			assert(result(frames).at("status") == "committed");
			assert(store.GetDropSnapshot()->empty() && store.GetInventory("profile-12")->at("items").at("test.esm:00000123").at("count") == 7);
			sendResult = CO_GNS_SEND_BACKPRESSURE;
			SendFrames(nullptr, host, frames, nullptr, &persistence, 23.2, 1000, nullptr);
			frames = host.RetryComponentSnapshots();
			const auto removal = ReliableTo(frames, 12);
			assert(removal.size() == 1 && ReadComponent(removal.front()).baseline.empty());
			sendResult = CO_GNS_SEND_SENT;
			SendFrames(nullptr, host, frames, nullptr, &persistence, 23.3, 1000, nullptr);
			AcceptComponent(receiver, removal.front());
			assert(receiver.CurrentView().empty());
			const auto projectionsBeforeIdle = host.Stats().inventoryProjections;
			(void)move(15, 24.0, "CAFE", "F00D", 1, "spawn");
			assert(host.Stats().inventoryProjections == projectionsBeforeIdle);
			const Json stale{ { "type", "inventoryDropRequest" }, { "operationId", "flood" }, { "expectedRevision", 99u }, { "itemKey", "test.esm:00000123" }, { "count", 1u } };
			const auto projectionsBeforeFlood = host.Stats().inventoryProjections;
			for (int burst = 0; burst < 4; ++burst) assert(result(request(12, stale, 30.0)).at("reason") == "staleRevision");
			const auto limited = result(request(12, stale, 30.0));
			assert(limited.at("status") == "rejected" && limited.at("reason") == "rateLimited" && limited.at("operationId") == "flood");
			assert(result(request(12, stale, 31.0)).at("reason") == "staleRevision");
			assert(host.Stats().inventoryProjections == projectionsBeforeFlood);
			// I3: container and trade requests are key-validated, share the owned
			// token bucket and stay reject-closed until I11/I12.
			const Json container{ { "type", "inventoryContainerRequest" }, { "operationId", "container" }, { "expectedRevision", 0u },
				{ "direction", "take" }, { "container", Json{ { "cellId", "0000CAFE" }, { "refId", "00001234" } } },
				{ "itemKey", "test.esm:00000123" }, { "count", 1u } };
			const Json trade{ { "type", "inventoryTradeRequest" }, { "operationId", "trade" }, { "expectedRevision", 0u },
				{ "vendor", Json{ { "cellId", "0000CAFE" }, { "refId", "00001234" } } },
				{ "sell", Json::array({ Json{ { "itemKey", "test.esm:00000123" }, { "count", 1u } } }) }, { "buy", Json::array() } };
			ready = join(18, true);
			SendFrames(nullptr, host, ready, &identity, &persistence, 40.0, 1000, nullptr);
			(void)move(18, 40.1, "CAFE", "F00D", 1, "spawn");
			const auto containerRejected = result(request(18, container, 40.2));
			assert(containerRejected.at("status") == "rejected" && containerRejected.at("reason") == "inventoryAuthorityUnavailable" && containerRejected.at("operationId") == "container");
			const auto tradeRejected = result(request(18, trade, 40.3));
			assert(tradeRejected.at("status") == "rejected" && tradeRejected.at("reason") == "inventoryAuthorityUnavailable" && tradeRejected.at("operationId") == "trade");
			auto padded = container;
			padded["unexpected"] = 1u;
			const auto paddedBefore = host.Stats().packetsRejected;
			assert(request(18, padded, 40.4).empty());
			assert(host.Stats().packetsRejected == paddedBefore + 1);
			auto runtimeRef = container;
			runtimeRef["container"] = Json{ { "cellId", "0000CAFE" }, { "refId", "FF001234" } };
			const auto malformedBefore = host.Stats().packetsRejected;
			assert(request(18, runtimeRef, 40.5).empty());
			assert(host.Stats().packetsRejected == malformedBefore + 1);
			for (int burst = 0; burst < 4; ++burst) {
				auto burstRequest = container;
				burstRequest["operationId"] = "container-" + std::to_string(burst);
				assert(result(request(18, burstRequest, 41.0)).at("reason") == "inventoryAuthorityUnavailable");
			}
			auto dropAfterContainer = drop;
			dropAfterContainer["operationId"] = "drop-after-container";
			assert(result(request(18, dropAfterContainer, 41.0)).at("reason") == "rateLimited");
			ready = join(19, true);
			SendFrames(nullptr, host, ready, &identity, &persistence, 42.0, 1000, nullptr);
			(void)move(19, 42.0, "CAFE", "F00D", 1, "spawn");
			for (int burst = 0; burst < 4; ++burst) {
				auto flood = stale;
				flood["operationId"] = "shared-" + std::to_string(burst);
				assert(result(request(19, flood, 42.0)).at("reason") == "staleRevision");
			}
			auto containerAfterDrop = container;
			containerAfterDrop["operationId"] = "container-after-drop";
			assert(result(request(19, containerAfterDrop, 42.0)).at("reason") == "rateLimited");
		} catch (...) {
			std::filesystem::remove_all(directory);
			throw;
		}
		std::filesystem::remove_all(directory);
	}
