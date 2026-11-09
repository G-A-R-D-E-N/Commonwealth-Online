#include "F4TWorkshopSettlers.h"

#include <cassert>

int main()
{
	using namespace F4T::WorkshopSettlers;

	{
		Table table;
		SettlerState settler{};
		settler.settlerId = 5;
		settler.buildZoneId = 7;
		assert(table.Upsert(settler));
		settler.buildZoneId = 8;
		assert(!table.Upsert(settler));
		assert(table.Find(5) != nullptr && table.Find(5)->buildZoneId == 7);
		assert(table.Find(6) == nullptr);
		assert(table.ForZone(8).empty());
		assert(table.TotalCount() == 1);
	}
	{
		Table minting;
		assert(minting.MintSettlerId() == 1);
		assert(minting.MintSettlerId() == 2);
	}
	{
		SettlerState settler{};
		settler.settlerId = 9;
		settler.buildZoneId = 7;
		assert(!CanRecoverSettler(settler));
		settler.dead = true;
		assert(CanRecoverSettler(settler));
	}
	{
		Table restored;
		SettlerState existing{};
		existing.settlerId = 40;
		existing.buildZoneId = 7;
		assert(restored.Restore({ existing }));
		assert(restored.MintSettlerId() == 41);
	}

	return 0;
}
