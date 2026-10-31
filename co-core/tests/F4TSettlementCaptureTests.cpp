#include "F4TSettlementCapture.h"
#include "F4TWorkshopProtocol.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace
{
	namespace Capture = F4T::SettlementCapture;
	using Json = nlohmann::json;
	using Capture::Op;
	using Capture::Presence;
	using Capture::Status;

	constexpr F4T::Workshop::BuildZoneId kZone = 9;
	const Capture::Workbench kBench{ kZone, 1000.0, 0.0, 0.0, 300.0 };

	Presence At(const std::string& a_profile, double a_x)
	{
		return Presence{ a_profile, true, 0.5, a_x, 0.0, 0.0, true, true, false };
	}

	Capture::Reply Send(Capture::Tracker& a_tracker, Op a_op, const std::string& a_owner, const std::string& a_who, const std::vector<Presence>& a_all, double a_now)
	{
		return a_tracker.Handle(Capture::Request{ kZone, a_op }, kBench, a_owner, a_who, a_all, a_now);
	}

	void TestClassify()
	{
		const Capture::Policy policy;
		assert(Capture::Classify(kBench, At("a", 1100.0), policy) == Capture::Standing::Eligible);
		assert(Capture::Classify(kBench, At("a", 1300.0), policy) == Capture::Standing::Eligible);
		assert(Capture::Classify(kBench, At("a", 1300.5), policy) == Capture::Standing::OutOfRange);
		auto stale = At("a", 1000.0);
		stale.positionAgeSeconds = 5.5;
		assert(Capture::Classify(kBench, stale, policy) == Capture::Standing::Stale);
		stale.positionAgeSeconds = -1.0;
		assert(Capture::Classify(kBench, stale, policy) == Capture::Standing::Stale);
		stale.positionAgeSeconds = 0.1;
		stale.positionKnown = false;
		assert(Capture::Classify(kBench, stale, policy) == Capture::Standing::Stale);
		auto nan = At("a", 1000.0);
		nan.x = std::numeric_limits<double>::quiet_NaN();
		assert(Capture::Classify(kBench, nan, policy) == Capture::Standing::Stale);
		auto dead = At("a", 1000.0);
		dead.alive = false;
		assert(Capture::Classify(kBench, dead, policy) == Capture::Standing::NotAlive);
		dead.alive = true;
		dead.vitalsKnown = false;
		assert(Capture::Classify(kBench, dead, policy) == Capture::Standing::NotAlive);
		auto downed = At("a", 1000.0);
		downed.downed = true;
		assert(Capture::Classify(kBench, downed, policy) == Capture::Standing::Downed);
		const Capture::Workbench nanBench{ kZone, std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0, 300.0 };
		assert(Capture::Classify(nanBench, At("a", 1000.0), policy) == Capture::Standing::OutOfRange);
		const Capture::Workbench infiniteBench{ kZone, 1000.0, 0.0, std::numeric_limits<double>::infinity(), 300.0 };
		assert(Capture::Classify(infiniteBench, At("a", 1000.0), policy) == Capture::Standing::OutOfRange);
		assert(Capture::ValidPolicy(policy));
		Capture::Policy bad;
		bad.holdSeconds = 0.0;
		assert(!Capture::ValidPolicy(bad));
	}

	void TestWorkbenchDefaults()
	{
		const Capture::Policy rules;
		F4T::Workshop::BuildZonePolicy zone{ 4, F4T::Workshop::FormKey{ "Fallout4.esm", 1 }, 10.0F, 20.0F, 30.0F, 2000.0F, 2.0F, 8 };
		auto bench = Capture::WorkbenchOf(zone, rules);
		assert(bench.buildZoneId == 4 && bench.x == 10.0 && bench.y == 20.0 && bench.z == 30.0 && bench.radius == rules.defaultRadius);
		zone.hasWorkbench = true;
		zone.workbenchX = 1.0F;
		zone.workbenchY = 2.0F;
		zone.workbenchZ = 3.0F;
		zone.captureRadius = 150.0F;
		bench = Capture::WorkbenchOf(zone, rules);
		assert(bench.x == 1.0 && bench.y == 2.0 && bench.z == 3.0 && bench.radius == 150.0);
		zone.captureRadius = 23655.0F;
		assert(Capture::WorkbenchOf(zone, rules).radius == rules.maximumRadius && rules.maximumRadius == 6000.0);
		zone.captureRadius = 5999.0F;
		assert(Capture::WorkbenchOf(zone, rules).radius == 5999.0);
		assert(F4T::Workshop::SettlementState{ zone }.PolicyValid());
		zone.captureRadius = -1.0F;
		assert(!F4T::Workshop::SettlementState{ zone }.PolicyValid());
		zone.captureRadius = 150.0F;
		zone.workbenchX = std::numeric_limits<float>::infinity();
		assert(!F4T::Workshop::SettlementState{ zone }.PolicyValid());
	}

	void TestUncontestedHoldCaptures()
	{
		Capture::Tracker tracker;
		const std::vector<Presence> alone{ At("profile-raider", 1050.0) };
		assert(Send(tracker, Op::Start, "profile-owner", "profile-raider", alone, 10.0).status == Status::Started);
		assert(tracker.AttemptCount() == 1 && tracker.ActiveZones().contains(kZone));
		const auto reply = Send(tracker, Op::Continue, "profile-owner", "profile-raider", alone, 20.0);
		assert(reply.status == Status::Holding && reply.elapsedSeconds == 10.0);
		for (const double now : { 30.0, 40.0, 50.0, 59.0 }) {
			assert(Send(tracker, Op::Continue, "profile-owner", "profile-raider", alone, now).status == Status::Holding);
		}
		assert(tracker.Update(kBench, "profile-owner", alone, 59.9).empty());
		assert(Send(tracker, Op::Continue, "profile-owner", "profile-raider", alone, 66.0).status == Status::Holding);
		const auto events = tracker.Update(kBench, "profile-owner", alone, 70.0);
		assert(events.size() == 1 && events[0].status == Status::Captured && events[0].capturerProfileId == "profile-raider" && events[0].buildZoneId == kZone);
		assert(tracker.AttemptCount() == 0);
		assert(tracker.Update(kBench, "profile-raider", alone, 71.0).empty());
	}

	void TestContestResetsHold()
	{
		Capture::Tracker tracker;
		const std::vector<Presence> alone{ At("profile-raider", 1000.0) };
		const std::vector<Presence> both{ At("profile-raider", 1000.0), At("profile-owner", 1100.0) };
		assert(Send(tracker, Op::Start, "profile-owner", "profile-raider", alone, 0.0).status == Status::Started);
		assert(Send(tracker, Op::Continue, "profile-owner", "profile-raider", alone, 8.0).status == Status::Holding);
		assert(Send(tracker, Op::Continue, "profile-owner", "profile-raider", alone, 16.0).status == Status::Holding);
		assert(Send(tracker, Op::Continue, "profile-owner", "profile-raider", alone, 24.0).status == Status::Holding);
		assert(Send(tracker, Op::Continue, "profile-owner", "profile-raider", alone, 32.0).status == Status::Holding);
		assert(Send(tracker, Op::Continue, "profile-owner", "profile-raider", alone, 40.0).status == Status::Holding);
		assert(Send(tracker, Op::Continue, "profile-owner", "profile-raider", alone, 48.0).status == Status::Holding);
		auto events = tracker.Update(kBench, "profile-owner", both, 50.0);
		assert(events.size() == 1 && events[0].status == Status::Contested);
		assert(Send(tracker, Op::Continue, "profile-owner", "profile-raider", both, 55.0).status == Status::Contested);
		for (const double now : { 63.0, 71.0, 79.0, 87.0, 95.0 }) {
			assert(Send(tracker, Op::Continue, "profile-owner", "profile-raider", both, now).status == Status::Contested);
		}
		assert(tracker.Update(kBench, "profile-owner", both, 100.0).empty());
		events = tracker.Update(kBench, "profile-owner", alone, 101.0);
		assert(events.size() == 1 && events[0].status == Status::Holding);
		assert(Send(tracker, Op::Continue, "profile-owner", "profile-raider", alone, 104.0).elapsedSeconds == 3.0);
		for (const double now : { 112.0, 120.0, 128.0, 136.0, 144.0, 152.0 }) {
			assert(Send(tracker, Op::Continue, "profile-owner", "profile-raider", alone, now).status == Status::Holding);
		}
		assert(tracker.Update(kBench, "profile-owner", alone, 160.9).empty());
		assert(Send(tracker, Op::Continue, "profile-owner", "profile-raider", alone, 160.9).status == Status::Holding);
		events = tracker.Update(kBench, "profile-owner", alone, 161.0);
		assert(events.size() == 1 && events[0].status == Status::Captured);
	}

	void TestIneligibleBystanderDoesNotContest()
	{
		Capture::Tracker tracker;
		auto downed = At("profile-owner", 1000.0);
		downed.downed = true;
		const std::vector<Presence> crowd{ At("profile-raider", 1000.0), downed, At("profile-far", 5000.0) };
		assert(Send(tracker, Op::Start, "profile-owner", "profile-raider", crowd, 0.0).status == Status::Started);
		assert(Send(tracker, Op::Start, "profile-owner", "profile-raider", crowd, 1.0).status == Status::Holding);
	}

	void TestRejections()
	{
		Capture::Tracker tracker;
		const std::vector<Presence> alone{ At("profile-raider", 1000.0) };
		assert(Send(tracker, Op::Start, "profile-raider", "profile-raider", alone, 0.0).status == Status::AlreadyOwner);
		assert(Send(tracker, Op::Continue, "profile-owner", "profile-raider", alone, 0.0).status == Status::NoAttempt);
		assert(Send(tracker, Op::Cancel, "profile-owner", "profile-raider", alone, 0.0).status == Status::NoAttempt);
		assert(Send(tracker, Op::Start, "profile-owner", "profile-stranger", alone, 0.0).status == Status::StalePosition);
		assert(Send(tracker, Op::Start, "profile-owner", "", alone, 0.0).status == Status::InvalidRequest);
		assert(Send(tracker, Op::Start, "profile-owner", "profile-raider", alone, std::nan("")).status == Status::InvalidRequest);
		assert(tracker.Handle(Capture::Request{ 0, Op::Start }, kBench, "profile-owner", "profile-raider", alone, 0.0).status == Status::InvalidRequest);
		assert(tracker.Handle(Capture::Request{ 10, Op::Start }, kBench, "profile-owner", "profile-raider", alone, 0.0).status == Status::InvalidRequest);
		const std::vector<Presence> far{ At("profile-raider", 2000.0) };
		assert(Send(tracker, Op::Start, "profile-owner", "profile-raider", far, 0.0).status == Status::NotInRange);
		auto dead = At("profile-raider", 1000.0);
		dead.alive = false;
		assert(Send(tracker, Op::Start, "profile-owner", "profile-raider", { dead }, 0.0).status == Status::NotAlive);
		auto downed = At("profile-raider", 1000.0);
		downed.downed = true;
		assert(Send(tracker, Op::Start, "profile-owner", "profile-raider", { downed }, 0.0).status == Status::Downed);
		assert(tracker.AttemptCount() == 0);
		assert(Send(tracker, Op::Start, "", "profile-raider", alone, 0.0).status == Status::Started);
	}

	void TestFailuresDropAttempt()
	{
		Capture::Tracker tracker;
		const std::vector<Presence> alone{ At("profile-raider", 1000.0) };
		const std::vector<Presence> gone{ At("profile-raider", 2000.0) };
		assert(Send(tracker, Op::Start, "profile-owner", "profile-raider", alone, 0.0).status == Status::Started);
		auto events = tracker.Update(kBench, "profile-owner", gone, 1.0);
		assert(events.size() == 1 && events[0].status == Status::NotInRange && tracker.AttemptCount() == 0);

		assert(Send(tracker, Op::Start, "profile-owner", "profile-raider", alone, 10.0).status == Status::Started);
		const auto ended = Send(tracker, Op::Continue, "profile-owner", "profile-raider", gone, 11.0);
		assert(ended.status == Status::NotInRange && ended.attemptEnded && tracker.AttemptCount() == 0);
		const auto refused = Send(tracker, Op::Start, "profile-owner", "profile-raider", gone, 12.0);
		assert(refused.status == Status::NotInRange && !refused.attemptEnded);

		assert(Send(tracker, Op::Start, "profile-owner", "profile-raider", alone, 20.0).status == Status::Started);
		assert(tracker.Update(kBench, "profile-owner", alone, 30.0).empty());
		events = tracker.Update(kBench, "profile-owner", alone, 30.1);
		assert(events.size() == 1 && events[0].status == Status::TimedOut && tracker.AttemptCount() == 0);

		assert(Send(tracker, Op::Start, "profile-owner", "profile-raider", alone, 40.0).status == Status::Started);
		assert(Send(tracker, Op::Continue, "profile-owner", "profile-raider", alone, 50.1).status == Status::TimedOut);

		assert(Send(tracker, Op::Start, "profile-owner", "profile-raider", alone, 90.0).status == Status::Started);
		auto quiet = At("profile-raider", 1000.0);
		quiet.positionAgeSeconds = 11.0;
		assert(Send(tracker, Op::Continue, "profile-owner", "profile-raider", { quiet }, 102.0).status == Status::StalePosition);
		assert(tracker.AttemptCount() == 0);

		assert(Send(tracker, Op::Start, "profile-owner", "profile-raider", alone, 60.0).status == Status::Started);
		assert(Send(tracker, Op::Cancel, "profile-owner", "profile-raider", alone, 61.0).status == Status::Cancelled);
		assert(tracker.AttemptCount() == 0);

		assert(Send(tracker, Op::Start, "profile-owner", "profile-raider", alone, 70.0).status == Status::Started);
		events = tracker.Update(kBench, "profile-raider", alone, 71.0);
		assert(events.size() == 1 && events[0].status == Status::AlreadyOwner);

		assert(Send(tracker, Op::Start, "profile-owner", "profile-raider", alone, 80.0).status == Status::Started);
		tracker.DropZone(kZone);
		assert(tracker.AttemptCount() == 0);
	}

	void TestOneAttemptPerProfileAndCap()
	{
		Capture::Tracker tracker;
		const std::vector<Presence> alone{ At("profile-raider", 1000.0) };
		assert(Send(tracker, Op::Start, "profile-owner", "profile-raider", alone, 0.0).status == Status::Started);
		const Capture::Workbench other{ 10, 1000.0, 0.0, 0.0, 300.0 };
		assert(tracker.Handle(Capture::Request{ 10, Op::Start }, other, "profile-owner", "profile-raider", alone, 1.0).status == Status::Busy);
		Capture::Policy tiny;
		tiny.maximumAttempts = 1;
		Capture::Tracker capped{ tiny };
		const std::vector<Presence> two{ At("profile-a", 1000.0), At("profile-b", 5000.0) };
		assert(Send(capped, Op::Start, "profile-owner", "profile-a", two, 0.0).status == Status::Started);
		const std::vector<Presence> twoClose{ At("profile-a", 1000.0), At("profile-b", 1000.0) };
		assert(Send(capped, Op::Start, "profile-owner", "profile-b", twoClose, 1.0).status == Status::Busy);
	}

	void TestZoneFile()
	{
		const Json entry{
			{ "buildZoneId", 4 },
			{ "space", Json{ { "plugin", "Fallout4.esm" }, { "localFormId", 60 } } },
			{ "centerX", 10.0 },
			{ "centerY", 20.0 },
			{ "centerZ", 30.0 },
			{ "radius", 2000.0 },
			{ "maxScale", 2.0 },
			{ "maxObjects", 8 }
		};
		const auto file = [](const Json& a_entry) { return Json{ { "version", 1 }, { "zones", Json::array({ a_entry }) } }; };
		const auto plain = F4T::WorkshopProtocol::ParseBuildZones(file(entry));
		assert(plain && plain->size() == 1 && !plain->front().hasWorkbench && plain->front().captureRadius == 0.0F);
		Json withBench = entry;
		withBench["workbench"] = Json{ { "x", 1.5 }, { "y", 2.5 }, { "z", 3.5 } };
		withBench["captureRadius"] = 250.0;
		const auto parsed = F4T::WorkshopProtocol::ParseBuildZones(file(withBench));
		assert(parsed && parsed->front().hasWorkbench && parsed->front().workbenchX == 1.5F && parsed->front().workbenchY == 2.5F);
		assert(parsed->front().workbenchZ == 3.5F && parsed->front().captureRadius == 250.0F);
		Json missingAxis = entry;
		missingAxis["workbench"] = Json{ { "x", 1.5 }, { "y", 2.5 } };
		assert(!F4T::WorkshopProtocol::ParseBuildZones(file(missingAxis)));
		Json badRadius = entry;
		badRadius["captureRadius"] = -5.0;
		assert(!F4T::WorkshopProtocol::ParseBuildZones(file(badRadius)));
		badRadius["captureRadius"] = "wide";
		assert(!F4T::WorkshopProtocol::ParseBuildZones(file(badRadius)));
	}

	void TestPackets()
	{
		const auto start = Capture::NormalizeRequest(Json{ { "buildZoneId", 9U }, { "op", "start" } });
		assert(start && start->buildZoneId == 9 && start->op == Op::Start);
		assert(Capture::NormalizeRequest(Json{ { "buildZoneId", 9U }, { "op", "continue" } })->op == Op::Continue);
		assert(Capture::NormalizeRequest(Json{ { "buildZoneId", 9U }, { "op", "cancel" } })->op == Op::Cancel);
		assert(!Capture::NormalizeRequest(Json{ { "buildZoneId", 9U }, { "op", "toggle" } }));
		assert(!Capture::NormalizeRequest(Json{ { "buildZoneId", -1 }, { "op", "start" } }));
		assert(!Capture::NormalizeRequest(Json{ { "buildZoneId", 9U } }));
		assert(!Capture::NormalizeRequest(Json{ { "op", "start" } }));
		const Capture::Workbench bench{ 9, 1.0, 2.0, 3.0, 800.0 };
		const auto state = Capture::MakeState(bench, "profile-raider", "profile-owner", Status::Contested, 0.0, 60.0);
		assert(state.at("type") == "settlementCaptureState" && state.at("status") == "contested" && state.at("raid") == true);
		assert(state.at("holdSeconds") == 60.0 && state.at("capturerProfileId") == "profile-raider" && state.at("ownerProfileId") == "profile-owner");
		assert(state.at("workbenchX") == 1.0 && state.at("workbenchY") == 2.0 && state.at("workbenchZ") == 3.0 && state.at("captureRadius") == 800.0);
		assert(Capture::MakeState(bench, "profile-raider", "", Status::Started, 0.0, 60.0).at("raid") == false);
		const auto decoded = Capture::DecodeState(state);
		assert(decoded && decoded->buildZoneId == 9 && decoded->status == "contested" && decoded->raid);
		assert(decoded->hasCapturePoint && decoded->workbenchX == 1.0 && decoded->captureRadius == 800.0);
		auto legacy = state;
		legacy.erase("workbenchX");
		legacy.erase("workbenchY");
		legacy.erase("workbenchZ");
		legacy.erase("captureRadius");
		const auto legacyDecoded = Capture::DecodeState(legacy);
		assert(legacyDecoded && !legacyDecoded->hasCapturePoint && legacyDecoded->status == "contested");
		assert(!Capture::DecodeState(Json{ { "type", "settlementCaptureState" } }));
		assert(!Capture::DecodeState(Json{ { "type", "workshopSnapshot" } }));
		const auto reject = Capture::MakeReject(9, Status::NotInRange);
		assert(reject.at("type") == "settlementCaptureReject" && reject.at("status") == "notInRange");
		assert(Capture::StatusName(Status::PersistenceUnavailable) == "persistenceUnavailable");
		assert(Capture::StatusName(Status::SettlementLimit) == "settlementLimit");
		assert(Capture::StatusName(Status::Captured) == "captured");
		const std::vector<Capture::Workbench> zoneList{ { 9, 1.0, 2.0, 3.0, 800.0 }, { 10, -4.0, 5.0, 6.0, 6000.0 } };
		const auto zonePacket = Capture::MakeZones(zoneList);
		assert(zonePacket.at("type") == "settlementCaptureZones" && zonePacket.at("zones").size() == 2);
		const auto zones = Capture::DecodeZones(zonePacket);
		assert(zones && zones->size() == 2 && (*zones)[1].buildZoneId == 10 && (*zones)[1].x == -4.0 && (*zones)[1].radius == 6000.0);
		assert(!Capture::DecodeZones(Json{ { "type", "settlementCaptureZones" } }));
		assert(!Capture::DecodeZones(Json{ { "type", "settlementCaptureZones" }, { "zones", Json::array({ 1 }) } }));
		assert(!Capture::DecodeZones(Json{ { "type", "settlementCaptureZones" }, { "zones", Json::array({ Json{ { "buildZoneId", 0 }, { "x", 1.0 }, { "y", 1.0 }, { "z", 1.0 }, { "radius", 1.0 } } }) } }));
		assert(!Capture::DecodeZones(Json{ { "type", "settlementCaptureZones" }, { "zones", Json::array({ Json{ { "buildZoneId", 4 }, { "x", 1.0 }, { "y", 1.0 }, { "z", 1.0 }, { "radius", -1.0 } } }) } }));
		assert(!Capture::DecodeZones(Json{ { "type", "workshopSnapshot" }, { "zones", Json::array() } }));
		auto oversized = state;
		oversized["captureRadius"] = 9000.0;
		const auto oversizedDecoded = Capture::DecodeState(oversized);
		assert(oversizedDecoded && oversizedDecoded->hasCapturePoint && oversizedDecoded->captureRadius == 6000.0);
		auto farPoint = state;
		farPoint["workbenchX"] = 1.0e8;
		const auto farDecoded = Capture::DecodeState(farPoint);
		assert(farDecoded && !farDecoded->hasCapturePoint && farDecoded->status == "contested");
		auto nanHold = state;
		nanHold["holdSeconds"] = std::numeric_limits<double>::quiet_NaN();
		assert(!Capture::DecodeState(nanHold));
		auto infiniteElapsed = state;
		infiniteElapsed["elapsedSeconds"] = std::numeric_limits<double>::infinity();
		assert(!Capture::DecodeState(infiniteElapsed));
		auto textElapsed = state;
		textElapsed["elapsedSeconds"] = "soon";
		assert(!Capture::DecodeState(textElapsed));
		const std::vector<Capture::Workbench> farZone{ { 11, 1.0e8, 2.0, 3.0, 800.0 } };
		assert(!Capture::DecodeZones(Capture::MakeZones(farZone)));
		const std::vector<Capture::Workbench> wideZone{ { 12, 1.0, 2.0, 3.0, 9000.0 } };
		const auto wideZones = Capture::DecodeZones(Capture::MakeZones(wideZone));
		assert(wideZones && (*wideZones)[0].radius == 6000.0);
	}

	void TestHugeZoneIdIsAcceptedWithoutThrowing()
	{
		constexpr auto kMaximumZone = std::numeric_limits<F4T::Workshop::BuildZoneId>::max();
		const auto request = Capture::NormalizeRequest(Json{ { "buildZoneId", kMaximumZone }, { "op", "start" } });
		assert(request && request->buildZoneId == kMaximumZone);
		const auto state = Json{
			{ "type", "settlementCaptureState" },
			{ "buildZoneId", kMaximumZone },
			{ "status", "started" },
			{ "capturerProfileId", "profile-raider" },
			{ "ownerProfileId", "" },
			{ "raid", false },
			{ "elapsedSeconds", 0.0 },
			{ "holdSeconds", 60.0 }
		};
		const auto decoded = Capture::DecodeState(state);
		assert(decoded && decoded->buildZoneId == kMaximumZone && !decoded->hasCapturePoint);
	}
}

int main()
{
	TestClassify();
	TestWorkbenchDefaults();
	TestUncontestedHoldCaptures();
	TestContestResetsHold();
	TestIneligibleBystanderDoesNotContest();
	TestRejections();
	TestFailuresDropAttempt();
	TestOneAttemptPerProfileAndCap();
	TestZoneFile();
	TestPackets();
	TestHugeZoneIdIsAcceptedWithoutThrowing();
	return 0;
}
