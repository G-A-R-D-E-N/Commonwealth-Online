#include "F4TInventorySnapshot.h"

#include "F4TContentManifest.h"
#include "F4TDropLedger.h"
#include "F4TDurableState.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

namespace
{
	using Json = nlohmann::json;
	using namespace F4T::InventorySnapshot;

	Json Item(const std::string& a_plugin, const std::string& a_localFormId, std::uint32_t a_count)
	{
		return Json{ { "plugin", a_plugin }, { "localFormId", a_localFormId }, { "count", a_count } };
	}

	Json PartPacket(const std::string& a_operationId, std::uint32_t a_baseRevision, std::uint32_t a_part, std::uint32_t a_parts, const Json& a_items)
	{
		return Json{ { "type", std::string{ kRequestType } }, { "operationId", a_operationId },
			{ "baseRevision", a_baseRevision }, { "part", a_part }, { "parts", a_parts }, { "items", a_items } };
	}

	Json Root(std::uint32_t a_revision)
	{
		Json profile{
			{ "inventory", Json{ { "initialized", true }, { "revision", a_revision }, { "items", Json::object() } } },
			{ "inventoryOperations", Json::array() }
		};
		return Json{ { "inventoryAuthorityBlocked", false }, { "profiles", Json{ { "a", profile } } } };
	}

	Json Numbered(std::size_t a_count)
	{
		Json items = Json::array();
		for (std::size_t index = 0; index < a_count; ++index) {
			char form[9]{};
			std::snprintf(form, sizeof(form), "%08zX", index + 1);
			items.push_back(Item("test.esm", form, 1));
		}
		return items;
	}

	void TestNormalizeItemsIsOrderFreeAndStrict()
	{
		const auto forward = NormalizeItems(Json::array({ Item("Test.esm", "123", 3), Item("other.esp", "ABC", 1) }));
		const auto reversed = NormalizeItems(Json::array({ Item("other.esp", "ABC", 1), Item("test.esm", "00000123", 3) }));
		assert(forward && reversed && *forward == *reversed);
		assert(!NormalizeItems(Json::array({ Item("a.esm", "1", 1), Item("a.esm", "00000001", 2) })));
		assert(!NormalizeItems(Json::array({ Item("a.esm", "1", 0) })));
		assert(NormalizeItems(Json::array({ Item("a.esm", "1", (std::numeric_limits<std::uint32_t>::max)()) })));
		assert(!NormalizeItems(Json::array({ Json{ { "plugin", "a.esm" }, { "localFormId", "1" }, { "count", 4294967296ULL } } })));
		assert(!NormalizeItems(Json::object()));
		assert(NormalizeItems(Numbered(2048)));
		assert(!NormalizeItems(Numbered(2049)));
	}

	void TestNormalizePartValidatesShape()
	{
		const auto valid = NormalizePart(PartPacket("s1", 4, 1, 2, Json::array({ Item("test.esm", "123", 3) })));
		assert(valid && valid->part == 1 && valid->parts == 2 && valid->baseRevision == 4 && valid->wireItems.size() == 1);
		assert(NormalizePart(PartPacket("s1", 0, 0, 1, Json::array())));
		assert(!NormalizePart(PartPacket("s1", 0, 2, 2, Json::array())));
		assert(!NormalizePart(PartPacket("s1", 0, 0, 0, Json::array())));
		assert(!NormalizePart(PartPacket("s1", 0, 0, kMaximumParts + 1, Json::array())));
		assert(!NormalizePart(PartPacket("", 0, 0, 1, Json::array())));
		assert(!NormalizePart(PartPacket("s1", 0, 0, 1, Numbered(kMaximumPartItems + 1))));
		assert(NormalizePart(PartPacket("s1", 0, 0, 1, Numbered(kMaximumPartItems))));
		auto extra = PartPacket("s1", 0, 0, 1, Json::array());
		extra["expectedRevision"] = 0u;
		assert(!NormalizePart(extra));
		assert(!NormalizePart(PartPacket("s1", 0, 0, 1, Json::array({ Item("a.esm", "1", 1), Item("a.esm", "00000001", 1) }))));
	}

