#include "F4TVoiceDevices.h"

#include <objbase.h>

#include <utility>

namespace F4T::Voice
{
	DeviceWatcher::DeviceWatcher(EDataFlow a_flow, std::function<void()> a_onChange) : m_flow(a_flow), m_onChange(std::move(a_onChange)) {}

	DeviceWatcher::~DeviceWatcher()
	{
		Unregister();
	}

	bool DeviceWatcher::Register()
	{
		if (m_enumerator) {
			return true;
		}
		Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
		HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
		if (FAILED(hr)) {
			return false;
		}
		hr = enumerator->RegisterEndpointNotificationCallback(this);
		if (FAILED(hr)) {
			return false;
		}
		m_enumerator = enumerator;
		return true;
	}

	void DeviceWatcher::Unregister()
	{
		if (m_enumerator) {
			m_enumerator->UnregisterEndpointNotificationCallback(this);
			m_enumerator.Reset();
		}
	}

	STDMETHODIMP DeviceWatcher::QueryInterface(REFIID a_riid, void** a_object)
	{
		if (!a_object) {
			return E_POINTER;
		}
		if (a_riid == __uuidof(IUnknown) || a_riid == __uuidof(IMMNotificationClient)) {
			*a_object = static_cast<IMMNotificationClient*>(this);
			AddRef();
			return S_OK;
		}
		*a_object = nullptr;
		return E_NOINTERFACE;
	}

	STDMETHODIMP_(ULONG) DeviceWatcher::AddRef()
	{
		return ++m_refs;
	}

	STDMETHODIMP_(ULONG) DeviceWatcher::Release()
	{
		return --m_refs;
	}

	STDMETHODIMP DeviceWatcher::OnDeviceStateChanged(LPCWSTR, DWORD a_newState)
	{
		if (a_newState == DEVICE_STATE_ACTIVE) {
			Signal();
		}
		return S_OK;
	}

	STDMETHODIMP DeviceWatcher::OnDeviceAdded(LPCWSTR)
	{
		return S_OK;
	}

	STDMETHODIMP DeviceWatcher::OnDeviceRemoved(LPCWSTR)
	{
		return S_OK;
	}

	STDMETHODIMP DeviceWatcher::OnDefaultDeviceChanged(EDataFlow a_flow, ERole a_role, LPCWSTR)
	{
		if (a_flow == m_flow && a_role == eConsole) {
			Signal();
		}
		return S_OK;
	}

	STDMETHODIMP DeviceWatcher::OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY)
	{
		return S_OK;
	}

	void DeviceWatcher::Signal() const
	{
		if (m_onChange) {
			m_onChange();
		}
	}
}
