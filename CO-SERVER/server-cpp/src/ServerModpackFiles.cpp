#include "ServerModpackFiles.h"

#include "F4TIdentityProtocol.h"
#include "F4TModpackDownload.h"
#include "F4TModpackManifest.h"
#include "F4TModpackServe.h"
#include "ServerRuntimeSupport.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace CommonwealthOnline::Server
{
	namespace
	{
		constexpr double kIdleTimeoutSeconds = 30.0;
		constexpr std::size_t kChunksPerTick = 16;

		struct DeliveryKey
		{
			co_gns_server_handle server{};
			std::uint32_t connectionId{};

			[[nodiscard]] bool operator<(const DeliveryKey& a_other) const noexcept
			{
				if (server != a_other.server) {
					return server < a_other.server;
				}
				return connectionId < a_other.connectionId;
			}
		};

		struct FileDelivery
		{
			F4T::ModpackServe::CatalogEntry entry;
			std::string requestedName;
			std::string address;
			std::ifstream stream;
			std::uint64_t nextOffset{};
			std::optional<std::string> pendingPayload;
			std::uint64_t pendingBytes{};
			bool endSent{};
			double lastProgressUnixSeconds{};
		};

		std::mutex g_catalogMutex;
		std::shared_ptr<const F4T::ModpackServe::Catalog> g_catalog;
		std::atomic_bool g_catalogStarted{ false };
		std::unique_ptr<F4T::ModpackServe::Limiter> g_limiter;
		std::map<DeliveryKey, FileDelivery> g_deliveries;

		enum class SendOutcome
		{
			sent,
			backpressure,
			failed
		};

		SendOutcome SendBytes(co_gns_server_handle a_server, std::uint32_t a_connectionId, const std::string& a_payload)
		{
			switch (co_gns_server_send(
				a_server,
				a_connectionId,
				a_payload.data(),
				static_cast<std::uint32_t>(a_payload.size()),
				static_cast<std::uint32_t>(CO_GNS_DELIVERY_RELIABLE_ORDERED))) {
			case CO_GNS_SEND_SENT:
				return SendOutcome::sent;
			case CO_GNS_SEND_BACKPRESSURE:
				return SendOutcome::backpressure;
			default:
				return SendOutcome::failed;
			}
		}

		SendOutcome SendJson(co_gns_server_handle a_server, std::uint32_t a_connectionId, const nlohmann::json& a_packet)
		{
			const auto payload = RuntimeSupport::JsonBytes(a_packet);
			return SendBytes(a_server, a_connectionId, std::string{ payload.begin(), payload.end() });
		}

		void Refuse(
			co_gns_server_handle a_server,
			std::uint32_t a_connectionId,
			double a_unixTimeSeconds,
			std::string_view a_code,
			std::string_view a_reason,
			const ModpackFileDisconnectHandler& a_disconnect)
		{
			(void)SendJson(a_server, a_connectionId, nlohmann::json{
				{ "type", "sessionEnded" },
				{ "code", a_code },
				{ "reason", a_reason },
				{ "serverTime", a_unixTimeSeconds }
			});
			a_disconnect(a_connectionId, std::string{ a_reason }.c_str());
		}

		std::shared_ptr<const F4T::ModpackServe::Catalog> CurrentCatalog()
		{
			const std::scoped_lock lock(g_catalogMutex);
			return g_catalog;
		}

		void ReleaseAdmission(F4T::Admission::AdmissionRegistry& a_admissions, std::uint64_t& a_admissionTicket)
		{
			if (a_admissionTicket != 0) {
				(void)a_admissions.Reject(a_admissionTicket, "modpack_file_request");
				a_admissionTicket = 0;
			}
		}
	}

	void StartModpackFileCatalog(const Configuration::Options& a_options)
	{
		if (!a_options.allowModpackDownloads || !a_options.requiredModpackManifestJson || !a_options.requiredModpackFingerprint ||
			!a_options.modpackDownloadDirectory || g_catalogStarted.exchange(true)) {
			return;
		}
		g_limiter = std::make_unique<F4T::ModpackServe::Limiter>(a_options.modpackDownloadConcurrency, a_options.modpackDownloadBytesPerHourPerAddress);
		std::thread([manifestJson = *a_options.requiredModpackManifestJson,
						fingerprint = *a_options.requiredModpackFingerprint,
						directory = std::filesystem::path{ *a_options.modpackDownloadDirectory },
						downloadable = a_options.modpackDownloadable]() {
			try {
				const auto manifest = F4T::ModpackManifest::ParseManifest(manifestJson);
				auto catalog = std::make_shared<F4T::ModpackServe::Catalog>(manifest ?
						F4T::ModpackServe::BuildCatalog(*manifest, fingerprint, directory, downloadable) :
						F4T::ModpackServe::Catalog{ fingerprint, {}, { { {}, "the required modpack manifest is invalid" } } });
				std::cout << "Modpack downloads: " << catalog->entries.size() << " file(s) ready to serve from " << directory.string() << std::endl;
				for (const auto& problem : catalog->problems) {
					std::cerr << "Modpack downloads: " << (problem.name.empty() ? std::string{ "folder" } : problem.name) << " not served: " << problem.reason << std::endl;
				}
				const std::scoped_lock lock(g_catalogMutex);
				g_catalog = std::move(catalog);
			} catch (...) {
				std::cerr << "Modpack downloads: the download folder could not be prepared." << std::endl;
			}
		}).detach();
	}

	ModpackFileRequest ServeModpackFilesIfRequested(
		co_gns_server_handle a_server,
		std::uint32_t a_connectionId,
		std::span<const std::uint8_t> a_message,
		const Configuration::Options& a_options,
		double a_unixTimeSeconds,
		const std::string& a_remoteAddress,
		const ModpackFileDisconnectHandler& a_disconnect,
		F4T::Admission::AdmissionRegistry& a_admissions,
		std::uint64_t& a_admissionTicket)
	{
		if (a_message.empty() || a_message.front() != '{') {
			return ModpackFileRequest::notRequest;
		}
		const auto hello = nlohmann::json::parse(
			std::string_view{ reinterpret_cast<const char*>(a_message.data()), a_message.size() },
			nullptr,
			false);
		if (hello.is_discarded() || !hello.is_object()) {
			return ModpackFileRequest::notRequest;
		}
		const bool wantsOffer = hello.contains(std::string{ F4T::ModpackDownload::kOfferRequestField });
		const bool wantsFile = hello.contains(std::string{ F4T::ModpackDownload::kFileRequestField });
		const auto type = hello.find("type");
		if ((!wantsOffer && !wantsFile) || type == hello.end() || !type->is_string() || type->get_ref<const std::string&>() != "hello") {
			return ModpackFileRequest::notRequest;
		}
		const auto code = F4T::ModpackDownload::kUnavailableCode;
		ReleaseAdmission(a_admissions, a_admissionTicket);
		if (!a_options.allowModpackDownloads || !a_options.requiredModpackFingerprint) {
			Refuse(a_server, a_connectionId, a_unixTimeSeconds, code, "This server does not offer modpack downloads.", a_disconnect);
			return ModpackFileRequest::refused;
		}
		if (const auto passwordRejection = F4T::Identity::CheckServerPassword(hello, Configuration::IdentityRequirements(a_options))) {
			Refuse(a_server, a_connectionId, a_unixTimeSeconds, passwordRejection->code, passwordRejection->reason, a_disconnect);
			return ModpackFileRequest::refused;
		}
		const auto catalog = CurrentCatalog();
		if (!catalog || !g_limiter) {
			Refuse(a_server, a_connectionId, a_unixTimeSeconds, code, "The server is still preparing its modpack downloads. Try again in a minute.", a_disconnect);
			return ModpackFileRequest::refused;
		}

		if (wantsOffer) {
			const auto request = F4T::ModpackDownload::ParseOfferRequest(hello);
			if (!request || request->fingerprint != *a_options.requiredModpackFingerprint) {
				Refuse(a_server, a_connectionId, a_unixTimeSeconds, code, "The modpack download request was not valid for this server.", a_disconnect);
				return ModpackFileRequest::refused;
			}
			const auto offer = F4T::ModpackServe::BuildOffer(*catalog, *request);
			(void)SendJson(a_server, a_connectionId, F4T::ModpackDownload::OfferPacket(offer));
			a_disconnect(a_connectionId, "Modpack offer served");
			return ModpackFileRequest::served;
		}

		const auto request = F4T::ModpackDownload::ParseFileRequest(hello);
		const auto* entry = request && request->fingerprint == *a_options.requiredModpackFingerprint ?
			F4T::ModpackServe::FindEntry(*catalog, request->name) :
			nullptr;
		if (entry == nullptr || entry->sha256Hex != request->sha256Hex || request->offset > entry->sizeBytes || !F4T::ModpackServe::StillMatches(*entry)) {
			Refuse(a_server, a_connectionId, a_unixTimeSeconds, code, "The server does not offer that file.", a_disconnect);
			return ModpackFileRequest::refused;
		}
		const DeliveryKey key{ a_server, a_connectionId };
		if (g_deliveries.contains(key)) {
			return ModpackFileRequest::started;
		}
		if (!g_limiter->TryStart(a_remoteAddress, a_unixTimeSeconds)) {
			Refuse(a_server, a_connectionId, a_unixTimeSeconds, code, "The server is busy or your download limit for this hour is used up. Try again later.", a_disconnect);
			return ModpackFileRequest::refused;
		}
		FileDelivery delivery;
		delivery.entry = *entry;
		delivery.requestedName = request->name;
		delivery.address = a_remoteAddress;
		delivery.nextOffset = request->offset;
		delivery.lastProgressUnixSeconds = a_unixTimeSeconds;
		delivery.stream.open(entry->path, std::ios::binary);
		if (delivery.stream) {
			delivery.stream.seekg(static_cast<std::streamoff>(request->offset));
		}
		if (!delivery.stream) {
			g_limiter->Finish(a_remoteAddress);
			Refuse(a_server, a_connectionId, a_unixTimeSeconds, code, "The server could not read that file.", a_disconnect);
			return ModpackFileRequest::refused;
		}
		g_deliveries.emplace(key, std::move(delivery));
		return ModpackFileRequest::started;
	}

	void AdvanceModpackFileDeliveries(
		co_gns_server_handle a_server,
		double a_unixTimeSeconds,
		const ModpackFileDisconnectHandler& a_disconnect)
	{
		for (auto it = g_deliveries.begin(); it != g_deliveries.end();) {
			if (it->first.server != a_server) {
				++it;
				continue;
			}
			auto& delivery = it->second;
			const auto connectionId = it->first.connectionId;
			std::string failure;
			bool finished = false;
			for (std::size_t sent = 0; sent < kChunksPerTick && failure.empty() && !finished; ++sent) {
				if (!delivery.pendingPayload) {
					if (delivery.nextOffset >= delivery.entry.sizeBytes) {
						if (delivery.endSent) {
							finished = true;
							break;
						}
						const auto end = F4T::ModpackDownload::EndPacket({ delivery.requestedName, delivery.entry.sha256Hex, delivery.entry.sizeBytes });
						const auto payload = RuntimeSupport::JsonBytes(end);
						delivery.pendingPayload = std::string{ payload.begin(), payload.end() };
						delivery.pendingBytes = 0;
					} else {
						std::string bytes(static_cast<std::size_t>(std::min<std::uint64_t>(F4T::ModpackDownload::kChunkPayloadBytes, delivery.entry.sizeBytes - delivery.nextOffset)), '\0');
						delivery.stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
						if (delivery.stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
							failure = "The server could not read the rest of the file.";
							break;
						}
						const auto packet = F4T::ModpackDownload::ChunkPacket(delivery.requestedName, delivery.nextOffset, bytes);
						const auto payload = RuntimeSupport::JsonBytes(packet);
						delivery.pendingPayload = std::string{ payload.begin(), payload.end() };
						delivery.pendingBytes = bytes.size();
					}
				}
				const auto outcome = SendBytes(a_server, connectionId, *delivery.pendingPayload);
				if (outcome == SendOutcome::backpressure) {
					break;
				}
				if (outcome == SendOutcome::failed) {
					failure = "The server could not send the file.";
					break;
				}
				delivery.lastProgressUnixSeconds = a_unixTimeSeconds;
				if (delivery.pendingBytes == 0) {
					delivery.endSent = true;
					finished = true;
				} else {
					delivery.nextOffset += delivery.pendingBytes;
					(void)g_limiter->Charge(delivery.address, delivery.pendingBytes, a_unixTimeSeconds);
				}
				delivery.pendingPayload.reset();
			}
			if (failure.empty() && !finished && a_unixTimeSeconds - delivery.lastProgressUnixSeconds > kIdleTimeoutSeconds) {
				failure = "The file download stalled.";
			}
			if (!failure.empty() || finished) {
				g_limiter->Finish(delivery.address);
				it = g_deliveries.erase(it);
				if (finished) {
					a_disconnect(connectionId, "Modpack file served");
				} else {
					Refuse(a_server, connectionId, a_unixTimeSeconds, F4T::ModpackDownload::kUnavailableCode, failure, a_disconnect);
				}
				continue;
			}
			++it;
		}
	}

	void ForgetModpackFileDelivery(
		co_gns_server_handle a_server,
		std::uint32_t a_connectionId)
	{
		const auto found = g_deliveries.find(DeliveryKey{ a_server, a_connectionId });
		if (found == g_deliveries.end()) {
			return;
		}
		if (g_limiter) {
			g_limiter->Finish(found->second.address);
		}
		g_deliveries.erase(found);
	}
}