	void TestNormalizeRequestValidatesShape()
	{
		const auto valid = NormalizeRequest("s1", 4, Json::array({ Item("test.esm", "123", 3) }));
		assert(valid && valid->at("baseRevision") == 4u && valid->at("items").is_object());
		assert(!NormalizeRequest("", 0, Json::array()));
		assert(!NormalizeRequest("s1", 0, Json::object()));
		assert(!NormalizeRequest("s1", 0, Numbered(2049)));
	}

	void TestPartsReassembleToOneRequest()
	{
		const auto first = Json::array({ Item("test.esm", "1", 1) });
		const auto second = Json::array({ Item("test.esm", "2", 2) });
		Json joined = first;
		for (const auto& item : second) joined.push_back(item);
		const auto whole = NormalizeRequest("s1", 0, joined);
		assert(whole && whole->at("items").size() == 2);
		Json duplicated = first;
		for (const auto& item : first) duplicated.push_back(item);
		assert(!NormalizeRequest("s1", 0, duplicated));
	}

	void TestReconcileMatchCommitsAndLogs()
	{
		auto root = Root(0);
		const auto request = *NormalizeRequest("s1", 0, Json::array({ Item("test.esm", "123", 3) }));
		const auto outcome = Reconcile(root, "a", request);
		assert(outcome.changed);
		assert(outcome.packet.at("status") == "committed" && outcome.packet.at("revision") == 1u);
		const auto& profile = root.at("profiles").at("a");
		assert(profile.at("inventory").at("revision") == 1u);
		assert(profile.at("inventory").at("items") == request.at("items"));
		assert(profile.at("inventoryOperations").size() == 1);
		const auto& logged = profile.at("inventoryOperations").at(0).at("request");
		assert(!logged.contains("items") && logged.at("itemsDigest") == F4T::ContentManifest::Sha256Hex(request.at("items").dump()));
		std::set<std::string> seen;
		assert(ValidateLoggedOperation(profile.at("inventoryOperations").at(0), seen, 1, 1) == 1);
	}

	void TestReconcileStaleLeavesLedgerUnchanged()
	{
		auto root = Root(0);
		const auto first = *NormalizeRequest("s1", 0, Json::array({ Item("test.esm", "123", 3) }));
		(void)Reconcile(root, "a", first);
		const auto before = root;
		const auto stale = *NormalizeRequest("s2", 0, Json::array());
		const auto outcome = Reconcile(root, "a", stale);
		assert(!outcome.changed && outcome.packet.at("status") == "rejected");
		assert(outcome.packet.at("reason") == "staleRevision" && outcome.packet.at("revision") == 1u);
		assert(root == before);
	}

	void TestReconcileReplaysAndRejectsReusedOperationId()
	{
		auto root = Root(0);
		const auto first = *NormalizeRequest("s1", 0, Json::array({ Item("test.esm", "123", 3) }));
		const auto committed = Reconcile(root, "a", first);
		const auto before = root;
		const auto replay = Reconcile(root, "a", first);
		assert(!replay.changed && replay.packet == committed.packet && root == before);
		const auto reused = *NormalizeRequest("s1", 1, Json::array({ Item("test.esm", "123", 4) }));
		const auto mismatch = Reconcile(root, "a", reused);
		assert(!mismatch.changed && mismatch.packet.at("reason") == "operationMismatch" && root == before);
	}

	void TestReconcileEqualItemsIsNoOp()
	{
		auto root = Root(0);
		const auto request = *NormalizeRequest("s1", 0, Json::array({ Item("test.esm", "123", 3) }));
		(void)Reconcile(root, "a", request);
		const auto before = root;
		const auto same = *NormalizeRequest("s2", 1, Json::array({ Item("test.esm", "123", 3) }));
		const auto outcome = Reconcile(root, "a", same);
		assert(!outcome.changed && outcome.packet.at("status") == "committed" && outcome.packet.at("revision") == 1u);
		assert(root == before);
	}

