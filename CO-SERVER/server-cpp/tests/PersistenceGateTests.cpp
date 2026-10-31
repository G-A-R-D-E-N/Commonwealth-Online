#include "PersistenceGate.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::Delivery;
	using CommonwealthOnline::Server::DurableCommitWriter;
	using CommonwealthOnline::Server::DurableStateFileStore;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::PersistenceGate;

	std::vector<std::uint8_t> Encode(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	struct BlockingCommit
	{
		std::mutex mutex;
		std::condition_variable changed;
		bool committing{};
		bool release{};
		std::vector<std::string> written;

		void Run(const std::string& a_contents)
		{
			std::unique_lock lock(mutex);
			committing = true;
			changed.notify_all();
			changed.wait_for(lock, std::chrono::seconds(5), [&] { return release; });
			written.push_back(a_contents);
		}

		void WaitForCommitting()
		{
			std::unique_lock lock(mutex);
			assert(changed.wait_for(lock, std::chrono::seconds(5), [&] { return committing; }));
		}

		void Release()
		{
			{
				const std::scoped_lock lock(mutex);
				release = true;
			}
			changed.notify_all();
		}

		std::vector<std::string> Written()
		{
			const std::scoped_lock lock(mutex);
			return written;
		}
	};

	void WriterPostReturnsBeforeCommitRuns()
	{
		BlockingCommit commit;
		DurableCommitWriter writer([&](const std::string& a_contents) { commit.Run(a_contents); });
		writer.Post("first");
		assert(commit.Written().empty());
		commit.WaitForCommitting();
		assert(commit.Written().empty());
		commit.Release();
		writer.Drain();
		assert((commit.Written() == std::vector<std::string>{ "first" }));
	}

	void WriterCoalescesToNewest()
	{
		BlockingCommit commit;
		DurableCommitWriter writer([&](const std::string& a_contents) { commit.Run(a_contents); });
		writer.Post("first");
		commit.WaitForCommitting();
		writer.Post("second");
		writer.Post("third");
		writer.Post("fourth");
		commit.Release();
		writer.Drain();
		assert((commit.Written() == std::vector<std::string>{ "first", "fourth" }));
	}

	void WriterDestructorWritesLastPost()
	{
		std::mutex mutex;
		std::vector<std::string> written;
		{
			DurableCommitWriter writer([&](const std::string& a_contents) {
				const std::scoped_lock lock(mutex);
				written.push_back(a_contents);
			});
			writer.Post("first");
			writer.Post("last");
		}
		assert(!written.empty() && written.back() == "last");
	}

	void WriterReportsFailures()
	{
		DurableCommitWriter writer([](const std::string& a_contents) {
			if (a_contents == "bad") {
				throw std::runtime_error("disk full");
			}
		});
		bool threw = false;
		try {
			writer.PostAndWait("bad");
		} catch (const std::runtime_error& exception) {
			threw = std::string{ exception.what() } == "disk full";
		}
		assert(threw);
		assert(writer.LastWriteFailed());
		assert(!writer.TakeFailure().has_value());

		writer.Post("bad");
		writer.Drain();
		assert(writer.LastWriteFailed());
		const auto failure = writer.TakeFailure();
		assert(failure.has_value() && *failure == "disk full");
		assert(!writer.TakeFailure().has_value());

		writer.PostAndWait("good");
		assert(!writer.LastWriteFailed());
	}

	void WriterPostAndWaitSupersedesPending()
	{
		BlockingCommit commit;
		DurableCommitWriter writer([&](const std::string& a_contents) { commit.Run(a_contents); });
		writer.Post("first");
		commit.WaitForCommitting();
		writer.Post("second");
		std::thread waiter([&] { writer.PostAndWait("third"); });
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		commit.Release();
		waiter.join();
		writer.Drain();
		assert((commit.Written() == std::vector<std::string>{ "first", "third" }));
	}

	void WriterWaiterIgnoresEarlierInFlightFailure()
	{
		std::mutex mutex;
		std::condition_variable changed;
		bool entered = false;
		bool release = false;
		DurableCommitWriter writer([&](const std::string& a_contents) {
			if (a_contents != "bad") {
				return;
			}
			std::unique_lock lock(mutex);
			entered = true;
			changed.notify_all();
			changed.wait(lock, [&] { return release; });
			throw std::runtime_error("disk full");
		});
		writer.Post("bad");
		{
			std::unique_lock lock(mutex);
			changed.wait(lock, [&] { return entered; });
		}
		bool threw = false;
		std::thread waiter([&] {
			try {
				writer.PostAndWait("good");
			} catch (const std::exception&) {
				threw = true;
			}
		});
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		{
			const std::scoped_lock lock(mutex);
			release = true;
		}
		changed.notify_all();
		waiter.join();
		assert(!threw);
		assert(!writer.LastWriteFailed());
		const auto failure = writer.TakeFailure();
		assert(failure.has_value() && *failure == "disk full");
		assert(!writer.TakeFailure().has_value());
	}

	void WriterThrowingCommitDoesNotHangDestructor()
	{
		{
			DurableCommitWriter writer([](const std::string&) { throw std::runtime_error("disk full"); });
			writer.Post("bad");
		}
		{
			DurableCommitWriter writer([](const std::string&) { throw std::runtime_error("disk full"); });
			bool threw = false;
			try {
				writer.PostAndWait("bad");
			} catch (const std::runtime_error&) {
				threw = true;
			}
			assert(threw);
		}
	}

	struct HeldPostCommit
	{
		std::mutex mutex;
		std::condition_variable changed;
		bool armed{};
		bool entered{};
		bool release{};

		void Hook()
		{
			std::unique_lock lock(mutex);
			if (!armed || entered) {
				return;
			}
			entered = true;
			changed.notify_all();
			changed.wait(lock, [&] { return release; });
		}

		void Arm()
		{
			const std::scoped_lock lock(mutex);
			armed = true;
		}

		void WaitEntered()
		{
			std::unique_lock lock(mutex);
			changed.wait(lock, [&] { return entered; });
		}

		void Release()
		{
			{
				const std::scoped_lock lock(mutex);
				release = true;
			}
			changed.notify_all();
		}
	};

	using PlayerStates = std::unordered_map<std::string, Json>;

	PlayerStates OnePlayer(const std::string& a_profileId, const std::string& a_name)
	{
		return PlayerStates{ { a_profileId, Json{ { "type", "playerState" }, { "characterName", a_name } } } };
	}

	F4T::Identity::TokenHash TestHash(std::uint8_t a_seed)
	{
		F4T::Identity::TokenHash hash{};
		hash[0] = a_seed;
		return hash;
	}

	std::string TestInstanceId()
	{
		return "0123456789abcdef0123456789abcdef";
	}

	void HeldWriterKeepsCredentialAndPlayerState(const std::filesystem::path& a_directory)
	{
		const auto path = a_directory / "held-state.json";
		const auto clock = [] { return 3000.0; };
		HeldPostCommit hold;
		{
			DurableStateFileStore store(path, clock, [] { return TestInstanceId(); }, [&] { hold.Hook(); });
			hold.Arm();
			(void)store.UpdatePlayerStates(OnePlayer("profile-held", "Held"));
			hold.WaitEntered();
			std::thread releaser([&] {
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
				hold.Release();
			});
			store.SaveResumeCredential("profile-held", TestHash(9), std::nullopt, true, 9000.0);
			releaser.join();
		}
		DurableStateFileStore reopened(path, clock, [] { return TestInstanceId(); });
		assert(reopened.GetPlayerState("profile-held").has_value());
		assert(reopened.GetPlayerState("profile-held")->at("characterName") == "Held");
		assert(reopened.GetResumeCredential("profile-held").has_value());
	}

	void FailedCredentialSaveRollsBackWithoutLosingPlayerState(const std::filesystem::path& a_directory)
	{
		const auto path = a_directory / "rollback-state.json";
		const auto backup = std::filesystem::path{ path.string() + ".bak" };
		const auto clock = [] { return 4000.0; };
		{
			DurableStateFileStore store(path, clock, [] { return TestInstanceId(); });
			std::filesystem::create_directories(backup / "occupied");
			(void)store.UpdatePlayerStates(OnePlayer("profile-rollback", "Async"));
			store.WaitForCommits();
			assert(store.TakePersistenceWarning().has_value());

			bool threw = false;
			try {
				store.SaveResumeCredential("profile-rollback", TestHash(7), std::nullopt, true, 9000.0);
			} catch (const std::exception&) {
				threw = true;
			}
			assert(threw);
			assert(!store.GetResumeCredential("profile-rollback").has_value());
			assert(store.GetPlayerState("profile-rollback")->at("characterName") == "Async");

			std::filesystem::remove_all(backup);
			assert(store.UpdatePlayerStates(PlayerStates{}));
			store.WaitForCommits();
			assert(!store.TakePersistenceWarning().has_value());
		}
		DurableStateFileStore reopened(path, clock, [] { return TestInstanceId(); });
		assert(reopened.GetPlayerState("profile-rollback")->at("characterName") == "Async");
		assert(!reopened.GetResumeCredential("profile-rollback").has_value());
	}

	void EmptyFlushRepostsAfterFailedWrite(const std::filesystem::path& a_directory)
	{
		const auto path = a_directory / "retry-state.json";
		const auto backup = std::filesystem::path{ path.string() + ".bak" };
		const auto clock = [] { return 5000.0; };
		{
			DurableStateFileStore store(path, clock, [] { return TestInstanceId(); });
			PersistenceGate gate(store);
			assert(gate.SessionReadyDelivered(OutboundFrame{
				4,
				Delivery::ReliableOrdered,
				Encode(Json{ { "type", "sessionReady" }, { "playerId", 6 }, { "profileId", "profile-retry" } })
			}, 5000.0).has_value());
			std::filesystem::create_directories(backup / "occupied");
			assert(gate.QueueAcceptedPlayerState(4, Json{ { "type", "playerState" }, { "characterName", "Retry" } }));
			(void)gate.FlushPendingPlayerStates();
			store.WaitForCommits();
			assert(store.TakePersistenceWarning().has_value());

			std::filesystem::remove_all(backup);
			assert(gate.FlushPendingPlayerStates());
			store.WaitForCommits();
			assert(!store.TakePersistenceWarning().has_value());
		}
		DurableStateFileStore reopened(path, clock, [] { return TestInstanceId(); });
		assert(reopened.GetPlayerState("profile-retry")->at("characterName") == "Retry");
	}

	void ShutdownFlushPersistsPendingPlayerState(const std::filesystem::path& a_directory)
	{
		const auto path = a_directory / "shutdown-state.json";
		const auto clock = [] { return 2000.0; };
		HeldPostCommit hold;
		{
			DurableStateFileStore store(path, clock, [] { return TestInstanceId(); }, [&] { hold.Hook(); });
			PersistenceGate gate(store);
			assert(gate.SessionReadyDelivered(OutboundFrame{
				3,
				Delivery::ReliableOrdered,
				Encode(Json{ { "type", "sessionReady" }, { "playerId", 5 }, { "profileId", "profile-shutdown" } })
			}, 2000.0).has_value());
			hold.Arm();
			assert(gate.QueueAcceptedPlayerState(3, Json{ { "type", "playerState" }, { "characterName", "First" } }));
			assert(gate.FlushPendingPlayerStates());
			hold.WaitEntered();
			assert(gate.QueueAcceptedPlayerState(3, Json{ { "type", "playerState" }, { "characterName", "Last" } }));
			std::thread releaser([&] {
				std::this_thread::sleep_for(std::chrono::seconds(1));
				hold.Release();
			});
			assert(gate.FlushPendingPlayerStates(true));
			std::ifstream input(path);
			const auto disk = Json::parse(input);
			const auto persisted = F4T::DurableState::GetPlayerState(disk, "profile-shutdown");
			assert(persisted.has_value());
			assert(persisted->at("characterName") == "Last");
			releaser.join();
		}
		DurableStateFileStore reopened(path, clock, [] { return TestInstanceId(); });
		assert(reopened.GetPlayerState("profile-shutdown")->at("characterName") == "Last");
	}

	void BlockedAuthorityKeepsJoinsReady(const std::filesystem::path& a_directory)
	{
		const auto path = a_directory / "blocked-join.json";
		const auto clock = [] { return 4000.0; };
		const auto catalog = Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5 } } });
		{
			DurableStateFileStore store(path, clock, [] { return TestInstanceId(); });
			store.SaveResumeCredential("profile-granted", TestHash(3), std::nullopt, true, 9000.0);
			(void)store.InitializeInventory("profile-granted", catalog);
			store.SaveResumeCredential("profile-new", TestHash(4), std::nullopt, true, 9000.0);
			store.UpdateServerWorldState(Json{ { "seed", 1 } });
		}
		{
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output << "corrupt";
		}
		DurableStateFileStore recovered(path, clock, [] { return TestInstanceId(); });
		assert(recovered.RecoveryNotice().has_value() && recovered.InventoryAuthorityBlocked());
		PersistenceGate gate(recovered, catalog);
		const auto sessionReady = [](std::uint32_t a_playerId, const char* a_profileId) {
			return OutboundFrame{ 7, Delivery::ReliableOrdered, Encode(Json{ { "type", "sessionReady" }, { "playerId", a_playerId },
				{ "profileId", a_profileId }, { "resumeToken", "opaque" } }) };
		};
		std::string inventoryError;
		auto granted = sessionReady(41, "profile-granted");
		assert(gate.PrepareSessionReady(granted, inventoryError));
		const auto grantedHeader = Json::parse(granted.payload.begin(), granted.payload.end()).at("serverInventory");
		assert(grantedHeader.at("initialized") == true);
		assert(grantedHeader.at("revision") == recovered.GetInventory("profile-granted")->at("revision"));
		assert(grantedHeader.at("itemCount") == 1u);
		assert(!grantedHeader.contains("items"));
		const auto grantedTransfer = gate.TakeInventoryTransferFrames(7);
		assert(grantedTransfer.size() == grantedHeader.at("chunkCount").get<std::size_t>());
		assert(!grantedTransfer.empty());
		const auto grantedChunk = Json::parse(grantedTransfer.front().payload.begin(), grantedTransfer.front().payload.end());
		assert(grantedChunk.at("type") == "inventoryTransfer");
		assert(grantedChunk.at("items") == recovered.GetInventory("profile-granted")->at("items"));
		auto newcomer = sessionReady(42, "profile-new");
		assert(gate.PrepareSessionReady(newcomer, inventoryError));
		assert(!Json::parse(newcomer.payload.begin(), newcomer.payload.end()).contains("serverInventory"));
		assert(!recovered.GetInventory("profile-new"));
	}
}

