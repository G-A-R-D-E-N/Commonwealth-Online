#pragma once

#include "F4TAdmission.h"
#include "ServerConfiguration.h"

#include "co_gns_server_bridge.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace CommonwealthOnline::Server
{
	enum class ModpackFileRequest
	{
		notRequest,
		served,
		started,
		refused
	};

	using ModpackFileDisconnectHandler = std::function<void(std::uint32_t, const char*)>;

	void StartModpackFileCatalog(const Configuration::Options& a_options);

	[[nodiscard]] ModpackFileRequest ServeModpackFilesIfRequested(
		co_gns_server_handle a_server,
		std::uint32_t a_connectionId,
		std::span<const std::uint8_t> a_message,
		const Configuration::Options& a_options,
		double a_unixTimeSeconds,
		const std::string& a_remoteAddress,
		const ModpackFileDisconnectHandler& a_disconnect,
		F4T::Admission::AdmissionRegistry& a_admissions,
		std::uint64_t& a_admissionTicket);

	void AdvanceModpackFileDeliveries(
		co_gns_server_handle a_server,
		double a_unixTimeSeconds,
		const ModpackFileDisconnectHandler& a_disconnect);

	void ForgetModpackFileDelivery(
		co_gns_server_handle a_server,
		std::uint32_t a_connectionId);
}
