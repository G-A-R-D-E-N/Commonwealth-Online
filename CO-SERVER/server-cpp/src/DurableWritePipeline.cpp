#include "DurableWritePipeline.h"

#include "F4TDurableState.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace CommonwealthOnline::Server
{
#ifdef CO_DURABLE_COMMIT_CRASH_PROBE
	void DurableCommitCrashPause(std::string_view a_stage);
#endif

	namespace
	{
		std::runtime_error IoFailure(std::string_view a_operation, const std::filesystem::path& a_path, std::uint64_t a_code)
		{
			return std::runtime_error(
				std::string{ a_operation } + " failed for '" + a_path.string() + "' (code " + std::to_string(a_code) + ")");
		}
	}

	DurableWritePipeline::DurableWritePipeline(
		std::filesystem::path a_path,
		Clock a_clock,
		PostCommitHook a_postCommitHook,
		std::function<void()> a_beforeCommit) :
		_path(std::filesystem::absolute(std::move(a_path))),
		_backupPath(_path.string() + ".bak"),
		_clock(std::move(a_clock)),
		_postCommitHook(std::move(a_postCommitHook)),
		_beforeCommit(std::move(a_beforeCommit)),
		_writer([this](const std::string& a_payload) { ApplyAndCommit(a_payload); })
	{
		if (!_clock) {
			throw std::invalid_argument("Durable write pipeline requires a clock callback.");
		}
		std::filesystem::create_directories(_path.parent_path());
	}

	DurableWritePipeline::~DurableWritePipeline()
	{
		_writer.Drain();
	}

	void DurableWritePipeline::SetDocument(const nlohmann::json& a_root)
	{
		// constraint: startup only, before any post, so the writer thread is idle.
		if (_writer.PostedSequence() != 0) {
			throw std::logic_error("Durable write pipeline document can only be replaced before the first post.");
		}
		_durableRoot = a_root;
	}

	void DurableWritePipeline::PersistDocument(const nlohmann::json& a_root)
	{
		// constraint: startup only, before any post, so the writer thread is idle.
		auto document = a_root;
		document["schemaVersion"] = F4T::DurableState::CurrentSchemaVersion;
		document["savedAt"] = _clock();
		F4T::DurableState::ValidateRoot(document);
		const auto contents = document.dump(2) + "\n";
		bool committed = false;
		CommitContents(contents, committed);
		_durableRoot = std::move(document);
	}

	std::uint64_t DurableWritePipeline::Post(std::vector<DurableSectionDelta> a_sections, bool a_awaitCommit)
	{
		const auto payload = [&] {
			const std::scoped_lock lock(_pendingMutex);
			const auto batch = ++_postedBatch;
			for (auto& delta : a_sections) {
				_pending[delta.name].push_back(PendingSection{ .batch = batch, .awaited = a_awaitCommit, .value = std::move(delta.value), .key = std::move(delta.key) });
			}
			return std::to_string(batch);
		}();
		if (a_awaitCommit) {
			return _writer.PostAndWait(payload);
		}
		return _writer.Post(payload);
	}

	void DurableWritePipeline::PostQuarantineNotice()
	{
		const auto payload = [&] {
			const std::scoped_lock lock(_pendingMutex);
			const auto batch = ++_postedBatch;
			_quarantinedBatches.insert(batch);
			return std::to_string(batch);
		}();
		_writer.Post(payload);
	}

	std::uint64_t DurableWritePipeline::LastPostedTicket() const
	{
		return _writer.PostedSequence();
	}

	bool DurableWritePipeline::Durable(std::uint64_t a_ticket) const
	{
		return _writer.DurableSequence() >= a_ticket;
	}

	bool DurableWritePipeline::AllPostedDurable() const
	{
		return _writer.PostedSequence() <= _writer.DurableSequence();
	}

	bool DurableWritePipeline::TicketFailed(std::uint64_t a_ticket) const
	{
		return _writer.FailedThrough() >= a_ticket && _writer.DurableSequence() < a_ticket;
	}

	bool DurableWritePipeline::TicketQuarantined(std::uint64_t a_ticket) const
	{
		return QuarantinedSince(0, a_ticket);
	}

	bool DurableWritePipeline::QuarantinedSince(std::uint64_t a_afterBatch, std::uint64_t a_throughBatch) const
	{
		const std::scoped_lock lock(_pendingMutex);
		const auto found = _quarantinedBatches.upper_bound(a_afterBatch);
		return found != _quarantinedBatches.end() && *found <= a_throughBatch;
	}

	void DurableWritePipeline::ApplyAndCommit(const std::string& a_payload)
	{
		std::uint64_t batch = 0;
		try {
			batch = std::stoull(a_payload);
		} catch (const std::exception&) {
			throw std::runtime_error("Durable write pipeline received an unreadable commit token.");
		}
		if (_beforeCommit) {
			_beforeCommit();
		}

		std::vector<PendingWrite> applying;
		{
			const std::scoped_lock lock(_pendingMutex);
			for (auto section = _pending.begin(); section != _pending.end();) {
				auto& entries = section->second;
				for (auto entry = entries.begin(); entry != entries.end();) {
					if (entry->batch <= batch) {
						applying.push_back(PendingWrite{ section->first, entry->batch, entry->awaited, std::move(entry->value), std::move(entry->key) });
						entry = entries.erase(entry);
					} else {
						++entry;
					}
				}
				if (entries.empty()) {
					section = _pending.erase(section);
				} else {
					++section;
				}
			}
		}
		std::stable_sort(applying.begin(), applying.end(), [](const PendingWrite& a_left, const PendingWrite& a_right) {
			return a_left.batch < a_right.batch;
		});

		std::vector<PendingWrite> good;
		std::vector<std::string> badSections;
		std::string badError;
		good.reserve(applying.size());
		for (auto& write : applying) {
			try {
				if (write.value) {
					(void)write.value->dump();
				}
				good.push_back(std::move(write));
			} catch (const std::exception& exception) {
				badSections.push_back(write.name);
				if (badError.empty()) {
					badError = exception.what();
				}
				MarkQuarantined(write.batch);
			} catch (...) {
				badSections.push_back(write.name);
				if (badError.empty()) {
					badError = "unknown serialisation failure";
				}
				MarkQuarantined(write.batch);
			}
		}
		if (!badSections.empty()) {
			LogQuarantine(badSections, badError);
		}

		std::vector<UndoEntry> previous;
		previous.reserve(good.size());
		std::string contents;
		try {
			for (const auto& write : good) {
				ApplyWrite(write, previous);
			}
			contents = BuildContents();
		} catch (const std::exception& exception) {
			std::vector<std::string> names;
			names.reserve(good.size());
			for (const auto& write : good) {
				names.push_back(write.name);
				MarkQuarantined(write.batch);
			}
			RestoreSections(previous);
			LogQuarantine(names, exception.what());
			throw;
		}

		bool committed = false;
		try {
			CommitContents(contents, committed);
		} catch (const std::exception& exception) {
			if (committed) {
				_writer.ReportFailure(exception.what());
				return;
			}
			RestoreSections(previous);
			Requeue(good);
			throw;
		} catch (...) {
			if (committed) {
				_writer.ReportFailure("Durable state committed with an unspecified post-commit warning.");
				return;
			}
			RestoreSections(previous);
			Requeue(good);
			throw;
		}
	}

	void DurableWritePipeline::Requeue(std::vector<PendingWrite>& a_writes)
	{
		const std::scoped_lock lock(_pendingMutex);
		for (auto& write : a_writes) {
			if (write.awaited) {
				continue;
			}
			auto& entries = _pending[write.name];
			const auto position = std::lower_bound(
				entries.begin(),
				entries.end(),
				write.batch,
				[](const PendingSection& a_entry, std::uint64_t a_batch) {
					return a_entry.batch < a_batch;
				});
			entries.insert(position, PendingSection{ .batch = write.batch, .awaited = write.awaited, .value = std::move(write.value), .key = std::move(write.key) });
		}
	}

	void DurableWritePipeline::MarkQuarantined(std::uint64_t a_batch)
	{
		const std::scoped_lock lock(_pendingMutex);
		_quarantinedBatches.insert(a_batch);
	}

	void DurableWritePipeline::LogQuarantine(const std::vector<std::string>& a_sections, std::string_view a_error) const
	{
		static std::mutex s_logMutex;
		static std::chrono::steady_clock::time_point s_lastLog{};
		const std::scoped_lock lock(s_logMutex);
		const auto now = std::chrono::steady_clock::now();
		if (s_lastLog.time_since_epoch().count() != 0 && now - s_lastLog < std::chrono::seconds(30)) {
			return;
		}
		s_lastLog = now;
		std::string names;
		for (const auto& name : a_sections) {
			if (!names.empty()) {
				names += ',';
			}
			names += name;
		}
		std::cerr << "Durable save: quarantined section(s) [" << names << "]: " << a_error << '\n';
	}

	void DurableWritePipeline::ApplyWrite(const PendingWrite& a_write, std::vector<UndoEntry>& a_undo)
	{
		auto* container = std::addressof(_durableRoot);
		if (a_write.key) {
			const auto section = _durableRoot.find(a_write.name);
			if (section == _durableRoot.end() || !section->is_object()) {
				throw std::runtime_error("Durable section '" + a_write.name + "' is not an object, so a member write cannot apply.");
			}
			container = std::addressof(*section);
		}
		const auto& member = a_write.key ? *a_write.key : a_write.name;
		const auto found = container->find(member);
		a_undo.push_back(UndoEntry{ a_write.name, a_write.key, found == container->end() ? std::nullopt : std::optional<nlohmann::json>{ std::move(*found) } });
		if (a_write.value) {
			(*container)[member] = *a_write.value;
		} else if (found != container->end()) {
			container->erase(found);
		}
	}

	void DurableWritePipeline::RestoreSections(std::vector<UndoEntry>& a_undo)
	{
		// constraint: undo runs newest first, because a member write may follow a whole-section write in one commit.
		for (auto entry = a_undo.rbegin(); entry != a_undo.rend(); ++entry) {
			auto* container = std::addressof(_durableRoot);
			if (entry->key) {
				container = std::addressof(_durableRoot[entry->name]);
			}
			const auto& member = entry->key ? *entry->key : entry->name;
			if (entry->value) {
				(*container)[member] = std::move(*entry->value);
			} else {
				container->erase(member);
			}
		}
		a_undo.clear();
	}
	std::string DurableWritePipeline::BuildContents()
	{
		_durableRoot["schemaVersion"] = F4T::DurableState::CurrentSchemaVersion;
		_durableRoot["savedAt"] = _clock();
		F4T::DurableState::ValidateRoot(_durableRoot);
		return _durableRoot.dump(2) + "\n";
	}

	void DurableWritePipeline::CommitContents(std::string_view a_contents, bool& a_committed)
	{
		const auto suffix = UniqueSuffix();
		const auto tempPath = std::filesystem::path{ _path.string() + ".tmp-" + suffix };
		const auto backupTempPath = std::filesystem::path{ _backupPath.string() + ".tmp-" + suffix };
		try {
			WriteFileSync(tempPath, a_contents);
#ifdef CO_DURABLE_COMMIT_CRASH_PROBE
			DurableCommitCrashPause("temp-synced");
#endif

			if (std::filesystem::exists(_path)) {
				std::filesystem::copy_file(_path, backupTempPath, std::filesystem::copy_options::none);
				SyncExistingFile(backupTempPath);
				AtomicReplace(backupTempPath, _backupPath);
#ifdef CO_DURABLE_COMMIT_CRASH_PROBE
				DurableCommitCrashPause("backup-replaced");
#endif
			}
			AtomicReplace(tempPath, _path, std::addressof(a_committed));
			if (a_committed && _postCommitHook) {
				_postCommitHook();
			}
		} catch (...) {
			std::error_code ignored;
			std::filesystem::remove(tempPath, ignored);
			std::filesystem::remove(backupTempPath, ignored);
			throw;
		}
	}

	void DurableWritePipeline::WriteFileSync(const std::filesystem::path& a_path, std::string_view a_contents)
	{
#ifdef _WIN32
		const HANDLE handle = CreateFileW(
			a_path.c_str(),
			GENERIC_WRITE,
			0,
			nullptr,
			CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
			nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			throw IoFailure("CreateFileW", a_path, GetLastError());
		}
		std::size_t offset = 0;
		try {
			while (offset < a_contents.size()) {
				const auto remaining = a_contents.size() - offset;
				const auto chunk = static_cast<DWORD>((std::min)(remaining, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
				DWORD written = 0;
				if (!WriteFile(handle, a_contents.data() + offset, chunk, &written, nullptr) || written != chunk) {
					throw IoFailure("WriteFile", a_path, GetLastError());
				}
				offset += written;
			}
			if (!FlushFileBuffers(handle)) {
				throw IoFailure("FlushFileBuffers", a_path, GetLastError());
			}
		} catch (...) {
			CloseHandle(handle);
			throw;
		}
		CloseHandle(handle);
#else
		const int descriptor = ::open(a_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
		if (descriptor < 0) {
			throw IoFailure("open", a_path, static_cast<std::uint64_t>(errno));
		}
		std::size_t offset = 0;
		try {
			while (offset < a_contents.size()) {
				const auto written = ::write(descriptor, a_contents.data() + offset, a_contents.size() - offset);
				if (written < 0) {
					if (errno == EINTR) {
						continue;
					}
					throw IoFailure("write", a_path, static_cast<std::uint64_t>(errno));
				}
				offset += static_cast<std::size_t>(written);
			}
			if (::fsync(descriptor) != 0) {
				throw IoFailure("fsync", a_path, static_cast<std::uint64_t>(errno));
			}
		} catch (...) {
			::close(descriptor);
			throw;
		}
		::close(descriptor);
#endif
	}

	void DurableWritePipeline::SyncExistingFile(const std::filesystem::path& a_path)
	{
#ifdef _WIN32
		const HANDLE handle = CreateFileW(
			a_path.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ,
			nullptr,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
			nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			throw IoFailure("CreateFileW", a_path, GetLastError());
		}
		if (!FlushFileBuffers(handle)) {
			const auto code = GetLastError();
			CloseHandle(handle);
			throw IoFailure("FlushFileBuffers", a_path, code);
		}
		CloseHandle(handle);
#else
		const int descriptor = ::open(a_path.c_str(), O_RDONLY | O_CLOEXEC);
		if (descriptor < 0) {
			throw IoFailure("open", a_path, static_cast<std::uint64_t>(errno));
		}
		if (::fsync(descriptor) != 0) {
			const auto code = errno;
			::close(descriptor);
			throw IoFailure("fsync", a_path, static_cast<std::uint64_t>(code));
		}
		::close(descriptor);
#endif
	}

	void DurableWritePipeline::AtomicReplace(
		const std::filesystem::path& a_source,
		const std::filesystem::path& a_destination,
		bool* a_replaced)
	{
#ifdef _WIN32
		if (!MoveFileExW(
				a_source.c_str(),
				a_destination.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			throw IoFailure("MoveFileExW", a_destination, GetLastError());
		}
		if (a_replaced != nullptr) {
			*a_replaced = true;
		}
#else
		if (::rename(a_source.c_str(), a_destination.c_str()) != 0) {
			throw IoFailure("rename", a_destination, static_cast<std::uint64_t>(errno));
		}
		// Constraint: the rename is the commit point, so a directory-sync failure after it is still a commit.
		if (a_replaced != nullptr) {
			*a_replaced = true;
		}
		SyncDirectory(a_destination.parent_path());
#endif
	}

	void DurableWritePipeline::SyncDirectory(const std::filesystem::path& a_directory)
	{
#ifndef _WIN32
		const int descriptor = ::open(a_directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (descriptor < 0) {
			throw IoFailure("open directory", a_directory, static_cast<std::uint64_t>(errno));
		}
		if (::fsync(descriptor) != 0) {
			const auto code = errno;
			::close(descriptor);
			throw IoFailure("fsync directory", a_directory, static_cast<std::uint64_t>(code));
		}
		::close(descriptor);
#else
		(void)a_directory;
#endif
	}

	std::string DurableWritePipeline::UniqueSuffix()
	{
		static std::atomic<std::uint64_t> sequence{ 0 };
		const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
		return std::to_string(static_cast<std::uint64_t>(ticks)) + "-" + std::to_string(++sequence);
	}

	void DurableWritePipeline::WaitForCommits()
	{
		_writer.Drain();
	}

	void DurableWritePipeline::ReportFailure(std::string a_message)
	{
		_writer.ReportFailure(std::move(a_message));
	}

	bool DurableWritePipeline::LastWriteFailed() const
	{
		return _writer.LastWriteFailed();
	}

	std::optional<std::string> DurableWritePipeline::TakeFailure()
	{
		return _writer.TakeFailure();
	}

	double DurableWritePipeline::SlowestCommitSeconds() const
	{
		return _writer.SlowestCommitSeconds();
	}

	std::size_t DurableWritePipeline::CommitCount() const
	{
		return _writer.CommitCount();
	}
}
