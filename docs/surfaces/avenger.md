# Avenger surfaces

Implementation rules for the ship — the ant farm, the rooms inside it, and the list screens the rooms open. Each ⛔ was earned by a live bug; each ⚠ is a
known limit or an open decision. **Read the section for a surface before editing
it** — `CLAUDE.md`'s Surfaces table points here for exactly that reason.

Cross-cutting rules (buffer sizing, the screen-accept trap, the empty-field trap,
speech joining, parity) live in `CLAUDE.md`; only what is specific to these
screens is here.

## Avenger ant farm

The first surface where the game already HAS keyboard navigation and merely never turns it
on. `UIFacilityGrid.InitScreen` builds its whole eight-way room adjacency graph inside
`` `if( `ISCONTROLLERACTIVE) `` — and that macro is **not a hardware check**, it is the 2D
movie's mouse-active flag (`Core/Globals.uci:207`). The fix is to call the gated function
ourselves, once per grid instance, so this layer is **narrate-only and claims no arrows**: the
game's own `UINavigator` moves and brings the camera swoop, focus delivery and highlight box
with it. The 51 DIAGONAL links are `FXS_VIRTUAL_LSTICK_DIAG_*` only and have no keyboard
chord; the cardinal-only graph is fully strongly connected across all 19 rooms. Keys are
re-armed off the GRAPH'S OWN EMPTINESS (`FacilityOverlays[0].Navigator.NavigationTargets.Length
== 0`) rather than screen identity: idempotent, self-healing across a map transition, a no-op
on a controller, and needs no `UIScreen` reference on the CDO. One claim: `Delete`
re-announces, displacing only `[Engine.PlayerInput]`'s `Toggledebugcamera`, still reachable at
`Ctrl+Delete`.

Every word comes off the OVERLAY, never the game state —
`Label`/`Status`/`Queue`/`bLocked`/`bUnderConstruction`/`m_bClearing`/`IconTypes`/`BGColor` are
each written by a setter under a dirty check immediately before the matching Flash op, so the
retained value IS what Flash was last told, and the class holds not one `XCOMHISTORY` access.
That buys parity for free: the room NAME is set under `if( bIsFocused )`, so it appears exactly
when the game shows it.

- ⛔ **Do NOT flip the `ISCONTROLLERACTIVE` flag**: mouse-inactive mode changes which HUD the
  game builds (the same reason the tile cursor uses a swallow), and here it makes the keyboard
  WORSE — `UIAvengerShortcuts` routes `2`-`6` through a highlight-then-confirm path in the
  controller branch instead of jumping straight to the room.
- ⛔ **BUILD MODE PUTS A SCREEN ON TOP OF THE GRID, AND BOTH HALVES OF THE GATE HAVE TO KNOW.**
  `ReadAvengerGate` tolerates `UIBuildFacilities`; the COMPANION's own `GetGrid` must too —
  resolving it as `UIFacilityGrid(GetCurrentScreen())` answers `none` in build mode, so
  `PollGrid` bails on its first line and native says *go ahead* ten times a second while script
  says *not on the grid*, on the one screen where a player is choosing a room to build in. Read
  `` `HQPRES.m_kFacilityGrid `` — the very object `UIBuildFacilities.OnUnrealCommand` forwards
  keys to. **A gate is only as good as its other half**; changing one half of a native+script
  gate is changing half a rule.
- ⛔ **The source's own room names are WRONG and the overlay's are right**:
  `InitializeNavigationGrid` calls slot 1 `PowerCore`, slot 2 `Hangar`, slot 17 `Storage`, but
  the player hears Research, Armory and Engineering — and Firaxis' own `shortcutRoomIndices`
  comments in that same function (`1 //Research`, `17 //Engineering`, `2 //Armory`) side with
  the player. Do NOT "simplify" the `Label` read into a lookup.
- **The strategy layer's text is COLOURED** (`GetQueueMessage` returns `<font color=...>` runs;
  `UpdateLockedRoom` runs `m_strLockedLabel` through `GetColoredText`), so `NativeResult` goes
  through `GameUI::CleanText` — a STRIP, not the translation the tutorial reader's `<img>`
  markup needs.
