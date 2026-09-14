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
- **A facility's doom count is on screen, on its pin**: `UIStrategyMapItem_AlienNetworkComponent`
  draws `MissionState.Doom` as the pin's level, so the ring's facility entry carries it, labelled
  with the meter's own title (`ПРОЕКТ "АВАТАР" 2`) — the word the game ties those blocks to, read
  off the HUD class default, which holds it. Not the tray: the tray's mission buttons draw no
  level.

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

## Avatar project meter

The geoscape's third Tab stop (`BuildDoomStop`) and a change watch beside it (`doom_meter.cpp`).
Parity both: the meter is drawn top centre from the first doom onward, and a doom change is a
ten-second sequence nobody sighted can miss — screen tear, a camera pan to the site, the game's own
message written over the meter, the new pips blinking.

- **The panel retains one number.** `UIStrategyMap_HUD.CachedDoom` is what the pips were last
  drawn from, rewritten by every `UpdateData` after the push, so it is the count on screen — pending
  doom mid-animation excluded, exactly as the pips exclude it. The maximum is the loop bound of that
  push and is retained nowhere; the companion answers it (`GetMaxDoom`, which Second Wave scales).
  The countdown that replaces the pips at completion goes out as nine strings and is retained as
  one flag (`bDoomCounterVisible`); its label and four unit words are the panel's own localized
  strings, populated under `[UIStrategyMap_HUD]`, and the values come off `GetTimerDisplayValues`
  at announce time because the clock runs under the stop.
- ⛔ **The change is watched from OUTSIDE the recipe, by polling the pips.** The whole sequence
  runs in `eSMS_Flight`, where the tray recipe refuses to attach — correctly, nothing is navigable
  there — so an announcement on the node would never fire. And it polls `CachedDoom` rather than
  listening for `OnFacilityAddsDoom`: `ClearPendingDoom` fires the pending entry's event name only
  when it has one, and the covert action's sabotage, a destroyed facility's removal and the first
  facility's second pass queue their pending doom with none. The pips move for all of them.
- **The words are the game's push.** `SetDoomMessage` / `SetDoomMessageRemove` is an
  `MC.FunctionString` on the HUD, so the Flash capture holds it against the HUD's cache index one
  frame before the pips change: "Alien Facility in West Africa", "Avatar Project" for the hidden
  fortress, "Minor Breakthrough", "SABOTAGE". ⛔ Each recorded call carries a sequence stamp and the
  watch remembers the last one it spoke — the per-panel history keeps old pushes, and a stale
  "Alien Facility in X" read against a change from somewhere else would name the wrong place. An
  edge with no new message waits 400 ms, then speaks the numbers alone.
- **A message that IS the title gets the count alone.** The fortress's own progress pushes the
  meter's title as its message, in title case where the panel draws capitals (`Проект "Аватар"`
  over `ПРОЕКТ "АВАТАР"` on the 2026-09-14 run), and the line came out as the title twice. The
  compare is Windows' case fold (`CompareStringEx`, `NORM_IGNORECASE`), because a byte-wise or
  ASCII fold would call the two Russian strings different.
- The change line joins title and count the way the stop does (`title, N out of M`), so the two
  readings of the meter cannot drift apart.
- ⛔ **A blank stack is not "the map left" — hold, and compare on return.** The stack finder
  answers nothing for a poll or two while a popup is pushed over the map (the navigator logs it as
  `stack valid 0`), and the watch took that for leaving the geoscape, reset, and re-baselined on
  the popup's close — swallowing every edge that landed under it. The max-doom run is the case:
  `GiveResource Doom 5` submitted, the alien AI entered Lose mode on the same update and pushed
  its popup, and the first `UpdateData` after the popup drew the five pips AND the countdown at
  once; the watch said nothing and only the Tab stop read the timer. `GeoscapeGate.stackRead` is
  the distinction; a reset needs a stack that was READ and holds no map. And when the countdown
  flips in the same redraw as a doom edge, the edge's line carries the timer rather than the meter
  being read twice.
- The count of blocks that blinked is the one thing the game states without words (`up 2`,
  `down 1`); the total is `Vocab::Fraction`, the same words the monthly report reads this meter
  with. Nothing about the alien's next timer, which the game draws nowhere.
