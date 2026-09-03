# Popup inventory — tactical, Avenger, geoscape

Survey date 2026-08-21, read off `Development/SrcOrig` (retail WOTC + DLC 1/2/3).
Purpose: name every transient/modal surface the game can put in front of the player,
say what it RETAINS, say how a mouse+keyboard player dismisses it, and mark what
X2Access already covers — so each can be made accessible one at a time.

Three questions decide the work for every entry, and they are the columns below:

1. **Retained?** Does the screen keep its words in script properties, or did it push
   them into Flash and forget them? (`MC.BeginFunctionOp` / `QueueString` = forgotten.)
2. **Keyboard?** Does `OnUnrealCommand` handle `FXS_KEY_*`, or only the controller
   constants `FXS_BUTTON_A` / `FXS_DPAD_*` and mouse clicks? A controller-only branch is
   dead code on mouse+keyboard — the same defect class as the hacking screen's reward
   choice and `UIFacilityGrid`'s room graph.
3. **Where does it live?** The screen stack (findable by class), or spawned on the
   presentation layer / a movie and never pushed (like `UIDialogueBox`, which needs the
   GObjects fallback).

---

## 0. Popup SHAPES — there are only five mechanisms

Everything below is one of these. Getting the shape right is most of the work; the
individual popups are then cheap.

