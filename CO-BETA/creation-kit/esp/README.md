# ESP Files

This folder is for original Commonwealth Online Creation Kit plugin files.

The active plugin is:

```text
CommonwealthOnline.esp
```

It includes custom remote-player proxy actor bases:

| Editor ID | Local FormID | Sex | Notes |
|---|---|---|---|
| `COPlayerProxy` | `002666` | Male | Duplicate of Player; used for male remotes |
| `COPlayerProxyFemale` | `0091E5` | Female | Duplicate of Player with Female checked; used for female remotes |

Both should use the same proxy-friendly settings (no Unique story AI, stripped
packages as needed). Set the full name to `Player` on both so the interact prompt
falls back cleanly before `characterName` arrives.

It contains `CO_CharacterSetupQuest` with:

- `COCharacterSetupQuestScript` attached, with its four object properties
  (`SexChangePending`, `GenderChoiceMessage`, `StartMarker`,
  `AnimFaceArchetypePlayer`).
- `StartMarker` assigned to a marker in `Vault111Cryo` with open space in front of
  it, since the camera idle frames the player from the front.
- `GenderChoiceMessage` assigned to a Message with buttons **Male** (index 0) and
  **Female** (index 1). This prompt appears before Looks/SPECIAL.
- `SexChangePending` assigned to a GlobalVariable whose Editor ID is exactly
  `CO_SexChangePending` (short/float, default `0`). The plugin watches this ID and
  runs `player.SexChange` when the script sets it to `1`.
- `AnimFaceArchetypePlayer` assigned to `AnimFaceArchetypePlayer` (`KYWD`,
  `Fallout4.esm` `0x000D755E`).
- Quest stage `100` present so the script can signal completion.
- Start Game Enabled and Starts Enabled are set. Run Once is clear. The stage 10 and stage 100 log entries do not complete or fail the quest, and `COCharacterSetupQuestScript` does not stop it.

`COQuestSyncScript` is attached to this same quest with no properties. It drains the
server's quest command queue and reports quest state every half second.

Neither the Creation Kit nor the FO4IDE record editor can add a script entry to an
existing VMAD, so the entry is patched into the record directly with
`creation-kit/tools/attach_quest_sync_script.py`:

```text
python creation-kit/tools/attach_quest_sync_script.py <in.esp> <out.esp>
```

1. The patcher finds the `QUST` record with EDID `CO_CharacterSetupQuest` and refuses a
   compressed record, a missing VMAD, or a plugin that already has `COQuestSyncScript`.
2. It inserts one script entry (name `COQuestSyncScript`, status `0`, no properties) after
   the last existing script entry and before the QUST fragment section, then increments
   `scriptCount`.
3. It fixes the VMAD subrecord size, the `QUST` record data size and the size of every
   GRUP that contains the record, and nothing else. Every other byte is compared and the
   run aborts on any unexpected difference, so the `COCharacterSetupQuestScript`
   properties and the stage 10 fragment stay byte-identical.
4. The output is verified with FO4IDE (`open_plugin`, `get_record_fields`) and with
   Radicle's QUST reader, which lists both scripts and the unchanged properties.

`COQuestSyncScript` primes itself with `Prime()`: it starts the timer and registers
`OnPlayerLoadGame` once. `OnInit` and `OnQuestInit` call it, but that is not enough for a
save whose `CO_CharacterSetupQuest` is already running, because a newly attached script on
a running quest is initialized without either event. `F4TSessionCharacterSetupFlow::
EnsureRunning` therefore issues `cqf CO_CharacterSetupQuest Prime` on every save load that
is not the first-join flow, using the same console path this flow already uses to start the
quest. `cqf CO_CharacterSetupQuest Prime` in the console primes one save by hand if needed.

Do not add or remove the script through the Creation Kit afterwards: the new entry has no
properties, and re-saving the plugin is not needed. The plugin starts
`CO_CharacterSetupQuest` itself after a save load when the quest is not running, so a
returning character does not depend on the start-game-enabled flag alone. The quest has to
stay in `CommonwealthOnline.esp` (`CO_CharacterSetupQuest`).