- The once-per-campaign `eAlert_Doom` popup ("Project Progress in <Region>") is the popup recipe's.

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
- ⛔ **A batch whose operands are NAMES is skipped by the op's name, never filtered by what the
  names look like.** Reading every batch on the alert's panel (the multi-batch fix) also read
  `UIPanel.LoadChild`'s `spawnChildPanel` — child MCName, child library id — and every popup of
  2026-09-04 opened with "ButtonGroup, 1 of N", "EmptyControl, 2 of N" ahead of its title, as the
  landing node. Neither string has a dot or a scheme, so the asset filter is the wrong tool; the
  op is structural by construction, so `IsStructuralBatch` names it. The child-addressed ops
  (`ChildSetString` and kin) carry their names in the capture's `child` field, not in the
  operands, and need no such rule. **The same afternoon the second of `UIPanel`'s three
  string-carrying ops leaked the same way**: `addTweenBetween` (`UIPanel.AnimateIn`) pushes
  `"_alpha"`, `"time"`, `"delay"` as strings, and `eAlert_FacilityComplete` opened on
  "_alpha, 1 of 8". The tween was recorded on the alert's index BEFORE its own `UpdateData`, and
  the alert's four panels have `bAnimateOnInit = false` — so it was a STALE batch from the index's
  previous owner, whose `CLEANUP_OP` the capture never saw: the one measured exception to
  `flash_shadow.cpp`'s "a recycled index is safe" argument. All three ops are now named; the
  reader logs every structural batch it skips (`skipped structural batch(es) … on cache index N`)
  and warns on any contributing batch that is not an `Update*`/`Set*` push, which is the shape
  every leak so far has had.
- ⛔ **A bare digit pushed as a string is a POSITION or a BADGE, and only the batch's push order
  says which operand it is.** `UpdateCovertSlot` leads with `string(Index)` — the Flash slot a
  soldier goes into, which nothing draws — and the covert-action confirmation read "0", "1", "2"
  between its soldiers' names (2026-09-04 12:16). `UpdateRetributionChosenPanel` pushes the
  Chosen's level second, the badge inside its icon, and the Retribution alert read "2" ahead of
  the Chosen's class and name. Both survive the type filter (strings) and the asset filter (no
  dot). `IsPositionalOperand` names the batch AND the position; the level badge is dropped
  rather than labelled because the report's Chosen page already drops the same badge
  (`X2AccessChosenReader.DescribeChosenEvents`), and two surfaces reading one fact must agree.
- ⛔ **A popup with NO Escape branch must send its own single button record.** The recipe's
  fallback pair (Continue / Dismiss on Enter / Escape) exists for a popup that answered nothing,
  and it assumes both keys do something. `UIChosenEncountered` and `UISoldierCaptured`
  (`chosen-bond-recon.md` §1.2-1.3) answer Enter and Space only, so a Dismiss there would be a
  node that does nothing when pressed — worse than no node. Their companion branches push one
  button, and the screen's own `OnContinue` is reached through its input path because that is
  where `UISoldierCaptured` fires its `SoldierCapturedClosed` event.
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
movie. The base game's tentpole cinematics are NOT this; they are in-engine matinees. The two can
never both be showing, which is why their lines cannot collide.

✅ **First live pass 2026-09-04, and it works.** The vtable pin held
(`MovieSubtitles: movie player at 0x… validated`) and `CIN_XP_PreIntro` read all 35 of its cues in
Russian, timed to the film, with not one `no localized text for cue` — which is what says the loc
lookup found the right package in a language nobody wrote the lookup for.

- ⛔ **A NAME on the movie player is not a Bink movie, and the discriminator is whether a `.bk2`
  exists.** `XComGame/Movies/` ships a `.txt` cue track for the in-engine matinees TOO
  (`CIN_TP_OldGods_pt1`, `CIN_TP_ShadowChamberReveal`, `CIN_TP_BuildOutpost` — cue tracks, no video),
  and the game pushes their names onto the full-screen movie player anyway: our `movie '…' opened`
  line fires and `IsAnyMoviePlaying` gates the navigator, but no cue table is ever loaded, so this
  surface stays silent. **That silence is correct here and must not be "fixed" from this side** — the
  cues are drawn by `RenderExtraSubtitles` timed against the matinee, not against a `BINKHANDLE`, so
  reading them needs a different owner. ⚠ **Nothing in the mod is that owner yet**: this doc used to
  claim `UINarrativeCommLink` had these because `HideUIForCinematics` spares it, and the 2026-09-04
  log refutes that — `CIN_TP_OldGods_pt1` ran 2m13s in total silence with 21 cues on disk. Filed
  under Known defects in `../next-steps.md`; do not re-derive the claim from `HideUIForCinematics`
  alone, it is exactly the reasoning that was wrong.

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

