# The research report — `UIResearchReport` recon

Survey date 2026-08-24, read off `Development/SrcOrig` (retail WOTC) plus the generated SDK's
live-dump offsets and the localization file the strings actually come from. Nothing here has been
measured in a running game yet — §7 is the list of things the first live pass has to answer.

`docs/ui-inventory.md:213` lists this as **P1** and it is the last uncovered screen in the
research-complete chain: `UIBreakthroughResearchComplete` (`XComHQPresentationLayer.uc:2723`),
`UIProvingGroundProjectComplete` (`:2827`) and `UIResearchComplete` (`:1859`) are all
`BuildUIAlert` → `QueueDynamicPopup`, i.e. `UIAlert` variants the popup recipe already reads.
`UIResearchArchives` and `UIShadowChamberArchives` both `extend UIChooseResearch` and ride the
commodity recipe. This screen is the hole in the middle of a covered chain.

## 1. What the screen is

`class UIResearchReport extends UIScreen` (`:12`), package
`/ package/gfxResearchReport/ResearchReport` (`:250`), `InputState = eInputState_Evaluate`.

It is the full-page dossier shown when a tech finishes: a letterhead, the project's name and
codename, the completion date, what it unlocked, and Tygan's in-world write-up.

**It spawns no widgets.** `InitScreen` (`:25-41`) calls `super`, `UpdateNavHelp`, and
`DisplayUI3D`; that is all. `var UILargeButton ContinueButton` is declared at `:23` (offset 0x454)
and **never assigned anywhere in the class** — the same shape as `UITacticalHUD`'s controller-only
buttons, which read null live. So the widget tree under this screen is empty and `/gui` will print
one line.

### Entry points

Four call sites, two presentation functions, one class.

| Path | Trigger | Movie |
|---|---|---|
| Labs | `UIFacility_Powercore.uc:56` (`InitScreen` → `NeedResearchReportPopup`, any tech with `!bSeenResearchCompleteScreen`) and `:506` (`TriggerResearchReport`, called by objective alerts) | 3D |
| Geoscape | `XComGameState_HeadquartersProjectResearch.uc:175` — an **instant** tech completing, with no Labs visit at all | 3D |
| Research archives | `UIResearchArchives.uc:22` (`OnPurchaseClicked`, the "View Report" row verb) | 3D |
| Shadow Chamber | `UIFacility_ShadowChamber.uc:56`, `:210` and `UIShadowChamberArchives.uc:18`, all via `ShadowChamberResearchReportPopup` (`XComHQPresentationLayer.uc:1961`) | **2D** — pushed with no movie argument |

`UIFacility_ProvingGround` has its own `NeedResearchReportPopup` (`:368`) but only calls
`DisplayTechCompletePopups` with it (`:55-66`); it never pushes this screen.

⚠ **`ResearchReportPopup` guards on `IsNotInStack` (`:1923`) while both Labs call sites push it in
a LOOP over every unseen tech.** Iteration 2 finds iteration 1's screen still in the stack and is
skipped. So two techs finishing together appear to show **one** report. That is vanilla behaviour,
not something to fix, but it decides whether the recipe ever needs a "report 2 of 3" concept —
§7.3.

### ⛔ Exactly one key closes it, and which one depends on the stack

`OnUnrealCommand` (`:199-235`):

```
B / ESCAPE / RMB      → CloseScreen() only if  ScreenStack.IsInStack(class'UIResearchArchives')
A / ENTER / SPACEBAR  → CloseScreen() only if !ScreenStack.IsInStack(class'UIResearchArchives')
```

Both branches set `bHandled = true` regardless, and the function returns `bHandled || super`, so
**the wrong key is swallowed and does nothing**. `UpdateNavHelp` (`:43-56`) tests the same
condition and offers `AddBackButton` or `AddContinueButton` to match. A recipe that sent a fixed
key would produce a modal a blind player cannot dismiss — the exact failure `Vocab::PopupContinue`
exists to prevent.

