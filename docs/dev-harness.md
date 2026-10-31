# Dev harness — the loopback server and how to drive it

The rules that outlive any one endpoint stay in `CLAUDE.md` (double-gating, the game-thread rule,
the shared-harness rule, the System32 curl rule). This file is the manual: what each endpoint
answers, what the selectors mean, and how to drive a session end to end without eyes or hands in
the game.

## The server

Loopback HTTP server for observing and driving the live game — sibling of the RTAccess (8772) and
CyberAccess (8773) harnesses; this one owns **8774**. Source: `src/dev_server.cpp`.

**Double-gated.** Compile: `-DX2ACCESS_DEVSERVER=ON` (the justfile's `devserver` variable, ON for the
local tree; OFF means the TU is not built and a release `.asi` holds no socket code). Runtime: a
marker FILE, `Binaries/Win64/scripts/X2Access.devserver` (optional first line = port override) — a
file rather than an env var because neither Steam nor `just run` inherits our environment, and it
survives a relaunch. `just arm` / `just disarm` write it.

**Threading.** The accept thread owns the socket and never touches engine memory. Engine-facing
endpoints queue a job that `DevServer::Pump()` drains on the GAME thread, first statement of the
`EventDispatch::SetFramePump` lambda — the same rule as outbound ProcessEvent calls, extended to
reads. `/health`, `/speech` and `/log` answer **off-thread** from process-memory rings, which is what
makes them useful when the game thread is the broken thing: a launch that fell into a `GetQueryData -
Timed out while waiting for GPU to catch up` loop left every game-thread endpoint timing out for
minutes while `/health` kept answering with a rising `pump-age-ms` and `/log` named the cause. A
game-thread timeout therefore means "the game is not running frames", NOT "the harness is broken" —
check `pump-age-ms` first, and remember the pump rides ProcessEvent traffic, so it is only as alive
as the game is. `Stop()` runs under the loader lock, so it does a **bounded** 1 s wait on the accept
thread and detaches rather than `join()`ing.

Endpoints:

- `/health` — build stamp and `pump-age-ms`.
- `/speech?since=N` — every line `TTS::Speak` was handed, cursor-paged.
- `/log?since=N&tail=N` — the ASI log ring, cursor-paged.
- `/status` — movie + load screen, the screen stack's gate flags, the attached recipe, the
  tile-reader gate, the dropship line, and a top-first dump of `UIScreenStack.Screens` with each
  entry's flags decoded (not-inited and removed entries shown too — the point is to answer "what is
  on screen" for a screen that has no recipe yet).
- `/tutorial` — replay frame, whether the player or the replay owns controls, the on-rails rejection
  count, the modal, and every active world message with its RAW text beside what the reader would
  SPEAK. That pair is the point — markup translation is the half a screenshot cannot check.
- `/exec` — a console command (POST body, or `?cmd=`).
- `/input?keys=Down,Down,Enter` — synthetic keys. Default tier is **SendInput scancodes** after
  raising the game window, which drives the WHOLE game (UE3 reads Raw Input, so a VK-only synthetic
  press is ignored). `&mode=kernel` injects into the arbiter's own queue via
  `InputArbiter::InjectSynthetic`, driving ONLY our layer, working with the game backgrounded, and
  reporting per key whether a claim took it. `&hold=` and `&gap=` are milliseconds.
- `/pad[?press=A,DDown&hold=&gap=&engine=]` — a virtual controller. The press is merged into what
  `XInputGetState` returns, read by the mod's own pad layer, and, outside what that layer acts on,
  delivered to the game as `XComInputBase.InputEvent(FXS_BUTTON_*, PRESS)` then `RELEASE` on the live
  input object (`DevServer` -> `Gamepad`, game tick). The last part is what makes it work in a
  mission and on the Avenger: the engine's own XInput poll is not reliable across launches
  (`docs/input-map.md`, the controller). Bare, it prints the controller layer's state, including how
  many times the ENGINE called the filter, from how many threads, and how many of those carried a
  virtual press: a count that stands still while `presses` rises is the failing launch. Names: `A B X
  Y LB RB LT RT Start Back L3 R3 DUp DDown DLeft DRight LUp LDown LLeft LRight RUp RDown RLeft RRight`. Inputs
  joined with `+` are held together (at most three), so `press=LT+DUp` runs a chord; a lone `LT` is a tap. A
  virtual input reaches the game exactly when the CURRENT layer does not act on it. Bare `/pad` prints the
  layer (`none menu hud world targeting antfarm`). `engine=0` skips the
  script-level delivery. It needs a connected pad to merge into. `tools/xctl.ps1 pad A,DDown` drives it
  anywhere; prove a press by `/status` (screen stack) and `/props` (a soldier index), not by the
  counters.