## Armory hub

`UIArmory_MainMenu`, the `armory` recipe with `X2AccessArmoryReader`. Built 2026-09-03 from source
(`../armory-recon.md` §1); **not yet run live**.

- ⛔ **Written against the Highlander's rewrite of the class (Issue #47), on purpose.** The mod
  requires the Highlander; its `UIArmory_MainMenu` names every row (`ArmoryMainMenu_<X>Button`),
  keeps them as fields, lets mods insert rows through `OnArmoryMainMenuUpdate`, and respawns all
  of them on every `OnReceiveFocus`. So **a row is identified by MCName on both halves** — never
  by index (a mod may reorder) and never by pointer (they die on every focus return). A dump
  without the eight fields is a vanilla dump, which `just sdk-adopt` refuses.
- ✅ **The rows retain their words** (`InitListItem` → `SetText`), so the labels are native; the
  promote row's `<img>` prefix is `CleanText`'s. What they do not retain — the disabled REASON
  (`SetDisabled` parks it in the tooltip manager) and the description under the list (a Flash
  child written by `OnSelectionChanged`) — the companion re-derives from the same cascades and the
  same object-identity switch the screen uses.
- **Enter is select-then-screen-Enter**, as on the loadout: `OnAccept` reads the selection and, in
  the Highlander, re-sends Enter to the list so a mod-wrapped delegate runs. A greyed row keeps
  its node; `OnAccept` refuses it with the game's own sound.
- **The soldier cycle is re-provided as a spinner** on the soldier node, gated exactly as
  `UIArmory.OnUnrealCommand` gates Tab/LeftShift (which the layer displaces), and the
  abilities/traits toggle — a nav-help button with no key — as a node under the summary. Both are
  the screen's own functions.
- **The header composers are shared with the loadout** (`X2AccessHelper.DescribeArmorySoldier` /
  `DescribeArmoryStats`), so the two screens cannot name a soldier differently — the
  two-surfaces-must-agree rule.

## Weapon upgrades

`UIArmory_WeaponUpgrade` and its pattern picker `UIArmory_WeaponTrait`, one `weapon upgrades`
recipe with `X2AccessWeaponUpgradeReader`. Built 2026-09-03 from source (`../armory-recon.md`
§2); **not yet run live**.

- ⛔ **The layout is the Highlander's** (`WeaponViewOffsets` heads the class, Issue #832): every
  screen offset is 0x10 past vanilla, and the pin on that field is what refuses a vanilla dump.
- ⛔ **One list is live, and the slots list is never moved under an open picker.** `ActiveList` is
  the mode bit, as on the loadout; `PreviewUpgrade` reads the SLOT from the slots list's selection
  whichever list the cursor is in, so a write-back there while the picker is open would preview
  the wrong slot. The slots become a read-only mirror titled `current slots`, with the frozen
  (`bIsLocked`) slot marked selected, and the picker is the landing stop.
- ✅ **The rows retain their words** (title, the effect line, Count, three bits); the greyed
  REASON is the one thing they lose — `disabledText` is never assigned and no tooltip gets it —
  so the companion re-runs the two tests the screen made.
- ⛔ **The stat block is a preview of a state that does not exist.** `UIArmory_WeaponUpgradeStats`
  has no members; the game feeds it from a throwaway change state with the selected upgrade
  applied. The companion reproduces that change state (never submitted) so the numbers spoken
  are the numbers drawn, and the stats read at announce time because the selection is what our
  write-back moves.
- **Enter is the screen's own** (`OnAccept` → `OnItemClicked` on the active list): the picker's
  open animation, the confirm dialog and the reuse-upgrades shortcut all live inside it. The
  customize rows — otherwise reached only through `OnAccept`'s no-selection forward, or the
  D-pad — activate through their own row handler. Escape is left to the game: it backs out of the
  picker, then the colour selector, then the screen.
