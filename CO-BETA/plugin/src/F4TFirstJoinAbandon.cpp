#include "pch.h"

#include "F4TFirstJoinAbandon.h"
#include "F4TFirstJoinAbandonDecision.h"

#include "F4TCharacterSlotsClient.h"
#include "F4TClientIdentity.h"
#include "F4TNetworking.h"
#include "F4TServerProfileStore.h"
#include "F4TUIClient.h"

#include <chrono>
#include <cstdint>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace F4T::FirstJoinAbandon
{
	namespace
	{
		using Outcome = F4T::SessionLaunch::LaunchOutcome;

		constexpr const char* kSaveRecoveryMessage = "Your first save needs recovery. Keep your saves and contact the server administrator before rejoining.";
		constexpr auto kReplyBudget = std::chrono::seconds{ 15 };

		struct Pending
		{
			std::string serverKey;
			std::string profile;
			std::uint32_t slot{};
			bool unscoped{};
			std::uint64_t generation{};
			std::chrono::steady_clock::time_point deadline{};
			F4T::SessionLaunch::JoinCompletion onComplete;
		};

		struct Reply
		{
			bool ok{};
			std::string reason;
		};

		std::optional<Pending> g_pending;
		std::mutex g_receiveMutex;
		bool g_serverSupportsAbandon = false;
		bool g_awaitingReply = false;
		std::optional<Reply> g_reply;

		constexpr const char* kConnectionEndedMessage = "The connection ended while the unfinished first character was being checked. Reconnect and join again.";

		std::optional<Reply> TakeReply()
		{
			const std::scoped_lock lock(g_receiveMutex);
			auto reply = std::move(g_reply);
			g_reply.reset();
			return reply;
		}

		void Complete(bool a_success, const std::string& a_reason)
		{
			{
				const std::scoped_lock lock(g_receiveMutex);
				g_awaitingReply = false;
				g_reply.reset();
			}
			if (!g_pending) return;
			auto completion = std::move(g_pending->onComplete);
			g_pending.reset();
			if (completion) completion(a_success, a_reason);
		}

		std::string FailureMessage(const std::string& a_reason, const Pending& a_pending)
		{
			if (a_pending.unscoped) {
				return std::format("Your interrupted first character could not be identified ({}) for profile {}. Keep your saves and ask the server administrator about legacy first-join recovery.", a_reason, a_pending.profile);
			}
			if (a_reason == "slotCommitted") {
				return std::format("This character has starting inventory or progress on the server, so it cannot be discarded here. Keep your saves and contact the server administrator to run first-join-abandon for {} (confirm discard-first-join-without-save if you never got a save for it).", a_pending.profile);
			}
			if (a_reason == "slotMismatch" || a_reason == "notBound") {
				return std::format("Select slot {} (the character from your interrupted first join) and join again. If that slot is not in your character list, contact the server administrator to run first-join-abandon for {}.", a_pending.slot, a_pending.profile);
			}
			if (a_reason == "rateLimited") {
				return "The server is busy. Wait a few seconds and join again.";
			}
			return std::format("The server could not discard the unfinished first character ({}). Keep your saves and contact the server administrator to run first-join-abandon for {}.", a_reason, a_pending.profile);
		}

		void Finish(bool a_ok, const std::string& a_reason)
		{
			if (!g_pending) return;
			const auto pending = *g_pending;
			if (a_ok || a_reason == "slotEmpty") {
				if (!ServerProfileStore::ClearSaveLessFirstJoinHold(pending.serverKey)) {
					Complete(false, "The character was discarded but the local first-join hold could not be cleared. Keep your saves and contact the server administrator.");
					return;
				}
				Complete(false, a_ok
					? "Your unfinished first character had no saved progress and was discarded. Reconnect and create a character to continue."
					: "Your unfinished first character no longer exists on the server. Reconnect and create a character to continue.");
				return;
			}
			Complete(false, FailureMessage(a_reason, pending));
		}
	}

	Outcome BeginForSaveLessHold(const std::string& a_serverKey, F4T::SessionLaunch::JoinCompletion a_onComplete, std::string& a_error)
	{
		if (!ServerProfileStore::FirstJoinHoldIsSaveLess(a_serverKey)) {
			a_error = kSaveRecoveryMessage;
			return Outcome::Failed;
		}
		bool supported = false;
		{
			const std::scoped_lock lock(g_receiveMutex);
			supported = g_serverSupportsAbandon;
		}
		if (!supported) {
			a_error = kSaveRecoveryMessage;
			return Outcome::Failed;
		}
		const auto account = F4T::ClientIdentity::ConnectedProfileId();
		const auto slot = ServerProfileStore::FirstJoinHoldSlot(a_serverKey);
		const bool unscoped = !HoldSlotKnown(slot);
		if (g_pending) {
			a_error = "The unfinished first character is already being checked. Wait a moment and join again.";
			return Outcome::Failed;
		}
		const auto generation = F4T::Networking::GetConnectionGeneration();
		{
			const std::scoped_lock lock(g_receiveMutex);
			g_reply.reset();
			g_awaitingReply = true;
		}
		g_pending = Pending{
			a_serverKey,
			unscoped ? account : std::format("{}#{}", account, *slot),
			slot.value_or(0),
			unscoped,
			generation,
			std::chrono::steady_clock::now() + kReplyBudget,
			std::move(a_onComplete)
		};
		const auto action = unscoped
			? std::string{ R"({"op":"slotAbandon"})" }
			: std::format(R"({{"op":"slotAbandon","slot":{}}})", *slot);
		if (!F4T::CharacterSlotsClient::HandleAction(action)) {
			g_pending.reset();
			{
				const std::scoped_lock lock(g_receiveMutex);
				g_awaitingReply = false;
			}
			a_error = "The server connection is busy. Try again in a moment.";
			return Outcome::Failed;
		}
		F4T::UIClient::PushConnectionStatus("Checking your unfinished first character.");
		return Outcome::Pending;
	}

	void NoteWelcome(const nlohmann::json& a_packet)
	{
		bool supported = false;
		if (const auto slots = a_packet.find("characterSlots"); slots != a_packet.end() && slots->is_object()) {
			const auto advertised = slots->find("abandon");
			supported = advertised != slots->end() && advertised->is_boolean() && advertised->get<bool>();
		}
		const std::scoped_lock lock(g_receiveMutex);
		g_serverSupportsAbandon = supported;
	}

	void NoteReplyReceived(bool a_ok, const std::string& a_reason)
	{
		const std::scoped_lock lock(g_receiveMutex);
		if (!g_awaitingReply) return;
		g_awaitingReply = false;
		g_reply = Reply{ a_ok, a_reason };
	}

	bool CancelPending()
	{
		if (!g_pending) return false;
		const auto reply = TakeReply();
		if (DecideCancel(reply.has_value()) == TickAction::Finish) {
			Finish(reply->ok, reply->reason);
		} else {
			Complete(false, kConnectionEndedMessage);
		}
		return true;
	}

	void Tick()
	{
		if (!g_pending) return;
		const auto reply = TakeReply();
		switch (DecideTick(reply.has_value(), F4T::Networking::IsConnectedToServer(),
			F4T::Networking::GetConnectionGeneration() == g_pending->generation,
			std::chrono::steady_clock::now() >= g_pending->deadline)) {
		case TickAction::Finish:
			Finish(reply->ok, reply->reason);
			break;
		case TickAction::ConnectionEnded:
			Complete(false, kConnectionEndedMessage);
			break;
		case TickAction::TimedOut:
			Complete(false, "The server did not answer the discard request in time. Reconnect and join again; if it persists, contact the server administrator.");
			break;
		case TickAction::Wait:
			break;
		}
	}
}
