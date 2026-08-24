# Strategy and transient surfaces

Implementation rules for the globe, the screens between missions, and everything that appears then vanishes. Each ⛔ was earned by a live bug; each ⚠ is a
known limit or an open decision. **Read the section for a surface before editing
it** — `CLAUDE.md`'s Surfaces table points here for exactly that reason.

Cross-cutting rules (buffer sizing, the screen-accept trap, the empty-field trap,
speech joining, parity) live in `CLAUDE.md`; only what is specific to these
screens is here.

## Geoscape tray

The globe's pins are not the ant farm's one-call ungate — selection is per-frame and
`ISCONTROLLERACTIVE`-gated — but the TRAY along the bottom already answers the arrows on
mouse+keyboard, and what the mouse-active guard withholds is only the visible half:
`MissionIcon.OnReceiveFocus` early-returns, so there are no brackets, no camera fly-to and no
tooltip refresh. The recipe supplies those three and the voice; every word but one is the game's
own hover tooltip.

- ⛔ **Hover is moved as a symmetric IN/OUT pair**, because IN saves the camera location and OUT
  restores it — an unmatched IN overwrites the saved location with a site the player never asked
  to return to.
- ⛔ **Focus write-back is THREE calls, not two, and the missing one cost the layer its only verb.**
  `UINavigator.SetSelected(Control)` does NOT fire `OnSelectedIndexChanged` (`UINavigator.uc:677` —
  it assigns `SelectedIndex`, calls `OnReceiveFocus()`, returns); `Next()`/`Prev()`/
  `SelectFirstAvailable*` do. So claiming Left/Right replaced the game's only route into
  `UIStrategyMap.OnNavigationChanged`, and `SelectedMapItem` froze on whatever `OnInit`'s one-shot
  `OnNavigationChanged(0)` had left there — tray icon 0, for the whole session. `Enter` acts on
  `SelectedMapItem` and not on the icon it is handed, so it flew nowhere; and when icon 0 happened
  to be the Haven the Avenger was parked on, the screen's Haven case answered `ToggleScan()` — the
  player pressed *fly here* and heard the scan start, indistinguishable from `Space`. The fix is
  `SetSelectedMapItem(pin, NavigationSelect: true)` called directly, NOT the delegate: the delegate
  drags in an icon-focus loop that is the game's other route into the camera save/restore pair the
  hover already owns, and it carries a `CanBeScanned()` filter that would leave the selection on
  the previous pin.
- ⛔ **Activation is the mouse's own click on the ICON — `OnMouseEvent(FXS_L_MOUSE_UP_DELAYED)`,
  400, never 391.** `OnClickedDelegate` was bound by `SetScanSite`/`SetMissionSite` to that icon's
  own site, so the target is decided by which icon gets the event and by nothing else. The
  screen-level `FXS_BUTTON_A` route is the one that reads `SelectedMapItem` and ignores what it is
  handed — the screen-accept trap in `CLAUDE.md` — and guarding that dependency is worse than not
  having it. ⚠ The icon's OWN `FXS_BUTTON_A` handler (`UIStrategyMap_MissionIcon.uc:228`) is a trap
  too: its else branch sends 391, which `UIIcon.OnMouseEvent` has no case for, so it would give you
  travel that works only on the site you are already parked on.
- Claiming the arrows costs nothing back (Ctrl+arrow still pans, by the leniency rule) and buys
  correctness: panning scrolls a pin off screen, `UpdateVisuals` gates on `bOnScreen`, and the
  reader would otherwise describe class defaults.
- Beside it a ring on its own keys — PageUp/Dn, Ctrl+them for the category, `O`, `Home`, `C`, `/`
  — an AID rather than parity, because it reads the fourteen regions the tray never holds.

## Region actions — make contact, install a radio relay

The geoscape's second Tab stop (`BuildRegionActions`, fed by
`X2AccessGeoScanner.DescribeRegionActions`). It lists every region whose pin is CURRENTLY drawing
one of its two hotlink buttons, and Enter fires that button's own handler.