⛔ **The discriminator is not "did I come from an archive".** `UIScreenStack.GetScreen` is EXACT
CLASS EQUALITY (`ScreenClass == Screens[Index].Class`, `UIScreenStack.uc:613`) — not `IsA`, which
is what `GetFirstInstanceOf` uses two functions below it. And `UIShadowChamberArchives extends
UIChooseResearch`, **not** `UIResearchArchives`. So a report opened from the Shadow Chamber
archives closes with **Enter**, and its nav help says "Continue", even though the player got there
by picking a row in an archive. Reproduce the game's test literally.

✅ For us that test is one call: `FindStackScreenRaw` matches by exact class-name string, which is
the same semantics. (It additionally filters on `StackEntryLiveRaw`, which `GetScreen` does not —
irrelevant here, an archives screen underneath is inited.)

## 2. What is on it — eight strings, one push

`InitResearchReport(TechRef)` (`:79-181`) resolves everything into LOCALS and hands it to
`AS_UpdateResearchReport` (`:183-195`), which is one `MC.BeginFunctionOp("UpdateResearchReport")`
followed by eight `MC.QueueString` and an `EndOp`.

| # | Arg | Source | Example |
|---|---|---|---|
| 0 | `header` | `m_strResearchReport` | "RESEARCH REPORT" |
| 1 | `project` | `TechState.GetDisplayName()`, through `CapsCheckForGermanScharfesS` | "Magnetic Weapons" |
| 2 | `code` | `m_strCodename @ TechState.GetCodeName()` — **already joined by the game** | "CODENAME: Orsted" |
| 3 | `date` | `GetDateString(TechState.CompletionTime)` — localized, euro or US field order per language (`X2StrategyGameRulesetDataStructures.uc:1608`) | — |
| 4 | `image` | `TechState.GetImage()` — an asset path | *(no words)* |
| 5 | `unlocks` | `\n`-joined, see below | "New Item Available: Magnetic Rifle" |
| 6 | `description` | `TechState.GetLongDescription()` | Tygan's write-up |
| 7 | `greeble` | `m_strTopSecret` | "TOP SECRET" |

The three localized strings are the screen's own (`XComGame.int:12928-12931`) and ride in the
payload, so even the letterhead needs no property read.

### Arg 5, the unlocks

`X2TechTemplate.GetUnlocks` (`X2TechTemplate.uc:64`) fills five arrays, and `:106-148` renders
them in this order — items, facilities, facility upgrades, techs, proving-ground projects —
through `UIAlert`'s static helpers (`UIAlert.uc:1686-1774`). Each is already a whole localized
sentence:

```
m_strItemUnlock     = New Item Available: <name>
m_strFacilityUnlock = New Facility Available: <name>
m_strUpgradeUnlock  = New Facility Upgrade Available: <name>
m_strResearchUnlock = New Research Available: <name>
m_strProjectUnlock  = New Proving Ground Project Available: <name>
```

Then, if the template has one, the tech's own `UnlockedDescription` is appended (`:150-170`) with
`XGParam.StrValue0` set to either the intel reward count (`IntelReward > 0`) or the region's
display name (`RegionRef.ObjectID != 0`) — "Recovered from Datapad: 40 Intel", "Alien Facility
Mission in North America Unlocked". Both are expanded before the push; nothing reaches us with
markup in it.

### Arg 6, and the number that shapes the whole design

`LongDescription` is a `localized string` on `X2TechTemplate` (`:51`), commented *"Research
complete screen in Tygan's voice"*. Measured over `XComGame.int`:

| | |
|---|---|
| count | **52** |
| shortest | 493 chars |
| average | **1038 chars** |
| longest | **2438 chars** (the Lost autopsy, four paragraphs) |

Paragraphs are separated by `\n\n` inside the string. **The payload is a document, not a control
set**, and that is the fact the readout has to be designed around.

