# The Avenger — recon

Status: **recon only, nothing built.** Script recon against
`SDK/Development/SrcOrig/XComGame/Classes/`, live recon 2026-08-20 against a
running campaign through the dev harness (`/status`, `/gui`, `/props`,
`/input`, `/screenshot`).

⚠ The live session used for this pass is **very early**: the campaign is at the
`T0_M1_WelcomeToLabs` step (right after Gatecrasher). Three consequences the
reader must keep in mind — everything marked *[not seen live]* below is hidden
by tutorial gates in that save, not missing from the game:

- the resource strip is hidden entirely (`ShowResources` requires
  `T0_M4_ReturnToAvenger` completed),
- the event queue and the objectives list had nothing to show,
- `Escape` out of the Commander's Quarters is a **deliberate no-op**
  (`UIFacility_CIC.OnCancel` returns without calling super until
  `T0_M1_WelcomeToLabs` is complete) — measured live, see §6.

A second recon pass on a mid-campaign save is needed before building; §9 lists
exactly what it has to answer.

---

## 1. What it is

The Avenger is the strategy layer's home. It is **not one screen** — it is a
persistent HUD with two nested views under it, and the player moves between
them constantly:

| Layer | Class | What it is |
|---|---|---|
| persistent HUD | `UIAvengerHUD` | never leaves; resources, shortcuts, to-do, events, objectives, nav help |
| zoomed-out | `UIFacilityGrid` | the "ant farm" cutaway — 19 room slots seen side-on |
| zoomed-in | `UIFacility` + 22 subclasses | one room's interior, with its staff slots and actions |

