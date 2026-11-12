#include "F4TAdminItemValidation.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstddef>
#include <string>

int main()
{
	using namespace F4T::AdminItemValidation;
	using Json = nlohmann::json;

	const auto item = [](const std::string& a_itemId, const std::string& a_name, const Json& a_count) {
		return Json{ { "itemId", a_itemId }, { "name", a_name }, { "count", a_count } };
	};
	const auto report = [&](const Json& a_items, const Json& a_truncated = false) {
		return Json{ { "type", "inventoryReport" }, { "opId", 8 }, { "truncated", a_truncated }, { "items", a_items } };
	};

	const auto valid = NormalizeInventoryReport(report(Json::array({ item("0000000f", "Bottle caps", 412) })));
	assert(valid.has_value());
	assert(valid->at("opId") == 8);
	assert(valid->at("truncated") == false);
	assert(valid->at("items").at(0).at("itemId") == "0000000F");
	assert(valid->at("items").at(0).at("name") == "Bottle caps");
	assert(valid->at("items").at(0).at("count") == 412);

	assert(!NormalizeInventoryReport(report(Json::array({ item("0000000", "x", 1) }))).has_value());
	assert(!NormalizeInventoryReport(report(Json::array({ item("0000000G", "x", 1) }))).has_value());
	assert(!NormalizeInventoryReport(report(Json::array({ item("00000000", "x", 1) }))).has_value());
	assert(!NormalizeInventoryReport(report(Json::array({ item("", "x", 1) }))).has_value());
	assert(!NormalizeInventoryReport(report(Json::array({ Json{ { "itemId", 15 }, { "name", "x" }, { "count", 1 } } }))).has_value());

	assert(!NormalizeInventoryReport(report(Json::array({ item("0000000F", "x", 0) }))).has_value());
	assert(!NormalizeInventoryReport(report(Json::array({ item("0000000F", "x", -1) }))).has_value());
	assert(!NormalizeInventoryReport(report(Json::array({ item("0000000F", "x", 1.5) }))).has_value());
	assert(!NormalizeInventoryReport(report(Json::array({ item("0000000F", "x", true) }))).has_value());

	assert(NormalizeInventoryReport(report(Json::array({ item("0000000F", std::string(64, 'a'), 1) }))).has_value());
	assert(!NormalizeInventoryReport(report(Json::array({ item("0000000F", std::string(65, 'a'), 1) }))).has_value());
	assert(!NormalizeInventoryReport(report(Json::array({ item("0000000F", "bad\nname", 1) }))).has_value());

	Json many = Json::array();
	for (std::size_t index = 0; index < MaximumInventoryItems; ++index) {
		many.push_back(item("0000000F", "x", 1));
	}
	assert(NormalizeInventoryReport(report(many)).has_value());
	many.push_back(item("0000000F", "x", 1));
	assert(!NormalizeInventoryReport(report(many)).has_value());

	Json oversized = report(Json::array({ item("0000000F", "x", 1) }));
	oversized["pad"] = std::string(MaximumInventoryReportBytes, 'p');
	assert(!NormalizeInventoryReport(oversized).has_value());

	Json noTruncated = report(Json::array());
	noTruncated.erase("truncated");
	assert(!NormalizeInventoryReport(noTruncated).has_value());

	Json zeroOp = report(Json::array());
	zeroOp["opId"] = 0;
	assert(!NormalizeInventoryReport(zeroOp).has_value());
	assert(!NormalizeInventoryReport(Json::array()).has_value());

	const Json okResult{ { "type", "adminItemResult" }, { "opId", 7 }, { "ok", true }, { "applied", 3 }, { "error", "" } };
	const auto normalizedOk = NormalizeAdminItemResult(okResult);
	assert(normalizedOk.has_value());
	assert(normalizedOk->at("applied") == 3);

	assert(!NormalizeAdminItemResult(Json{ { "opId", 7 }, { "ok", false }, { "applied", 0 }, { "error", "" } }).has_value());
	assert(NormalizeAdminItemResult(Json{ { "opId", 7 }, { "ok", false }, { "applied", 0 }, { "error", "denied" } }).has_value());
	assert(!NormalizeAdminItemResult(Json{ { "opId", 7 }, { "ok", true }, { "applied", -1 }, { "error", "" } }).has_value());
	assert(!NormalizeAdminItemResult(Json{ { "opId", 7 }, { "ok", true }, { "applied", 1 }, { "error", std::string(257, 'e') } }).has_value());
	assert(!NormalizeAdminItemResult(Json{ { "ok", true }, { "applied", 1 }, { "error", "" } }).has_value());
	assert(!NormalizeAdminItemResult(Json{ { "opId", 7 }, { "ok", "yes" }, { "applied", 1 }, { "error", "" } }).has_value());

	return 0;
}
