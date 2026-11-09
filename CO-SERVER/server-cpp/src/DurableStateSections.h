#pragma once

#include <string_view>

namespace CommonwealthOnline::Server::DurableStateSections
{
	inline constexpr double kSlowMainThreadSaveSeconds = 0.1;
	inline constexpr double kRecentInventoryDropWindowSeconds = 5.0;

	inline constexpr std::string_view kProfilesSection = "profiles";
	inline constexpr std::string_view kDropsSection = "drops";
	inline constexpr std::string_view kContainersSection = "containers";
	inline constexpr std::string_view kWorldStateSection = "worldState";
	inline constexpr std::string_view kFactionsSection = "factions";
	inline constexpr std::string_view kSettlementAccessSection = "settlementAccess";
	inline constexpr std::string_view kQuestsSection = "quests";
	inline constexpr std::string_view kQuestsBackupSection = "quests_backup";
	inline constexpr std::string_view kWorkshopSection = "workshop";
	inline constexpr std::string_view kParkedSuitsSection = "parkedSuits";
	inline constexpr std::string_view kSteamIdentitiesSection = "steamIdentities";
	inline constexpr std::string_view kCharacterSlotsSection = "characterSlots";

	inline constexpr std::string_view kProfileSections[] = { kProfilesSection };
	inline constexpr std::string_view kDropSections[] = { kProfilesSection, kDropsSection };
	inline constexpr std::string_view kContainerSections[] = { kProfilesSection, kContainersSection };
	inline constexpr std::string_view kContainerClaimsSection = "containerClaims";
	inline constexpr std::string_view kContainerClaimSections[] = { kContainersSection, kContainerClaimsSection };
	inline constexpr std::string_view kContainerClaimOnlySections[] = { kContainerClaimsSection };
	inline constexpr std::string_view kDropOnlySections[] = { kDropsSection };
	inline constexpr std::string_view kContainerOnlySections[] = { kContainersSection };
	inline constexpr std::string_view kWorldStateSections[] = { kWorldStateSection };
	inline constexpr std::string_view kFactionSections[] = { kFactionsSection };
	inline constexpr std::string_view kSettlementAccessSections[] = { kSettlementAccessSection, "settlementCharges", "settlementChargeNextOperationId" };
	inline constexpr std::string_view kQuestSections[] = { kQuestsSection };
	inline constexpr std::string_view kQuestBackupSections[] = { kQuestsBackupSection };
	inline constexpr std::string_view kWorkshopSections[] = { kWorkshopSection, "settlementCharges", "settlementChargeNextOperationId" };
	inline constexpr std::string_view kParkedSuitSections[] = { kParkedSuitsSection };
	inline constexpr std::string_view kCampSections[] = { kDropsSection, kWorkshopSection, kSettlementAccessSection };
	inline constexpr std::string_view kSteamIdentitySections[] = { kSteamIdentitiesSection };
	inline constexpr std::string_view kVendorLedgerSection = "vendorLedger";
	inline constexpr std::string_view kVendorLedgerSections[] = { kVendorLedgerSection };
	inline constexpr std::string_view kCharacterSlotSections[] = { kCharacterSlotsSection };
	inline constexpr std::string_view kEraseCharacterSlotSections[] = { kCharacterSlotsSection, "pendingQuestErase" };
	inline constexpr std::string_view kEraseCharacterSlotStateSections[] = { kCharacterSlotsSection, "pendingQuestErase", kProfilesSection, "knownPeople" };
	inline constexpr std::string_view kCharacterSlotMigrationSections[] = {
		kProfilesSection, kCharacterSlotsSection, kDropsSection
	};
}
