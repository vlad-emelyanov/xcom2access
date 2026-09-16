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
with it. (The HUD's panels around the grid are a separate, AMBIENT recipe — §Avenger HUD layer
below — which coexists with this precisely because it takes nothing until Tab is pressed.) The 51 DIAGONAL links are `FXS_VIRTUAL_LSTICK_DIAG_*` only and have no keyboard
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

## Resource strip

The band across the top of every strategy screen (`UIAvengerHUD.ResourceContainer`, a
`UIX2ResourceHeader`). Native-only: `game_ui_avenger.cpp` reads it, `Recipes::AddResourceStop`
turns it into a trailing Tab stop in six recipes, and `resource_strip.cpp` puts the whole strip
on `R`. No companion class — the words are the game's own, captured as they were drawn. Recon
and the full per-screen table: `../avenger-view-recon.md` §2.2.

- ⛔ **ONE widget, ONE owner, ONE reader.** `UIX2ResourceHeader` is named in exactly two files
  in all of `SrcOrig`. Every screen that shows resources shows the strip on `UIAvengerHUD`
  UNDERNEATH it, so a reader that went looking for a strip on the screen asking would find
  nothing — the same arrangement the room reader already follows for the facility header.
- ⛔ **It is not a fixed bar.** `UpdateResources()` shows a different subset per screen and
  hides it entirely on the twenty-odd classes that fall to its `default:`. Every consumer here
  therefore gates on `ResourceStripView::showing` and NEVER on "this screen is one that has
  resources" — a list the game owns and we would have to keep in sync. That gate is also what
  keeps the strip silent for the whole early tutorial (`ShowResources` no-ops until
  `T0_M4_ReturnToAvenger`) and while the Avenger is in flight.
  - ⚠ This is `bIsVisible` on a CHILD widget, which is the documented exception to CLAUDE.md's
    never-gate-on-`bIsVisible` rule — the game toggles it deliberately here, on every screen
    change, exactly as it does for a button it means to hide.
- ⛔ **The strip is painted by N pushes, not one — so the capture needed a BOUNDARY.** A consumer
  that names the call it wants gets the most recent batch under that name (`GetCall`). This
  screen pushes one `addResource` per resource, so "most recent" is one resource out of nine. `ClearResources` (`MC.FunctionVoid`, no operands)
  is retained as a delimiter specifically for this — see `IsRepaintDelimiter` — and the reader
  walks back to the LAST one. Without it the walk reads the previous screen's resources, which
  is not hypothetical: stepping from the ant farm's nine into the Power Generator's one leaves
  the eight before it looking current.
  - ⛔ `kMaxCallsPerPanel` is sized from this screen (24, against a worst case of 10). At the
    old value of 8 the delimiter and INCOME fell off the front silently — the tail-truncation
    failure mode, which reads to a player as *the game is not showing that*.
- ⛔ **Read the colour BEFORE cleaning.** The value arrives wrapped by
  `UIUtilities_Text.GetColoredText`, and the tint is the whole difference between "SUPPLIES 0"
  and "SUPPLIES 0, and that is why you cannot buy this". `CleanText` strips the `<font>` the
  colour lives in, so `FlashArg`'s cleaned answer is too late — the state comes off the raw
  operand, matched against `UIUtilities_Colors`' own constants, case-insensitively because the
  game's own table is not consistent about case (`5CD16C` beside `bf1e2e`).
- ⛔ **The stop is TRAILING and the key reads everything — that split is the design.** Attach
  lands on stop 0, so a resources stop built first would open every commodity list on the
  supplies figure instead of the screen's content. And the key is not redundant with the stop:
  the **ant farm has no recipe** (narrate-only, by the rule below) and shows the WIDEST strip in
  the game, so without the key the screen with the most resources to report would be the one
  screen that could not report them.
- ⚠ `R` is claimed only while the strip is showing, which is what keeps it away from tactical
  `R` (Reload, `DefaultInput.ini:354`, `[XComGame.XComTacticalInput]`) and from the save-name
  text field (`UISaveGame` hides the strip). The gate is the GAME's answer, not an exclusion
  list of ours. `[XComGame.XComHeadquartersInput]` binds `R` only with `Shift` held, and UE3
  requires a modifier a binding declares — so a bare `R` there displaces nothing.
