#!/usr/bin/env python3
import argparse
import collections
import json
import sys
from pathlib import Path

RACE_KINGDOM = {
    "HumanRace": "human",
    "HumanChildRace": "human",
    "GhoulRace": "human",
    "GhoulChildRace": "human",
    "SynthGen1Race": "human",
    "SynthGen2Race": "human",
    "SynthGen2RaceValentine": "human",
    "PowerArmorRace": "human",
    "HandyRace": "robot",
    "ProtectronRace": "robot",
    "SentryBotRace": "robot",
    "AssaultronRace": "robot",
    "TurretTripodRace": "robot",
    "TurretBubbleRace": "robot",
    "TurretWorkshopRace": "robot",
    "EyeBotRace": "robot",
    "LibertyPrimeRace": "robot",
    "BrahminRace": "animal",
    "CatRace": "animal",
    "RadStagRace": "animal",
    "DogmeatRace": "animal",
    "ViciousDogRace": "creature",
    "RaiderDogRace": "creature",
    "FEVHoundRace": "creature",
    "DeathclawRace": "creature",
    "MirelurkRace": "creature",
    "MirelurkHunterRace": "creature",
    "MirelurkKingRace": "creature",
    "MirelurkQueenRace": "creature",
    "SuperMutantRace": "creature",
    "SupermutantBehemothRace": "creature",
    "FeralGhoulRace": "creature",
    "FeralGhoulGlowingRace": "creature",
    "RadScorpionRace": "creature",
    "RadRoachRace": "creature",
    "BloodbugRace": "creature",
    "StingwingRace": "creature",
    "BloatflyRace": "creature",
    "MoleratRace": "creature",
    "YaoGuaiRace": "creature",
    "GorillaRace": "creature",
    "AlienRace": "creature",
    "VertibirdRace": "creature",
}

COMPANION_FACTIONS = {
    "CurrentCompanionFaction",
    "PotentialCompanionFaction",
    "HasBeenCompanionFaction",
    "Voices_CompanionsFaction",
}
COMPANION_FACTION_PREFIXES = ("CompanionCrimeFaction_",)

HOSTILE_FACTIONS = {
    "RaiderFaction",
    "GunnerFaction",
    "CreatureFaction",
    "SuperMutantFaction",
    "FeralGhoulFaction",
    "BugFaction",
    "MirelurkFaction",
    "RobotFaction",
    "TurretFaction",
    "DogFaction",
    "DeathclawFaction",
    "BrahminFaction",
    "PlayerEnemyFaction",
    "TheForgedFaction",
}

FRIENDLY_FACTIONS = {
    "PlayerFaction",
    "PlayerAllyFaction",
    "PlayerFriendFaction",
    "DogmeatFaction",
    "DomesticAnimalFaction",
    "WorkshopNPCFaction",
}

VENDOR_FLAG = 0x00004000
VENDOR_ASSIGNMENT_FACTION_PREFIXES = ("WorkshopVendorFaction",)

QUEST_TYPES = {
    0: "none",
    1: "main",
    2: "brotherhood",
    3: "institute",
    4: "minutemen",
    5: "railroad",
    6: "miscellaneous",
    7: "side",
    8: "dlc01",
    9: "dlc02",
    10: "dlc03",
    11: "dlc04",
    12: "dlc05",
    13: "dlc06",
    14: "dlc07",
}

QUEST_FLAG_START_GAME_ENABLED = 0x0001
QUEST_FLAG_ALLOW_REPEATED_STAGES = 0x0008
QUEST_FLAG_RUN_ONCE = 0x0100
QUEST_FLAG_WARN_ON_ALIAS_FILL_FAILURE = 0x0400

ACBS_FEMALE = 0x00000001
ACBS_ESSENTIAL = 0x00000002
ACBS_AUTO_CALC_STATS = 0x00000010
ACBS_UNIQUE = 0x00000020
ACBS_PROTECTED = 0x00000800


def load_jsonl(path):
    rows = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


def local_id(form_key):
    return form_key.split(":")[0]


def kingdom_of(race_edid):
    return RACE_KINGDOM.get(race_edid, "unknown")


def faction_edids(npc):
    return [f["edid"] for f in npc["factions"]]


def is_companion(npc):
    for edid in faction_edids(npc):
        if edid in COMPANION_FACTIONS:
            return True
        if edid.startswith(COMPANION_FACTION_PREFIXES):
            return True
    return False


def vendor_factions(npc, fact_by_key):
    hits = []
    for faction in npc["factions"]:
        edid = faction["edid"]
        if edid.startswith(VENDOR_ASSIGNMENT_FACTION_PREFIXES):
            continue
        fact = fact_by_key.get(faction["faction"])
        if fact is not None and (fact["flags"] & VENDOR_FLAG):
            hits.append(edid)
    return hits


def is_vendor(npc, fact_by_key):
    return bool(vendor_factions(npc, fact_by_key))


