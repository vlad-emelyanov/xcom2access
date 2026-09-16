# Long War of the Chosen — mod-ecosystem recon

Survey date 2026-09-16, read off shallow clones of the three mods' own repositories, the SDK's
`Development/SrcOrig`, and this tree's `src/`. **Nothing here has been measured in a running
game** — LWOTC is not installed on this machine (the workshop folder holds eleven items; LWOTC and
its dependencies are not among them). §10 is the list of things the first live pass has to answer.

This is the first recon in `docs/` that is not about a screen. X2Access has only ever targeted
vanilla WOTC — a `grep` for `long war`, `modded`, `ModClassOverride` or `mod compat` over `docs/`,
`README.md` and `CLAUDE.md` returns nothing but an aside about modded factions. So the first
question this document answers is not "how do we read this screen" but "what does our architecture
already survive, and where does it stop".

## 0. Where the sources are

Shallow clones under `refmods/`, which `.gitignore` excludes. Read-only reference; never a build
input, never ours to commit.

| Path | Repository | Size |
|---|---|---|
| `refmods/lwotc` | `long-war-2/lwotc` (master) | 346 MB |
| `refmods/robojumperSquadSelect` | `robojumper/robojumperSquadSelect` | 829 KB |
| `refmods/X2CommunityPromotionScreen` | `X2CommunityCore/X2CommunityPromotionScreen` | 1.1 MB |

`BetterSecondWaveSupport` is vendored inside the LWOTC repo at its root, so it needs no separate
clone. Mod Config Menu is already installed on this machine and ships its full `Src/` in the
workshop item (`.../268500/667104300/Src/`).

⛔ Building LWOTC from source needs the Shen's Last Gift DLC, the `full_content` SDK branch, and
three hand-patches to the SDK's `SrcOrig`. **We do not need to build it** — recon reads the
repository, and testing runs the Workshop build. Do not let the build instructions in its README
pull this tree onto that path.

## 1. LWOTC is five mods, not one

`LongWarOfTheChosen/Config/XComGame.ini:162-166` declares:

| Required mod | What it does to the UI | Do we cover the screen today |
|---|---|---|
| `X2WOTCCommunityHighlander` | — | ✅ we already build against it |
| `robojumperSquadSelect_WotC` | replaces squad select wholesale | ✅ `squad select`, `X2AccessSquadReader.uc` |
| `X2WOTCCommunityPromotionScreen` | replaces **both** promotion screens | ✅ `promotion`, `X2AccessPromotionReader.uc` |
| `BetterSecondWaveSupport` | replaces `UIShellDifficulty` | ✅ shell recipe, and `surfaces/shell.md` has rules earned there |
| `WOTCRevertOverwatchRules` | gameplay only, no UI | n/a |

So "make LWOTC accessible" means making three more mods accessible, and all three replace screens
we already read. Two of them are worth doing on their own account — robojumper's squad select and
the Community Promotion Screen are widely run *without* LWOTC.

## 2. The first kind of work: renamed screens

LWOTC installs twelve `ModClassOverrides` (`Config/XComEngine.ini:59-85`, plus one in
`XComLW_Overhaul.ini:4`). Every one is a **direct subclass** of the class it replaces:

```
UIAfterAction_LW extends UIAfterAction                    UIRecruitSoldiers_LW extends UIRecruitSoldiers
UIAfterAction_ListItem_LW extends UIAfterAction_ListItem  UIOptionsPCScreen_LW extends UIOptionsPCScreen
UIAdventOperations_LW extends UIAdventOperations          UICovertActionReport_LW extends UICovertActionReport
UIMission_ChosenAmbush_LW extends UIMission_ChosenAmbush  UIStrategyMapItem_Region_LW extends UIStrategyMapItem_Region
UIInventory_LootRecovered_LW extends UIInventory_LootRecovered
UIInventory_VIPRecovered_LW extends UIInventory_VIPRecovered
UIPersonnel_SoldierListItem_LW extends UIPersonnel_SoldierListItem
UIStrategyMapItem_Mission_LW / UIStrategyMapItem_AlienNetworkComponent_LW, likewise
```

The two dependency mods do the same thing:

- `robojumper_UISquadSelect extends UISquadSelect`, installed by `ModClassOverrides`
  (`robojumperSquadSelect/Config/XComEngine.ini:12`).
