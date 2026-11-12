#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <unordered_map>

namespace CommonwealthOnline::Server
{
	class LiveSettingsDelivery
	{
	public:
		void Queue(std::uint32_t a_connection, const nlohmann::json& a_packet)
		{
			_pending.insert_or_assign(a_connection, a_packet);
		}

		void Erase(std::uint32_t a_connection) { _pending.erase(a_connection); }

		template <typename Send>
		void Flush(Send&& a_send)
		{
			for (auto it = _pending.begin(); it != _pending.end();) {
				if (a_send(it->first, it->second)) it = _pending.erase(it);
				else ++it;
			}
		}

	private:
		std::unordered_map<std::uint32_t, nlohmann::json> _pending;
	};
}