- ⚠ **A showing strip with nothing captured is a real state**, not a fault: the game paints the
  strip only on a screen change, so a capture that acquired the panel after the last paint (or
  whose entry the LRU evicted) has nothing while the player can plainly see it. The tick asks
  the HUD for its OWN `UpdateResources()` at most once per 10 s to repair it, and the key says
  so meanwhile rather than falling silent.

## Avenger HUD layer

The ant farm's Tab layer: the mod's second AMBIENT recipe (`kAvengerHudRecipe`,
`recipe_avenger.cpp`), attached silently whenever `UIFacilityGrid` is the current screen and
focused by Tab. Five stops, information first and the verbs last: the resource
strip (`Recipes::AddResourceStop`), the objectives and dark events (the Ctrl+X composers), the
to-do strip, the event queue with the date, and — last, because its Enter leaves the screen and
Shift+Tab from the landing reaches it in one press — the row of category buttons plus the
hologlobe.
Readers in `game_ui_avenger.cpp` ("the rest of the HUD"); no companion class — three panels
retain their words in script, the strip and the date come through the capture.

- ⛔ **This does NOT revoke the ant farm's narrate-only ruling — it is how the two coexist.**
  The rule forbids taking the ARROWS (the room name is set under `if( bIsFocused )`, and the
  camera swoop and highlight box ride the game's own navigator). An ambient recipe takes
  nothing while unfocused; while focused the player is reading panels, not walking rooms, so
  nothing the rule protects is in play. `avenger.cpp`'s poll and its `Delete` key gate on
  `OwnsInput()`, never `IsAttached()` — the layer being attached is their normal state for as
  long as the player stands on the grid.
- ⛔ **Plain Tab is claimed here and is NOT free anywhere else** — measure before copying this.
  The bare binding (`[XComGame.XComHeadquartersInput]`, `DefaultInput.ini:468`) forwards to
  `FXS_KEY_TAB`, and on this screen every handler it reaches is dead in retail: `UIAvengerHUD
  .OnUnrealCommand`'s Tab case is inside `` `if(`notdefined(FINAL_RELEASE)) `` (it opened the
  personnel list in dev builds, beside `P`), `UIFacilityGrid` never handles Tab, and the
  shortcut bar cycles categories on the bumpers only. In tactical the same key is next unit;
  inside a room the shortcut list is a `UIList` with its own Tab. The claim is gated on the
  grid being current AND the layer not already focused, so it is disjoint from the navigator's
  own Tab (the stop cycle) by construction.
- ⛔ **Two ambient layers means two exit words.** `ExitAmbientFocus` used to say "Map"
  unconditionally; `Recipe::ambientExitName` now carries the word per recipe ("Rooms" here),
  and `OnLayerToggle`'s refusal names whichever layer the player is standing in via
  `Avenger::IsGridActive()`. Copy the field, not the tactical constant, for any third layer.
- **Every activation is the game's own path, called by name with the game's own parameters** —
  no field is written by hand. Categories: `UIAvengerShortcuts.SelectCategory(i)`, which IS
  the `2`-`6` digit key. Hologlobe: `UINavigationHelp.HotlinkToGeoscape()`, which IS digit
  `1`. A to-do message: `OnCategoryMouseEvent(icon, FXS_L_MOUSE_IN)` then
  `OnListItemCallback(List, i)` — the hover-then-click, with the game's own icon so it reads
  the category off `MCName` itself (writing `CurrentCategory` was the alternative and is the
  kind of poke the screen-accept rule warns about). The queue's expand button:
  `ToggleExpanded(none)`, which is R3's own call.
- ⚠ **The to-do strip is MOUSE-ONLY in vanilla** (hover reveals the list; L3 is
  controller-only). Reading it is parity with the hover, and the recipe exposes only
  categories whose icon the game is DRAWING (`Icon.bIsVisible`, toggled in
  `UpdateCategories`) with messages behind them — both tests, logged when they disagree. The
  label is the row a hovering player reads; the description is the tooltip, on Space.
