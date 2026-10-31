#pragma once

namespace F4T::JoinOverlay
{
	void Initialize();
	void EnsureView();
	void AllowCreateRetry();
	[[nodiscard]] bool DidCreateFail() noexcept;
	void PushStateJson(const char* a_stateJson);
	void Hide();
}
