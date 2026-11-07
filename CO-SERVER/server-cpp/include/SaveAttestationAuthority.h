#pragma once

#include "F4TSaveProfileLogic.h"

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CommonwealthOnline::Server
{
	class DurableStateFileStore;

	using ProfileSecretDeriver = std::function<std::string(std::string_view)>;

	class SaveAttestationAuthority
	{
	public:
		virtual ~SaveAttestationAuthority() = default;

		[[nodiscard]] virtual std::string EnsureProfileSecret(std::string_view a_profileId) = 0;
		[[nodiscard]] virtual std::string ProfileSecret(std::string_view a_profileId) const = 0;
		[[nodiscard]] virtual bool IsKnownProfile(std::string_view a_profileId) const = 0;
		[[nodiscard]] virtual std::vector<F4T::SaveProfileLogic::AttestedSave> GetSaveAttestations(
			std::string_view a_profileId) const = 0;
		[[nodiscard]] virtual bool RecordSaveAttestation(
			std::string_view a_profileId,
			const F4T::SaveProfileLogic::AttestedSave& a_save) = 0;
	};

	class MemorySaveAttestationAuthority final : public SaveAttestationAuthority
	{
	public:
		explicit MemorySaveAttestationAuthority(ProfileSecretDeriver a_deriveSecret);

		[[nodiscard]] std::string EnsureProfileSecret(std::string_view a_profileId) override;
		[[nodiscard]] std::string ProfileSecret(std::string_view a_profileId) const override;
		[[nodiscard]] bool IsKnownProfile(std::string_view a_profileId) const override;
		[[nodiscard]] std::vector<F4T::SaveProfileLogic::AttestedSave> GetSaveAttestations(
			std::string_view a_profileId) const override;
		[[nodiscard]] bool RecordSaveAttestation(
			std::string_view a_profileId,
			const F4T::SaveProfileLogic::AttestedSave& a_save) override;

	private:
		struct ProfileRecord
		{
			std::string secret;
			std::vector<F4T::SaveProfileLogic::AttestedSave> attestations;
		};

		mutable std::mutex _gate;
		ProfileSecretDeriver _deriveSecret;
		std::unordered_map<std::string, ProfileRecord> _profiles;
	};

	class DurableSaveAttestationAuthority final : public SaveAttestationAuthority
	{
	public:
		DurableSaveAttestationAuthority(DurableStateFileStore& a_store, ProfileSecretDeriver a_deriveSecret);

		[[nodiscard]] std::string EnsureProfileSecret(std::string_view a_profileId) override;
		[[nodiscard]] std::string ProfileSecret(std::string_view a_profileId) const override;
		[[nodiscard]] bool IsKnownProfile(std::string_view a_profileId) const override;
		[[nodiscard]] std::vector<F4T::SaveProfileLogic::AttestedSave> GetSaveAttestations(
			std::string_view a_profileId) const override;
		[[nodiscard]] bool RecordSaveAttestation(
			std::string_view a_profileId,
			const F4T::SaveProfileLogic::AttestedSave& a_save) override;

	private:
		DurableStateFileStore& _store;
		ProfileSecretDeriver _deriveSecret;
	};
}
