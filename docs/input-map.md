# The keyboard budget — what the game binds, what we claim

The measured record of XCOM 2's own key bindings and of every key X2Access takes, with the
reasoning behind each claim. `CLAUDE.md` keeps the RULES (leniency and what it obliges, the three
tactical input modes, the Shift swallow, Tab stays the game's); this file is the census they rest
on, and it is the file to update when a new hotkey is claimed.

⛔ Re-measure across EVERY input section of `DefaultInput.ini` before calling a key free —
`[Engine.PlayerInput]` binds keys the tactical block does not, and most letter bindings are generic
`X_Key_Press` forwarders that look empty and are not.

## The tactical layer: input ownership

Three modes, and the layer must always know which one it is in:

- **World mode** (default) — arrows drive the tile cursor.
- **HUD mode** — the tactical recipe is FOCUSED; arrows navigate it.
- **Targeting mode** (`UITacticalHUD.m_isMenuRaised`) — the GAME owns the arrows, Tab and F1-F8. We
  narrate and read there; we claim nothing that steers. ⚠ **Narrowed to targeting with a TARGET
  LIST.** In free aim the arrows steer nothing: the controller aiming branch is dead code on
  mouse+keyboard, so a bare arrow lands in `CycleAbilitySelection` and abandons the ability the player
  just chose. There the rule inverts and the arrows are ours — the principle was "do not take a key
  the game is using to steer", and here it is not.

The recipe is ATTACHED BUT UNFOCUSED while the cursor keeps the arrows — the navigator's focus
split (`Recipe::ambient` + `MenuNavigator::OwnsInput()`): the tile reader and the legacy-listener
mute gate on OWNS INPUT, the targeting poll on `MenuAttached()` (a non-ambient recipe took the
surface), and every navigator claim on attached-AND-focused. Gating the tile reader on mere
attachment is right for menus and was wrong here.

HUD mode's own keys: bare `Tab` enters the layer — claimed whenever a mission is active and
the layer is not already focused, not merely while attached, so that a press the layer cannot
honour is refused aloud ("HUD unavailable.") rather than dying silently. Tab is free to take
because the companion's `XComInput.ini` moves the game's next-unit command off it (§Soldier
switching below): there is no vanilla binding left under the claim. `Escape` while HUD-focused
exits to world mode ("Map") and is claimed only then — on every menu recipe Escape stays the
game's own close key. While focused the navigator's generic set applies (arrows, Tab/Shift+Tab
between stops, Enter, Space, Home/End). `E` / `Q` are NOT claimed in either mode: a soldier
switch made while the graph is focused is the game's own, and the selection poll narrates it.

**UE3 matches modifiers LENIENTLY.** A `KeyBind`'s `Control`/`Shift`/`Alt` are modifiers the chord
REQUIRES; modifiers the player merely happens to hold are NOT checked.
`bIgnoreCtrl`/`bIgnoreShift`/`bIgnoreAlt` are the opt-in SUPPRESSORS — `Input.uc`: *"if true, the bind
will not be activated if the corresponding key is held down"* — and the entire game config uses them
exactly twice: on `R`, so `Shift+R` reaches ErrorReport without also reloading, and on `F4`, so Alt+F4
does not also fire the game action. (Those exist to SUPPRESS a bare bind, which is only necessary
because matching is lenient.) So a bare binding fires under ANY modifier. **Every modifier chord we
advertise on a key that carries a vanilla bare binding MUST be claimed, or it double-fires.** (This
is why the HUD layer's entry key was `Ctrl+Tab` for as long as bare Tab carried the game's
next-unit binding — the chord had to be claimed mission-wide to keep leniency from firing the bare
binding under it. With that binding moved to `E` by config the chord is retired and bare Tab is
the entry key.)

**Multiline text entry:** `UIInputDialogue` uses bare Enter to insert a newline and ignores
Tab, leaving its visible Confirm action without a keyboard route. `text_input.cpp` claims
`Ctrl+Enter` only while a multiline dialog is active, then invokes the game's
`OnMouseAccept` handler. The claim swallows that chord so Flash cannot also insert a line;
single-line dialogs and all other editing keys keep their vanilla paths.

**The engine executes exactly ONE binding per key event.** `UPlayerInput::InputKey` asks
`GetBind` for a single command string and execs that; `GetBind` walks `Bindings` from the END and
returns the first match, so a later entry shadows an earlier one for the same key. Measured in the
exe (the key handler at `0x1407504a0` fetches one string through the vtable and hands it to the
exec parser at `0x140742b60`). Consequence for a config change: an appended `+Bindings` line for
a key already bound silently WINS; removing the old line is cosmetic (the Controls screen would
otherwise list the key twice), not functional.

**The accessibility verbs outrank the vanilla bindings**, and taking a bare letter costs **nothing: no
config snippet, no rebind at all**. Because matching is lenient, every vanilla bare-key action is
ALREADY reachable at `Ctrl+key`: the arbiter claims the bare key and swallows it, the Ctrl chord is
not claimed so it passes through, and the engine matches the vanilla binding with Ctrl held. Proven
live — bare `Left` drove the tile cursor with no camera movement, `Ctrl+Left` scrolled the camera. Of
RT's keymap only `, . / ' ; H I` are free in XCOM tactical (`V M N P L K O U X Y Z T G J B` all carry
vanilla actions), and all of those vacate onto Ctrl+letter for free. ⛔ Still do NOT poke
`PlayerInput.Bindings` at runtime: `UIKeybindingsPCScreen.ReadBindings()` reads the live array, so a
runtime edit shows up in the Controls screen and is baked into the player's profile permanently the
moment they press Save and Exit there.

**Four bare letters are taken on that argument**, all by the object scanner and all gated on
`TileReader::IsActive()`, so they stand down the moment world mode does: `B` next marker, `O`
re-announce the selection, `J` next cover position, `V` next exit (`Shift`+each for the previous
one). Each
displaces a generic `X_Key_Press | onrelease X_Key_Release` forwarder — push-to-talk, the Chosen
Info screen, and two of the ability hotkey slots — and each of those stays reachable at
`Ctrl`+the same letter, which is the whole leniency bargain above. ⚠ `J` is bound in **both**
`[Engine.PlayerInput]` (`:183`) and `[XComGame.XComTacticalInput]` (`:306`); a grep of one section
would have reported it half-free, which is the `U` trap below in a different disguise.

⛔ **`V` is the Interact ability's default hotkey** (`AddInteractAbility` sets `DefaultKeyBinding =
FXS_KEY_V`; `XComTacticalController:2628` routes `Key_V` → `ActivateAbilityByHotKey`), so it is
`J`'s case exactly and is taken on identical terms — Interact stays at `Ctrl+V` and is on the
ability bar besides, which the tactical recipe already reads. ⚠ `rooms-recon.md` §6 called `V`
"the first vanilla binding the mod DISPLACES"; that was wrong when written — `B`, `O` and `J`
preceded it on the same argument — and the recon is corrected in place. **Displacing an ability
hotkey has been the rule here since the cover ring, not an exception being made for exits.**

## The controller

⛔ **The game has two input modes, and the player's saved Input Device option picks one.**
`XComOnlineProfileSettingsDataBlob.m_bActivateMouse` seeds `UIMovie.MouseActive` (`UIMovie.uc:118`),
which is what `ISCONTROLLERACTIVE` reads. The mod does not flip it, because that rebuilds HUDs
(`avenger-view-recon.md` §6.1), and it must work in both modes. Measured 2026-10-30 in controller
mode by physical presses: the ant farm's arrows, Escape, `E` and `Q` reach the window, the arbiter
passes them on, and the game does nothing, while the stick, Start and the bumpers all work. Keys we
claim still work. So a route that rests on a key reaching the game exists in mouse mode only.

The gate is `XComInputBase.InputEvent` (`:483`), through `IsEventWithinInputTypeRange` (`:823`): with
the mouse inactive it drops every keyboard and mouse event before any screen sees it. The two
exceptions are `J` and, inside Options, Escape. So a key "passed to the game" in the arbiter's log
reached the engine and died in script. `bAllowAllInput` overrides the gate in both directions, and
`UIOptionsPCScreen` sets it on entry and clears it on exit (`:927`, `:2919`).
`UIMovie.ActivateMouse` changes `MouseActive` in memory only, so it writes no profile, but a new
movie seeds from the profile again and the HUD's mode branches have already run.

`src/gamepad.cpp`. A pad acts as a second keyboard on the shell: each mapped input runs the existing
claim for a key, and every recipe works on a pad without knowing one exists.

| Pad | Key it stands for | Meaning |
|---|---|---|
| D-pad, left stick | arrows (repeat while held) | move, adjust |
| A | Enter | activate |
| Y | Space | tooltip |
| X | Backspace | secondary action, where the row has one |
| RB / LB | Tab / Shift+Tab | next / previous stop |
| RT / LT | PageDown / PageUp | next / previous settings tab |
| B | none: the game's own `FXS_BUTTON_B` | back, which shares a case with Escape on each screen |

Start, Back and the stick clicks stay the game's.

**A press stops speech, because a reader cannot see one.** JAWS cuts its own speech on a key going
down, and every line reaches it queued, so without this a player scrolling on the pad heard each
item in full, falling further behind with each press. `TTS::Silence` runs on each press and repeat
the layer owns, B included, standing in for the keypress the reader never saw.

⛔ **JAWS's COM `StopSpeech` reports success and stops nothing.** Measured on the jfw audio session
meter with a 7 s sentence: it played on for 6757 ms after `StopSpeech` against 6764 ms with no call.
`SayString(" ", true)` cut it, and `RunFunction("StopSpeech")` did too, but Prism cannot reach that
one and Prism is pinned. A line queued straight after the flush still played in full, so
`src/tts.cpp` sends the blank flush after every stop on JAWS. An empty string fails Prism's
conversion, which is why it is a blank. Meter a reader's session rather than trusting a stop call's
return.

**The game must not see a mapped press as well.** `XCom2.exe` imports `XInputGetState` from
`XINPUT1_3.dll` by ordinal 2, and the filter is a swap of that one IAT slot, found by walking the
import table at runtime, so it needs no per-build address. While the layer owns the pad, EVERY
mapped input is hidden, not only those with a live claim: an unclaimed X or Y on the settings screen
is save-and-exit or reset-to-defaults, and the engine can poll between a press and our tick.

**Scope is one predicate**, `LayerOwnsPad`: `UIFinalShell` on the stack, the game window in front,
and a menu recipe holding input, with 400 ms of grace for the detach gap between two screens.
Anywhere else the pad is untouched, vanilla, so a screen with no recipe still answers to it. In
controller mode that vanilla pad already works with our polling on top, measured 2026-10-30: the
stick walks the ant farm, Start opens the pause menu and the bumpers switch soldiers, all with speech.
No recipe's Tab stops are reachable on a pad outside the shell.

⛔ **A sleeping wireless pad stays "connected" and reports frozen sticks** — measured at full up on
both sticks with the packet number standing still. A stick counts for nothing until it has been seen
near centre once. The press threshold sits at 16000, well above XInput's 7849 dead zone.

## Soldier switching: `E` / `Q`, moved there by config

⚠ This section is mouse mode. In controller mode the bumpers switch soldiers and `E` / `Q` do nothing.

**The one binding change the mod ships**, `mods/X2AccessCompanion/Config/XComInput.ini`, merged
over `DefaultInput.ini` when the companion loads. It removes the vanilla `Q`, `E`, `Tab` and
`LeftShift` lines from `[XComGame.XComTacticalInput]` and the `Tab` / `LeftShift` lines from
`[XComGame.XComHeadquartersInput]`, and adds `E` = the `Tab_Key_Press` command and `Q` = the
`Left_Shift_Key_Press` command in both. In tactical, the soldier bindings carry
`bIgnoreCtrl=True`, and the original camera commands remain reachable on `Ctrl+Q` / `Ctrl+E`
with `Control=True`. The previous mod's bare soldier entries are removed before adding their
Ctrl-excluding replacements, so merging over those saved entries does not retain a duplicate.
Why the commands and their bindings matter:

- **The game's Controls screen is a key-name-for-command swap over the same array.** The
  rebindable actions are fixed command strings in the exe: "NEXT UNIT / TARGET" is
  `Tab_Key_Press | onrelease Tab_Key_Release`, "PREVIOUS UNIT / TARGET" is
  `Left_Shift_Key_Press | onrelease Left_Shift_Key_Release` (both literal in `XCom2.exe` at
  `0x1417f1a40`/`0x1417f1ab0`). Every state's handler in `XComTacticalInput` and every screen's
  `OnUnrealCommand` keys on the COMMAND (the exec runs `InputEvent(FXS_KEY_TAB)`), never on the
  physical key. So a key bound to Tab's command IS Tab in every meaning: next soldier in world
  mode, next target while the shot HUD is raised (`UITacticalHUD.uc:459`), next soldier in the
  Armory / Customize / Bond screens (`UIArmory.uc:448`, `UICustomize.uc:425`,
  `UISoldierBondScreen.uc:576`), dead in the tutorial as Tab was. The game's own key hints follow,
  because `%NEXT_SOLDIER` / `%NEXT_TARGET` resolve from the live binding
  (`UIUtilities_Input.uc:373-383`).
- **`Q` / `E` were camera rotation and nothing else** — every tactical state's `Key_Q`/`Key_E`
  is a `YawCamera(±90)` (`XComTacticalInput.uc:636`, `:1243`, `:2373`; the secondary-targeting
  state forwards to the D-pad handlers, which also just yaw). The tile cursor is world-absolute
  (`X2AccessTileReader.uc`: Up is always north), so rotating the camera does not rotate the
  cursor's directions. The commands must still have real bindings: the tutorial looks them up
  even if the player never rotates the camera (the crash below). They now live on `Ctrl+Q` /
  `Ctrl+E`; in the headquarters section Q/E carried no vanilla binding at all.
- **`LeftShift` carried the previous-unit command as a key of its own**, and `Key_Left_Shift`'s
  RELEASE calls `PrevUnit()` UNCONDITIONALLY (`XComTacticalInput.uc:2147`, state
  `ActiveUnit_Moving`), so every `Shift+` chord cycled the soldier when the player let go. It
  never showed up in testing because the tutorial runs `InReplayPlayback`, whose copy of the same
  function (`:587`) is a bare `return false`. The arbiter used to SWALLOW the modifier itself
  (`{VK_LSHIFT, ModNone}`) and re-provide `Shift+Tab` by hand through
  `X2AccessObjectScanner.PrevSoldier`; the swallow also had to widen to the HUD graph's own
  `Shift+Tab`. **A command with no binding has no handler to reach**, so with the line removed the
  release fires nothing and Shift is an ordinary modifier again — in the Armory too, where the
  same release reached `UIArmory.OnUnrealCommand`'s prev-soldier case under the graph's
  `Shift+Tab` (`UIArmory.uc:468`, gated on release only). ⛔ A Shift chord that cycles the
  soldier again means the CONFIG did not merge; check `PlayerInput.Bindings` through
  `just dev-props` before touching the arbiter. ⛔ Neither the modifier swallow nor
  `object-scanner-recon.md` §4.6's mouse-gate thunk comes back: the thunk does not apply to
  `Key_Left_Shift`, which takes an `int ActionMask` and returns a bool, so `MouseGate`'s
  `EX_EndFunctionParms` assumption would leave the VM's `Code` pointer mid-expression.
- **Text entry is unaffected in the strategy layer.** `UIInputDialogue` and `UISaveGame` are
  `eInputState_Consume` and ignore Tab, so an `E` typed into a name never reaches the Armory
  underneath. The retail census of `FXS_KEY_TAB` handlers is the soldier-cycle screens above,
  the Photobooth (swallows both keys), and Squad Select's slot focus (`UIList_SquadEditor.uc:53`,
  Tab only — its previous-slot key is the left bumper, so `Q` may do nothing there); every other
  case is compiled out under `FINAL_RELEASE`.
- **The Controls screen bakes it.** Its Save writes the live array into the player's own
  `XComInput.ini`, so once they save there the bindings persist without the mod — and that
  screen is where to put them back. The headquarters pair is "BINDABLE in game but not in menus"
  (`DefaultInput.ini:467`), so config is the only way to move those two at all.
- ⛔ **A `-Bindings` removal matches the vanilla line TEXT exactly, tabs included.** The four
  tactical lines and the two headquarters lines in our file reproduce `DefaultInput.ini`'s
  spacing; reformatting them turns a removal into a no-op (harmless, per the one-binding rule
  above, but the Controls screen then lists the key twice).
- **Measured 2026-09-16, before the camera-binding fix**, at the main menu,
  `just dev-props "cdo=XComTacticalInput" Bindings`
  and the same for `XComHeadquartersInput`: all six removals took (no `Tab`, `LeftShift`,
  `Q_Key_Press` or `E_Key_Press` entry left in either array), the additions sit at the array
  tail (tactical `[98]`/`[99]` of 100, headquarters `[78]`/`[79]` of 80), and the player's own
  `Documents\...\XComInput.ini` — which carries a Controls-screen save from 2026-08-20 — was not
  rewritten. So the mod layer merges IN MEMORY over the player's saved file, not only over
  `Default*.ini`, and a config array is readable off the CDO before any input class is
  instanced, which is the cheap way to verify any future config change.

**Never re-provide the switch by claim.** It is narrated by POLLING the controller, which leaves
the binding untouched AND catches the mouse click, the turn start and the death-driven re-select
that a key claim never could. `E` / `Q` are unclaimed in every mode, HUD mode included.

`RightShift` is not claimed and carries no binding in the tactical section. `Ctrl` remains the
safe modifier for anything that does NOT want to displace a vanilla key: `Key_Left_Control`
merely toggles the pathing pawn's waypoint mode on press and untoggles it on release.

### Tutorial-completion crash: camera commands need bindings

Anton's GOG dump from 2026-09-23 pins the failure to an engine warning formatter, after
`CIN_SP_SkyrangerReturn` finished and `CIN_TP_CommanderAwakens` started. The warning's actual
format argument was `Failed to find key binding localized for mission narrative text:` followed
by the Russian camera hint containing literal `%KEY:Q%` and `%KEY:E%`. The engine passed the
warning itself as a format string to `__stdio_common_vsnwprintf_s`; the percent markup triggered
`SECURE CRT: Invalid parameter detected`. The GOG engine return RVA above the CRT was `0x1A73CF`.

`X2MissionNarrativeTemplate.GetObjectiveText` maps those tokens to `eTBC_CamRotateLeft` and
`eTBC_CamRotateRight`. `UIUtilities_Input.FindAbilityKey` tries primary and secondary bindings,
then the template queues this warning when neither exists. Our earlier config deleted both
camera commands. A shadowed bare Q/E entry would satisfy the lookup but advertise the wrong
action; the replacement is a reachable Ctrl chord. `XComKeybindingData.GetKeyString` includes
the localized modifier label, so the hint can name the chord without our own text substitution.

**Keep camera commands bound when moving their keys.** The shipped fix changes only the
companion input config, adds the two Ctrl camera chords, and excludes Ctrl from tactical soldier
switching. Headquarters bindings stay as before. Static merge checks do not prove Controls
serialization or key-release behavior: test both release orders and Save/restart, then finish
the tutorial on GOG before calling the crash fixed live (`next-steps.md`).

**Steam transition confirmed 2026-09-23.** The user reported the test working. The matching
Steam log (`0x621562FA`, image `0x20C7000`) records Skyranger Return finishing at engine time
1602.91, Commander Awakens at 1806.17, Avenger play, and the next mission loading at 2102.12.
There was no missing-binding warning or secure-CRT assertion. The native log records two
Ctrl+Q/E pass-through events; this alone does not establish both release orders or Controls
Save/restart behavior. Speech audit: 759 lines, 522 digit-normalized shapes, no double spaces
or unresolved `%KEY:` tokens, and no logged frame-pump faults. Anton's GOG retest is still owed.

**`LeftAlt` is claimed PASS-THROUGH in world mode** — `Claim::passThrough`, a third kind beside
swallow and observe: the handler runs from the tick like any claim's, and the game receives the key
as well. The tile cursor speaks the game's target preview on it (`surfaces/tactical.md`), and the
game still gets to raise its own strip (`Left_Alt_Key_Press`, bound at `DefaultInput.ini:396`, is
how `UITacticalHUD_EnemyPreview` hears the key). A swallow would have cost more than the strip:
`F4` is bound with `bIgnoreAlt=True` (`:380`) precisely so that Alt+F4 reaches `ALT_F4_QUIT` (`:390`)
instead of `F4_Key_Press`, and an engine that never saw Alt go down would fire the bare binding and
skip the quit. Presses only — a pass-through is never latched, so the release and the repeats are the
game's. Displaces nothing. `RightAlt` is left alone: the game's strip listens for the left key only.

**Bare `Tab` is the HUD layer's entry key** because the config above leaves the game nothing on
it — in world mode, and on the Avenger's ant farm, which never had a live Tab (see the strategy
table). Escape is the way back. One claim, one handler (`MenuNavigator::OnLayerEnter`), gated on
the mission or the grid being active and the layer NOT already focused; focused, Tab is the stop
cycle, and the two predicates are disjoint by construction.

**Exactly two letter keys are free: H and I.** (`Q` and `E` are not free either: the config above
spends them on the soldier switch.) Measured across every input section of
`DefaultInput.ini` — and it must be measured across ALL of them. Two traps. First, most letter
bindings are generic `X_Key_Press | onrelease X_Key_Release` forwarders into
`XComInputBase.InputEvent`: they look empty and are not (`Y` is Overwatch's default ability hotkey via
`Template.DefaultKeyBinding`, and `T`, `V`, `B`, `N` are live the same way). Second, **`U` is bound in
`[Engine.PlayerInput]`, not in the tactical block** (`ClearAllWaypoints`), so a tactical-section-only
grep reports it free and is wrong. Claimed: `H` = shot breakdown (targeting only), `I` = re-read the
tutorial message (tutorial only). That is the whole supply of bare letters.

**Punctuation is where the budget actually is**, and it costs even less: `;` and `'` carry NO
`Bindings=` entry in any input section, so they displace nothing and need no leniency argument (the
only two lines in `DefaultInput.ini` mentioning `Semicolon` are `WidgetInputAliases` character lists
for the editor's edit boxes, which bind no command). `;` is the selected-unit readout; `'` remains
free, earmarked for the vantage read.

⛔ **A GLOBAL claim — no `activeWhen`, live on every screen in every layer — has to earn the WHOLE
keyboard, not just the screens it was tested on.** `src/input.cpp` holds exactly one (`?`,
navigation help). The retired F1-F4 / Ctrl+Shift+S scaffolding was never free: **F2/F3/F4 are live
game keys in the Avenger** (`UIFacilityStaffContainer.OnUnrealCommand` maps `FXS_KEY_F2..F4` to
"activate staff slot 0..2" with no `` `ISCONTROLLERACTIVE `` gate), and F1-F8 are the shot HUD's
target hotkeys (`UITacticalHUD.uc:432`). The dev harness answers what a key answered (`/health`,
`/speech`, `/log`).

## The strategy layer

A different and much richer budget, because `[XComGame.XComHeadquartersInput]` binds almost
nothing: the geoscape recon measured **sixteen free bare letters** —
`C E F G H I K L O Q R T V X Y Z` (`geoscape-recon.md` §7) — against tactical's two. Two of
them are spent since: the companion's `XComInput.ini` binds `E` / `Q` to the barracks soldier
cycle in this section too (§Soldier switching above), and `C`, `O` and `R` are claimed in the table
below, so on the Geoscape **eleven** remain: `F G H I K L T V X Y Z`.

⛔ **The two layers' budgets are NOT interchangeable, and the tell is `R`.** In
`[XComGame.XComTacticalInput]` it carries a bare `R_Key_Press | onrelease R_Key_Release`
forwarder (line 354) — that is Reload, one of the generic forwarders the tactical section is full
of. In `[XComGame.XComHeadquartersInput]` it appears only as `Shift+R` (`ErrorReport`, which
`[Engine.PlayerInput]` also binds the same way), and UE3 REQUIRES a modifier a binding declares,
so a bare `R` there matches nothing at all. A key is free per LAYER, never per game.

Claimed here:

| Key | Verb | Gate | What it displaces |
|---|---|---|---|
| `PgUp` / `PgDn`, `Ctrl+` same | geo ring: step within / between categories | `UIStrategyMap` current | — |
| `O` | geo ring: re-announce selection | same | — |
| `Home` | geo ring: sort from selection | same | the navigator's edge jump, deliberately |
| `C` | geo ring: sort from the Avenger | same | — |
| `/` | geo ring: camera to selection | same | — |
| `Delete` | ant farm / dropship: re-announce | that screen | `Toggledebugcamera`, still at `Ctrl+Delete` |
| `R` | read the whole resource strip | **the strip is on screen** | nothing — no bare `R` in this layer |
| `Tab` | enter the Avenger HUD layer | the ant farm is current AND the layer is not focused (one claim shared with tactical's mission gate) | nothing — see below, and the config removes the section's Tab line anyway |
| `E` / `Q` | next / previous soldier on the Armory, Customize and Bond screens | — (not claimed: the game's own binding under our config) | the vanilla Tab / LeftShift cycle, moved rather than displaced |
| `Escape` | leave the layer | an ambient recipe is focused | the pause menu, for that one press |
| `Backspace` | the focused graph node's secondary action (including voice preview) | a focused recipe whose current node offers `OnSecondary` | the game's Backspace only on that node; elsewhere it passes through |
| `+` / `-` (also `Shift+=` and numpad `+`/`-`) | adjust the focused control — the kernel's `TryAdjust`; today the trading post's sell quantity | ANY menu recipe focused, in either layer | `=`/`-`: nothing, no binding in any input section; numpad `Add`/`Subtract`: tactical `SpeedUpGame`/`SlowDownGame`, only while a menu holds the keyboard mid-mission, still at `Ctrl+numpad` |

⚠ **`+`/`-` exist because a sheet spends the arrows.** Left/Right already adjust a focused
slider or spinner (`OnArrow` tries `TryAdjust` first), but inside a `GraphSheet` they are column
travel and cannot be both — and the trading post's sell quantity is a table row that adjusts,
whose vanilla keys are exactly the row's `FXS_ARROW_LEFT`/`_RIGHT`. The claim is general
(`OnAdjustKey`), not per screen: silent on a node with no `OnAdjust`, like every refusal here.

⚠ **Bare `Tab` on the ant farm was dead in retail even before the config change.** The
forwarder at line 468 reaches `FXS_KEY_TAB`, and every handler on that screen that could act on
it does not: `UIAvengerHUD.OnUnrealCommand`'s Tab case is compiled out under
`` `if(`notdefined(FINAL_RELEASE)) `` (`UIAvengerHUD.uc:588` — it opened the personnel list in
dev builds, beside `P`), `UIFacilityGrid` never handles Tab, and `UIAvengerShortcuts` cycles
categories on the bumpers only. Since the config moved the section's Tab command to `E`, no
strategy screen has a native Tab left; the menu recipes take it as the stop cycle while
attached, and the ant farm's claim is the layer entry.

⚠ **`R`'s gate is the strip's own visibility, and that is doing two jobs.** It keeps the claim out
of tactical, where `R` IS bound, without the module knowing anything about layers; and it keeps
the key off the save-name text field, since `UISaveGame` falls to `UpdateResources`' `default:`
and hides the strip. Both fall out of the GAME's answer rather than an exclusion list of ours —
which matters, because an exclusion list is a thing that goes stale. See
`surfaces/avenger.md` §Resource strip.