- **The condition words come off the widget too**: `m_strLocked` and `m_strUnderConstruction`
  are localized AND, for an un-excavated room, literally what the game prints in the status
  line — which is why `Status` is dropped when it CONTAINS the condition (`InStr`, not equality:
  the build-mode branch wraps it in markup). Clearing emits no word, because
  `UpdateClearingRoom` sets `strTitle` unconditionally and `Label` already says it. In build
  mode `strLabel` is where `UpdateEmptyRoom` puts BUILD FACILITY and `UpdateLockedRoom` the
  locked marker.
- ⛔ **A tutorial-locked room's name must NOT be spoken** — `UpdateTutorialLocked()` is the one
  branch that sets no title in any case, focused or not, and a sighted player selecting that
  room is told nothing either; speaking a name we looked up ourselves would hand a blind player
  what the game withholds from everyone.
- ✅ **A locked room's REFUSAL must be spoken** — `OnConfirm` opens with a bare
  `if( bLocked ) return;`, no sound and no popup, but a sighted player SEES the room dimmed
  (`eUIState_Disabled`, `iAlpha` 0) and never presses it twice. Saying *locked* on arrival is
  parity with the dimming, and needs no `Enter` claim (`FXS_KEY_ENTER`/`FXS_KEY_SPACEBAR` are
  handled unconditionally by the overlay).
- ⚠ POSITION is spoken always — the highlight box's place is visible even when no title is
  drawn and, in the tutorial, is the only thing distinguishing one arrow press from the next.
  Core slots as `row R, column C` (indices 3-14, row-major, 3 wide x 4 tall), the seven fixed
  rooms as a spatial phrase, and **POSITIONS ONLY, never facility names**. The gate is the
  game's OWN test: `GameUI::ReadAvengerGate` asks whether `UIFacilityGrid` is the FIRST stack
  entry, matching `GetCurrentScreen()`'s definition (first non-none, no state-bit filter),
  because that is what `UIFacilityGrid.OnUnrealCommand` checks on mouse+keyboard. The companion
  additionally honours the grid's own `bIsVisible` — the documented exception to "never gate
  readiness on `bIsVisible`", since here the GAME gates on it.
- ⚠ What stays English is what the game ships no string for: `needs attention` (a gloss of a
  border colour), the position phrases and the staff phrasing — an open decision, not a bug.
- `LogRoomGeometry` dumps every overlay's `m_RoomLocation` + `RoomMapIndex` once per grid
  install. Use it to replace the seven hand-written fixed-room position phrases with measured
  ones, and to check that `FacilityOverlays` really is sorted into MapIndex order, which the
  whole index scheme assumes.

## Inside a room

⛔ **The room SCREEN is not the room's content, and building for its staff strip produces a
reader that is silent on every room a new player can reach.** All seven FIXED rooms declare
**zero** `StaffSlotDefs` — CommandersQuarters, CIC, PowerCore (Research), Hangar (Armory),
LivingQuarters, BarMemorial, Storage (Engineering) — so `RealizeStaffSlots` calls
`Navigator.RemoveControl` and `UIFacility`'s navigator holds nothing. Staff slots exist only in
BUILDABLE facilities, all twelve of whose slots read `BGColor="0x828282"` (unexcavated) on an
ordinary early campaign.

The content is one screen **down**: `UIFacility.OnUnrealCommand` returns false when it has
nothing, `UIScreenStack.OnInput` walks LIFO, and the keys land on `UIAvengerHUD` →
`Shortcuts.OnUnrealCommand` → `List` — the submenu the game AUTO-OPENS for the room just entered
(`SelectCategoryForFacilityScreen` → `ShowList` → `List.SetSelectedNavigation`). That is where
the Lab's *New Research / View Scientists* and the Armory's *Soldiers / Recruit / Memorial* live,
so the view spans two screens and the recipe's nodes come from both.

