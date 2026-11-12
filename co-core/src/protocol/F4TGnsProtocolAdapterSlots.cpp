#include "F4TGnsProtocolAdapterSlots.h"
#include "F4TGnsProtocolAdapterJson.h"
#include "F4TCharacterSlots.h"

#include <nlohmann/json.hpp>

namespace F4T::GnsProtocolAdapter::Detail
{
	void AddSlotProtocolToHello(nlohmann::json& a_hello)
	{
		a_hello["slotProtocol"] = 1;
	}

	bool ForwardCharacterSlotsToWelcome(const nlohmann::json& a_sessionReady, nlohmann::json& a_welcome, std::string_view a_account)
	{
		if (const auto slots = a_sessionReady.find("characterSlots"); slots != a_sessionReady.end()) {
			a_welcome["characterSlots"] = *slots;
		}
		const auto selected = a_sessionReady.find("persistenceProfile");
		if (selected == a_sessionReady.end()) { a_welcome.erase("persistenceProfile"); return true; }
		if (a_account.empty() || !selected->is_string()) return false;
		const auto slot = F4T::CharacterSlots::ParseSlotKey(selected->get_ref<const std::string&>());
		const auto slots = a_sessionReady.find("characterSlots");
		if (!slot || slot->first != a_account || slots == a_sessionReady.end() || !slots->is_object()) return false;
		const auto count = ReadUnsigned32(*slots, "slots");
		if (!count || !F4T::CharacterSlots::IsValidSlot(slot->second, *count) ||
			ReadBoolean(*slots, "bound") != true || ReadUnsigned32(*slots, "selected") != slot->second) return false;
		a_welcome["persistenceProfile"] = *selected;
		return true;
	}
}