⛔ **This is the whole resistance economy, and vanilla gives the keyboard no route to ANY of it.**
`UIStrategyMapItem_Region` spawns `ContactButton` (`'contactButtonMC'`, captioned
`m_strButtonMakeContact`) and `OutpostButton` (`'towerButtonMC'` — captioned by NOTHING:
`InitButton` is called with no label and the radio-tower ART is the whole caption). Four dead ends
leave them mouse-and-controller-only, and each had to be checked before building anything:

- the pin's own `OnUnrealCommand` answers `FXS_BUTTON_A` and nothing else (`:657`), a controller
  constant no key on a mouse+keyboard setup produces;
- the screen's `Enter`/`Space` go to `ToggleScan` (`UIStrategyMap:1663`) and never reach the pin;
- the screen-level pair that would have fixed it — `UIStrategyMap`'s own `OutpostButton` /
  `ContactButton`, wired to `OnBuildOutpostClicked` / `OnMakeContactClicked` — is **commented out**
  (`UIStrategyMap.uc:112-116`);
- `UIResistance`, the screen `DisplaySelectionPrompt` was once meant to open for this, is dead
  code: its only call site is commented out (`XComGameState_WorldRegion.uc:1313`).

⛔ **The TRAY does not cover this and cannot.** A region enters `GetAvailableScanningSites()` only
once `CanBeScanned()` is true, which for a region means `bCanScanForContact` / `bCanScanForOutpost`
— flags set by the very callback this stop exists to reach. The tray holds regions whose verb has
ALREADY been pressed and paid for.

- ⛔ **The gate is `ShowContactButton()` / `ShowOutpostButton()` mirrored exactly, and nothing is
  added to it.** Not affordability, not comms capacity, not "is the pin on screen". The game draws
  both buttons whatever you can pay and explains itself only inside the alert; refusing the stop
  would hand a blind player less than the globe shows AND hide the explanation with it. The two
  refusal reasons are ANNOUNCED (`cannot afford`, `no comms capacity` — the second is
  `UIAlert.CanMakeContact()`, `GetRemainingContactCapacity() > 0`) and the node stays activatable.
- ✅ **This is the one activation on this screen that does not touch `SelectedMapItem`.** Both
  entry points take the region as a PARAMETER (`OnContactClicked`/`OnOutpostClicked` are one line
  each, passing `GetRegion()`), so the screen-accept trap that cost the tray's `Enter` two sessions
  cannot arise here — there is no selection for our idea of the target to disagree with.
- ⛔ **The verb is re-checked at activation, by (region, verb).** The geoscape clock keeps running
  under this stop, so a contact scan can complete between the render that spoke a line and the
  `Enter` that acts on it — moving the region from `Unlocked` to `Contact` and turning *make
  contact* into *install a relay* under the player's finger. Re-testing makes that a silent no-op.
- **Caching a payload that is game STATE, not widgets**: `gGeoActions` keys on the screen plus the
  tray handles, which cannot see a resistance level change. It is still correct because every
  transition that changes the set pops a MODAL — `eAlert_ContactMade`, `eAlert_OutpostBuilt`,
  `UnlockPopup`, the `ResistanceRadio` tech alert, and our own confirm alert — and a modal detaches
  the recipe, which invalidates every wire cache. ⚠ A console command is the one case it misses.
- Words are the game's throughout: the caption (the relay button has none, so it borrows
  `UIAlert.m_strOutpostConfirm` — the words on the confirm the player meets one keypress later),
  the `Cost`/`Time` labels the alert uses, `GetStrategyCostString`, and the `+N supplies` figure
  `UpdateFlyoverText` puts beside the button as `HoverInfo`. ⚠ Day words come from
  `UIUtilities_Text.GetDaysString`, NOT the alert's own `m_strDays` — that one is declared on
  `UIX2SimpleScreen` and populated under `[UIX2SimpleScreen]`, so reading it off `UIAlert` is the
  localized-var trap in `CLAUDE.md`.

