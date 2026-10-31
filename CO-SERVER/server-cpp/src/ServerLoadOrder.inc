		std::vector<OutboundFrame> ExistingPluginListFrames(std::uint32_t a_connectionId) const
		{
			std::vector<OutboundFrame> frames;
			for (const auto& [connectionId, session] : sessions) {
				if (connectionId == a_connectionId || !session.active || !session.pluginListPacket) {
					continue;
				}
				frames.push_back(ReliableFrame(a_connectionId, *session.pluginListPacket));
			}
			return frames;
		}

		std::vector<OutboundFrame> HandlePluginList(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_unixTimeSeconds)
		{
			if (a_session.pluginListPacket) {
				Reject();
				stats.pluginListsRejected++;
				return {};
			}

			const auto data = ReadString(a_packet, "data");
			const auto decoded = data ? F4T::LoadOrder::DecodePluginList(*data) : std::nullopt;
			if (!decoded) {
				Reject();
				stats.pluginListsRejected++;
				return {};
			}

			stats.pluginListsReceived++;
			Json stored{
				{ "type", "pluginList" },
				{ "playerId", a_session.playerId },
				{ "data", *data },
				{ "serverTime", a_unixTimeSeconds }
			};
			a_session.pluginListPacket = stored;

			std::vector<OutboundFrame> frames;
			for (const auto& [connectionId, recipient] : sessions) {
				if (connectionId == a_connectionId || !recipient.active) {
					continue;
				}
				frames.push_back(ReliableFrame(connectionId, stored));
			}
			return frames;
		}