Live screen stack, top first (2026-08-20, standing in the Commander's Quarters):

```
0 UIFacility_CIC    0x279 inited focused visible
1 UIFacilityGrid    0x019 inited
2 UIAvengerHUD      0x239 inited visible
3 UIEventNotices    0x219 inited
```

`UIFacility_CIC` declares `InputState = eInputState_Evaluate`, so unhandled
input falls **down** the stack to the grid and then to the HUD. That is why the
HUD's `1`–`6` shortcuts still work while the player is inside a room, and it is
the mechanism any layer of ours has to coexist with.

`UIFacilityGrid` stays on the stack, inited but **not visible**, while a room is
open. It is not re-created per visit.

---

## 2. The persistent HUD — `UIAvengerHUD`

46 panels live. Ten children, and the readability story differs for each. The
short version: **the two most information-dense widgets on this screen — the
shortcut bar and the to-do strip — retain everything in script properties**,
which is the opposite of what tactical taught us to expect.

### 2.1 Screen header — `UIX2ScreenHeader` ✅ retained

`LocationField` / `RoomField`, live `"МСТИТЕЛЬ"` / `"Каюта шефа"`. Set through
`SetText`, which stores before piping to Flash. Shown top-left. Inside a room it
carries the facility's `DisplayName`; on `UIFacilitySummary` it carries that
screen's title instead.

### 2.2 Resource strip — `UIX2ResourceHeader` ⛔ write-only *[not seen live]*

`AddResource(label, data)` is `MC.BeginFunctionOp("addResource")` and retains
**nothing** — the tactical-HUD pattern exactly. But the source is trivially
re-derivable in script: every value comes from
`UIUtilities_Strategy.GetResource` or `XComGameState_HeadquartersXCom`.

What makes this one interesting is that **the strip's contents are
context-dependent** — `UpdateResources()` switches on
`ScreenStack.GetCurrentScreen().Class`. A sighted player does not see a fixed
resource bar; they see the subset relevant to where they are:

| Screen | Shown |
|---|---|
| `UIFacilityGrid` (the default) | monthly income, supplies, intel, elerium, alloys, scientists, engineers, contacts, power — plus unstaffed-engineer icons |
| `UIStrategyMap` | same minus power, minus staff icons |
| `UIChooseResearch` | intel, elerium, alloys, sci, eng |
| `UIChooseProject` | income, supplies, cores, elerium, alloys, eng |
| `UIBuildFacilities` / `UIChooseFacility` | income, supplies, **power** |
| `UIFacilityUpgrade` / `UIChooseUpgrade` | income, supplies, elerium, alloys, power |
| `UIInventory_BuildItems` | income, supplies, elerium, alloys, eng |
| `UIFacility_Labs` | scientists only |
| `UIFacility_PowerGenerator` | power only |
| `UIFacility_ResistanceComms` | contacts only |
| `UICovertActions` | supplies, intel, alloys, elerium |
| everything else | **hidden** |

Colour carries meaning and is lost to us as colour: supplies/income go
`eUIState_Bad` when negative, contacts go Warning at ≤2 free and Bad at 0 free,
power goes Green/Yellow/Red off `XComHQ.PowerState`. Those are the states a
readout has to say in words.

Two exact forms worth copying rather than reinventing: monthly income prints as
a signed parenthesised value (`(+§40)`), and contacts and power both print as
`current/total`.

### 2.3 Room shortcut bar — `UIAvengerShortcuts` ✅ fully retained

The row of buttons along the bottom. This is the game's own primary navigation
and **all of it is readable**. Live:

```
Categories[0] ИССЛЕДОВАТЕЛЬСКИЙ ОТСЕК  bAlert=true   HotLinkRef=404
    "Показать ученых"     — Показать список ученых
    "Новое исследование"  — Выбрать новое научное исследование
Categories[1] ИНЖЕНЕРНЫЙ ОТСЕК         bAlert=false  HotLinkRef=408   (0 messages)
Categories[2] ОРУЖЕЙНАЯ                bAlert=true   HotLinkRef=405
    "Мемориал" / "Вербовать" / "Бойцы"
Categories[3] ШТАБ                     bAlert=false  HotLinkRef=402
    "Архивы проекта XCOM"
Categories[4] ЗАЛ ТЕНЕЙ                bAlert=false  HotLinkRef=0     (hidden — not built)
CurrentCategory = 3
```

Each `UIAvengerShortcutMessage` carries `Label`, `Description` (the tooltip
text), `Urgency` (Low/Medium/High → plain / Warning / Bad colouring on the list
item), `bDisabled` and `HotLinkRef`. Each category carries `bAlert`, which is
`FacilityState.NeedsAttention()` — that is the "!" badge a sighted player sees
on the button.

`CurrentCategory` is which room the player is physically in (−1 = none, e.g. on
the geoscape). `InRoomCurrentShortcutTabHighlightIndex` is which tab is
highlighted; the two differ only on controller.

The Shadow Chamber button is hidden until the facility exists
(`ShouldShowCurrentCategory`), which is why it read `inited` but not `visible`.

### 2.4 To-do strip — `UIToDoWidget` ✅ fully retained

The small icon row. This is the closest thing the game has to a
"what should I do next" list, and it is **the single highest-value surface on
this screen for a blind player** — because it is already the answer to the
question, pre-computed by the game.

Ten category labels ship, nine are wired:

```
0 Исследовательский отсек   5 Персонал
1 Инженерный отсек          6 Зона испытаний
2 Энергия                   7 Состояние солдата
3 Сопротивление             8 Взаимосвязь солдат
4 Доход                     (9 Секретные задания — label only, no Categories entry)
```

Live, exactly two categories were non-empty:

```
[0] Research  { Label="НЕТ АКТИВНОГО ИССЛЕДОВАНИЯ",
                Description="В настоящее время исследование не проводится…",
                Urgency=2 (High), OnItemClicked=ChooseResearchHotlink }
[7] SoldierStatus { Label="ПОВЫШЕНИЕ: Нов. Jane Kelly",
                Description="Нов. Jane Kelly может получить повышение…",
                Urgency=0 (Low),  OnItemClicked=PromoteSoldierHotlink }
```

A sighted player sees this as two coloured glyphs (red = High) and must hover
each to read the list. Every message carries a working hotlink delegate, so it
is also an **action** surface, not just a report.

⚠ One gate that will bite: `UpdateCategories()` runs only through
`ShouldUpdateCategories`, which fires **when `UIFacilityGrid` is the current
screen and visible**. Inside a room the data is whatever it was when the player
last stood on the grid. Anything of ours that speaks the to-do list either has
to accept that staleness (which matches what the sighted player's icons show —
they are stale too) or call `UpdateCategories()` itself.

### 2.5 Event queue — `UIEventQueue` ⛔ not retained on the widget *[not seen live]*

The timeline of pending projects, bottom-right. Fed
`array<HQEvent>` by `UIAvengerHUD.ShowEventQueue()` →
`XGMissionControlUI.UpdateEvents()` → `XComHQ.GetEvents()`. The widget keeps
only `bIsExpanded`; the data is not stored. But `GetEvents` is a plain script
call any companion function can repeat, and `HQEvent` is a good struct:

```
Data, Hours, Title, DayLabel, DayValue, ImagePath,
FacilityHotLinkRef, ActionRef, bActionEvent
```

sorted soonest-first. Eleven contributors: orders, soldiers, research, proving
ground, items, facilities, room clearing, facility upgrades, psi training,
resistance, covert actions.

Two visibility rules matter for parity: **the queue is hidden entirely inside a
room** (`UIFacility.InitScreen` calls `HideEventQueue()`), and when collapsed it
shows only **one** event (two if a covert action is pending) — the expand button
is the only way a sighted player sees the rest, and it is mouse-only.

### 2.6 Objectives — `UIObjectiveList` *[not seen live]*

Same class the tactical HUD uses; per CLAUDE.md its `UIObjectiveListItem.Data`
(`ObjectiveDisplayInfo.DisplayLabel`) is readable. Sourced from
`XComGameState_ObjectivesList`. The full screen behind it is `UIViewObjectives`,
reachable from the Commander's Quarters shortcut submenu, which lists main and
sub objectives with in-progress/completed state.

### 2.7 Nav help — `UINavigationHelp`

The hint strip. Live labels: `m_strBackButtonLabel="НАЗАД"`,
`m_strGeoscapeLabel="МОСТИК"`. On mouse+keyboard the geoscape entry renders as
an **icon button only** (`AddGeoscapeButton` takes the `Movie.IsMouseActive()`
branch and pushes `eButtonIconPC_Hologlobe`); the word "МОСТИК" is the
controller-branch label. Its tooltip resolves the real binding through
`FindAbilityKey(..., eABC_Geoscape, ..., eKC_Avenger)` — i.e. `%KEY:ONE%`.

### 2.8 Unstaffed-staff icons — `UIAvengerHUD.StaffIcons`

A list of `UIStaffIcon`s under the resource strip, one per **unstaffed
engineer** (`XComHQ.GetUnstaffedEngineers()`). The scientist loop is present but
commented out in shipped source, so scientists never appear here. Clicking one
opens the engineer personnel list.

### 2.9 Crew status indicators — `UIAvengerHUD_SoldierStatusIndicatorContainer`

Floating icons anchored to crew **pawns walking around the ship**: a bond icon
when two soldiers may bond, a trait icon when a soldier has an
`AlertTraits` entry. Anchored in 3D, so they move and can be off-camera —
structurally the same problem as tactical world messages. Source is
`XComHQ.Crew` + `Unit.HasSoldierBondAvailable`, fully script-readable; the
widget itself holds only pawn pointers.

### 2.10 `UIAvengerLinks` — dead code

Spawned, `LabelBridgeButton="МОСТИК"`, and **the entire body is commented out**
("Commenting this out per Jake on 4/16/2015"). It draws nothing and does
nothing. Ignore it.

---

## 3. The ant farm — `UIFacilityGrid`

134 panels: 19 `UIFacilityGrid_FacilityOverlay`s, each with an Upgrade /
Remove / Cancel button and a staff-icon list.

### 3.1 The layout is fixed and hardcoded

`InitializeNavigationGrid()` spells out the whole adjacency graph. Seven fixed
rooms around a 3×4 buildable core:

```
        [0] Commander's Quarters   [15] CIC        [2] Armory
        [16] Living Quarters
  [1] Power Core     ┌ 3  4  5 ┐                   [18] Memorial
                     │ 6  7  8 │
                     │ 9 10 11 │                   [17] Engineering
                     └12 13 14 ┘
```

(indices are into `FacilityOverlays`, sorted by `RoomMapIndex`). The graph
includes diagonals — `AddNavTargetDiagUpRight` and friends — so the game itself
already models this as a spatial map with eight directions, not a list.

The game state carries the same thing independently:
`XComGameState_HeadquartersRoom` has `MapIndex`, `GridRow` and
`AdjacentRooms`. **Two independent sources for the same adjacency** — worth
knowing before we invent a third.

### 3.2 What each room tile shows a sighted player

`UpdateData()` dispatches on the room's condition, and each branch fills a
`TUIRoomData`:

| Condition | Title | Status line | Other |
|---|---|---|---|
| tutorial-locked | — | — | greyed, locked |
| under construction | facility name | **time remaining**, or "PAUSED" in red | orange border, construction flag |
| core facility | facility name *(hover only)* | queue message *(hover only)* | staff icons |
| built facility | facility name *(hover only)* | queue message *(hover only)* | staff icons; **orange border + highlight when `NeedsAttention()`** |
| clearing | — | time remaining | |
| locked (needs excavation) | — | "РАСЧИСТИТЕ СОСЕДНИЙ ОТСЕК, ЧТОБЫ РАЗБЛОКИРОВАТЬ" | |
| special feature | feature name (alien machinery, reinforced bulkhead, exposed/shielded power cell, alien debris) | | |
| empty | "ПУСТО" *(hover only)*, "ПОСТРОИТЬ ОТСЕК" in build mode | | green border |

⚠ **The room's name is drawn only while the mouse is over it.** `kData.strTitle`
is set inside `if( bIsFocused )` in every non-construction branch. What is
*always* drawn is the border colour, the staff icons, and — for a room under
construction or being cleared — the countdown.

So the at-a-glance information a sighted player gets from the ant farm is:
**which rooms need attention (orange), which have empty staff slots (icons),
what is building and how long is left.** Names require hovering, one room at a
time. That is a lower bar than it first appears, and it is the bar parity is
measured against.

### 3.3 Readability

`Label`, `Status`, `Queue`, `bUnderConstruction`, `m_bClearing`, `bLocked` are
**retained script vars** on the overlay, written by `SetLabel`/`SetStatus` under
a dirty check before the Flash op — so the retained value is exactly what Flash
was last told. Live, `Label` and `Status` both read `""`, which is correct and
informative: this session had never hovered that room while the grid was up.

That is the trap. The widget holds **the words the game actually showed**, which
is normally what our rules want — but here "showed" means "showed on hover", so
for 18 of 19 rooms it holds nothing. The words themselves are one script hop
away (`Room.GetFacility().GetMyTemplate().DisplayName`,
`Facility.GetQueueMessage()`, `Facility.GetStatusMessage()`,
`FacilityProject.GetCurrentNumHoursRemaining()`), so the companion supplies
them and the widget supplies structure and selection — the same division
tactical settled on.

### 3.4 Staff icons per room

`GetStaffIconData(Facility)` walks `Facility.StaffSlots` and emits an
`EUIStaffIconType` per slot — filled slots get the occupant's type
(soldier / engineer / scientist / …), empty slots get an "empty" variant. This
is the always-visible signal, and it is the one that answers "where can I put
this engineer".

---

## 4. Inside a room — `UIFacility` and its 22 subclasses

`RealizeFacility()` sets the header and builds the staff container; subclasses
add their own controls.

- **Staff slots** — `UIFacilityStaffContainer` → `UIStaffSlot` per slot. The
  widget's `Update()` is a pure Flash pipe, but every string comes from
  `XComGameState_StaffSlot`: `GetNameDisplayString()`,
  `GetBonusDisplayString()`, `GetUnitTypeImage()`, `IsUnitAvailableForThisSlot()`,
  `IsLocked()`. The slot's `StaffSlotRef` is on the widget, so this reads
  *the screen's own* state object — no re-lookup.
- **Room function buttons** — `UIRoomContainer`, one per facility that has extra
  verbs. Only six facilities have any: Academy ("View Unlocks"), Bar/Memorial
  ("Obituaries"), Covert Actions ("View Actions"), Living Quarters
  ("Personnel"), Proving Ground ("Start Project"), Training Center
  ("Train Abilities").
- **Upgrade button** — bottom-right, shown when `CanUpgrade()` or
  `HasBeenUpgraded()`.
- **Research progress bar** (`UIFacility_ResearchProgress`), **covert ops
  status** (`UIFacility_CovertOpsStatus`) on the facilities that have them.

The Commander's Quarters (`UIFacility_CIC`) has **no staff slots and no room
buttons** — live it was 2 panels total. Its content is reached entirely through
the shortcut submenu (Objectives, Avenger Report, XCOM Archives, Daily
Challenge, Resistance Orders).

**`UIFacilitySummary`** — the "Avenger Report" (`m_strFacilitySummary`,
`Состояние "Мстителя"`) — is the one screen that presents everything at once: a
sortable table over all facilities with columns **name (+ upkeep cost) / staff
slot 1 / staff slot 2 / power / construction date / status / queue**, sortable
by name, staff, power, construction date or status. For a blind player this is
almost certainly a better entry point into the ship than the ant farm is, and it
already exists.

---

## 5. The vanilla keyboard map

Measured from source; the `eKC_Avenger` binding context makes 1–6 rebindable
through the Controls screen.

| Key | Action | Where handled |
|---|---|---|
| `1` | jump to the geoscape (bridge) | `UIAvengerShortcuts.OnUnrealCommand` → `NavHelp.HotlinkToGeoscape()` |
| `2` | Research | `SelectCategory(0)` |
| `3` | Engineering | `SelectCategory(1)` |
| `4` | Barracks / Armory | `SelectCategory(2)` |
| `5` | Commander's Quarters | `SelectCategory(3)` |
| `6` | Shadow Chamber | `SelectCategory(4)` |
| `Escape` | leave the room to the grid | `UIFacility.OnUnrealCommand` → `OnCancel` |
| `F2`–`F4` | activate staff slot 0–2 | `UIFacilityStaffContainer.OnUnrealCommand` |
| `Enter` / `Space` | enter the focused room | `UIFacilityGrid_FacilityOverlay.OnUnrealCommand` |
| right mouse | pause menu (on the grid) / back (in a room) | |

On mouse+keyboard `SelectCategory` fires **immediately** — the two-step
"highlight, then confirm" is the `ISCONTROLLERACTIVE` branch only.

---

## 6. Where the keyboard dead-ends

Six gaps, and they are the shape of the problem. The first two turn out to be
**one** gap with **one** fix — see §6.1.

1. **The ant farm has no keyboard spatial navigation.**
   `UIFacilityGrid.InitScreen` calls `InitializeNavigationGrid()` and sets
   `Navigator.OnlyUsesNavTargets = true` **only** `if( ISCONTROLLERACTIVE )`.
   The whole eight-direction adjacency graph in §3.1 is dead code on
   mouse+keyboard.
2. **Room names are mouse-only.** `strTitle` is set under `bIsFocused`, and on
   mouse+keyboard focus arrives only from `FXS_L_MOUSE_IN`.
3. **A facility screen with no staff slots and no room buttons has an empty
   navigator**, so arrows inside it do nothing at all. `UIFacility.InitScreen`
   spawns the staff container, then `RealizeStaffSlots()` calls
   `Navigator.RemoveControl(m_kStaffSlotContainer)` when it has no children.
   The Commander's Quarters is exactly this case — live it was **2 panels** —
   and it is the room the early tutorial parks the player in. Confirmed by the
   user pressing arrows there: nothing happens, correctly.
4. **The to-do submenus are mouse-only.** `UIToDoWidget.ShowList` is driven from
   `OnCategoryMouseEvent`. The full-screen `UINotificationMenu` is opened from
   `UIAvengerHUD.OnUnrealCommand` on **`FXS_BUTTON_L3` — a controller stick
   click, with no keyboard equivalent at all.**
   ⛔ **And unlike the grid, this one is a DOUBLE gate — `/call` cannot ungate
   it** (measured 2026-08-20). `UIToDoWidget.OpenScreen` takes no parameters, so
   the endpoint invokes it happily and it returns normally — pushing nothing,
   because its body is wrapped in `if(HelpIcon != None && HelpIcon.bIsVisible)`
   and `HelpIcon` is spawned inside `if( ISCONTROLLERACTIVE )` in
   `InitToDoWidget` (`:178`). Live it reads **null**. So the notification screen
   is unreachable on mouse+keyboard by any zero-argument route; reaching it
   means pushing `UINotificationMenu` ourselves, and
   `InitNotificationMenu(Self, PC, Movie)` takes three parameters — a real
   outbound `CallScriptFunction` with a hand-rolled param struct, not `/call`.
   **The lesson generalises: a controller gate is only one call away when the
   gated code has no controller-only DEPENDENCY.** `InitializeNavigationGrid`
   had none; this has one.
5. **The shortcut submenu is reachable but awkward.** `List.OnUnrealCommand` is
   forwarded only while `List.bIsVisible && InRoomCurrentShortcutTabHighlightIndex
   == CurrentCategory`, and on mouse+keyboard `InRoomCurrentShortcutTabHighlightIndex`
   is maintained only in the controller path.
6. **The event queue's expand button is mouse-only**, so collapsed it shows one
   event of however many.

### Two live results, and what they do NOT establish

Both keys were injected and the arbiter logged them delivered
(`InputArbiter: passed vk 0x09` / `0x1B`), and neither changed the screen stack.
What each *means* is still open, and the log shows why: the user was pressing
arrows in the same window in the same seconds (`vk 0x27`, `0x25`, `0x25` at
19:24:45–46, straddling an injected Tab). **Two drivers on one session — the
shared-harness rule applied to a human.** Neither result is clean evidence.

- **`Escape` in the Commander's Quarters did nothing** — ✅ **RESOLVED
  2026-08-20 (second pass): the input half was never involved.** Invoking the
  screen's own `OnCancel()` through `/call`, with no keyboard in the loop at
  all, left the stack byte-identical. So `Escape` was reaching the game, the
  handler was running, and the handler refuses. The gate is
  `UIFacility_CIC.OnCancel`'s `IsObjectiveCompleted('T0_M1_WelcomeToLabs')`.
  ⚠ My earlier doubt about that was **bad reasoning, not bad data**:
  `GetResearchMessages` fires on `!= eObjectiveState_InProgress`, which
  **NotStarted** satisfies just as well as Completed, while `IsObjectiveCompleted`
  demands Completed — the two readings never conflicted. `CloseScreen`'s
  `CAMIsBusy()` is ruled out separately: the refusal reproduced minutes apart
  with the camera long idle, and the screen reads `bIsInited`.
  **Consequence for design: at this tutorial step the ant farm is not a
  destination at all.** The player is held in the room until the objective
  advances, and the only sanctioned move is the shortcut bar. A layer of ours
  must not offer a "back" the game refuses — it should say where the game wants
  the player to go instead.
- **`Tab` in the Commander's Quarters did nothing.** `UIAvengerHUD` declares
  `FXS_KEY_TAB` / `FXS_KEY_P` → personnel list, inside
  `` `if(`notdefined(FINAL_RELEASE)) ``. Retail is otherwise not FINAL_RELEASE
  (the cheat manager exists — CLAUDE.md), so the block should be compiled in;
  the likelier explanation is that something upstream ate it. Either way: **not
  a shortcut we can rely on** without a clean retest on the grid.

