#pragma once

#include "F4TDurableState.h"
#include <cassert>
#include <string>

inline void ReconcileStartingGrantFixture(nlohmann::json& a_root, const std::string& a_profile)
{
	const F4T::SaveProfileLogic::AttestedSave before{ "Save_fixture_1", std::string(64, 'a'), 0 };
	const F4T::SaveProfileLogic::AttestedSave after{ "Save_fixture_2", std::string(64, 'b'), 0 };
	F4T::DurableState::RecordSaveAttestation(a_root, a_profile.substr(0, a_profile.find('#')), before, 1000.0);
	assert(F4T::DurableState::BeginStartingGrant(a_root, a_profile, "fixture", before));
	assert(F4T::DurableState::ResolveStartingGrant(a_root, a_profile, "fixture", after, "test fixture", false));
}