- `CPS_UIArmory_PromotionHero extends UIArmory_PromotionHero`, installed **not** by
  `ModClassOverrides` but through the Highlander event `OverridePromotionUIClass`
  (`X2EventListener_PromotionScreenOverrides.uc`). ⚠ It answers for `eCHLPST_Hero` **and**
  `eCHLPST_Standard`, so with CPS installed *every* promotion — rookie to colonel — runs through a
  class descending from `UIArmory_PromotionHero`. Our `PromotionKind::Classic` path goes dead and
  the hero path carries the whole game.

Subclassing means **field offsets stay valid**: UnrealScript appends a subclass's vars after the
parent's and never reorders. What breaks is finding the screen at all.

### 2.1 Why it breaks: we match by `strcmp`

`FindStackScreenRaw` (`src/game_ui.cpp:750`) compares class names with `strcmp`; so do
`IsInstanceRaw` (`:282`) and `ObjectClassNamedRaw` (`:919`). Nine call sites across five TUs name a
class LWOTC or a dependency renames out from under them:

| TU | Class named |
|---|---|
| `game_ui.cpp` | `UIAfterAction`, `UIShellDifficulty`, `UISquadSelect` |
| `game_ui_avenger.cpp` | `UIPersonnel`, `UIRecruitSoldiers` |
| `game_ui_popup.cpp` | `UIAdventOperations` |
| `game_ui_shell.cpp` | `UIOptionsPCScreen` |
| `game_ui_strategy.cpp` | `UIArmory_Promotion`, `UICovertActionReport` |

✅ **The companion side survives untouched.** `UIScreenStack.GetFirstInstanceOf` matches with
`IsA` (`SrcOrig/XComGame/Classes/UIScreenStack.uc:620-630`), which is subclass-tolerant — and
CLAUDE.md already mandates that call over a held reference. Only `GetScreen`, which compares the
class object directly, is exact, and we do not use it.

### 2.2 ⛔ Subclass tolerance is necessary but not sufficient

Making the finders walk `SuperField` recovers the screen. It does **not** recover the read, and the
difference is the failure mode that matters: a reader that claims a screen and then reads it empty
narrates silence, which a blind player cannot tell from *the screen has nothing on it*.

Sorting the overrides by how much they actually change:

| Override | Shape | Cost once matching is tolerant |
|---|---|---|
| `UIRecruitSoldiers_LW` | one function | free |
| `UICovertActionReport_LW` | two functions, calls `super` | free |
| `UIAdventOperations_LW` | six functions, calls `super` | ~free |
| `UIShellDifficultySW` | rebuilds the Second Wave list but repopulates the **parent's** `m_SecondWaveList` and `m_Difficulty*MechaItem` fields | probably free; re-check against `surfaces/shell.md`'s Second Wave rules |
| `UIAfterAction_LW` | adds `m_kSlotList2/3/4` — three more soldier rows, because LW squads outgrow one | small: read the three appended lists |
| `UIOptionsPCScreen_LW` | adds a `TabList` of per-mod option pages | small: one new tab stop |
| `robojumper_UISquadSelect` | ⛔ does **not** populate the inherited `m_kSlotList`; spawns its own `robojumper_UIList_SquadEditor SquadList` with `robojumper_UISquadSelect_ListItem` rows | a new read path — `X2AccessSquadReader.uc:76` loops `m_kSlotList.ItemCount` and would find it empty |

## 3. The second kind of work: genuinely new screens

Sized by source lines, most substantial first. All of them are `UIScreen`/`UIPanel` built from the
widget vocabulary the readers already handle.

| Screen | Lines | What it is |
|---|---|---|
| `UIMission_LWCustomMission` | 1238 | a `UIMission` subclass — the blade recipe's family gains a member |
| `UIOutpostManagement` + `_ListItem` | 1197 + 528 | one haven: rebel roster, job assignment, liaison, income |
| `UIArmory_LWOfficerPromotion` + item | 909 | the officer perk tree |
| `UIMission_LWLaunchDelayedMission` | 880 | second new `UIMission` subclass |
| `UIPersonnel_SquadBarracks` | 813 | LW2's persistent squads |
| `UISquadSelect_InfiltrationPanel` + `_InfiltrationItem` | 654 | infiltration %, covertness, timers |
| `UIResistanceManagement_LW` + `_ListItem` | 638 + 280 | the global haven table |
| `UISquadContainer` / `UISquadListItem` / `UISquad_DropDown` / `UISquadIconSelectionScreen` | ~750 | the rest of squad management |
| `UIPersonnel_Liaison`, `UIPersonnel_LWOfficer`, `UIPersonnel_LEBVS` | small | all extend `UIPersonnel` — may fall out of tolerant matching |
| `UIAlert_LWOfficerTrainingComplete` | small | one more row for the popup reader's class table |