- ⚠ **The event queue shows what the LIST holds, not what `GetEvents()` would return**:
  collapsed, that is one row (two with a covert action), and the expand button is the only way
  a sighted player sees the rest. Parity is the rows on screen plus that button, which the
  layer offers when the game draws it (`ExpandButton.bIsVisible`). The date and time are the
  one Flash push on this HUD (`RefreshDateTime`), read off the queue's cache index by name.
  - ⛔ **A button that relabels itself keeps ONE identity, and speaks the new face as
    `StateText`.** The expand/collapse button's label went into its node id on the first pass,
    on the theory that the differ would "land on the new button" — it does not. The old id
    vanishes, focus falls onto the neighbouring row, and the player hears a row where they
    pressed a button, which read live as *nothing happened* (the toggle HAD fired). Held id +
    a `StateText` that re-reads the label is the pattern for any control whose caption is its
    state.
- ⚠ Shortcut categories: `bAlert` is `FacilityState.NeedsAttention()` — the "!" badge — and a
  DISABLED category is offered and passed through, because `SelectCategory` tests
  `Button.IsDisabled` itself and moves the highlight without opening the room, exactly as the
  digit does. The Shadow Chamber's button is spawned hidden until the facility exists and gets
  no node until it shows.
- Urgency words: a to-do message's `EUIToDoMsgUrgency` maps to the SAME two words the room
  recipe uses for the shortcut submenu's `SetBad`/`SetWarning` (`RoomUrgent`/`RoomWarning`), so
  the one enum reads identically on both surfaces (the shared-oracle corollary).
