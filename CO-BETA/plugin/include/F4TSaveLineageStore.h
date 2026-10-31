#pragma once

#include "F4TSaveProfileLogic.h"

#include <optional>
#include <string>
#include <vector>

namespace F4T::SaveLineageStore
{
	std::vector<F4T::SaveProfileLogic::AttestedSave> LookupAttestedSaves(const std::string& a_serverKey);
	bool RecordAttestedSave(const std::string& a_serverKey, const F4T::SaveProfileLogic::AttestedSave& a_save);
	std::optional<std::string> LookupServerSecret(const std::string& a_serverKey);
	bool StoreServerSecret(const std::string& a_serverKey, const std::string& a_serverSecret);
}
