#include "pch.h"

#include "F4TStateBagSync.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TDebugLogging.h"
#include "F4TNetworkingInternal.h"
#include "F4TStateBagProtocol.h"

#include <mutex>
#include <set>
#include <string>

namespace F4T::StateBagSync
{
	namespace
	{
		using Json = nlohmann::json;
		namespace StateBag = F4T::StateBagProtocol;

		std::mutex            g_mutex;
		Json                  g_bags = Json::object();
		std::set<std::string> g_openResources;

		COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI16* Api()
		{
			static COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI16* api = nullptr;
			if (!api) {
				api = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
					COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI16>();
			}
			return api;
		}

		void PushBags()
		{
			auto* api = Api();
			if (!api || !api->PushStateBag) {
				return;
			}
			const auto text = g_bags.dump();
			api->PushStateBag(text.c_str());
		}

		void HandleBagPacket(const Json& a_packet)
		{
			const auto op = F4T::Networking::Detail::ReadString(a_packet, "op").value_or("");
			if (op == "snapshot") {
				const auto found = a_packet.find("bags");
				if (found != a_packet.end() && found->is_object()) {
					g_bags = *found;
				}
			} else if (op == "clear") {
				const auto bag = F4T::Networking::Detail::ReadString(a_packet, "bag");
				if (bag && StateBag::ParseBag(*bag)) {
					g_bags[*bag] = Json::object();
				}
			} else {
				const auto bag = F4T::Networking::Detail::ReadString(a_packet, "bag");
				const auto key = F4T::Networking::Detail::ReadString(a_packet, "key");
				if (!bag || !key || !StateBag::ParseBag(*bag) || !StateBag::IsValidKey(*key)) {
					return;
				}
				if (!g_bags.contains(*bag) || !g_bags[*bag].is_object()) {
					g_bags[*bag] = Json::object();
				}
				if (op == "delete") {
					if (g_bags.contains(*bag)) {
						g_bags[*bag].erase(*key);
					}
				} else if (op == "set") {
					const auto value = a_packet.find("value");
					if (value != a_packet.end()) {
						g_bags[*bag][*key] = *value;
					}
				}
			}
			PushBags();
		}

		void HandleViewPacket(const Json& a_packet)
		{
			const auto op = F4T::Networking::Detail::ReadString(a_packet, "op").value_or("");
			const auto resource = F4T::Networking::Detail::ReadString(a_packet, "resource");
			auto* api = Api();
			if (!resource || !api) {
				return;
			}
			if (op == "open") {
				const auto view = F4T::Networking::Detail::ReadString(a_packet, "view");
				if (!view || !api->OpenResourceView) {
					return;
				}
				if (api->OpenResourceView(resource->c_str(), view->c_str())) {
					g_openResources.insert(*resource);
				} else {
					F4T::DebugLogging::Log("StateBag", "could not open resource view {}", *resource);
				}
			} else if (op == "close") {
				if (api->CloseResourceView) {
					api->CloseResourceView(resource->c_str());
				}
				g_openResources.erase(*resource);
			}
		}

		void OnResourceMessage(const char* a_resource, const char* a_json)
		{
			if (!a_resource || !a_json) {
				return;
			}
			auto data = Json::parse(a_json, nullptr, false);
			if (data.is_discarded()) {
				return;
			}
			const Json packet{
				{ "type", std::string{ StateBag::kResourceMessagePacketType } },
				{ "resource", a_resource },
				{ "data", std::move(data) }
			};
			F4T::Networking::Detail::SendPacketBytes(packet.dump());
		}
	}

	bool Attach()
	{
		auto* api = Api();
		if (!api || !api->SetResourceMessageCallback) {
			return false;
		}
		api->SetResourceMessageCallback(OnResourceMessage);
		F4T::DebugLogging::Log("StateBag", "registered the V16 resource message callback");
		return true;
	}

	void Reset()
	{
		std::set<std::string> resources;
		{
			std::lock_guard lock(g_mutex);
			g_bags = Json::object();
			resources.swap(g_openResources);
		}
		auto* api = Api();
		if (!api) {
			return;
		}
		if (api->PushStateBag) {
			api->PushStateBag("{}");
		}
		if (api->CloseResourceView) {
			for (const auto& resource : resources) {
				api->CloseResourceView(resource.c_str());
			}
		}
	}

	bool TryHandlePacket(std::string_view a_type, const Json& a_packet)
	{
		if (a_type == StateBag::kPacketType) {
			std::lock_guard lock(g_mutex);
			HandleBagPacket(a_packet);
			return true;
		}
		if (a_type == StateBag::kResourceViewPacketType) {
			std::lock_guard lock(g_mutex);
			HandleViewPacket(a_packet);
			return true;
		}
		return false;
	}
}
