#pragma once

#include "F4TDropLedger.h"
#include "F4TInventoryContract.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace F4T::InventoryOps
{
	using EntityId = F4T::Drops::EntityId;
	using Operation = F4T::InventoryContract::OwnedOperation;
	using OwnedResult = F4T::InventoryContract::OwnedResult;

	inline constexpr std::size_t kMaximumPendingOperations = 8;
	inline constexpr std::uint64_t kFirstResendMs = 2000;
	inline constexpr std::uint64_t kSecondResendMs = 5000;
	inline constexpr std::uint64_t kOperationTimeoutMs = 10000;
	inline constexpr std::uint64_t kRateLimitedRequeueMs = 250;
	inline constexpr std::uint32_t kMaximumStaleResyncs = 1;
	inline constexpr std::size_t kMaximumResolvedOperations = F4T::Drops::kMaximumOperationHistory;
	inline constexpr std::size_t kMaximumBoundEntities = 4096;
	inline constexpr std::size_t kMaximumSnapshotEntriesPerChunk = 256;
	inline constexpr std::size_t kMaximumDeltaChangesPerChunk = 64;

	struct OwnedRequest
	{
		Operation kind{ Operation::Drop };
		std::string itemKey;
		std::uint32_t count{};
		EntityId entityId{};
	};

	enum class PollAction : std::uint8_t
	{
		Idle,
		Send,
		Resend,
		Fail
	};

	struct PollOutcome
	{
		PollAction action{ PollAction::Idle };
		Operation kind{ Operation::Drop };
		std::string operationId;
		nlohmann::json request;
	};

	enum class ResolveOutcome : std::uint8_t
	{
		Commit,
		Reject,
		StaleResync,
		Unknown,
		Duplicate,
		LateCommit,
		Requeued
	};

	struct LateCommitAction
	{
		Operation kind{ Operation::Drop };
		EntityId entityId{};
		std::string itemKey;
		std::uint32_t count{};
	};

	class PendingOwnedOps
	{
	public:
		explicit PendingOwnedOps(std::string a_sessionNonce);

		[[nodiscard]] std::optional<std::string> Begin(const OwnedRequest& a_request);
		[[nodiscard]] PollOutcome Poll(std::uint64_t a_nowMs);
		[[nodiscard]] ResolveOutcome Resolve(const OwnedResult& a_result, std::uint64_t a_nowMs, LateCommitAction* a_late = nullptr);
		[[nodiscard]] std::vector<std::string> Reset();
		void SetExpectedRevision(std::uint32_t a_revision) noexcept;
		[[nodiscard]] std::uint32_t ExpectedRevision() const noexcept;
		[[nodiscard]] std::size_t PendingCount() const noexcept;
		[[nodiscard]] bool InFlight() const noexcept;

	private:
		enum class FailedState : std::uint8_t
		{
			Committed,
			Rejected,
			FailedSent,
			FailedUnsent
		};

		struct Pending
		{
			std::string operationId;
			OwnedRequest request;
			nlohmann::json sent;
			bool hasSent{};
			bool everSent{};
			std::uint64_t firstSentMs{};
			std::uint64_t sentAtMs{};
			std::uint32_t sentRevision{};
			std::uint32_t staleResyncs{};
			bool firstResendDone{};
			bool secondResendDone{};
			std::uint64_t notBeforeMs{};
		};

		struct History
		{
			std::string operationId;
			FailedState state{ FailedState::Rejected };
			OwnedRequest request;
		};

		[[nodiscard]] std::optional<nlohmann::json> BuildRequest(const Pending& a_pending) const;
		void Rearm(Pending& a_pending, std::uint64_t a_notBeforeMs) noexcept;
		[[nodiscard]] History* FindHistory(const std::string& a_operationId);
		void Remember(const std::string& a_operationId, FailedState a_state, const OwnedRequest& a_request);
		void AdvanceRevision(std::uint32_t a_revision) noexcept;

		std::string nonce;
		std::uint32_t counter{};
		std::uint32_t expectedRevision{};
		std::vector<Pending> pending;
		std::vector<History> history;
	};

	class DropEntityBindings
	{
	public:
		[[nodiscard]] bool ReserveForLocalOp(EntityId a_entityId);
		[[nodiscard]] bool Bind(EntityId a_entityId, const std::string& a_refKey);
		[[nodiscard]] bool Unbind(EntityId a_entityId);
		[[nodiscard]] std::optional<EntityId> FindEntity(const std::string& a_refKey) const;
		[[nodiscard]] bool ShouldMaterialize(EntityId a_entityId) const;
		[[nodiscard]] bool IsReserved(EntityId a_entityId) const;
		[[nodiscard]] bool IsBound(EntityId a_entityId) const;
		[[nodiscard]] std::size_t TrackedCount() const noexcept;

	private:
		struct Entry
		{
			std::string refKey;
			bool bound{};
		};

		std::map<EntityId, Entry> entries;
		std::map<std::string, EntityId> byRef;
	};

	enum class DropVerdict : std::uint8_t
	{
		PassThrough,
		Gate,
		Block
	};

	enum class FormKind : std::uint8_t
	{
		Book,
		Note,
		Other
	};

	enum class DropOrigin : std::uint8_t
	{
		Menu,
		CompanionApp,
		Scripted,
		Unknown
	};

	struct DropContext
	{
		std::optional<std::string> itemKey;
		bool hasInstanceData{};
		FormKind formKind{ FormKind::Other };
		DropOrigin origin{ DropOrigin::Menu };
		std::uint32_t count{};
		std::optional<std::uint32_t> viewCount;
	};

	struct DropPolicy
	{
		bool blockInstanceData{ true };
		bool excludeBooksAndNotes{ true };
		bool gateCompanionApp{ true };
	};

	struct DropDecision
	{
		DropVerdict verdict{ DropVerdict::PassThrough };
		std::string reason;
	};

	[[nodiscard]] DropDecision ClassifyDrop(const DropContext& a_context, const DropPolicy& a_policy = {});

	enum class PickupVerdict : std::uint8_t
	{
		PassThrough,
		Gate
	};

	struct PickupDecision
	{
		PickupVerdict verdict{ PickupVerdict::PassThrough };
		std::string reason;
	};

	[[nodiscard]] PickupDecision ClassifyPickup(const std::optional<std::string>& a_refKey, const DropEntityBindings& a_bindings);

	struct InventoryCount
	{
		std::string itemKey;
		std::uint32_t count{};

		bool operator==(const InventoryCount&) const = default;
	};

	struct InventoryChange
	{
		std::string itemKey;
		std::uint32_t count{};

		bool operator==(const InventoryChange&) const = default;
	};

	using CountMap = std::map<std::string, std::uint32_t>;

	[[nodiscard]] std::vector<nlohmann::json> BuildInventorySnapshot(const std::vector<InventoryCount>& a_engineCounts);
	[[nodiscard]] std::vector<nlohmann::json> BuildInventoryDelta(std::uint32_t a_sequence, const std::vector<InventoryChange>& a_changes);
	[[nodiscard]] std::vector<InventoryChange> DiffSnapshot(const CountMap& a_mirror, const std::vector<InventoryCount>& a_engineCounts);
}
