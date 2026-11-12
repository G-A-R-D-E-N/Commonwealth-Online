#include "F4TQuestReadyGate.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using F4T::QuestReady::Drive;
	using F4T::QuestReady::Gate;

	struct Client
	{
		Gate gate;
		bool tutorialComplete{};
		std::uint16_t stage{};
		bool sendWorks{ true };
		std::uint64_t now{};
		std::vector<std::string> sent;

		bool Send(std::string_view a_packet)
		{
			if (!sendWorks) return false;
			sent.emplace_back(a_packet);
			return true;
		}

		void Tick()
		{
			(void)Drive(
				gate,
				tutorialComplete,
				stage,
				now,
				[this] { return Send("report"); },
				[this] { return Send("ready"); });
		}
	};

	void SaveAlreadyPastThePod()
	{
		Client client;
		client.tutorialComplete = true;
		client.stage = 200;
		client.Tick();
		assert(client.sent.size() == 2);
		assert(client.sent.at(0) == "report");
		assert(client.sent.at(1) == "ready");
		assert(client.gate.ReportSent());
		assert(client.gate.ReadySent());
		client.Tick();
		assert(client.sent.size() == 2);
	}

	void LivePodExit()
	{
		Client client;
		client.tutorialComplete = true;
		client.stage = 2;
		client.Tick();
		assert(client.sent.size() == 2);
		assert(client.sent.at(0) == "report");
		assert(client.sent.at(1) == "ready");
	}

	void FailedSendRetriesWithBackoff()
	{
		Client client;
		client.tutorialComplete = true;
		client.stage = 2;
		client.sendWorks = false;
		client.Tick();
		assert(client.sent.empty());
		assert(!client.gate.ReportSent());
		assert(!client.gate.ReadySent());
		client.Tick();
		assert(client.sent.empty());
		client.now += 1'000;
		client.sendWorks = true;
		client.Tick();
		assert(client.sent.size() == 2);
		assert(client.sent.at(0) == "report");
		assert(client.sent.at(1) == "ready");
		assert(client.gate.ReadySent());
	}

	void RefusedReadyResendsTheReport()
	{
		Client client;
		client.tutorialComplete = true;
		client.stage = 2;
		client.Tick();
		assert(client.gate.ReadySent());
		client.gate.NoteRefused(client.now);
		assert(!client.gate.ReadySent());
		client.sent.clear();
		client.Tick();
		assert(client.sent.empty());
		client.now += 1'000;
		client.Tick();
		assert(client.sent.size() == 2);
		assert(client.sent.at(0) == "report");
		assert(client.sent.at(1) == "ready");
	}

	void RepeatedRefusalsBackOffAndCap()
	{
		Client client;
		client.tutorialComplete = true;
		std::vector<std::uint64_t> delays;
		for (int attempt = 0; attempt < 7; ++attempt) {
			client.Tick();
			const auto refusedAt = client.now;
			client.gate.NoteRefused(refusedAt);
			while (client.gate.Next(true, 2, client.now) == Gate::Action::Idle && client.now - refusedAt < 60'000) {
				++client.now;
			}
			delays.push_back(client.now - refusedAt);
		}
		assert(delays.size() == 7);
		assert(delays.at(0) == 1'000);
		assert(delays.at(1) == 2'000);
		assert(delays.at(2) == 4'000);
		assert(delays.at(3) == 8'000);
		assert(delays.at(4) == 16'000);
		assert(delays.at(5) == 30'000);
		assert(delays.at(6) == 30'000);
		assert(!client.gate.Exhausted());

		client.Tick();
		client.gate.NoteRefused(client.now);
		assert(client.gate.Exhausted());
		client.now += 600'000;
		assert(client.gate.Next(true, 2, client.now) == Gate::Action::Idle);
	}

	void RefusedWithoutAReadyIsIgnored()
	{
		Client client;
		client.tutorialComplete = true;
		client.stage = 2;
		client.gate.NoteRefused(client.now);
		assert(client.gate.Next(true, 2, client.now) == Gate::Action::Report);
		assert(!client.gate.Exhausted());
		client.Tick();
		assert(client.gate.ReadySent());
		client.sent.clear();
		client.gate.NoteRefused(client.now);
		assert(client.gate.Next(true, 2, client.now) == Gate::Action::Idle);
		client.now += 1'000;
		assert(client.gate.Next(true, 2, client.now) == Gate::Action::Report);
	}

	void QuietPeriodResetsTheBackoff()
	{
		Client client;
		client.tutorialComplete = true;
		client.stage = 2;
		for (int attempt = 0; attempt < 3; ++attempt) {
			client.Tick();
			client.gate.NoteRefused(client.now);
			while (client.gate.Next(true, 2, client.now) == Gate::Action::Idle) {
				++client.now;
			}
		}
		client.Tick();
		assert(client.gate.ReadySent());

		client.now += F4T::QuestReady::kQuietResetMilliseconds;
		assert(client.gate.Next(true, 2, client.now) == Gate::Action::Idle);
		client.gate.NoteRefused(client.now);
		const auto refusedAt = client.now;
		while (client.gate.Next(true, 2, client.now) == Gate::Action::Idle) {
			++client.now;
		}
		assert(client.now - refusedAt == 1'000);
		assert(!client.gate.Exhausted());
	}

	void TutorialStageAdvanceResendsTheReport()
	{
		Client client;
		client.tutorialComplete = true;
		client.stage = 2;
		client.Tick();
		assert(client.gate.ReadySent());
		client.sent.clear();
		client.stage = 12;
		client.Tick();
		assert(client.sent.size() == 1);
		assert(client.sent.at(0) == "report");
		assert(client.gate.ReadySent());
	}

	void UnfinishedTutorialSendsNothing()
	{
		Client client;
		client.stage = 200;
		client.Tick();
		assert(client.sent.empty());
		assert(!client.gate.ReadySent());
	}

	void ResetStartsAFreshSession()
	{
		Client client;
		client.tutorialComplete = true;
		client.stage = 2;
		client.Tick();
		client.gate.NoteRefused(client.now);
		client.gate.NoteRefused(client.now);
		client.gate.Reset();
		assert(!client.gate.ReportSent());
		assert(!client.gate.ReadySent());
		assert(!client.gate.Exhausted());
		client.sent.clear();
		client.Tick();
		assert(client.sent.size() == 2);
		assert(client.sent.at(0) == "report");
		assert(client.sent.at(1) == "ready");
	}
}

int main()
{
	SaveAlreadyPastThePod();
	LivePodExit();
	FailedSendRetriesWithBackoff();
	RefusedReadyResendsTheReport();
	RepeatedRefusalsBackOffAndCap();
	RefusedWithoutAReadyIsIgnored();
	QuietPeriodResetsTheBackoff();
	TutorialStageAdvanceResendsTheReport();
	UnfinishedTutorialSendsNothing();
	ResetStartsAFreshSession();
	return 0;
}
