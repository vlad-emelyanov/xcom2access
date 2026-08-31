# Mission blade recon — the `UIMission` family (G2)

Measured 2026-08-31 against a live `UIMission_Retaliation` (Russian locale, Chosen-controlled
region) plus a full SrcOrig survey of all 14 shipped subclasses. The popup inventory ranks this
family "the highest-value geoscape target after `UIAlert`" (`popup-inventory.md` G2): it is the
screen that commits the campaign to a mission, and it read as dead silence.

## 1. The family

`UIMission` (base, `UIX2SimpleScreen`) + 14 SrcOrig subclasses (`_GOps`, `_Council`,
`_Retaliation`, `_SupplyRaid`, `_LandedUFO`, `_AlienFacility`, `_GoldenPath`, `_GPIntelOptions`,
`_ResOps`, `_RescueSoldier`, `_ChosenAmbush`, `_ChosenStronghold`, `_ChosenAvengerAssault`,
`_LostAndAbandoned`) + DLC_2 `UIMission_AlienNest` + DLC_3 `UIMission_LostTowers`. One widget
skeleton for all of them, spawned in `UIMission.BindLibraryItem`:

- `LibraryPanel` — the Flash blade; every content string goes through
  `LibraryPanel.MC.BeginFunctionOp(...)` + `QueueString` and is NOT retained (house style).
- `Button1`, `Button2`, `Button3`, `ConfirmButton` — real `UIButton`s. Base wiring (subclasses
  keep the semantics): Button1 = launch, Button2 = cancel. `ConfirmButton` is `DisableNavigation`'d
  and hidden by most subclasses; GOps keeps it as the launcher.
- `LockedPanel` + `LockedButton` — spawned only when `CanTakeMission()` is false
  (AlienFacility, GoldenPath, GPIntelOptions, ChosenStronghold, RescueSoldier). LockedButton is
  `SetText`'d (`m_strBack`) and wired to `OnCancelClicked`.
- `ShadowChamber` (`UIAlertShadowChamberPanel`), `SitrepPanel` (`UIAlertSitRepPanel`),
  `ChosenPanel` — base `UpdateData` populates or hides each. **Their `bIsVisible` IS the gate**
  (child widgets, deliberately toggled — the documented exception to the never-gate-on-visible
  rule): `UpdateShadowChamber` hides the panel when no Shadow Chamber is built, `UpdateSitreps`
  hides it when the mission has no sitreps or `bBlockSitrepDisplay`, `UpdateChosen` hides it
  when no live met Chosen controls the region — and `UIMission_ChosenAmbush` overrides
  `UpdateChosen` to hide it unconditionally, so recomputing the game-state gate would speak a
  strip the game deliberately blanked.

## 2. Live measurements (2026-08-31, dev harness)

`/status` with the blade up: `UIMission_Retaliation` at stack index 0, `0x279
inited focused visible`, over `UIStrategyMap` / `UIFacilityGrid` / `UIAvengerHUD` /
`UIEventNotices`. Stack interactive, navigator detached (nothing covered it), tile reader
correctly blocked with `blocker=UIMission_Retaliation`.

`/props screen=UIMission_Retaliation` — offsets match the generated SDK dump exactly:

| Offset | Field | Live value (ru) |
|---|---|---|
| 0x0600 | `MissionRef` | `{ObjectID=651}` |
| 0x0654 | `m_strMissionObjective` | "ЗАДАЧА" |
| 0x0664 | `m_strMissionLabel` | "ОПЕРАЦИЯ" |
| 0x0704 | `m_strLaunchMission` | "НАЧАТЬ ОПЕРАЦИЮ" |
| 0x0734 | `m_strChosenWarning` | "КОНТРОЛИРУЕТ РЕГИОН" |
| 0x0768 | `LibraryPanel` | live `UIPanel` |
| 0x0788–0x07A0 | `Button1/2/3`, `ConfirmButton` | live, `Button3` null (removed) |
| 0x07B0/0x07B8 | `LockedPanel`, `LockedButton` | null (mission takeable) |

Every `localized` var on the chain was POPULATED in Russian (the §13.7
declared-on-two-classes trap does not bite here), including the `UIX2SimpleScreen` /
`UISimpleScreen` generics (`m_strIgnore` = "ИГНОРИРОВАТЬ", `m_strCancel` = "ОТМЕНА").

## 3. Keyboard reality (PC, mouse+keyboard)

