#include "F4TVoiceWasapiRender.h"

#include "F4TVoiceWasapiCapture.h"

// NOGDI empties wingdi.h, so keep mmreg.h's bitmap structs (BITMAPINFOHEADER) out; audio does not use them.
#define NOBITMAP
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <ksmedia.h>
#include <mmreg.h>
#include <objbase.h>
#include <wrl/client.h>

namespace F4T::Voice
{
	namespace
	{
		constexpr UINT32 kSampleRate = 48000;
		constexpr REFERENCE_TIME kBufferDuration = 100000;
		constexpr DWORD kStreamFlags =
			AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
		constexpr DWORD kWaitMilliseconds = 10;
		constexpr HRESULT kReopenRequested = S_FALSE;

		HRESULT FillBuffer(
			IAudioClient* a_client,
			IAudioRenderClient* a_render,
			UINT32 a_bufferFrames,
			WasapiRender::FillFn a_onFill,
			void* a_user)
		{
			UINT32 padding = 0;
			HRESULT hr = a_client->GetCurrentPadding(&padding);
			if (FAILED(hr)) {
				return hr;
			}
			const UINT32 frames = a_bufferFrames - padding;
			if (frames == 0) {
				return S_OK;
			}
			BYTE* data = nullptr;
			hr = a_render->GetBuffer(frames, &data);
			if (FAILED(hr)) {
				return hr;
			}
			a_onFill(a_user, reinterpret_cast<float*>(data), frames);
			return a_render->ReleaseBuffer(frames, 0);
		}
	}

	WasapiRender::WasapiRender()
	{
		reopenEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	}

	WasapiRender::~WasapiRender()
	{
		Stop();
		if (reopenEvent_) {
			CloseHandle(reopenEvent_);
		}
	}

	bool WasapiRender::Start(FillFn a_onFill, ErrorFn a_onError, void* a_user)
	{
		if (thread_ && WaitForSingleObject(thread_, 0) == WAIT_OBJECT_0) {
			Stop();
		}
		if (thread_ || !reopenEvent_ || !a_onFill || !a_onError) {
			return false;
		}
		ResetEvent(reopenEvent_);
		stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!stopEvent_) {
			return false;
		}
		onFill_ = a_onFill;
		onError_ = a_onError;
		user_ = a_user;
		thread_ = CreateThread(nullptr, 0, ThreadMain, this, 0, nullptr);
		if (!thread_) {
			CloseHandle(stopEvent_);
			stopEvent_ = nullptr;
			return false;
		}
		return true;
	}

	void WasapiRender::Stop()
	{
		if (!thread_) {
			return;
		}
		SetEvent(stopEvent_);
		if (GetThreadId(thread_) == GetCurrentThreadId()) {
			return;
		}
		WaitForSingleObject(thread_, INFINITE);
		CloseHandle(thread_);
		CloseHandle(stopEvent_);
		thread_ = nullptr;
		stopEvent_ = nullptr;
	}

	void WasapiRender::RequestReopen() noexcept
	{
		if (reopenEvent_) {
			SetEvent(reopenEvent_);
		}
	}

	bool WasapiRender::OutputFailed() const noexcept
	{
		return outputFailed_.load();
	}

	DWORD WINAPI WasapiRender::ThreadMain(void* a_context)
	{
		static_cast<WasapiRender*>(a_context)->Run();
		return 0;
	}

	void WasapiRender::Run()
	{
		const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(comResult)) {
			outputFailed_ = true;
			onError_(user_, comResult);
			return;
		}
		DWORD taskIndex = 0;
		const HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);
		const HANDLE audioEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (audioEvent) {
			RenderWithRecovery(audioEvent);
			CloseHandle(audioEvent);
		} else {
			outputFailed_ = true;
			onError_(user_, E_FAIL);
		}
		if (mmcss) {
			AvRevertMmThreadCharacteristics(mmcss);
		}
		CoUninitialize();
	}

	void WasapiRender::RenderWithRecovery(HANDLE a_audioEvent)
	{
		const HANDLE events[] = { stopEvent_, reopenEvent_ };
		reopenFailures_ = 0;
		for (;;) {
			const HRESULT rendered = Render(a_audioEvent);
			if (rendered == S_OK) {
				return;
			}
			if (rendered == kReopenRequested) {
				continue;
			}
			outputFailed_ = true;
			onError_(user_, rendered);
			const DWORD waited = WaitForMultipleObjects(2, events, FALSE, ReopenDelayMs(reopenFailures_));
			if (waited == WAIT_OBJECT_0) {
				return;
			}
			if (waited == WAIT_OBJECT_0 + 1) {
				reopenFailures_ = 0;
				continue;
			}
			++reopenFailures_;
		}
	}

	HRESULT WasapiRender::Render(HANDLE a_audioEvent)
	{
		Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
		HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
		if (FAILED(hr)) {
			return hr;
		}
		Microsoft::WRL::ComPtr<IMMDevice> device;
		hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
		if (FAILED(hr)) {
			return hr;
		}
		Microsoft::WRL::ComPtr<IAudioClient> client;
		hr = device->Activate(
			__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.ReleaseAndGetAddressOf()));
		if (FAILED(hr)) {
			return hr;
		}

		WAVEFORMATEXTENSIBLE format{};
		format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		format.Format.nChannels = 2;
		format.Format.nSamplesPerSec = kSampleRate;
		format.Format.wBitsPerSample = 32;
		format.Format.nBlockAlign = 8;
		format.Format.nAvgBytesPerSec = kSampleRate * 8;
		format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
		format.Samples.wValidBitsPerSample = 32;
		format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
		format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
		hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, kStreamFlags, kBufferDuration, 0, &format.Format, nullptr);
		if (FAILED(hr)) {
			return hr;
		}
		hr = client->SetEventHandle(a_audioEvent);
		if (FAILED(hr)) {
			return hr;
		}
		UINT32 bufferFrames = 0;
		hr = client->GetBufferSize(&bufferFrames);
		if (FAILED(hr)) {
			return hr;
		}
		Microsoft::WRL::ComPtr<IAudioRenderClient> render;
		hr = client->GetService(IID_PPV_ARGS(&render));
		if (FAILED(hr)) {
			return hr;
		}
		hr = FillBuffer(client.Get(), render.Get(), bufferFrames, onFill_, user_);
		if (FAILED(hr)) {
			return hr;
		}
		hr = client->Start();
		if (FAILED(hr)) {
			return hr;
		}
		outputFailed_ = false;
		reopenFailures_ = 0;

		const HANDLE events[] = { stopEvent_, reopenEvent_, a_audioEvent };
		for (;;) {
			const DWORD waited = WaitForMultipleObjects(3, events, FALSE, kWaitMilliseconds);
			if (waited == WAIT_OBJECT_0) {
				hr = S_OK;
				break;
			}
			if (waited == WAIT_OBJECT_0 + 1) {
				hr = kReopenRequested;
				break;
			}
			if (waited == WAIT_FAILED) {
				hr = E_FAIL;
				break;
			}
			if (waited == WAIT_TIMEOUT) {
				UINT32 padding = 0;
				hr = client->GetCurrentPadding(&padding);
				if (FAILED(hr)) {
					break;
				}
				continue;
			}
			hr = FillBuffer(client.Get(), render.Get(), bufferFrames, onFill_, user_);
			if (FAILED(hr)) {
				break;
			}
		}
		const HRESULT stopped = client->Stop();
		if (FAILED(hr) || hr == kReopenRequested) {
			return hr;
		}
		return FAILED(stopped) ? stopped : hr;
	}
}