| Shape | Mechanism | Retention | Covered? |
|---|---|---|---|
| **A. Modal dialogue box** | `Pres.UIRaiseDialog(TDialogueBoxData)` → the single permanent `UIDialogueBox`, queued in `m_arrData` | ✅ whole struct retained (`strTitle`/`strText`/`strAccept`/`strCancel`) | ✅ `kDialogRecipe` |
| **B. Strategy alert** | `` `HQPRES.UIAlert(...) `` → one `UIAlert` screen, **118 named variants** | ❌ Flash-only — but `eAlertName` and `DisplayPropertySet` ARE retained, so the companion can re-derive | ❌ **the big gap** |
| **C. Bespoke popup screen** | its own `UIScreen` subclass, pushed on the stack | mixed — most keep the state refs, none keep the built strings | ❌ mostly |
| **D. Transient toast / banner** | a manager screen holding a list, auto-expiring | varies — one is retained, one is not; see §4 | partial |
| **E. Tutorial help blade** | `UITutorialBox` (tactical) / `UIStrategyTutorialBox` (strategy) | ✅ both retain their text | ✅ tactical only |

---

## 1. TACTICAL layer

| # | Class | What the player sees | Retained? | Keyboard? | Status |
|---|---|---|---|---|---|
| T1 | `UIDialogueBox` | End-turn-with-actions-left, friendly-fire warning, hit-a-friendly-object, hive/objective confirms, Avenger lift-off refusal | ✅ | ✅ Enter/Esc | ✅ **done** |
| T2 | `UITutorialBox` | The modal tutorial instruction box | ✅ `Title`/`Desc`/`ButtonHelp` | ✅ Enter/Space/Esc | ✅ **done** (`tutorial_reader.cpp`) |
| T3 | `UIWorldMessageMgr` | Anchored flyovers, damage numbers, "Unconscious", tutorial blades | ✅ `m_arrMsgs` | n/a (auto-expire) | ✅ **done** |
| T4 | `UIEventNoticesTactical` / `UIEventNotices` | Top-right toast stack: "Concealment lost", objective ticks, banner notices | ✅ **`Notices` retained in full** — `DisplayString`, `Label`, `Value`, `eState`, `MaxDisplayTime` | n/a | ❌ **not read** — cheap win, poll it exactly like `m_arrMsgs`. ⚠ corrects CLAUDE.md, which says this retains nothing |
| T5 | `UITacticalHUDMessageBanner` | The big dramatic banner: Chosen kidnap, Chosen extract, Chosen defeated, `SeqAct_ShowDramaticMessage` | ❌ pure `MC.FunctionString("SetBanner", …)` | n/a | ❌ — but the SOURCE is reachable: `X2Action_PlayMessageBanner.AddMessageBanner(Title, Icon, Msg1, Msg2, Color)` is a visualization action, so the companion catches it there rather than at the widget |
| T6 | `UIChosenReveal` | Chosen first-encounter reveal (strength / weakness / rival faction) | ❌ built from templates into Flash | ✅ Enter/Space/Esc | ❌ |
| T7 | `UICombatLose` | Mission failed — Restart / Reload / Exit to main | localized strings ✅, choice list Flash | to verify | ❌ — high value: a blind player must be able to leave a lost mission |
| T8 | `UIProgressDialogue` | "Saving…" / "Loading…" blocking spinner with optional Cancel | ✅ `m_kData` (`strTitle`/`strDescription`/`strAbortButtonText`) | ✅ Esc (cancel only) | ❌ — trivial, and today it reads as a silent freeze |
| T9 | `UIRedScreen` | Script error overlay | ✅ `m_strErrorMsg` + `RedscreenLines` | ✅ | ❌ (dev-facing; low priority but free) |
| T10 | `UIInventoryTactical` | Post-kill loot grab window | `UIInventory` family | ✅ | ⚠ partial — the `commodity list` recipe covers `UIInventory`; this subclass unverified |
| T11 | `UIMissionSummary` / `UIChosenMissionSummary` | End-of-mission recap | ❌ | ✅ | ❌ |

Tactical narrative goes through `UINarrativeCommLink` — see §4.

---

## 2. AVENGER layer

| # | Class | What the player sees | Retained? | Keyboard? | Status |
|---|---|---|---|---|---|
| A1 | `UIDialogueBox` | Confirm build, cancel project, overwrite save, dismiss staff, scrap item, "you cannot afford this" | ✅ | ✅ | ✅ **done** |
| A2 | **`UIAlert`** | **The entire Avenger notification system** — research complete, item built, facility complete, room cleared, upgrade complete, staff slot open/filled, new staff available, soldier promoted, class earned, psi training done, soldier shaken/recovered, proving-ground done, shadow project done, weapon upgrades available, customizations available, negative/positive trait acquired, soldier tired, breakthrough/inspired research, and ~90 more | ❌ Flash-only, **but `eAlertName` (a Name) and `DisplayPropertySet` are retained** | ✅ Enter/Space/Esc, plus `X` for "Tell me more" on objectives | ❌ **the single biggest gap in the strategy layer** |
| A3 | `UIStrategyTutorialBox` | Avenger tutorial help blade (bottom-left) | ✅ **`m_strHelpText` retained outright** | n/a (`Show`/`Hide` only) | ❌ — cheapest item on this whole list |
| A4 | `UINarrativeCommLink` | Central / Tygan / Shen radio portrait + subtitle. **This is how essentially all story dialogue is delivered** | ✅ via `m_kNarrativeMgr.CurrentOutput` (`strTitle`/`strText`/`strImage`/`fDuration`) | n/a (auto-advances with the VO) | ❌ — **very high value**: the story is currently inaudible to a screen reader unless the player also parses the voice acting |
| A5 | `UISoldierBondAlert` | "These two soldiers can bond" | state refs + localized strings ✅ | ✅ Enter/Space/Esc | ❌ |
| A6 | `UIAbilityPopup` | New ability unlocked for a soldier | `UnitRef` only | ✅ Enter/Space/Esc | ❌ |
| A7 | `UIFactionPopup` | Faction met / influence increased / order slot unlocked | `FactionRef` + `bInfluenceIncreased` + localized strings ✅ | ✅ Enter/Space/Esc | ❌ |
| A8 | `UIChosenFragmentRecovered` | Chosen stronghold intel fragment gained | `ChosenRef` + `FragmentLevel` ✅ | ✅ Enter/Space/Esc | ❌ |
| A9 | `UIChosenLevelUp` | A Chosen grew stronger (new strengths) | `ChosenRef` + `DisplayPropertySet` ✅ | ✅ Enter/Esc | ❌ |
| A10 | `UIInputDialogue` | Name-your-save, crash reporter | Flash TextField | ✅ typing | ✅ **done** (`text_input.cpp`) |
| A11 | `UIProgressDialogue` | Saving / loading spinner | ✅ | ✅ Esc | ❌ (same item as T8) |
| A12 | `UIAlert_DLC_Day60` / `UIAlert_DLC_Day90` | Alien Hunters / Shen's Last Gift intro alerts | `UIAlert` subclasses | ✅ | ❌ (free once A2 lands) |
| A13 | `UIEventNotices` | Avenger toast stack | ✅ `Notices` | n/a | ❌ (same item as T4) |

---

## 3. GEOSCAPE layer

The geoscape shares `UIAlert` with the Avenger — most geoscape "popups" ARE `UIAlert`
variants. What is distinct here is the **mission blade** family.

| # | Class | What the player sees | Retained? | Keyboard? | Status |
|---|---|---|---|---|---|
| G1 | **`UIAlert`** (geoscape variants) | Council comm, guerrilla ops, council mission, retaliation, supply raid, landed UFO, alien facility, Avatar doom / hidden doom, UFO inbound / evaded, new objective, contact made, outpost built, continent bonus, region unlocked, black market appeared, dark event activated, mission expired, time-sensitive mission, Avatar victory imminent, low intel / supplies / scientists / engineers, new scanning site, scan complete, resource cache, POI available, covert actions, Chosen ambush / stronghold / sabotage / favored, Lost and Abandoned, rescue soldier, strategy card received | ❌ Flash-only (`eAlertName` + `DisplayPropertySet` retained) | ✅ Enter/Space/Esc | ❌ **same item as A2 — one fix covers both layers** |
| G2 | `UIMission` + **16 subclasses** | The mission-launch blade: GOps, Council, Retaliation, Supply Raid, Landed UFO, Alien Facility, Golden Path, GP Intel Options, Chosen Ambush / Stronghold / Avenger Assault, Lost and Abandoned, Rescue Soldier, ResOps, Alien Nest, Lost Towers. Objective, reward, sitrep, difficulty, Launch / Ignore | mixed | ✅ | ◐ built — `mission blade` recipe + `X2AccessMissionReader` (`mission-blade-recon.md`); first live pass owed |
| G3 | `UIUFOAttack` | The Avenger is under attack — confirm the defence mission | localized strings ✅, `MissionRef` ✅, `Button1..3` are real `UIButton`s | ✅ | ❌ |
| G4 | `UISitRepInformation` | Sitrep detail popup off a mission blade | `MissionRef` + `MissionData` ✅ | ✅ | ❌ |
| G5 | `UIResistanceReport` (+ `_ChosenEvents`, `_FactionEvents`) | The monthly resistance report | ❌ | ✅ | ❌ |
| G6 | `UIRewardsRecap` | Post-mission reward summary | ❌ | ✅ | ❌ |
| G7 | `UIAdventOperations` | Dark event / ADVENT operations status popup | localized strings ✅ | ✅ | ❌ |
| G8 | `UIStrategyTutorialBox` | Geoscape tutorial help blade (`m_bIsStrategy`) | ✅ `m_strHelpText` | n/a | ❌ (same item as A3) |
| G9 | `UINarrativeCommLink` | Central over the radio while scanning | ✅ via narrative mgr | n/a | ❌ (same item as A4) |
| G10 | `UIDialogueBox` | Confirm scan, confirm contact, "not enough intel" | ✅ | ✅ | ✅ **done** |

---

## 4. Cross-layer notes worth separating out

- **`UINarrativeCommLink` (A4/G9/tactical)** — spawned by `UIMovie_2D` and **never pushed
  on the screen stack**, so it needs the `UIDialogueBox`-style GObjects /
  `m_2DMovie.CommLink` lookup rather than `FindStackScreen`. Its text lives one hop away
  on `UINarrativeMgr.CurrentOutput`, a plain retained struct. This is a POLL, not a
  recipe — nothing is navigable and it auto-advances with the voice line.
- ⛔ **`UINarrativePopup` is DEAD CODE.** Both spawn sites in `XComPresentationLayerBase`
  (around lines 1153 and 1171) are inside a `/* … */` block. It also handles only
  `FXS_BUTTON_A` and a Flash `theStartButton` click — so had it shipped, it would have
  been unadvanceable by keyboard. Do not spend time on it.
- ⚠ **`UIEventNotices` retains everything** (`Notices : array<UIEventNoticeItem>`), which
  contradicts the note in CLAUDE.md. It is the same polling shape as `UIWorldMessageMgr`
  and should be nearly free once T4 is written.
- **Tooltips are a separate, larger problem** and are deliberately not in this list: they
  are hover-only, have no keyboard trigger anywhere in the game, and belong to a
  "read the focused node's tooltip" feature rather than to popup handling.

---

## 5. Suggested order

Ranked by (players affected) × (how mute the surface is today) ÷ (cost).

1. **`UIAlert`** (A2 + G1) — one screen, 118 variants, both strategy layers. It is the
   Avenger's entire notification system and most of the geoscape's. Everything needed is
   retained (`eAlertName`, `DisplayPropertySet`), so the companion re-derives the words
   the way `X2AccessSaveReader` re-derives a save row — and because the variants share a
   handful of `Build*Alert` shapes, the work is a switch, not 118 readers.
2. **`UINarrativeCommLink`** (A4/G9) — the story itself. Small: one poll, one retained
   struct.
3. **`UIEventNotices`** (T4/A13) — a second `m_arrMsgs`; the poll already exists in shape.
4. **`UIProgressDialogue`** (T8/A11) — currently indistinguishable from a hang.
5. **`UIStrategyTutorialBox`** (A3/G8) — one retained string; near-zero cost.
6. **`UIMission` family** (G2) — 17 classes, one layout, the commit-to-a-mission screen.
7. **`UICombatLose`** (T7) — being unable to leave a lost mission is a hard stop.
8. **`UITacticalHUDMessageBanner`** (T5) via `X2Action_PlayMessageBanner` — Chosen events.
9. The remaining bespoke one-screen popups (A5–A9, G3, G4, G7), each cheap and independent.
10. `UIResistanceReport` / `UIRewardsRecap` / mission summaries (G5, G6, T11) — long
    read-only recaps, best as recipes once the above land.

---

## 6. The UIAlert Flash field map (recovered 2026-08-21)

`UIAlert` retains none of its 118 variants' words. This is where they actually are, and how the
map was obtained — because it decides whether `UIAlert` is a week of re-derivation or an
afternoon of reading.

### How it was extracted, with no live game

1. `XComGame/CookedPCConsole/gfxAlerts_SF.upk` — the package `UIAlert.Package` names
   (`/ package/gfxAlerts/Alerts`).
2. ⛔ **The WOTC copy is unreadable and the BASE-GAME copy is not.** WOTC's package sets
   `COMPRESSION_FLAGS 0x08` (the encrypted LZO variant) with one 2 MB block; the base game's sets
   `0x02` (plain LZO1X) with 131072-byte blocks. A stock LZO1X decompressor handles the second
   and fails on the first — and it fails in a way worth recognising, since plaintext literals
   still show through an encrypted stream: the first name in the table reads `"Adve"` and then
   turns to noise, where the base game reads `"Advent_Icon"` in full.
3. So the map comes from `XCOM 2/XComGame/CookedPCConsole/gfxAlerts_SF.upk` — the pre-WOTC
   package, still installed beside the expansion. Decompress the LZO chunks, find the `GFX`
   magic (Scaleform's uncompressed SWF variant, **not** `CWS`/`CFX` — those never appear), and
   walk the tag stream collecting `DoInitAction` (tag 59) `ActionConstantPool` (opcode 0x88)
   entries, keyed by character id through `ExportAssets` (tag 56).
4. The constant pool of each `__Packages.Alert_*` class lists, IN ORDER, the child clips its
   `UpdateData` assigns — which is both the field list and the reading order.

Scripts live in the session scratchpad, not the repo; they are ~200 lines of Python and are
reproducible from this description.

### Reading a field

Each entry below is a child of `LibraryPanel.MCPath`. Three shapes, and the decompile says which:

| Shape | Read as | Used by |
|---|---|---|
| `XComScrollingTextField` / `XComButton` | `<field>.text` — the class calls `addProperty('text', …)`, so GFx's `GetVariableString` runs the getter | most fields |
| plain clip wrapping a TextField named `theText` | `<field>.theText.text` | the HEADER field of nearly every panel |
| raw TextField | `<field>.textField.text` | fallback |

Buttons are always `ButtonGroup.Button0` (confirm) and `ButtonGroup.Button1` (cancel) — note the
Flash names are 0/1 while the UnrealScript vars are `Button1`/`Button2`.

### The 20 base-game panels, fields in UpdateData order

`(h)` marks the `theText` shape; everything else is the `.text` getter.

| Library symbol | Fields |
|---|---|
| `Alert_AlienSplash` | headerMC (h), titleMC, descriptionMC |
| `Alert_AssignStaff` | headerMC (h), nameMC, bonusMC, titleMC, descriptionMC |
| `Alert_BlackMarket` | logoTitle, title, subTitle, description, footer, leftText, rightText |
| `Alert_Complete` | headerTitle (h), headerSubTitle, alertTitle, alertDescription |
| `Alert_ContactMade` | titleMC (h), nameMC, descriptionMC, incomeLabel, incomeValue, rewardLabel, rewardValue |
| `Alert_CouncilReport` | councilTitle (h) |
| `Alert_DarkEventActive` | headerMC (h), titleMC, descriptionMC, quoteMC, quoteNameMC |
| `Alert_Help` | headerMC (h), titleMC, descriptionMC |
| `Alert_ItemAvailable` | headerTitle (h), headerField, header, headTitle, headName |
| `Alert_MakeContact` | titleMC (h), nameMC, costLabelMC, subLabelMC, costValueMC, costDescriptionMC, daysLabelMC, daysValueMC, descriptionMC, hintMC, rewardLabelMC, rewardValueMC |
| `Alert_NewObjective` | headerTitle (h), headerSubTitle, alertTitle, alertDescription, alertObjectiveText |
| `Alert_NewRegion` | titleMC (h), nameMC, descriptionMC, hintMC |
| `Alert_NewStaff` | NewStaffHeader (h), NewStaffName, NewStaffBonus, NewStaffTitle |
| `Alert_NewStaffListItem` | facilityName, facilityBonus |
| `Alert_OpenStaff` | headerMC (h), titleMC, descriptionMC, wideTipMC |
| `Alert_POI` | headerTitle (h), headerSubTitle, alertDescription, POILabel01, POIValue01, POILabel02, POIValue02 |
| `Alert_SpecialPOI` | headerTitle (h), headerSubTitle, alertDescription, POILabel01, POIValue01 |
| `Alert_TrainingComplete` | headerMC (h), rankNameMC, nameMC, classNameMC, abilityLabelMC, abilityNameMC, descriptionMC |
| `Alert_Warning` | headerMC (h), titleMC, descriptionMC |
| `Alert_XcomGeneric` | headerMC (h), titleMC, descriptionMC |

### ✅ WOTC's packages: flag 0x08 is OODLE, and the game ships the decoder

An earlier pass concluded this was an encrypted LZO variant and a dead end. **That was wrong.**
Flag 0x08 is **Oodle**, and the tell was sitting in the install the whole time:

    XCom2-WarOfTheChosen/Binaries/Win64/oo2core_4_win64.dll     <- present
    XCOM 2/Binaries/Win64/                                      <- absent

That maps exactly onto the compression survey — base game **0x02** (LZO1X) on 4603 of 4648
packages, WOTC **0x08** on 6189 of 6207. One install ships Oodle and uses 0x08; the other ships
neither. Every LZO/zlib/XOR attempt failed because the data was never LZO.

So there is no external tool needed and nothing to crack: load the game's own DLL and call
`OodleLZ_Decompress` with the Oodle 2.4-era 14-argument signature —

    OodleLZ_Decompress(comp, compLen, raw, rawLen,
                       fuzzSafe=1, checkCRC=0, verbosity=0,
                       decBufBase=NULL, decBufSize=0,
                       fpCallback=NULL, callbackUserData=NULL,
                       decoderMemory=NULL, decoderMemorySize=0,
                       threadPhase=3 /* Unthreaded */)

— per UE3 block, dispatching on the package's own `COMPRESSION_FLAGS`. The first WOTC block then
decodes to `0c 00 00 00 "Advent_Icon"`, the same name table the base game starts with.

⚠ **Two panels differ between the base and WOTC movies**, so the base-game map alone would have
been subtly wrong: `Alert_ItemAvailable` **drops `headerField`** in WOTC (and gains a decorative
`headerDags`), and `Alert_NewStaff` / `Alert_TrainingComplete` gain a `factionLogo` image. The
table above is the base-game reading; the WOTC-only panels are listed below.

### The WOTC-only panels

`UIAlert.GetLibraryID()` can return exactly **30** symbol names (read off its own switch), and the
reader's table now covers all of them. The fourteen not in the base movie:

| Library symbol | Fields |
|---|---|
| `Alert_Breakthrough` | breakthroughTitle, descriptionTitle, descriptionText — ⚠ **no ButtonGroup**: its buttons are `breakthroughButton` / `abandonButton` |
| `Alert_ChosenSplash` | titleMC, chosenType, chosenName, chosenNickname, descriptionMC |
| `Alert_CouncilMissionSplash` | splashTitle, splashSubtitle, splashDescription, splashGreebleText |
| `Alert_GuerrillaOpsSplash` | GOPSSplashTitle, GOPSSplashHeader, GOPSSplashDescription |
| `Alert_NegativeSoldierEvent` | headerMC, rankNameMC, nameMC, classNameMC, abilityLabelMC, abilityNameMC, descriptionMC |
| `Alert_NewStaffSmall` | NewStaffHeader, NewStaffTitle, NewStaffName, NewStaffBonus |
| `Alert_ProvingGroundAvailable` | header, headTitle, headName |
| `Alert_RetaliationSplash` | headerMC, subHeaderMC, descriptionMC, footerMC |
| `Alert_SupplyRaidSplash` | splashHeader, splashTitle, splashDescription, splashGreebleText |
| `Alert_SitRep` | theTitle (+ `SitRepContainer`, a list — unread) |
| `XPACK_Alert_CovertOpConfirm` | titleMC, confirmHeader, missionName, rewardLabel, rewardDescription |
| `XPACK_Alert_CovertOps` | titleMC, descriptionText |
| `XPACK_Alert_MissionSplash` | titleMC, descriptionText |
| `XPACK_Alert_Retribution` | theRegion, theDescription, bottomLeftText, bottomRightText |

⛔ **These fourteen carry NO AS2 class**, so their fields are the sprite's placed child-instance
names rather than an `UpdateData` constant pool. That means the decompile says *which* children
exist but not *how* each wraps its text — which is why `ReadAlertField` always tries both
`.text` and `.theText.text` rather than trusting the shape. Guessing wrong would silently drop a
line, and a dropped line reads to a blind player exactly like a screen with nothing more to say.

---

## 7. Build status (2026-08-21)

Both halves compile: the ASI builds clean and `X2AccessCompanion` compiles **0 errors,
0 warnings**. **Nothing below has run live.**

### Built

| Piece | Where |
|---|---|
| Popup finder, `SendScreenCommand`, alert Flash read-back + cache | `src/game_ui.cpp` (`FindPopupScreen`, `ReadAlertText`) |
| `popup` recipe — message stop (one node per LINE), buttons stop | `src/menu_navigator.cpp`, probed 2nd after `dialog` |
| Words for 12 popup classes | `X2AccessPopupReader.uc` |
| Commlink subtitle, toast stack, strategy help blade — polled at 250 ms | `src/transient_reader.cpp` + `X2AccessTransientReader.uc` |

Covered: `UIAlert` (+ both DLC subclasses), `UIFactionPopup`, `UIAbilityPopup`,
`UISoldierBondAlert`, `UIChosenFragmentRecovered`, `UIChosenLevelUp`, `UIChosenReveal`,
`UIUFOAttack`, `UIProgressDialogue`, `UICombatLose`, `UISitRepInformation`,
`UIAdventOperations`, plus §4's three transient surfaces. Added 2026-09-03, unexercised:
`UIChosenEncountered` and `UISoldierCaptured` (`chosen-bond-recon.md` §1.2-1.3) — both answer
Enter/Space and NOT Escape, so their record carries a single button.

The popup family hand-transcribes **no offsets** and adds **no `static_assert`s** — activation is
each screen's own `OnUnrealCommand`, so there are no button offsets to pin.

### Known gaps

- ⛔ `UIRedScreen` is recognised but **unreadable by design**: `m_strErrorMsg` and
  `RedscreenLines` are both `private`, so no companion can reach them. It stays in the popup
  table anyway — announcing that a script-error overlay is up, and offering the key that
  dismisses it, beats treating the screen as invisible. Reading its text would need a native
  offset this family otherwise does not have.
- The XPACK alert symbols (§6) — union fallback, and every unrecognised symbol is logged.
- `UICombatLose`'s three verbs are spoken but **not pressable**: `AS_SetDisplay` hands them to
  Flash and the handlers are mouse-only, so the recipe offers Enter/Escape. Speaking them is
  parity; making them work needs the Flash click path.
- Not attempted: the `UIMission` blade family (G2), `UIResistanceReport`, `UIRewardsRecap`,
  the mission summaries, and the Chosen message banner (T5, which needs the visualization
  action rather than the widget).

### First live pass should check

1. An alert reads its **body**, not just its buttons — that is the whole Flash read-back bet.
   `just dev-log` will carry `Popup: unknown alert symbol …` for any XPACK panel.
2. Tab reaches the message as **separate lines** rather than one paragraph.
3. Enter and Escape actually dismiss — especially `eAlert_TrainingComplete`, where the game
   routes Enter to *cancel* rather than confirm.
4. The commlink speaks story dialogue **with the speaker's name**, and does not repeat a line as
   its subtitle scrolls (the marker in `X2AccessTransientReader` is what prevents that).
5. Shape-count the ring afterwards
   (`grep -oP 'TTS!?: "\K[^"]*' X2Access.log | sed 's/[0-9]\+/N/g' | sort | uniq -c | sort -rn`).
   Frequent `No readable text` means a missing field name in the §6 table, not bad wording.

---

## 8. ⛔ Flash cannot give you the alert's MESSAGE — the screen can (measured live 2026-08-22)

The Flash read-back in §6 was half right, and the half that was wrong is the important half.
Measured on a live `eAlert_LostAndAbandoned` (`XPACK_Alert_MissionSplash`):

| Probe | Result |
|---|---|
| `ButtonGroup.Button0.text` | ✅ `"<p align='CENTER'>ПОКАЗАТЬ ЗАДАНИЕ</p>"` — `XComButton.text` is a plain var |
| `titleMC`, `descriptionText` | resolve as clips (`titleMC._width` = 665.75) — the field names from §6 are **correct** |
| `titleMC.text` / `.htmlText` / `._text` / `.textField` / `.theText` / `getText()` | ❌ all empty |
| `UIMCController.CommandQueue` | ❌ empty array — `EndOp()` flushes and clears it |

`XComScrollingTextField` exposes its text through an **`addProperty` getter**, which GFx's
`GetVariableString` cannot run, and the backing storage is not reachable by path either. So the
message text is genuinely unreachable through the movie. Buttons work only because `XComButton`
happens to keep a plain variable.

### What answers instead: the screen's own localized vars, by NAME

Every `Build*Alert` resolves its words out of `UIAlert`'s own `localized` strings, and those are
retained — **383** of them — under a naming convention:

    eAlert_LostAndAbandoned  ->  m_strLostAndAbandonedTitle / ...Label / ...Body / ...Confirm

Measured across the whole enum, **81 of 118** alert names follow it. This is the one thing native
can do and the companion cannot: reading a property *by name* is what UE3 reflection is, and
UnrealScript has no equivalent. `ReadAlertLocalized` derives the stem from `eAlertName` and reads
`m_str<Stem><Suffix>` off the screen — no Flash calls, no per-alert code, correctly localized.

Suffixes are chosen by counting `QueueString` calls in `UIAlert.uc`, the only honest test of
whether a suffix ever reaches the screen: **Title 48, Header 24, Body 25, Label 20,
Desc/Description 3, Text 0**. `Text` is declared but never displayed, so it is not read; `Flare`
is queued 7 times but only as the map caption.

Verified against a live screenshot: `m_strLostAndAbandonedTitle` = *ВХОДЯЩЕЕ СООБЩЕНИЕ!* and
`m_strLostAndAbandonedBody` = *Шеф, нам надо выступить посредниками в делах двух фракций
Сопротивления.* — exactly the two strings on screen.

⚠ **Known over-read.** An alert uses only some suffixes and the convention cannot say which:
`eAlert_LostAndAbandoned` shows Title and Body but not Label, so its Label is spoken though a
sighted player never sees it. Bounded and one-directional — the alternative drops a line that 20
other alerts genuinely show — but it is the first thing to measure on the next live pass.

The §6 Flash table is kept as the FALLBACK for the 37 alert names outside the convention, and
still supplies the button labels, which it reads correctly.

---

## 9. First live pass (2026-08-22) — what actually happened

Run on a real WOTC campaign. **The popup layer works.** Corrections to §8, which overstated its case:

### ⚠ §8 was too broad: Flash read-back DOES work — on the BASE panels

`eAlert_ClassEarned` fell through to Flash on `Alert_TrainingComplete` and read the whole panel:

    СОЛДАТ ПОЛУЧИЛ НОВОЕ ЗВАНИЕ · РЯДОВОЙ · Ряд. Svetlana Sokolova ·
    ГРЕНАДЕР · НОВАЯ СПОСОБНОСТЬ · Выстрел гранатой
    [ПОКАЗАТЬ БОЙЦА] [ДАЛЬШЕ]

So the correct statement is narrower than §8 claimed: the base-game panels expose their text
through readable clips, and the **XPACK** panels (`titleMC` / `descriptionText` on
`XPACK_Alert_MissionSplash`) do not. The two-source design — localized vars first, Flash second —
is what makes that difference invisible to the player, and each source covers the other's gap.

### Convention misses seen so far

Both logged, both recovered by the Flash fallback:

| Alert | Symbol | Why it missed |
|---|---|---|
| `eAlert_ClassEarned` | `Alert_TrainingComplete` | no `m_strClassEarned*` vars — the builder uses the unit's own class/ability data |
| `eAlert_BuildSlotFilled` | `Alert_AssignStaff` | builder uses `m_strConstructionSlotFilled*` and `m_strStaffSlotFilledTitle` — a DIFFERENT stem from the alert name |

`eAlert_BuildSlotFilled` is the interesting shape: the convention assumes stem == alert name, and
here the game uses a synonym. Not worth hand-mapping until a live run shows an alert that misses
the convention AND has no readable Flash panel — neither of these did.

### ✅ The narrative commlink works

Story dialogue reads with the speaker attributed, in every layer:

    "Подстрекатель": Мы на точке, все чисто.
    Центр: Жнецы и Заступники враждуют уже долгие годы, поэтому точку встречи мы держим в секрете…

### ⛔ Fixed: the message read ACROSS instead of DOWN

The message lines were built inside one `StartRow()`, so Left/Right walked them and each carried a
positional stamp — *"СОЛДАТ ПОЛУЧИЛ НОВОЕ ЗВАНИЕ, 1 of 6"*, a menu position on a sentence. A
message is prose stacked vertically, so each line is now its own single-item row: Up/Down walks
it and the stamp is gone (GraphBuilder only stamps positions within a row holding more than one
item). The BUTTONS keep their row — they really are side by side.

### Still unmeasured

The `Label` over-read from §8 has not yet been seen in the ring, because every alert so far either
missed the convention or did not set Label. Worth watching on a `Complete`-family alert.

## 10. ⛔ A panel is painted by SEVERAL batches, and the last one is often a picture (2026-09-03)

Reported live as *"we don't speak this screen"* on `eAlert_StaffInfo` (the PERSONNEL DATA popup).
The screen was attached and spoke exactly one line — its title — and lost the recruit's name, the
bonus label and both facility rows.

**The mechanism.** `GetOrStartWaitingForStaffImage` (`UIAlert.uc:3972`) starts an ASYNCHRONOUS
photobooth headshot request. Its callback `UpdateAlertImage` (`:3988`) pushes a *later* batch
through the same `LibraryPanel.MC` the prose went through:

```unrealscript
LibraryPanel.MC.BeginFunctionOp("UpdateImage");
LibraryPanel.MC.QueueString(class'UIUtilities_Image'.static.ValidateImagePath(PathName(StaffPicture)));
LibraryPanel.MC.EndOp();
```

The reader took the newest string-carrying batch, so it took the picture; the asset filter
correctly dropped its single operand; the alert reported nothing captured and fell through to the
localized-var fallback, which for `Alert_NewStaff` yields `m_str<Stem>Title` and nothing else
(`Bonus` is not one of the six convention suffixes, and the name is runtime data). And because that
fallback returned one non-empty line, the third fallback — the Flash field sweep — never ran.

**Distribution, over four sessions of `X2Access*.log`** (the audit CLAUDE.md prescribes; `.UpdateData`
drops are healthy — an image riding inside the prose batch — while `.Update*Image`/`*Icon` drops are
a whole batch lost):

```
7  eAlert_SoldierPromoted.UpdateImage        6  eAlert_ClassEarned.UpdateImage
6  eAlert_NewStaffAvailableSmall.UpdateImage 5  eAlert_StaffSlotFilled.UpdateImage
3  eAlert_StaffInfo.UpdateImage              3  eAlert_NewStaffAvailable.UpdateImage
2  eAlert_ClearRoomSlotFilled.UpdateImage    2  eAlert_XPACK_Retribution.UpdateRetributionChosenIcon
```

Severity varied by PANEL, not by bug: `eAlert_ClassEarned` degraded to the Flash sweep and got six
lines back because `Alert_TrainingComplete` uses plain `theText` fields; `Alert_NewStaff` could not,
its fields being `XComScrollingTextField`s the path API cannot read at all.

**Multi-batch builders** (comments excluded; `AnimateIn` ignored). Nine more reach `UpdateImage`
through the async callback rather than their own body — `BuildTrainingCompleteAlert`,
`BuildPsiTrainingCompleteAlert`, `BuildSoldierPromotedAlert`, `BuildPsiOperativeIntroAlert`,
`BuildNewStaffAvailableAlert`, `BuildStaffInfoAlert`, `BuildConstructionSlotFilledAlert`,
`BuildClearRoomSlotFilledAlert`, `BuildStaffSlotFilledAlert`:

| Builder | Batches, in push order |
|---|---|
| `BuildNewStaffAvailableAlert` | UpdateFacilityList, UpdateData |
| `BuildStaffInfoAlert` | UpdateFacilityList, UpdateData |
| `BuildSuperSoldierAlert` | UpdateData, UpdateImage (synchronous) |
| `BuildConfirmCovertActionAlert` | UpdateCovertInfoBlade, UpdateCovertButtonBlade (+ UpdateCovertSlot, SetFactionIcon via helpers) |
| `BuildRetributionAlert` | UpdateRetributionTitle, …Description, …InfoPanel, …ChosenPanel, …ChosenIcon |
| `AddChosenInfoPanel` (helper) | SetChosenName, UpdateChosenIcon — on top of its caller's own batch |

**The fix.** `ReadAlertCaptured` reads EVERY batch on the panel, in push order, and `GetTextCall`
is deleted so nothing can make the assumption again. Concatenating is sound because panel identity
is per alert: `BindLibraryItem` (`:1044`) spawns a fresh `UIPanel` for each `UIAlert` instance, so
every batch on that cache index belongs to that alert, and `CLEANUP_OP` evicts it when the panel
goes.

⚠ **The one ordering exception is a batch that FEEDS a child list.** `UpdateFacilityList` fills the
`Alert_NewStaffListItem` rows that sit *under* the "available staff bonus" label the following
`UpdateData` supplies, so Flash needs it FIRST while a reader wants it LAST — otherwise a recruit's
bonuses are read before the recruit is named. It is a named exception of one (`IsListFeederBatch`),
not a policy: everywhere else push order is reading order.

⚠ **Watch for over-read on the four-and-five-batch alerts.** Retribution and the covert-action
confirmation now contribute every batch they push, and no one has heard them since the change; the
`read N line(s) from the Flash capture (A+B+C, …)` log line names which batches contributed.