	void TestReconcileRejectsUnknownProfileAndCapacity()
	{
		auto root = Root(0);
		const auto request = *NormalizeRequest("s1", 0, Json::array());
		const auto unknown = Reconcile(root, "missing", request);
		assert(!unknown.changed && unknown.packet.at("reason") == "inventoryAuthorityUnavailable");

		auto full = Root((std::numeric_limits<std::uint32_t>::max)());
		const auto atMax = *NormalizeRequest("s2", (std::numeric_limits<std::uint32_t>::max)(), Json::array({ Item("test.esm", "123", 3) }));
		const auto capped = Reconcile(full, "a", atMax);
		assert(!capped.changed && capped.packet.at("reason") == "capacityExceeded");
	}

	void TestReconcileWaitsForPendingStartingGrant()
	{
		auto root = Root(0);
		root.at("profiles").at("a")["startingGrant"] = Json{ { "granted", false }, { "items", Json::object() } };
		const auto before = root;
		const auto request = *NormalizeRequest("s1", 0, Json::array({ Item("test.esm", "123", 3) }));
		const auto held = Reconcile(root, "a", request);
		assert(!held.changed && held.packet.at("reason") == "startingInventoryPending");
		assert(!held.packet.contains("revision"));
		assert(root == before);
		root.at("profiles").at("a")["startingGrant"]["granted"] = true;
		assert(Reconcile(root, "a", request).packet.at("reason") == "startingInventoryPending");
		const F4T::SaveProfileLogic::AttestedSave save{ "Save_fixture_2", std::string(64, 'b'), 0 };
		assert(F4T::DurableState::ResolveStartingGrant(root, "a", "fixture", save, "inspected legacy inventory", true));
		assert(Reconcile(root, "a", request).changed);
	}

	void TestReconcileHistoryIsBounded()
	{
		auto root = Root(0);
		for (std::uint32_t index = 0; index < F4T::Drops::kMaximumOperationHistory + 2; ++index) {
			const auto request = *NormalizeRequest("op" + std::to_string(index), index,
				Json::array({ Item("test.esm", "123", index + 1) }));
			assert(Reconcile(root, "a", request).changed);
		}
		const auto& history = root.at("profiles").at("a").at("inventoryOperations");
		assert(history.size() == F4T::Drops::kMaximumOperationHistory);
		std::set<std::string> seen;
		std::uint32_t expected = F4T::Drops::kMaximumOperationHistory + 2 - static_cast<std::uint32_t>(history.size()) + 1;
		for (const auto& operation : history) {
			assert(ValidateLoggedOperation(operation, seen, expected, F4T::Drops::kMaximumOperationHistory + 2) == expected);
			++expected;
		}
	}

	Json Keyed(const std::string& a_plugin, const std::string& a_localFormId, std::uint32_t a_count)
	{
		Json items = Json::object();
		items[a_plugin + ":" + a_localFormId] = Item(a_plugin, a_localFormId, a_count);
		return items;
	}

	void TestGainWithinBudgetChargesBucket()
	{
		const auto policy = DefaultGainPolicy(GainMode::Enforce);
		const auto verdict = EvaluateGain(Json::object(), Keyed("test.esm", "00000123", 1000), false, policy, {}, 1000.0);
		assert(!verdict.limited);
		assert(verdict.next.at("units").tokens == 49000.0);
		assert(verdict.next.at("newKeys").tokens == 255.0);
	}

