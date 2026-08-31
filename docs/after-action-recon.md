# After action (post-mission squad walk-up) — recon

_Source-derived 2026-08-31 from the SDK's `Development/SrcOrig/XComGame/Classes/` plus the
Highlander's rebuilds of both classes and the regenerated SDK's dump comments; the recipe was
built before the screen could be measured (it exists only in the minutes after a mission, and no
save sits on it). **Offsets and both §5 predictions live-verified the same day** — the player
reached the screen and every §3 offset was read off the running process through `/props` (marked
✅ below). What has NOT run live is the RECIPE itself — the deployed ASI predated it — so the
first pass in `next-steps.md` still stands, minus its step 0._

---

## 1. What it is

`UIAfterAction` — the post-mission squad view: the soldiers walk up to the camera on the Avenger
deck with one status panel each (В СТРОЮ / wounded + days out / MIA / KIA, rank, name, nickname,
class, missions, kills, HP and will bars, promotion banner, bond icon), the mission result block
top-left, and a ПРОДОЛЖИТЬ button. It is the screen where promotions are SPENT — the Повышение
banner's button opens `UIArmory_Promotion` — so a blind player who cannot work it loses every
level-up the mission earned. Item 7 of the P1 short list (`ui-inventory.md`), and the middle of
the post-mission chain: `UIMissionSummary` (⬜ not covered) → dropship briefing (✅) → **this** →
`UIInventory_LootRecovered` (✅ commodity family) → geoscape (✅).

The Highlander overrides both classes; the diffs are behavioral only (auto-photo config gate,
promotion-blueprint event, stacked class icon for the faction crest) — **no field added or moved
on either class**, so the SrcOrig declarations plus the generated dump describe the running
layout.

Three facts shape everything, all inherited from squad select (`squad-select-recon.md`):

1. **The rows retain no text.** Every string on a panel goes into Flash through one
   `AS_SetData(rank, lastName, nick, classIcon, rankIcon, promote, statusLabel, statusText,
   daysLabel, daysText, missionsLabel, missions, killsLabel, kills, isPsi, className)` call plus
   `AS_SetUnitHealth` / `AS_SetUnitWill` / `AS_SetFactionIcon`, and is retained nowhere. What a
   row keeps is `UnitReference`, `m_bCanPromote`, `m_bKIA`, `Index` — so the companion re-derives
   the words exactly the way `UpdateData` derived them.
2. **The widget tree holds the selection model**: `m_kSlotList` (a horizontal `UIList`, one
   `UIAfterAction_ListItem` per soldier PRESENT — absent squad slots create no item), the card's
   `PromoteButton` and `BondIcon`, and the focus bits.
3. **The screen is deaf until its remote event.** `OnUnrealCommand` swallows everything while
   `!bRecievedShowHUDRemoteEvent`; the flag is set by `PostM_ShowSoldierHUD` once the fly-in
   cinematic hands over. Same gate shape as squad select's `bReceivedWalkupEvent`: do not attach
   before it.

---

## 2. What a sighted player sees

From the reference screenshot (Russian, six soldiers), top to bottom:

| On screen | Source |
|---|---|
| `РЕЗУЛЬТАТЫ ЗАДАНИЯ` header | `UIAvengerHUD.FacilityHeader.SetText(…, m_strAfterActionReport)` — the string is `UIAfterAction.m_strAfterActionReport` |
| `Операция "Стальной фургон"` | `GeneratedMissionData.BattleOpName` via `m_kMissionInfo.UpdateData` |
| `ЗАДАЧИ:` / `Уничтожить ретранслятор пришельцев` | `m_strObjectives` label + `MissionState.GetMissionObjectiveText()` — difficulty and rewards are passed `""` on this screen |
| per panel: `В СТРОЮ` (green) | `m_strActive`, timeline label `active` |
| per panel: `ЛЕГКО РАНЕН` / `РАНЕНИЕ` (red) + `дн. 7` | `Caps(Unit.GetWoundStatus(injuryHours, true))` + `GetDaysString(days)` + the number, hours rounded UP to days |
| (not in shot) `ПРОПАЛ БЕЗ ВЕСТИ` / `УБИТ` | `m_strMIA` (captured) / `m_strKIA` (dead); a KIA panel swaps in the soldier's portrait and a `Memorialize` button |
| per panel: `КАПРАЛ` / `KELLY` / `'СТОП-СИГНАЛ'` / `РЕЙНДЖЕР` | rank name, `GetName(eNameType_Last)`, `eNameType_Nick`, class name — class line **only for rank > 0**, same rookie rule as squad select |
| per panel: `ЗАДАНИЙ 4` / `УБИТО 5` | `m_strMissionsLabel` + `GetNumMissions()`, `m_strKillsLabel` + `GetNumKills()` |
| per panel: two bars (+ and ⚡) | `AS_SetUnitHealth(cur, max)`, `AS_SetUnitWill(percent, colour)` |
| `ПОВЫШЕНИЕ` banner over a panel | `AS_SetData`'s promote param — `UISquadSelect_ListItem.m_strPromote`, passed only when `Unit.ShowPromoteIcon()` |
| bond icon (handshake) per panel | `UIBondIcon`, hidden (`SetBondLevel(-1)`) for a class that cannot bond and for KIA; tooltip carries the bondmate |
| flyovers during the walk-up | `BuildFlyoverList`: tired/shaken (`GetMentalStateLabel`), `m_strMostCohesion` on the top cohesion pair, `m_strBondAvailable` / `m_strBondLevelUpAvailable` |
| psi markup icon | `PsiMarkup.Show()` when `HasPsiGift()`, alive branch only |
| faction crest | `AS_SetFactionIcon` — under the Highlander (#1134) a stacked CLASS icon, so it is no longer a faction fact |
| bottom centre: `ПРОДОЛЖИТЬ` | `NavHelp.AddContinueButton(OnContinue)` — label `UIUtilities_Text.m_strGenericContinue`, button lives on the Avenger HUD |

---

## 3. What is readable off the tree

Offsets from the generated SDK dump (`XComGame_classes.hpp`), pinned in `sdk_layout_checks.cpp`,
and ✅ **read back off the LIVE screen 2026-08-31** (`/props screen=UIAfterAction` and a live
list item, unit ObjectID 428) — every offset below matched the running class:

**`UIAfterAction`** — flag dword +0x0410 (`isBondIconFocused` 0x1, `bWalkupStarted` 0x2,
`bRecievedShowHUDRemoteEvent` 0x4 — read `true` on the settled screen, `bForceHelpActivate`
0x8), `m_strContinue` +0x046C (live: `"ПРОДОЛЖИТЬ"`), `m_strAfterActionReport` +0x047C,
`m_kMissionInfo` +0x04CC, `m_kSlotList` +0x04E4, `XComHQ` +0x04F4.

**`UIAfterAction_ListItem`** — `UnitReference` +0x03D8 (a bare 4-byte `StateObjectReference`),
`m_strMemorialize` +0x043C, `PsiMarkup` +0x044C, `PromoteButton` +0x0454, `MemorialButton`
+0x045C (live: **null**, as predicted below), flag dword +0x0464 (`bShowPortrait` 0x1,
`m_bCanPromote` 0x2, `m_bKIA` 0x4 — both masks confirmed by `/props`), `BondIcon` +0x0468,
`Index` +0x0470. The localized statuses are POPULATED on the running instances
(`В СТРОЮ` / `РАНЕНО` / `В ПЛЕНУ` / `УБИТ В БОЮ`) — this is not a declared-but-empty loc pair.

Two live focus facts for the write-back: the game's resting focus is list `sel=0` with card 0
`bIsFocused` and its `PromoteButton` focused; a soldier who cannot promote has NO `UIButton`
child at all (`UpdateData` calls `PromoteButton.Remove()`), so a null `promoteButton` — not a
hidden one — is what "no promote node" looks like.

So the tree answers *which card, whether it can promote, whether the soldier is dead, where the
two buttons are* — and nothing about the soldier. The companion re-derives the words from
`UnitReference`, reaching the unit the way `UpdateData` does. `m_bCanPromote`/`m_bKIA` are the
screen's own cached answers and are read natively (read-the-screen rule) rather than re-asking
`ShowPromoteIcon()`.

⚠ **`MemorialButton` is a dead field.** `UpdateData` reuses `PromoteButton` for the KIA card
(same `promoteButtonMC`, delegate swapped to `OnClickedMemorialize`); `MemorialButton` is never
assigned in either the vanilla or Highlander source. Do not read it.

⚠ **`Index` is assigned only at creation** (`ListItem.Index = ListItemIndex`) and `UpdateData`
never touches it — unlike squad select's `SlotIndex` it cannot collide, but the join key stays
the LIST POSITION anyway, per the strategy.md rule: both halves walk `m_kSlotList` in the same
order, and a companion-side skip must leave a visible hole.

---

## 4. Input: what vanilla does — from source, not yet measured

`UIAfterAction.OnUnrealCommand` (everything below gated on `bRecievedShowHUDRemoteEvent`):

| Key | Effect |
|---|---|
| `Left` / `Right` | `super.OnUnrealCommand` → Navigator moves `m_kSlotList`'s selection card to card, then re-imposes bond-vs-promote focus (`isBondIconFocused`) |
| `Up` / `Down` | handed to the selected card's `OnUpDownInputHandler` — toggles focus between `PromoteButton` and `BondIcon` |
| `Enter` | falls through to the card's own `OnUnrealCommand`: promote if the promote button is focused, memorialize if KIA, else the bond icon |
| `Escape` / right mouse | `OnContinue()` — Continue is on CANCEL, which no blind player would guess |
| `Tab` | **dev-build only** (`notdefined(FINAL_RELEASE)`) — also `OnContinue()`; assume absent in retail |

So vanilla keyboard "works" here the way it worked on squad select — silently — and the Continue
verb is reachable only as Escape's side effect or a mouse click on a nav-help button
(`AddContinueButton`, mouse-dispatched). The list's selection skips nothing: WOTC calls
`EnableNavigation()` on every card ("we will always want navigation now that the bond icon is a
thing"), overriding the earlier `bIsNavigable = bCanPromote`.

**Activation entry points** (all verified argument-free or ignore-their-argument in source):

- Promote: `ListItem.AfterActionPromote()` — zero-param, calls `UIAfterAction.OnPromote(UnitReference)`
  which pushes `UIArmory_Promotion` (✅ covered by the promotion recipe) and slides the pawns away.
- Memorialize (KIA): `ListItem.OnClickedMemorialize(None)` — its `UIButton` param is never read;
  opens `UIArmory_Photobooth` (⬜ not covered — note in next-steps).
- Bond icon: `ListItem.OnClickBondIcon(None)` — param never read; guards on `bCanHaveBonds` and
  opens `UISoldierBondScreen` (⬜ P2, not covered).
- Continue: `UIAfterAction.OnContinue()` — zero-param; submits the After Action state, fires the
  exit cinematic, closes the screen, opens loot recovered. ⚠ `OnContinue` itself does NOT check
  `bRecievedShowHUDRemoteEvent` — the game's input handler does — so our node must honor the same
  gate (the recipe never attaches before the flag, which covers it).

**Focus write-back**, mirroring the game's own path: `m_kSlotList.SetSelectedIndex(i)` + the
card's `OnReceiveFocus()` (which lands focus on the promote button, or the bond icon when there
is no promote), then `BondIcon.OnReceiveFocus()` + `PromoteButton.OnLoseFocus()` (or the mirror)
to place the inner focus deterministically — NOT `OnUpDownInputHandler()`, which toggles from
whatever state the game is in.

---

## 5. Traps

- ⛔ **Do not attach before `bRecievedShowHUDRemoteEvent` (0x4 at +0x410).** Before it the panels
  are hidden, the screen `Hide()`s itself for the fly-in matinee, and every key is swallowed.
  Same rule, same shape, as squad select's walk-up gate.
- ⛔ **`CreateFlyoverTooltip` runs before the card has a unit.** `InitListItem` calls it while
  `UnitReference` is still zero, and nothing calls it again after `UpdateData` — so the tired
  tooltip squad select reads off the card is never created here. ✅ Confirmed live: the card
  reads `bHasTooltip=false` while its BOND ICON reads `bHasTooltip=true` (`CachedTooltipId` 79),
  so the bond node's label source works and the card tooltip must not be built on. The mental
  state comes from the unit state (`BelowReadyWillState` → `GetMentalStateLabel`) in the will
  line instead, which is also what the screen's own flyover speaks.
- ⛔ **A KIA card's button is the PROMOTE button wearing a different delegate.** `m_bKIA` decides
  which verb the widget means; reading the widget's presence alone would call a memorial button
  "Повышение". The flag dword is the authority.
- ⚠ **The list is rebuilt in place on every `OnReceiveFocus` of the screen** (`UpdateData` is
  called from it, re-running `ListItem.UpdateData` on existing items) — returning from a
  promotion re-derives every panel. The recipe re-reads live state per render anyway; the wire
  cache, if one is ever added, must invalidate on reattach.
- ⚠ **`isBondIconFocused` is the screen's own focus memory** across Left/Right — the bsg patches
  keep bond-vs-promote focus sticky. While our recipe owns the arrows it goes stale; harmless,
  since the game consults it only in its own arrow handler, but write-back keeps the Flash
  highlight honest per card.
- ⚠ **Six panels maximum, but fewer items than squad slots**: `UpdateData` creates a list item
  only for `Squad[i].ObjectID > 0`, so unlike squad select there are no empty/locked cards at
  all. `GetListItemIndexForUnit` exists precisely because list position ≠ squad index; we never
  need it (position is the key on both halves).

---

## 6. The shape it takes — a graph recipe plus a companion reader

Same division as squad select, for the same reasons: the mission block and the panels' facts are
words no focus ever visits (a narrate-what-focuses reader would read two buttons and nothing
else), Continue has no honest keyboard path, and the card facts live behind `UnitReference`.

- **Native** (`game_ui.cpp` `ReadAfterAction`, offsets pinned): the gate flag, the card list with
  per-card `m_bCanPromote` / `m_bKIA` / focus bit / `PromoteButton` / `BondIcon`, focus
  write-back and the three activations.
- **Companion** (`X2AccessAfterActionReader.uc`, `DescribeAfterAction`): one `mission` record
  (header, op name, objective + label, continue / promote / memorialize labels, missions/kills
  labels — all class defaults, sent ONCE per the strategy.md label rule), one `card` record per
  list item keyed by `pos` (status + days exactly as `UpdateData` words them, rank / name / nick
  / class as separate fields joined after cleaning, missions, kills, hp, will + mental state,
  psi, bond state, most-cohesion marker).
- **Recipe** (`recipe_strategy.cpp`, probed beside squad select): stops `mission` → `squad` →
  `actions`. Cards are columns — Up/Down inside a card, Left/Right across, clamped, explicit
  edges, self-edge walls — squad select's geometry. A card's children are at most
  [promote | memorialize] and [bond icon]; a card with neither still gets one read-only node so
  every soldier is reachable and announced. `actions` holds Continue.

Sighted parity notes: the rank/class ICONS ride on the words beside them; the faction crest is
skipped (under the Highlander it is a class-icon stack — the class is already spoken); the
XP-relevant facts a sighted player infers from the banner (who can be promoted) are the promote
node's existence. The walk-up FLYOVERS are transient banners a sighted player sees once — their
content (tired/shaken, bond available/level-up, top cohesion pair) is folded into the card lines
as persistent fields, which is an aid-shaped rendering of the same facts, not extra information.

---

## 7. Open questions for the live pass

1. Whether `bRecievedShowHUDRemoteEvent` is really the whole readiness story on a
   no-Skyranger mission (`CIN_StartWithoutFlyIn`) and on the `UIRedScreen`/`UIProgressDialogue`
   special cases its remote-event handler tolerates.
2. Whether the stack is cinematic-gated (`bCinematicMode`) during the walk-up — if so the
   navigator's global gate already refuses attach and the flag gate is belt-and-braces.
3. What `BondIcon` reads as when hidden by `SetBondLevel(-1)` — the recipe emits the node off the
   companion's `canbond`, but the write-back touches the icon; confirm no side effect on a hidden
   icon.
4. The KIA path end to end (portrait, memorialize → photobooth and BACK — the photobooth has no
   recipe, so the return leg is the part to watch).
5. Whether `OnPromote`'s pawn slide-away plus `UIArmory_Promotion` push detaches and reattaches
   cleanly, and whether promotion's own recipe reads the hero-promotion variant this screen can
   open for faction soldiers.
6. The most-cohesion pair loop mirrors `AddMostCohesionFlyovers` including its `MaxCohesion == 0`
   first-pair quirk; confirm against what actually flies over.
7. ✅ **DONE 2026-08-31** — `/props` against a live walk-up confirmed every §3 offset, both §5
   predictions (dead `MemorialButton`, card tooltip absent / bond tooltip present), and the
   resting focus state. The deployed ASI predated the recipe, so the RECIPE's own first pass
   (speech, navigation, activation) is still owed and is the next-steps entry.
