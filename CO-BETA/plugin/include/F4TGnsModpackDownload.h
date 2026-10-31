#pragma once

#include "F4TModpackAutoFix.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

namespace F4T::GnsModpackDownload
{
	class GnsTransport final : public ModpackAutoFix::Transport
	{
	public:
		GnsTransport(std::string a_host, std::uint16_t a_port, std::optional<std::string> a_serverPassword, const std::atomic_bool* a_cancel);
		~GnsTransport() override;

		[[nodiscard]] std::optional<ModpackDownload::Offer> FetchOffer(const ModpackDownload::OfferRequest& a_request) override;
		[[nodiscard]] std::optional<ModpackDownload::FileEnd> FetchFile(const ModpackDownload::FileRequest& a_request, const ChunkHandler& a_onChunk) override;
		[[nodiscard]] const std::string& LastRefusal() const noexcept;

	private:
		std::string host;
		std::uint16_t port{};
		std::optional<std::string> serverPassword;
		const std::atomic_bool* cancel{};
		std::string lastRefusal;
	};
}
