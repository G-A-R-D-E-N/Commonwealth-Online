			F4T::ParkedSuit::Table parkedSuits;
			PersistenceGate* parkedSuitPersistence = nullptr;
			static constexpr double kParkedSuitRefillPerSecond = 1.0;
			static constexpr double kParkedSuitBurst = 4.0;
			static constexpr double kParkedSuitChallengeLifetimeSeconds = 120.0;
			static constexpr double kParkedSuitNearbyOwnerUnits = 4096.0;
			static constexpr double kParkedSuitPieceAuditSeconds = 15.0;
			static constexpr double kParkedSuitFrameObservationSeconds = 600.0;
			static constexpr double kParkedSuitFrameObservationThrottleSeconds = 1.0;
			static constexpr double kParkedSuitFrameObservationPurgeSeconds = 10.0;
			static constexpr double kParkedSuitBreachCooldownSeconds = 30.0;
			static constexpr double kParkedSuitOfflineRetentionSeconds = 14.0 * 24.0 * 60.0 * 60.0;
			static constexpr double kParkedSuitSweepIntervalSeconds = 3600.0;
			static constexpr std::size_t kParkedSuitOfflineBreachLength = F4T::ParkedSuit::Breach::kMaximumLength;
			static constexpr std::size_t kMaximumParkedSuitBreachCooldowns = 4096;
			double nextParkedSuitSweepMonotonic = 0.0;
			std::map<std::pair<std::string, std::string>, double> parkedSuitBreachCooldowns;
			std::map<std::string, SessionTypes::ParkedSuitFrameObservation> parkedSuitFrameObservations;
			double nextParkedSuitFrameObservationPurgeMonotonic = 0.0;

		public:
			void EnableParkedSuits(PersistenceGate* a_persistence);
		private:

			bool SaveParkedSuits();

			Json ParkPacketFor(const std::string& a_recipientKey, const F4T::ParkedSuit::Record& a_record) const;

			std::vector<OutboundFrame> ParkedSuitFramesFor(std::uint32_t a_connectionId, const Session& a_target,
				const std::optional<F4T::InterestScope::StateScope>& a_previous = std::nullopt, double a_previousMarginUnits = 0.0) const;

			bool ParkedSuitInReach(const Session& a_session, const F4T::ObjectWire::ObjectPosition& a_position, double a_monotonicSeconds) const;

			bool ParkedSuitRecipientOnline(const std::string& a_playerKey) const;

			std::vector<OutboundFrame> ParkedSuitReleaseFramesFor(std::uint32_t a_connectionId, const Session& a_target);

			bool ParkedSuitChallengeLive(const std::optional<SessionTypes::ParkedSuitBreachChallenge>& a_challenge, std::string_view a_suitId, double a_monotonicSeconds) const;

			std::size_t ParkedSuitBreachLength(const std::string& a_ownerKey, const F4T::ObjectWire::ObjectPosition& a_position) const;

			void ArmParkedSuitPieceAudit(std::uint32_t a_connectionId, Session& a_session, const F4T::ParkedSuit::Record& a_record, double a_monotonicSeconds);

			void AuditParkedSuitPiecesOnSnapshot(std::uint32_t a_connectionId, Session& a_session, double a_monotonicSeconds);

			std::vector<OutboundFrame> ParkedSuitWriteRejected(std::uint32_t a_connectionId, const Json& a_packet, std::string_view a_reason = {});

			std::vector<OutboundFrame> HandleParkedSuitWrite(
				std::uint32_t a_connectionId,
				Session& a_session,
				const Json& a_packet,
				double a_monotonicSeconds);

			std::vector<OutboundFrame> HandleParkedSuitBreach(
				std::uint32_t a_connectionId,
				Session& a_session,
				const Json& a_packet,
				double a_monotonicSeconds);

			void ObserveParkedSuitFrame(Session& a_session, const Json& a_packet, double a_monotonicSeconds);

			bool ParkedSuitFrameObserved(const std::string& a_ownerKey, const Json& a_powerArmor, double a_monotonicSeconds) const;

			std::vector<OutboundFrame> ParkedSuitEvictionFrames(const F4T::ParkedSuit::Record& a_record) const;

			std::set<std::string> ParkedSuitOnlineOwners() const;

			bool ParkedSuitBreachCooling(const std::string& a_hackerKey, const std::string& a_suitId, double a_monotonicSeconds) const;

			void StartParkedSuitBreachCooldown(const std::string& a_hackerKey, const std::string& a_suitId, double a_monotonicSeconds);

			void LogParkedSuitEviction(const F4T::ParkedSuit::Record& a_record, std::string_view a_reason) const;

			std::vector<OutboundFrame> SweepParkedSuits(double a_monotonicSeconds, double a_unixTimeSeconds);

			std::vector<OutboundFrame> HandleParkedSuitRequest(
				std::uint32_t a_connectionId,
				Session& a_session,
				const Json& a_packet,
				double a_monotonicSeconds);