The ring's region readout gained the matching clause (`radio relay available`, and the game's own
`BUILDING RADIO RELAY: N Days remaining` while one is in progress), and the continents category
gained `regions contacted N of M, radio relays N of M` — `SetContinentLevel`'s own four numbers,
which are the only place the globe says how far a continent is from its bonus.

## Skyranger briefing

Not a movie and not a progress dialog but a real `UIScreen` on the transition map, where the
presentation layer is destroyed and rebuilt — which is why "did we find it" earns its own
`/status` line rather than being left to the stack dump. The one edge that matters, the map
finishing loading, is published by the screen's OWN 1 s timer, so the 250 ms poll resolves it four
times finer than the game does.

⛔ `gAnnounced` is a POINTER, not a bool: mission-start and mission-end briefings are different
objects, so a changed pointer is exactly "a new briefing to read".

## Modals, alerts, and the transient family

One recipe for every modal notification in the game — `UIAlert`'s 118 variants, the bespoke
faction/bond/Chosen/UFO popups, the blocking progress dialog — which look nothing alike and share
exactly what a reader needs: topmost on the stack, Enter and Escape answered at SCREEN level. Two
stops, and the message is one node per LINE, because a Chosen reveal is several paragraphs and
speech is serial.

- ⛔ **Activation is the screen's own `OnUnrealCommand` and never a button `Click`**: the buttons
  are spawned with empty labels and get their text through Flash, and several alert names branch
  before dispatching (`eAlert_Contact` refuses when contact is unaffordable,
  `eAlert_TrainingComplete` routes Enter to cancel), so sending the key keeps every branch a poke
  would lose.
- ⛔ **`UIAlert`'s WORDS come from the Flash command-queue capture** (`src/flash_shadow.cpp`), not
  from the screen and not from a Flash path read. All 106 `Build*Alert` functions push their text
  through `LibraryPanel.MC.BeginFunctionOp("Update…")` + `QueueString` and retain none of it, so
  the capture — which records that argument list in flight — is the only reading that is both
  complete and in display order. The two older paths remain as fallbacks in trustworthiness order:
  the screen's `m_str<Stem><Suffix>` localized vars (right for the 81 of 118 names that follow the
  convention, but blind to which suffixes the panel actually SHOWS, so it over-reads), then the
  per-symbol Flash field sweep (blind to any `XComScrollingTextField`, whose text sits behind an
  AS2 getter `GetVariableString` cannot run). `just dev-flashqueue` prints what was captured.
  - The captured argument list is NOT all speech: `BuildGOpsAlert` queues title, subtitle, body,
    **image path**, confirm, ignore. Two filters clean it, and both are exact rather than clever
    where they can be — **button labels are matched out by string equality** against the labels
    already read off `ButtonGroup.Button0/1` (which is why the buttons are read FIRST), and asset
    references are dropped by a deliberately narrow test (no whitespace at all, plus a URL scheme
    or a `Package.Asset` dot in the ASCII path alphabet — so a one-word title survives, and so does
    any title in a non-Latin script). **Every asset drop is logged**, so a live run enumerates what
    the filter rejected instead of a line going silently missing.
- The transient half is the unglamorous file with the highest value in it: `UINarrativeCommLink`
  is how essentially ALL story dialogue in XCOM 2 is delivered, and without it a blind player gets
  the voice acting and nothing else — nothing at all in a language whose VO they do not have.
  Toasts and the strategy help blade ride along. **No graph recipe**: a tab stop over a line about
  to vanish is worse than useless, and attaching would take the arrows from the screen the player
  is working on.
- ⛔ **Change detection is deliberately NOT in native** — all three surfaces re-render continuously
  (the commlink retypes its subtitle as the line scrolls), so "what is on screen" reads true for
  hundreds of consecutive polls and only the model can turn that level into an edge.
