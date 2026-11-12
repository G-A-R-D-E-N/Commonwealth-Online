#pragma once

#include <cstdint>

#include <windows.h>

namespace F4T::Voice
{
	inline constexpr DWORD kReopenFirstDelayMs = 1000;
	inline constexpr DWORD kReopenRetryDelayMs = 2000;

	[[nodiscard]] constexpr DWORD ReopenDelayMs(std::uint32_t a_failedAttempts) noexcept
	{
		return a_failedAttempts == 0 ? kReopenFirstDelayMs : kReopenRetryDelayMs;
	}

	class WasapiCapture
	{
	public:
		using SamplesFn = void (*)(void* user, const std::int16_t* mono, std::uint32_t frames);
		using TickFn = void (*)(void* user);
		using ErrorFn = void (*)(void* user, long hresult);
		using ReopenFn = void (*)(void* user);

		WasapiCapture();
		WasapiCapture(const WasapiCapture&) = delete;
		WasapiCapture& operator=(const WasapiCapture&) = delete;
		~WasapiCapture();

		bool Start(SamplesFn onSamples, TickFn onTick, ErrorFn onError, ReopenFn onReopen, void* user);
		void Stop();
		void RequestReopen() noexcept;

	private:
		static DWORD WINAPI ThreadMain(void* context);
		void Run();
		void CaptureWithRecovery(HANDLE audioEvent);
		HRESULT Capture(HANDLE audioEvent);

		HANDLE thread_{ nullptr };
		HANDLE stopEvent_{ nullptr };
		HANDLE reopenEvent_{ nullptr };
		std::uint32_t reopenFailures_{ 0 };
		SamplesFn onSamples_{ nullptr };
		TickFn onTick_{ nullptr };
		ErrorFn onError_{ nullptr };
		ReopenFn onReopen_{ nullptr };
		void* user_{ nullptr };
	};
}