- `/flashqueue[?index=N]` — what panels PUSHED into Scaleform, captured in flight by
  `src/flash_shadow.cpp`. For the whole retains-nothing family this is the only place the words
  ever existed, so it is the endpoint that answers "what does this screen actually say" for a
  screen `/gui` prints as wordless. Bare it lists every tracked panel with its cache index; with
  `?index=` it dumps one panel's recorded calls, operands typed and in order. Answers **off the
  game thread** (it is our own map, not engine memory), so it still reports when the game thread
  is the broken thing.
- `/screenshot[?path=&foreground=0&output=N]` — PNG of the monitor the game is on, via DXGI Desktop
  Duplication (`src/screen_capture.cpp`).
- `/gui[?depth=N]` — the widget tree under a screen: one line per `UIPanel` with class, address,
  state bits and the text it carries.
- `/props[?filter=<substring>]` — every property the object's class chain declares: **offset, type,
  name and live value**, grouped by declaring class. `?cdo=<Class>` reaches a class default object
  (`GameUI::FindDefaultObject`), the only way to see the companion subsystems' model. ⚠ The value
  is printed with markup STRIPPED: a localized template's `<XGParam:IntValue0/>` shows as a bare
  gap (`"Этаж  из"`, vanilla's `m_strLockedDescription` alike), which is the dump, not the loc
  file. To prove a template loaded whole, `/call` its expander (`fn=Fill1&cdo=X2AccessVocab`).
- `/call?fn=<Function>` — invoke a script function on the selected object. Built for the recurring
  shape of accessibility bug in this game: not a missing feature but an UNREACHABLE one — the game
  already has the function and only calls it from a branch a mouse+keyboard player never takes
  (`UIFacilityGrid.InitializeNavigationGrid` behind `ISCONTROLLERACTIVE`; `UIToDoWidget`'s list behind
  mouse handlers). Turns "would ungating that work?" into one HTTP call instead of a
  build-deploy-relaunch per hypothesis. Arguments go in as `&arg=<value>`, once per declared argument
  in declaration order; a count mismatch is refused with the signature printed. Passing them is safe
  for the reason the parameter semantics below give: parameters are shallow-`memcpy`'d and the
  destroy pass skips everything under `ParmsSize`, so a buffer the harness owns and keeps alive
  across the call is all that is required. ⛔ Only values it can actually WRITE are accepted — string,
  int, float, bool, byte. An object, struct, array, name or delegate argument is refused, because the
  only thing the endpoint could put there is a zero, and a zeroed object pointer is a null deref
  inside script in the very session it exists to observe. A declared RETURN value was never a
  disqualifier: the engine writes it into our block and reads nothing from it. **Arguments are what
  make the Scaleform side reachable** — every fact about a live movie is read through path-string
  getters, so `just dev-flash ActionScriptString "_global.Selection.getFocus"` is a question one can
  ask. Parameters are counted REFLECTIVELY (`UFunction`'s own `Children`, filtered on `CPF_Parm`)
  rather than from `ParmsSize`, which the generated header buries inside `UFunction`'s padding.
  Class-scope functions only — a state-scoped one needs `GameUI::FindStateFunction`.
- `/flash?field=<name>` — the same read, but **panel-relative**: it resolves the SELECTED panel's own
  `MCPath` and reads `<MCPath>.<field>`. That is Firaxis' own composition —
  `UIMCController.GetString(Field)` is literally
  `Movie.GetVariableString(Panel.MCPath $ "." $ Field)` — so a child field nests the way their comment
  says. Both handles come out of reflection, so this verb transcribes no offsets of its own. `&fn=`
  picks the getter and therefore the return type — `GetVariableString` (default) / `Number` / `Bool`,
  or `ActionScriptString` / `Int` / `Float` / `Void`; `GetVariable` and `Invoke` are REFUSED by name
  rather than guessed at, because their parameter blocks differ (an `FASValue` struct, and an array of
  them) and reading either through the shared block would print half a pointer as a number. Omitting
  `&field=` asks for the MCPath itself, the cheapest test that the panel resolves in the movie at all.
  **Why it exists**: this is the answer to the hard rule that *our own readout is never evidence about
  the game's state*. For every screen that pipes its text into Flash and retains none of it, "what
  does the screen actually say?" had no answer short of asking a sighted person. ⚠ Blank is
  AMBIGUOUS: an unresolved path answers exactly like an empty field, so the endpoint says so on every
  blank reading.

  All four take the same selector — `?screen=<Class>` (the screen stack, topmost when unnamed),
  `?instance=<Class>` (newest instance anywhere in GObjects, reaching objects the stack never holds —
  `XCom3DCursor`, a cheat manager, a tooltip manager; falls back to the CDO when nothing live
  matches), or `?ptr=0x<addr>` copied out of an earlier dump and re-validated against GObjects before
  it is walked. So the loop is: `/gui` to see the tree, `?ptr=` from a line of it, `/props` to read
  that widget out, `/call` to make it do something — and `/flash` to check what it actually put on
  screen.

An unknown path returns the endpoint list. `/speech` and `/log` are cursor-paged: poll with the
returned `cursor:` value for exactly-once delivery.

Driving it: `just arm`, then `just dev-health` / `dev-status` / `dev-speech` / `dev-log` /
`dev-exec "<cmd>"` / `dev-input <keys> [kernel]` / `dev-shot` / `dev-gui [depth] [select]` /
`dev-props [select] [filter]` / `dev-call <fn> [select]` / `dev-flash <fn> <arg> [select]` /
`dev-field [select] [field] [fn]` / `dev-flashqueue [index]`, or `just dev '<endpoint>?<query>'`
for anything else.
⛔ Scripts must call curl as `$env:SystemRoot/System32/curl.exe` — bare `curl` on PATH resolves to
MSYS2's cygwin build, whose path translation mangles URL arguments into blanks. The justfile's `curl`
variable already does this.
⛔ **Shared-harness rule** (from RTAccess): do not drive the live game while another agent or session
is driving it — give the user manual steps instead.

### `/exec` and the console — three facts established live

1. **The retail binary has a CheatManager.** `XComPlayerController.AddCheats` and
   `XComGameInfo.AllowCheats` both sit inside `` `if(`notdefined(FINAL_RELEASE)) `` blocks, so this
   was an open question. `obj list class=XComTacticalCheatManager` in a live mission returns
   `XComTacticalController_0.XComTacticalCheatManager_0` — so retail is NOT a FINAL_RELEASE build and
   all **1246 `exec function`s** in the SDK sources are reachable by name. This is the one place
   XCOM 2 is *easier* than both sibling projects: no CET-Lua bridge, no `Mono.CSharp`.
2. **`ConsoleCommand`'s return value is useless here** — empty even for commands that clearly ran.
   Output goes to the game's `Launch.log`, so `/exec` snapshots that file's size, runs the command,
   and returns what got appended.
3. **Route: `WorldInfo.GetWorldInfo()` (static native) → `GetALocalPlayerController()` (on Actor) →
   `ConsoleCommand(cmd, bWriteToLog)`.** No object cached across frames, so the GC rule holds. The
   param struct is `#pragma pack(4)`: `ReturnValue` sits at **0x14**, directly after the bool
   bitfield with no padding; natural alignment would put it at 0x18 and read garbage. Pinned by
   `static_assert`.

### ProcessEvent's parameter semantics (decompiled, moduleBase+0x143B50)

Needed before any call that passes or returns an `FString`:

- Parameters are **shallow-`memcpy`'d** into ProcessEvent's alloca'd frame — not per-property
  deep-copied. So a buffer we own is passed by pointer.
- The `ConstructorLink` destroy pass only runs `DestroyValue` on properties whose
  `Offset >= ParmsSize` — true locals, **never parameters**. So the engine never frees an `FString`
  we passed in; ours merely has to outlive the call.
- The return value is written **straight into our block**: ProcessEvent passes
  `Parms + ReturnValueOffset` as the native's result pointer. It arrives in an ENGINE-allocated
  buffer, so reading it is free and freeing it would need `appFree`. `ConsoleCommand` deliberately
  leaks it — a few dozen bytes per dev-only call beats calling the wrong deallocator.

### `/gui` and `/props`: reflection, not transcription

`src/gui_dump.cpp` reads everything through the engine's own reflection spine (`UStruct::Children` →
`UProperty::Offset`/`ArrayDim`/`ElementSize`, the property's CLASS name for its type,
`UBoolProperty::BitMask`, `UByteProperty::Enum` for enum literals), so it hand-transcribes **no
offsets of its own** — the four raw primitives it cannot get that way (`FindStackScreen`,
`FindAnyInstance`, `IsLiveObject`, `ReadChildPanels`) are dev-only exports from `game_ui.cpp` built on
offsets already `static_assert`-pinned. That is what makes it authoritative where the SDK dump is not:
the numbers come from the class the running process actually has, under whatever mod set is loaded.
The Highlander's rebuild of `UIShellSoundtrackPicker` is exactly the case a dump cannot answer and
this can. It also prints state bits **by name** rather than decoding a flag dword at a fixed offset.

