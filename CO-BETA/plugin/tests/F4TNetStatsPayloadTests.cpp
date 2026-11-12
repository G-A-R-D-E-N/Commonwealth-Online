#include "F4TNetStatsPayload.h"

#include <cassert>
#include <limits>
#include <string>

int main()
{
	using namespace F4T::NetStatsPayload;

	assert(EffectivePingMs(42, false) == 42);
	assert(EffectivePingMs(0, false) == 0);
	assert(EffectivePingMs(-1, false) == -1);
	assert(EffectivePingMs(42, true) == -1);
	assert(EffectivePingMs(0, true) == -1);

	const Sample sample{ 42, 1.0F, 0.5F, 2048.0F, 1024.0F, 10.0F, 20.0F, 300 };
	assert(BuildJson(sample, false) ==
		R"({"pingMs":42,"qualityLocal":100,"qualityRemote":50,"inKBps":2.0,"outKBps":1.0,"inPps":10.0,"outPps":20.0,"pendingBytes":300})");
	assert(BuildJson(sample, true).starts_with(R"({"pingMs":-1,)"));
	assert(BuildJson(sample, true).find(R"("pingMs":42)") == std::string::npos);

	const Sample unsafe{ 7, std::numeric_limits<float>::quiet_NaN(), 2.0F, std::numeric_limits<float>::infinity(), 0.0F, 0.0F, 0.0F, -9 };
	assert(BuildJson(unsafe, false) ==
		R"({"pingMs":7,"qualityLocal":0,"qualityRemote":100,"inKBps":0.0,"outKBps":0.0,"inPps":0.0,"outPps":0.0,"pendingBytes":0})");

	return 0;
}
