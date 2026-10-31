	void TestStableDropProjection()
	{
		using namespace F4T::Replication;
		StableDroppedObject object{ Json{ { "plugin", std::string(128, 'a') }, { "localFormId", "00ABCDEF" }, { "count", 65535u } }, position };
		const auto bytes = EncodeStableDroppedObject(object);
		assert(bytes && bytes->size() == 156);
		const auto decoded = DecodeStableDroppedObject(*bytes);
		assert(decoded && decoded->item == object.item && decoded->position.cellId == "0000CAFE" && decoded->position.worldspaceId == "0000F00D");
		assert(decoded->position.x == position.x && decoded->position.y == position.y && decoded->position.z == position.z);
		assert(EncodeStableDroppedObject(*decoded) == bytes);
		for (std::size_t size = 0; size < bytes->size(); ++size) assert(!DecodeStableDroppedObject(std::span{ *bytes }.first(size)));
		auto malformed = *bytes;
		malformed.push_back(0);
		assert(!DecodeStableDroppedObject(malformed));
		for (const std::size_t offset : { 0u, 1u, 2u }) {
			malformed = *bytes;
			malformed[offset] = offset == 2 ? '/' : 0;
			assert(!DecodeStableDroppedObject(malformed));
		}
		malformed = *bytes;
		malformed[2] = 'A';
		assert(!DecodeStableDroppedObject(malformed));
		for (const auto [offset, length] : { std::pair{ 130u, 4u }, std::pair{ 134u, 2u }, std::pair{ 136u, 4u } }) {
			malformed = *bytes;
			std::fill_n(malformed.begin() + offset, length, 0);
			assert(!DecodeStableDroppedObject(malformed));
		}
		malformed = *bytes;
		std::fill_n(malformed.end() - 12, 4, 0xFF);
		assert(!DecodeStableDroppedObject(malformed));
		object.position.worldspaceId.clear();
		const auto interior = DecodeStableDroppedObject(*EncodeStableDroppedObject(object));
		assert(interior && interior->position.worldspaceId.empty());
		object.item["plugin"] = std::string(129, 'a');
		assert(!EncodeStableDroppedObject(object));
		object.item["plugin"] = "test.esm";
		object.item["count"] = 65536u;
		assert(!EncodeStableDroppedObject(object));
		object.item["count"] = 1u;
		object.position.x = (std::numeric_limits<float>::infinity)();
		assert(!EncodeStableDroppedObject(object));
		auto root = Funded(65535);
		const auto large = Json{ { "plugin", std::string(128, 'a') }, { "localFormId", "00000123" }, { "count", 65535u } };
		const auto medium = Json{ { "plugin", std::string(13, 'b') }, { "localFormId", "00000123" }, { "count", 5u } };
		const auto small = Json{ { "plugin", "c" }, { "localFormId", "00000123" }, { "count", 5u } };
		root["profiles"]["a"]["inventory"]["items"] = F4T::DurableState::NormalizeStartingInventory(Json::array({ large }));
		root["profiles"]["b"]["inventory"]["items"] = F4T::DurableState::NormalizeStartingInventory(Json::array({ large, medium, small }));
		const auto largeKey = std::string(128, 'a') + ":00000123";
		for (std::uint32_t index = 0; index < 399; ++index) {
			const auto profile = index < 256 ? "a" : "b";
			const auto revision = index < 256 ? index : index - 256;
			auto request = Request("max-" + std::to_string(index), revision);
			request["itemKey"] = largeKey;
			assert(DropLedger::Drop(root, profile, request, position).outcome == Outcome::Committed);
		}
		for (const auto [revision, itemKey] : { std::pair{ 143u, std::string(13, 'b') + ":00000123" }, std::pair{ 144u, std::string{ "c:00000123" } } }) {
			auto request = Request("boundary-" + std::to_string(revision), revision);
			request["itemKey"] = itemKey;
			assert(DropLedger::Drop(root, "b", request, position).outcome == Outcome::Committed);
		}
		const auto projection = BuildStableDropSnapshot(root.at("drops").at("live"));
		assert(projection && projection->size() == 401);
		const auto baseline = EncodeBaseline(*projection, 1, Channel::Reliable);
		assert(baseline && baseline->size() == kMaximumDropBaselineBytes);
		DecodedFrame frame;
		assert(DecodeFrame(*baseline, frame, [](Family a_family) { return a_family == Family::StableDroppedItem; }) == DecodeStatus::Valid);
		SnapshotReceiver lateJoin;
		assert(lateJoin.AcceptBaseline(frame.baseline, frame.snapshotSequence));
		assert(lateJoin.CurrentView() == *projection);
		for (const auto& [component, data] : frame.baseline) assert(component.family == Family::StableDroppedItem && DecodeStableDroppedObject(data));
		const auto before = root;
		auto next = Request("over-byte-limit", 145);
		next["itemKey"] = "c:00000123";
		const auto overCapacity = DropLedger::Drop(root, "b", next, position);
		if (overCapacity.outcome != Outcome::Rejected) {
			std::fprintf(stderr, "Oversized funded drop committed: baseline=%zu limit=%zu rootUnchanged=%d\n",
				EncodeBaseline(*BuildStableDropSnapshot(root.at("drops").at("live")), 1, Channel::Reliable)->size(), kMaximumDropBaselineBytes, root == before);
		}
		assert(overCapacity.outcome == Outcome::Rejected && overCapacity.packet.at("reason") == "capacityExceeded");
		assert(root == before);
		auto replayRequest = Request("max-398", 142);
		replayRequest["itemKey"] = largeKey;
		assert(DropLedger::Drop(root, "b", replayRequest, position).outcome == Outcome::Replayed && root == before);
		root = F4T::DurableState::LoadAndMigrate(Json::parse(root.dump()), 1001.0).root;
		assert(BuildStableDropSnapshot(root.at("drops").at("live")) == projection);
		assert(DropLedger::Pickup(root, "b", Pickup("free-capacity", 145, kFirstDroppedEntityId), position).outcome == Outcome::Committed);
		next["expectedRevision"] = 146u;
		assert(DropLedger::Drop(root, "b", next, position).outcome == Outcome::Committed);
		F4T::DurableState::ValidateRoot(root);
		assert(EncodeBaseline(*BuildStableDropSnapshot(root.at("drops").at("live")), 2, Channel::Reliable)->size() < kMaximumDropBaselineBytes);
	}
