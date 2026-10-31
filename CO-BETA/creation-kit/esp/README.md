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

## Creating the gender Message and Global

1. Create a **Global** named `CO_SexChangePending`, type Short or Float, value `0`.
2. Create a **Message** (for example `CO_GenderChoiceMessage`) with two buttons:
   - Button 0: `Male`
   - Button 1: `Female`
3. On `CO_CharacterSetupQuest` script properties, assign:
   - `GenderChoiceMessage` → that Message
   - `SexChangePending` → `CO_SexChangePending`

Do not place Bethesda master files or other mods' plugin files here.
