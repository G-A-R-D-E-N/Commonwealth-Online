#include "F4TInventoryGrantDelivery.h"
#include "F4TDeathBagGrantLedger.h"

#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

int main()
{
	{
		using F4T::DeathBagGrantLedger::Entry;
		std::map<std::string, Entry> entries{ { "saved", Entry{ 0, true } }, { "not-added", Entry{ 0, false } }, { "later", Entry{ 0, true } } };
		std::deque<std::string> order{ "saved", "not-added", "later" };
		std::set<std::string> unsaved{ "saved", "not-added", "later" };
		const std::set<std::string> saving{ "saved", "not-added", "later" };
		F4T::DeathBagReceipts::ReceiptSet frozen(false);
		assert(frozen.Insert("saved") && frozen.Insert("not-added"));
		for (const auto& operation : F4T::DeathBagJournal::SavedRemovals(entries, saving, frozen)) {
			entries.erase(operation);
			unsaved.erase(operation);
		}
		std::erase_if(order, [&](const auto& operation) { return !entries.contains(operation); });
		assert(!entries.contains("saved") && entries.contains("not-added") && entries.contains("later"));
		assert(!unsaved.contains("saved") && unsaved.contains("not-added") && unsaved.contains("later"));
		assert((order == std::deque<std::string>{ "not-added", "later" }));
	}
	using Json = nlohmann::json;
	const Json items{ { "a", { { "count", 2u } } }, { "b", { { "count", 3u } } } };
	int first = 1;
	int second = 2;
	std::vector<int> delivered;
	const auto add = [&](int* a_item, std::uint32_t a_count) { delivered.push_back(*a_item * static_cast<int>(a_count)); };
	const auto missing = [&](const std::string& a_key) -> int* { return a_key == "a" ? &first : nullptr; };
	assert(!F4T::InventoryGrantDelivery::Apply(items, missing, add));
	assert(delivered.empty());
	const auto resolve = [&](const std::string& a_key) -> int* { return a_key == "a" ? &first : &second; };
	assert(F4T::InventoryGrantDelivery::Apply(items, resolve, add));
	assert((delivered == std::vector<int>{ 2, 6 }));
	delivered.clear();
	bool interrupted = false;
	try {
		(void)F4T::InventoryGrantDelivery::Apply(items, resolve, [&](int* a_item, std::uint32_t a_count) {
			if (!delivered.empty()) throw std::runtime_error("interrupted delivery");
			add(a_item, a_count);
		});
	} catch (const std::runtime_error&) { interrupted = true; }
	assert(interrupted && (delivered == std::vector<int>{ 2 }));
}
