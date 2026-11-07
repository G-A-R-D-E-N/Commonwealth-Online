#include "F4TVoiceRelay.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace
{
	using F4T::InterestScope::StateScope;
	using namespace F4T::Voice;

	static_assert(VoiceTalkerActiveSeconds == 0.3);
	static_assert(MaxVoiceRecipients == 16);
	static_assert(MaxVoiceBlockedPlayers == 15);
	static_assert(MaxVoiceTalkers == 4);

	constexpr std::uint16_t ListenerId = 100;

	VoicePeer MakePeer(std::uint16_t a_playerId, double a_x, double a_z = 0.0, double a_y = 0.0)
	{
		VoicePeer peer{};
		peer.playerId = a_playerId;
		peer.active = true;
		peer.voiceV1 = true;
		peer.canPlay = true;
		peer.profile = 1;
		peer.scope = StateScope{ "00000100", "0000F00D", a_x, a_y };
		peer.z = a_z;
		return peer;
	}

	VoiceRecipients Relay(
		const VoicePeer& a_sender,
		Tier a_tier,
		double a_nowSeconds,
		const VoicePeer& a_listener,
		TalkerSlots& a_slots)
	{
		return SelectVoiceRecipients(
			a_sender,
			a_tier,
			a_nowSeconds,
			std::span<const VoicePeer>(&a_listener, 1),
			std::span<TalkerSlots>(&a_slots, 1));
	}

	bool Hears(const VoiceRecipients& a_recipients, std::uint16_t a_playerId)
	{
		const auto end = a_recipients.playerIds.begin() + a_recipients.count;
		return std::find(a_recipients.playerIds.begin(), end, a_playerId) != end;
	}

	bool Reaches(const VoicePeer& a_sender, const VoicePeer& a_listener, Tier a_tier)
	{
		TalkerSlots slots{};
		return Hears(Relay(a_sender, a_tier, 1.0, a_listener, slots), a_listener.playerId);
	}
}

