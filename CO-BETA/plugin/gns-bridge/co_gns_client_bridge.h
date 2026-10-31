#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#if defined(CO_GNS_BRIDGE_BUILD)
#define CO_GNS_API __declspec(dllexport)
#else
#define CO_GNS_API __declspec(dllimport)
#endif
#else
#define CO_GNS_API __attribute__((visibility("default")))
#endif

extern "C"
{
	using co_gns_client_handle = void*;

	enum co_gns_event_type : std::uint32_t
	{
		CO_GNS_EVENT_NONE = 0,
		CO_GNS_EVENT_CONNECTED = 1,
		CO_GNS_EVENT_DISCONNECTED = 2,
		CO_GNS_EVENT_MESSAGE = 3,
		CO_GNS_EVENT_OVERSIZE_MESSAGE = 4
	};

	enum co_gns_delivery : std::uint32_t
	{
		CO_GNS_DELIVERY_UNRELIABLE_SEQUENCED = 0,
		CO_GNS_DELIVERY_RELIABLE_ORDERED = 1
	};

	enum co_gns_send_result : int
	{
		CO_GNS_SEND_ERROR = -1,
		CO_GNS_SEND_SENT = 0,
		CO_GNS_SEND_DROPPED = 1,
		CO_GNS_SEND_BACKPRESSURE = 2,
		CO_GNS_SEND_NOT_CONNECTED = 3,
		CO_GNS_SEND_TOO_LARGE = 4
	};

	struct co_gns_event
	{
		std::uint32_t type;
		std::uint32_t connection_id;
		std::int32_t reason;
		std::uint32_t payload_size;
		char debug[128];
	};

	CO_GNS_API int co_gns_client_create(
		const char* remote_host,
		std::uint16_t port,
		co_gns_client_handle* out_handle,
		char* error_buffer,
		std::size_t error_buffer_size);

	CO_GNS_API void co_gns_client_destroy(co_gns_client_handle handle);

	CO_GNS_API int co_gns_client_poll(
		co_gns_client_handle handle,
		co_gns_event* out_event,
		void* payload_buffer,
		std::uint32_t payload_capacity);

	CO_GNS_API int co_gns_client_send(
		co_gns_client_handle handle,
		const void* payload,
		std::uint32_t payload_size,
		std::uint32_t delivery);

	CO_GNS_API int co_gns_client_disconnect(
		co_gns_client_handle handle,
		std::int32_t reason,
		const char* debug);
}
