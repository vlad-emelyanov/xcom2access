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

HUD mode's own keys: `Ctrl+Tab` toggles the layer — claimed whenever a mission is active, not
merely while attached, because an UNCLAIMED Ctrl+Tab is not a dead key (leniency fires the bare
Tab binding under it — next soldier, or next target while the shot HUD is raised), so the chord
is swallowed mission-wide and the handler speaks its refusals ("HUD unavailable."). `Escape`
while HUD-focused exits to world mode ("Map") and is claimed only then — on every menu recipe
Escape stays the game's own close key. While focused the navigator's generic set applies
(arrows, Tab/Shift+Tab between stops, Enter, Space, Home/End), which displaces vanilla Tab =
next soldier for exactly as long as the player stays in the layer. The `LeftShift` swallow also
holds while HUD-focused (see below), or the Shift release at the end of Shift+Tab would fire
`PrevUnit()`.

**UE3 matches modifiers LENIENTLY.** A `KeyBind`'s `Control`/`Shift`/`Alt` are modifiers the chord
REQUIRES; modifiers the player merely happens to hold are NOT checked.
`bIgnoreCtrl`/`bIgnoreShift`/`bIgnoreAlt` are the opt-in SUPPRESSORS — `Input.uc`: *"if true, the bind
will not be activated if the corresponding key is held down"* — and the entire game config uses them
exactly twice: on `R`, so `Shift+R` reaches ErrorReport without also reloading, and on `F4`, so Alt+F4
does not also fire the game action. (Those exist to SUPPRESS a bare bind, which is only necessary
because matching is lenient.) So a bare binding fires under ANY modifier. **Every modifier chord we
advertise on a key that carries a vanilla bare binding MUST be claimed, or it double-fires.** Ctrl+Tab
therefore enters/exits the HUD layer and plain Tab is claimed ONLY while the layer is focused.

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

**Three bare letters are taken on that argument**, all by the object scanner and all gated on
`TileReader::IsActive()`, so they stand down the moment world mode does: `B` next marker, `O`
re-announce the selection, `J` next cover position (`Shift`+each for the previous one). Each
displaces a generic `X_Key_Press | onrelease X_Key_Release` forwarder — push-to-talk, the Chosen
Info screen, and one of the ability hotkey slots — and each of those stays reachable at
`Ctrl`+the same letter, which is the whole leniency bargain above. ⚠ `J` is bound in **both**
`[Engine.PlayerInput]` (`:183`) and `[XComGame.XComTacticalInput]` (`:306`); a grep of one section
would have reported it half-free, which is the `U` trap below in a different disguise.

⚠ **The one thing leniency does NOT hand us is a free `Shift` — so we take it.** `LeftShift` is bound
in `[XComGame.XComTacticalInput]` and `Key_Left_Shift`'s RELEASE calls `PrevUnit()` UNCONDITIONALLY
(`XComTacticalInput.uc:2147`, state `ActiveUnit_Moving`), so every `Shift+` chord cycled the soldier
when the player let go. It never showed up in testing because the tutorial runs `InReplayPlayback`,
whose copy of the same function (`:587`) is a bare `return false` — the one place being a replay HID a
bug instead of causing one. **The fix is a SWALLOW of the modifier itself**, not of the function: the
arbiter accepts a claim on a modifier key (`{VK_LSHIFT, ModNone}`, side-resolved from lParam's scan
code because WM_KEYDOWN reports the generic `VK_SHIFT`), so while our tactical layer owns input the
engine never sees LeftShift go down or up. **Our own chord matching is untouched**, and that is the
linchpin: `GetKeyState` reflects the state the OS updated when the message was pulled off the queue,
not what the window procedure then did with it.
⛔ **`object-scanner-recon.md` §4.6's Option B is WRONG and must not be tried**: the mouse-gate
trick does not apply verbatim to `Key_Left_Shift`, which takes an `int ActionMask` and returns a bool.
`MouseGate`'s thunk consumes a lone `EX_EndFunctionParms`; here it would meet a parameter expression
instead, leave the VM's `Code` pointer mid-expression, and hand the caller an unwritten return value.
Doing it properly needs `UStruct::Script`, an offset this project has never measured — and the swallow
needs no engine offset at all.
Two consequences: `RightShift` is deliberately NOT claimed (it carries no binding in the tactical
section). And the swallow costs `PressedKeys` its `LeftShift` entry, which is what `Key_Tab`'s
prev-soldier branch reads — so `Shift+Tab` is claimed and re-provided by
`X2AccessObjectScanner.PrevSoldier`, reproducing `XComTacticalInput.IsManualUnitSwitchAllowed`'s guard
rule for rule. `Ctrl` remains the safe modifier for anything that does NOT want to displace a vanilla
key: `Key_Left_Control` merely toggles the pathing pawn's waypoint mode on press and untoggles it on
release.