**A screen's `ChildPanels` is not its children.** It is a flat REGISTRY of every panel on that screen,
itself included at index 0 — `UITacticalHUD` lists all 159 of its panels, among them a `UIStatList`
that really hangs four levels down inside `UITacticalHUD_ShotHUD`. Panels below the screen do list
only their own children, so a naive recursive walk *looks* right and quietly reparents everything the
recursion happens to reach last. The link that means the same thing at every level is
`UIPanel.ParentPanel` (+0x2E0), and `/gui` filters on it; whatever the walk does not reach is listed
with its actual parent rather than dropped.

Confirmed `UIPanel` offsets against the running class: `bIsInited` 0x10, `bIsVisible` 0x20,
`bIsFocused` 0x40, `bIsRemoved` 0x80, `bHasTooltip` 0x100, `bIsNavigable` 0x200 in the dword at
+0x284, `CachedTooltipId` +0x2AC, `Movie` +0x2B8, `ChildPanels` +0x2E8. Dumping a screen we already
have a recipe for is the calibration check worth running after any SDK regeneration.

## Live testing without eyes or hands in the game

`tools/xctl.ps1` is the short way through everything below, and it needs neither `just` nor a
hand-built curl line: `build`, `stop`, `start`, `status`, `key Down,Enter`, `pad A,DDown`, `sr`,
`wait <regex>`, `log`, `shot`. A press prints the speech it caused as `SR> line` and returns when
speech goes quiet. Exit 5 means the process is gone or the frame pump has stopped, which includes the
player choosing Exit to Desktop. `key` refuses the keys that are JAWS commands. ⚠ It never tests
what a screen reader said: `/speech` is what the mod handed over, whichever backend took it.

The dev server makes a session drivable end to end from outside: `/speech` is exactly what the screen
reader was handed (poll with the cursor), `/status` is the gate + screen stack, and
`tools/send-game-keys.ps1` injects scancode-level keys (SendInput with KEYEVENTF_SCANCODE — WScript
SendKeys silently drops arrow keys for this game, and the INPUT struct must be 40 bytes or SendInput
injects nothing). Screenshot via `/screenshot`, then read the image. This loop found and fixed the
compass 180° rotation in one session without the player touching the keyboard.

