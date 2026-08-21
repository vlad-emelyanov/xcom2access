# Armory loadout — recon

`UIArmory_Loadout`, the screen squad select hands the player to and the only place a soldier's
armour and inventory can actually be changed. Built 2026-08-21; **not yet run live.**

Why it jumped the queue: the squad-select run on 2026-08-21 reported a frag grenade still being
named after a medikit had been equipped for it. The reader's chain is live at every keypress and
has no cache, so a stale answer had nowhere to come from — and the log showed why. In the 15
seconds `UISquadSelect` was unfocused, **five Escapes and two Enters went straight to the game**.
This screen had no recipe and no listener: the swap was attempted with zero feedback of any kind,
and most likely never landed. A correct readout looked like a broken one because the screen that
does the work was silent.

---

## 1. Structure: two lists, one of them live

```
UIArmory_Loadout (extends UIArmory)
├─ Header            UISoldierHeader — name, rank, class, status, the stat column
├─ EquippedList      UIList of UIArmory_LoadoutItem — the soldier's slots
├─ LockerList        UIList of UIArmory_LoadoutItem — what could go in the chosen slot
├─ ActiveList        points at whichever of the two the game is driving
└─ InfoTooltip       UIArmory_LoadoutItemTooltip — the item's stats, on hover
```

`ChangeActiveList` is the whole input model: it calls `DisableNavigation()` on one list and
`EnableNavigation()` on the other, moves the screen `Navigator` between the two containers, and
disables mouse hits on the inactive one. Enter on an equipped row runs `UpdateLockerList()` and
switches to the locker; Enter on a locker row switches back and calls `EquipItem`. Escape
(`OnCancel`) backs out one level.

⛔ **The inactive list is still on screen.** `MC.FunctionVoid("openList")` only slides it aside, so
a sighted player keeps reading their current loadout while choosing. That is why the recipe
carries the equipped list as a **read-only mirror** while the locker is open — and why the mirror
gets no focus write-back: moving a selection in a list the game has switched off is how the two
models drift apart.

---

## 2. What is readable, and the surprise

**Every row RETAINS its words.** `UIArmory_LoadoutItem` keeps `Title`, `Subtitle`, `SlotType`,
`Count` and five state bits (`IsLocked` / `IsInfinite` / `IsDisabled` / `bCanBeCleared` /
`bLoadoutSlot`) in real script properties, each written under a dirty check immediately before the
matching Flash op. This is the **ant farm's** pattern, not the tactical HUD's: the retained value
IS what Flash was last told. So native reads the rows and no companion is needed for them.

| Field | Offset | Note |
|---|---|---|
| `Title` | 0x3D8 | the item's friendly name; `""` for an empty slot |
| `Subtitle` | 0x3E8 | `string(ItemTemplate.ItemCat)` — an internal name, not localized |
| `disabledText` | 0x3F8 | **declared and never assigned** — see below |
| `SlotType` | 0x428 | the localized slot label; set only for equipped rows |
| `Count` | 0x438 | how many the inventory holds |
| flag dword | 0x43C | pinned between `Count` and `EquipmentSlot` |
| `EquipmentSlot` | 0x440 | `EInventorySlot` |
| `DropItemButton` | 0x458 | the row's clear-this-slot button |

⛔ **`disabledText` is a trap.** It is declared on the row and no setter ever writes it —
`SetDisabled(bDisabled, Reason)` queues the reason straight into Flash and stores only the bool.
So the one thing a greyed row most needs to say is the one thing it does not keep. The companion
recovers it by asking the screen the same question the screen asked: `GetDisabledReason(Item,
GetSelectedSlot())` for a locker row, and `CHUIItemSlotEnumerator.LockedReason` for an equipped
one.

**The header retains nothing.** `UISoldierHeader.PopulateData` resolves the name, status and the
whole stat column into locals and hands them to `SetSoldierInfo`, which is a `BeginFunctionOp` /
`QueueString` run. Tactical-HUD pattern — companion territory.

`m_strInventoryLabels` is declared as a **67-entry** array in the running class rather than the
stock 17, which is a second independent confirmation that the generated SDK is Highlander-built
(`CHItemSlot` extends `EInventorySlot`).

---

## 3. Input: what vanilla already does

Unlike the ant farm, nothing here is un-ungated — `EnableNavigation()` on the active list means
the arrows, Enter and Escape already work on mouse+keyboard. So this is a **reader**, like
save/load, not a rescue.

What a keyboard genuinely cannot reach, all of it nav-help:

| Verb | How vanilla dispatches it | Reachable by keyboard? |
|---|---|---|
| Strip items | `NavHelp.AddRightHelp` mouse handler, or `FXS_BUTTON_LTRIGGER` | no |
| Strip weapons | mouse handler, or `FXS_BUTTON_R3` | no |
| Strip gear | mouse handler, or `FXS_BUTTON_RTRIGGER` | no |
| Make available (unequip) | the row's `DropItemButton`, or `FXS_BUTTON_X` | no |
| Item stat tooltip | mouse hover; `DelayedShowTooltip` is called only under `` `ISCONTROLLERACTIVE `` | no |

Those are the four nodes the `actions` stop exists for, plus the tooltip on the navigator's
existing Space key.

⚠ `CanCancel()` returns **false** while `T0_M5_EquipMedikit` is in progress and squad select is on
the stack — the tutorial deliberately traps the player here until they equip the medikit. The
layer claims no Escape, so that behaviour is unchanged; it is worth knowing before reading a
refused Escape as a bug in us.

---

## 4. The shape it took

**Native** (`game_ui.cpp`: `ReadArmoryLoadout` / `SetLoadoutSelection` /
`ActivateLoadoutSelection` / `ClearLoadoutRow`, every offset pinned in `sdk_layout_checks.cpp`)
reads which list is live, its rows, and the game's own selection.

**Companion** (`X2AccessLoadoutReader.uc`) supplies only what the screen computes and discards:

- `count` — `SetCount` retains the number and pushes `XEXPAND.ExpandString(m_strCount)` into
  Flash, so the localized sentence around it has to be rebuilt.
- `reason` — see the `disabledText` trap above.
- `detail` — the hover tooltip's stat block, from the item's own
  `GetUISummary_ItemBasicStats()`; spoken on **Space**, not on every arrow step.
- `delta` — what equipping this candidate would change, computed the way `UISoldierHeader`
  computes the numbers it recolours. ⛔ It rides the **row**, not the header: the focus
  write-back lands after the render that announces, so a header-sourced delta would always
  describe the previous row. ⛔ And it is measured against the item in the **selected equipped
  row**, not `GetEquippedItem(GetSelectedSlot())` — a soldier with two utility slots has two rows
  sharing `eInvSlot_Utility`, and that getter answers with the first of them whichever row is
  open.

**Recipe** (`menu_navigator.cpp`, registered before `kSquadSelectRecipe` because it sits on top of
it):

| Stop | Nodes |
|---|---|
| `soldier` | who, status, the stat column — read-only, and no focus visits any of them in vanilla |
| `items` | the ACTIVE list, one node per row: slot label + item, count and delta as value, the refusal as reason, Space for the stat block |
| `equipped` | the read-only mirror, only while the locker is open |
| `actions` | make available (when the selected row `bCanBeCleared`), strip items / weapons / gear, each with the game's own tooltip |

**Activation order is load-bearing.** `OnAccept()` is the argument-free entry point into
`OnItemClicked(ActiveList, ActiveList.SelectedIndex)` — it reads the selection and ignores
everything else — so the recipe writes the selection FIRST and then calls it. Same rule the
save/load rows earned, where a click sent while the two disagreed deleted the wrong save.

`ClearLoadoutRow` goes through the row's own `OnDropItemClicked(UIButton)`, whose parameter its
body never reads; the real `DropItemButton` is passed anyway so the frame is honest.

Equipped-row order is `CHUIItemSlotEnumerator` order, because that is what the Highlander's
`UpdateEquippedList` builds the rows from. Walking the enumerator again is what lets a locked
slot's reason be found by list **position** instead of by guessing which slot a row meant — which
matters for a soldier with two utility slots where only the second is locked.

---

## 5. Open for a second pass

1. **First live run.** `just dev-status` should show the `armory loadout` recipe attached, and
   `just dev-speech` should carry the soldier line on attach and one line per slot.
2. **The rest of the tooltip.** Only `GetUISummary_ItemBasicStats()` is spoken today.
   `GetUISummary_TacticalText()`, `..._TacticalTextUpgrades()` and `..._TacticalTextAbilities()`
   are the prose half — what the item actually DOES — and are the obvious next addition to the
   Space key.
3. **Weapon upgrades.** `UIArmory_WeaponUpgradeItem` is a separate screen with the same
   `disabledText`-declared-never-assigned shape; it will want the same treatment.
4. `UIArmory_MainMenu` and the other armory screens (promotion, abilities) are untouched, so the
   Avenger route into the loadout is still silent above this screen.
5. **Shape-count the first run.** `grep -oP 'TTS!?: "\K[^"]*' X2Access.log | sed 's/[0-9]\+/N/g' |
   sort | uniq -c | sort -rn` — the audit that found the tile cursor's 260-to-0 cover split, and
   the cheapest way to catch a field whose values have collapsed to one.
