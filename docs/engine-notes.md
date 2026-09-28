# Engine notes — the measurement record

What was measured about Firaxis' UE3, in full, with the decompiled and live-read detail behind it.
`CLAUDE.md` states the RULES this produced in a line or two each and points here; this file is the
evidence. When a re-measurement changes something here, restate the rule there in the same pass —
and note that a rule with no measurement under it any more is a rule to delete, not to keep.

## What was measured

- Heavily modified UE3, 64-bit only, Scaleform GFx UI. No anti-cheat; Firaxis ships EasyHook64.dll
  themselves — injection is tolerated.
- ProcessEvent: UObject vtable slot 72, address `moduleBase + 0x143B50` (found via Ghidra +
  `tools/OffsetFinder`). GNames/GObjects offsets live in the generated SDK constants;
  `src/sdk_globals.cpp` validates them with sanity heuristics before init proceeds.
- ⛔ **Every one of those addresses is per BUILD, and lives in the table in `src/game_build.cpp` —
  never as a constant in the TU that uses it.** A second retail build of the same game exists.
  The GOG release (`XCom2.exe`, PE timestamp `0x5B9A17C4` = 2018-09-13, `SizeOfImage` `0x20CA000`,
  32,008,704 bytes, no `steam_api64.dll` reference) is the same source RELINKED, not a different
  game: entry points 0x90 apart, 2 KB of file size between them. Everything pinned moved, and
  before the table existed the mod died on it in `SDK::Init` with *GNames validation failed* —
  correct behaviour, and total silence, since every word the mod speaks is native. An image the
  table does not know is NOT refused: it gets the reference row and a named warning, because
  refusing would break a re-signed copy of a build we do support, and the validation is what
  actually decides. Measured 2026-09-20 from a tester's copy of the binary, each
  anchor twice, by methods sharing no assumptions. `tools/derive_offsets.py` is that measurement,
  re-runnable against any third build: `--reference` the exe whose RVAs are known, `--target` the
  new one. ⛔ Keep a copy of any non-Steam exe somewhere OUTSIDE this repo (it is 32 MB of someone
  else's game) — without the file there is nothing to re-derive from.

  | Anchor | Steam (`0x621562FA`, `SizeOfImage 0x20C7000`) | GOG (`0x5B9A17C4`) | delta |
  |---|---|---|---|
  | GNames | `0x1DAF450` | `0x1DB2490` | +0x3040 |
  | GObjects | `0x1DAF4A8` | `0x1DB24E8` | +0x3040 |
  | ProcessEvent | `0x143B50` | `0x144310` | +0x7C0 |
  | `GFullScreenMovie` global | `0x1DB8900` | `0x1DBB950` | +0x3050 |
  | `FFullScreenMovieBink` vtable | `0x13E9B28` | `0x13EA7B8` | +0xC90 |

  **How each was derived, and what confirms it.** A global is reached by RIP-relative operands, so
  the bytes AROUND a Steam reference are the signature: wildcard the 4-byte displacement, find that
  shape in the GOG image, read ITS displacement. 105 of 400 sampled sites matched for GNames and
  166 for GObjects, **unanimously** — no site voted for any other address. A function is matched on
  its own opening instructions with capstone masking every byte that encodes an address.
  Confirmations, each independent of the derivation:
  - **Reference symmetry.** The same global is reached from the same NUMBER of code sites in both
    images: GObjects 870 and 870, the movie global 2 and 2, GNames 2156 and 2155 (the scan
    over-approximates on purpose — it counts any 4-byte window that WOULD reach the address, so a
    site or two of that is noise). A wrong address lands in unrelated data and the counts are not
    in the same league.
  - **`GObjects − GNames` is `0x58` in both**, as it must be for two adjacent globals.
  - **ProcessEvent is pointed at by exactly 1612 stored pointers in each image**, and the vtable
    index those sit at has the same distribution in both, peaking at **72** — so the slot index the
    hook patches is measured on the new build, not assumed.
  - **The two builds' ProcessEvent is the same function**: 246 instructions in lockstep to the
    `ret`, and **zero** differing structure displacements. Every `[reg+0xNN]` it touches — the
    UObject and UFunction layout the whole mod is transcribed against — is identical.
  - The movie vtable, which no code match reached, was ported through its CONTENTS instead:
    signature-match the 16 functions it points at, find where GOG stores pointers to them, vote on
    where slot 0 must be. 16 of 16 agreed.
  ⚠ The one check that FAILED was a test, not a build: looking for an RTTI locator at `vtable[-1]`
  scored 0% on **both** images, Steam included. Run every new measurement against the build we know
  works before believing what it says about the one we do not.
  ⚠ Residual risk not settled by any of this: the GOG install's `.upk` files are a 2018 build's.
  The Highlander replaces `XComGame`, `Engine` and `Core`, which is where nearly everything we
  transcribe lives, so those layouts are the Highlander's on both stores — but a package it does
  not replace has not been compared.
- Runtime `FString` is `TArray<wchar_t>` (UTF-16) even though the generated SDK declares `char` —
  `ue3_reflection.cpp` handles the conversion.
- **The UE3 audio device is alive beside Wwise, and its lifetime rules are Firaxis' own**
  (decompiled 2026-09-17 for the wall tones: `UAudioDevice::GetSortedActiveWaveInstances` at
  moduleBase + 0x416AD0, `UAudioComponent::UpdateWaveInstances` at +0x431830, `StopSources` at
  +0x42E130, the `AudioComponent` natives through the `UAudioComponentexec*` lookup table at
  +0x1C2EAA0). Per device tick:
  - Before updating a component it recomputes `SoundCue.Duration` from `FirstNode->GetDuration()`
    (under a global flag pair, or whenever it reads 0) and **STOPS the component when
    `PlaybackTime > Duration × 16` unless `Duration ≥ 10000`** — the log line, when the log
    category is on, is *"Sound stopped due to duration"*. A loop's cue carries `Duration = 10000`
    for exactly this (`tools/make_sound_upk.py`).
  - The active set is the priority-sorted wave-instance list from the first entry whose volume is
    ABOVE 0.0001; everything under it is culled and its source stopped. A culled instance keeps
    `bIsFinished` clear only if it is `LOOP_Forever` or its component has
    `bShouldRemainActiveIfDropped`. A source whose instance was not heard (volume above 0.0001)
    for 60 ticks is stopped too, active or not. ⛔ So a looping bed idles ABOVE the threshold,
    never at 0 (`X2AccessWallTones.SILENT`).
  - `AdjustVolume(Duration, Level)` (+0x3F2E00) has no playing guard: with `Duration ≥ 0` it sets
    `AdjustVolumeStartTime = PlaybackTime`, `AdjustVolumeStopTime = PlaybackTime + Duration` and
    `AdjustVolumeTargetVolume = Level`, nothing else. The per-tick multiplier (+0x4055D0) lerps
    from `CurrAdjustVolumeTargetVolume` and writes it back only once `PlaybackTime` passes the
    stop time — so the current level is the last LANDED level, and it defaults to 1.0. `Play()`
    (+0x424620) zeroes `PlaybackTime`, resets the fade state and the wave instances, refuses
    while the cue's `MaxConcurrentPlayCount` is reached, and leaves the adjust state alone;
    `IsPlaying()` is `bWasPlaying && !bFinished`.
- **The ProcessEvent hook sees only NATIVE→script dispatch.** A full HUD refresh (Tab switched
  soldier, `m_arrAbilities` 4 → 5, every panel repainted) produces not ONE UI script function — no
  `UpdateAbilitiesArray`, no `SetStats`, no `OnUnrealCommand`, no `RealizeTargets`. What arrives is
  the engine calling into script: `Tick` / `PostRender` / `HandleInputKey` / `BeginState` on
  `XComTacticalInput`, `FilterButtonInput` on `UIMovie_2D`, and the simulation surface the native
  visibility and pathing systems call — `CallAbilityMeetsCondition`,
  `GetVisibleFlankingEnemiesOfLocation`, `NumActionPointsForMoving`, `TargetIsEnemy`,
  `UpdateGameplayVisibility`, `GetEnvironmentDamagePreview`. That is UE3 as documented: a
  script→script call runs through `CallFunction` → `Function->Func` (`ProcessInternal`), never the
  virtual slot 72 we patch. **So "watch a UI function by name" is not an available technique**, and
  the obvious narration tap — `UIWorldMessageMgr.Message`, `UIEventNotices.Notify`/`NotifyBanner`,
  `UITacticalHUDMessageBanner.SetBanner` — cannot be HOOKED this way.
  - ⚠️ But `UIWorldMessageMgr` can be **POLLED**: it RETAINS every message it is showing in
    `m_arrMsgs` (0x450, stride 0x78, text at +0x10) with `m_arrActiveMessages` (0x460) indexing the
    ones on screen — so damage numbers, crit labels, "Unconscious", the `UITacticalTutorialMgr`
    cursor-help strings and the tutorial's anchored blades are readable after all. Only the
    *unanchored* HUD blade is genuinely lost to Flash. `UIEventNotices` and
    `UITacticalHUDMessageBanner` retain nothing.
  - ✅ **A NATIVE function CAN be hooked individually, and it needs no hooking library**: its
    `UFunction::Func` (+0xF0) already holds its exec thunk, and that one field is what BOTH
    dispatch paths call — `CallFunction` for a `FUNC_Native` callee, and ProcessEvent itself
    (`pcVar3 = *(code **)(param_2 + 0xf0)`, decompiled at moduleBase+0x143B50). So chaining one is
    a POINTER WRITE — save the original, write ours, call theirs — the same mechanism
    `mouse_gate.cpp` already uses to SUPPRESS, used instead to OBSERVE. `src/flash_shadow.cpp` is
    the worked example. ⛔ Two limits keep this from being the general answer: the thunk receives
    the frame with parameters STILL UNEVALUATED (`Code` points at bytecode, so reading an argument
    means replaying the VM's param pass — the trap `object-scanner-recon.md` §4.6 names), so
    it is only worth it when the payload is reachable from `this` or from engine state; and the
    per-function trick does not transfer to SCRIPT functions, whose `Func` is the single shared
    `ProcessInternal` (pointing that at a thunk is the whole-call-graph hook below, not a targeted
    one).
  - Reaching script→script traffic would need a second hook on `ProcessInternal`, which would hand
    us the whole script call graph and is the one thing that would change this calculus. It is **not
    virtual** — the UObject vtable block around slot 72 matches UE3's declaration order exactly with
    no gaps (72 ProcessEvent, 73 ProcessDelegate, 74 ProcessState, 75 ProcessRemoteFunction,
    76 InitExecution, 77 GotoLabel, 78 GotoState, 79 CallFunctionByNameWithArguments), so it needs
    an INLINE hook (the first real use for MinHook, not vendored yet). Its address needs no
    signature scan: `UFunction::Func` (+0xF0) of any SCRIPT function is the dispatch target, and
    ProcessEvent calls exactly that at `0x140143E40` — read it once at init and the hook survives a
    game patch. The frame it receives carries the params ALREADY EVALUATED, so existing reflection
    reads apply unchanged; layout under pack(4) is `Node` +0x14, `Object` +0x1C, `Code` +0x24,
    `Locals` +0x2C (the `Code` offset cross-checks with what `mouse_gate.cpp` derived independently
    from `execSetLocation`). Native calls would NOT appear in such a tap (a native's `Func` is its
    exec thunk), and script calls made through ProcessEvent would appear TWICE.
- ⛔ **A `localized` var can be declared on TWO classes and populated on only ONE — and the empty one
  fails silently.** `m_sShotChanceLabel` / `m_sCritChanceLabel` are declared by both
  `UITacticalHUD_InfoPanel` (`:43`) and `UITacticalHUD_ShotHUD` (`:40`), which use them identically;
  but `XComGame.<loc>` ships the values under **`[UITacticalHUD_ShotHUD]` only**, so the InfoPanel's
  copies are `""` at runtime — as is EVERY localized string on that class (`m_sUnavailable`,
  `m_sNoTargetsHelp`, `m_sFreeAimingTitle`) while its plain defaults (`HitMCPath`) hold their
  values. Reading the empty pair costs more than a missing caption: with nothing to tell them apart,
  a sniper shot whose hit and crit both sit at 60 speaks the same number twice, meaning two
  different things. **The tell is a DOUBLE SPACE**, because `label @ value` still inserts its
  separator when the label is empty; a `grep -P '  '` over the speech ring finds it, and that is
  part of the post-run audit. When sourcing a word from a class, verify the CDO actually holds it
  (`just dev-props "cdo=<Class>"`) rather than trusting that the `.uc` declares it and the loc file
  contains the key somewhere.
- ⛔ **A list row in this game retains no text — that is the house style, not a quirk of one
  screen.** `UIPersonnel_ListItem`, `UIInventory_ListItem`, the debris room's picker rows and
  `UIFacility_RoomFunc` all take their words as parameters and push them through
  `MC.BeginFunctionOp` / `AS_UpdateData` / `MC.FunctionString`, BYPASSING `UIButton.SetText` /
  `UIListItemString.SetText` — so the retained `Text` is EMPTY on a row that is visibly showing a
  name. What a row DOES keep is the data it was built from (`UnitRef`, `ItemComodity`,
  `StaffSlotRef`, a delegate), so the companion re-derives the words the way the row did. Two
  screens break the pattern and are the ones native can simply read:
  `UIChooseFacility.PopulateList` and `UIButton.SetText`-based upgrade buttons. Check which kind a
  row is before planning the read; do not read a delegate's FName as a label — it would say
  *"OnShowObituaries"* to a Russian player.
  - ✅ **THERE IS NOW A THIRD OPTION, and for a screen whose words cannot be re-derived it is the
    right one: catch them IN FLIGHT.** `src/flash_shadow.cpp` chains `UIMovie.CopyCommandQueue`
    (native `Func` swap, see the hooking fact above) and decodes the movie-wide
    `UIMovie.CommandQueue` (0x270, `TArray<FASValue>`) at the one instant it is whole — inside
    that call, which `ProcessQueuedCommands` makes once a frame immediately BEFORE clearing the
    array and invoking into Flash. ⛔ This is why POLLING that queue never worked and the failed
    attempt is recorded in `game_ui.cpp`: by the time anything else looks, it is empty. Batches
    are keyed by `UIMCController.CacheIndex` (via `UIPanel.MC`, 0x2D0), so a surface asks for its
    own panel's words by cache index; `just dev-flashqueue` prints the lot. Three rules it was
    built with: only batches carrying a non-empty STRING are kept (the per-frame stream is mostly
    `_x`/`_y`/`_alpha` churn, and dropping it is what makes an always-on capture affordable);
    `CLEANUP_OP` evicts, which is both the recycled-cache-index guard and — because
    `UIMCController.Remove` queues it with NO `EndOp` and therefore NO null delimiter — a batch
    that MUST be special-cased or the decoder swallows the next panel's text whole; and it
    captures the PUSH, so it knows the words and nothing about selection, disabled state, or
    anything an `.as` computed after receiving them. `UIAlert` is the first consumer.
  - ⛔ **A PANEL IS NOT PAINTED BY ONE BATCH, and the last batch is often a PICTURE.** Measured
    2026-09-03 on the staff popups. `UIAlert.GetOrStartWaitingForStaffImage` (`UIAlert.uc:3972`)
    fires an ASYNCHRONOUS photobooth request whose callback pushes a *later* `UpdateImage` batch
    carrying one operand — the headshot's texture path — onto the same `LibraryPanel.MC` the
    prose went through. Any reader taking "the newest batch carrying a string" therefore read the
    picture, dropped it as an asset path, and reported the screen as empty; the alert then fell
    to its weakest fallback and spoke a title alone. Seven alert names lost their words this way
    in four sessions of logs (`SoldierPromoted`, `ClassEarned`, `NewStaffAvailable(Small)`,
    `StaffInfo`, `StaffSlotFilled`, `ClearRoomSlotFilled`, `XPACK_Retribution`) — every popup
    carrying a soldier headshot or a Chosen icon. Multi-batch painting is ordinary besides:
    `BuildRetributionAlert` uses five batches, `BuildConfirmCovertActionAlert` four, and the two
    staff builders feed a child list (`UpdateFacilityList`) BEFORE painting the frame that labels
    it. **So a consumer either names the call it wants (`GetCall`) or reads them ALL (`GetCalls`)
    — there is deliberately no "the batch that painted this panel" accessor any more.** What
    makes reading them all sound is that panel identity is per screen instance:
    `UIAlert.BindLibraryItem` (`:1044`) spawns a fresh `UIPanel` per alert, and `CLEANUP_OP`
    evicts the index when it goes. ⚠ Push order is reading order everywhere EXCEPT a batch that
    feeds a child list, which must be pushed first and read last.
- Some screens are Flash-driven with no UIList (e.g. key bindings): neither layer can enumerate
  rows, and the same is true of anything a screen wrote into Flash and did not retain — a text
  field's caret and selection above all. The route in is `GFxMoviePlayer`'s own path-string API, ALL
  of it script-callable and therefore reachable by outbound ProcessEvent: `GetVariableString` /
  `GetVariableNumber` / `GetVariableBool` / `Invoke`, plus the `ActionScriptInt` /
  `ActionScriptString` / `ActionScriptObject` wrappers that INVOKE a method at a path (how
  `UIInputDialogue.AS_GetInputText` itself works — `Movie.ActionScriptString(MCPath $
  ".GetInputText")`). ✅ GFx's path resolution DOES reach AS2's global `Selection` accessors, prefix
  optional, and TextField properties (`.text`, `.length`, `.maxChars`, `.caretIndex`) read straight
  through `GetVariableString`. A path that does not resolve answers 0 or an empty string, so a probe
  needs a value it can tell apart from silence.
  ⛔ That is a VERIFICATION tool and must not become a reading strategy — `UIMCController.uc` labels
  those getters *"an immediate and blocking operation … very expensive, use sparingly"*, and the
  shipped game takes its own advice: two call sites in the whole of `XComGame`. Flash also holds
  only the rendered TEXT, never the semantics (which row is selected, what a number means, whether a
  button is disabled), and no field name in it can be `static_assert`ed — so the companion CDO stays
  the way words are read at runtime. It also cannot read an `XComScrollingTextField` AT ALL: its
  text is exposed through an AS2 `addProperty` getter, which `GetVariableString` cannot run, and
  the backing storage is not reachable by path either (measured live on `descriptionText`). **When
  the question is "what words did this panel get", the capture above answers it and this does
  not**; keep the path API for caret, selection and focus, which the capture cannot see.
- ⛔ **A full-screen `.bk2` movie is engine-native all the way down — no UObject, no reflection, no
  script.** `FFullScreenMovieBink` is a plain C++ object behind the global `GFullScreenMovie`
  (RVA `0x1DB8900`); it owns the Bink handle, the frame counter and the parsed
  `Movies/<name>.txt` cue track, and it draws its subtitles with native canvas calls
  (`XComEngine::RenderExtraSubtitles`). Script sees exactly two things about it, both booleans in
  disguise: `XComEngine.PlayMovie` starts one and `IsAnyMoviePlaying` says whether one is up. So
  **do not go looking for a UIScreen, a Flash path or a companion hook for anything Bink-related**
  — `src/movie_subtitles.cpp` is the only surface in this mod that dereferences a raw C++ object,
  and the rule it established is the one to copy if a second ever needs to: hard-code ONE address
  (the global), and validate it by its VTABLE before dereferencing anything through it, because no
  offset on a non-UObject can be `static_assert`ed and that check is therefore the entire pin.
- ⛔ **No gameplay event fires in the TUTORIAL — it replays history, it never submits it.**
  `X2GameRuleset.SubmitGameStateContext` (`:292`, `:401`) routes every context to
  `XComTutorialMgr.HandleSubmittedGameStateContext` instead of `SubmitGameStateContext_Internal`
  while `` `REPLAY.bInTutorial ``; on a match that handler calls `ResumeReplay()` /
  `StepReplayForward()` and returns `true`, DISCARDING the player's context, and
  `XComReplayMgr.StepReplayForward` merely advances `CurrentHistoryFrame` and calls
  `BuildVisualizationFrame` on a frame already in the history at mission start. So
  `ContextBuildGameState` never runs and `X2EventManager.TriggerEvent` is never reached — for our
  listeners or the game's own. Two consequences: **anything built on the event bus must be tested on
  a normal mission** (Gatecrasher — campaign with the tutorial checkbox off), and the tutorial
  reader's `UIWorldMessageMgr` polling is not a nicety there but the ONLY narration source.
  ✅ `X2AccessReplayNarration.uc` answers the gap: in a replay the history IS the event stream, so it
  walks frames instead of waiting for events, through the SAME builders.
- **Cinematic mode hides SOME screens, and the stack flag says nothing about which.** Read from
  source 2026-09-04, after the Chosen reveal read as three minutes of silence in a live Gatecrasher
  run (`X2Access.log` 12:42:50 → 12:45:54 — "UI gated … cinematic 1", one detach, no attach, and the
  player pressed Enter on an unread screen). `UIScreenStack.HideUIForCinematics()` sets
  `bCinematicMode` and then walks `Screens` calling `UIScreen.HideForCinematics()`, whose whole body
  is `if( !bShowDuringCinematic ) { super.Hide(); bIsVisiblePreCinematic = bPreviouslyViz; }`. So:
  - The hide is per screen, and it touches `bIsVisible` ONLY — inited, focused, navigable and the
    screen's place on the stack all survive it. A screen the cutscene took away is therefore
    indistinguishable from one it left alone by every bit the readiness predicate is allowed to use
    (the never-gate-on-`bIsVisible` rule). `bShowDuringCinematic` itself is the only tell.
  - Seventeen shipped classes declare it: `UIChosenReveal`, `UIPauseMenu`, `UIOptionsPCScreen`,
    `UILoadGame`, `UIDialogueBox`, `UIHackingScreen`, `UIDropShipBriefingBase`, `UIDropShipHUD`,
    `UIInventoryTactical`, `UITactical_Photobooth`, `UINarrativeCommLink`, `UIStrategyTutorialBox`,
    `UIWorldMessageMgr`, `UIAnchoredMessageMgr`, `UIMultiplayerChatManager`, `UIDebugInfo`,
    `UIRedscreen`. Half of those are surfaces we already read, so the layer-wide refusal was
    silencing the pause menu, the load screen, a dialog and the hacking pad mid-cutscene too.
  - The game agrees it is not a gate: `XComPresentationLayerBase.UIPauseMenu(bAllowCinematicMode,
    …)` refuses only on `ScreenStack.IsInputBlocked`, and takes cinematic mode as a PARAMETER.
  - Offsets: the stack's flag dword is at 0x60 (`IsInputBlocked` 0x1, `ScreensVisible` 0x2,
    `bCinematicMode` 0x8); `UIScreen`'s own is at 0x3E0 with `bShowDuringCinematic` = 0x10 —
    a different dword from `UIPanel`'s at 0x284, and pinned by `UIScreen::Package` (0x3D8) and
    `IdleDistortionAnimationLength` (0x3E4) in `sdk_layout_checks.cpp`.
- **Screen lifetime: a pointer outlives its screen, a GObjects index says nothing, and readiness is
  two bits.** The measurements behind the caching, corpse and readiness rules in `CLAUDE.md`:
  - GC moves on, and type-revalidation is not enough: after a world change the old screen is still a
    well-formed instance of its class for as long as GC leaves it allocated, so "is it still a
    `UIFinalShell`?" happily says yes about a dead one. Getting this wrong cost 63 seconds of dead
    silence after exiting a campaign, and it looked like a hang rather than a stale pointer. A cache
    may short-circuit a rescan only while the instance is still USABLE (focused, showing).
  - UE3 reuses freed GObjects slots, so a freshly spawned screen routinely sits at a LOWER index than
    corpses of its own class. Removal sets `bIsRemoved` (0x80) and clears nothing else, so a corpse
    still reads inited/visible/focused. "Take the last instance" latched onto a dead pause menu that
    passed every readiness test, read as empty, and left the layer silently detached with the menu
    open. The durable fix is walking `UIScreenStack.Screens` (0x6C, index 0 = the top; Push inserts
    at 0): a popped screen leaves the stack immediately, whereas as an OBJECT it lingers with every
    state bit intact until GC. `FindStackScreenRaw` walks those few dozen entries under one of two
    policies. `ClassMatch::Exact` compares class NAMES and resolves no `UClass` at all;
    `ClassMatch::Derived` — what makes a mod's `ModClassOverrides` subclass findable — must first
    resolve the name to a class object through a full GObjects pass, then chases each entry's
    `SuperField` chain. A resolved name is cached for the session and an unresolvable one backs off
    to a retry a minute apart, so the steady state is cheap either way. ⛔ But "the stack is small"
    is a statement about Exact; it is not a reason to assume Derived is free.
    GObjects scanning survives only as a fallback — for screens the stack does not manage
    (`UIDialogueBox` is permanent and never pushed) and for the window while the stack itself is
    being rebuilt — and the first fallback per class is logged, so "not stack-managed" is a fact in
    the log rather than an assumption.
  - Topmost is not reachable: starting a campaign pops the narrative screen and leaves
    `UIShellDifficulty` underneath inited, focused and top of the stack for the ~12 s the loading
    movie plays. The stack's own answer is its flag dword at 0x60 — `IsInputBlocked` (0x1) and
    `ScreensVisible` (0x2, so hidden when CLEAR) take the whole UI away — plus
    `XComEngine.IsAnyMoviePlaying`, which is not a UI flag at all.
  - `bIsVisible` (UIPanel flag dword bit 0x20) is FALSE on screens the game is plainly showing: a
    live, focused pause menu reads 0x259 and so does the exit-to-main-menu confirmation dialog over
    it — `bIsNavigable|bIsFocused|bIsInited`, no visible bit. Testing it meant silence at a modal the
    player then answered blind. `bIsFocused` is what the screen stack maintains for "top of the
    stack"; `UIDialogueBox`'s real state is its queue, so it is `bIsInited` plus a non-empty
    `m_arrData`. Child widgets differ: a BUTTON's `bIsVisible` is toggled deliberately and does mean
    hidden, and the ant farm's grid is the one screen-level exception because the GAME gates on it.
  - ⛔ **A covered screen is not a closed one, and the navigator must not treat it as one.** A push
    (personnel picker, armory, any popup) takes focus off the screen beneath, its recipe's probe
    fails and the navigator detaches — but the screen stays a live stack entry, and the player
    comes back to it. Detaching used to discard the cursor, so every return landed on the recipe's
    start node; on covert actions that was the list, where the next arrow emptied the slots just
    filled. The navigator now PARKS the cursor of a detached screen that is still live on the stack
    and resumes it when the same instance re-attaches, skipping the screen name (`ParkFocus` /
    `TakeParkedFocus` in `menu_navigator.cpp`). The park is keyed on the instance and dropped on
    the first poll that does not find it on the stack, the corpse rule above applied to a cursor;
    it keeps structural keys only (`GraphState::WithoutReferences`), since the widget pointers do
    not survive the gap.
- ⛔ **`UE3Reflect::ReadStringProperty` is the single choke point every companion payload passes
  through** — tile cursor, shot HUD, object scanner, hacking pad, every recipe's wire records, and
  `X2AccessNarrator`'s 24-line drain — and it read all of them into a fixed `wchar_t[2048]`,
  clipping anything longer and saying nothing. The XCOM archives' 39 entry titles came to **2051
  characters** against the 2047 that fit, and those four characters took the LAST record with them —
  the item card. The rows all read perfectly and Tab found no card stop, so the bug presented as
  *the recipe never built that stop*: to a blind player a truncated tail reads as *the screen has
  nothing more*, indistinguishable from the truth and therefore never reported. The length is now
  read first (`FStringLengthRaw`), the buffer sized to it, and an implausible count refused with a
  log line. The remaining `wchar_t[256]`/`[512]` buffers in the `game_ui*.cpp` family are
  per-WIDGET fields bounded by what fits on screen; the payload paths are not, and must never go
  back to a fixed buffer.
- **The tactical HUD holds no text.** Unlike the shell screens, every panel of `UITacticalHUD` is a
  write-only pipe into Flash: `SetStats` resolves name, rank, class, aim and effects out of the game
  state and hands them straight to `MC.BeginFunctionOp` / `QueueString`, retaining nothing
  (`SrcOrig/.../UITacticalHUD_SoldierInfo.uc:199`) — as do the shot HUD, info panel, enemy list,
  countdown and banner. So `/gui` on tactical prints 159 panels and almost no words, and the
  read-the-widget recipe pattern does NOT transfer. What IS readable: the ability bar
  (`UITacticalHUD_AbilityContainer.m_arrAbilities` + `m_iCurrentIndex`, and per icon
  `m_strAntennaText` / `m_strCooldown` / `m_strCharge` / `IsAvailable` — antenna text only while
  `Movie.IsMouseActive()` — so the whole bar including each entry's `AvailableCode` reason needs NO
  companion call), the enemy refs (`UITacticalHUD_Enemies.m_arrTargets`),
  `UITacticalHUD.CurrentTargetID` / `m_isMenuRaised` / `ConcealmentMode`, and
  `UIObjectiveListItem.Data` (`ObjectiveDisplayInfo.DisplayLabel`, plus its timer). Everything else
  — HP, armour, action points, effects, ammo, hit chance — exists only in `XCOMHISTORY`. In tactical
  the widget tree supplies STRUCTURE and SELECTION while the companion supplies the WORDS.
- **Tooltips come in three registrations, and the one that looks empty is not.** Read from
  source 2026-09-03 while auditing Space on the Avenger HUD. (1) `UIPanel.SetTooltipText` adds a
  `TTextTooltipData` element to the manager's ONE shared `UITextTooltip` (`TextTooltipData`, stride
  0xAC, id at 0x00, targetPath 0x1C, sBody 0x54) and stamps the panel with `bHasTooltip` +
  `CachedTooltipId` — the panel-keyed read. (2) `UITooltipMgr.AddNewTooltipTextBox` called
  DIRECTLY with a Flash path adds the same element with no panel behind it (`UINavigationHelp
  .GenerateTooltip`, the covert row's `MCPath.slotN`) — the path-keyed read. (3)
  `UIX2MenuButton.SetTooltipWIthCallback` registers a PLACEHOLDER ("Init") and stores a
  `del_OnMouseIn` delegate in the element (0x6C, an FScriptDelegate: Object then FunctionName at
  +8); the real words exist only after the hover fires it. The hover sequence is
  `UITooltipMgr.OnMouse` → `UITextTooltip.MatchesPath`/`MatchesID` (sets `DataIndex` to the element
  and copies its delegate onto the object) → `del_OnMouseIn(Tooltip)` → the callback's
  `UITextTooltip.SetText`, which writes `TextTooltipData[DataIndex].sBody`. So a cold read of (3)
  speaks "Init", and reproducing the hover natively MUST select the element first
  (`MatchesID(id)`, an int-param script call) or the refreshed words land in whichever element the
  last real hover left selected. `GameUI::ReadCallbackTooltip` is that sequence minus the
  `ActivateTooltip`/`UpdateData` Flash push, so nothing appears on screen. The Avenger shortcut
  buttons and the ability icons are (3); nearly everything else is (1).
- ⛔ **A state object can carry SEVERAL components of the same class, and the game's own code is
  written so that it never matters.** Every player-visible query is a per-OBJECT boolean, never a
  count: `Helpers.IsObjectiveTarget(ObjectID)` is `FindComponentObject(class'XComGameState_
  ObjectiveInfo') != none`; `UIUnitFlag.UpdateFromState`/`Show` and `UISimpleFlag.Show` gate on
  `History.GetGameStateComponentForObjectID(StoredObjectID, ...) == none`; `XComTacticalMissionManager`
  builds its objective list by iterating the OBJECTS and asking each one that same boolean; and
  `UIUnitFlagManager.AddFlag` opens with an explicit «don't add duplicate units» scan over
  `StoredObjectID`. Meanwhile the creators do not check: `SeqAct_MakeObjective.AddObjectiveInfo`
  unconditionally `CreateNewStateObject` + `AddComponentObject`, so a mission that re-flags a thing
  when its stage advances stacks a second component on it. Measured live 2026-09-04 on
  `Neutralize_Vehicle`, where the VIP carried two — #50208 `AffectsLineOfPlay=true` (the class
  default, from `CreateObjective_Pawn` at spawn) and #51149 `AffectsLineOfPlay=false`
  (`SeqAct_MakeObjective`'s defaultproperty, the Kismet re-flag). Of the whole SDK only
  `SeqAct_GetObjectiveInfo` and `X2AutoPlayManager` iterate these components by class type, and
  neither draws anything. **So `IterateByClassType` over a COMPONENT class is not the game's idiom
  for "list the things that are X" and does not answer that question** — it returns attachments,
  which are many-to-one with the objects a player perceives. Enumerate objects and ask each, or
  enumerate components and collapse by owner; the object scanner does the latter, keyed on the
  owner's ObjectID.
- ⛔ **Three of `XComGameState_Unit.GetName`'s six name types SILENTLY PREFIX THE RANK, and one of
  them is the one that sounds like it means "name".** `eNameType_RankFull` and `eNameType_RankLast`
  say so; **`eNameType_FullNick` does not, and does it anyway** — every branch of it returns
  `` `GET_RANK_ABBRV(m_SoldierRank, …) `` before the first name, in the campaign path
  (`XComGameState_Unit.uc:4646`), in the MP path (`:4754`) and with or without a nickname. Only the
  `GhostSourceUnit` branch omits it. The rank-free forms are `eNameType_Full`, `eNameType_First`,
  `eNameType_Last` and `eNameType_Nick`. **What this costs is a duplicate against a RANK COLUMN**,
  which is how it was found: the personnel roster and the bond screen both draw a rank column and
  both read `FullNick`, so every row said the rank twice (reported live 2026-09-04). The rows
  themselves are the oracle for what belongs where — `UIPersonnel_SoldierListItem.uc:110` and
  `UISoldierBondListItem.uc:110` each hand Flash `eNameType_Full`, `eNameType_Nick` and
  `GET_RANK_ABBRV` as THREE separate fields, so name, nickname and rank are three things on screen
  and no one field carries another. ⚠ Two places where the doubling is the GAME's and must be
  mirrored, not corrected: `UIPersonnel_DeceasedListItem.uc:20` passes `FullNick` on a tab with no
  rank column, and `UISoldierBondScreen.RefreshHeader:132` passes `GET_RANK_STR` beside `FullNick`
  in one call. ⚠ And `eNameType_Nick` answers `" "` — a lone space, not `""` — when there is no
  nickname, so a script-side join leaves a separator behind; send it as its own wire field and let
  native clean before it joins.
- Best public documentation of Firaxis' UE3 internals: robojumper's "Too Real" series
  (https://robojumper.github.io/too-real/). Highlander event reference:
  https://x2communitycore.github.io/X2WOTCCommunityHighlander/

