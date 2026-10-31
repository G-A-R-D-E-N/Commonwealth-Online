#include "SaveAttestationAuthority.h"

#include "DurableStateFileStore.h"
#include "F4TDurableState.h"

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace CommonwealthOnline::Server
{
	namespace
	{
		bool IsHex(std::string_view a_value) noexcept
		{
			for (const char ch : a_value) {
				if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) {
					return false;
				}
			}
			return true;
		}

		void ValidateSecret(std::string_view a_secret)
		{
			if (a_secret.size() != F4T::DurableState::ProfileSecretLength || !IsHex(a_secret)) {
				throw std::invalid_argument("A profile secret must be 32 hex characters.");
			}
		}

		std::string DeriveValidSecret(const ProfileSecretDeriver& a_deriveSecret, std::string_view a_profileId)
		{
			auto secret = a_deriveSecret(a_profileId);
			ValidateSecret(secret);
			return secret;
		}
	}

	MemorySaveAttestationAuthority::MemorySaveAttestationAuthority(ProfileSecretDeriver a_deriveSecret) :
		_deriveSecret(std::move(a_deriveSecret))
	{}

	std::string MemorySaveAttestationAuthority::EnsureProfileSecret(std::string_view a_profileId)
	{
		const std::scoped_lock lock(_gate);
		auto& record = _profiles[std::string{ a_profileId }];
		if (record.secret.empty()) {
			record.secret = DeriveValidSecret(_deriveSecret, a_profileId);
		}
		return record.secret;
	}

	std::string MemorySaveAttestationAuthority::ProfileSecret(std::string_view a_profileId) const
	{
		return DeriveValidSecret(_deriveSecret, a_profileId);
	}

	bool MemorySaveAttestationAuthority::IsKnownProfile(std::string_view a_profileId) const
	{
		const std::scoped_lock lock(_gate);
		const auto found = _profiles.find(std::string{ a_profileId });
		return found != _profiles.end() && (!found->second.secret.empty() || !found->second.attestations.empty());
	}

	std::vector<F4T::SaveProfileLogic::AttestedSave> MemorySaveAttestationAuthority::GetSaveAttestations(
		std::string_view a_profileId) const
	{
		const std::scoped_lock lock(_gate);
		const auto found = _profiles.find(std::string{ a_profileId });
		return found == _profiles.end() ? std::vector<F4T::SaveProfileLogic::AttestedSave>{}
		                                : found->second.attestations;
	}

	bool MemorySaveAttestationAuthority::RecordSaveAttestation(
		std::string_view a_profileId,
		const F4T::SaveProfileLogic::AttestedSave& a_save)
	{
		F4T::DurableState::ValidateSaveAttestation(a_save);
		const std::scoped_lock lock(_gate);
		auto& attestations = _profiles[std::string{ a_profileId }].attestations;
		for (auto entry = attestations.begin(); entry != attestations.end(); ++entry) {
			if (F4T::SaveProfileLogic::EqualsIgnoreCase(entry->saveFile, a_save.saveFile)) {
				attestations.erase(entry);
				break;
			}
		}
		attestations.push_back(a_save);
		if (attestations.size() > F4T::DurableState::MaximumSaveAttestationsPerProfile) {
			attestations.erase(
				attestations.begin(),
				attestations.begin() +
					static_cast<std::ptrdiff_t>(attestations.size() - F4T::DurableState::MaximumSaveAttestationsPerProfile));
		}
		return true;
	}

	DurableSaveAttestationAuthority::DurableSaveAttestationAuthority(
		DurableStateFileStore& a_store,
		ProfileSecretDeriver a_deriveSecret) :
		_store(a_store),
		_deriveSecret(std::move(a_deriveSecret))
	{}

	std::string DurableSaveAttestationAuthority::EnsureProfileSecret(std::string_view a_profileId)
	{
		const auto secret = DeriveValidSecret(_deriveSecret, a_profileId);
		(void)_store.EnsureProfileSecret(a_profileId, secret);
		return secret;
	}

	std::string DurableSaveAttestationAuthority::ProfileSecret(std::string_view a_profileId) const
	{
		return DeriveValidSecret(_deriveSecret, a_profileId);
	}

	bool DurableSaveAttestationAuthority::IsKnownProfile(std::string_view a_profileId) const
	{
		return _store.GetProfileSecret(a_profileId).has_value() || !_store.GetSaveAttestations(a_profileId).empty();
	}

	std::vector<F4T::SaveProfileLogic::AttestedSave> DurableSaveAttestationAuthority::GetSaveAttestations(
		std::string_view a_profileId) const
	{
		return _store.GetSaveAttestations(a_profileId);
	}

	bool DurableSaveAttestationAuthority::RecordSaveAttestation(
		std::string_view a_profileId,
		const F4T::SaveProfileLogic::AttestedSave& a_save)
	{
		return _store.RecordSaveAttestation(a_profileId, a_save);
	}
}
