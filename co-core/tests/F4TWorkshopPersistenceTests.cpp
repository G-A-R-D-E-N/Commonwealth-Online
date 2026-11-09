#include "F4TWorkshopPersistence.h"

#include <nlohmann/json.hpp>

#include <cassert>

int main()
{
	using Json = nlohmann::json;
	using F4T::WorkshopPersistence::CurrentSchemaVersion;
	using F4T::WorkshopPersistence::MigrateWorkshopRoot;

	{
		Json root{ { "version", 1 }, { "zones", Json{ { "7", Json::object() } } } };
		assert(MigrateWorkshopRoot(root));
		assert(root["version"] == CurrentSchemaVersion);
		assert(root["zones"].contains("7"));
		assert(root["meta"]["dirtyFlushSeconds"] == 1);
	}

	{
		Json root{ { "version", 2 }, { "zones", Json::object() } };
		assert(MigrateWorkshopRoot(root));
	}

	{
		Json root{ { "version", 3 }, { "zones", Json::object() } };
		assert(!MigrateWorkshopRoot(root));
	}

	{
		Json root{ { "version", 0 }, { "zones", Json::object() } };
		assert(!MigrateWorkshopRoot(root));
	}

	{
		Json root{ { "version", "2" }, { "zones", Json::object() } };
		assert(!MigrateWorkshopRoot(root));
	}

	{
		Json root{ { "version", 2 }, { "zones", Json::array() } };
		assert(!MigrateWorkshopRoot(root));
	}

	{
		Json root = Json::array();
		assert(!MigrateWorkshopRoot(root));
	}

	{
		Json root = Json::object();
		assert(MigrateWorkshopRoot(root));
		assert(root["version"] == CurrentSchemaVersion);
	}

	return 0;
}