- **The pattern picker** is the same recipe (a subclass, topmost when up): a plain list with the
  current pattern marked selected and landed on, Enter picks and closes, ⛔ Escape REVERTS to the
  starting pattern and closes — vanilla's own meaning, and the reason the current row says
  `selected`.
- The colour row opens `UIColorSelector`, which the customize recipe reads (below) — it is the
  same widget, and the topmost-of-the-family probe there does not cover this screen, so its
  grid is unread HERE until the weapon-upgrade recipe learns the same build. The row itself now
  names its swatch by hue. The name row opens the text-entry dialog, which the observer echoes.

## Manage equipment

`UIManageEquipmentMenu`, the `manage equipment` recipe, native only. Built 2026-09-04 from source
(`../customize-recon.md` §1); **not yet run live**.

- **The pause menu's shape, and the pause menu's activation** — select, then Enter to the LIST.
  It works here for the reason the rows are plain: a `UIListItemString` without a confirm button
  consumes nothing, so `UIList.OnUnrealCommand` reaches its own `OnItemClicked`. ⛔ That is a
  property of THIS row class; see the customize rules below for the row class where it fails.
- The opener is a nav-help button with no key (a gamepad's LT), which the loadout and
  squad-select recipes already expose as verbs of their own. Escape stays the game's.

## Soldier customization

The `UICustomize` family — the category menu, the category screens, the option picker, and the
colour selector inside any of them — one `customize` recipe with `X2AccessCustomizeReader`, probed
as the TOPMOST stack entry that `IsKindOf UICustomize`. Built 2026-09-04 from source
(`../customize-recon.md` §2); **not yet run live**.

- ⛔ **A `UIMechaListItem` CONSUMES Enter, so "send Enter to the list" does not reach the list's
  delegate on these rows.** `HandleClickableControls` answers any row with a `Desc` (every mecha
  row) by `Click()`ing its delegate and returning true, and `HandleCustomControls` answers a
  checkbox row through its box first — a `bReadOnly` box (the menu screen's three ALLOW rows)
  returns true WITHOUT toggling. So the activation is the SCREEN's own Enter (which is what
  reaches the row, and on the picker what picks-and-closes), and a checkbox row is toggled
  through the screen's `SimulateMouseClickOnCheckbox` on the selection — the game's own
  FXS_BUTTON_A body, minus a `bIsVisible` gate the game leaves false on screens it shows.
- ⛔ **The picker keeps no title** (`UpdateTrait` drops the string it is handed), and the screen
  beneath keeps its selection across the push. The picker is named by THAT row's label — the
  word a sighted player just read — found by walking the stack to the next screen of the
  family, past the mouse guard `bConsumeMouseEvents` pushes under every screen.
- **The rows are REUSED across a soldier cycle** (`GetListItem` spawns only past `ItemCount`),
  so the wire cache is keyed on the `Unit` POINTER as well as the rows, and discriminated on
  `UnitRef.ObjectID` — which is 0 for a character-pool unit, hence the pointer.
- **Escape stays the game's, and it REVERTS**: the picker re-fires its delegate with
  `StartingIndex`, the colour selector closes with `InitialSelection`. Both are marked `selected`
  and landed on, the pattern picker's rule.
- **The colour grid is an AID**, labelled as one in `Vocab::ColourName`: the game draws a swatch
  and a number, and the hue name is ours — coarse on purpose (eight hues, three lightnesses).
  Distribution audit: every soldier's hair reading the same hue is the collapsed-value failure.
- **The voice picker's PREVIEW is the row's secondary action** (Backspace): the screen's own
  Enter picks and closes before the row's button could see it, so vanilla reaches PREVIEW by
  mouse only.
- The soldier cycle is re-provided as a spinner exactly as the armory hub's, behind the same
  gate vanilla puts on Tab / LeftShift (`bInArmory` and a soldier to cycle to); in the character
  pool's editor there is no cycle and no spinner.

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

## Covert action report

The loop's RESULT screen (`UICovertActionReport`), the `covert report` recipe with
`X2AccessCovertActionReader.DescribeCovertActionReport`. Built 2026-09-03 from source
(`../covert-actions-recon.md` §8b); **not yet run live**.

- ⛔ **Escape is Continue, and Continue deletes the action.** `OnUnrealCommand` answers A, Enter,
  Space and Escape with `OnContinueClicked`, which closes, raises the reward popups and
  `RemoveEntity`s the action's state object. There is no back and no abort; the recipe offers one
  button over the screen's own Enter, and the companion answers empty once `GetAction()` is none
  rather than dereferencing.
- ⛔ **The slot verbs are dead here and are not offered.** `HandleClick` gates on `!bStarted` /
  `!bCompleted`, both false forever on a completed action. The picker's `ClickCovertActionSlot` /
  `EditCovertActionLoadout` would be silent no-ops.
- ✅ **The per-soldier results are READ OFF THE SLOT OBJECTS, never recomputed.** `UpdateData`
  writes `PromoteLabel` / `Value1..Value4` onto `UICovertActionSlot` and the report branch of
  `Update()` hands exactly those to Flash — so the retained values ARE the card's argument list,
  and they are right under the Highlander's `RewardsNotGivenOnCompletion` (#810) for free. Only
  the label is a local, re-derived as the picker does (`GetNameDisplayString`, the cost string).
- **The reward line is the GRANTED values** (`GetRewardValuesString`), not the picker's preview;
  the screen's own `GetRewardString()` answers with the right one and is what is called.
- The widget tree is two nodes: `ActionSlotPanels` is empty on this screen, and `ContinueButton`'s
  `Text` is a real retained label (icon-injected on a controller, cleaned).

## Resistance orders

The card board (`UIStrategyPolicy` + `_Card` + `_DeckList`): three faction columns of order slots
plus a wild-card column, the hand of unplayed orders below, and Continue.

- ⛔ **Vanilla's keyboard has ONE verb here, and it is "leave".** `OnUnrealCommand` answers Escape
  and Enter with the same `OnClickedContinue()`; every other branch is a controller constant
  (`FXS_BUTTON_X` remove, `FXS_BUTTON_Y` filter, the DPAD navigation) or wrapped in
  `` `ISCONTROLLERACTIVE ``. Placing a card is **drag and drop** — `UIStrategyPolicy_Card.OnMouseEvent`
  is the only path into `SelectCard`. So this recipe is not narration over something that works;
  like the debris room, it is what makes the screen exist.
- ✅ **The cards retain their STRUCTURE and none of their words.** `CardRef`, `bLocked` and
  `bVital` are real fields, so native reads the board layout for itself;
  `SetCardData(Title, Desc, Quote, Image, …)` takes every word as a parameter, so the companion
  re-derives them from the `XComGameState_StrategyCard` the ref names.
- ⛔ **`ColumnIndex`/`ColumnSlot` are real fields and MUST NOT be read: the game assigns them only
  in `RealizeColumn`'s slot loop.** `CreateLockedCard` — the locked placeholder appended AFTER
  that loop on every met faction below Influential — leaves both at the default 0, so a column
  with a real slot 0 holds two cards answering (column, slot) identically. Live, that was a
  duplicate control id thrown from the build, and the navigator's throw-detach-reprobe loop made
  the whole screen silently dead at ~4 exceptions a second. The address both halves use is the
  LIST POSITION (column = the screen's `Columns[]` index, −1 the hand; slot = the item index in
  whichever list holds it) — the same index the companion's `ResolveCard` hands to `GetItem()`,
  which is what the "(column, slot)" in the rules above means.
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
- ⛔ **`SelectCard`'s refusal is SILENT — its final `else` plays a negative click and says
  nothing — and a blind player cannot tell that from a dead key.** Found live: a player held a
  Templar card over the Reapers empty slot pressing Enter into silence, with no way to even put
  the card down (the mouse abandons a drag by releasing over nothing; a two-step selection has no
  such gesture, and vanilla keyboard Escape LEAVES the screen). Three answers, all from the
  game's own controller build, which had already solved each: while a card is held, every legal
  drop slot says `Vocab::PolicyDropTarget` (the game highlights drop spots during a drag — this
  is parity); `X2AccessPolicyReader.DropMarker` predicts refusals by mirroring `SelectCard`
  branch for branch, and the recipe captions them (`PolicyCantPlace`, `PolicyCantPick`,
  `PolicyCantMove`) **while still sending the verb — the game stays the authority, the caption
  only covers its silence**; Enter on the held card puts it down (the `` `ISCONTROLLERACTIVE ``
  branch at `UIStrategyPolicy.uc:1612`) and Backspace while holding is `ClearInspector` on every
  node, replacing the remove verb for the duration (the controller's B/Escape at `:1253`).
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
