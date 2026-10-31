from pathlib import Path


root = Path(__file__).resolve().parents[3]
source = (root / "CO-BETA/plugin/src/F4THandshakeRejectionAlert.cpp").read_text(encoding="utf-8")

assert "g_lastRejectedServerKey" not in source

start = source.index("void ClearRejectedResumeCredentialAndRetry(const char* a_serverKey)")
end = source.index("\n\tvoid ShowHandshakeRejectionNotification", start)
fn = source[start:end]

assert "!a_serverKey" in fn
assert "!a_serverKey[0]" in fn
assert "ClearResumeCredential" in fn
assert "ShowRetryFailed" in fn
assert "return;" in fn
assert "RetryLastJoin();" in fn

empty_check = fn.index("!a_serverKey")
retry_call = fn.index("RetryLastJoin();")
assert empty_check < retry_call
assert fn.index("ShowRetryFailed") < retry_call
assert fn.index("return;") < retry_call

assert "struct RejectionAlertContext" in source
assert "g_alertContexts" in source
assert "OnAlertResponse(const char*, const char* a_actionId, void* a_userData)" in source
assert "ClearRejectedResumeCredentialAndRetry(boundServerKey.c_str());" in source
assert "presentation.id, generation" in source
assert "hasClearResume" in source
assert "g_alertContexts.erase(generation);" in source

class RejectionAlertLifecycleModel:
    def __init__(self):
        self.generation = 0
        self.contexts = {}
        self.cleared = []
        self.retried = []
        self.failed = []

    def show_rejection(self, code: str, server_key: str, ui_available: bool = True, has_last_join: bool = True, show_succeeds: bool = True):
        if not ui_available:
            return None
        has_clear_resume = (code == "invalid_resume" and has_last_join)
        gen = 0
        if has_clear_resume:
            self.generation += 1
            gen = self.generation
            self.contexts[gen] = server_key
        if not show_succeeds:
            if gen > 0:
                self.contexts.pop(gen, None)
            return None
        alert_id = f"session-resume-invalid-{gen}" if gen > 0 else "session-rejected"
        return {"id": alert_id, "user_data": gen}

    def on_alert_response(self, action_id: str, user_data: int, task_interface_available: bool = True):
        bound_key = None
        if user_data in self.contexts:
            bound_key = self.contexts.pop(user_data)
        if not action_id or not task_interface_available:
            return
        if action_id == "clear_resume":
            self.clear_rejected_and_retry(bound_key)

    def clear_rejected_and_retry(self, server_key: str | None):
        if not server_key:
            self.failed.append("no_key")
            return
        self.cleared.append(server_key)
        self.retried.append(server_key)

model = RejectionAlertLifecycleModel()

assert model.show_rejection("invalid_resume", "server-1", ui_available=False) is None
assert len(model.contexts) == 0

alert_no_retry = model.show_rejection("invalid_resume", "server-1", has_last_join=False)
assert alert_no_retry is not None
assert alert_no_retry["user_data"] == 0
assert len(model.contexts) == 0

assert model.show_rejection("invalid_resume", "server-1", show_succeeds=False) is None
assert len(model.contexts) == 0

alert_task_fail = model.show_rejection("invalid_resume", "server-1")
assert alert_task_fail is not None
assert len(model.contexts) == 1
model.on_alert_response("clear_resume", alert_task_fail["user_data"], task_interface_available=False)
assert len(model.contexts) == 0
assert len(model.cleared) == 0

alert_a = model.show_rejection("invalid_resume", "server-alpha")
alert_b = model.show_rejection("invalid_resume", "server-beta")

assert alert_a["id"] == f"session-resume-invalid-{alert_a['user_data']}"
assert alert_b["id"] == f"session-resume-invalid-{alert_b['user_data']}"
assert model.contexts[alert_a["user_data"]] == "server-alpha"
assert model.contexts[alert_b["user_data"]] == "server-beta"

model.on_alert_response("clear_resume", alert_a["user_data"])
assert model.cleared == ["server-alpha"]
assert model.retried == ["server-alpha"]
assert alert_a["user_data"] not in model.contexts
assert model.contexts[alert_b["user_data"]] == "server-beta"

model.on_alert_response("clear_resume", alert_b["user_data"])
assert model.cleared == ["server-alpha", "server-beta"]
assert model.retried == ["server-alpha", "server-beta"]
assert len(model.contexts) == 0

model.on_alert_response("clear_resume", alert_a["user_data"])
assert len(model.failed) == 1
assert model.cleared == ["server-alpha", "server-beta"]
assert model.retried == ["server-alpha", "server-beta"]

print("Handshake rejection action contract tests passed")