def is_hostile(npc):
    kingdom = kingdom_of(npc["race_edid"])
    factions = set(faction_edids(npc))
    if factions & FRIENDLY_FACTIONS:
        return False
    if factions & HOSTILE_FACTIONS:
        return True
    return kingdom in ("creature", "robot")


def classify(npc, fact_by_key):
    if is_companion(npc):
        return "companion"
    if is_vendor(npc, fact_by_key):
        return "vendor"
    if kingdom_of(npc["race_edid"]) == "animal":
        return "animal"
    if is_hostile(npc):
        return "enemy"
    if npc["acbs"] & ACBS_UNIQUE:
        return "story_unique"
    return "town_npc"


class WorldPolicy:
    def __init__(self, dump_dir, plugin_name="Fallout4.esm"):
        dump = Path(dump_dir)
        self.plugin_name = plugin_name
        self.npcs = load_jsonl(dump / "npc.jsonl")
        self.facts = load_jsonl(dump / "fact.jsonl")
        self.races = load_jsonl(dump / "race.jsonl")
        self.keywords = load_jsonl(dump / "kwd.jsonl")
        self.quests = load_jsonl(dump / "qust.jsonl")
        self.doors = load_jsonl(dump / "door.jsonl")
        self.refdoors = load_jsonl(dump / "refdoor.jsonl")
        self.keyword_by_key = {k["fk"]: k["edid"] for k in self.keywords}
        self.fact_by_key = {f["fk"]: f for f in self.facts}
        self.placed_by_base = collections.Counter(r["base"] for r in self.refdoors)
        self.lock_by_base = {}
        self.teleport_by_base = {}
        for ref in self.refdoors:
            if ref["lock"] and ref["base"] not in self.lock_by_base:
                self.lock_by_base[ref["base"]] = ref
            if ref["teleport"] and ref["base"] not in self.teleport_by_base:
                self.teleport_by_base[ref["base"]] = ref

    def npc_row(self, npc):
        return {
            "formkey": npc["fk"],
            "editor_id": npc["edid"],
            "class": classify(npc, self.fact_by_key),
            "race": npc["race_edid"],
            "name_string_id": None,
            "companion": is_companion(npc),
            "vendor": is_vendor(npc, self.fact_by_key),
            "vendor_factions": vendor_factions(npc, self.fact_by_key),
            "unique": bool(npc["acbs"] & ACBS_UNIQUE),
            "essential": bool(npc["acbs"] & ACBS_ESSENTIAL),
            "protected": bool(npc["acbs"] & ACBS_PROTECTED),
            "female": bool(npc["acbs"] & ACBS_FEMALE),
            "aggression": npc["aggression"],
            "template": [t["edid"] or t["form"] for t in npc["template"]],
            "default_template": npc["tplt"] or None,
            "factions": faction_edids(npc),
            "keywords": [self.keyword_by_key.get(k, k) for k in npc["keywords"]],
        }

    def quest_row(self, quest):
        return {
            "formkey": quest["fk"],
            "editor_id": quest["edid"],
            "type": quest["quest_type"],
            "type_name": QUEST_TYPES.get(quest["quest_type"], "unknown"),
            "start_game_enabled": bool(quest["quest_flags"] & QUEST_FLAG_START_GAME_ENABLED),
            "run_once": bool(quest["quest_flags"] & QUEST_FLAG_RUN_ONCE),
            "allow_repeated_stages": bool(quest["quest_flags"] & QUEST_FLAG_ALLOW_REPEATED_STAGES),
            "name_string_id": quest["full"] if isinstance(quest["full"], int) else None,
            "stage_count": len(quest["stages"]),
            "stage_indices": [s["i"] for s in quest["stages"]],
            "alias_count": len(quest["aliases"]),
            "objective_count": len(quest["objectives"]),
            "fragment_count": len((quest["vmad"] or {}).get("fragments", [])),
            "fragments": (quest["vmad"] or {}).get("fragments", []),
        }

    def door_row(self, door):
        placed = self.placed_by_base.get(door["fk"], 0)
        lock_ref = self.lock_by_base.get(door["fk"])
        teleport_ref = self.teleport_by_base.get(door["fk"])
        return {
            "formkey": door["fk"],
            "editor_id": door["edid"],
            "placed_count": placed,
            "lock": None if lock_ref is None else {
                "reference": lock_ref["fk"],
                "level": lock_ref["lock"]["level"],
                "key": lock_ref["lock"]["key"] or None,
                "flags": lock_ref["lock"]["flags"],
            },
            "teleport": None if teleport_ref is None else {
                "reference": teleport_ref["fk"],
                "door": teleport_ref["teleport"]["door"] or None,
                "interior": teleport_ref["teleport"]["interior"] or None,
                "flags": teleport_ref["teleport"]["flags"],
            },
        }

    def build(self):
        npc_rows = [self.npc_row(n) for n in self.npcs]
        class_counts = collections.Counter(r["class"] for r in npc_rows)
        quest_rows = [self.quest_row(q) for q in self.quests]
        type_counts = collections.Counter(q["type_name"] for q in quest_rows)
        door_rows = [self.door_row(d) for d in self.doors]
        return {
            "schema": "co.world-policy/1",
            "plugin": self.plugin_name,
            "counts": {
                "npc": len(npc_rows),
                "npc_by_class": dict(sorted(class_counts.items())),
                "quest": len(quest_rows),
                "quest_by_type": dict(sorted(type_counts.items())),
                "quest_start_game_enabled": sum(1 for q in quest_rows if q["start_game_enabled"]),
                "quest_run_once": sum(1 for q in quest_rows if q["run_once"]),
                "quest_with_fragments": sum(1 for q in quest_rows if q["fragment_count"]),
                "door": len(door_rows),
                "placed_door_refs": len(self.refdoors),
                "door_with_lock": sum(1 for d in door_rows if d["lock"]),
                "door_with_teleport": sum(1 for d in door_rows if d["teleport"]),
            },
            "npcs": npc_rows,
            "quests": quest_rows,
            "doors": door_rows,
        }


