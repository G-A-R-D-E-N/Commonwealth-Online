#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace CommonwealthOnline::Server
{
	enum class ManifestChunkPacket
	{
		chunk,
		endMarker
	};

	enum class ManifestChunkSendResult
	{
		sent,
		blocked,
		failed
	};

	enum class ManifestChunkStep
	{
		blocked,
		complete,
		failed
	};

	struct ManifestChunkQueue
	{
		std::size_t chunkCount{};
		std::size_t nextChunk{};
		bool endAccepted{};
		double deadlineUnixSeconds{};
		std::string failureReason;
	};

	using ManifestChunkSendFunction = std::function<ManifestChunkSendResult(ManifestChunkPacket, std::size_t)>;

	[[nodiscard]] inline ManifestChunkStep AdvanceManifestChunkQueue(
		ManifestChunkQueue& a_queue,
		double a_unixTimeSeconds,
		const ManifestChunkSendFunction& a_sendNext)
	{
		if (!a_queue.failureReason.empty()) {
			return ManifestChunkStep::failed;
		}
		if (a_unixTimeSeconds > a_queue.deadlineUnixSeconds) {
			a_queue.failureReason = "The server's modpack manifest delivery timed out.";
			return ManifestChunkStep::failed;
		}
		while (a_queue.nextChunk < a_queue.chunkCount) {
			const auto result = a_sendNext(ManifestChunkPacket::chunk, a_queue.nextChunk);
			if (result == ManifestChunkSendResult::blocked) {
				return ManifestChunkStep::blocked;
			}
			if (result == ManifestChunkSendResult::failed) {
				a_queue.failureReason = "The server could not deliver its modpack manifest.";
				return ManifestChunkStep::failed;
			}
			++a_queue.nextChunk;
		}
		if (!a_queue.endAccepted) {
			const auto result = a_sendNext(ManifestChunkPacket::endMarker, a_queue.chunkCount);
			if (result == ManifestChunkSendResult::blocked) {
				return ManifestChunkStep::blocked;
			}
			if (result == ManifestChunkSendResult::failed) {
				a_queue.failureReason = "The server could not deliver its modpack manifest.";
				return ManifestChunkStep::failed;
			}
			a_queue.endAccepted = true;
		}
		return ManifestChunkStep::complete;
	}
}
