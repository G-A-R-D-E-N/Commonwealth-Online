#include "ServerAdminMenuDrain.h"
#include "ServerAdminMenu.h"
#include <exception>

namespace CommonwealthOnline::Server
{
void DrainAdminMenuActions(ProtocolHost& a_protocol, const AdminCommandDispatcher& a_dispatcher, const AdminFrameSender& a_send)
{
    for (auto& pending : a_protocol.TakePendingAdminActions()) {
        if (!a_protocol.IsAdminActionAuthorized(pending)) {
            a_protocol.RejectAdminAction(pending, "not-admin");
            continue;
        }
        std::vector<OutboundFrame> frames;
        try {
            frames = a_protocol.CompleteAdminAction(pending, a_dispatcher.Dispatch(AdminMenuService::DispatchRequest(pending.action)));
        } catch (const std::exception& a_error) {
            frames = a_protocol.FailAdminAction(pending, a_error.what());
        } catch (...) {
            frames = a_protocol.FailAdminAction(pending, "unknown exception");
        }
        a_send(frames);
    }
}
}
