#include "DurableWritePipeline.h"
#include "ServerDeferredFrames.h"

#include "F4TDurableState.h"
#include "DurableStateFileStore.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	void Require(bool a_condition)
	{
		if (a_condition) {
			return;
		}
		std::cerr << "check failed\n";
		std::abort();
	}

	using CommonwealthOnline::Server::DurableSectionDelta;
	using CommonwealthOnline::Server::DurableStateFileStore;
	using CommonwealthOnline::Server::DurableWritePipeline;

	nlohmann::json NewDocument()
	{
		return F4T::DurableState::NewRoot("0123456789abcdef0123456789abcdef", 1000.0);
	}

	nlohmann::json ReadDocument(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		std::ostringstream buffer;
		buffer << input.rdbuf();
		return nlohmann::json::parse(buffer.str());
	}

	DurableSectionDelta Delta(std::string a_name, nlohmann::json a_value)
	{
		return DurableSectionDelta{
			.name = std::move(a_name),
			.value = std::optional<nlohmann::json>{ std::move(a_value) }
		};
	}

	std::filesystem::path MakeDirectory(const char* a_name)
	{
		const auto directory = std::filesystem::temp_directory_path() / a_name;
		std::filesystem::remove_all(directory);
		std::filesystem::create_directories(directory);
		return directory;
	}

	void AnAwaitedSectionIsDurableWhenTheCallReturns()
	{
		const auto directory = MakeDirectory("co-durable-pipeline-awaited");
		const auto path = directory / "state.json";
		std::atomic<double> now{ 1000.0 };
		DurableWritePipeline pipeline(path, [&] { return now.load(); });
		pipeline.PersistDocument(NewDocument());

		now = 1001.0;
		pipeline.Post({ Delta("factions", nlohmann::json{ { "version", 7 } }) }, true);
		const auto document = ReadDocument(path);
		Require(document.at("factions").at("version") == 7);
		Require(document.at("savedAt") == 1001.0);
	}

	void CoalescedPostsCarryEveryChangedSection()
	{
		const auto directory = MakeDirectory("co-durable-pipeline-coalesced");
		const auto path = directory / "state.json";
		std::atomic<double> now{ 1000.0 };
		DurableWritePipeline pipeline(path, [&] { return now.load(); });
		pipeline.PersistDocument(NewDocument());

		now = 1002.0;
		pipeline.Post({ Delta("factions", nlohmann::json{ { "version", 1 } }) }, false);
		pipeline.Post({ Delta("quests", nlohmann::json{ { "version", 2 } }) }, false);
		pipeline.Post({ Delta("workshop", nlohmann::json{ { "version", 3 } }) }, true);

		const auto document = ReadDocument(path);
		Require(document.at("factions").at("version") == 1);
		Require(document.at("quests").at("version") == 2);
		Require(document.at("workshop").at("version") == 3);
	}

	void DrainCommitsEveryPendingSection()
	{
		const auto directory = MakeDirectory("co-durable-pipeline-drain");
		const auto path = directory / "state.json";
		std::atomic<double> now{ 1000.0 };
		DurableWritePipeline pipeline(path, [&] { return now.load(); });
		pipeline.PersistDocument(NewDocument());

		now = 1003.0;
		pipeline.Post({ Delta("factions", nlohmann::json{ { "version", 4 } }) }, false);
		pipeline.WaitForCommits();

		const auto document = ReadDocument(path);
		Require(document.at("factions").at("version") == 4);
		Require(pipeline.CommitCount() >= 1);
	}

	void ATicketIsDurableOnceItsCommitLands()
	{
		const auto directory = MakeDirectory("co-durable-pipeline-ticket");
		const auto path = directory / "state.json";
		std::atomic<double> now{ 1000.0 };
		DurableWritePipeline pipeline(path, [&] { return now.load(); });
		pipeline.PersistDocument(NewDocument());

		now = 1004.0;
		pipeline.Post({ Delta("factions", nlohmann::json{ { "version", 5 } }) }, false);
		const auto ticket = pipeline.LastPostedTicket();
		Require(ticket > 0);
		pipeline.WaitForCommits();
		Require(pipeline.Durable(ticket));
		Require(pipeline.AllPostedDurable());
		Require(!pipeline.TicketFailed(ticket));
	}

	void HeldFramesKeepTheirOrderAndTheirOwnTickets()
	{
		CommonwealthOnline::Server::DeferredFrameQueue queue;
		CommonwealthOnline::Server::OutboundFrame first{ .connectionId = 3, .payload = { 1, 2 } };
		CommonwealthOnline::Server::OutboundFrame second{ .connectionId = 3, .payload = { 3 } };
		queue.Park(3, std::move(first), true, 7, 100.0);
		queue.Park(3, std::move(second), false, 9, 200.0);
		Require(queue.Blocked(3));
		Require(!queue.Blocked(4));
		const auto& entries = queue.Entries();
		Require(entries.size() == 1);
		Require(entries.front().frames.size() == 2);
		Require(entries.front().frames.front().ticket == 7);
		Require(entries.front().frames.front().expiresAtMonotonic == 100.0);
		Require(entries.front().frames.front().identityReady);
		Require(entries.front().frames.back().ticket == 9);
		Require(entries.front().frames.back().expiresAtMonotonic == 200.0);
		Require(entries.front().frames.back().frame.payload.size() == 1);
		queue.Erase(3);
		Require(queue.Empty());
	}

	void AFrameBehindAHeldOneWaitsForItsHead()
	{
		CommonwealthOnline::Server::DeferredFrameQueue queue;
		queue.Park(3, CommonwealthOnline::Server::OutboundFrame{ .connectionId = 3, .payload = { 1 } }, true, 7, 100.0);
		queue.Park(3, CommonwealthOnline::Server::OutboundFrame{ .connectionId = 3, .payload = { 2 } }, false, 9, 100.0);
		const auto durableOnlySeven = [](std::uint64_t a_ticket) { return a_ticket <= 7; };

		auto sweep = CommonwealthOnline::Server::SweepDeferredFrames(queue.Entries().front(), 50.0, durableOnlySeven, [](std::uint64_t, std::uint64_t) { return false; });
		Require(sweep.delivered.size() == 1);
		Require(sweep.abandoned.empty());
		Require(queue.Entries().front().frames.size() == 1);
		Require(!queue.Entries().front().frames.front().identityReady);

		auto rest = CommonwealthOnline::Server::SweepDeferredFrames(queue.Entries().front(), 50.0, [](std::uint64_t) { return true; }, [](std::uint64_t, std::uint64_t) { return false; });
		Require(rest.delivered.size() == 1);
		Require(rest.abandoned.empty());
		Require(rest.delivered.front().frame.payload.size() == 1);
		Require(queue.Entries().front().frames.empty());

		CommonwealthOnline::Server::DeferredFrameQueue blocked;
		blocked.Park(4, CommonwealthOnline::Server::OutboundFrame{ .connectionId = 4, .payload = { 5 } }, true, 7, 100.0);
		blocked.Park(4, CommonwealthOnline::Server::OutboundFrame{ .connectionId = 4, .payload = { 6 } }, false, 0, 100.0);
		auto blockedSweep = CommonwealthOnline::Server::SweepDeferredFrames(
			blocked.Entries().front(), 50.0, [](std::uint64_t) { return false; }, [](std::uint64_t, std::uint64_t) { return false; });
		Require(blockedSweep.delivered.empty());
		Require(blockedSweep.abandoned.empty());
		Require(blocked.Entries().front().frames.size() == 2);

		auto released = CommonwealthOnline::Server::SweepDeferredFrames(
			blocked.Entries().front(), 50.0, [](std::uint64_t) { return true; }, [](std::uint64_t, std::uint64_t) { return false; });
		Require(released.delivered.size() == 2);
		Require(released.delivered.front().frame.payload.size() == 1);
		Require(released.delivered.back().frame.payload.size() == 1);
	}

	void AnExpiredHoldIsAbandonedWithEverythingBehindIt()
	{
		CommonwealthOnline::Server::DeferredFrameQueue queue;
		queue.Park(3, CommonwealthOnline::Server::OutboundFrame{ .connectionId = 3, .payload = { 1 } }, true, 7, 100.0);
		queue.Park(3, CommonwealthOnline::Server::OutboundFrame{ .connectionId = 3, .payload = { 2 } }, false, 0, 100.0);
		const auto neverDurable = [](std::uint64_t) { return false; };

		auto sweep = CommonwealthOnline::Server::SweepDeferredFrames(queue.Entries().front(), 100.0, neverDurable, [](std::uint64_t, std::uint64_t) { return false; });
		Require(sweep.delivered.empty());
		Require(sweep.abandoned.size() == 2);
		Require(queue.Entries().front().frames.empty());
		Require(CommonwealthOnline::Server::DeferredVerdict(false, false, 99.9, 100.0) == CommonwealthOnline::Server::DeferredSweepVerdict::hold);
		Require(CommonwealthOnline::Server::DeferredVerdict(true, false, 200.0, 100.0) == CommonwealthOnline::Server::DeferredSweepVerdict::deliver);
	}

	void AQuarantinedSectionAbandonsItsRepliesImmediately()
	{
		CommonwealthOnline::Server::DeferredFrameQueue queue;
		queue.Park(5, CommonwealthOnline::Server::OutboundFrame{ .connectionId = 5, .payload = { 1 } }, true, 7, 100.0);
		queue.Park(5, CommonwealthOnline::Server::OutboundFrame{ .connectionId = 5, .payload = { 2 } }, false, 0, 100.0);
		auto sweep = CommonwealthOnline::Server::SweepDeferredFrames(
			queue.Entries().front(),
			1.0,
			[](std::uint64_t) { return false; },
			[](std::uint64_t, std::uint64_t a_through) { return a_through == 7; });
		Require(sweep.delivered.empty());
		Require(sweep.abandoned.size() == 2);
		Require(queue.Entries().front().frames.empty());

		CommonwealthOnline::Server::DeferredFrameQueue durableButQuarantined;
		durableButQuarantined.Park(6, CommonwealthOnline::Server::OutboundFrame{ .connectionId = 6, .payload = { 3 } }, false, 7, 100.0);
		auto quarantined = CommonwealthOnline::Server::SweepDeferredFrames(
			durableButQuarantined.Entries().front(), 1.0,
			[](std::uint64_t) { return true; },
			[](std::uint64_t, std::uint64_t a_through) { return a_through == 7; });
		Require(quarantined.delivered.empty());
		Require(quarantined.abandoned.size() == 1);
		Require(durableButQuarantined.Entries().front().frames.empty());
	}

	void AQuarantinedTicketIsNotDeliveredAfterLaterCommit()
	{
		CommonwealthOnline::Server::DeferredFrameQueue queue;
		queue.Park(5, CommonwealthOnline::Server::OutboundFrame{ .connectionId = 5, .payload = { 1 } }, false, 7, 100.0, 6);
		auto sweep = CommonwealthOnline::Server::SweepDeferredFrames(
			queue.Entries().front(), 1.0,
			[](std::uint64_t) { return true; },
			[](std::uint64_t a_before, std::uint64_t a_through) { return a_before == 6 && a_through == 7; });
		Require(sweep.delivered.empty());
		Require(sweep.abandoned.size() == 1);
	}

	void ABadSectionDoesNotStopLaterSaves()
	{
		const auto directory = MakeDirectory("co-durable-pipeline-quarantine");
		const auto path = directory / "state.json";
		std::atomic<double> now{ 1000.0 };
		DurableWritePipeline pipeline(path, [&] { return now.load(); });
		pipeline.PersistDocument(NewDocument());

		const auto badTicket = pipeline.Post({ Delta("quests", nlohmann::json{ { "text", std::string{ 1, '\xff' } } }) }, true);
		Require(badTicket > 0);
		Require(pipeline.TicketQuarantined(badTicket));

		const auto goodTicket = pipeline.Post({ Delta("factions", nlohmann::json{ { "version", 21 } }) }, true);
		Require(pipeline.Durable(goodTicket));
		CommonwealthOnline::Server::DeferredFrameQueue replies;
		replies.Park(5, CommonwealthOnline::Server::OutboundFrame{ .connectionId = 5, .payload = { 1 } }, false, badTicket, 100.0);
		const auto sweep = CommonwealthOnline::Server::SweepDeferredFrames(replies.Entries().front(), 1.0,
			[&](std::uint64_t ticket) { return pipeline.Durable(ticket); },
			[&](std::uint64_t before, std::uint64_t through) { return pipeline.QuarantinedSince(before, through); });
		Require(sweep.delivered.empty() && sweep.abandoned.size() == 1);
		const auto document = ReadDocument(path);
		Require(document.at("factions").at("version") == 21);
		Require(!document.contains("quests"));
	}

	void AStoreRefusesABadAwaitedSave()
	{
		const auto directory = MakeDirectory("co-durable-pipeline-awaited-refusal");
		const auto path = directory / "state.json";
		std::atomic<double> now{ 1000.0 };
		const auto instanceId = []
		{
			return std::string{ "0123456789abcdef0123456789abcdef" };
		};
		DurableStateFileStore store(path, [&] { return now.load(); }, instanceId);
		store.SaveQuests(nlohmann::json{ { "stages", 3 } });
		bool threw = false;
		try {
			store.SaveQuests(nlohmann::json{ { "text", std::string{ 1, '\xff' } } });
		} catch (const std::exception&) {
			threw = true;
		}
		Require(threw);
		Require(store.GetQuests().at("stages") == 3);
		store.SaveQuests(nlohmann::json{ { "stages", 4 } });
		store.WaitForCommits();
		Require(ReadDocument(path).at("quests").at("stages") == 4);
	}

	void AGoodTicketIsSavedWhenAnotherTicketIsQuarantined()
	{
		const auto directory = MakeDirectory("co-durable-pipeline-mixed-batch");
		const auto path = directory / "state.json";
		std::atomic<double> now{ 1000.0 };
		DurableWritePipeline pipeline(path, [&] { return now.load(); });
		pipeline.PersistDocument(NewDocument());

		const auto badTicket = pipeline.Post({ Delta("quests", nlohmann::json{ { "text", std::string{ 1, '\xff' } } }) }, false);
		const auto goodTicket = pipeline.Post({ Delta("factions", nlohmann::json{ { "version", 33 } }) }, false);
		pipeline.WaitForCommits();

		Require(pipeline.TicketQuarantined(badTicket));
		Require(!pipeline.QuarantinedSince(badTicket, goodTicket));
		Require(pipeline.Durable(goodTicket));
		const auto document = ReadDocument(path);
		Require(document.at("factions").at("version") == 33);
		Require(!document.contains("quests"));
	}

	void ADeferredStoreRefusesTheSameBadSectionTwice()
	{
		const auto directory = MakeDirectory("co-durable-pipeline-deferred-bad-section");
		const auto path = directory / "state.json";
		std::atomic<double> now{ 1000.0 };
		const auto instanceId = []
		{
			return std::string{ "0123456789abcdef0123456789abcdef" };
		};
		DurableStateFileStore store(path, [&] { return now.load(); }, instanceId);
		store.SetDeferCommits(true);
		store.SaveQuests(nlohmann::json{ { "stages", 0 } });
		store.WaitForCommits();
		Require(ReadDocument(path).at("quests").at("stages") == 0);

		Require(!store.SaveQuests(nlohmann::json{ { "text", std::string{ 1, '\xff' } } }));
		Require(store.GetQuests().at("stages") == 0);
		const auto badTicket = store.LastPostedTicket();
		Require(store.TicketQuarantined(badTicket));

		Require(store.SaveQuests(nlohmann::json{ { "stages", 5 } }));
		store.WaitForCommits();
		Require(ReadDocument(path).at("quests").at("stages") == 5);

		store.SaveQuests(nlohmann::json{ { "text", std::string{ 1, '\xff' } } });
		store.SaveFactions(nlohmann::json{ { "version", 1 } });
		store.WaitForCommits();
		Require(ReadDocument(path).at("factions").at("version") == 1);
	}
	void ARefusedDeferredSavesReportFailure()
	{
		const auto directory = MakeDirectory("co-durable-pipeline-refused-saves");
		const auto path = directory / "state.json";
		DurableStateFileStore store(path, [] { return 1000.0; }, [] { return std::string{ "0123456789abcdef0123456789abcdef" }; });
		store.SetDeferCommits(true);
		const auto bad = nlohmann::json{ { "text", std::string{ 1, '\xff' } } };
		Require(!store.SaveFactions(bad));
		Require(!store.SaveKnownPeople(bad));
		Require(!store.SaveSettlementAccess(bad));
		Require(!store.SaveWorkshop(bad, true));
		Require(store.GetFactions() != bad);
		Require(store.SaveFactions(nlohmann::json{ { "version", 1 } }));
		store.WaitForCommits();
		Require(ReadDocument(path).at("factions").at("version") == 1);
	}
	void AReopenedStoreKeepsTheLoadedDocument()
	{
		const auto directory = MakeDirectory("co-durable-pipeline-reopen");
		const auto path = directory / "state.json";
		std::atomic<double> now{ 1000.0 };
		const auto instanceId = []
		{
			return std::string{ "0123456789abcdef0123456789abcdef" };
		};
		{
			DurableStateFileStore store(path, [&] { return now.load(); }, instanceId);
			store.SaveFactions(nlohmann::json{ { "version", 1 } });
			store.WaitForCommits();
		}
		{
			now = 2000.0;
			DurableStateFileStore store(path, [&] { return now.load(); }, instanceId);
			store.SaveFactions(nlohmann::json{ { "version", 2 } });
			store.WaitForCommits();
			Require(store.ServerInstanceId() == instanceId());
		}
		const auto document = ReadDocument(path);
		Require(document.at("serverInstanceId") == instanceId());
		Require(document.at("inventoryAuthorityBlocked") == false);
		Require(document.at("factions").at("version") == 2);
	}

	void ABlockedWriterCoalescesTwoPostsIntoOneCommit()
	{
		const auto directory = MakeDirectory("co-durable-pipeline-coalesce");
		const auto path = directory / "state.json";
		std::atomic<double> now{ 1000.0 };
		std::mutex gateMutex;
		std::condition_variable gate;
		bool entered = false;
		bool released = false;
		DurableWritePipeline pipeline(path, [&] { return now.load(); }, {}, [&] {
			std::unique_lock lock(gateMutex);
			entered = true;
			gate.notify_all();
			gate.wait(lock, [&] { return released; });
		});
		pipeline.PersistDocument(NewDocument());
		const auto commitsBefore = pipeline.CommitCount();
		const auto first = pipeline.Post({ Delta("factions", nlohmann::json{ { "version", 11 } }) }, false);
		{
			std::unique_lock lock(gateMutex);
			gate.wait(lock, [&] { return entered; });
		}
		const auto second = pipeline.Post({ Delta("quests", nlohmann::json{ { "version", 12 } }) }, false);
		const auto third = pipeline.Post({ Delta("workshop", nlohmann::json{ { "version", 13 } }) }, false);
		{
			const std::scoped_lock lock(gateMutex);
			released = true;
		}
		gate.notify_all();
		pipeline.WaitForCommits();

		const auto document = ReadDocument(path);
		Require(document.at("factions").at("version") == 11);
		Require(document.at("quests").at("version") == 12);
		Require(document.at("workshop").at("version") == 13);
		Require(first < second && second < third);
		Require(pipeline.CommitCount() - commitsBefore < 3);
		Require(pipeline.Durable(third));
	}

	void ANullSectionIsWrittenAndAnAbsentOneIsRemoved()
	{
		const auto directory = MakeDirectory("co-durable-pipeline-null");
		const auto path = directory / "state.json";
		std::atomic<double> now{ 1000.0 };
		DurableWritePipeline pipeline(path, [&] { return now.load(); });
		pipeline.PersistDocument(NewDocument());

		pipeline.Post({ Delta("quests", nlohmann::json(nullptr)) }, true);
		auto document = ReadDocument(path);
		Require(document.contains("quests"));
		Require(document.at("quests").is_null());

		pipeline.Post({ CommonwealthOnline::Server::DurableSectionDelta{ .name = "quests", .value = std::nullopt } }, true);
		document = ReadDocument(path);
		Require(!document.contains("quests"));
	}

	void ACommitThatCannotBeWrittenReachedTheCaller()
	{
		const auto directory = MakeDirectory("co-durable-pipeline-failure");
		const auto path = directory / "state.json";
		std::atomic<double> now{ 1000.0 };
		DurableWritePipeline pipeline(path, [&] { return now.load(); });
		pipeline.PersistDocument(NewDocument());
		std::filesystem::remove_all(directory);

		bool threw = false;
		try {
			pipeline.Post({ Delta("factions", nlohmann::json{ { "version", 9 } }) }, true);
		} catch (const std::exception&) {
			threw = true;
		}
		Require(threw);
		const auto ticket = pipeline.LastPostedTicket();
		Require(ticket > 0);
		Require(pipeline.LastWriteFailed());
		Require(!pipeline.Durable(ticket));
		Require(pipeline.TicketFailed(ticket));
	}

	nlohmann::json GoodProfile(const char* a_name)
	{
		return nlohmann::json{ { "playerState", nlohmann::json{ { "characterName", a_name } } } };
	}

	nlohmann::json InvalidProfile()
	{
		return nlohmann::json{ { "profileSecret", "not-hex" } };
	}

	void AnInvalidProfileInAWholeSectionWriteDoesNotBlockTheOthers()
	{
		const auto directory = MakeDirectory("co-durable-pipeline-invalid-profile-whole");
		const auto path = directory / "state.json";
		std::atomic<double> now{ 1000.0 };
		DurableWritePipeline pipeline(path, [&] { return now.load(); });
		pipeline.PersistDocument(NewDocument());

		pipeline.Post({ Delta("profiles", nlohmann::json{ { "keeper", GoodProfile("Kept") }, { "victim", GoodProfile("Before") } }) }, true);
		const auto ticket = pipeline.Post({ Delta("profiles", nlohmann::json{
			{ "keeper", GoodProfile("Renamed") }, { "victim", InvalidProfile() }, { "newcomer", GoodProfile("New") } }) }, true);
		Require(pipeline.Durable(ticket));
		Require(!pipeline.TicketQuarantined(ticket));
		auto document = ReadDocument(path);
		Require(document.at("profiles").at("keeper").at("playerState").at("characterName") == "Renamed");
		Require(document.at("profiles").at("newcomer").at("playerState").at("characterName") == "New");
		Require(document.at("profiles").at("victim").at("playerState").at("characterName") == "Before");

		const auto again = pipeline.Post({ Delta("profiles", nlohmann::json{
			{ "keeper", GoodProfile("Again") }, { "victim", InvalidProfile() }, { "newcomer", GoodProfile("New") } }) }, true);
		Require(pipeline.Durable(again));
		document = ReadDocument(path);
		Require(document.at("profiles").at("keeper").at("playerState").at("characterName") == "Again");
		Require(document.at("profiles").at("victim").at("playerState").at("characterName") == "Before");
	}

	void AnInvalidProfileMemberWriteIsQuarantinedAlone()
	{
		const auto directory = MakeDirectory("co-durable-pipeline-invalid-profile-member");
		const auto path = directory / "state.json";
		std::atomic<double> now{ 1000.0 };
		DurableWritePipeline pipeline(path, [&] { return now.load(); });
		pipeline.PersistDocument(NewDocument());
		pipeline.Post({ Delta("profiles", nlohmann::json{ { "victim", GoodProfile("Before") } }) }, true);

		const auto member = [](const char* a_id, nlohmann::json a_value) {
			return DurableSectionDelta{ .name = "profiles", .value = std::optional<nlohmann::json>{ std::move(a_value) }, .key = std::string{ a_id } };
		};
		const auto badTicket = pipeline.Post({ member("victim", InvalidProfile()) }, false);
		const auto goodTicket = pipeline.Post({ member("bystander", GoodProfile("Saved")) }, false);
		pipeline.WaitForCommits();

		Require(pipeline.TicketQuarantined(badTicket));
		Require(!pipeline.QuarantinedSince(badTicket, goodTicket));
		Require(pipeline.Durable(goodTicket));
		const auto document = ReadDocument(path);
		Require(document.at("profiles").at("bystander").at("playerState").at("characterName") == "Saved");
		Require(document.at("profiles").at("victim").at("playerState").at("characterName") == "Before");

		bool threw = false;
		try {
			pipeline.Post({ member("victim", InvalidProfile()) }, true);
		} catch (const std::exception&) {
			threw = true;
		}
		Require(threw);
		const auto afterwards = pipeline.Post({ member("bystander", GoodProfile("Saved again")) }, true);
		Require(pipeline.Durable(afterwards));
		Require(ReadDocument(path).at("profiles").at("bystander").at("playerState").at("characterName") == "Saved again");
	}

	void ADeferredStoreSaveReturnsWhileTheWriterIsBlocked()
	{
		const auto directory = MakeDirectory("co-durable-store-deferred-return");
		const auto path = directory / "state.json";
		std::atomic<double> now{ 1000.0 };
		std::mutex gateMutex;
		std::condition_variable gate;
		bool armed = false;
		bool entered = false;
		bool released = false;
		const auto instanceId = []
		{
			return std::string{ "0123456789abcdef0123456789abcdef" };
		};
		DurableStateFileStore store(path, [&] { return now.load(); }, instanceId, [&] {
			std::unique_lock lock(gateMutex);
			if (!armed) {
				return;
			}
			entered = true;
			gate.notify_all();
			gate.wait(lock, [&] { return released; });
		});
		{
			const std::scoped_lock lock(gateMutex);
			armed = true;
		}
		store.SetDeferCommits(true);

		std::mutex returnMutex;
		std::condition_variable returned;
		bool saved = false;
		std::thread saver([&] {
			store.SaveFactions(nlohmann::json{ { "version", 1 } });
			store.SaveQuests(nlohmann::json{ { "stages", 1 } }, true);
			const std::scoped_lock lock(returnMutex);
			saved = true;
			returned.notify_all();
		});
		{
			std::unique_lock lock(returnMutex);
			Require(returned.wait_for(lock, std::chrono::seconds(10), [&] { return saved; }));
		}
		{
			std::unique_lock lock(gateMutex);
			Require(gate.wait_for(lock, std::chrono::seconds(10), [&] { return entered; }));
		}
		{
			const std::scoped_lock lock(gateMutex);
			released = true;
		}
		gate.notify_all();
		saver.join();
		store.WaitForCommits();

		const auto document = ReadDocument(path);
		Require(document.at("factions").at("version") == 1);
		Require(document.at("quests").at("stages") == 1);
	}

	void AStoreKeepsANullQuestsSectionAcrossReopen()
	{
		const auto directory = MakeDirectory("co-durable-store-null-quests");
		const auto path = directory / "state.json";
		std::atomic<double> now{ 1000.0 };
		const auto instanceId = []
		{
			return std::string{ "0123456789abcdef0123456789abcdef" };
		};
		{
			DurableStateFileStore store(path, [&] { return now.load(); }, instanceId);
			store.SaveQuests(nlohmann::json(nullptr));
			store.WaitForCommits();
			Require(store.GetQuests().is_null());
		}
		const auto document = ReadDocument(path);
		Require(document.contains("quests"));
		Require(document.at("quests").is_null());

		now = 2000.0;
		DurableStateFileStore reopened(path, [&] { return now.load(); }, instanceId);
		Require(reopened.GetQuests().is_null());
	}

	void AReopenWithoutChangesLeavesTheFilesUntouched()
	{
		const auto directory = MakeDirectory("co-durable-store-reopen-untouched");
		const auto path = directory / "state.json";
		auto backupPath = path;
		backupPath += ".bak";
		std::atomic<double> now{ 1000.0 };
		const auto instanceId = []
		{
			return std::string{ "0123456789abcdef0123456789abcdef" };
		};
		{
			DurableStateFileStore store(path, [&] { return now.load(); }, instanceId);
			store.SaveFactions(nlohmann::json{ { "version", 1 } });
			store.WaitForCommits();
		}
		const auto primaryBefore = ReadDocument(path);
		const auto primaryWrittenBefore = std::filesystem::last_write_time(path);
		const auto backupBefore = ReadDocument(backupPath);
		const auto backupWrittenBefore = std::filesystem::last_write_time(backupPath);

		now = 2000.0;
		{
			DurableStateFileStore store(path, [&] { return now.load(); }, instanceId);
			Require(store.GetFactions().at("version") == 1);
		}
		Require(ReadDocument(path) == primaryBefore);
		Require(std::filesystem::last_write_time(path) == primaryWrittenBefore);
		Require(ReadDocument(backupPath) == backupBefore);
		Require(std::filesystem::last_write_time(backupPath) == backupWrittenBefore);
	}
}

