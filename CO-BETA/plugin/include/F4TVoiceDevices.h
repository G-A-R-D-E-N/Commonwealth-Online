#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <atomic>
#include <functional>

namespace F4T::Voice
{
	class DeviceWatcher final : public IMMNotificationClient
	{
	public:
		DeviceWatcher(EDataFlow a_flow, std::function<void()> a_onChange);
		DeviceWatcher(const DeviceWatcher&) = delete;
		DeviceWatcher& operator=(const DeviceWatcher&) = delete;
		~DeviceWatcher();

		bool Register();
		void Unregister();

		STDMETHODIMP QueryInterface(REFIID a_riid, void** a_object) override;
		STDMETHODIMP_(ULONG) AddRef() override;
		STDMETHODIMP_(ULONG) Release() override;
		STDMETHODIMP OnDeviceStateChanged(LPCWSTR a_deviceId, DWORD a_newState) override;
		STDMETHODIMP OnDeviceAdded(LPCWSTR a_deviceId) override;
		STDMETHODIMP OnDeviceRemoved(LPCWSTR a_deviceId) override;
		STDMETHODIMP OnDefaultDeviceChanged(EDataFlow a_flow, ERole a_role, LPCWSTR a_deviceId) override;
		STDMETHODIMP OnPropertyValueChanged(LPCWSTR a_deviceId, const PROPERTYKEY a_key) override;

	private:
		void Signal() const;

		EDataFlow m_flow;
		std::function<void()> m_onChange;
		std::atomic<ULONG> m_refs{ 1 };
		Microsoft::WRL::ComPtr<IMMDeviceEnumerator> m_enumerator;
	};
}
