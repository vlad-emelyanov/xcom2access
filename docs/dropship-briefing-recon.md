# The Skyranger load screen — `UIDropShipBriefing_*` recon

Survey date 2026-08-22, read off `Development/SrcOrig` (retail WOTC) plus the generated SDK's
live-dump offsets. Nothing here has been measured in a running game yet — §8 is the list of
things the first live pass has to answer.

## 1. What the screen actually is

The "Skyranger load screen" is not a movie and not a `UIProgressDialogue`. It is a real
`UIScreen` on the game's own screen stack, drawn over a matinee of your squad sitting in the
dropship interior. Two variants, one per direction of travel:

| Class | When | Flash LibID |
|---|---|---|
| `UIDropShipBriefing_MissionStart` | Avenger/geoscape → tactical | `DropshipBriefing` |
| `UIDropShipBriefing_MissionEnd` | tactical → Avenger | `DropshipPostMission` |

Both `extend UIDropShipBriefingBase extends UIScreen`, package
`/ package/gfxDropshipBriefing/DropshipBriefing`.

Spawn path — `XComPlayerController`:

```
NotifyLoadedWorld(pkg, bFinalDest=false)     :1294   Pres.Destroy(); InitPres();
  └─ InitDropshipUI()                        :1332   retries on a 0.1 s timer until both movies inited
       └─ SetupDropshipBriefingScreen()      :1613   TACTICALGRI != none ? MissionStart : MissionEnd
            BriefingScreen.AllowShowDuringCinematic(true)
            Pres.ScreenStack.Push(BriefingScreen, Pres.Get3DMovie())     :1635
```

✅ **It is stack-managed**, so `GameUI::FindStackScreenRaw("UIDropShipBriefing_MissionStart")`
finds it — no GObjects fallback, no `UIDialogueBox`-style special case.

⚠ **`Pres` is destroyed and rebuilt on the transition map** (`:1298`). Every screen-finder cache
in `game_ui.cpp` must re-find rather than revalidate a type here. `FindStackScreenRaw` already
does the right thing (`StackIsLiveRaw` rejects the outgoing stack), but this is the exact shape
of the stale-pointer bug the hard rules were written for.

## 2. What is on it — and where each word lives

`UIDropShipBriefing_MissionStart.UpdateBriefingScreen()` (`:52`) pushes THIRTEEN strings through
one `MC.BeginFunctionOp("updatePreBriefing")`. The screen retains the labels and the tip; the
values it does not.

| On screen | Source | Retained? |
|---|---|---|
| `m_strOpLabel` — "TOP SECRET" / "СОВЕРШЕННО СЕКРЕТНО" | localized var, 0x420 | ✅ |
| operation name | `XComGameState_BattleData.m_strOpName` | ❌ re-derivable |
| `m_strLocationLabel` — "LOCATION:" | 0x430 | ✅ |
| location | `BattleData.m_strLocation` (city + country) | ❌ re-derivable |
| `m_strBriefingLabel` — "BRIEFING:" | 0x440 | ✅ |
| briefing paragraph | `X2MissionTemplate.Briefing` (localized) | ❌ re-derivable |
| `m_strObjectiveLabel` — "OBJECTIVE:" | 0x450 | ✅ |
| objective lines 0..2 | `MissionTemplate.GetObjectiveText(i, GeneratedMission.MissionQuestItemTemplate)`, guarded by `GetNumObjectives()` | ❌ re-derivable |
| the gameplay tip | **`m_strCurrentTip`, 0x4C4** | ✅ **retained outright** |
| `m_strLoadingText` — "TRAVELING TO DROP POINT" | 0x470 | ✅ |
| `m_strSplashTitle` / `m_strSplashLabel` | 0x480 / 0x490 | ✅ |

Plus, written separately once loading finishes:

| `m_strLaunch` — "PRESS ANY KEY TO LAUNCH" / "НАЖМИТЕ ЛЮБУЮ КНОПКУ ДЛЯ ЗАПУСКА" | 0x460 | ✅ |