✅ 2438 fits: `flash_shadow.cpp`'s `kMaxPlausibleStringChars` is 65536, and the capture sizes each
string from its own length — this is not a rerun of the XCOM-archives truncation.

## 3. ⛔ The screen retains nothing — not even the tech it is about

All eight strings are locals in `InitResearchReport`. So are `TechState` and every unlock array.
`TechRef` is a PARAMETER and is stored nowhere. Three consequences, in the order CLAUDE.md's
read-the-screen rule prefers:

1. **The screen's own fields and functions** — there are none. The class holds three localized
   strings, two `FName` camera tags, a bool and a null button pointer (§5).
2. **State objects the screen cached** — there are none, for the same reason.
3. **A fresh history query** — possible only after first *guessing the tech*: scan
   `XComHQ.TechsResearched` for `!bSeenResearchCompleteScreen`, or read the archives screen's
   `m_arrRefs[List.SelectedIndex]` when there is an archives screen underneath. Then re-run
   `X2TechTemplate.GetUnlocks`, which walks every buildable item template, every facility
   template, every upgrade template, all of `XComHQ.Inventory` and all of `XComHQ.Facilities`
   (`X2TechTemplate.uc:207-309`). Expensive, and it is a second question that can answer
   differently from what is on screen — which is precisely the failure the read-the-screen rule
   names.

✅ **The Flash command-queue capture answers all of it, and this is the cleanest case for it yet.**

```cpp
FlashShadow::Call call;
if (FlashShadow::GetCall(FlashShadow::PanelCacheIndex(screen), "UpdateResearchReport", call))
    const std::vector<std::string> args = call.StringArgs();   // eight, in push order
```

Why it is cleaner than `UIAlert`, the capture's first consumer:

- **Matched by NAME**, not by "the most recent batch carrying a string". `UIAlert` cannot do that
  (`UpdateData`, `UpdateGuerrillaOpsSplash`, `UpdateRetaliationSplash`, … — 106 build functions),
  so `GetTextCall` guesses and then needs a per-symbol field table, an asset-path filter and a
  button-label matcher. Here the function name is unique to this screen and the argument layout is
  fixed by the `.uc`.
- **Arg 4 is skipped by POSITION**, not by looking like an asset. `StringArgs()` keeps non-string
  operands as empty entries precisely so a positional consumer does not shift — the wire-format
  rule, restated for the capture.
- **Zero transcribed offsets.** Every word including the localized letterhead is in the payload.
  Nothing new to `static_assert` beyond `UIPanel.MC` (0x2D0) and `UIMCController.CacheIndex`
  (0x68), both already pinned by `PanelCacheIndex`.

⚠ **`gPanels` is keyed by cache index ALONE, not by `(movie, index)`** (`flash_shadow.cpp:66`).
Two panels on the 2D and 3D movies can therefore share a key. Matching by function name makes that
harmless *here* — no other panel pushes `UpdateResearchReport` — but the gap is real and this
screen is the one that exposes it, because the Shadow Chamber path puts the same class on the 2D
movie while every other path puts it on the 3D one. §7.2.

## 4. What a sighted player is looking at, and the one thing that is not settled

`OnUnrealCommand` handles no scroll keys, and the screen spawns no `UIScrollbar`. So if the Flash
field clips a 2438-character description, a sighted player sees **less** than we would read, and
the full text becomes a labelled aid rather than parity. Nothing in `SrcOrig` states it — Flash
owns the decision, the same way it owns which hack reward slot is highlighted
(`hacking-screen-recon.md` §309). §7.1.

## 5. Offsets

`AUIResearchReport`, `0x004F` of own data over `AUIScreen` (generated SDK,
`XComGame_classes.hpp:79327`):