**`LeftAlt` is claimed PASS-THROUGH in world mode** — `Claim::passThrough`, a third kind beside
swallow and observe: the handler runs from the tick like any claim's, and the game receives the key
as well. The tile cursor speaks the game's target preview on it (`surfaces/tactical.md`), and the
game still gets to raise its own strip (`Left_Alt_Key_Press`, bound at `DefaultInput.ini:396`, is
how `UITacticalHUD_EnemyPreview` hears the key). A swallow would have cost more than the strip:
`F4` is bound with `bIgnoreAlt=True` (`:380`) precisely so that Alt+F4 reaches `ALT_F4_QUIT` (`:390`)
instead of `F4_Key_Press`, and an engine that never saw Alt go down would fire the bare binding and
skip the quit. Presses only — a pass-through is never latched, so the release and the repeats are the
game's. Displaces nothing. `RightAlt` is left alone: the game's strip listens for the left key only.

⛔ **Tab stays the game's, in both of its meanings.** `eTBC_NextUnit` in world mode
(`UIUtilities_Input.uc:451`) and `NextTarget` while the shot HUD is raised (`UITacticalHUD.uc:459`)
are the player's own bindings and the layer does not take them: switching soldiers is narrated by
POLLING the controller instead, which leaves the binding untouched AND catches the mouse click, the
turn start and the death-driven re-select that a key claim never could. The one exception is
`Shift+Tab`, claimed and re-provided only because the Shift swallow costs `Key_Tab`'s prev-soldier
branch the `PressedKeys` entry it reads — a debt the swallow created, not a verb the layer wanted.

**Exactly two letter keys are free: H and I.** Measured across every input section of
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
`C E F G H I K L O Q R T V X Y Z` (`geoscape-recon.md` §7) — against tactical's two.

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
| `Tab` | enter the Avenger HUD layer | the ant farm is current AND the layer is not focused | nothing — see below |
| `Ctrl+Tab` | enter/exit the Avenger HUD layer | the ant farm is current (shared with tactical's mission gate) | the bare Tab it would double-fire, which is dead here |
| `Escape` | leave the layer | an ambient recipe is focused | the pause menu, for that one press |
| `+` / `-` (also `Shift+=` and numpad `+`/`-`) | adjust the focused control — the kernel's `TryAdjust`; today the trading post's sell quantity | ANY menu recipe focused, in either layer | `=`/`-`: nothing, no binding in any input section; numpad `Add`/`Subtract`: tactical `SpeedUpGame`/`SlowDownGame`, only while a menu holds the keyboard mid-mission, still at `Ctrl+numpad` |

⚠ **`+`/`-` exist because a sheet spends the arrows.** Left/Right already adjust a focused
slider or spinner (`OnArrow` tries `TryAdjust` first), but inside a `GraphSheet` they are column
travel and cannot be both — and the trading post's sell quantity is a table row that adjusts,
whose vanilla keys are exactly the row's `FXS_ARROW_LEFT`/`_RIGHT`. The claim is general
(`OnAdjustKey`), not per screen: silent on a node with no `OnAdjust`, like every refusal here.

⚠ **Bare `Tab` on the ant farm is dead in retail, which is the only reason it is claimed.** The
forwarder at line 468 reaches `FXS_KEY_TAB`, and every handler on that screen that could act on
it does not: `UIAvengerHUD.OnUnrealCommand`'s Tab case is compiled out under
`` `if(`notdefined(FINAL_RELEASE)) `` (`UIAvengerHUD.uc:588` — it opened the personnel list in
dev builds, beside `P`), `UIFacilityGrid` never handles Tab, and `UIAvengerShortcuts` cycles
categories on the bumpers only. This does NOT generalise: in tactical Tab is next unit, and
inside a room the shortcut submenu is a `UIList` with its own Tab. Measure the screen, not the
layer, before claiming it anywhere else.

⚠ **`R`'s gate is the strip's own visibility, and that is doing two jobs.** It keeps the claim out
of tactical, where `R` IS bound, without the module knowing anything about layers; and it keeps
the key off the save-name text field, since `UISaveGame` falls to `UpdateResources`' `default:`
and hides the strip. Both fall out of the GAME's answer rather than an exclusion list of ours —
which matters, because an exclusion list is a thing that goes stale. See
`surfaces/avenger.md` §Resource strip.