	void TestGainOverBurstRejectsWithRevisionAndLeavesLedger()
	{
		const auto policy = DefaultGainPolicy(GainMode::Enforce);
		const GainBuckets buckets{};
		const GainCheck check = [&](const Json& a_before, const Json& a_after, std::uint32_t a_revision) {
			return EvaluateGain(a_before, a_after, a_revision == 0, policy, buckets, 1000.0).limited;
		};
		auto root = Root(1);
		const auto before = root;
		const auto request = *NormalizeRequest("s1", 1, Json::array({ Item("test.esm", "123", 60000) }));
		const auto outcome = Reconcile(root, "a", request, check);
		assert(!outcome.changed && outcome.packet.at("status") == "rejected");
		assert(outcome.packet.at("reason") == "gainLimited" && outcome.packet.at("revision") == 1u);
		assert(root == before);
		assert(root.at("profiles").at("a").at("inventoryOperations").empty());
	}

	void TestDecreasesAndUnchangedKeysAreFree()
	{
		const auto policy = DefaultGainPolicy(GainMode::Enforce);
		const GainBuckets drained{ { "units", Bucket{ 0.0, 1000.0 } }, { "newKeys", Bucket{ 0.0, 1000.0 } } };
		const auto before = Keyed("test.esm", "00000123", 10);
		assert(!EvaluateGain(before, Keyed("test.esm", "00000123", 5), false, policy, drained, 1000.0).limited);
		assert(!EvaluateGain(before, before, false, policy, drained, 1000.0).limited);
	}

	void TestRefillOverTime()
	{
		const auto policy = DefaultGainPolicy(GainMode::Enforce);
		const GainBuckets drained{ { "units", Bucket{ 0.0, 0.0 } } };
		const auto gain = Keyed("test.esm", "00000123", 1000);
		assert(EvaluateGain(Json::object(), gain, false, policy, drained, 0.0).limited);
		const auto later = EvaluateGain(Json::object(), gain, false, policy, drained, 60.0);
		assert(!later.limited && later.next.at("units").tokens == 19000.0);
		const GainBuckets future{ { "units", Bucket{ 0.0, 100.0 } } };
		assert(EvaluateGain(Json::object(), gain, false, policy, future, 50.0).limited);
	}

	void TestOnboardingUsesOnboardingBurst()
	{
		const auto policy = DefaultGainPolicy(GainMode::Enforce);
		const auto big = Keyed("test.esm", "00000123", 500000);
		assert(!EvaluateGain(Json::object(), big, true, policy, {}, 1000.0).limited);
		assert(EvaluateGain(Json::object(), big, false, policy, {}, 1000.0).limited);
	}

	void TestNewKeyBudget()
	{
		const auto policy = DefaultGainPolicy(GainMode::Enforce);
		Json many = Json::object();
		for (const auto& item : Numbered(300)) {
			many[item.at("plugin").get<std::string>() + ":" + item.at("localFormId").get<std::string>()] = item;
		}
		assert(EvaluateGain(Json::object(), many, false, policy, {}, 1000.0).limited);
		assert(!EvaluateGain(Json::object(), many, true, policy, {}, 1000.0).limited);
	}

	void TestCapsBucketIsSeparate()
	{
		const auto policy = DefaultGainPolicy(GainMode::Enforce);
		const auto caps = EvaluateGain(Json::object(), Keyed("fallout4.esm", "0000000F", 200000), false, policy, {}, 1000.0);
		assert(!caps.limited);
		assert(caps.next.at(std::string{ kCapsItemKey }).tokens == 50000.0);
		assert(!caps.next.contains("units"));
		assert(EvaluateGain(Json::object(), Keyed("test.esm", "00000123", 60000), false, policy, {}, 1000.0).limited);
	}

	void TestStackCeiling()
	{
		const auto policy = DefaultGainPolicy(GainMode::Enforce);
		assert(EvaluateGain(Keyed("test.esm", "00000123", 10000000U), Keyed("test.esm", "00000123", 10000001U), false, policy, {}, 1000.0).limited);
		assert(!EvaluateGain(Keyed("test.esm", "00000123", 3000000000U), Keyed("test.esm", "00000123", 3000000000U), false, policy, {}, 1000.0).limited);
	}

