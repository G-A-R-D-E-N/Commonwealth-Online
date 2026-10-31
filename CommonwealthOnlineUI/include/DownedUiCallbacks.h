#pragma once

namespace F4T::DownedUi
{
	using RespawnChosenFn = void (*)(const char* a_point, const char* a_friendId);

	void Initialize();
	void EnsureViews();
	void AllowCreateRetry();
	[[nodiscard]] bool DidCreateFail() noexcept;
	void SetRespawnChosenCallback(RespawnChosenFn a_callback) noexcept;
	void PushStateJson(const char* a_json);
}