- `Enter`/`Space`: the screen's own `OnUnrealCommand` fires `ConfirmButton`'s delegate if
  visible+enabled, else `Button1`'s — BEFORE the Navigator sees the key. Subclass overrides
  (Retaliation, GOps…) keep the same shape.
- `Escape`/right-click: `CanBackOut()` → `CloseScreen()`. `bCannotBackOutUI` sources (the
  ambush, the final mission) refuse.
- Arrows: `RefreshNavigation` puts the VISIBLE buttons in the screen's Navigator with
  `LoopSelection` — but since Enter never reaches the navigator-focused control, **arrow focus
  changes nothing a keyboard can act on**. Consequences:
  - **GOps mission switching is mouse-only in vanilla**: `OnMissionOneClicked/Two/Three` are
    `OnClickedDelegate`s on Button1-3; the keyboard path can only ever launch the currently
    selected mission. Our button nodes are PARITY, not an aid.
  - **LockedButton is `DisableNavigation`'d** — mouse-only too.
- Controller-only extras (not ours to claim): L3 opens `UISitRepInformation` off the sitrep
  panel's InfoButton; PC gets that panel's info button by mouse only.

## 4. Where the words are (per the read-the-screen precedence)

Nothing content-bearing is retained in widgets, but the screen retains `MissionRef` and OWNS
every getter its own populate functions called — so the companion re-derives by calling the
screen's own functions, exactly the `X2AccessHackingReader` pattern:

- `GetOpName()` — full localized "Operation X" string (`MissionData.BattleOpName`).
- `GetObjectiveString()`, `GetMissionDescString()` — subclass-overridden; GetMissionDescString
  does the `XGParam` region expansion (Retaliation, SupplyRaid…), so calling it beats reading
  `m_strFlavorText` raw, which still carries the unexpanded tag.
- `GetRegionName()`, `GetDifficultyString()`, `GetRewardString()`, `GetMissionTitle()` (pin
  label), `GetCrewString()`/`GetCrewCount()` (Shadow Chamber lines), `CanTakeMission()`,
  `CanBackOut()`.
- Sitreps: `XCOMHQ().GetGeneratedMissionData(...).SitReps` → template
  `GetFriendlyName()`/`Description` — same walk `UpdateSitreps` does, gated on
  `SitrepPanel.bIsVisible` instead of re-running its conditions.
- Chosen strip: `GetRegion().GetControllingChosen()` → class name / name / nickname +
  `m_strChosenWarning`, gated on `ChosenPanel.bIsVisible`.
- Button labels: `Button.Text` where the subclass `SetText`'d (GOps mission selectors carry the
  per-mission strings; ResOps/RescueSoldier embed `<img>` markup — `FetchCompanionRecords`
  already CleanTexts every wire field); the localized vars the class pushed to Flash where it
  did not (Council/SupplyRaid/LandedUFO push `m_strLaunchMission`/`m_strIgnore`,
  Retaliation the generic confirm/cancel pair, the Chosen blades `m_strConfirmMission`/generic
  cancel).

## 5. Per-class content map (the Flash pushes to mirror, in visual order)