- ⛔ **The commlink obeys the game's SUBTITLE SETTING, because our speech IS the subtitle.** With
  `Interface → Show Subtitles` cleared, `UINarrativeCommLink.RefreshSubtitleVisibility` calls
  `AS_HideSubtitles` and the text block leaves the screen while the portrait and speaker name stay;
  reading the line anyway would make the one option in the game that means *stop showing me the
  words* the one option the mod ignores. `PollCommLink` tests the same flag the widget tests —
  `` `XENGINE.bSubtitlesEnabled ``, the ENGINE's copy, not `` `XPROFILESETTINGS.Data.m_bSubtitles ``
  where the checkbox writes: `XComOnlineProfileSettings.ApplyOptions` copies one to the other, so
  the profile value can be a toggle ahead of what is actually being drawn, and the drawn one is
  what parity is defined against. The last-spoken marker is cleared on the way out so re-enabling
  mid-conversation still announces the line in progress. ⚠ Two of the game's own paths already
  interact with this and neither replaces the gate: an `AkEvent` with `HideSpeaker` hides the whole
  commlink when subtitles are off (`UINarrativeMgr:1417` — caught by the existing `bIsVisible`
  test), and `EndCurrentConversation` is delayed 0.75-1.5 s only while subtitles are ON, so the
  widget vanishes a beat sooner with them off.
- **Tick order is load-bearing in one place**: `Dropship` runs after `TransientReader`, so the
  arrival block queues behind Central rather than over him.

## Movie subtitles (`src/movie_subtitles.cpp`)

The OTHER subtitle system, and nothing else in this project touches it: the text the engine draws
over a full-screen `.bk2` — the WOTC pre-intro, the Chosen strongholds, the XP autopsies, the final
movie. The base game's tentpole cinematics are NOT this; they are in-engine matinees over which
`UINarrativeMgr` deliberately keeps the commlink alive (`HideUIForCinematics` spares
`UINarrativeCommLink` and `UIStrategyTutorialBox`), so the transient reader above has them. The two
can never both be showing, which is why their lines cannot collide.

- **The pipeline**: `XComGame/Movies/<name>.txt` is a cue track (`startMs endMs CueKey`) that the
  Bink player loads beside the video; each cue resolves through `Localize("Subtitles", <CueKey>,
  "Subtitles")` and is drawn by `XComEngine::RenderExtraSubtitles`. **No `UIScreen`, no Flash, no
  script-visible property anywhere on it** — which is why this surface reads a raw C++ object,
  the only place in the mod that does.
- ⛔ **There are TWO `Subtitles` loc files and only one is the right one.**
  `Engine/Localization/<lang>/Subtitles.<lang>` is 72-76 bytes and holds `MovieIsPaused` alone; the
  cue text is in **`XComGame/Localization/<lang>/Subtitles.<lang>`**, 167 KB of `CueKey="line"`
  under `[Subtitles]`. Reading only the engine copy says the game ships no movie subtitles at all,
  which is wrong and was believed here for one session.
- ⛔ **Read what the game is SHOWING; do not rebuild it.** Both halves are plain files on disk and
  a private parser would have been half the code — and a second subtitle system agreeing with the
  game only by luck: it would keep talking after the player turned subtitles off, guess a language
  instead of using the one the engine resolved, drift the moment a mod replaced a track, and miss
  the loop counter that says which pass through a looping movie is running. So the cue table, the
  frame, the option and the text are all read out of the live player. The only thing reproduced is
  the interval scan that picks a cue for a time.
- ⛔ **One hard-coded address, and it is validated before anything is dereferenced through it.**
  `GFullScreenMovie` is at RVA `0x1DB8900`; the object's vtable must be RVA `0x13E9B28`, the one
  `FFullScreenMovieBink`'s constructor writes. A mismatch disables the surface permanently with one
  `LOG_ERROR`. **That check IS the pin** — none of these offsets can be `static_assert`ed, because
  the type is not a UObject and appears in no SDK dump. Provenance for re-derivation: `FUN_14024c630`
  is the lazy creator (names the global and the constructor), `FUN_1402073b0` the constructor
  (writes the vtable, allocates the cue track into `+0x114`), `FUN_14023d7a0` `PlayMovie` (movie
  name `+0x40`, `BINKHANDLE` `+0x84`), `FUN_14023ed10` `PumpMovie` (the time arithmetic, transcribed
  line for line), `FUN_14034db80` the cue lookup.
