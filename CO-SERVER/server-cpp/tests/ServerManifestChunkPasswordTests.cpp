#include "F4TIdentityProtocol.h"
#include "ServerConfiguration.h"

#include <cassert>
#include <string>

namespace
{
	using namespace CommonwealthOnline::Server;

	nlohmann::json ManifestRequest(const std::string* a_password)
	{
		nlohmann::json request{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "capabilities", nlohmann::json::array({ "modpack-manifest-chunks-v1" }) },
			{ "requestModpackManifest", std::string(64, 'a') }
		};
		if (a_password != nullptr) {
			request["serverPassword"] = *a_password;
		}
		return request;
	}
}

int main()
{
	const Configuration::Options open;
	assert(!F4T::Identity::CheckServerPassword(ManifestRequest(nullptr), Configuration::IdentityRequirements(open)));

	Configuration::Options passworded;
	passworded.serverPassword = std::string{ "correct-horse" };
	const auto requirements = Configuration::IdentityRequirements(passworded);
	const std::string wrong{ "wrong-horse" };
	const std::string correct{ "correct-horse" };

	const auto missing = F4T::Identity::CheckServerPassword(ManifestRequest(nullptr), requirements);
	assert(missing && missing->code == "password_required");
	const auto invalid = F4T::Identity::CheckServerPassword(ManifestRequest(&wrong), requirements);
	assert(invalid && invalid->code == "password_invalid");
	assert(!F4T::Identity::CheckServerPassword(ManifestRequest(&correct), requirements));
	return 0;
}