All ten localized keys are present in `XComGame.INT` **and** `XComGame.RUS` — checked, not
assumed. `m_strConsoleLaunch` (0x4A0) is the controller wording and carries a `%A` glyph token;
it is the wrong one to read on mouse+keyboard.

The tip comes from `XGLocalizedData.GameplayTips_Tactical` (strategy list for the mission-end
screen), prefixed with `m_strTipLabel` ("TIP:" / "Совет!"). `UIDropShipBriefingBase` runs a
seen-tips ledger in `XComGameState_DropShipTipStore` so tips do not repeat until the pool is
exhausted — which means **the tip is genuinely new information most of the time**, not filler.

### Mission-end variant

`UIDropShipBriefing_MissionEnd.InitScreen` pushes op name, `MissionTemplate.PostMissionType`, a
coloured mission result (`m_strMissionComplete` / `m_strMissionAbandoned` / `m_strMissionFailed`
off `UIMissionSummary`), a strategy tip and the campaign poster. Then `PopulateBattleStatistics`
fills two real `UIList`s with eight rows:

- left: successful shot %, average damage per attack, average kills per turn, average cover bonus
  — each computed from `XComGameState_Analytics` tactical floats, each possibly suffixed
  `m_strNewRecord`;
- right: dealt most damage / took most shots / most under fire / moved furthest, each a soldier
  named `eNameType_FullNick`.

⛔ **The rows retain nothing.** `UIDropShipBriefing_ListItem extends UIPanel` and
`InitListItem` pushes label+value straight through `MC.BeginFunctionOp("setData")`. That is the
fourth screen family in this project with the same house style (debris-room picker, commodity
rows, personnel rows) — the values have to be recomputed the way the screen computed them.

## 3. The load-done signal, and why it is the whole feature

`OnCheckLoading()` runs on a **1 s repeating timer** (`:47`) and does exactly two things:

```
if (PC.bSeamlessTravelDestinationLoaded) {
    LaunchButton.bIsVisible = true;                  // <-- the edge we want
    MC.FunctionString("updateLaunch", m_strLaunch);  // "PRESS ANY KEY TO LAUNCH"
    SetTimer(0.0f);                                  // stop polling
} else {
    TipCycle = (TipCycle + 1) % 10;                  // new tip every ~10 s
    if (TipCycle == 0) { m_strCurrentTip = GetTip(...); MC.FunctionString("updateTip", ...); }
}
```

`LaunchButton.bIsVisible` flips in the same statement block that writes the prompt, so reading
that bit IS reading the screen's own answer — no `XCOMHISTORY`, no player-controller poke. It is
also squarely inside the documented exception to *never gate readiness on `bIsVisible`*: this is
a child BUTTON whose visibility the game toggles deliberately.

Today, from the moment the player confirms the squad until the tactical HUD builds, X2Access says
**nothing at all**. The generic 10-second "Loading" cue in `menu_navigator.cpp:4108` cannot help
here: it keys off `UILoadScreenAnimation`, and `UILoadAnimation(true)` is called only by
`UINarrativeMgr` and `XComWaitCondition_NarrativesLoading` — never on the dropship path. So a
blind player gets silence, then more silence after the game is ready and waiting for them.

## 4. Input — the constraint that shapes everything

Dropship input is intercepted **before the screen stack**, in `XComInputBase` (`:466`-`:478`):

