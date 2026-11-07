#include "ServerPacketDecode.h"

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace CommonwealthOnline::Server;
using namespace CommonwealthOnline::Server::PacketDecode;

namespace
{
	std::vector<std::uint8_t> Bytes(std::string_view a_text)
	{
		return { a_text.begin(), a_text.end() };
	}

	OutboundFrame Frame(std::uint32_t a_connectionId, std::string_view a_text)
	{
		OutboundFrame frame;
		frame.connectionId = a_connectionId;
		frame.payload = Bytes(a_text);
		return frame;
	}

	void TestDecodeJson()
	{
		assert(DecodeJson(Bytes(R"({"type":"hello"})")));
		assert(!DecodeJson(std::vector<std::uint8_t>{}));
		assert(!DecodeJson(Bytes("[1]")));
		assert(!DecodeJson(Bytes("{not json")));
	}

	void TestRawPacketType()
	{
		assert(RawPacketType(Bytes(R"({"type":"hello"})")) == std::optional<std::string>{ "hello" });
		assert(!RawPacketType(Bytes(R"({"type":3})")));
		assert(!RawPacketType(Bytes("[]")));
	}

	void TestJsonBytes()
	{
		assert(JsonBytes(nlohmann::json{ { "a", 1 } }) == Bytes(R"({"a":1})"));
	}

	void TestPlayerIdFromFrames()
	{
		const std::vector<OutboundFrame> frames{
			Frame(4, R"({"type":"hello","playerId":9})"),
			Frame(4, R"({"type":"sessionReady","playerId":7})")
		};
		assert(PlayerIdFromFrames(frames, 4) == 7U);
		assert(!PlayerIdFromFrames(frames, 5));
		assert(!PlayerIdFromFrames(std::vector<OutboundFrame>{ Frame(4, R"({"type":"sessionReady","playerId":0})") }, 4));
		assert(!PlayerIdFromFrames(std::vector<OutboundFrame>{ Frame(4, R"({"type":"sessionReady","playerId":true})") }, 4));
		assert(!PlayerIdFromFrames(std::vector<OutboundFrame>{ Frame(4, R"({"type":"sessionReady","playerId":4294967296})") }, 4));
	}

	void TestFramesContainType()
	{
		const std::vector<OutboundFrame> frames{ Frame(4, R"({"type":"sessionReady","playerId":7})") };
		assert(FramesContainType(frames, 4, "sessionReady"));
		assert(!FramesContainType(frames, 5, "sessionReady"));
		assert(!FramesContainType(frames, 4, "hello"));
	}
}

int main()
{
	TestDecodeJson();
	TestRawPacketType();
	TestJsonBytes();
	TestPlayerIdFromFrames();
	TestFramesContainType();
	return 0;
}