| Offset | Type | Name |
|---|---|---|
| 0x0410 | FString | `m_strCodename` *(localized; also arg 2's prefix)* |
| 0x0420 | FString | `m_strTopSecret` *(localized; also arg 7)* |
| 0x0430 | FString | `m_strResearchReport` *(localized; also arg 0)* |
| 0x0440 | FName | `DisplayTag` |
| 0x0448 | FName | `CameraTag` |
| 0x0450:1 | bool | `bInstantInterp` |
| 0x0454 | `UILargeButton*` | `ContinueButton` — **never assigned; null live** |

**We need none of them.** Every one that carries a word is in the Flash payload already, and the
recipe is better off reading the payload than the property, because the payload is what the screen
is SHOWING. Listed here so the next session does not re-derive them.

## 6. Proposed shape — two stops, one vertical column

A graph recipe, not narrate-only: there is nothing to destroy by taking the arrows (the screen has
no navigable widget, no focus model and no camera behaviour keyed to selection), and without a
recipe the report is unreadable.

**Stop 1 — `report`.** One node per line, no `StartRow` anywhere, exactly the popup recipe's rule:
a control added outside a row becomes its own single-item row, so the document reads DOWN with
Up/Down. It also means `GraphBuilder` stamps no "N of M", which it only applies within a row of
more than one item — right, because "TOP SECRET, 2 of 11" reads as a menu position on a
classification banner.

| # | Content | Arg |
|---|---|---|
| 0 | greeble | 7 |
| 1 | project name ← **start** | 1 |
| 2 | codename | 2 |
| 3 | completion date | 3 |
| 4… | unlock lines, split on `\n` | 5 |
| … | description paragraphs, split on `\n` | 6 |

- **The greeble is read.** It is rendered text a sighted player sees, so skipping it invents a
  parity gap. It sits at the top as the letterhead's second half rather than last where the push
  order puts it; push order is not layout order (arg 4 is an image path between the date and the
  unlocks).
- ⛔ **The header is NOT a node — it is the screen name.** `Attach()` speaks
  `Recipe::ScreenName()` interrupting and then the landing node's full readout
  (`menu_navigator.cpp:351-356`), so a header that was both would be said twice in a row: the same
  wording defect CLAUDE.md already lists against the recentering readout. As the screen name it is
  spoken exactly once, on arrival, which is what a document title is for. (A `PushContext` would
  have been worse still — it re-announces on every entry into the stop.)
- **Empties are dropped at node-build time, after the positional read.** A tech with no codename,
  no unlocks or no `UnlockedDescription` is ordinary.

**Stop 2 — `continue`.** One node. Label: `Vocab` (the game's own continue/back words are not
reachable — `AddContinueButton` spawns a Flash `X2ContinueButton` whose label lives in the movie,
and the PC back button is an ICON, `UINavigationHelp.uc:99-107`). `OnActivate` sends
`kCmdEnter` or `kCmdEscape` per the exact-class test in §1.

**Start on the project name.** It is the fact the player opened the screen for, the greeble stays
one Up away, and everything below is the document in page order — with the unlocks, the half a
player acts on, BEFORE the prose, so nothing forces them through Tygan to reach it. That last
property is what lets the whole report be one stop; it is also the only reason the dropship
briefing had to reverse its screen order, and here the page already reads the right way round.

**Screen name on attach**: arg 0, the game's own localized "RESEARCH REPORT", falling back to
`Vocab::ScreenResearchReport`. So arrival reads "RESEARCH REPORT" then "Magnetic Weapons".

### Accepted tradeoff

One paragraph is one node, so a node can be ~600 characters, and a description with no internal
`\n\n` would be a single node of up to 2438. Pressing Down interrupts it (`Focus` clears the
queue), so the escape hatch exists — but it is a deal made deliberately, not a thing to discover
live on the Lost autopsy.

### What we deliberately do NOT do

- **No sentence axis.** A `StartRow` per paragraph with sentences to the Right is buildable today
  with no kernel work, and it was considered and dropped: prose stacked vertically on screen
  should not be walked sideways, and it would reintroduce the position stamps this shape avoids.
- **No `GraphSheet`.** Nothing on this screen draws a column heading.
- **No companion class.** This would be the first recipe whose words cost no UnrealScript at all.

### Kernel impact: none

Everything the shape needs exists — `BeginStop`, `AddLabel`, `AddItem`, `SendScreenCommand`. No
new graph primitive, no `NodeVtable` field, no announcer change. The one small new thing is a
`\n` line splitter; `transient_reader.cpp:50`'s `SplitLines` is NOT it — that one splits the
companion's `~|~` wire separator and lives in an anonymous namespace.

## 7. What the first live pass must answer

1. **Does the Flash field clip the description?** `/screenshot` on a long report (the Lost autopsy
   is the 2438-char worst case; any Tier-3 autopsy is long) beside `just dev-flashqueue` for the
   same panel. Decides parity vs. labelled aid — §4.
2. **Does the capture answer for the Shadow Chamber path?** That is the only one on the 2D movie,
   and the only place a cache-index collision could bite — §3. `just dev-flashqueue` with no
   argument lists every tracked index; the report's should appear on both paths.
3. **Two techs at once.** Finish two in one Labs visit and check whether one report shows or two
   queue up — §1. If two, the recipe needs nothing extra (each is a fresh attach); if one, note it
   so nobody later reads the missing second report as our bug.
4. **The three close paths.** Enter from the Labs, Enter from the Shadow Chamber archives, Escape
   from the research archives — and confirm the *other* key does nothing in each, so the node's
   label matches what the key actually does.
5. **The instant-tech geoscape path.** The report over the strategy map, not inside a facility:
   confirm the recipe probes above the geoscape recipe and that detaching returns the arrows to
   the tray.
6. **Shape-count the speech ring afterwards**, per the distribution rule:
   `grep -oP 'TTS!?: "\K[^"]*' X2Access.log | sed 's/[0-9]\+/N/g' | sort | uniq -c | sort -rn`.
   The thing to look for here is a double full stop — the unlock sentences and `UnlockedDescription`
   are whole sentences ending in their own punctuation, and every one of them is its own node, so
   nothing should be joining them. If a `. .` appears, something is composing where it should be
   listing.
7. **A tech with no unlocks and no codename.** The empty-drop path — a repeatable tech
   (`bRepeatable`) or a datapad decryption is the case.

## 8. What was built (2026-08-24) — not yet run live

Builds clean, kernel tests pass, unexercised in a running game. Everything in §7 still stands.

| File | What |
|---|---|
| `game_ui.h` / `game_ui.cpp` | `ResearchReportView`, `ReadResearchReport`, `FindResearchReportScreen` — the capture's second consumer, and much smaller than the first: no symbol table, no field map, no asset heuristic, no button matcher, no cache |
| `recipes/recipe_avenger.cpp` | `kResearchReportRecipe` — probe, build, screen name |
| `recipes/recipes.h` | declared; `kRecipeCount` 19 → 20 |
| `menu_navigator.cpp` | listed in `kRecipes` third, after the dialog and popup recipes and above everything it can be raised over |
| `vocab.h` | `ScreenResearchReport`, `ReportContinue`, `ReportBack`, `ReportNoText` |

**No kernel change, no new graph primitive, no companion class.** The first recipe with no
UnrealScript behind it at all.

Two implementation notes worth keeping:

- ⛔ **Split before cleaning.** `CleanText` (`game_ui.cpp:5276`) collapses *every* run of
  whitespace, newlines included. Running it over arg 6 before splitting would fuse a
  four-paragraph report into one 2438-character node — and it would have looked fine in a log.
  `ReportArgLines` splits on `\n` first and cleans each line after. Same family as the
  join-after-cleaning rule, in the other direction.
- `fromArchives` is captured into the close button's `OnActivate` while `gScreen` is read live
  there. That asymmetry is deliberate and stated in place: the screen pointer can go stale between
  render and the input drain, but `fromArchives` is a fact about a screen BELOW this one, which
  cannot leave the stack while this one sits on top of it.
