#pragma once
#include "F4TInventoryContract.h"
#include "F4TNpcSync.h"
#include "F4TPowerArmorSync.h"
#include "F4TRemotePlayerState.h"
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::Networking::Detail
{
	using Json = nlohmann::json;
	bool SendPacketBytes(std::string_view a_packet);
	bool SendPacketBytes(std::string_view a_packet, std::optional<std::uint64_t> a_generation);
	void InvalidateReplicationSession();
	bool SendPacketBytesForGeneration(std::string_view a_packet, std::uint64_t a_generation);
	void HandlePacketLine(const std::string& a_line);
	void HandleObjectPacket(const Json& a_packet);
	void HandleServerWorldStatePacket(const Json& a_packet);
	void HandleInventoryResultPacket(const Json& a_packet, F4T::InventoryContract::OwnedOperation a_expected);
	void HandleNpcHitPacket(const Json& a_packet);
	void HandleNpcDamagePacket(const Json& a_packet);
	void HandleNpcLootPacket(const Json& a_packet);
	void HandleNpcLootTakePacket(const Json& a_packet);
	void HandleNpcLootResultPacket(const Json& a_packet);
	void HandleAdminItemOpPacket(const Json& a_packet);
	void HandleAdminInventoryRequestPacket(const Json& a_packet);
	void HandleNpcLootAckPacket(const Json& a_packet);
	void HandleSaveAttestationResultPacket(const Json& a_packet, std::string_view a_type);
	void LogInfoWithLocalPlayerPrefix(std::string_view a_message);
	void LogStatusWithLocalPlayerPrefix(std::string_view a_message);
	void LogWarningWithLocalPlayerPrefix(std::string_view a_message);
	bool ShouldLogWarning(std::string a_key);
	void LogThrottledWarning(const std::string& a_key, std::string_view a_message);
	std::optional<std::uint32_t> ReadPlayerId(const Json& a_packet, const char* a_fieldName = "playerId");
	std::optional<double> ReadDouble(const Json& a_packet, const char* a_fieldName);
	std::optional<bool> ReadBool(const Json& a_packet, const char* a_fieldName);
	std::optional<std::string> ReadString(const Json& a_packet, const char* a_fieldName);
	std::optional<std::uint32_t> ReadUint32(const Json& a_packet, const char* a_fieldName);
	std::optional<float> ReadFloat(const Json& a_packet, const char* a_fieldName);
	bool ShouldLogRemoteUpdate(std::uint32_t a_playerId);
	bool ShouldLogRemoteMovementState(std::uint32_t a_playerId);
	std::optional<std::string_view> GetMissingRequiredTransformField(const Json& a_packet);
	std::optional<std::uint32_t> ParseHexFormId(std::string_view a_hex);
	std::vector<F4T::RemotePlayerState::RemoteActionEvent> ReadActionEvents(const Json& a_packet);
	std::optional<std::uint32_t> ReadMemberLimit(const Json& a_packet);
	// Wire caps; values must match co-core/src/validation/F4TPlayerStateValidation.cpp and F4TAppearanceValidation.cpp.
	inline constexpr std::size_t kMaximumEquippedItems = 32;
	inline constexpr std::size_t kMaximumAppearanceHeadParts = 64;
	inline constexpr std::size_t kMaximumAppearanceMorphs = 128;
	inline constexpr std::size_t kMaximumAppearanceMorphRegions = 128;
	inline constexpr std::size_t kMaximumAppearanceFacialBoneMorphs = 128;
	inline constexpr std::size_t kMaximumAppearanceTints = 128;
	std::optional<std::vector<F4T::RemotePlayerState::RemoteEquippedItem>> ReadEquippedItems(
		const Json& a_packet,
		bool& a_hasEquipmentUpdate);
	std::optional<F4T::RemotePlayerState::RemoteAppearance> ReadAppearance(const Json& a_packet, bool& a_hasAppearanceUpdate);
	F4T::PowerArmorSync::PowerArmorState ReadPowerArmor(const Json& a_packet, bool& a_hasPowerArmorUpdate);
	F4T::RemotePlayerState::RemotePipboyLight ReadPipboyLight(const Json& a_packet, bool& a_hasPipboyLightUpdate);
	void ResetPacketDiagnostics();
	void ErasePacketDiagnostics(std::uint32_t a_playerId);
}