For scale: `kRecipeCount` is 49 today (`src/recipes/recipes.h:82`). This is roughly 12–18 more, of
which 4–6 are substantial.

⛔ **`UIResistanceManagement_LW` and `UIOutpostManagement` draw column headings** — Region / Status
/ Rebel count / Adviser / Income, and Name / Job / Perks. That makes them `GraphSheet` surfaces, not
lists, by the rule in CLAUDE.md. Do not plan them as lists.

Also unread and adjacent: **Mod Config Menu** (`MCM_OptionsScreen extends UIScreen`, a `UIList` of
tabs over `MCM_SettingsPanel` pages). LWOTC routes much of its configuration through MCM, and MCM is
already installed here — it is a gap today regardless of LWOTC.

## 4. ✅ LWOTC's UI retains more text than vanilla's does

The house rule in CLAUDE.md — *a list row in this game retains no text* — is a statement about
Firaxis' style, and LWOTC only half-inherits it. Its rows spawn real `UIText` / `UIScrollingText`
widgets and call `SetHTMLText`, and `UIText` keeps both `Text` and `HtmlText`
(`SrcOrig/XComGame/Classes/UIText.uc:15-16`). Counting `UI*.uc` files under `LongWarOfTheChosen/Src`:

- **15 files** spawn retained `UIText`/`UIScrollingText` widgets → plain widget reads.
- **11 files** push straight to Flash via `MC.FunctionString` / `MC.BeginFunctionOp` → need
  `flash_shadow.cpp`'s `CopyCommandQueue` capture: `UIAdventOperations_LW`,
  `UIAlert_LWOfficerTrainingComplete`, `UIArmory_LWOfficerPromotion`, `UIInventory_VIPListItem`,
  `UIMission_LWCustomMission`, `UIMission_LWLaunchDelayedMission`, `UIPersonnel_SquadBarracks`,
  `UIScreenListener_Alert`, `UISquadSelect_InfiltrationItem`,
  `UITacticalHUD_AbilityContainer_LWExtended`, `UIVerticalScrollingText`.

Both patterns are already solved in this tree. Neither is a new technique.

## 5. ✅ Navigability matches the pattern we already handle

`UIResistanceManagement_LW` and `UIOutpostManagement` both set `List.bIsNavigable = true` and mark
everything else `bIsNavigable = false` explicitly — the sort headers, the liaison button, the radio
tower upgrade button. That is the vanilla shape: the list takes the arrows, the rest is mouse-only
for a sighted player too. The parity rule resolves it the same way it always has — those controls
get tab stops because a sighted player can reach them with the mouse.

## 6. The tactical layer

- `UITacticalHUD_AbilityContainer_LWExtended extends UITacticalHUD_AbilityContainer`, swapped in by
  a screen listener that re-spawns `HUDScreen.m_kAbilityHUD`
  (`UIScreenListener_TacticalHUD_LW.uc:33`). Offsets hold. ⚠ It **paginates** the ability bar and
  adds left/right scroll buttons — our ability-bar read has no concept of a page, and the shot
  HUD's `F1`-`F8` hotkeys are per-page under it. This needs measuring before it is designed.
- `UITacticalHUD_MouseControls_LWOfficerPack` adds a control to the strip we build tactical tab
  stops from — which is the right behaviour, since that strip is the game's own answer to "what
  does this HUD offer".
- Perfect Information is integrated and overrides `UIEffectListItem`.
- LW_Tutorial replaces the vanilla tutorial with `UIAlert`-shaped popups driven by seven
  `UIScreenListener_*Tutorial` classes. Our popup reader covers `UIAlert`; the vanilla tutorial
  reader's `UIWorldMessageMgr` polling is unaffected.
- Ability and item **names** need nothing: they resolve through templates, and the companion is
  template-generic. The only hardcodes in it are `'PsiOperative'`
  (`X2AccessHelper.uc:211`) and the `'Overwatch'` reserve-point check
  (`X2AccessUnitReader.uc:399`), both of which LWOTC keeps.
- What does need words is the new **concepts**: infiltration, haven jobs, rebel morale, liberation
  state, officer ranks. `Localization/XComGame.int` is ~8,100 lines.

## 7. ⛔ Do we need a second mod? Almost certainly not