int main()
{
	AnAwaitedSectionIsDurableWhenTheCallReturns();
	CoalescedPostsCarryEveryChangedSection();
	DrainCommitsEveryPendingSection();
	ATicketIsDurableOnceItsCommitLands();
	HeldFramesKeepTheirOrderAndTheirOwnTickets();
	AFrameBehindAHeldOneWaitsForItsHead();
	AnExpiredHoldIsAbandonedWithEverythingBehindIt();
	AQuarantinedSectionAbandonsItsRepliesImmediately();
	AQuarantinedTicketIsNotDeliveredAfterLaterCommit();
	ABadSectionDoesNotStopLaterSaves();
	AStoreRefusesABadAwaitedSave();
	AGoodTicketIsSavedWhenAnotherTicketIsQuarantined();
	ADeferredStoreRefusesTheSameBadSectionTwice();
	ARefusedDeferredSavesReportFailure();
	AReopenedStoreKeepsTheLoadedDocument();
	ABlockedWriterCoalescesTwoPostsIntoOneCommit();
	ANullSectionIsWrittenAndAnAbsentOneIsRemoved();
	ACommitThatCannotBeWrittenReachedTheCaller();
	AnInvalidProfileInAWholeSectionWriteDoesNotBlockTheOthers();
	AnInvalidProfileMemberWriteIsQuarantinedAlone();
	ADeferredStoreSaveReturnsWhileTheWriterIsBlocked();
	AStoreKeepsANullQuestsSectionAcrossReopen();
	AReopenWithoutChangesLeavesTheFilesUntouched();
	return 0;
}
