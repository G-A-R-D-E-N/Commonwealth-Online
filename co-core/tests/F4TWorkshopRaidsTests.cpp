#include "F4TWorkshopRaids.h"

#include <cassert>

int main()
{
	using namespace F4T::WorkshopRaids;

	const RaidConfig fullDay{ RaidMode::Windowed, 0.0F, 24.0F };
	assert(WindowedOutsiderRaidAllowed(fullDay, 0.0F, false, true));
	assert(WindowedOutsiderRaidAllowed(fullDay, 13.5F, false, true));
	assert(!WindowedOutsiderRaidAllowed(fullDay, 13.5F, false, false));
	assert(WindowedOutsiderRaidAllowed(fullDay, 13.5F, true, false));

	const RaidConfig overnight{ RaidMode::Windowed, 23.0F, 2.0F };
	assert(WindowedOutsiderRaidAllowed(overnight, 23.0F, false, true));
	assert(WindowedOutsiderRaidAllowed(overnight, 0.5F, false, true));
	assert(!WindowedOutsiderRaidAllowed(overnight, 1.0F, false, true));
	assert(!WindowedOutsiderRaidAllowed(overnight, 12.0F, false, true));
	assert(!WindowedOutsiderRaidAllowed(overnight, 22.5F, false, true));

	assert(OwnerLocalHour(0, 0.0F) == 0.0F);
	assert(OwnerLocalHour(18000, 0.0F) == 5.0F);
	assert(OwnerLocalHour(18000, -5.0F) == 0.0F);
	assert(OwnerLocalHour(79200, 3.5F) == 1.5F);
	assert(OwnerLocalHour(3600, -2.0F) == 23.0F);
	assert(OwnerLocalHour(286200, 0.0F) == 7.5F);

	assert(UtcDayIndex(86399) == 0);
	assert(UtcDayIndex(86400) == 1);

	RaidDeclaration declaration{};
	declaration.expiresMonotonic = 200.0;
	assert(RaidDeclarationLive(declaration, 200.0));
	assert(!RaidDeclarationLive(declaration, 200.5));

	assert(!ValidWindowSet(0.0F, 0.5F, 0.0F));
	assert(ValidWindowSet(0.0F, 1.0F, 0.0F));
	assert(ValidWindowSet(23.0F, 24.0F, -5.0F));
	assert(!ValidWindowSet(0.0F, 24.5F, 0.0F));
	assert(ParseExplosiveKind("grenade") == ExplosiveKind::GrenadeOrMine);
	assert(ParseExplosiveKind("mine") == ExplosiveKind::GrenadeOrMine);
	assert(ParseExplosiveKind("missile") == ExplosiveKind::Missile);
	assert(ParseExplosiveKind("mininuke") == ExplosiveKind::MiniNuke);
	assert(!ParseExplosiveKind("nuke").has_value());
	assert(ExplosiveDamage(ExplosiveKind::GrenadeOrMine) == 60.0F);
	assert(ExplosiveDamage(ExplosiveKind::Missile) == 150.0F);
	assert(ExplosiveDamage(ExplosiveKind::MiniNuke) == 600.0F);
	assert(ExplosiveKindName(ExplosiveKind::GrenadeOrMine) == "grenade");
	assert(ExplosiveKindName(ExplosiveKind::Missile) == "missile");
	assert(ExplosiveKindName(ExplosiveKind::MiniNuke) == "mininuke");
	assert(ParseExplosiveKind(ExplosiveKindName(ExplosiveKind::MiniNuke)) == ExplosiveKind::MiniNuke);
	using WeaponKind = F4T::WorkshopMaterials::WeaponKind;
	assert(WeaponHitDamage(WeaponKind::Melee) == 25.0F);
	assert(WeaponHitDamage(WeaponKind::Light) == 15.0F);
	assert(WeaponHitDamage(WeaponKind::Rifle) == 30.0F);
	assert(WeaponHitDamage(WeaponKind::Heavy) == 60.0F);
	assert(WeaponHitDamage(WeaponKind::Explosive) == 0.0F);
	assert(StructureHitIntervalSeconds(WeaponKind::Melee) == 0.4F);
	assert(StructureHitIntervalSeconds(WeaponKind::Light) == 0.15F);
	assert(StructureHitIntervalSeconds(WeaponKind::Rifle) == 0.3F);
	assert(StructureHitIntervalSeconds(WeaponKind::Heavy) == 0.1F);
	assert(StructureHitIntervalSeconds(WeaponKind::Explosive) == 1.0F);
	assert(HitRefusalName(HitRefusal::FactionMate) == "factionMate");
	assert(HitRefusalName(HitRefusal::WeaponUnknown) == "weaponUnknown");
	assert(ExplosiveKindForSource(kMiniNukeSourceFormId) == ExplosiveKind::MiniNuke);
	assert(ExplosiveKindForSource(kMissileSourceFormId) == ExplosiveKind::Missile);
	assert(ExplosiveKindForSource(kFragGrenadeSourceFormId) == ExplosiveKind::GrenadeOrMine);
	assert(!ExplosiveKindForSource(kSidearmWeaponFormId).has_value());
	assert(ClassifyStanding(true, true, true) == SettlementStanding::Owner);
	assert(ClassifyStanding(false, true, true) == SettlementStanding::FactionMate);
	assert(ClassifyStanding(false, true, false) == SettlementStanding::Allowlisted);
	assert(ClassifyStanding(false, false, false) == SettlementStanding::Raider);

	const RaidConfig nightWindow{ RaidMode::Windowed, 20.0F, 2.0F };
	assert(WindowOverlapsInterval(nightWindow, 0.0F, 72000.0, 79200.0));
	assert(!WindowOverlapsInterval(nightWindow, 0.0F, 19800.0, 27000.0));
	assert(WindowOverlapsInterval(nightWindow, 2.0F, 64800.0, 72000.0));
	assert(!WindowOverlapsInterval(nightWindow, 0.0F, 64800.0, 72000.0));

	return 0;
}