- **The option gate is the game's own test, not a policy of ours**: `PumpMovie` blanks the string
  it just fetched unless `UEngine.bSubtitlesEnabled` is set OR the movie carries flag `0x80` on
  `+0x60` (which is how a legal screen stays readable). Both are mirrored. `GameUI::SubtitlesEnabled`
  reads the ENGINE's copy for the same reason the commlink does — the profile can be a toggle ahead.
- **A missing localization is logged, not spoken.** The engine falls back to drawing the raw cue key;
  we do not say "CIN_XP_PreIntro_01" out loud, because an internal name tells a blind player exactly
  as much as it tells a sighted one. The `LOG_WARN` naming the cue is what keeps a missing line from
  being indistinguishable from a silent one.
- Speech is on the **Combat** channel (subtitles are a sequence — a line that interrupted its
  predecessor would cut off the half not yet heard) with source **Flyover** (the game put it on
  screen). A changed `BINKHANDLE` means a different movie and clears the last-cue marker, so a
  second cinematic reusing a cue key is not swallowed as already-said.

## Squad select and armory loadout

- ⛔ **A field looked up in the wrong place answers `""`, and an empty answer is indistinguishable
  from "the game is not showing this"** — the ambiguity the drop-empties wire format is built on,
  and four of squad select's five first-run defects were that one bug in different clothes. The
  four screen-wide labels (edit, dismiss, promote, select-unit) are class defaults, identical for
  every card, so the companion sends them once on the MISSION record; asking the CARD for them
  yields `""`, the build skips an unlabelled node, and whole controls silently cease to exist.
- ⛔ **A repeating field must never be dropped when empty.** `util` is indexed by list position, so
  a locked slot in front of an occupied one makes the occupied one answer for the pocket in front
  of it and pushes the last slot off the end into the fallback.
- ⛔ **`UISquadSelect_ListItem.SlotIndex` IS NOT UNIQUE, so it cannot be the key that joins the two
  halves' records.** `UpdateData` returns at `if(bDisabled) return;` exactly one line before
  `SlotIndex = Index != -1 ? Index : SlotIndex`, so a locked "increase squad size" card is never
  given one and keeps the default **0**. Live 2026-08-23 a six-card strip read `0,1,0,2,3,0`: both
  locked cards collided with the genuine squad-slot-0 soldier, who matched the FIRST record and
  announced herself as *"увеличьте размер отряда в партизанской школе"* for the whole screen — and
  because the locked record carries no `weapon`/`util` fields, every one of her controls whose
  words come from the card record lost its label and was skipped, leaving her card three nodes
  deep instead of six. Both halves now key on the card's POSITION in `m_kSlotList`, the one index
  they walk in the same order. The generalisation outranks the "never by push order" rule it
  replaces: **a field both halves read off the same object is a key only if the game guarantees it
  is distinct — and a field the game sometimes never writes cannot be.** The list position is
  still the right choice over a running counter, because the companion's `Card == none` skip must
  leave a visible HOLE rather than shift every later card onto the wrong soldier.