---

## 6.1 Ungating the grid — one call, no mouse-mode change

`ISCONTROLLERACTIVE` is **not a hardware check**. `Core/Globals.uci:207`:

```
`define ISCONTROLLERACTIVE (... && `PRESBASE.Get2DMovie().IsMouseActive() == false)
```

It is the 2D movie's mouse-active flag — the same flag CLAUDE.md already
discusses under `UIMovie.DeactivateMouse()`. ⛔ **Do not flip it.** Turning the
mouse off globally changes which HUD the game builds (proven in tactical:
`UITacticalHUD.OnInit` spawns a different button set per branch), and here it
would actively make the keyboard *worse* — `UIAvengerShortcuts.OnUnrealCommand`
routes the `2`–`6` keys through `SelectCategoryButtonOnly` (highlight, then
confirm) in the controller branch instead of jumping straight to the room.

The surgical ungate is to call the gated function ourselves:

**`UIFacilityGrid.InitializeNavigationGrid()` — `simulated function`, no params,
no return value, declared at class scope.** That is exactly the shape
`GameUI::CallScriptFunction` already handles, and `FindScriptFunction`'s
class-chain walk reaches it (unlike the state-scoped free-aim functions in
`docs/free-aim-recon.md` §4.2). One outbound call, on the game thread, when the
grid becomes the current screen.

Why that is sufficient — four facts, all from source:

1. **Arrows already reach the navigator on mouse+keyboard.** The m+kb branch of
   `UIFacilityGrid.OnUnrealCommand` handles only right-mouse and then ends with
   `return bHandled || super.OnUnrealCommand(cmd, arg)`. `UIPanel`/`UIScreen`
   route that into `Navigator.OnUnrealCommand`.
2. **The nav targets live on the overlays, not on the screen.**
   `UINavigator.OnUnrealCommand` calls `SendToNavigationTargets` *first*, and
   that delegates to `GetSelected().Navigator.SendToNavigationTargets(...)`.
   So the graph works **independently of the screen navigator's
   `OnlyUsesNavTargets`**. Setting that flag only suppresses the linear
   Prev/Next fallback at graph edges — worth doing for clean behaviour, not
   required for the graph to function.
3. **`Enter` / `Space` are not gated at all.**
   `UIFacilityGrid_FacilityOverlay.OnUnrealCommand` handles `FXS_KEY_ENTER` and
   `FXS_KEY_SPACEBAR` unconditionally (guarded only by `HQPRES.m_bCanPause`, and
   in the tutorial by `bLocked`). Entering a room by keyboard works the moment
   something is selected.
4. **The camera follows for free.** `Navigator.OnSelectedIndexChanged =
   OnSelectedRoomChanged` is assigned in `OnInit` **unconditionally**, and it
   calls `LookAtSelectedRoom()` plus the shortcut-bar highlight sync.

### The bonus: gap 2 closes itself

`strTitle` is set under `if( bIsFocused )`, and `bIsFocused` is set by
`UIFacilityGrid_FacilityOverlay.OnReceiveFocus` — which the **navigator** calls
on keyboard selection exactly as the mouse does. `OnReceiveFocus` then calls
`UpdateData()`, which runs `SetLabel`/`SetStatus` and populates the retained
`Label` / `Status` / `Queue`. So keyboard-navigating the grid makes the room's
name and status appear *and* readable, through the game's own code path. That is
sighted parity produced by the game itself, not an aid we invented — and it
means §3.3's "retained but only for hovered rooms" stops being a problem for the
room the player is actually on.

### Measured live 2026-08-20 (second pass, `/call`)

The call lands and does exactly what §6.1 predicted. `UIFacilityGrid_FacilityOverlay_0`'s
own `UINavigator`, before and after
`just dev-call InitializeNavigationGrid "screen=UIFacilityGrid"`:

```
              NavigationTargets   OnlyUsesNavTargets
  before              0                 false
  after              13                 true
```

The screen's own navigator, unchanged by the call and worth recording because it
is the **baseline** the ungate improves on:

```
  SelectedIndex        0        (an overlay IS selected on mouse+keyboard)
  NavigableControls   19        (all rooms registered)
  NavigationTargets    0
  OnlyUsesNavTargets   false    <- the flag InitScreen sets only under the gate
  OnSelectedIndexChanged = UIFacilityGrid.OnSelectedRoomChanged   <- hooked, unconditionally
```

So unmodified, on mouse+keyboard, arrows fall to `UINavigator`'s linear
Prev/Next: `HorizontalNavigation` is false, so **Up/Down step through the 19
rooms in map order and Left/Right do nothing** — with the camera swooping on
each step, since the delegate is hooked either way. That is the fallback the
graph replaces.

### ✅ CONFIRMED LIVE 2026-08-20 — arrows drive the ant farm

With the graph installed and `UIFacilityGrid` finally the top screen (which took
completing `T0_M1_WelcomeToLabs` — see §6), injected arrows moved the selection
along the hardcoded graph, exactly:

| key | `Navigator.SelectedIndex` | what the source declares |
|---|---|---|
| Right | 1 → **9** | `PowerCore.AddNavTargetRight(FacilityOverlays[9])` |
| Right | 9 → **10** | slot 9 `Right` → 10 |
| Down | 10 → **13** | slot 10 `Down` → 13 |
| Left | 13 → **12** | slot 13 `Left` → 12 |
| Up | 12 → **9** | slot 12 `Up` → 9 |
| Left | 9 → **1** | slot 9 `Left` → PowerCore |

Every step matched. Two facts fall out of the numbers themselves:
`Navigator.SelectedIndex` indexes `NavigableControls` and those values are
`FacilityOverlays` indices, so **the two arrays are in the same order** — an
overlay's selection index IS its `FacilityOverlays` index, which is what makes
the §3.1 layout map directly usable at runtime. And **Left/Right move at all**,
which is the clearest proof the graph is doing the work: the linear fallback has
`HorizontalNavigation = false`, so before the ungate they did nothing.

Screenshot confirms the visible half: the selected room is drawn with its
highlight box, following the keyboard.

### ⚠ The room-name bonus is real in mechanism but invisible in the tutorial

§6.1 predicted keyboard focus would populate `Label`/`Status` through
`OnReceiveFocus` → `UpdateData()`. The focus delivery is confirmed —
`UINavigator.SetSelected` calls `NavigableControls[Index].OnReceiveFocus()`
directly, and the overlays do read `FOCUSED` after each arrow. But every room
measured, including the **Power Core** (a real facility, not an empty slot),
came back:

```
  Label = ""   Status = ""   Queue = ""   bLocked = true
```

The branch is `UpdateTutorialLocked()`, which is reached whenever
`Facility.bTutorialLocked` — and it is the **one** update branch that sets no
title in any case, focused or not. During the tutorial the whole ship is locked
this way.

⛔ **This is parity, not a gap, and must not be "fixed".** The screenshot shows
the selected Power Core with its highlight box and **no name text** — a sighted
player selecting that room right now is told nothing either. Speaking a room's
name here would hand a blind player information the game is deliberately
withholding from everyone, which is exactly what the sighted-parity rule
forbids. The name-on-focus behaviour should light up on its own once the
tutorial unlocks the ship; that is the thing to re-measure then, not to force
now.

### ⚠ A locked room refuses in SILENCE — and this one IS a parity gap

Measured 2026-08-20: `Enter` on the tutorial-locked Power Core changed nothing.
`UIFacilityGrid_FacilityOverlay.OnConfirm` opens with a bare
`if( bLocked ) return;` — no sound, no popup, no feedback of any kind.

Unlike the missing room names, **this one must be covered.** A sighted player is
not told anything either, but they can SEE the room is dimmed: `UpdateTutorialLocked`
sets `eBorderColor = eUIState_Disabled` and `iAlpha = 0`, so the room is visibly
inert and nobody sighted presses it twice. A blind player gets pure silence and
no way to tell "locked" from "our layer is broken". Speaking *locked* is
therefore parity with the greyed-out treatment, not an aid — the same reasoning
as the object scanner's tower gate naming its own gate rather than claiming the
map is empty.

### ⚠ Diagonal links are controller-only, and it does not matter

`AddNavTargetLeft/Right/Up/Down` each register **six** entries — `FXS_ARROW_*`,
`FXS_DPAD_*` and `FXS_VIRTUAL_LSTICK_*`, once for `FXS_ACTION_PRESS` and once for
`FXS_ACTION_POSTHOLD_REPEAT`. The four `AddNavTargetDiag*` helpers register
**one** entry each, and it is `FXS_VIRTUAL_LSTICK_DIAG_*` only. There is no
keyboard chord for a diagonal at all.

(That is the arithmetic behind the 13: the Commander's Quarters declares
Right + Down + DiagDownRight = 6 + 6 + 1.)

Parsing all **152** `AddNavTarget*` calls out of `InitializeNavigationGrid` and
keeping only the cardinal ones gives the keyboard's view of the ship:

- **Every room has at least one keyboard exit.** No diagonal-only room exists.
- **The cardinal-only graph is fully strongly connected: from any of the 19
  rooms, arrows alone reach all 19.** Checked in both directions — no room is a
  one-way trip, and every room can get back to the Commander's Quarters.

So the 51 diagonal links are pure convenience for a stick, and losing them costs
a blind player reachability of nothing. **The ungate needs no diagonal
substitute.**

### Risks to measure, not assume

- **Does the mouse steal the selection back?** Unlike
  `Mouse_CheckForPathing` in tactical, nothing here re-pushes per frame — an
  overlay takes focus on `FXS_L_MOUSE_IN` only. Expected to be fine; a stationary
  mouse resting over a room at the moment we navigate is the case to try.
- **Idempotence.** `InitializeNavigationGrid` starts by clearing all nav targets
  on all 19 overlays, so calling it twice is safe. It hardcodes `i < 19` and
  indexes `FacilityOverlays[0..18]`; a mod that changes the room count would
  break the game's own function, not ours, but the call should still be guarded
  on `FacilityOverlays.Length == 19`.
- **When to call.** The grid is created once and kept; `InitScreen` runs once. So
  one call per `UIFacilityGrid` instance, on first sighting, is enough — but it
  must be re-armed if the screen is ever re-created (map transitions).

---

## 7. What a blind player gets today

Nothing. No screen in this layer has a companion listener or a native recipe.
The player cannot learn what room they are in, what the ship needs, what is
building, who is unassigned, or how much of anything they have — and cannot
reach the ant farm's rooms without the mouse at all.

The one mercy is that `1`–`6` work, so the five main rooms are reachable
blind — but with no readout, entering one is silent.

---

## 8. Data inventory — where each fact actually lives

| Fact | Source | Kind |
|---|---|---|
| location / room name | `UIX2ScreenHeader.LocationField` / `.RoomField` | ✅ widget, retained |
| shortcut categories, labels, alerts, submenu items | `UIAvengerShortcuts.Categories[]` | ✅ widget, retained |
| current room | `UIAvengerShortcuts.CurrentCategory` | ✅ widget, retained |
| to-do items per category | `UIToDoWidget.Categories[].Messages[]` | ✅ widget, retained (refreshed only on the grid) |
| room title / status / queue | `UIFacilityGrid_FacilityOverlay.Label` / `.Status` / `.Queue` | ⚠ retained but only for hovered rooms |
| room condition flags | overlay `bUnderConstruction` / `m_bClearing` / `bLocked` | ✅ retained |
| room adjacency | `XComGameState_HeadquartersRoom.AdjacentRooms` / `GridRow`, and `UIFacilityGrid.InitializeNavigationGrid` | ✅ state + hardcoded graph |
| facility name, upkeep, power | `X2FacilityTemplate.DisplayName`, `Facility.UpkeepCost`, `GetPowerOutput()` | script |
| facility status / queue / attention | `Facility.GetStatusMessage()` / `GetQueueMessage()` / `NeedsAttention()` | script |
| construction time left | `Room.GetBuildFacilityProject().GetCurrentNumHoursRemaining()` | script |
| staff slot occupant + bonus | `XComGameState_StaffSlot` via the widget's `StaffSlotRef` | script, off the screen's own ref |
| resources | `UIUtilities_Strategy.GetResource(...)`, `XComHQ.GetNumberOf*`, `GetCurrentResContacts` / `GetPossibleResContacts`, `GetPowerConsumed` / `GetPowerProduced`, `PowerState` | script |
| event queue | `XComHQ.GetEvents(out array<HQEvent>)` | script |
| objectives | `XComGameState_ObjectivesList` / `UIObjectiveListItem.Data` | ✅ widget + state |
| crew alerts (bonds, traits) | `XComHQ.Crew` + `Unit.HasSoldierBondAvailable` / `AlertTraits` | script |
| the whole ship at once | `UIFacilitySummary` (name/staff/power/date/status/queue, sortable) | screen |

The pattern that falls out: **this layer is the inverse of tactical.** There the
widget tree held structure and no words. Here two of the richest widgets hold
complete, localized, urgency-tagged text — and the rest is one plain script hop
from the state objects the screens themselves reference. Nothing here needs a
new hand-transcribed offset the way the shell recipes did; it needs the
companion to read what the screens read.

---

## 9. What the next live pass must answer

**Target state: the earliest Avenger tutorial step, not a mid-campaign save**
(decided 2026-08-20). That step is where a new blind player actually arrives, so
it is what has to work first; the campaign-save questions below are deferred, not
dropped. Everything in §2.2, §2.5 and §2.6 stays *[not seen live]* until later,
and features built now must degrade gracefully when those surfaces are hidden —
which is the tutorial-shaped special case CLAUDE.md's "read the screen" rule
exists to avoid, so prefer readouts that are simply empty there.

⚠ **One driver at a time.** The 2026-08-20 pass had injected keys and the user's
own keypresses interleaved in the same window within the same second, which cost
two inconclusive results (§6). Agree who is driving before the next pass.

✅ **Questions 1–3 were answered live on 2026-08-20** — see §6 (the `Escape`
gate) and §6.1 (the baseline, the ungate, and the arrow test). They are kept
below for the record; 4–6 remain open.

Recon-blocking, in order:

1. ✅ **ANSWERED — no, not until `T0_M1_WelcomeToLabs` completes.** The tutorial
   holds the player in the Commander's Quarters; picking the first research
   releases it, and the ant farm becomes reachable by `Escape` immediately.
   **Can the ant farm be reached at all from the early tutorial step?** `Escape`
   out of the Commander's Quarters did nothing (§6) and the reason is
   unresolved. If the grid is unreachable there, ungating it is still right but
   is not the *first* thing this step needs. Establish the reachable surface set
   before designing.
2. **Does `InitializeNavigationGrid()` actually ungate it?** The §6.1 argument is
   source-derived and untested. Test: on the grid, read
   `/props?screen=UIFacilityGrid` and one overlay's Navigator before and after
   the call; press Left/Right/Up/Down; watch which overlay carries `FOCUSED` in
   `/gui?screen=UIFacilityGrid`, whether the camera moves, and whether that
   overlay's `Label`/`Status` become non-empty (the §6.1 bonus).
   The harness can do this now: **`just dev-call InitializeNavigationGrid
   "screen=UIFacilityGrid"`** (`/call`, built 2026-08-20 for exactly this
   question — see CLAUDE.md's harness section). It refuses functions that take
   parameters; this one takes none.
3. **Baseline first:** before ungating, press the arrows on the grid *unmodified*
   and record what the linear fallback does. Without that baseline the ungate has
   nothing to be measured against.
4. **Retest `Tab` and `P` on the grid**, with nobody else touching the keyboard —
   does `UIPersonnel` appear on the stack?
5. **Is the shortcut submenu keyboard-navigable inside a room?**
   `SelectCategoryForFacilityScreen` sets
   `InRoomCurrentShortcutTabHighlightIndex = CurrentCategory` and calls
   `ShowList`, which should satisfy the forwarding condition in gap 5 — yet live
   the list read `inited nav sel=-2` and **not visible**. Resolve: is
   `HQPRES.ShowAvengerShortcutList` deferred, or is the list genuinely never
   shown on mouse+keyboard?
6. **Hover one room with the mouse, then read that overlay's `Label`/`Status`**
   to confirm the retained-on-hover model end to end.

Deferred to a mid-campaign save: a populated resource strip (to confirm nothing
of it is retained anywhere), a multi-event queue, an objectives list with
entries, rooms under construction and rooms with empty staff slots, and whether
`UIToDoWidget` refreshes anywhere but the grid.

---

## 10. Open design questions (for the UI discussion, not decided here)

- The ship is a **spatial map with a hardcoded eight-way adjacency graph** and
  also a **flat sortable table** (`UIFacilitySummary`). Which is the primary
  navigation model for a blind player, and is the other an aid?
- The to-do widget is the game's own prioritised action list, with working
  hotlinks. Does it become a first-class surface (its own key), or does it fold
  into a general "status" readout?
- The `1`–`6` shortcuts already work and already move the camera. How much of
  our navigation should just *be* those keys plus narration?
- Sighted parity here is unusually **low**: room names need hovering, the event
  queue shows one line collapsed, the to-do lists need hovering. A readout that
  speaks a room's full state on arrival is arguably *more* than parity. Where is
  the line, and what gets labelled as an aid?

---

## 11. Phase 1 as built (2026-08-21) — the ant farm reader

Built, compiled, **not yet run live.** Three files:

- `src/avenger.cpp` / `.h` — the gate, one key, the poll.
- `mods/X2AccessCompanion/.../X2AccessAvengerReader.uc` — the ungate and every word.
- `GameUI::ReadAvengerGate()` in `src/game_ui.cpp` — the stack test.

### It claims no arrow keys, and that is the whole design

Every other navigable surface in this project needed a Graph recipe or a cursor
because the game had **no** keyboard path. The ant farm is the opposite case:
the game ships the complete eight-way graph and merely never installs it on
mouse+keyboard. So the layer installs it and then only narrates — the game's own
`UINavigator` does the moving, and brings the camera swoop, the focus delivery
(`OnReceiveFocus` → `UpdateData()`, which is what makes `Label` appear at all)
and the highlight box with it. Claiming the arrows would displace all three and
buy nothing.

One key is claimed: **`Delete` re-announces the selected room**, the same verb
the tile cursor and the shot HUD put on it, and it never contends with either —
those gate on tactical, this on a strategy screen. What it displaces is
`[Engine.PlayerInput]`'s binding of `Delete` to `Toggledebugcamera`, which stays
reachable at `Ctrl+Delete` for free under UE3's lenient modifier matching.

### The gate is the game's own test

`ReadAvengerGate()` walks `UIScreenStack.Screens` and asks whether
`UIFacilityGrid` is the **first** entry — matching `GetCurrentScreen()`'s own
definition (first non-none, no state-bit filter), because that is exactly what
`UIFacilityGrid.OnUnrealCommand` tests before handling anything on
mouse+keyboard. A gate that disagreed with that test would narrate a screen the
arrows do not reach. The companion re-checks the same thing from script and
additionally honours the grid's own `bIsVisible` — the documented exception to
"never gate readiness on `bIsVisible`", because here the *game* gates on it.

### The ungate is re-armed from the graph's own emptiness

`EnsureNavigationGrid` calls `InitializeNavigationGrid()` when
`FacilityOverlays[0].Navigator.NavigationTargets.Length == 0`, which is:

- **idempotent** — the game's function clears every overlay's targets first;
- **self-healing** — a grid re-created by a map transition comes back empty and
  is re-armed on the next tick, with no screen identity tracked (a CDO must not
  hold a `UIScreen` reference at all);
- **a no-op on a controller**, where the game already installed it.

It also sets the screen navigator's `OnlyUsesNavTargets`, mirroring the
controller branch of `InitScreen`: without it an arrow with no target in that
direction falls through to `UINavigator`'s linear Prev/Next and jumps to an
unrelated room in map order.

### Every word comes off the overlay, none from the game state

`UIFacilityGrid_FacilityOverlay` retains `Label` / `Status` / `Queue` /
`bLocked` / `bUnderConstruction` / `m_bClearing` / `IconTypes` / `BGColor`, each
written by a setter under a dirty check immediately before the matching Flash
op. So the retained value **is** what Flash was last told, and there is not one
`XCOMHISTORY` access in the class. That is CLAUDE.md's "read it FROM that
screen" rule in its easiest form, and it buys the §6.1 parity determination for
free: the name is set under `if( bIsFocused )`, so it appears exactly when the
game shows it and is empty exactly when the game does not.

The line, in order: **name** (when the game gives one) · **position** ·
**condition** · **status** · **queue** · **staff**.

- **Position** is spoken always, because the highlight box's place in the ship
  is the part of the selection a sighted player can see even when no title is
  drawn — and during the tutorial it is the *only* thing distinguishing one
  arrow press from the next. Core slots read `row R, column C` (indices 3–14,
  row-major, 3 wide × 4 tall); the seven fixed rooms get a spatial phrase.
  ⚠ These are POSITIONS, never facility names — see §6.1's parity ruling.
- **`locked`** is the one word here that closes a gap rather than mirroring
  text (§6.1: `OnConfirm` refuses in complete silence, while a sighted player
  sees the room dimmed).
- **`needs attention`** is the orange border, which on a built or core facility
  means exactly `Facility.NeedsAttention()` and is drawn whether or not the room
  is focused — so it is part of the at-a-glance read without hovering anything.
- **Staff** counts empties per KIND (`2 engineer slots free`), because an empty
  engineer slot and an empty scientist slot are different jobs but two empty
  engineer slots are one job twice. `eUIFG_Upgrade` is counted apart — it is not
  a staff slot at all.

### The position table, and where it came from

Derived from the **cardinal** half of `InitializeNavigationGrid`'s graph (the 51
diagonal links are `FXS_VIRTUAL_LSTICK_DIAG_*` only — no keyboard chord exists
for them, §6.1):

```
              [0]  top left
        [16] upper left   [15] top right
  [1]   ┌  3   4   5 ┐    [2]  right side, upper
  far   │  6   7   8 │    [18] right side, middle
  left  │  9  10  11 │    [17] right side, lower
        └ 12  13  14 ┘
```

⚠ The seven fixed rooms' phrases are the graph's relations put into words, not
a reading of the rendered ship. **Re-check them against a screenshot once the
tutorial unlocks the Avenger** — the buildable core's row/column is exact, the
outer ring's wording is the part that could be improved.

### What phase 1 deliberately does not do

- **No `Enter` claim.** The overlay's `OnUnrealCommand` handles `FXS_KEY_ENTER`
  and `FXS_KEY_SPACEBAR` unconditionally already, so entering a room works; the
  silent refusal on a locked room is covered by speaking *locked* on arrival
  rather than by intercepting the key.
- **No edge cue.** An arrow with no target in that direction is silent, which is
  parity (the box does not move) but would be friendlier with a bump sound.
- **Nothing inside a room.** `UIFacility` and its 22 subclasses are §4 and a
  later phase — as are the resource strip, the to-do widget and the event queue.

### First live run must check

1. **Does the announcement fire at all**, and does `just dev-log` carry
   `Avenger: ant farm active` plus `Avenger navigation grid installed`?
2. **Does a resting mouse steal the selection back?** (§6.1 "Risks to measure":
   nothing re-pushes per frame here the way `Mouse_CheckForPathing` does in
   tactical, but a stationary pointer over a room at the moment we navigate is
   the case to try.)
3. **`/props?screen=UIFacilityGrid&filter=Selected`** next to what was spoken —
   the same read-back-the-write check that settled the Second Wave checkboxes.
4. **Leaving and re-entering the grid re-announces** (the falling-edge poll).
5. Post-tutorial: does `Label` populate on keyboard focus as §6.1 predicts?