- **Space, audited panel by panel (2026-09-03).** The strip, the objectives and the plain event
  rows register NO tooltip in the game, so Space is silent there — that is parity, not a gap.
  The three that do, and the three different mechanisms they use:
  - ⛔ **The category buttons carry a CALLBACK tooltip** (`SetTooltipWIthCallback("Init",
    RefreshTooltipX)`): the words are computed on hover — `TooltipResearch` with `%KEY:TWO%`
    resolved to the player's real binding — and a cold `ReadPanelTooltip` speaks the
    placeholder "Init". They go through `GameUI::ReadCallbackTooltip`, which runs the game's
    own hover sequence minus the showing: `UITextTooltip.MatchesID(id)` (selects the element on
    the shared tooltip AND copies its delegate), then the element's `del_OnMouseIn(Tooltip)`,
    then the read. ⛔ Select FIRST — the callback's `SetText` writes
    `TextTooltipData[DataIndex].sBody`, so without it the words land in whichever element the
    last hover left selected. Same mechanism ability icons use; reach for it before assuming a
    tooltip that reads "Init" is empty.
  - The hologlobe's tooltip is registered by FLASH PATH (`AddLeftHelp` → `GenerateTooltipPath`:
    `MCPath.leftBtnHelpContainer.buttonNavHelp_N`), so it is `ReadTooltipByPath` — and `N` is
    found by locating `HotlinkToGeoscape` in `m_arrButtonClickDelegates`, never guessed.
  - The covert-action event row registers one tooltip per STAFF slot (`MCPath.slotN`: the
    soldier's name, then the reward), probed by index and joined — the only place this HUD says
    who is on the action.

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
- ⛔ **A BUILT facility's staff slots are nodes, and for weeks they were not.** The recipe read
  `StaffSlots.Length` and built nothing from it, because no staffed room existed when it was
  written (§13.6 above) — so the debris room could be cleared by keyboard and the Power Relay
  built in it could not be staffed (reported 2026-09-03). The slots now sit in the **crew** stop
  under the "ENGINEERS: 2" line they are drawn under, and the picker is the debris room's:
  `X2AccessStaffSlots` emits `kind=slot` / `kind=candidate` for BOTH readers, and
  `StaffSlotVtable` / `BuildStaffPicker` in the recipe TU build both rooms' nodes. **One
  implementation** — a slot must read the same in a construction site and in the facility it
  becomes.
- ⛔ **Every slot state is ACTIVATABLE, because `UIStaffSlot.HandleClick` is the branch point**:
  a padlocked slot opens the upgrade screen that unlocks it, a greyed slot still opens its
  (empty) picker, and a FILLED project slot — the GTS trainee, a Psi Lab operative, a bonding
  pair, an Infirmary patient — raises the game's stop-training confirmation through the slot
  subclass's `ShowDropDown` override. Enter always goes to `HandleClick`; marking any state
  `Disabled` would take Enter away from a control the mouse can press. The vanilla keyboard
  route to the same call is **F2/F3/F4** (`UIFacilityStaffContainer.OnUnrealCommand`), the
  live keys `CLAUDE.md` warns about — unclaimed, so they still work beside our Enter.
- ⛔ **Container index is not facility index.** `UIFacilityStaffContainer.Refresh` skips a slot
  that is locked with no upgrade to buy and one whose template hides it, so `StaffSlots[i]` is
  not `Facility.GetStaffSlot(i)`. The companion iterates the WIDGETS and follows each one's own
  `StaffSlotRef`; native matches the record's index against the widget array it read. Nothing
  indexes the facility's slots by position.
- **The slot's words are the state object's, the tooltip is re-run, never read.** Name and bonus
  come from `GetNameDisplayString` / `GetBonusDisplayString` (what `UpdateData` pushed and
  dropped). The hover tooltip — *"Upgrade the facility to unlock the staff slot"*, *"No
  engineers available"* — is `UIFacility_StaffSlot.RefreshTooltip` re-run branch for branch,
  because the game writes it only on MOUSE-IN, so the tooltip manager's stored text for a slot
  nobody hovered is the class default whatever the slot's state. A locked or unfillable slot
  speaks it as the Reason; a fillable one gets it on Space. ⚠ `UIFacility_BondSlot` overrides
  the whole function and its `m_strNoSoldiersTooltip` is a different sentence, so the bond cast
  is tested FIRST.
- **Words re-ask on every render while the picker is open** (the debris room's rule, now the
  facility's too): `RealizeStaffSlots` calls `UpdateData()` on the widgets it already has, so
  an assignment changes the occupant with no pointer change, and the project slots close the
  picker BEFORE the dialog that commits the choice. The picker's pointer is in the cache key as
  well, so its opening and closing each refetch.
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

## Facility upgrades

`UIChooseUpgrade` — behind the room's UPGRADE / REVIEW UPGRADES button, a padlocked staff slot,
the to-do widget's nudge and the ant farm overlay's upgrade path.

- ⛔ **The screen is `UIChooseUpgrade`, not `UIFacilityUpgrade`.** Every one of those paths calls
  `XComHQPresentationLayer.UIFacilityUpgrade()`, and that function spawns a `UIChooseUpgrade`
  (`XComHQPresentationLayer.uc:1994`). `UIFacilityUpgrade` ships in the package — with a list
  of `UIFacilityUpgrade_ListItem`s whose `UIText` IS retained, which is what made the census
  file it as the natively readable one — and is never instantiated. A recipe probing for it
  would never attach. **Check what the presentation layer SPAWNS before building for a class
  name** (`CLAUDE.md`, adding a screen recipe).
- **It is `UIChooseFacility`'s twin, and built as one**: `m_List` at the same offset (pinned on
  its OWN class — sharing 0x418 is declaration-order luck, not a contract), `UIListItemString`
  rows populated through `SetText` (names, `bDisabled`, the attention icon all retained),
  `OnAccept` reading the screen's own `SelectedIndex` so Enter goes to the SCREEN after the
  selection lands, and a panel `UpdateSelection` resolves into locals and pushes through
  `AS_SetDescription` / `AS_SetResources`. One `BuildChoiceScreen` serves both; only the finder,
  the pin, the companion class and the words differ.
- **The words are the screen's own functions again**: `GetStrategyCostString` with the screen's
  scalars and discount (⚠ `UpdateSelection` draws with `FacilityBuildCostScalars` while
  `CanBuildUpgrade` tests `FacilityUpgradeCostScalars` — the DRAWN number is the parity target,
  the test is the game's to make on Enter), `GetPowerRequirement`, `GetStrategyReqString`,
  `HasEnoughPower`, `UpgradeAlreadyPerformed`, `CanBuildUpgrade`. No time line: an upgrade is
  installed in the same game state that pays for it.
- **A greyed row here can mean two things** — cannot afford, or already installed — and the
  panel says neither in words. The card gets a COMPLETED line for the second, in the game's own
  word: `UIFacilityUpgrade_ListItem.m_strCompleted` ships populated under its own loc section
  (INT and RUS checked) even though the class is never spawned. Derivable from what is on
  screen (the room's button says REVIEW UPGRADES, the upgrade is visibly built), so it hands a
  blind player nothing a sighted one lacks.
- ⚠ `m_strTitle` ships as `UPGRADE <XGParam:StrValue0/!UpgradeName/>` and the screen hands it
  to `UIX2PanelHeader.SetText` UNEXPANDED — no `ExpandString` anywhere on the path — so Flash
  draws whatever it makes of a stray tag. The spoken title expands it with the facility's own
  `DisplayName`, which is what the one shipped use of the same key (`UIFacilityUpgrade
  .InitScreen`) fills in.

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
- ⛔ **Two screens have a row that draws more than a name, and hide the card.** `UIChoosePsiAbility`
  and `UIChooseClass` spawn `UIInventory_ClassListItem`, whose `PopulateData` pushes icon, title,
  training time AND the whole description into Flash, and both call `ItemCard.Hide()` at init —
  so the card record is correctly absent and the row is the only place those facts exist. A
  player picked a psi ability by name alone until the row record grew `time` (on the line, the
  right-hand column) and `desc` (behind Space, where every other Avenger description lives).
  The test that would have caught it: a row class the family's base never spawns.
- ⛔ **The same two screens have a DEAD keyboard Enter in vanilla, and the fix is gated on the
  game's answer, not on class.** Their `InitScreen` calls `Navigator.SetSelected(List)` on the
  SCREEN's navigator, whose navigables are the header and `ListContainer` — `UINavigator.SetSelected`
  answers an unknown control with `SelectedIndex = -1`, so the screen's Enter reaches
  `GetSelected() == none` and falls through (live: `-1` with the list selected and focused). The
  controller survives through the `ISCONTROLLERACTIVE`-gated `FXS_BUTTON_A` hatch in
  `UISimpleCommodityScreen.OnUnrealCommand`; the keyboard has none. `ActivateCommodityRow` reads
  `OnUnrealCommand`'s own `bHandled` and, only when the screen says no, resumes the routing one
  hop below the break at the LIST — so LootRecovered's close and every purchase that went through
  never see a second Enter. There is no separate stop for the row's green confirm button: it is
  the selected row's own action, and Enter on the row is what presses it.
- What native DOES read is the part it should — the row colour bits at 0x414, the whole
  affordability channel (`RealizeBadState` sets `bIsBad` from `CanAffordItem`,
  `RealizeDisabledState` sets `bDisabled` from `MeetsItemReqs`/`IsItemPurchased`, both refreshed
  on every `PopulateData`).
- ⛔ **The row's red says THAT it cannot be built; the card's red says WHICH cost item is short,
  and nothing on the screen says it in words.** `GetStrategyCostString` wraps every item the HQ
  cannot pay in `BAD_HTML_COLOR` and every other in `GOOD`, and `CleanText` strips the `<font>` on
  the way to speech — so a Predator Armor short of six Trooper corpses read *"6 ADVENT Trooper
  corpses, 150 Supplies, 20 Alien Alloys"* under a row saying *cannot afford*, which is the
  sighted screen with the one fact that matters removed (screenshot, 2026-09-16). The companion
  reads the red runs back off the string the game drew, by the game's own constant (the card's
  `short` field), and the recipe speaks them as their own line after the full cost, trailed by
  `Vocab::CommodityShortfall`. Same rule as the resources strip above: **read the colour before
  cleaning** — and re-asking `CanAffordResourceCost` would have been a second opinion where the
  card already holds the first.
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
- ⛔ **The NAME cell is `eNameType_Full` plus a separate nickname — never `eNameType_FullNick`**,
  which prefixes `GET_RANK_ABBRV` and so said the rank twice against the RANK column beside it
  (reported live 2026-09-04; the general engine fact is in `../engine-notes.md`). The row itself
  settles it: `AS_UpdateDataSoldier` is handed the full name, the nickname and the rank abbreviation
  as three fields, so the rank's place is its own column. ⚠ The MEMORIAL row is the deliberate
  opposite — the game hands IT `FullNick` and draws no rank column, so `DeceasedCells` keeps it.
  The nickname travels as its own wire field rather than pre-joined, because `eNameType_Nick`
  answers a lone SPACE when there is none and only native's clean-then-join order makes that vanish
  without leaving its separator behind.
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

## Black market

Three screens behind the geoscape POI. The BUY side (`UIBlackMarket_Buy`) is a
`UISimpleCommodityScreen` and rides the commodity recipe; the ENTRY MENU (`UIBlackMarket`) and the
TRADING POST (`UIBlackMarket_Sell`) are their own recipes — `black market` and `black market sell`
— sharing one companion class, `X2AccessBlackMarketReader`. Built 2026-09-03 straight from source
(recon `../avenger-view-recon.md` §19); **not yet run live**, so every ⛔ below is a reading of the
game's code rather than a bug earned, and the first pass (`../next-steps.md`) is what promotes or
demotes each.

- ⛔ **The entry menu is mouse-only in vanilla, and the fix is `Click()`.** `BuildScreen` calls
  `Navigator.Clear()`, and `OnUnrealCommand`'s only accept branches are `FXS_BUTTON_A` and
  `FXS_BUTTON_X` — controller constants — so a keyboard player can open the shop and never buy or
  sell. The two buttons are activated through `UIButton.Click`, which runs the screen's own
  `OnBuyClicked` / `OnSellClicked` guards (`HasItemsForSale`, `PlayerHasItemsToSell`) and refuses a
  disabled button itself. Nothing goes in `gFocus`: there is no navigator to hold it.
- **The greyed reason is the button's TOOLTIP.** `RefreshButtons` calls
  `SetDisabled(true, m_strEmptyThisMonth)` / `SetDisabled(true, m_strEmptyInventory)`, and
  `UIButton.SetDisabled` parks the string in the tooltip manager and removes it again on
  re-enable — so `ReadPanelTooltip` on a disabled button IS the reason, read only while
  `IsDisabled` is set.
- **One mechanism per fact, chosen by what only it can do.** The welcome line is
  `m_strWelcome[Rand(7)]` pushed through `LibraryPanel.SetMenuQuote` with the index stored nowhere,
  so it is the Flash capture's and best effort: evicted, the recipe has no quote node and says
  nothing about it. The interest sentence is real information (which items sell at a premium), so
  the companion re-runs the screen's own `GetInterestsString()` rather than trusting the capture;
  the caption (`m_strInterestTitle`) and the button labels are native FString reads.
- ⛔ **The trading post is a TABLE and is emitted as one** (`GraphSheet`, like the roster) — it
  draws headings over its columns. Column 0 is the item name carrying the cells as
  HEADING-PREFIXED parts ("QTY 3, COST §20"), unlike the roster's bare cells, because a number
  under a heading the player cannot see means nothing. ⚠ The heading-to-column pairing — QTY over
  the owned count, COST over the unit value, SELL over the number selling, TOTAL over the line
  total — is read off `UpdateSellInfo`'s push order and the loc strings, NOT off the Flash layout,
  and is the first thing the live pass checks against a screenshot.
- ⛔ **The quantity verb is the kernel's ADJUST on `+`/`-`, not Left/Right.** The game's own keys
  for it are the row's `FXS_ARROW_LEFT`/`_RIGHT`, but inside a sheet the arrows are column travel
  and cannot be both. Every cell of a row carries `OnAdjust`, which sends the ROW its own arrow
  command (`SendRowCommand`) — the click sound, `ChangeQuantity`'s clamp to what the player owns,
  `UpdateTotalValue` and the card refresh come with it — and `StateText` re-reads the row's
  numbers: "SELL 2, TOTAL §40". The claim is general (`OnAdjustKey` → `TryAdjust`,
  `../input-map.md`), so the same keys adjust a slider or a spinner on any screen.
- **Numbers native, words companion, and the payload is independent of the numbers.** A row
  retains `NumSelling`, `Price` and `ItemRef` and pushes everything else into Flash. The companion
  carries the name, the RAW owned count (`Item.Quantity`, not the `Quantity - NumSelling` the row
  draws) and the interest flag; native subtracts the live `NumSelling` and multiplies the total, so
  a `+` press changes no word the companion supplied and re-asks nothing. The card is keyed on the
  selection (it is the selection's shadow — `List.OnSelectionChanged = SelectedItemChanged`), so an
  arrow press is one outbound call, as on the commodity list.
- ⛔ **Enter anywhere is the SCREEN's Enter.** `UIBlackMarket_Sell.OnUnrealCommand` answers
  `FXS_KEY_ENTER` with `OnConfirmButtonClicked` ahead of its navigator — a sighted keyboard player
  sells from any row — so the rows and the CONFIRM node all route to `SendScreenCommand(Enter)`,
  never to `Click()`: the screen's handler answers a greyed button (nothing marked) with the game's
  refusal sound, where `Click()` would answer it with nothing. A sale respawns every row, so the
  verb invalidates the wire cache (the address-reuse hazard).
- **The screen has no title.** The loc file ships `[UIBlackMarket_Sell] m_strTitle=TRADING POST`,
  but the class declares no such var, so that string exists nowhere at runtime; the recipe is named
  by the entry menu's own SELL INVENTORY label (`class'UIBlackMarket'.default.m_strSell`), the
  button the player pressed to get here.
- **A row with nothing marked draws "-" and a blank.** The SELL cell speaks the number (0) and the
  TOTAL cell is SPARSE — unlandable, column number kept, so Up/Down still match the TOTAL column
  across rows. The flat reading omits both until something is marked, as the row does.

## Recruits

The Armory's RECRUIT door (`UIRecruitSoldiers`, spawned by `UIFacility_Armory.Recruit()` alone).
The `recruits` recipe with `X2AccessRecruitReader`. Built 2026-09-03 from source
(`../recruit-recap-recon.md` §1); **not yet run live**, so each ⛔ is a reading of the code and the
first pass promotes or demotes it.

- ⛔ **A row is joined to its recruit by POSITION, and that is the game's own guarantee.**
  `UIRecruitmentListItem` retains no name, no `metadataInt`, nothing — only `bDisabled`. The
  screen rebuilds `m_arrRecruits` and the list in ONE loop of `UpdateData`, so `m_arrRecruits[i]`
  is row i; the companion walks the array by the list's index and native stamps the same index.
  A join on anything else would be a guess.
- ⛔ **Enter is the SCREEN's Enter, selection first.** `OnUnrealCommand` calls
  `OnRecruitSelected(List, List.SelectedIndex)` and ignores the widget the key came through; the
  row's own RECRUIT button is `DisableNavigation()`ed. The purchase is immediate — no confirm —
  and respawns every row, so the verb invalidates the wire cache (the address-reuse hazard).
- **A greyed row keeps its activation.** `InitRecruitItem`'s `SetDisabled` means the supplies were
  short at build time; the game still takes Enter there and answers with its own NOT ENOUGH
  SUPPLIES dialog, which the dialog recipe reads. Spoken as `unavailable` + `cannot afford`, the
  commodity list's wording for the same colour.
- **The panel is the selection's shadow** (`OnRecruitChanged` repaints it on every move), so the
  wire cache is keyed on the selection and the details nodes read at announce time, exactly as the
  commodity card does. The cost is GLOBAL (`GetRecruitSupplyCost`); the time line is
  `m_strInstant` unconditionally, which is what the screen draws whatever `RecruitOrderTime` is.
- The flag beside a name is spoken as the country's `DisplayName` — the icon's meaning, not its
  path.

## Rewards recap

The post-mission council letter (`UIRewardsRecap`) — the tail of the loot → research-available →
recap chain, NOT the monthly report's (`../recruit-recap-recon.md` §2.1). The `rewards recap`
recipe with `X2AccessRecapReader`. Built 2026-09-03 from source; **not yet run live**.

- ⛔ **No widget on the screen holds a word.** The class declares no list, no text, no button;
  `BuildScreen` pushes locals into Flash. Every line comes from the screen's own PUBLIC accessors —
  `GetOpNameString`, `GetOpLocationString`, `GetOpSummaryString`, `GetRewardStrings` — re-run on
  the live instance by the companion; native supplies the finder only.
- ⛔ **Read before Continue, never after.** `OnContinue` → `ExitPostMissionSequence` →
  `ClearRewardsRecapData` empties the reward and effect arrays and clears `MissionRef`. The
  companion returns empty once either is gone, and the wire cache keys on the screen alone so
  nothing re-asks on the way out.
- **The outcome is colour alone on this screen** (the name is green or red), so the companion
  borrows `UIMissionSummary`'s `m_strMissionComplete` / `m_strMissionFailed` — the words the
  player heard one screen earlier — keyed on `WasMissionSuccessful()`.
- **Rewards and effects are one node each**, where the screen compacts the rewards into slots and
  fuses the effects into one space-joined string. The reward index carried is the SLOT (empties
  skipped, as `BuildScreen` skips them); the effects come off the plain `RecapGlobalEffectsGood` /
  `…Bad` arrays, good first. The captions ride the header only when the screen would draw them.
- **Every key is Continue** (A, Enter, Escape, B, right-click; Space is swallowed; Tab only outside
  `FINAL_RELEASE`), so the one button is the whole action list and Escape is left to the game.
