#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(CO_GNS_BRIDGE_BUILD)
#define CO_GNS_API __declspec(dllexport)
#else
#define CO_GNS_API __declspec(dllimport)
#endif
#else
#define CO_GNS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void* co_gns_server_handle;
typedef void* co_gns_client_handle;

enum co_gns_event_type
{
    CO_GNS_EVENT_NONE = 0,
    CO_GNS_EVENT_CONNECTED = 1,
    CO_GNS_EVENT_DISCONNECTED = 2,
    CO_GNS_EVENT_MESSAGE = 3,
    CO_GNS_EVENT_OVERSIZE_MESSAGE = 4
};

enum co_gns_delivery
{
    CO_GNS_DELIVERY_UNRELIABLE_SEQUENCED = 0,
    CO_GNS_DELIVERY_RELIABLE_ORDERED = 1
};

enum co_gns_send_result
{
    CO_GNS_SEND_ERROR = -1,
    CO_GNS_SEND_SENT = 0,
    CO_GNS_SEND_DROPPED = 1,
    CO_GNS_SEND_BACKPRESSURE = 2,
    CO_GNS_SEND_NOT_CONNECTED = 3,
    CO_GNS_SEND_TOO_LARGE = 4
};

typedef struct co_gns_event
{
    uint32_t type;
    uint32_t connection_id;
    int32_t reason;
    uint32_t payload_size;
    char debug[128];
} co_gns_event;

CO_GNS_API int co_gns_server_create(
    const char* bind_host,
    uint16_t port,
    co_gns_server_handle* out_handle,
    char* error_buffer,
    size_t error_buffer_size);

CO_GNS_API void co_gns_server_destroy(co_gns_server_handle handle);
CO_GNS_API uint16_t co_gns_server_local_port(co_gns_server_handle handle);
CO_GNS_API uint32_t co_gns_server_connection_count(co_gns_server_handle handle);

CO_GNS_API int co_gns_server_poll(
    co_gns_server_handle handle,
    co_gns_event* out_event,
    void* payload_buffer,
    uint32_t payload_capacity);

CO_GNS_API int co_gns_server_send(
    co_gns_server_handle handle,
    uint32_t connection_id,
    const void* payload,
    uint32_t payload_size,
    uint32_t delivery);

// Consulted while a connection is still in the Connecting state, before the
// transport admits it. Return non-zero to admit, zero to refuse. Invoked on the
// thread that calls co_gns_server_poll.
// reason_buffer receives the refusal text handed to the peer when the callback
// returns zero.
typedef int (*co_gns_admission_callback)(
    void* user,
    uint32_t ipv4_host_order,
    uint16_t port,
    char* reason_buffer,
    size_t reason_buffer_size);

CO_GNS_API int co_gns_server_set_admission_callback(
    co_gns_server_handle handle,
    co_gns_admission_callback callback,
    void* user);

// Consulted instead of the IPv4 callback for peers that have no IPv4 address.
// address is the peer's IPv6 address as text, without brackets or port. When an
// IPv4 admission callback is set and this one is not, such peers are refused.
typedef int (*co_gns_address_admission_callback)(
    void* user,
    const char* address,
    uint16_t port,
    char* reason_buffer,
    size_t reason_buffer_size);

CO_GNS_API int co_gns_server_set_address_admission_callback(
    co_gns_server_handle handle,
    co_gns_address_admission_callback callback,
    void* user);

CO_GNS_API int co_gns_server_disconnect(
    co_gns_server_handle handle,
    uint32_t connection_id,
    int32_t reason,
    const char* debug);

CO_GNS_API int co_gns_server_remote_ipv4(
    co_gns_server_handle handle,
    uint32_t connection_id,
    uint32_t* out_ipv4_host_order,
    uint16_t* out_port);

// Writes the peer address as text (dotted IPv4, or IPv6 without brackets).
// Returns 1 on success, 0 if the connection is unknown or the buffer is too small.
CO_GNS_API int co_gns_server_remote_address(
    co_gns_server_handle handle,
    uint32_t connection_id,
    char* out_address,
    size_t out_address_size,
    uint16_t* out_port);

CO_GNS_API int co_gns_client_create(
    const char* remote_host,
    uint16_t port,
    co_gns_client_handle* out_handle,
    char* error_buffer,
    size_t error_buffer_size);

CO_GNS_API void co_gns_client_destroy(co_gns_client_handle handle);

CO_GNS_API int co_gns_client_poll(
    co_gns_client_handle handle,
    co_gns_event* out_event,
    void* payload_buffer,
    uint32_t payload_capacity);

CO_GNS_API int co_gns_client_send(
    co_gns_client_handle handle,
    const void* payload,
    uint32_t payload_size,
    uint32_t delivery);

CO_GNS_API int co_gns_client_disconnect(
    co_gns_client_handle handle,
    int32_t reason,
    const char* debug);

// Test-only symbols are exported only by bridges configured with
// CO_GNS_ENABLE_TEST_NETWORK=ON. The declarations stay visible so dedicated
// smoke-test clients can compile against the same public header; shipped
// bridges still omit the symbols entirely.
CO_GNS_API int co_gns_test_set_network_conditions(
    float packet_loss_percent,
    float packet_reorder_percent);

CO_GNS_API int co_gns_test_get_network_conditions(
    float* out_packet_loss_percent,
    float* out_packet_reorder_percent);

#ifdef __cplusplus
}
#endif