	void TestReplayedOperationIgnoresGuard()
	{
		auto root = Root(0);
		const auto request = *NormalizeRequest("s1", 0, Json::array({ Item("test.esm", "123", 3) }));
		const auto committed = Reconcile(root, "a", request);
		assert(committed.changed);
		const GainCheck rejectAll = [](const Json&, const Json&, std::uint32_t) { return true; };
		const auto replay = Reconcile(root, "a", request, rejectAll);
		assert(!replay.changed && replay.packet == committed.packet);
	}

	void TestIdenticalItemsNeverCallGuard()
	{
		auto root = Root(0);
		const auto first = *NormalizeRequest("s1", 0, Json::array({ Item("test.esm", "123", 3) }));
		assert(Reconcile(root, "a", first).changed);
		int calls = 0;
		const GainCheck counting = [&calls](const Json&, const Json&, std::uint32_t) {
			++calls;
			return true;
		};
		const auto same = *NormalizeRequest("s2", 1, Json::array({ Item("test.esm", "123", 3) }));
		const auto outcome = Reconcile(root, "a", same, counting);
		assert(!outcome.changed && outcome.packet.at("status") == "committed" && calls == 0);
	}

	void TestParseGainMode()
	{
		assert(ParseGainMode("off") == GainMode::Off);
		assert(ParseGainMode("warn") == GainMode::Warn);
		assert(ParseGainMode("enforce") == GainMode::Enforce);
		assert(!ParseGainMode("maybe") && !ParseGainMode(""));
	}

	void TestValidateLoggedOperationRejectsTampering()
	{
		auto root = Root(0);
		const auto request = *NormalizeRequest("s1", 0, Json::array({ Item("test.esm", "123", 3) }));
		(void)Reconcile(root, "a", request);
		const auto operation = root.at("profiles").at("a").at("inventoryOperations").at(0);
		auto wrongResult = operation;
		wrongResult["result"]["revision"] = 2u;
		bool threw = false;
		std::set<std::string> seen;
		try { (void)ValidateLoggedOperation(wrongResult, seen, 1, 1); } catch (const std::invalid_argument&) { threw = true; }
		assert(threw);
		auto wrongDigest = operation;
		wrongDigest["request"]["itemsDigest"] = "not-a-digest";
		threw = false;
		seen.clear();
		try { (void)ValidateLoggedOperation(wrongDigest, seen, 1, 1); } catch (const std::invalid_argument&) { threw = true; }
		assert(threw);
		threw = false;
		seen.clear();
		try { (void)ValidateLoggedOperation(operation, seen, 2, 1); } catch (const std::invalid_argument&) { threw = true; }
		assert(threw);
		seen.clear();
		assert(ValidateLoggedOperation(operation, seen, 1, 1) == 1);
		threw = false;
		try { (void)ValidateLoggedOperation(operation, seen, 1, 1); } catch (const std::invalid_argument&) { threw = true; }
		assert(threw);
	}
}

int main()
{
	TestNormalizeItemsIsOrderFreeAndStrict();
	TestNormalizePartValidatesShape();
	TestNormalizeRequestValidatesShape();
	TestPartsReassembleToOneRequest();
	TestReconcileMatchCommitsAndLogs();
	TestReconcileStaleLeavesLedgerUnchanged();
	TestReconcileReplaysAndRejectsReusedOperationId();
	TestReconcileEqualItemsIsNoOp();
	TestReconcileRejectsUnknownProfileAndCapacity();
	TestReconcileWaitsForPendingStartingGrant();
	TestReconcileHistoryIsBounded();
	TestGainWithinBudgetChargesBucket();
	TestGainOverBurstRejectsWithRevisionAndLeavesLedger();
	TestDecreasesAndUnchangedKeysAreFree();
	TestRefillOverTime();
	TestOnboardingUsesOnboardingBurst();
	TestNewKeyBudget();
	TestCapsBucketIsSeparate();
	TestStackCeiling();
	TestReplayedOperationIgnoresGuard();
	TestIdenticalItemsNeverCallGuard();
	TestParseGainMode();
	TestValidateLoggedOperationRejectsTampering();
	return EXIT_SUCCESS;
}
