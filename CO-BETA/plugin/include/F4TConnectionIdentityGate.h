#pragma once

#include <string>
#include <utility>

namespace F4T::ConnectionIdentityGate
{
	template <class ClearFn, class PrepareFn, class HoldFn, class ConnectFn>
	bool PrepareHoldConnect(
		ClearFn&& a_clear,
		PrepareFn&& a_prepare,
		HoldFn&& a_hold,
		ConnectFn&& a_connect,
		std::string& a_error)
	{
		a_clear();
		a_error.clear();
		auto identity = a_prepare(a_error);
		if (!identity) {
			return false;
		}

		a_hold(std::move(*identity));
		if (a_connect(a_error)) {
			return true;
		}

		a_clear();
		return false;
	}
}
