#include "F4TGnsProtocolAdapter.h"
#include "F4TPacketRateLimit.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using Session = F4T::GnsProtocolAdapter::Session;
	using Delivery = F4T::GnsProtocolAdapter::Delivery;
	using F4T::PacketRateLimit::Verdict;

	struct AppearanceSize
	{
		std::size_t headParts;
		std::size_t morphs;
		std::size_t morphRegions;
		std::size_t facialBoneMorphs;
		std::size_t tints;
	};

	constexpr AppearanceSize kTypicalAppearance{ 6, 60, 20, 30, 12 };
	constexpr AppearanceSize kServerCapAppearance{ 128, 128, 128, 128, 128 };
	constexpr std::size_t kBudgetSafetyBytesPerSecond = 64U * 1024U;

	void CompleteHandshake(Session& a_session)
	{
		a_session.AdaptInbound(Json{ { "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 } }.dump());
		a_session.AdaptInbound(Json{
			{ "type", "sessionReady" }, { "playerId", 7 }, { "protocolVersion", 2 }, { "serverProtocolVersion", 2 },
			{ "worldStateHostPlayerId", 7 }, { "orderedPlayerVisibility", false }, { "visibilityPauseSupported", false },
			{ "authoritativeDownedSupported", false }, { "consoleAllowed", true }, { "startingInventoryRecovery", nullptr }
		}.dump());
		assert(a_session.IsReady());
	}

	Json Appearance(AppearanceSize a_size)
	{
		Json morphs = Json::array();
		for (std::size_t i = 0; i < a_size.morphs; ++i) {
			morphs.push_back({ { "id", "0001F4A6" }, { "value", 0.1234567 + 0.0007 * static_cast<double>(i) } });
		}
		Json regions = Json::array();
		for (std::size_t i = 0; i < a_size.morphRegions; ++i) {
			regions.push_back(0.3 + 0.01 * static_cast<double>(i));
		}
		Json facial = Json::array();
		for (std::size_t i = 0; i < a_size.facialBoneMorphs; ++i) {
			const double offset = 0.0123 * static_cast<double>(i + 1);
			facial.push_back({ { "id", 1000 + i }, { "position", { offset, -offset, 0.0 } }, { "rotation", { 0.0, offset * 0.5, 0.0 } }, { "scale", { 1.0, 1.0, 1.0 } } });
		}
		Json heads = Json::array();
		for (std::size_t i = 0; i < a_size.headParts; ++i) {
			heads.push_back("00012345");
		}
		Json tints = Json::array();
		for (std::size_t i = 0; i < a_size.tints; ++i) {
			Json tint{ { "id", i + 1 }, { "type", 2 }, { "value", 50 + i } };
			if (i % 2 == 0) {
				tint["color"] = "00A1B2C3";
				tint["swatch"] = 12 + i;
			}
			tints.push_back(tint);
		}
		return Json{
			{ "version", 4 }, { "raceFormId", "00000001" }, { "height", 0.983 },
			{ "morphWeight", { { "thin", 0.25 }, { "muscular", 0.5 }, { "large", 0.125 } } },
			{ "bodyTintColor", { { "r", 120 }, { "g", 90 }, { "b", 70 }, { "a", 255 } } },
			{ "hairColorFormId", "00011111" }, { "facialHairColorFormId", "" }, { "complexionFormId", "00022222" },
			{ "isFemale", false }, { "headParts", heads }, { "morphs", morphs }, { "morphRegions", regions },
			{ "facialBoneMorphs", facial }, { "tints", tints }
		};
	}

	Json Equipment(const char* a_formId)
	{
		return Json::array({ Json{
			{ "slot", "right" },
			{ "formId", a_formId },
			{ "mods", Json::array({ Json{ { "index", 0 }, { "formId", "00003344" }, { "rank", 1 } } }) }
		} });
	}

	Json FireActions(std::uint32_t a_sequence)
	{
		return Json::array({ Json{ { "sequence", a_sequence }, { "type", 3 }, { "eventName", "fireSingle" } } });
	}

	Json Transform(AppearanceSize a_size, double a_x, const Json* a_actions)
	{
		Json packet{
			{ "type", "transform" }, { "x", a_x }, { "y", 2048.5 }, { "z", 128.25 }, { "angleZ", 90.0 },
			{ "movementType", "normal" }, { "clientTime", 1780212140.25 }, { "isMoving", false },
			{ "isSprinting", false }, { "isSneaking", false }, { "isJumping", false }, { "isCrouching", false },
			{ "weaponDrawn", true }, { "movementSpeed", 0.0 }, { "characterName", "Vault Dweller" },
			{ "animationDirection", 0.0 }, { "aimPitch", -1.25 }, { "turnDelta", 0.0 },
			{ "cellId", "0001F4A6" }, { "worldspaceId", "0000003C" },
			{ "equippedItems", Equipment("0001ABCD") }, { "pipboyLight", { { "on", true }, { "color", 983062 } } },
			{ "appearance", Appearance(a_size) }
		};
		if (a_actions != nullptr) {
			packet["actionEvents"] = *a_actions;
		}
		return packet;
	}

	std::size_t ServerBytes(const F4T::GnsProtocolAdapter::OutboundMessage& a_message)
	{
		return a_message.payload.size() + (a_message.delivery == Delivery::UnreliableSequenced ? 12U : 0U);
	}

	void AppendAdapted(Session& a_session, const std::string& a_legacy, std::vector<F4T::GnsProtocolAdapter::OutboundMessage>& a_out)
	{
		for (auto& message : a_session.AdaptOutbound(a_legacy)) {
			a_out.push_back(std::move(message));
		}
	}

	void ShotCarriesOnlyActionEventsOnceTheDurableStateIsSent()
	{
		Session session;
		CompleteHandshake(session);
		const auto first = session.AdaptOutbound(Transform(kTypicalAppearance, 1.0, nullptr).dump());
		assert(first.size() == 2);
		const auto firstState = Json::parse(first[1].payload);
		assert(firstState.contains("appearance") && firstState.contains("equippedItems") && firstState.contains("characterName"));

		const auto actions = FireActions(1);
		const auto shot = session.AdaptOutbound(Transform(kTypicalAppearance, 1.1, &actions).dump());
		assert(shot.size() == 2 && shot[0].delivery == Delivery::UnreliableSequenced && shot[1].delivery == Delivery::ReliableOrdered);
		const auto state = Json::parse(shot[1].payload);
		assert(state.at("actionEvents").size() == 1);
		assert(!state.contains("appearance"));
		assert(!state.contains("equippedItems"));
		assert(!state.contains("characterName"));
		assert(shot[1].payload.size() < 512);
	}

	void ChangedEquipmentIsResentWithTheShot()
	{
		Session session;
		CompleteHandshake(session);
		session.AdaptOutbound(Transform(kTypicalAppearance, 1.0, nullptr).dump());

		const auto actions = FireActions(2);
		auto changed = Transform(kTypicalAppearance, 1.2, &actions);
		changed["equippedItems"] = Equipment("00002222");
		const auto messages = session.AdaptOutbound(changed.dump());
		assert(messages.size() == 2);
		const auto state = Json::parse(messages[1].payload);
		assert(state.at("equippedItems")[0].at("formId") == "00002222");
		assert(state.at("actionEvents").size() == 1);
	}

	void StickyStateIsResentAfterTheInterval()
	{
		Session session;
		CompleteHandshake(session);
		session.AdaptOutbound(Transform(kTypicalAppearance, 1.0, nullptr).dump());
		const auto actions = FireActions(2);
		const auto early = session.AdaptOutbound(Transform(kTypicalAppearance, 1.1, &actions).dump());
		assert(early.size() == 2 && !Json::parse(early[1].payload).contains("appearance"));

		std::this_thread::sleep_for(std::chrono::seconds{ 5 });
		const auto late = session.AdaptOutbound(Transform(kTypicalAppearance, 1.2, &actions).dump());
		assert(late.size() == 2);
		const auto state = Json::parse(late[1].payload);
		assert(state.contains("appearance") && state.contains("equippedItems") && state.contains("characterName"));
		assert(state.at("actionEvents").size() == 1);
	}

	void ResetResendsTheFullStickyState()
	{
		Session session;
		CompleteHandshake(session);
		session.AdaptOutbound(Transform(kTypicalAppearance, 1.0, nullptr).dump());
		session.Reset();
		CompleteHandshake(session);
		const auto actions = FireActions(3);
		const auto messages = session.AdaptOutbound(Transform(kTypicalAppearance, 1.1, &actions).dump());
		assert(messages.size() == 2);
		const auto state = Json::parse(messages[1].payload);
		assert(state.contains("appearance") && state.contains("equippedItems") && state.contains("characterName"));
	}

	void MinigunBurstStaysUnderTheDefaultBudget()
	{
		Session session;
		CompleteHandshake(session);
		auto meter = F4T::PacketRateLimit::Initialize(0.0);
		std::uint32_t sequence = 1;
		std::size_t secondBytes = 0;
		std::size_t worstSecondBytes = 0;
		int currentSecond = 0;
		for (int shot = 0; shot < 30 * 20; ++shot) {
			const double now = shot * 0.05;
			if (static_cast<int>(now) != currentSecond) {
				worstSecondBytes = worstSecondBytes > secondBytes ? worstSecondBytes : secondBytes;
				secondBytes = 0;
				currentSecond = static_cast<int>(now);
			}
			const auto actions = FireActions(sequence++);
			std::vector<F4T::GnsProtocolAdapter::OutboundMessage> messages;
			AppendAdapted(session, Transform(kTypicalAppearance, 1000.0 + shot * 0.01, &actions).dump(), messages);
			AppendAdapted(session, Json{ { "type", "projectileAim" }, { "direction", { { "x", 0.9981 }, { "y", -0.0213 }, { "z", 0.0551 } } } }.dump(), messages);
			AppendAdapted(session, Json{
				{ "type", "npcHit" }, { "npcId", 77 }, { "cellId", "0001F4A6" }, { "worldspaceId", "0000003C" },
				{ "sourceFormId", "00012345" }, { "sequence", 1000 + shot }, { "attackSequence", 44 + shot },
				{ "authorityEpoch", 3 }, { "damage", 12.5 }, { "weaponFormId", "0001F4A6" }
			}.dump(), messages);
			for (const auto& message : messages) {
				secondBytes += ServerBytes(message);
				assert(F4T::PacketRateLimit::Observe(meter, now, {}, ServerBytes(message)).verdict == Verdict::Allowed);
			}
		}
		worstSecondBytes = worstSecondBytes > secondBytes ? worstSecondBytes : secondBytes;
		assert(worstSecondBytes < kBudgetSafetyBytesPerSecond);
		assert(meter.violationWindows == 0);
	}

	void FloodOfTheOldPerShotStateIsStillDisconnected()
	{
		auto meter = F4T::PacketRateLimit::Initialize(0.0);
		bool disconnected = false;
		for (int shot = 0; shot < 20 * 5 && !disconnected; ++shot) {
			const auto actions = FireActions(static_cast<std::uint32_t>(shot + 1));
			const Json oldState{
				{ "type", "playerState" }, { "actionEvents", actions }, { "appearance", Appearance(kServerCapAppearance) },
				{ "equippedItems", Equipment("0001ABCD") }, { "characterName", "Vault Dweller" }
			};
			disconnected = F4T::PacketRateLimit::Observe(meter, shot * 0.05, {}, oldState.dump().size()).verdict == Verdict::Disconnect;
		}
		assert(disconnected);
	}

	void FloodOfTinyMessagesIsStillDisconnected()
	{
		auto meter = F4T::PacketRateLimit::Initialize(0.0);
		bool disconnected = false;
		for (int message = 0; message < 1600 && !disconnected; ++message) {
			disconnected = F4T::PacketRateLimit::Observe(meter, message * 0.0025, {}, 64).verdict == Verdict::Disconnect;
		}
		assert(disconnected);
	}
}

int main()
{
	ShotCarriesOnlyActionEventsOnceTheDurableStateIsSent();
	ChangedEquipmentIsResentWithTheShot();
	ResetResendsTheFullStickyState();
	StickyStateIsResentAfterTheInterval();
	MinigunBurstStaysUnderTheDefaultBudget();
	FloodOfTheOldPerShotStateIsStillDisconnected();
	FloodOfTinyMessagesIsStillDisconnected();
	return 0;
}