def hand_check_sample(policy):
    wanted = [
        "CompanionPiper", "PrestonGarvey", "CompanionNickValentine", "Codsworth", "dogmeat",
        "CompanionCait", "CompanionCurie", "CompanionStrong", "CompanionDeacon", "BoSPaladinDanse",
        "CompanionMacCready", "Myrna", "ArturoRodriguez", "Daisy", "Kleo", "Takahashi",
        "VadimBobrov", "YefimBobrov", "Polly", "BeckyFallon", "RufusRubins", "Solomon",
        "MS16BobbiNoNose", "EncRaider02", "EncDeathclaw02", "EncBrahminPack",
        "EncWorkshopNPCFemaleFarmer01", "CaptiveSettler_Base", "EncSynthGen2", "DiamondCitySecurity",
    ]
    by_edid = collections.defaultdict(list)
    for row in policy["npcs"]:
        by_edid[row["editor_id"]].append(row)
    rows = []
    for name in wanted:
        hits = by_edid.get(name)
        if not hits:
            rows.append({"editor_id": name, "class": "MISSING"})
            continue
        row = dict(hits[0])
        row["duplicates"] = len(hits)
        rows.append(row)
    for race, label in (("ViciousDogRace", "mongrel/dog"), ("CatRace", "cat")):
        hit = next((r for r in policy["npcs"] if r["race"] == race), None)
        if hit:
            row = dict(hit)
            row["editor_id"] = row["editor_id"] + " (" + label + ")"
            rows.append(row)
    return rows


def report_unmapped(npc_rows):
    unmapped = collections.Counter(row["race"] for row in npc_rows if row["race"] not in RACE_KINGDOM)
    if not unmapped:
        return
    names = ", ".join(sorted(race or "<none>" for race in unmapped))
    print(f"{sum(unmapped.values())} NPCs use {len(unmapped)} unmapped races "
          f"(kingdom unknown, not enemy unless a hostile faction): {names}", file=sys.stderr)


def self_test():
    unlisted = {"race_edid": "SettlerNewRace", "factions": [], "acbs": 0}
    assert kingdom_of("SettlerNewRace") == "unknown", kingdom_of("SettlerNewRace")
    assert classify(unlisted, {}) == "town_npc", classify(unlisted, {})
    hostile = {"race_edid": "SuperMutantRace", "factions": [], "acbs": 0}
    assert classify(hostile, {}) == "enemy", classify(hostile, {})
    print("self-test PASS")
    return 0


def main(argv):
    parser = argparse.ArgumentParser(description="Build a Commonwealth Online world-policy list from a Radicle probe dump.")
    parser.add_argument("--dump")
    parser.add_argument("--out")
    parser.add_argument("--plugin", default="Fallout4.esm")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--check-out")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if not args.dump or not args.out:
        parser.error("--dump and --out are required")

    policy = WorldPolicy(args.dump, args.plugin).build()
    report_unmapped(policy["npcs"])
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(policy, handle, indent=1, sort_keys=False)
        handle.write("\n")
    print(json.dumps(policy["counts"], indent=1))
    if args.check:
        rows = hand_check_sample(policy)
        for row in rows:
            if row["class"] == "MISSING":
                print(f"  MISSING {row['editor_id']}")
                continue
            print(f"  {row['class']:12s} {row['editor_id']:32s} race={row['race']:24s} unique={int(row['unique'])} "
                  f"essential={int(row['essential'])} vendor={int(row['vendor'])} companion={int(row['companion'])} "
                  f"factions={','.join(row['factions'][:4])}")
        if args.check_out:
            with open(args.check_out, "w", encoding="utf-8") as handle:
                json.dump({"schema": policy["schema"], "plugin": policy["plugin"], "samples": rows}, handle, indent=1)
                handle.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
