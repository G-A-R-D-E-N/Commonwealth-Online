#pragma once

#include <atomic>
#include <cstdint>

#include <windows.h>

namespace F4T::Voice
{
	class WasapiRender
	{
	public:
		using FillFn = void (*)(void* user, float* interleavedStereo, std::uint32_t frames);
		using ErrorFn = void (*)(void* user, long hresult);

		WasapiRender();
		WasapiRender(const WasapiRender&) = delete;
		WasapiRender& operator=(const WasapiRender&) = delete;
		~WasapiRender();

		bool Start(FillFn onFill, ErrorFn onError, void* user);
		void Stop();
		void RequestReopen() noexcept;
		[[nodiscard]] bool OutputFailed() const noexcept;

	private:
		static DWORD WINAPI ThreadMain(void* context);
		void Run();
		void RenderWithRecovery(HANDLE audioEvent);
		HRESULT Render(HANDLE audioEvent);

		HANDLE thread_{ nullptr };
		HANDLE stopEvent_{ nullptr };
		HANDLE reopenEvent_{ nullptr };
		std::uint32_t reopenFailures_{ 0 };
		std::atomic<bool> outputFailed_{ false };
		FillFn onFill_{ nullptr };
		ErrorFn onError_{ nullptr };
		void* user_{ nullptr };
	};
}