- ⛔ **"Is this card empty" is a question only the unit ref can answer, never the widget tree.** An
  unmanned card keeps every widget it was built with — the `ВЫБРАТЬ БОЙЦА` card still owned a
  two-item `UtilitySlots` list under a hidden parent — so a `controls.empty()` test never fired,
  the card was built as an ordinary one, and the node the player landed on was **Edit**.
  `OnClickedEditUnitButton` guards its whole body with `if( XComHQ.Squad[...].ObjectID > 0 )`, so
  on an empty slot it fell through and did nothing: no picker, no negative sound, **no way to put
  a soldier on the mission**. The verb that opens `UIPersonnel_SquadSelect` is
  `OnClickedSelectUnitButton`, reachable in vanilla only from `selectUnitButtonMC` and from
  `HandleButtonSelect`'s own `if(ListItemIsEmpty())` branch — a special case we did not inherit,
  because activating through `HandleButtonSelect` is deliberately refused (it dispatches on the
  game's `m_eActiveButton` rather than ours). `SquadButton::SelectUnit` names it; it is **not** an
  `ESquadSelectButton` member and is blocked from reaching `SetSquadSelectButtonFocus`.
- **Join after cleaning, never before**: rank, name, nickname and class pre-joined in the companion
  left a separator behind when `CleanText` reduced a nickname to nothing. They are four fields,
  joined after each is cleaned. Header labels come from `UISquadSelectMissionInfo`'s own strings —
  English constants against localized values read as a mixed-language sentence.
- **An icon in its unlit state gets no words**: every card ended in the bond icon's title for the
  one state where `UpdateData` leaves it at `SetBondLevel(0)` with `AnimateCohesion(false)` —
  unlit, and passed straight over by a sighted eye.
- **Arrows follow the screen's geometry, not the builder's automatic single-item chain**, which ran
  Down out of the bottom of one card into the top of the next. Cards sit side by side and each
  stacks its controls vertically, so Up/Down walks one soldier's card and Left/Right steps to the
  soldier beside them at the same height. Every edge is wired explicitly, and a direction with
  nowhere to go points at ITSELF — `KeyGraph::Move` treats `dest == node` as a wall, which is how a
  card's top and bottom stop without wrapping. Cards are ragged (a Reaper has one utility slot
  where a Grenadier has three), so Left/Right clamps to the neighbour's last control.
- The loadout screen exists for verbs a keyboard cannot reach at all: strip items, weapons and gear
  are dispatched from mouse handlers and controller trigger codes, and unequip is the row's X
  button.

## Covert actions

The WOTC core strategy loop (`docs/covert-actions-recon.md` is the survey). It fails in three
different ways at once, and each half of the mod owns one.

- ✅ **The action ROWS really are retained**, which puts this screen in the `UIChooseFacility`
  family rather than the `UIPersonnel_ListItem` one: `InitListItem` goes through
  `UIListItemString.SetText`, so `Text` and `bDisabled` are readable natively — and
  `Item.metadataInt = ActionState.ObjectID` (`UICovertActions.uc:368`) is written unconditionally
  for every action row, which gives the two halves a **real join key** instead of a position.
  Check which kind a row is before planning the read; this screen is the good kind.
- ⛔ **Everything else retains nothing, and the slots are not even widgets.**
  `UICovertActionSlot extends Object`, so the four slot cards never appear in the panel tree at
  all and `/gui` cannot see them. The detail card, faction panel and risk rows are ordinary
  `MC.BeginFunctionOp` pipes. What the screen DOES keep is `arrActions` — the state objects it
  populated Flash FROM — so the companion re-derives every word through the screen's own public
  accessors (`GetActionName`, `GetActionRisks`, `IsActionInfluenceMet`, …) with **not one
  `XCOMHISTORY` lookup for the card**. Tier 2 of the read-the-screen rule, verbatim.
- ⛔ **`CONFIRM ACTION` is MOUSE-ONLY in vanilla, and `EDIT` loadout is gamepad-only.**
  `LaunchButton.DisableNavigation()` (`:433`) takes the launch button out of the navigator and the
  only key path to `OnLaunchClicked` is `FXS_BUTTON_X` (`:1087`); `EditSoldierLoadout` is
  `FXS_BUTTON_Y`. Both constants come only from `XboxTypeS_X`/`_Y` in `DefaultInput.ini` — there is
  no keyboard binding in any input section, so a keyboard-only player cannot launch a covert action
  at all. Re-providing those two is most of what this surface is for. Launch goes through
  `UIButton::Click()`, which self-gates on `IsDisabled && bIsVisible`; the slot buttons go through
  the slot object's own `HandleClick("theButton"/"theButton2")`.