int main()
{
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto directory = std::filesystem::temp_directory_path() / ("co-persistence-gate-test-" + std::to_string(stamp));
	std::filesystem::create_directories(directory);
	double now = 1000.0;
	std::size_t clockCalls = 0;

	try {
		WriterPostReturnsBeforeCommitRuns();
		WriterCoalescesToNewest();
		WriterDestructorWritesLastPost();
		WriterReportsFailures();
		WriterPostAndWaitSupersedesPending();
		WriterWaiterIgnoresEarlierInFlightFailure();
		WriterThrowingCommitDoesNotHangDestructor();
		HeldWriterKeepsCredentialAndPlayerState(directory);
		FailedCredentialSaveRollsBackWithoutLosingPlayerState(directory);
		EmptyFlushRepostsAfterFailedWrite(directory);
		ShutdownFlushPersistsPendingPlayerState(directory);
		BlockedAuthorityKeepsJoinsReady(directory);

		DurableStateFileStore store(
			directory / "durable-state.json",
			[&] { ++clockCalls; return now; },
			[] { return "0123456789abcdef0123456789abcdef"; });
		store.UpdateServerWorldState(Json{
			{ "timeHHmm", "1800" },
			{ "weatherConsoleArg", "001ca7e4" },
			{ "weatherFormId", "001CA7E4" }
		});

		PersistenceGate gate(store);
		const auto playerStateBytes = Encode(Json{
			{ "type", "playerState" },
			{ "characterName", "Nomad" }
		});
		assert(!gate.CapturePlayerStateCandidate(7, playerStateBytes).has_value());
		assert(!gate.ProfileForConnection(7).has_value());

		OutboundFrame incompleteReady{
			7,
			Delivery::ReliableOrdered,
			Encode(Json{ { "type", "sessionReady" }, { "playerId", 42 } })
		};
		assert(!gate.SessionReadyDelivered(incompleteReady, 1001.0).has_value());

		OutboundFrame ready{
			7,
			Delivery::ReliableOrdered,
			Encode(Json{
				{ "type", "sessionReady" },
				{ "playerId", 42 },
				{ "profileId", "profile-001" },
				{ "resumeToken", "opaque" }
			})
		};
		F4T::Identity::TokenHash hash{};
		store.SaveResumeCredential("profile-001", hash, std::nullopt, false, 5000.0);
		const auto catalog = Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5 } } });
		PersistenceGate inventoryGate(store, catalog);
		std::string inventoryError;
		assert(!inventoryGate.PrepareSessionReady(incompleteReady, inventoryError));
		assert(!inventoryError.empty());
		assert(!store.GetInventory("profile-001"));
		auto oversizedReady = ready;
		auto oversizedPacket = Json::parse(ready.payload.begin(), ready.payload.end());
		oversizedPacket["metadata"] = std::string(64 * 1024, 'x');
		oversizedReady.payload = Encode(oversizedPacket);
		assert(!inventoryGate.PrepareSessionReady(oversizedReady, inventoryError));
		assert(inventoryError.find("readiness limit") != std::string::npos);
		assert(!store.GetInventory("profile-001"));
		assert(inventoryGate.PrepareSessionReady(ready, inventoryError));
		const auto enriched = Json::parse(ready.payload.begin(), ready.payload.end());
		const auto& inventoryHeader = enriched.at("serverInventory");
		assert(inventoryHeader.at("initialized") == true);
		assert(inventoryHeader.at("revision") == 0u);
		assert(inventoryHeader.at("itemCount") == 1u);
		assert(!inventoryHeader.contains("items"));
		const auto transfer = inventoryGate.TakeInventoryTransferFrames(7);
		assert(transfer.size() == inventoryHeader.at("chunkCount").get<std::size_t>());
		const auto transferPacket = Json::parse(transfer.front().payload.begin(), transfer.front().payload.end());
		assert(transferPacket.at("type") == "inventoryTransfer");
		assert(transferPacket.at("items").at("test.esm:00000123").at("count") == 5);
		assert(!inventoryGate.ProfileForConnection(7));
		assert(inventoryGate.PrepareSessionReady(ready, inventoryError));
		const auto firstPlan = gate.SessionReadyDelivered(ready, 1001.0);
		assert(firstPlan.has_value());
		assert(firstPlan->profileId == "profile-001");
		assert(firstPlan->playerId == 42);
		assert(!firstPlan->playerState.has_value());
		assert(firstPlan->worldPackets.size() == 2);
		assert(firstPlan->worldPackets[0].at("type") == "serverWorldState");
		assert(firstPlan->worldPackets[0].at("timeHHmm") == "1800");
		assert(!firstPlan->worldPackets[1].contains("weatherConsoleArg"));
		assert(firstPlan->worldPackets[1].at("weatherFormId") == "001CA7E4");
		assert(gate.ProfileForConnection(7).value() == "profile-001");

		const auto candidate = gate.CapturePlayerStateCandidate(7, playerStateBytes);
		assert(candidate.has_value());
		now = 1010.0;
		assert(gate.QueueAcceptedPlayerState(7, *candidate));
		assert(!store.GetPlayerState("profile-001").has_value());
		assert(gate.QueueAcceptedPlayerState(7, Json{
			{ "type", "playerState" },
			{ "appearance", Json{ { "version", 7 } } }
		}));
		assert(gate.QueueAcceptedPlayerState(7, Json{
			{ "type", "playerState" },
			{ "characterName", "Nomad Prime" }
		}));

		const auto actionOnly = Encode(Json{
			{ "type", "playerState" },
			{ "actionEvents", Json::array({ Json{ { "sequence", 1 }, { "type", 3 }, { "eventName", "fireSingle" } } }) }
		});
		const auto actionCandidate = gate.CapturePlayerStateCandidate(7, actionOnly);
		assert(actionCandidate.has_value());
		assert(!gate.QueueAcceptedPlayerState(7, *actionCandidate));

		OutboundFrame secondReady{
			10,
			Delivery::ReliableOrdered,
			Encode(Json{
				{ "type", "sessionReady" },
				{ "playerId", 100 },
				{ "profileId", "profile-002" }
			})
		};
		assert(gate.SessionReadyDelivered(secondReady, 1010.5).has_value());
		assert(gate.QueueAcceptedPlayerState(10, Json{
			{ "type", "playerState" },
			{ "characterName", "Second Survivor" }
		}));

		gate.ConnectionClosed(7);
		OutboundFrame preFlushReconnect{
			8,
			Delivery::ReliableOrdered,
			Encode(Json{
				{ "type", "sessionReady" },
				{ "playerId", 88 },
				{ "profileId", "profile-001" }
			})
		};
		const auto pendingReplay = gate.SessionReadyDelivered(preFlushReconnect, 1011.0);
		assert(pendingReplay.has_value());
		assert(pendingReplay->playerState.has_value());
		assert(pendingReplay->playerState->at("characterName") == "Nomad Prime");
		assert(pendingReplay->playerState->at("appearance").at("version") == 7);

		const auto backupPath = std::filesystem::path{ (directory / "durable-state.json").string() + ".bak" };
		std::filesystem::remove(backupPath);
		std::filesystem::create_directories(backupPath / "occupied");
		bool flushFailed = false;
		try {
			(void)gate.FlushPendingPlayerStates();
		} catch (const std::exception&) {
			flushFailed = true;
		}
		assert(!flushFailed);
		store.WaitForCommits();
		const auto failedWarning = store.TakePersistenceWarning();
		assert(failedWarning.has_value());
		assert(!store.TakePersistenceWarning().has_value());
		const auto failedReplay = gate.SessionReadyDelivered(preFlushReconnect, 1011.5);
		assert(failedReplay.has_value());
		assert(failedReplay->playerState.has_value());
		assert(failedReplay->playerState->at("characterName") == "Nomad Prime");
		assert(failedReplay->playerState->at("appearance").at("version") == 7);
		std::filesystem::remove_all(backupPath);

		now = 1012.0;
		assert(gate.FlushPendingPlayerStates());
		store.WaitForCommits();
		assert(!store.TakePersistenceWarning().has_value());
		assert(!gate.FlushPendingPlayerStates());
		const auto stored = store.GetPlayerState("profile-001");
		assert(stored.has_value());
		assert(stored->at("characterName") == "Nomad Prime");
		assert(stored->at("appearance").at("version") == 7);
		const auto secondStored = store.GetPlayerState("profile-002");
		assert(secondStored.has_value());
		assert(secondStored->at("characterName") == "Second Survivor");

		clockCalls = 0;
		assert(gate.QueueAcceptedPlayerState(8, Json{
			{ "type", "playerState" },
			{ "characterName", "Nomad Prime" },
			{ "appearance", Json{ { "version", 7 } } }
		}));
		assert(gate.QueueAcceptedPlayerState(10, Json{
			{ "type", "playerState" },
			{ "characterName", "Second Survivor" }
		}));
		assert(!gate.FlushPendingPlayerStates());
		assert(!gate.FlushPendingPlayerStates());
		assert(clockCalls == 0);

		const auto other = Encode(Json{ { "type", "transform" }, { "x", 0 } });
		assert(!gate.CapturePlayerStateCandidate(8, other).has_value());

		gate.ConnectionClosed(8);
		assert(!gate.ProfileForConnection(8).has_value());
		assert(!gate.CapturePlayerStateCandidate(8, playerStateBytes).has_value());

		OutboundFrame resumedReady{
			9,
			Delivery::ReliableOrdered,
			Encode(Json{
				{ "type", "sessionReady" },
				{ "playerId", 99 },
				{ "profileId", "profile-001" }
			})
		};
		const auto resumed = gate.SessionReadyDelivered(resumedReady, 1020.0);
		assert(resumed.has_value());
		assert(resumed->playerState.has_value());
		assert(resumed->playerState->at("characterName") == "Nomad Prime");
	} catch (...) {
		std::error_code ignored;
		std::filesystem::remove_all(directory, ignored);
		throw;
	}

	std::filesystem::remove_all(directory);
	return 0;
}