| Class | Heading | Body lines (beyond op name + objective pair) | Button1 / Button2 labels |
|---|---|---|---|
| `_Retaliation` | `m_strRetaliationMission` | `m_strRetaliationWarning` + `GetMissionDescString()` | generic confirm / generic cancel (`SetText`'d) |
| `_Council` | `m_strCouncilMission` | reward pair | `m_strLaunchMission` / `m_strIgnore` |
| `_SupplyRaid` | `m_strSupplyMission` | `GetRaidDesc()` | `m_strLaunchMission` / `m_strIgnore` |
| `_LandedUFO` | `m_strLandedUFOMission` | `GetRaidDesc()` | `m_strLaunchMission` / `m_strIgnore` |
| `_GOps` | `m_strGOpsTitle` | difficulty pair, reward pair, dark event (label + `GetDarkEvent().GetDisplayName()` + `GetPreMissionText()` when set), `GetUnlockHelpString()` | Button1-3 = mission selectors (`Text`), ConfirmButton = launch |
| `_AlienFacility` | `GetMissionTitle()` | `GetMissionDescString()`, reward pair when non-empty | `m_strLaunchMission` / generic cancel |
| `_GoldenPath` | `GetMissionTitle()` + `GetMissionSubtitle()` | `GetObjectiveHeader()` pair, desc, reward pair | `m_strLaunchMission` / generic cancel |
| `_GPIntelOptions` | `GetMissionTitle()` | desc + intel option rows (`UIMechaListItem` checkboxes: name, cost, checked=purchased) + intel available/total lines | `m_strLaunchMission` / generic cancel |
| `_ResOps` | `m_strResOpsMission` | faction title + name, reward label | `m_strLaunchMission` / `m_strIgnore` |
| `_RescueSoldier` | `m_strRescueMission` | faction title + name, reward pair | `m_strLaunchMission` / `m_strIgnore` |
| `_LostAndAbandoned` | `m_strLostAndAbandonedMission` | faction title + name, reward pair | `m_strLaunchMission` / `m_strIgnore` |
| `_ChosenAmbush` | `m_strTitle` | chosen class/name/nickname, `m_strChosenAmbushObjective(Desc)` pair, `m_strBody` | `m_strConfirmMission` / — (Button2 dev-only `OnSimCombat`, hidden in retail; no back-out) |
| `_ChosenStronghold` | `GetMissionTitle()` | chosen identity, desc | `m_strLaunchMission` / generic cancel |
| `_ChosenAvengerAssault` | `m_strTitle` | chosen identity, `m_strAvengerAssaultObjective(Desc)` pair, desc | `m_strConfirmMission` / generic cancel |
| DLC / unknown | `GetMissionTitle()` | desc, difficulty pair, reward pair (empties dropped) | `Button.Text`, else launch/ignore by slot |

Locked variants (AlienFacility, GoldenPath, GPIntelOptions, ChosenStronghold, RescueSoldier):
`m_strLocked` + `m_strLockedHelp` (GoldenPath swaps in `m_strAvatarLocked(Help)` when
`IsFinalMission()`), and the only live control is LockedButton (`m_strBack` → cancel);
Button1/2 are `SetDisabled`'d.

## 6. Design calls (and the rules they follow)

- **A dedicated recipe + a dedicated reader class** (`X2AccessMissionReader`), not an extension
  of the popup family: the popup recipe's activation rule is load-bearing ("the SCREEN's own
  OnUnrealCommand, never a button's Click") and exactly wrong here — GOps mission switching and
  LockedButton are reachable ONLY through `UIButton.Click()`, which every subclass wires with
  real delegates. Click() carries the game's own visible/disabled gates.
- **Probe by IsA, not by name table**: the topmost stack screen that `IsInstanceRaw`s against
  the `UIMission` class object — refusing (like the popup finder) if the top of the stack is
  something else, since a blade under another screen has lost the keys. IsA covers both DLC
  blades and mod-added subclasses for free; the companion's default branch reads those through
  the base getters.
- **Cache key**: screen pointer + `MissionRef.ObjectID` as the WireCache `extra` — GOps switches
  missions IN PLACE (`OnMissionOneClicked` rewrites `MissionRef` + `UpdateData`, no respawn), so
  the pointer key alone would serve stale words. That is native's ONE offset read (0x600,
  static_assert-pinned).
- **Activation**: `ActivateMissionButton(index)` on the reader CDO (the covert-actions
  slot-verb shape) → resolve the screen fresh, `Click()` the named button. Wire cache
  invalidated after every activation.
- **Escape** stays the screen's own (`CanBackOut` branch included) — the recipe's cancel node
  clicks Button2/LockedButton where one exists; the raw key still works.
- The heading (blade title + region) rides as the graph CONTEXT and as the attach name
  (`CovertActionsName` pattern), so the screen announces itself in the player's language.
- **A caption whose value is absent is dropped whole** (`PushValuePair`): the first live pass
  (2026-08-31, a locked alien facility) spoke a naked "НАГРАДА" where a sighted player saw no
  reward row at all — the game pushes those rows conditionally and the reader must mirror the
  condition, not just the strings. The locked pair is the deliberate exception:
  "ЗАДАНИЕ НЕДОСТУПНО!" stands on its own.

## 7. First-pass checklist (→ next-steps.md until run)

1. A Retaliation/Council/GOps blade attaches, speaks its heading + region, Tab walks
   lines → buttons, Enter on the launch node launches (squad select follows), cancel node backs
   out to the geoscape.
2. GOps with 2-3 missions: clicking a selector node re-announces the new mission's lines
   (MissionRef extra-key rebuild), ConfirmButton launches the SELECTED one.
3. A locked blade (facility before the objective unlocks) reads the locked title/help and
   offers only Back.
4. Chosen strip speaks on a controlled region and stays silent on the ambush blade.
5. Shadow Chamber lines appear once the chamber is built; sitrep names read on a sitrep
   mission.
6. Shape-count the speech ring afterwards (the standing audit).
