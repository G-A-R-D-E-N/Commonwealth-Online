#include "F4TVoiceRelay.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace F4T::Voice
{
	namespace
	{
		[[nodiscard]] bool IsActive(const TalkerEntry& a_entry, double a_nowSeconds) noexcept
		{
			return a_entry.used && a_nowSeconds - a_entry.lastFrameSeconds <= VoiceTalkerActiveSeconds;
		}

		[[nodiscard]] double PeerDistance(const VoicePeer& a_from, const VoicePeer& a_to) noexcept
		{
			const auto dx = a_from.scope->x - a_to.scope->x;
			const auto dy = a_from.scope->y - a_to.scope->y;
			const auto dz = a_from.z - a_to.z;
			if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz)) {
				return std::numeric_limits<double>::infinity();
			}
			return std::hypot(dx, dy, dz);
		}
	}

	bool AdmitTalker(
		TalkerSlots& a_slots,
		std::uint16_t a_speakerId,
		double a_distance,
		double a_nowSeconds) noexcept
	{
		if (!std::isfinite(a_distance) || !std::isfinite(a_nowSeconds)) {
			return false;
		}
		for (auto& entry : a_slots) {
			if (entry.used && entry.speakerId == a_speakerId) {
				entry.lastFrameSeconds = a_nowSeconds;
				entry.distance = a_distance;
				return true;
			}
		}
		for (auto& entry : a_slots) {
			if (!IsActive(entry, a_nowSeconds)) {
				entry = TalkerEntry{ a_speakerId, a_nowSeconds, a_distance, true };
				return true;
			}
		}
		auto* victim = &a_slots.front();
		for (auto& entry : a_slots) {
			if (entry.distance > victim->distance || (entry.distance == victim->distance && entry.speakerId > victim->speakerId)) {
				victim = &entry;
			}
		}
		if (a_distance < victim->distance && a_distance <= VoiceTalkerReplaceRatio * victim->distance) {
			*victim = TalkerEntry{ a_speakerId, a_nowSeconds, a_distance, true };
			return true;
		}
		return false;
	}

	bool VoiceInRange(const VoicePeer& a_from, const VoicePeer& a_to, Tier a_tier) noexcept
	{
		return F4T::InterestScope::StatesShareInterest(a_from.scope, a_to.scope) && PeerDistance(a_from, a_to) <= TierRange(a_tier);
	}

	bool VoiceBlocked(const VoicePeer& a_sender, const VoicePeer& a_listener) noexcept
	{
		const auto end = a_listener.blocked.begin() + std::min<std::size_t>(a_listener.blockedCount, a_listener.blocked.size());
		return std::find(a_listener.blocked.begin(), end, a_sender.playerId) != end;
	}

	VoiceRecipients SelectVoiceRecipients(
		const VoicePeer& a_sender,
		Tier a_tier,
		double a_nowSeconds,
		std::span<const VoicePeer> a_listeners,
		std::span<TalkerSlots> a_talkerSlots) noexcept
	{
		if (a_listeners.size() > MaxVoiceRecipients || a_talkerSlots.size() != a_listeners.size()) {
			return {};
		}

		VoiceRecipients result{};
		for (std::size_t i = 0; i < a_listeners.size(); ++i) {
			const auto& listener = a_listeners[i];
			if (!listener.active || !listener.voiceV1 || !listener.canPlay || listener.deafened) {
				continue;
			}
			if (listener.playerId == a_sender.playerId || listener.profile != a_sender.profile) {
				continue;
			}
			if (VoiceBlocked(a_sender, listener) || !VoiceInRange(a_sender, listener, a_tier)) {
				continue;
			}
			if (!AdmitTalker(a_talkerSlots[i], a_sender.playerId, PeerDistance(a_sender, listener), a_nowSeconds)) {
				continue;
			}
			result.playerIds[result.count] = listener.playerId;
			++result.count;
		}
		return result;
	}
}