```
if (`XENGINE.IsAnyMoviePlaying()) return;                    // dead while a bink plays
if (!PC.bProcessedTravelDestinationLoaded) {
    if (PC.bSeamlessTravelDestinationLoaded && (ActionMask & FXS_ACTION_RELEASE)) {
        PC.CleanupDropshipStart();                            // <- ANY key release launches
        PC.ClientSetCameraFade(true, black, (0,1), 0.5);
    }
    return;                                                   // <- and nothing else ever runs
}
```

Three consequences:

1. **While loading, every key is dead.** The branch returns before the screen stack is offered
   anything. So during the load window our arbiter can claim whatever it likes at zero cost —
   nothing is being taken from the player.
2. **After loading, any unclaimed key launches.** Conversely, any key we DO claim stops being a
   launch key, because the arbiter swallows claimed chords in the WNDPROC and the engine never
   sees them. That is a real trade and has to be deliberate.
3. ⛔ **`UIDropshipHUD` is dead code — do not build against it.** It has a proper
   `OnUnrealCommand` gated on `bWaitingForInput`, but `NotifyDestinationLoaded()` (its only
   setter) has no caller anywhere in the sources, and `XComPresentationLayerBase.UIDropshipHUD()`
   (`:2308`), which would spawn it, has no caller either. The `ScreenStack.PopFirstInstanceOfClass(class'UIDropshipHUD')`
   in `XComHQPresentationLayer:2131` pops something that was never pushed.

There is **no vanilla keyboard navigation on this screen at all**: `LaunchButton.DisableNavigation()`
in both variants, no list navigation on the mission-end stat lists, and the whole stack is
bypassed anyway by the branch above.

## 5. ⛔ The screen stack is in CINEMATIC MODE here

`SetupDropshipBriefingScreen` calls `BriefingScreen.AllowShowDuringCinematic(true)` and both
classes set `bShowDuringCinematic = true` in defaults. They need it: the dropship plays a
narrative moment on arrival — `StartDropshipNarrative()` (`XComPlayerController:1578`) for the
mission start, `PlayPostMissionNarrative()` for the mission end — and `UINarrativeMgr:1408` calls
`ScreenStack.HideUIForCinematics()`, which sets `UIScreenStack.bCinematicMode`.

`MenuNavigator::OnTick` detaches and refuses to attach whenever `StackState::cinematic` is set.
**So a graph recipe cannot attach to this screen as the gate stands.** This is the same shape as
the hacking pad, which reports cinematic=1 for the same reason and therefore deliberately does
not gate on `StackState::Interactive()`.

## 6. Offsets, if native reads them directly

From the generated SDK dump (Highlander build). Every one needs a `static_assert` in
`sdk_layout_checks.cpp` per the project rule, and a `/props?screen=…` confirmation live.

```
AUIDropShipBriefing_MissionStart : AUIDropShipBriefingBase : AUIScreen
  0x420 m_strOpLabel        0x430 m_strLocationLabel   0x440 m_strBriefingLabel
  0x450 m_strObjectiveLabel 0x460 m_strLaunch          0x470 m_strLoadingText
  0x480 m_strSplashTitle    0x490 m_strSplashLabel     0x4A0 m_strConsoleLaunch
  0x4B0 TipCycle (int)      0x4B4 LaunchButton (AUIButton*)   0x4BC TestImage
  0x4C4 m_strCurrentTip

AUIDropShipBriefing_MissionEnd
  0x420 TipCycle   0x424 LaunchButton   0x42C LeftList   0x434 RightList
  0x43C m_strPressKeyToContinue   0x44C m_strConsolePressKeyToContinue
  0x45C..0x4CC the six stat labels    0x4DC m_strLoadingText   0x4EC m_strNewRecord

AUIDropShipBriefingBase   0x410 TipCounters (TArray<int>)
AXComPlayerController     0x6D4 bit 0x80 bSeamlessTravelDestinationLoaded
                          0x6D4 bit 0x100 bProcessedTravelDestinationLoaded
                          0x6E4 BriefingScreen   0x70C MapImagePath