**This is a RECIPE, and the contrast with the ant farm is the whole ruling.** Graph recipes are
the RULE — they buy focus tracking, a stable reading order, and stops on panels the game makes
unfocusable; game-native narrate-only is the EXCEPTION and each one must be justified. The test:
*narrate-only is justified only when taking the arrows would destroy information we cannot
reproduce.* The ant farm passes (the room NAME is set inside `if( bIsFocused )`, so our own focus
model would leave every room nameless, plus the camera swoop and highlight box). Here nothing is
destroyed and three things are bought: a stop on the crew line (`UIStaffContainer.Title` +
`Skill` — drawn in the room, reachable by no keyboard, and the number that decides whether the
room's verbs will do anything, which is why it reads BEFORE the actions); the row's
`bIsBad`/`bIsWarning` colouring, a whole channel of meaning speech otherwise loses; and focus
tracking where the game's own is invisible anyway — `UIStaffSlot.OnReceiveFocus` sends its
highlight op only under `` `ISCONTROLLERACTIVE ``.

Every word is a RETAINED widget read, no `XCOMHISTORY` and no Flash: `FacilityHeader`
(`UIX2ScreenHeader`) keeps `LocationField`/`RoomField`, the staff container keeps
`Title`/`Icon`/`Skill` **even while hidden**, and each row keeps its own localized `Text`.

- ⛔ **The name comes from `RoomField` and must keep coming from there**: the room TEMPLATES'
  own `DataName`s are systematically wrong — `PowerCore` is Research, `Hangar` is the Armory,
  `Storage` is Engineering — the same trap as the ant farm's local variables, in shipped data
  rather than one function.
- **Activation is select-then-Enter on the LIST** (`SetSelectedIndex` then
  `SendRowCommand(list, kCmdEnter)`), because `UIList.OnUnrealCommand` fires
  `OnItemClicked(self, SelectedIndex)` and ignores the row the caller had in mind — the same
  trap `UISaveGame.OnAccept` sets. Rows are matched by the GAME's index (padding skipped, never
  renumbered) and re-read at activation, since `DoShowList` respawns every row on a category
  change.
- ⛔ **A room's verbs live in THREE places, and the shortcut submenu is only one.** The others
  are on the room screen: `UIFacility`'s **`m_kRoomContainer`**, a bottom bar of
  `UIFacility_RoomFunc`s added by the subclass's `CreateFacilityButtons()`, and
  **`UpgradeButton`**. Only six facilities have a bottom bar, but two are FIXED rooms a new
  commander walks into on day one — the Bar's *Obituaries* and the Living Quarters' *Personnel*
  — and for those it is the ONLY content, their shortcut category being -1. The game's own input
  order settles reading order: `UIFacility.OnUnrealCommand` offers keys to the room first and
  lets them fall through to `UIAvengerShortcuts` only when the room handles nothing, so a room
  WITH a bar reads it before the submenu. An empty list is legitimate — but it must mean empty,
  not "empty in the one place I looked".
- ⛔ **A `UIFacility_RoomFunc` retains NO TEXT** — `InitRoomFunc` takes the label as a PARAMETER,
  pushes it through `MC.FunctionString("update", label)` and keeps only the delegate — so
  `X2AccessFacilityReader` reads it back off the SCREEN's own `localized` properties
  (`m_strObituaries`, `m_strPersonnel`, `m_strViewUnlocks`, `m_strStartProject`,
  `m_strTrainAbilities`, `m_strViewActions`). The class switch only ROUTES; it never supplies a
  word. Reading the delegate's FName instead — the one other thing the widget keeps — would say
  *"OnShowObituaries"* to a Russian player. The upgrade button needs none of that:
  `ShowUpgradeButton` goes through `UIButton.SetText`, so its `Text` is retained. Activation is
  `FXS_BUTTON_A`, **not Enter**: `UIFacility_RoomFunc.OnUnrealCommand` tests that one constant
  and nothing else, which is why the bar is mouse-or-gamepad in vanilla. Labels are cached on the
  button pointers because the frame differ rebuilds ten times a second and `AddFacilityButton`
  runs once, in `InitScreen`.
- ⛔ **The Resistance Ring has a SECOND wordless panel, and it is the one that says whether
  walking in achieves anything.** `UIFacility_CovertOpsStatus` (`UIFacility.m_kCovertOpsStatus`,
  spawned only by `UIFacility_CovertActions.InitScreen`) pushes the header, the running action's
  name and the days left through `MC.FunctionString("setTitle"/"setDaysValue"/…)` and retains
  none of it — its own `Label` var is declared and **never assigned**, so reading the widget gets
  `""`. `X2AccessFacilityReader` re-derives it by re-running the SCREEN'S OWN `UpdateData`:
  iterate the history for the `XComGameState_CovertAction` with `bStarted`, then format with the
  same `UIUtilities_Text.GetTimeValueAndLabel(Hours, …, 0)` call `Refresh` makes, falling back to
  `CovertOpsStatus_EmptyLabel`/`EmptyTitle`. Same rule as `X2AccessSaveReader` — re-run the
  formatting the panel ran, never invent a second one. Three fields on the wire (`label`, `title`,
  `time`), joined by native through `AppendPhrase`, because the game ends
  `CovertOpsStatus_EmptyLabel` with a space. Two details earned in the reading:
  `CovertActions_ScreenHeader` is declared on **both** `UICovertActions` and
  `UICovertActionReport` with different populated values, so the class qualifier is load-bearing;
  and the overdue branch (`Hours < 0`) draws a literal `"--"`, which a sighted eye reads as *no
  number* and speech would read as noise, so the time is dropped instead. It joins the CREW stop
  rather than getting one of its own — same kind of fact, same question. The fetch is
  deliberately **not** gated on the room having bottom-bar buttons: the Ring happens to have both,
  so the gate would work today and turn any future status-panel-without-a-button into silence.
  (`m_kResearchProgressBar` is the same shape and is still unread.)
- Probe insists the facility screen is TOPMOST, which keeps this recipe from attaching over the
  choose-research screen or the facility summary the room's own verbs push.
- ⚠ Known gap: `UIFacility_Infirmary` spawns its own `PatientList` (`UIList`), the only facility
  subclass that adds navigable content of its own; it is unread.

## Debris room

`UIRoom`, not `UIFacility`. An un-built room has no facility, so staffing is over
`Room.BuildSlots`.

- ⛔ **Vanilla gives this screen NO KEYBOARD AT ALL**, the clearest case in the project for a
  recipe rather than narration: `ExcavateButton` and `ExcavationPanel` both call
  `DisableNavigation()`; the staff slots move only on `FXS_DPAD_LEFT/RIGHT` (the ARROW constants
  are absent from that switch); `StartExcavation` is bound only to `FXS_BUTTON_X`; and
  `UIRoom.OnAccept` — what Enter and Space reach — is an **empty function body**. Clicking was
  the only way to clear a room.
- TWO SHAPES, one recipe: while the personnel picker is open the graph becomes candidate-only,
  because `UIStaffContainer.OnUnrealCommand` tests `m_kPersonnelDropDown.bIsVisible` before
  anything else — mirroring the game's routing rather than offering stops behind a modal.
- Activation is the game's own path throughout: the slot through its `HandleClick` (which
  branches to the facility upgrade when LOCKED), candidate and submenu through select-then-Enter
  on the LIST, Excavate through the button's `Click`. ⛔ Never `UIRoom.OnAccept` (empty), and
  never `StartClearRoom` on the room state — `StartExcavation` also closes the screen, and a room
  left open over a started project is a state the game never produces.
- Words come from the companion because the screen retains almost none: `ExcavationPanel` is a
  pure Flash pipe, `UIStaffSlot` keeps only `StaffSlotRef`, and the picker's rows call
  `AS_UpdateData` DIRECTLY — bypassing `UIButton.SetText`, so even the retained `Text` is empty.
  The status strings are the screen's OWN accessors (`GetExcavationStatusString`,
  `GetExcavationTimeEstimateString`, `GetExcavationRewardString`), which already branch on
  under-construction / clearing / staffed / no-engineers: this screen's whole state machine,
  expressed once, by the game.
- ⛔ **The room's NAME must NOT come from `UIAvengerHUD.FacilityHeader`.** `UIRoom` never writes
  that header — only `UIFacility.RealizeFacility` does — so it still holds the previous screen's
  title (a debris room announced itself as MISSION RESULTS). The companion names it off the room
  STATE using the grid overlay's own three branches (`GetSpecialFeatureLabel` /
  `GetClearingInProgressLabel` / the facility), so the room is called the same thing here as one
  screen up.
- **Getting there needs BUILD MODE**, which is why `ReadAvengerGate` tolerates exactly one screen
  above the grid: `UIBuildFacilities` ends its `OnUnrealCommand` by forwarding every unhandled
  key to the grid's own `Navigator.OnUnrealCommand`. Treating it as a blocker went silent exactly
  where a player is choosing a room to excavate.

## Choose facility

`UIChooseFacility`, the screen build mode's Enter opens and the only route by which a facility
ever gets onto the Avenger.

- ⛔ **It LOOKS like a commodity screen and is not one**: a `UIList` beside a panel of cost, power,
  upkeep, time, requirements and description — but it `extends UIScreen` rather than
  `UIInventory`, so `FindCommodityScreen`'s class-chain test never matches and the screen is
  SILENT.
- ⛔ **Its two halves fail in OPPOSITE directions, which is why it needs both readers.** The list
  is the good case and the first in this layer whose text native can simply read: `PopulateList`
  goes through `UIListItemString.SetText`, so names, `bDisabled` (`CanBuildFacility`) and the
  attention icon (`bPriority`) are retained. The PANEL is the bad case — `UpdateSelection`
  resolves every number into LOCALS and pushes them through
  `AS_SetDescription`/`AS_SetResources`, retaining nothing. Every value is re-derived by asking
  the SCREEN's own functions again (`GetStrategyCostString` with its own pending discount,
  `GetPowerRequirement`, which knows a shielded power coil doubles a generator and zeroes
  everything else, `GetStrategyReqString`, `GetBuildTime`, and the
  `CanBuildFacility`/`HasEnoughPower` warning branch in the game's own order) — never
  reimplemented.
- ⛔ It reads `m_selectedFacility`, not a re-index of `m_arrFacilities`: that field IS what
  `UpdateSelection` set and what `OnAccept` will build.
- ⚠ POWER is drawn as an icon plus a bare number with the SIGN carrying the meaning — positive
  produced, negative consumed, absolute value shown either way — so speech supplies the word the
  icon stood in for.
- Vanilla's keyboard was never the problem (Up/Down walk, Enter/Space accept, Escape cancels;
  Left/Right are deliberately swallowed) — the silence was. Enter goes to the **SCREEN** after the
  selection lands, because `OnAccept` reads the screen's own `SelectedIndex`, which only
  `UpdateSelection` writes and only `UIList::SetSelectedIndex` triggers. A disabled row is passed
  through rather than refused: `OnAccept` re-tests and plays its own refusal, keeping the refusal
  where the game can be right about why.

## Commodity list

The widest single recipe — `UIInventory` and everything derived from it is THIRTEEN screens with
one layout, a `UIList` of `UIInventory_ListItem`s beside a `UIItemCard`: choose research and the
archives, choose project, psi ability, view objectives, choose class, the officer school, both
resistance screens, the black market's buy side, build items, storage, implants, the XCOM
database and post-mission loot. (`UIBlackMarket_Sell` is NOT in the family despite the name — it
`extends UIScreen` and has its own card call.) **ONE OFFSET for thirteen screens**
(`AUIInventory::List`, 0x498).

- ⛔ **BOTH halves are Flash-only, and the second is the one that matters.** `UIItemCard` retains
  NOTHING — `PopulateData`/`PopulateCostData`/`PopulateResearchCostData`/
  `PopulateSimpleCommodityCostData` resolve title, description, cost, duration and requirements
  into LOCALS and push them through `MC.BeginFunctionOp`; its only retained vars are a
  `Texture2D` and two flags — AND it is in **no navigator**, so for a tech every fact that decides
  whether to research it is unreachable twice over. Meanwhile
  `UIInventory_ListItem.PopulateData` bypasses `UIListItemString.SetText`, so the retained `Text`
  is EMPTY on a row visibly showing a name. What the rows DO retain is the data they were built
  from — `ItemComodity` (the whole `Commodity` struct), `ItemTemplate`, `ItemRef`, `Quantity`,
  `XComDatabaseEntry` — so the companion re-derives every string from that, mirroring
  `UIInventory.SelectedItemChanged`'s branch and then the populator it reaches, populator for
  populator. Three details only the source gives up: `OrderHours == 0` means *instant* on the
  research card and *no time row at all* on the simple commodity card; a finished non-repeatable
  tech shows **no time**, so a researched tech must not keep advertising a duration; and `bTech`
  picks a DIFFERENT requirement formatter — `GetTechReqString` folds the cost's own science and
  engineering scores into the sentence and `GetStrategyReqString` never sees them.
- What native DOES read is the part it should — the row colour bits at 0x414, the whole
  affordability channel (`RealizeBadState` sets `bIsBad` from `CanAffordItem`,
  `RealizeDisabledState` sets `bDisabled` from `MeetsItemReqs`/`IsItemPurchased`, both refreshed
  on every `PopulateData`).
- ⛔ The list holds things that are NOT rows: `UIInventory_LootRecovered` interleaves
  `UIInventory_HeaderListItem` dividers, plain `UIPanel`s with no such bitfield — skipped by CLASS
  while keeping `i` as the index, so every remaining number equals the game's own.
- ⛔ **Enter goes to the SCREEN, not to the row**, and the counter-example proves it:
  `UIInventory_LootRecovered` overrides `OnUnrealCommand` to CLOSE on Enter and its rows buy
  nothing, so a row-targeted Enter would strand a blind player on the post-mission loot screen
  with no continue key; `UISimpleCommodityScreen` uses the same window to re-sync `iSelectedItem`,
  the number its own purchase branch reads. Everything else falls through
  `UIScreen.OnUnrealCommand` → `Navigator` → `ListContainer` → `List`, and `UIList` answers Enter
  by calling the SELECTED row's `OnUnrealCommand` first (`:515`) — so the row still gets it, by
  the game's own path, which is why selecting first is the argument rather than a courtesy.
- **Three stops**, in the screen's own order: the category strip (Build Items only — the clearest
  small case of *reachable but unspeakable*, since Left/Right already switch tabs while
  `CreateTabButton` calls `DisableNavigation()` on every button and `SetCategory` puts the new name
  only into Flash); the list; and the CARD as its own stop with **one node per LINE**. Separate,
  because a research list is fifteen entries and a card is a paragraph; split by line, because
  speech is serial and *what does it cost* should not require sitting through the write-up. Its
  nodes read at ANNOUNCE time with the build-time text as fallback: the card is repopulated the
  instant our write-back moves the selection (`List.OnSelectionChanged`) while the graph is up to
  one poll old, so a Tab hard on the heels of an arrow would otherwise describe the row just left.
- ⛔ **`EUIItemType` IS NOT THE ORDER THE CODE READS AS**: it declares `Misc=0, Weapon=1, Armor=2`,
  while `BuildScreen` CALLS `CreateTabButton` in weapon, armor, misc order and INDEXES
  `TabButtons` by the enum — so Flash slot 0 is Utility. Pairing index 0 with the weapon label
  shifts every caption AND every greyed flag by one and makes Enter switch to a different category
  from the one just spoken. `SwitchTab` and `SetTabHighlight` are expressed in the INDEX, so the
  index leads and the label follows — never the call order. A tab's post-activation line is read
  LIVE rather than from the render's closure: a captured flag can only describe the state before
  the press.

## Personnel roster

One recipe for every door the game opens when it asks *which person?* — `UIPersonnel` and its ten
subclasses: the Living Quarters' roster, the Bar's obituaries, the Armory's soldiers, staffing a
facility, choosing a scientist, a covert action's squad, the training centre, boosting a soldier.
The subclasses swap which array is filled and which tabs exist; layout, rows and input path are
all the base class's, so **ONE OFFSET** (`AUIPersonnel::m_kList`, 0x45C) covers the family.

**This is a READER, not a rescue** — `UIPersonnel.OnUnrealCommand` already walks the list on
Up/Down (through its `default:` branch into `m_kList.Navigator`), switches tabs on Left/Right,
accepts on Enter/Space and backs out on Escape. Three stops, in the screen's own order, each for
something the keyboard cannot reach: the TAB STRIP (`CreateTabButton` calls `DisableNavigation()`
and `SwitchTab` announces the change only into Flash); the SORT header; and the list.

- ⛔ **A row retains only its `UnitRef`.** `UIPersonnel_ListItem` extends `UIButton` and then
  `UpdateData` pushes name, status and location through `MC.BeginFunctionOp` — bypassing
  `UIButton.SetText`, so the retained `Text` is EMPTY on a row visibly showing a soldier's name
  (the house style for this game's list items). What it DOES keep is the reference it was built
  from, so the companion re-derives every column the way the row did, one branch per row class —
  `SoldierListItem` rank/name/class/status/time/location/mental state/promote,
  `DeceasedListItem` name/kills/missions/last operation/date, plain name/status/location for
  scientists and engineers.
- ⛔ **No `Caps()`**: every row caps the name before handing it to Flash, and that is a visual
  style, not information — a reader that spells out all-caps words turns every soldier into an
  acronym.
- ⛔ **It is a TABLE, and it is emitted as one** (`GraphSheet`). Column 0 is the NAME and carries
  every other cell as extra announcement parts, so walking DOWN reads the whole row exactly as a
  flat list would; stepping RIGHT lands on one fact at a time under the heading the game draws
  over it. Every column takes a heading the GAME ships (`m_strButtonLabels[]`, the same localized
  array the sort headers draw from) — and that constraint is what merges the soldier's TIME cell
  into STATUS: the row draws them stacked in one column and the only time heading the game has is
  the memorial's DATE, right for a date of death and wrong for "5 days until return". The mental
  state folds in there too (no heading exists, and `GetMentalStateStringsSeparate` overwrites the
  time pair anyway). Columns: rank/class/status/location for soldiers,
  kills/missions/operation/date for the memorial, status/location for scientists and engineers.
  The wire format for rows is POSITIONAL — `col=` per heading, `cell=` per value **including the
  empty ones** — because an empty cell is a real thing (a scientist has no rank) and dropping it
  shifts every column after it, the one mistake a table cannot survive.
- ⛔ **Focus STARTS on the list, not on the first stop.** `UpdateList` ends with
  `Navigator.SetSelected(m_kList)`, so the roster is where the game itself puts a sighted player —
  reading order and landing spot are different questions, and `GraphBuilder::SetStart` answers the
  second without inverting the first.
- ⛔ **Sorting is the one verb worth re-providing, and on mouse+keyboard it is CLICK-ONLY**: the
  keyboard branches are `FXS_BUTTON_X` and `FXS_DPAD_LEFT`/`RIGHT`, all controller constants, so
  the only vanilla route is clicking a `UIFlipSortButton`. On a roster of twenty the sort column
  IS the reading order. Unlike the rows, those headers RETAIN everything — `Label`, `Value`,
  `bArrowVisible` ("this is the sort column") and `bArrowFlipped` (its direction) — so only the
  two WORDS need deriving, because the game draws both facts as one triangle. Activation goes
  through the header's own `OnMouseEvent(FXS_L_MOUSE_UP)` rather than `SetSortType`, because that
  handler owns three things a poke drops: deselecting every other header, FLIPPING the direction
  when the column was already selected instead of resetting it, and calling `RefreshData()`.
- Tab switching goes through the screen's own `SwitchTab`, which re-points
  `m_kList.OnItemClicked` at the right selection delegate — assigning `m_eCurrentTab` directly
  leaves the PREVIOUS tab's delegate running against the new tab's list. Enter goes to the
  **SCREEN** after the selection lands: `OnAccept` → `OnButtonCallback` reads
  `m_kList.selectedIndex` and ignores everything else. A disabled row is refused rather than
  announced-and-tried, because every `OnXSelected` delegate opens by testing `IsDisabled`. The
  empty-list line is the game's own (`m_strEmptyListLabels[m_eCurrentTab]`, or the Bar's own
  `EmptyListMessage`, which it spawns a `UIText` for).
- ⛔ Words are cached on the row pointers but **NOT keyed on the selection**, unlike the commodity
  list's: nothing here is the selection's shadow, so keying on it would walk the whole roster
  through `XCOMHISTORY` once per arrow press for no new words.
