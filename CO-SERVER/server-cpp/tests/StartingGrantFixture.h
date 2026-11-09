#pragma once

#include "DurableStateFileStore.h"
#include <cassert>
#include <string>

inline F4T::SaveProfileLogic::AttestedSave StartingGrantFixtureSave()
{
	return F4T::SaveProfileLogic::AttestedSave{ "Save_fixture_2", std::string(64, 'b'), 0 };
}

inline F4T::SaveProfileLogic::AttestedSave ReconcileStartingGrantFixture(
	CommonwealthOnline::Server::DurableStateFileStore& a_store, const std::string& a_profile)
{
	const F4T::SaveProfileLogic::AttestedSave before{ "Save_fixture_1", std::string(64, 'a'), 0 };
	const auto after = StartingGrantFixtureSave();
	(void)a_store.RecordSaveAttestation(a_profile.substr(0, a_profile.find('#')), before);
	assert(a_store.BeginStartingGrant(a_profile, "fixture", before));
	assert(a_store.ResolveStartingGrant(a_profile, "fixture", after, "test fixture", false));
	return after;
}

inline void RearmStartingGrantFixture(CommonwealthOnline::Server::DurableStateFileStore& a_store, const std::string& a_profile)
{
	assert(a_store.ResolveStartingGrant(a_profile, "fixture", StartingGrantFixtureSave(), "test fixture", false));
}