If the Creation Kit ever writes a SEQ file for `CommonwealthOnline.esp`, keep that file
with the plugin. This package does not contain one, and an ESP quest does not auto-start
from the Start Game Enabled flag alone.

`FaceCameraIdle` and `BodyCameraIdle` are optional overrides and should normally be
left as `None`. The Creation Kit crashes when opening the picker for an `Idle`
property, so the script resolves the vanilla camera idles by form ID instead:

| Idle | Form ID |
|---|---|
| `ElevatorFaceCamera` | `Fallout4.esm` `0x0001A9EE` |
| `ElevatorBodyCamera` | `Fallout4.esm` `0x0001B491` |

Those idles are what position the customization camera. Without them the menu opens
on the normal over-the-shoulder third-person camera.

## Camp kit and welcome quest

The plugin also holds the camp kit, the item a player places to start a camp, and
the quest that points the player to it. `add_camp_kit.py` adds these records.

| Record | Local form ID | Purpose |
|---|---|---|
| `CO_CampKit` (MISC) | `00997F` | The camp kit: "Camp Kit", value 2, weight 0.1, model `SetDressing\DuffleBag\DuffleBag.nif` |
| Placed REFR | `009980` | The camp kit on the Vault table, base `CO_CampKit`, temporary |
| `CO_WelcomeQuest` (QUST) | `009981` | Stage 10 says where the camp kit is. Stage 20 completes the quest. |

Placement, in cell `Vault111Cryo` (`0016D8`):

| Value | Setting |
|---|---|
| Position | x -1574.76, y -845.97, z 60 |
| Rotation | z 0.072 |
| Table | `Vault_Table_01` REFR `02D281`, top at z 57 |

z 60 sits 3 units above the table top. Check in game and lower it to 57 if it floats.

`CO_WelcomeQuest` has Start Game Enabled and Starts Enabled set, and Run Once clear,
like `CO_CharacterSetupQuest`. Its objective 10 is "Find your camp kit". It targets
the reference alias `CampKit`, filled by REFR `009980`, and the target only shows
while the quest is at stage 10 (`GetStage == 10`). Stage 20 completes the quest.

The records do not start the quest or display objective 10 on their own. The plugin
(`EnsureWelcomeQuest` in `F4TReplicationRuntime.cpp`) runs `startquest CO_WelcomeQuest`, sets
stage 10 and displays objective 10 once per session.

No SEQ file is created for this quest. The package has none, and an ESP quest does
not start from the Start Game Enabled flag alone. The plugin has to start
`CO_WelcomeQuest`, the same way it starts `CO_CharacterSetupQuest`.

Patcher:

```text
python creation-kit/tools/add_camp_kit.py <in.esp> <out.esp>
```

1. It refuses a localized plugin, and a plugin that already has `CO_CampKit`,
   `CO_WelcomeQuest` or local IDs `00997F` to `009981`.
2. It adds the MISC record, a temporary group under `0016D8` with the REFR, and the
   QUST record. It sets `nextObjectID` to at least `009982` and adds 3 to the HEDR
   record count.
3. It fixes the size of every GRUP it grows. Every other byte is compared with the
   input, and the run aborts on any difference. The output is re-parsed and the new
   records are checked before anything is written.

## Creating the gender Message and Global

1. Create a **Global** named `CO_SexChangePending`, type Short or Float, value `0`.
2. Create a **Message** (for example `CO_GenderChoiceMessage`) with two buttons:
   - Button 0: `Male`
   - Button 1: `Female`
3. On `CO_CharacterSetupQuest` script properties, assign:
   - `GenderChoiceMessage` → that Message
   - `SexChangePending` → `CO_SexChangePending`

Do not place Bethesda master files or other mods' plugin files here.

Patcher record layout sources: Radicle docs/FORMAT_NOTES.md (QUST grammar), docs/QUEST_ALIASES.md (ANAM, ALST/ALFR), F4RR-co verification/150-quest.md (CTDA layout, GetStage is function 58).
