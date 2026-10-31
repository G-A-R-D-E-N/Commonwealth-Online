#include "ServerManifestChunkQueue.h"

#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

namespace
{
	using namespace CommonwealthOnline::Server;

	struct ScriptedSends
	{
		std::vector<ManifestChunkSendResult> results;
		std::size_t nextResult{};
		std::vector<ManifestChunkPacket> packets;
		std::vector<std::size_t> indexes;

		ManifestChunkSendResult operator()(ManifestChunkPacket a_packet, std::size_t a_index)
		{
			packets.push_back(a_packet);
			indexes.push_back(a_index);
			if (nextResult >= results.size()) {
				return ManifestChunkSendResult::sent;
			}
			return results[nextResult++];
		}
	};

	ManifestChunkStep Advance(ManifestChunkQueue& a_queue, double a_now, ScriptedSends& a_script)
	{
		return AdvanceManifestChunkQueue(a_queue, a_now, [&](ManifestChunkPacket a_packet, std::size_t a_index) {
			return a_script(a_packet, a_index);
		});
	}

	void TestBlockedChunkIsRetriedNotSkipped()
	{
		ManifestChunkQueue queue{ .chunkCount = 2, .deadlineUnixSeconds = 10.0 };
		ScriptedSends script{ .results = { ManifestChunkSendResult::blocked } };
		assert(Advance(queue, 0.0, script) == ManifestChunkStep::blocked);
		assert(queue.nextChunk == 0);
		assert(script.indexes.size() == 1 && script.indexes[0] == 0);
		assert(Advance(queue, 0.0, script) == ManifestChunkStep::complete);
		assert(queue.nextChunk == 2 && queue.endAccepted);
		assert(script.indexes.size() == 4);
		assert(script.indexes[0] == 0 && script.indexes[1] == 0 && script.indexes[2] == 1 && script.indexes[3] == 2);
		assert(script.packets[3] == ManifestChunkPacket::endMarker);
	}

	void TestBlockedEndMarkerIsRetried()
	{
		ManifestChunkQueue queue{ .chunkCount = 1, .deadlineUnixSeconds = 10.0 };
		ScriptedSends script{ .results = { ManifestChunkSendResult::sent, ManifestChunkSendResult::blocked } };
		assert(Advance(queue, 0.0, script) == ManifestChunkStep::blocked);
		assert(queue.nextChunk == 1 && !queue.endAccepted);
		assert(Advance(queue, 0.0, script) == ManifestChunkStep::complete);
		assert(queue.endAccepted);
		assert(script.packets.size() == 3);
		assert(script.packets[1] == ManifestChunkPacket::endMarker && script.packets[2] == ManifestChunkPacket::endMarker);
	}

	void TestAllChunksThenEndInOnePass()
	{
		ManifestChunkQueue queue{ .chunkCount = 64, .deadlineUnixSeconds = 10.0 };
		ScriptedSends script;
		assert(Advance(queue, 0.0, script) == ManifestChunkStep::complete);
		assert(queue.nextChunk == 64 && queue.endAccepted);
		assert(script.packets.size() == 65);
		assert(script.packets.back() == ManifestChunkPacket::endMarker);
	}

	void TestTimeoutFailsWithoutSending()
	{
		ManifestChunkQueue queue{ .chunkCount = 2, .deadlineUnixSeconds = 5.0 };
		ScriptedSends script;
		assert(Advance(queue, 6.0, script) == ManifestChunkStep::failed);
		assert(!queue.failureReason.empty());
		assert(queue.failureReason.find("timed out") != std::string::npos);
		assert(script.packets.empty());
		assert(Advance(queue, 0.0, script) == ManifestChunkStep::failed);
	}

	void TestHardSendFailureFailsTheDelivery()
	{
		ManifestChunkQueue queue{ .chunkCount = 2, .deadlineUnixSeconds = 10.0 };
		ScriptedSends script{ .results = { ManifestChunkSendResult::sent, ManifestChunkSendResult::failed } };
		assert(Advance(queue, 0.0, script) == ManifestChunkStep::failed);
		assert(!queue.failureReason.empty());
		assert(queue.failureReason.find("could not deliver") != std::string::npos);
		assert(queue.nextChunk == 1);
	}

	void TestPresetFailureSkipsSending()
	{
		ManifestChunkQueue queue{ .chunkCount = 2, .deadlineUnixSeconds = 10.0, .failureReason = "could not be split" };
		ScriptedSends script;
		assert(Advance(queue, 0.0, script) == ManifestChunkStep::failed);
		assert(script.packets.empty());
	}

	void TestBlockedDeliveryFailsWhenResumedPastDeadline()
	{
		ManifestChunkQueue queue{ .chunkCount = 2, .deadlineUnixSeconds = 5.0 };
		ScriptedSends script{ .results = { ManifestChunkSendResult::blocked } };
		assert(Advance(queue, 1.0, script) == ManifestChunkStep::blocked);
		assert(Advance(queue, 6.0, script) == ManifestChunkStep::failed);
		assert(queue.failureReason.find("timed out") != std::string::npos);
		assert(queue.nextChunk == 0);
		assert(script.packets.size() == 1);
	}
}

int main()
{
	TestBlockedChunkIsRetriedNotSkipped();
	TestBlockedEndMarkerIsRetried();
	TestAllChunksThenEndInOnePass();
	TestTimeoutFailsWithoutSending();
	TestHardSendFailureFailsTheDelivery();
	TestPresetFailureSkipsSending();
	TestBlockedDeliveryFailsWhenResumedPastDeadline();
	return 0;
}
