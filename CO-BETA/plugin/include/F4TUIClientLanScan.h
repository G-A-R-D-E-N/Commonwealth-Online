#pragma once

#include <cstddef>

namespace F4T::UIClientLanScan
{
	bool ScanToJson(char* a_jsonOut, std::size_t a_jsonOutSize, char* a_errorBuf, std::size_t a_errorBufSize);
}
