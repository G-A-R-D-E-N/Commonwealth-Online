#pragma once

#include "DurableCommitWriter.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server
{
	struct DurableSectionDelta
	{
		std::string name;
		std::optional<nlohmann::json> value;
		// constraint: set means value replaces (or, when empty, erases) only member `key` of object section `name`.
		std::optional<std::string> key;
	};

	// Constraint: the durable document is only touched on the writer thread, so the main loop never serialises it.
	class DurableWritePipeline final
	{
	public:
		using Clock = std::function<double()>;
		using PostCommitHook = std::function<void()>;

		DurableWritePipeline(
			std::filesystem::path a_path,
			Clock a_clock,
			PostCommitHook a_postCommitHook = {},
			std::function<void()> a_beforeCommit = {});
		~DurableWritePipeline();
		DurableWritePipeline(const DurableWritePipeline&) = delete;
		DurableWritePipeline& operator=(const DurableWritePipeline&) = delete;

		void SetDocument(const nlohmann::json& a_root);
		void PersistDocument(const nlohmann::json& a_root);
		// constraint: callers must serialise posts so the batch order matches the commit order.
		std::uint64_t Post(std::vector<DurableSectionDelta> a_sections, bool a_awaitCommit);
		// constraint: posted after the store rolled a section back, so its replies are failed instead of sent.
		void PostQuarantineNotice();
		void WaitForCommits();
		[[nodiscard]] std::uint64_t LastPostedTicket() const;
		[[nodiscard]] bool Durable(std::uint64_t a_ticket) const;
		[[nodiscard]] bool AllPostedDurable() const;
		[[nodiscard]] bool TicketFailed(std::uint64_t a_ticket) const;
		[[nodiscard]] bool TicketQuarantined(std::uint64_t a_ticket) const;
		[[nodiscard]] bool QuarantinedSince(std::uint64_t a_afterBatch, std::uint64_t a_throughBatch) const;
		void ReportFailure(std::string a_message);
		[[nodiscard]] bool LastWriteFailed() const;
		[[nodiscard]] std::optional<std::string> TakeFailure();
		[[nodiscard]] double SlowestCommitSeconds() const;
		[[nodiscard]] std::size_t CommitCount() const;

	private:
		struct PendingSection
		{
			std::uint64_t batch{};
			bool awaited{};
			std::optional<nlohmann::json> value;
			std::optional<std::string> key;
		};

		struct PendingWrite
		{
			std::string name;
			std::uint64_t batch{};
			bool awaited{};
			std::optional<nlohmann::json> value;
			std::optional<std::string> key;
		};

		struct UndoEntry
		{
			std::string name;
			std::optional<std::string> key;
			std::optional<nlohmann::json> value;
		};

		void ApplyAndCommit(const std::string& a_payload);
		void Requeue(std::vector<PendingWrite>& a_writes);
		void MarkQuarantined(std::uint64_t a_batch);
		void LogQuarantine(const std::vector<std::string>& a_sections, std::string_view a_error) const;
		void RestoreSections(std::vector<UndoEntry>& a_undo);
		void ApplyWrite(const PendingWrite& a_write, std::vector<UndoEntry>& a_undo);
		void ValidateDocument();
		[[nodiscard]] std::string BuildContents();
		// constraint: re-applies a failed batch write by write; returns true when an awaited write had to be dropped.
		bool IsolateInvalidWrites(std::vector<PendingWrite>& a_writes, std::vector<UndoEntry>& a_undo, std::string_view a_batchError);
		bool SaveValidProfiles(const PendingWrite& a_write, std::vector<UndoEntry>& a_undo, std::string_view a_error);
		void CommitContents(std::string_view a_contents, bool& a_committed);

		static void WriteFileSync(const std::filesystem::path& a_path, std::string_view a_contents);
		static void SyncExistingFile(const std::filesystem::path& a_path);
		static void AtomicReplace(
			const std::filesystem::path& a_source,
			const std::filesystem::path& a_destination,
			bool* a_replaced = nullptr);
		static void SyncDirectory(const std::filesystem::path& a_directory);
		[[nodiscard]] static std::string UniqueSuffix();

		std::filesystem::path _path;
		std::filesystem::path _backupPath;
		Clock _clock;
		PostCommitHook _postCommitHook;
		std::function<void()> _beforeCommit;
		nlohmann::json _durableRoot;
		mutable std::mutex _pendingMutex;
		std::map<std::string, std::vector<PendingSection>> _pending;
		std::set<std::uint64_t> _quarantinedBatches;
		std::uint64_t _postedBatch{};
		DurableCommitWriter _writer;
	};
}