Three separate answers, because "the mod" means three different things here.

**The ASI needs no companion at all.** Recipes match screens by class-name *string*, and a recipe
whose screen is absent simply never probes true. LWOTC recipes can ship inside X2Access with zero
hard dependency and zero cost to a vanilla player — same binary, more recipes. This is the whole
argument for doing LWOTC support natively.

**UnrealScript is the only place a second mod would be forced.** A `.u` that references
`XComGameState_LWOutpost` will not load without LWOTC present, so anything touching LW state would
have to live in a separate package (`X2AccessCompanion_LWOTC`, with
`+RequiredMods="LongWarOfTheChosen"`).

**But two things mean we mostly avoid that.** First, CLAUDE.md's hard rule already says to read the
fact *from the screen*, and §4 shows LWOTC's screens largely hold their facts in retained widgets.
Second, `ue3_reflection.cpp` reads arbitrary UObject properties **by name**, so even a raw
`XComGameState_LWOutpost` is reachable from C++ with no compile-time dependency on LWOTC.

**Decision: build it native-only. Do not create a second companion mod up front.** Keep it in
reserve for the handful of derivations that genuinely need script-natural resolution, and record
here which ones forced it if any ever do.

## 8. ⛔ The `static_assert` net does not reach third-party classes

Every hand-transcribed offset in this tree is pinned by a `static_assert` in
`src/sdk_layout_checks.cpp` against the generated SDK, so a bad transcription is a build error
rather than a silent wrong read. **The generated SDK contains no LW classes**, so that net stops at
the vanilla boundary — and a wrong LW offset would be exactly the silent-wrong-read the net exists
to prevent.

The fix is available and should be decided **before the first LW offset is transcribed, not
after**: the generator dumps from a **live process** (`sdk-gen` injects into the running game with
Frida — `docs/build.md`), so a dump taken with LWOTC loaded contains LW layouts. That implies a
second dump and a separate pinning TU, kept out of the vanilla build path. Note that LWOTC is not a
Highlander — it ships no replacement `XComGame`/`Engine`/`Core.upk` — so the *vanilla* layouts in
such a dump stay the ones we already have.

## 9. Proposed sequence

1. **Subclass-tolerant screen matching** in the native finders. Cheap, self-contained, testable on
   vanilla, and it makes X2Access survive `ModClassOverrides` from *any* mod, not just this one. ⛔
   It is not a blanket replacement: `UIArmory_PromotionHero` and `UIArmory_PromotionPsiOp` both
   extend `UIArmory_Promotion`, and `UIMission` has sixteen subclasses the blade recipe tells apart
   by name — those hierarchy probes must keep most-derived-first ordering. And it changes which
   recipe wins a screen, so `kRecipes` order has to be re-reasoned, not assumed.
2. Install LWOTC + the four dependencies, take an SDK dump with them loaded, confirm the LW layouts
   land and decide §8.
3. Recon the haven layer proper (`UIResistanceManagement_LW`, `UIOutpostManagement`) — it is the
   mechanic with no vanilla analogue at all, so it has nothing to inherit.
4. Then recipes, cheapest first: the two `UIMission` subclasses and the `UIAfterAction_LW` extra
   rows before the haven sheets.

## 10. What the first live pass must answer

1. Does tolerant matching actually pick up `UIAfterAction_LW`, `UIRecruitSoldiers_LW`,
   `robojumper_UISquadSelect` and `CPS_UIArmory_PromotionHero` on the stack, or does something
   about how they are pushed defeat it?
2. With CPS installed, does the standard promotion genuinely route to
   `CPS_UIArmory_PromotionHero`, and does our hero reader read it as-is?
3. `UIShellDifficultySW` — does it leave the parent's `m_SecondWaveList` and mecha items populated
   the way `surfaces/shell.md`'s rules assume, or is `SecondWaveOptionsReal` the only live array?
4. How does the paginated ability bar present itself to a widget read, and what happens to the
   `F1`-`F8` target hotkeys across pages?
5. Which of LWOTC's Flash-pushing rows does `flash_shadow.cpp` actually catch, keyed by cache index?
6. Does the resource strip change? LWOTC adds resources and the strip is read by Flash capture.
7. Are there LWOTC screens the census above missed because they are spawned rather than pushed —
   run `just dev-gui` on each new screen rather than trusting this file's class list.
8. Does anything about running LWOTC alongside the companion break the companion itself? We install
   no `ModClassOverrides` of our own, so no direct conflict is expected — confirm it.