- ⛔ **Selecting a row is DESTRUCTIVE, so the warning is preventive rather than a report.**
  `SelectedItemChanged` (`:883`) calls `ClearSlots()` BEFORE switching `ActionRef`, submitting a
  game state that empties the OUTGOING action's staff and cost slots — so arrowing to a neighbour
  to compare it throws away the squad you just assembled. The recipe mirrors vanilla rather than
  decoupling (the confirm handler reads the screen's own `ActionRef`, so a focus that disagreed
  would be the `UISaveGame.OnAccept` hazard at campaign scale). It cannot be ANNOUNCED after the
  fact either: `WriteBackFocus` runs before `SpeakMove(..., interrupt=true)`, so anything said
  there is cut off. `Vocab::CovertActionStaffed` therefore rides on the SELECTED row while it
  still has something to lose.
- ⛔ **A slot's gate is its own `IsDisabled`, not a re-derived condition.**
  `UICovertActionStaffSlot.HandleClick` only tests `!GetAction().bStarted`, so with ANOTHER action
  in progress it would happily open the personnel picker for an action that cannot be run —
  vanilla is saved from that by `!bActionInProgress` in `OnUnrealCommand` and by
  `DisableMouseHit()`. `UpdateData` records the same decision on the slot as `IsDisabled`
  (`DisableAllSlots`/`EnableAllSlots`), so the verbs test that. One gate, the game's own.
- **The game's own slot mode is skipped.** Enter on a row calls `SelectStaffSlots()`, which rewires
  the screen's navigator into a 2×2 grid with its own Escape meaning. Our recipe owns the arrows,
  so the slots are ordinary nodes in one graph and the 2×2 is flattened to four rows — geometry is
  not information. A row therefore has NO Enter: selecting IS the action, and Tab is the way on to
  the slots.
- **The risks are a LIST, not a `GraphSheet`.** Two facts per row, no column headings drawn
  anywhere, and "Soldier captured, moderate" is a complete sentence. ⚠ The bucket
  (`Low`/`Moderate`/`High`) is all a sighted player gets — `GetRiskLevel` thresholds
  `ChanceToOccur` and the raw number is never displayed, so speaking it would be the initiative-
  order leak again.
- ⚠ **We deliberately speak MORE risks than the screen draws.** `RefreshRisksPanel` bounds its loop
  by `arrActions.Length` where it means `Labels.Length` (`:524`) — the number of ACTIONS on offer,
  not of risks — so with fewer actions listed than the action has risks, the extras are blanked and
  hidden from sighted players too. The Highlander does not fix it. This goes the opposite way to
  the cheat-gated WOTC initiative order because the direction of the error is opposite: that
  overlay withholds on purpose, this is a typo losing information the design means to show.
  `covert-actions-recon.md` §6.1.