int main()
{
	{
		const auto sender = MakePeer(1, 0.0);
		assert(Reaches(sender, MakePeer(2, 256.0), Tier::Whisper));
		assert(!Reaches(sender, MakePeer(2, 256.001), Tier::Whisper));
		assert(Reaches(sender, MakePeer(2, 1024.0), Tier::Normal));
		assert(!Reaches(sender, MakePeer(2, 1024.001), Tier::Normal));
		assert(Reaches(sender, MakePeer(2, 2560.0), Tier::Shout));
		assert(!Reaches(sender, MakePeer(2, 2560.001), Tier::Shout));
		assert(!Reaches(sender, MakePeer(2, 300.0), Tier::Whisper));
		assert(Reaches(sender, MakePeer(2, 300.0), Tier::Normal));

		assert(Reaches(sender, MakePeer(2, 0.0, 256.0), Tier::Whisper));
		assert(!Reaches(sender, MakePeer(2, 0.0, 256.001), Tier::Whisper));
		assert(!Reaches(sender, MakePeer(2, 200.0, 200.0), Tier::Whisper));
		assert(Reaches(sender, MakePeer(2, 0.0, 1024.0), Tier::Normal));
		assert(!Reaches(sender, MakePeer(2, 0.0, 1024.001), Tier::Normal));
		assert(Reaches(sender, MakePeer(2, 0.0, 2560.0), Tier::Shout));
		assert(!Reaches(sender, MakePeer(2, 0.0, 2560.001), Tier::Shout));
	}

	{
		const auto sender = MakePeer(1, 0.0);
		assert(Reaches(sender, MakePeer(2, 0.0, 0.0, 1000.0), Tier::Normal));
		assert(!Reaches(sender, MakePeer(2, 0.0, 0.0, 1025.0), Tier::Normal));

		const auto lowSender = MakePeer(1, 0.0, 0.0, -500.0);
		assert(Reaches(lowSender, MakePeer(2, 0.0, 0.0, 500.0), Tier::Normal));
		assert(!Reaches(lowSender, MakePeer(2, 0.0, 0.0, 525.0), Tier::Normal));
	}

	{
		const auto sender = MakePeer(1, 0.0);
		auto sameWorldOtherCell = MakePeer(2, 100.0);
		sameWorldOtherCell.scope->cellId = "00000200";
		assert(Reaches(sender, sameWorldOtherCell, Tier::Normal));

		auto otherWorld = MakePeer(2, 100.0);
		otherWorld.scope->cellId = "00000400";
		otherWorld.scope->worldspaceId = "0000DEAD";
		assert(!Reaches(sender, otherWorld, Tier::Normal));

		auto interiorSender = MakePeer(1, 0.0);
		interiorSender.scope->cellId = "0000CAFE";
		interiorSender.scope->worldspaceId.clear();
		auto interiorOther = MakePeer(2, 10.0);
		interiorOther.scope->cellId = "0000BEEF";
		interiorOther.scope->worldspaceId.clear();
		assert(!Reaches(interiorSender, interiorOther, Tier::Whisper));
		interiorOther.scope->cellId = "0000CAFE";
		assert(Reaches(interiorSender, interiorOther, Tier::Whisper));
	}

	{
		const auto sender = MakePeer(1, 0.0);
		assert(Reaches(sender, MakePeer(2, 100.0), Tier::Normal));

		auto inactive = MakePeer(2, 100.0);
		inactive.active = false;
		assert(!Reaches(sender, inactive, Tier::Normal));

		auto noVoice = MakePeer(2, 100.0);
		noVoice.voiceV1 = false;
		assert(!Reaches(sender, noVoice, Tier::Normal));

		auto cannotPlay = MakePeer(2, 100.0);
		cannotPlay.canPlay = false;
		assert(!Reaches(sender, cannotPlay, Tier::Normal));

		auto deafened = MakePeer(2, 100.0);
		deafened.deafened = true;
		assert(!Reaches(sender, deafened, Tier::Normal));

		assert(!Reaches(sender, MakePeer(1, 100.0), Tier::Normal));

		auto otherProfile = MakePeer(2, 100.0);
		otherProfile.profile = 2;
		assert(!Reaches(sender, otherProfile, Tier::Normal));

		auto blocked = MakePeer(2, 100.0);
		blocked.blocked[14] = 1;
		blocked.blockedCount = 15;
		assert(!Reaches(sender, blocked, Tier::Normal));

		auto blockedBeyondCount = MakePeer(2, 100.0);
		blockedBeyondCount.blocked[14] = 1;
		blockedBeyondCount.blockedCount = 14;
		assert(Reaches(sender, blockedBeyondCount, Tier::Normal));
	}

	{
		TalkerSlots slots{};
		const auto listener = MakePeer(ListenerId, 0.0);
		for (std::uint16_t id = 1; id <= 4; ++id) {
			assert(Hears(Relay(MakePeer(id, 100.0 * id), Tier::Normal, 1.0, listener, slots), ListenerId));
		}
		assert(!Hears(Relay(MakePeer(5, 500.0), Tier::Normal, 1.0, listener, slots), ListenerId));
	}

	{
		TalkerSlots slots{};
		const auto listener = MakePeer(ListenerId, 0.0);
		for (std::uint16_t id = 5; id >= 2; --id) {
			assert(Hears(Relay(MakePeer(id, 100.0 * id), Tier::Normal, 1.0, listener, slots), ListenerId));
		}
		assert(Hears(Relay(MakePeer(1, 100.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		assert(!Hears(Relay(MakePeer(5, 500.0), Tier::Normal, 1.0, listener, slots), ListenerId));
	}

	{
		TalkerSlots slots{};
		const auto listener = MakePeer(ListenerId, 0.0);
		for (std::uint16_t id = 1; id <= 3; ++id) {
			assert(Hears(Relay(MakePeer(id, 100.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		}
		assert(Hears(Relay(MakePeer(4, 1000.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		assert(!Hears(Relay(MakePeer(5, 900.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		assert(Hears(Relay(MakePeer(6, 800.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		assert(!Hears(Relay(MakePeer(4, 1000.0), Tier::Normal, 1.0, listener, slots), ListenerId));
	}

	{
		TalkerSlots slots{};
		const auto listener = MakePeer(ListenerId, 0.0);
		for (std::size_t frame = 0; frame < 1000; ++frame) {
			const double now = 1.0 + 0.02 * static_cast<double>(frame);
			for (std::uint16_t id = 1; id <= 3; ++id) {
				assert(Hears(Relay(MakePeer(id, 100.0), Tier::Normal, now, listener, slots), ListenerId));
			}
			const std::uint16_t speaker = frame % 2 == 0 ? 10 : 11;
			assert(Hears(Relay(MakePeer(speaker, 500.0), Tier::Normal, now, listener, slots), ListenerId) == (speaker == 10));
		}
	}

	{
		TalkerSlots slots{};
		const auto listener = MakePeer(ListenerId, 0.0);
		for (std::uint16_t id = 1; id <= 4; ++id) {
			assert(Hears(Relay(MakePeer(id, 300.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		}
		assert(!Hears(Relay(MakePeer(5, 300.0), Tier::Normal, 1.2, listener, slots), ListenerId));
		for (std::uint16_t id = 2; id <= 4; ++id) {
			assert(Hears(Relay(MakePeer(id, 300.0), Tier::Normal, 1.2, listener, slots), ListenerId));
		}
		assert(Hears(Relay(MakePeer(5, 300.0), Tier::Normal, 1.4, listener, slots), ListenerId));
		assert(!Hears(Relay(MakePeer(1, 300.0), Tier::Normal, 1.4, listener, slots), ListenerId));
	}

	{
		TalkerSlots slots{};
		const auto listener = MakePeer(ListenerId, 0.0);
		for (std::uint16_t id = 1; id <= 4; ++id) {
			assert(Hears(Relay(MakePeer(id, 300.0), Tier::Normal, 0.0, listener, slots), ListenerId));
		}
		assert(!Hears(Relay(MakePeer(5, 300.0), Tier::Normal, 0.3, listener, slots), ListenerId));
		assert(Hears(Relay(MakePeer(5, 300.0), Tier::Normal, 0.31, listener, slots), ListenerId));
	}

	{
		TalkerSlots slots{};
		const auto listener = MakePeer(ListenerId, 0.0);
		assert(Hears(Relay(MakePeer(7, 500.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		assert(Hears(Relay(MakePeer(3, 500.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		assert(Hears(Relay(MakePeer(8, 100.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		assert(Hears(Relay(MakePeer(9, 100.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		assert(Hears(Relay(MakePeer(10, 100.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		assert(!Hears(Relay(MakePeer(7, 500.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		assert(Hears(Relay(MakePeer(3, 500.0), Tier::Normal, 1.0, listener, slots), ListenerId));
	}

	{
		TalkerSlots slots{};
		const auto listener = MakePeer(ListenerId, 0.0);
		for (std::uint16_t id = 1; id <= 4; ++id) {
			assert(Hears(Relay(MakePeer(id, 0.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		}
		for (std::size_t frame = 1; frame <= 100; ++frame) {
			const double now = 1.0 + 0.02 * static_cast<double>(frame);
			assert(!Hears(Relay(MakePeer(11, 0.0), Tier::Normal, now, listener, slots), ListenerId));
			for (std::uint16_t id = 1; id <= 4; ++id) {
				assert(Hears(Relay(MakePeer(id, 0.0), Tier::Normal, now, listener, slots), ListenerId));
			}
		}
	}

	{
		TalkerSlots slots{};
		const auto listener = MakePeer(ListenerId, 0.0);
		for (std::uint16_t id = 1; id <= 4; ++id) {
			assert(Hears(Relay(MakePeer(id, 1000.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		}
		assert(Hears(Relay(MakePeer(5, 850.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		assert(!Hears(Relay(MakePeer(6, 850.5), Tier::Normal, 1.0, listener, slots), ListenerId));
	}

	{
		TalkerSlots slots{};
		const auto listener = MakePeer(ListenerId, 0.0);
		assert(Hears(Relay(MakePeer(1, 1000.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		for (std::uint16_t id = 2; id <= 4; ++id) {
			assert(Hears(Relay(MakePeer(id, 300.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		}
		assert(Hears(Relay(MakePeer(1, 100.0), Tier::Normal, 1.1, listener, slots), ListenerId));
		assert(!Hears(Relay(MakePeer(5, 500.0), Tier::Normal, 1.1, listener, slots), ListenerId));
	}

	{
		TalkerSlots slots{};
		const auto listener = MakePeer(ListenerId, 0.0);
		assert(!Hears(Relay(MakePeer(20, 1100.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		for (std::uint16_t id = 1; id <= 3; ++id) {
			assert(Hears(Relay(MakePeer(id, 100.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		}
		assert(Hears(Relay(MakePeer(4, 1000.0), Tier::Normal, 1.0, listener, slots), ListenerId));
	}

	{
		std::array<TalkerSlots, 2> slots{};
		for (std::uint16_t id = 201; id <= 204; ++id) {
			assert(AdmitTalker(slots[0], id, 100.0, 1.0));
		}
		const std::array<VoicePeer, 2> listeners{ MakePeer(ListenerId, 0.0), MakePeer(ListenerId + 1, 0.0) };
		const auto recipients = SelectVoiceRecipients(MakePeer(5, 500.0), Tier::Normal, 1.0, listeners, slots);
		assert(recipients.count == 1);
		assert(recipients.playerIds[0] == ListenerId + 1);
	}

	{
		TalkerSlots slots{};
		auto listener = MakePeer(ListenerId, 0.0);
		listener.blocked[0] = 5;
		listener.blockedCount = 1;
		for (std::uint16_t id = 1; id <= 3; ++id) {
			assert(Hears(Relay(MakePeer(id, 100.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		}
		assert(!Hears(Relay(MakePeer(5, 50.0), Tier::Normal, 1.0, listener, slots), ListenerId));
		assert(Hears(Relay(MakePeer(6, 100.0), Tier::Normal, 1.0, listener, slots), ListenerId));
	}

	{
		const auto sender = MakePeer(16, 0.0);
		auto overfull = MakePeer(2, 100.0);
		overfull.blockedCount = 16;
		assert(Reaches(sender, overfull, Tier::Normal));
	}

	{
		constexpr double nan = std::numeric_limits<double>::quiet_NaN();
		const auto sender = MakePeer(1, 0.0);
		assert(!Reaches(MakePeer(1, nan), MakePeer(2, 100.0), Tier::Normal));
		assert(!Reaches(sender, MakePeer(2, nan), Tier::Normal));
		assert(!Reaches(sender, MakePeer(2, 0.0, nan), Tier::Normal));

		TalkerSlots slots{};
		assert(!AdmitTalker(slots, 1, nan, 1.0));
		for (std::uint16_t id = 2; id <= 5; ++id) {
			assert(AdmitTalker(slots, id, 100.0, 1.0));
		}
		assert(AdmitTalker(slots, 6, 50.0, 1.0));
		constexpr double inf = std::numeric_limits<double>::infinity();
		TalkerSlots clockSlots{};
		for (std::uint16_t id = 1; id <= 4; ++id) {
			assert(AdmitTalker(clockSlots, id, 100.0, 1.0));
		}
		assert(!AdmitTalker(clockSlots, 1, 100.0, nan));
		assert(!AdmitTalker(clockSlots, 2, 100.0, inf));
		assert(clockSlots[0].speakerId == 1 && clockSlots[0].lastFrameSeconds == 1.0);
		assert(clockSlots[1].speakerId == 2 && clockSlots[1].lastFrameSeconds == 1.0);
		assert(AdmitTalker(clockSlots, 5, 99.0, 1000.0));
	}

	{
		std::array<VoicePeer, MaxVoiceRecipients + 1> listeners{};
		std::array<TalkerSlots, MaxVoiceRecipients + 1> slots{};
		for (std::size_t i = 0; i < listeners.size(); ++i) {
			listeners[i] = MakePeer(static_cast<std::uint16_t>(200 + i), 0.0);
		}
		assert(SelectVoiceRecipients(MakePeer(1, 100.0), Tier::Normal, 1.0, listeners, slots).count == 0);

		std::array<VoicePeer, 2> pair{ MakePeer(ListenerId, 0.0), MakePeer(ListenerId + 1, 0.0) };
		std::array<TalkerSlots, 1> oneSlot{};
		assert(SelectVoiceRecipients(MakePeer(1, 100.0), Tier::Normal, 1.0, pair, oneSlot).count == 0);

		std::array<VoicePeer, 16> sixteen{};
		std::array<TalkerSlots, 16> sixteenSlots{};
		for (std::size_t i = 0; i < sixteen.size(); ++i) {
			sixteen[i] = MakePeer(static_cast<std::uint16_t>(300 + i), 0.0);
		}
		assert(SelectVoiceRecipients(MakePeer(1, 100.0), Tier::Normal, 1.0, sixteen, sixteenSlots).count == 16);
	}

	{
		const auto sender = MakePeer(1, 0.0);
		assert(VoiceInRange(sender, MakePeer(2, 256.0), Tier::Whisper));
		assert(!VoiceInRange(sender, MakePeer(2, 256.001), Tier::Whisper));
		assert(VoiceInRange(sender, MakePeer(2, 0.0, 256.0), Tier::Whisper));
		assert(!VoiceInRange(sender, MakePeer(2, 0.0, 256.001), Tier::Whisper));
		assert(VoiceInRange(sender, MakePeer(2, 1024.0), Tier::Normal));
		assert(!VoiceInRange(sender, MakePeer(2, 1024.001), Tier::Normal));
		assert(!VoiceInRange(sender, MakePeer(2, 300.0), Tier::Whisper));

		auto otherCell = MakePeer(2, 100.0);
		otherCell.scope->cellId = "00000200";
		assert(VoiceInRange(sender, otherCell, Tier::Normal));

		auto interiorSender = MakePeer(1, 0.0);
		interiorSender.scope->cellId = "0000CAFE";
		interiorSender.scope->worldspaceId.clear();
		auto interiorOther = MakePeer(2, 0.0);
		interiorOther.scope->cellId = "0000BEEF";
		interiorOther.scope->worldspaceId.clear();
		assert(!VoiceInRange(interiorSender, interiorOther, Tier::Shout));
		interiorOther.scope->cellId = "0000CAFE";
		assert(VoiceInRange(interiorSender, interiorOther, Tier::Shout));
	}

	{
		const auto sender = MakePeer(1, 0.0);
		auto listener = MakePeer(ListenerId, 0.0);
		listener.blocked[0] = 1;
		listener.blockedCount = 1;
		assert(VoiceBlocked(sender, listener));
		listener.blockedCount = 0;
		assert(!VoiceBlocked(sender, listener));
		listener.blockedCount = 1;
		listener.blocked[0] = 2;
		assert(!VoiceBlocked(sender, listener));

		auto blocker = MakePeer(1, 0.0);
		blocker.blocked[0] = ListenerId;
		blocker.blockedCount = 1;
		assert(!VoiceBlocked(blocker, MakePeer(ListenerId, 0.0)));
	}

	return 0;
}
