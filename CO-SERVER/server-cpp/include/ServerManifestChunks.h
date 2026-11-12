#pragma once

#include "F4TAdmission.h"
#include "ServerConfiguration.h"

#include "co_gns_server_bridge.h"

#include <cstdint>
#include <functional>
#include <span>

namespace CommonwealthOnline::Server
{
	class IdentityGate;
	enum class ManifestChunkRequest
	{
		notRequest,
		started,
		alreadyServing,
		refused
	};

	using ManifestChunkDisconnectHandler = std::function<void(std::uint32_t, const char*)>;

	[[nodiscard]] ManifestChunkRequest ServeManifestChunksIfRequested(
		co_gns_server_handle a_server,
		std::uint32_t a_connectionId,
		std::span<const std::uint8_t> a_message,
		const Configuration::Options& a_options,
		double a_unixTimeSeconds,
		const ManifestChunkDisconnectHandler& a_disconnect,
		F4T::Admission::AdmissionRegistry& a_admissions,
		std::uint64_t& a_admissionTicket,
		IdentityGate* a_identity = nullptr,
		std::uint64_t a_sourceKey = 0);

	void AdvanceManifestChunkDeliveries(
		co_gns_server_handle a_server,
		double a_unixTimeSeconds,
		const ManifestChunkDisconnectHandler& a_disconnect);

	[[nodiscard]] bool HasManifestChunkDelivery(co_gns_server_handle a_server, std::uint32_t a_connectionId);

	void ForgetManifestChunkDelivery(
		co_gns_server_handle a_server,
		std::uint32_t a_connectionId);
}
