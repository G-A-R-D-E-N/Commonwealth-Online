#include "F4TVoiceWasapiCapture.h"

// NOGDI empties wingdi.h, so keep mmreg.h's bitmap structs (BITMAPINFOHEADER) out; audio does not use them.
#define NOBITMAP
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmreg.h>
#include <objbase.h>
#include <wrl/client.h>

#include <memory>
#include <new>

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

		HRESULT DrainPackets(
			IAudioCaptureClient* a_capture,
			const std::int16_t* a_silence,
			WasapiCapture::SamplesFn a_onSamples,
			void* a_user,
			HANDLE a_stop)
		{
			UINT32 packetFrames = 0;
			HRESULT hr = a_capture->GetNextPacketSize(&packetFrames);
			while (SUCCEEDED(hr) && packetFrames > 0 && WaitForSingleObject(a_stop, 0) != WAIT_OBJECT_0) {
				BYTE* data = nullptr;
				UINT32 frames = 0;
				DWORD flags = 0;
				hr = a_capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
				if (FAILED(hr)) {
					return hr;
				}
				const std::int16_t* samples =
					(flags & AUDCLNT_BUFFERFLAGS_SILENT) ? a_silence : reinterpret_cast<const std::int16_t*>(data);
				a_onSamples(a_user, samples, frames);
				hr = a_capture->ReleaseBuffer(frames);
				if (FAILED(hr)) {
					return hr;
				}
				hr = a_capture->GetNextPacketSize(&packetFrames);
			}
			return hr;
		}
	}

	WasapiCapture::WasapiCapture()
	{
		reopenEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	}

	WasapiCapture::~WasapiCapture()
	{
		Stop();
		if (reopenEvent_) {
			CloseHandle(reopenEvent_);
		}
	}

	bool WasapiCapture::Start(
		SamplesFn a_onSamples,
		TickFn a_onTick,
		ErrorFn a_onError,
		ReopenFn a_onReopen,
		void* a_user)
	{
		if (thread_ && WaitForSingleObject(thread_, 0) == WAIT_OBJECT_0) {
			Stop();
		}
		if (thread_ || !reopenEvent_ || !a_onSamples || !a_onTick || !a_onError || !a_onReopen) {
			return false;
		}
		ResetEvent(reopenEvent_);
		stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!stopEvent_) {
			return false;
		}
		onSamples_ = a_onSamples;
		onTick_ = a_onTick;
		onError_ = a_onError;
		onReopen_ = a_onReopen;
		user_ = a_user;
		thread_ = CreateThread(nullptr, 0, ThreadMain, this, 0, nullptr);
		if (!thread_) {
			CloseHandle(stopEvent_);
			stopEvent_ = nullptr;
			return false;
		}
		return true;
	}

	void WasapiCapture::Stop()
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

	void WasapiCapture::RequestReopen() noexcept
	{
		if (reopenEvent_) {
			SetEvent(reopenEvent_);
		}
	}

	DWORD WINAPI WasapiCapture::ThreadMain(void* a_context)
	{
		static_cast<WasapiCapture*>(a_context)->Run();
		return 0;
	}

	void WasapiCapture::Run()
	{
		const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(comResult)) {
			onError_(user_, comResult);
			return;
		}
		DWORD taskIndex = 0;
		const HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);
		const HANDLE audioEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (audioEvent) {
			CaptureWithRecovery(audioEvent);
			CloseHandle(audioEvent);
		} else {
			onError_(user_, E_FAIL);
		}
		if (mmcss) {
			AvRevertMmThreadCharacteristics(mmcss);
		}
		CoUninitialize();
	}

	void WasapiCapture::CaptureWithRecovery(HANDLE a_audioEvent)
	{
		const HANDLE events[] = { stopEvent_, reopenEvent_ };
		reopenFailures_ = 0;
		bool retry = false;
		for (;;) {
			if (retry) {
				onReopen_(user_);
			}
			const HRESULT captured = Capture(a_audioEvent);
			if (captured == S_OK) {
				return;
			}
			retry = true;
			if (captured == kReopenRequested) {
				continue;
			}
			onError_(user_, captured);
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

	HRESULT WasapiCapture::Capture(HANDLE a_audioEvent)
	{
		Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
		HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
		if (FAILED(hr)) {
			return hr;
		}
		Microsoft::WRL::ComPtr<IMMDevice> device;
		hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
		if (FAILED(hr)) {
			return hr;
		}
		Microsoft::WRL::ComPtr<IAudioClient> client;
		hr = device->Activate(
			__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.ReleaseAndGetAddressOf()));
		if (FAILED(hr)) {
			return hr;
		}

		WAVEFORMATEX format{};
		format.wFormatTag = WAVE_FORMAT_PCM;
		format.nChannels = 1;
		format.nSamplesPerSec = kSampleRate;
		format.wBitsPerSample = 16;
		format.nBlockAlign = 2;
		format.nAvgBytesPerSec = kSampleRate * 2;
		hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, kStreamFlags, kBufferDuration, 0, &format, nullptr);
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
		Microsoft::WRL::ComPtr<IAudioCaptureClient> capture;
		hr = client->GetService(IID_PPV_ARGS(&capture));
		if (FAILED(hr)) {
			return hr;
		}
		std::unique_ptr<std::int16_t[]> silence(new (std::nothrow) std::int16_t[bufferFrames]());
		if (!silence) {
			return E_OUTOFMEMORY;
		}
		hr = client->Start();
		if (FAILED(hr)) {
			return hr;
		}
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
			hr = DrainPackets(capture.Get(), silence.get(), onSamples_, user_, stopEvent_);
			if (FAILED(hr) || WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0) {
				break;
			}
			onTick_(user_);
		}
		const HRESULT stopped = client->Stop();
		if (FAILED(hr)) {
			return hr;
		}
		return FAILED(stopped) ? stopped : hr;
	}
}
