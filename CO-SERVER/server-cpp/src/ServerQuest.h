		public:
			bool EnableQuestSync(
				PersistenceGate* a_persistence,
				F4T::QuestProtocol::Mode a_mode,
				std::vector<F4T::QuestProtocol::OverrideEntry> a_overrides);

			[[nodiscard]] bool QuestSyncEnabled() const noexcept;

			[[nodiscard]] F4T::QuestProtocol::Mode QuestMode() const noexcept;

			[[nodiscard]] Json QuestStatus() const;

			[[nodiscard]] bool IsQuestGated(const F4T::QuestProtocol::FormKey& a_quest, std::uint16_t a_stage) const;

			[[nodiscard]] bool IsQuestSolo(std::string_view a_profileId, const F4T::QuestProtocol::FormKey& a_quest) const;

			void SetQuestStoryOrder(const F4T::QuestStory::Order& a_order);

			std::vector<OutboundFrame> SetQuestMode(F4T::QuestProtocol::Mode a_mode, double a_unixTimeSeconds);

			std::vector<OutboundFrame> SetQuestGate(
				const F4T::QuestProtocol::FormKey& a_quest,
				std::uint16_t a_stage,
				bool a_release,
				double a_unixTimeSeconds);

			std::vector<OutboundFrame> SetQuestSolo(
				std::string_view a_profileId,
				const F4T::QuestProtocol::FormKey& a_quest,
				bool a_solo,
				double a_unixTimeSeconds,
				std::optional<std::uint32_t> a_requesterConnection = std::nullopt,
				bool a_awaitCommit = true);

			std::vector<OutboundFrame> SetQuestScope(
				std::string_view a_profileId,
				const F4T::QuestProtocol::FormKey& a_quest,
				F4T::QuestProtocol::Scope a_scope,
				double a_unixTimeSeconds,
				std::optional<std::uint32_t> a_requesterConnection,
				bool a_awaitCommit);

		private:
			bool questsEnabled{};
			PersistenceGate* questPersistence{};
			F4T::QuestProtocol::Mode questMode{ F4T::QuestProtocol::Mode::CoopStory };
			std::vector<F4T::QuestProtocol::OverrideEntry> questOverrides;
			F4T::QuestState::Table quests;
			F4T::QuestStory::Order storyOrder;
			std::map<std::string, std::uint16_t> tutorialExitStages;

			bool SaveQuests(bool a_awaitCommit = true);

			[[nodiscard]] static bool StoryContentAllowed(F4T::QuestProtocol::Mode a_mode) noexcept;

			enum class StoryRefusal : std::uint8_t
			{
				None,
				UnknownQuest,
				PrerequisiteMissing,
				NotStartable,
				StageRegression
			};

			struct StoryCheck
			{
				StoryRefusal refusal{ StoryRefusal::None };
				std::optional<F4T::QuestProtocol::FormKey> missing;
			};

			[[nodiscard]] StoryCheck StoryReachable(
				std::string_view a_profileId,
				const F4T::QuestProtocol::FormKey& a_quest,
				std::uint16_t a_stage) const;

			[[nodiscard]] bool StoryStageVisible(
				std::string_view a_profileId,
				const F4T::QuestProtocol::FormKey& a_quest,
				std::uint16_t a_stage) const;

			[[nodiscard]] static bool SupportsQuestSync(const Session& a_session) noexcept;

			[[nodiscard]] static bool RequiresQuestReady(const Session& a_session) noexcept;

			[[nodiscard]] bool NoteTutorialProgress(std::string_view a_profileId, std::uint16_t a_stage);
			[[nodiscard]] bool TutorialExitSeen(std::string_view a_profileId) const;
			[[nodiscard]] std::optional<std::uint16_t> StoryStageFor(
				std::string_view a_profileId,
				const F4T::QuestProtocol::FormKey& a_quest) const;

			[[nodiscard]] std::optional<std::uint32_t> QuestConnectionForProfile(std::string_view a_profileId) const;

			std::vector<OutboundFrame> CommitQuestScope(
				const F4T::QuestState::Table& a_snapshot,
				std::string_view a_profileId,
				double a_unixTimeSeconds,
				std::optional<std::uint32_t> a_requesterConnection,
				bool a_awaitCommit);

			[[nodiscard]] bool QuestAudience(
				const Session& a_recipient,
				const F4T::QuestProtocol::FormKey& a_quest,
				std::string_view a_track,
				std::string_view a_sourceParty) const;

			[[nodiscard]] std::string QuestFactionOfPlayer(std::uint32_t a_playerId) const;

			std::vector<OutboundFrame> BroadcastQuestStage(
				const F4T::QuestProtocol::Stage& a_stage,
				std::string_view a_track,
				std::string_view a_sourceParty,
				double a_unixTimeSeconds) const;

			std::vector<OutboundFrame> QuestSyncFrames(std::uint32_t a_connectionId, const Session& a_target, double a_unixTimeSeconds) const;

			F4T::QuestProtocol::Policy QuestPolicyFor(const Session& a_target) const;

			Json QuestPolicyPacket(const Session& a_target, double a_unixTimeSeconds) const;

			std::vector<OutboundFrame> QuestNotReadyFrame(
				std::uint32_t a_connectionId,
				const Json& a_packet,
				std::string_view a_reason,
				double a_unixTimeSeconds) const;

			std::vector<OutboundFrame> QuestNotReachableFrame(
				std::uint32_t a_connectionId,
				const F4T::QuestProtocol::FormKey& a_quest,
				const StoryCheck& a_check,
				double a_unixTimeSeconds) const;

			std::vector<OutboundFrame> QuestPolicyFrames(double a_unixTimeSeconds) const;
		private:
			std::vector<OutboundFrame> HandleQuestReport(
				std::uint32_t a_connectionId,
				Session& a_session,
				const Json& a_packet,
				double a_monotonicSeconds,
				double a_unixTimeSeconds);

			std::vector<OutboundFrame> HandleQuestScope(
				std::uint32_t a_connectionId,
				Session& a_session,
				const Json& a_packet,
				double a_monotonicSeconds,
				double a_unixTimeSeconds);

			std::optional<std::vector<OutboundFrame>> DispatchQuestPacket(
				const std::string& a_type,
				std::uint32_t a_connectionId,
				Session& a_session,
				const Json& a_packet,
				double a_monotonicSeconds,
				double a_unixTimeSeconds);

			std::vector<OutboundFrame> ExistingQuestFrames(
				std::uint32_t a_connectionId,
				const Session& a_target,
				double a_unixTimeSeconds);