```

⚠ `m_strConsoleLaunch` carries a `<workshop> UI_CONSOLE_LOAD_SCREEN_FIXES` marker in `SrcOrig`,
i.e. it was added by a patch — exactly the kind of field that differs between the stock package
and the Highlander's recompile (`UIShellSoundtrackPicker` all over again). It sits at the END of
the class's own block, so a divergence would move only `TipCycle` and everything after it —
including `m_strCurrentTip` and `LaunchButton`, the two fields that matter. Verify live before
trusting them.

## 7. Proposed shape

### Why narrate-only, not a graph recipe

Graph recipes are the rule in this project and the exception has to be argued each time. Here the
argument is the input model, not the content:

- The gate blocks a recipe outright (§5) — attaching would need a documented cinematic exception,
  the second one in the project.
- There is nothing to track focus ON: no navigable widget exists, so a recipe would be inventing
  a focus model rather than exposing the game's.
- **After the load completes, every claim is a key subtracted from "press any key to launch".** A
  recipe claims Up/Down/Left/Right/Tab/Enter/Space/Home/End. Claiming Enter and Space — the two
  keys a player will actually reach for — and then having to re-provide the launch by calling
  `CleanupDropshipStart()` ourselves is a lot of machinery to end up where vanilla already is.

So: a poll-and-speak reader in the shape of `avenger.cpp` / `hacking_screen.cpp`, with **one**
claimed key.

### Phase 1 — native only, no companion changes

Everything below is a retained read at a fixed offset. This is the whole functional fix.

1. **On attach** (the screen appears on the stack): speak `m_strLoadingText` — *"TRAVELING TO
   DROP POINT"* — so the player knows the mission is loading and the game has not hung.
2. **On the `LaunchButton.bIsVisible` false→true edge**: speak `m_strLaunch` on the Alert channel
   (interrupting) — *"PRESS ANY KEY TO LAUNCH"*. This is the announcement the feature exists for.
   The mission-end variant reads `m_strPressKeyToContinue` the same way.
3. **`Delete` re-announces** the whole block, including `m_strCurrentTip`. `Delete` is free here:
   every other `Delete` claim in the project (tile cursor, shot HUD, hacking pad, ant farm) gates
   on tactical or on the facility grid, none of which is up on the transition map. Cost: `Delete`
   stops being a launch key. Document it; it is the same trade the ant farm made.
4. Gate: `FindStackScreenRaw("UIDropShipBriefing_MissionStart" | "…_MissionEnd")` non-null. That
   is it — no `StackState` test, deliberately, for the reason in §5.

### Phase 2 — the briefing content, via a companion class

`X2AccessDropshipReader.uc`, mirroring `UpdateBriefingScreen()` line for line: `BattleData`
singleton for `m_strOpName` / `m_strLocation`, the mission template for `Briefing` and
`GetObjectiveText(0..2, GeneratedMission.MissionQuestItemTemplate)` under `GetNumObjectives()`.
Read once on attach and cached — the screen builds these once and never touches them again.

Speech order is a decision, not a transcription: **operation name, location, objectives, briefing,
tip**. The objectives are what the player acts on and the briefing paragraph is flavour, so the
paragraph goes after the list even though the screen draws it before. Same reasoning as the save
row's timestamp.

Join with `X2Access::Graph::AppendPhrase`, not `", "` — these are localized strings that end in
`:` and `.` and this surface composes its own speech, so it owes the check the ant farm owed.

### Phase 3 — the mission-end stat block

Eight rows recomputed from `XComGameState_Analytics` exactly as `PopulateBattleStatistics` does,
including the `m_strNewRecord` suffix and the `"--"` placeholder. This is a table (label/value
pairs in two columns) but it is read-only and un-navigable, so it is a block read rather than a
`GraphSheet`. Lower value than phases 1-2: `UIMissionSummary` already covered the outcome.

### What we deliberately do NOT do

- ⛔ Do not auto-speak each new tip as it cycles (every ~10 s). It would talk over the pre-mission
  VO that `TransientReader` is already reading, and the tip is not why the player is here. On
  demand via `Delete` only.
- ⛔ Do not claim Enter, Space or Escape. They are the launch keys.
- ⛔ Do not touch `WorldInfo.bContinueToSeamlessTravelDestination` or call `CleanupDropshipStart()`
  ourselves in phases 1-3. The vanilla any-key path works on mouse+keyboard; this is a reader,
  not a rescue.

## 8. What the first live pass must answer

Ordered so that a failure early makes the rest moot.

1. ✅ **ANSWERED 2026-08-22 — our layer IS alive on the transition map.** The frame pump rides
   ProcessEvent traffic and this is the first surface on a map that is neither the shell, the
   Avenger nor tactical, so it was the one blocking unknown. The test cost no code: the dropship
   plays a narrative moment on arrival, `UINarrativeCommLink` carries its subtitle, and
   `TransientReader` already reads that — so *"does Central's pre-mission briefing speak today?"*
   answers it. Reported by the player: it does. The pump, the companion CDO lookup and the
   outbound-call path all therefore work over there.
2. `/status` while the dropship is up: does `FindStackScreenRaw` see
   `UIDropShipBriefing_MissionStart`, and what do `cinematic` / `screensHidden` / `inputBlocked`
   read? §5 predicts cinematic=1 at least while the VO plays.
3. `/props?screen=UIDropShipBriefing_MissionStart`: confirm the §6 offsets under the Highlander,
   and confirm `m_strCurrentTip` is non-empty and `m_strLaunch` is populated (the §13.7
   localized-var trap — a `localized` var declared on a class the loc file has no section for
   reads as `""`; here both variants declare their own and both sections exist, but verify).
4. How long is the window? A 3-second load and a 60-second load want different pacing.
5. Does the briefing screen appear for the FIRST mission / the tutorial at all? `XGBattle_SP:296`
   plays `1080_PropLoad_001.bik` and `X2TacticalGameRuleset:5262` plays `CIN_TP_CommanderAwakens.bk2`
   as loading movies, and input is dead while a movie plays — so the tutorial may take a different
   route into tactical.
6. The mission-END screen on the way back, which is the same code path in the other direction and
   the one a player hits after every single mission.

## 9. What was built (2026-08-22) — phases 1 and 2, not yet run live

`src/dropship.{h,cpp}` + `GameUI::ReadDropship` + `X2AccessDropshipReader.uc`. Phase 3 (the eight
mission-end statistic rows) is deliberately not in it.

- **Native, all retained reads**: `ReadDropship()` takes ONE top-first stack walk matching either
  briefing class, then reads the status line, the prompt, the tip and the launch button. The
  §6 offsets are pinned by `static_assert`s in `sdk_layout_checks.cpp` — which the build passes,
  so they match the Highlander's compiled layout and §8.3's offset half is already answered.
- ⛔ **Readiness is the VISIBLE bit alone, not `IsShownRaw`.** Also requiring `bIsInited` would
  tie the announcement to the launch button's Flash init callback, and a callback that never
  arrived would mean we simply never say "ready" — a silent failure indistinguishable from a slow
  load, i.e. the exact thing this feature exists to remove.
- **Speech**: the arrival block goes out on the **Combat** channel (queues, does not interrupt) so
  it falls in behind Central rather than over him; the ready prompt goes out on **Alert**
  (interrupts the current utterance, keeps the queue) because a player waiting to launch must not
  have to sit out the rest of a line first. `Dropship::OnTick` is placed after `TransientReader`
  in the frame pump for the same reason.
- **The briefing words get a bounded 4 s retry**, and that is not belt-and-braces:
  `FindCompanionCDO` throttles its GObjects scan to once a second and **the throttle is shared
  across every class**, so a cold first lookup can answer null purely because another module
  scanned a beat earlier. One shot would have left the player with the loading line and no
  briefing — and a briefing that is silently absent sounds exactly like a mission that has none.
- **`Delete` re-announces**, tip included, and while this screen is up it therefore stops being
  one of the "any key" that launches. Stated here because it is a real cost, and it is the same
  trade the ant farm made for the same key.
- **`/status` grew a `dropship:` line** — variant, pointer, ready flag, status text and tip. That
  is §8.2 and §8.3 in one request, and it works even before any speech is heard.

Live checks specific to what was built, beyond §8: that the arrival block does not talk over the
pre-mission VO; that the prompt fires ONCE per briefing and not on every poll; that the retry
either lands within its window or the log says it did not (`briefing words arrived N ms after the
screen`); and a shape-count of the speech ring afterwards, where a healthy launch shows exactly
one arrival block and one prompt.
