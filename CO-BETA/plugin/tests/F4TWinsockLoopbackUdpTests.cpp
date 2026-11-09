#include "F4TWinsockLoopbackUdp.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

namespace
{
	using F4T::Transport::UdpDatagram;

	constexpr std::uint16_t kNeverOpenedPort = 1;
	constexpr int kMaximumAttempts = 400;

	std::vector<UdpDatagram> DrainUntil(F4T::Transport::UdpSocketApi& a_udp, std::uint16_t a_port, std::size_t a_wanted)
	{
		std::vector<UdpDatagram> drained;
		for (int attempt = 0; attempt < kMaximumAttempts && drained.size() < a_wanted; ++attempt) {
			auto batch = a_udp.receive(a_port);
			if (batch.empty()) {
				std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
				continue;
			}
			drained.insert(drained.end(), batch.begin(), batch.end());
		}
		return drained;
	}
}

int main()
{
	using F4T::Steam::MakeWinsockLoopbackUdp;

	auto udp = MakeWinsockLoopbackUdp();
	assert(udp.IsComplete());

	const auto first = udp.openLoopback();
	const auto second = udp.openLoopback();
	assert(first.has_value());
	assert(second.has_value());
	assert(*first != 0);
	assert(*second != 0);
	assert(*first != *second);

	const std::vector<std::uint8_t> payload{ 1, 2, 3, 4, 5 };
	assert(udp.sendTo(*first, *second, payload));

	const auto delivered = DrainUntil(udp, *second, 1);
	assert(delivered.size() == 1);
	assert(delivered[0].sourcePort == *first);
	assert(delivered[0].payload == payload);

	const std::vector<std::uint8_t> oversize(3000, 7);
	assert(udp.sendTo(*first, *second, oversize));
	assert(udp.sendTo(*first, *second, payload));

	const auto afterOversize = DrainUntil(udp, *second, 1);
	assert(afterOversize.size() == 1);
	assert(afterOversize[0].payload == payload);

	const auto closing = udp.openLoopback();
	assert(closing.has_value());
	udp.close(*closing);
	udp.close(*closing);

	udp.sendTo(*first, *closing, payload);
	udp.sendTo(*first, *second, payload);
	const auto afterClosedTarget = DrainUntil(udp, *second, 1);
	assert(afterClosedTarget.size() == 1);
	assert(afterClosedTarget[0].payload == payload);
	assert(udp.receive(*closing).empty());

	assert(!udp.sendTo(*closing, *second, payload));
	assert(!udp.sendTo(kNeverOpenedPort, *second, payload));
	assert(udp.receive(kNeverOpenedPort).empty());

	{
		auto scoped = MakeWinsockLoopbackUdp();
		const auto port = scoped.openLoopback();
		assert(port.has_value());
		assert(scoped.sendTo(*port, *first, payload));
		assert(!DrainUntil(udp, *first, 1).empty());
	}

	udp.close(*first);
	udp.close(*second);

	return 0;
}