- ⚠ `XComGameState_CovertAction` is a **counter-example to "the Highlander appends"**:
  `AmbushMissionSource` (#485) and `RewardsNotGivenOnCompletion` (#810) are declared before the
  struct declarations, so `StaffSlots`/`CostSlots`/`Risks`/`NegatedRisks` all move against a
  vanilla dump. Nothing native reads them, but it is the concrete case `just sdk-adopt`'s vanilla
  refusal exists for.

## Resistance orders

The card board (`UIStrategyPolicy` + `_Card` + `_DeckList`): three faction columns of order slots
plus a wild-card column, the hand of unplayed orders below, and Continue.

- ⛔ **Vanilla's keyboard has ONE verb here, and it is "leave".** `OnUnrealCommand` answers Escape
  and Enter with the same `OnClickedContinue()`; every other branch is a controller constant
  (`FXS_BUTTON_X` remove, `FXS_BUTTON_Y` filter, the DPAD navigation) or wrapped in
  `` `ISCONTROLLERACTIVE ``. Placing a card is **drag and drop** — `UIStrategyPolicy_Card.OnMouseEvent`
  is the only path into `SelectCard`. So this recipe is not narration over something that works;
  like the debris room, it is what makes the screen exist.
- ✅ **The cards retain their STRUCTURE and none of their words.** `CardRef`, `ColumnIndex`,
  `ColumnSlot`, `bLocked` and `bVital` are real fields, so native reads the board layout for
  itself; `SetCardData(Title, Desc, Quote, Image, …)` takes every word as a parameter, so the
  companion re-derives them from the `XComGameState_StrategyCard` the ref names.
- ⛔ **The cards are walked off the SCREEN'S OWN lists, never off the game state.**
  `Screen.Columns[i]` and `Screen.Hand` are what the screen built, in the order it built them, and
  both halves address a card by `(column, slot)` — column −1 being the hand. Re-deriving the layout
  from `ResHQ.GetHandCards()` / `FactionState.GetCardSlots()` would be a second opinion about which
  card is where, and the first time the two disagreed a verb would move the wrong card. Same
  family as the squad-select key.
- ⛔ **`SelectCard` is a two-step state machine, and reproducing the mouse gesture means
  reproducing BOTH of its gates — which are not the same gate.** Picking a card UP is
  `FXS_L_MOUSE_DOWN`, which requires the card's own `CanDragCard()`; putting one DOWN is
  `FXS_L_MOUSE_IN` under `if( bDragging )`, which tests **nothing** about the target. A blank slot
  is the normal drop target and `CanDragCard()` returns FALSE on one — so testing it on the drop
  makes every empty slot unfillable, which is the whole screen.
- ⛔ **`bEnableDragging` is the other gate and `SelectCard` does not check it** on the
  mouse+keyboard path (only the controller branch does). Outside the monthly report the card's own
  handler refuses and raises `UICantChangeOrders()`; calling `SelectCard` without that gate would
  let a player rearrange orders on a day the game forbids it. The refusal is reproduced whole —
  negative sound and the game's own popup — because that popup is the only statement anywhere of
  why, and the popup recipe already reads it. ⚠ The read-only state is said ONCE, on the screen
  name at attach (`Vocab::PolicyReadOnly`), not on each of a dozen cards: a sighted player finds
  out by clicking, which is cheap for them and costs a blind player a modal per attempt.
- **The board is its own geometry and menu mode already speaks it**: one ROW per column, its items
  the slots, all sharing a row key — so Left/Right walks one faction's slots and Up/Down steps
  between factions keeping the slot position, which is the comparison the board exists for. The
  faction's name and influence are a `PushContext` around each row, announced once on entry rather
  than repeated per card.
- ⛔ **The held card is said with a plain Value part, NOT `AnnouncementKinds::Selected`.** Selected
  also makes a node its stop's LANDING node, which would silently reroute Tab past every card
  declared before it — the hazard the kernel's own comment names.
- **An empty slot is NAMED, never skipped.** `RealizeColumn` hints only the wild-card column's
  blanks, so most empty slots carry no text at all; an unnamed node is a control that silently
  ceases to exist, on the one screen where the empty slots are what the player is aiming at.
  `Vocab::PolicyEmptySlot`.
- **No focus write-back through `Recipe::SetListIndex`**: the board is FIVE lists (four columns and
  the hand) and that hook takes one index against one screen. `FocusTarget::button` instead — the
  cards are navigable `UIPanel`s and the screen's navigator is live on mouse+keyboard
  (`DisableNavigation` is called only under `` `ISCONTROLLERACTIVE ``).
- The hand is NOT paginated on mouse+keyboard: `UIStrategyPolicy_DeckList.FilterCardsToDisplay`
  returns every card unless a controller is active, and the page arrows stay hidden. So the graph
  reads the whole hand and needs no paging verb.
- ⚠ `XComGameState_ResistanceFaction.GetColumnHint()` returns `""` unconditionally (a shipped
  `//TODO`), so the column-hint pipe carries nothing and is not read.
