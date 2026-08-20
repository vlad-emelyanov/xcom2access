# Hacking screen — recon

_Live-verified 2026-08-20 against the running game (tutorial mission `Tutorial_01`, replay frame
1534, hacking the objective door with Jane Kelly). All script claims cite the SDK's
`Development/SrcOrig/XComGame/Classes/` unless the Highlander's rebuild differs, which is called
out where it does._

---

## 1. What it is

`UIHackingScreen` — the hack / skulljack / skullmine overlay. Three facts about it shape
everything else:

1. **It is spawned by the visualization action, not by the HUD.**
   `X2Action_Hack.SetupHackingScreen()` spawns the screen, wires two delegates back to the action
   (`OnHackingComplete`, `OnHackingRewardUnlocked`) and pushes it with the **3D movie**
   (`` `PRES.Get3DMovie() ``). The action then blocks in `while(!HasFinishedUI) { Update2DCursorLocation(); Sleep(0.1); }`.
   So the whole hack is one `X2Action` sitting inside a visualization block, and the screen is a
   texture rendered onto the Gremlin's `LCD_Panel` static mesh at the `Screen` socket. Mouse
   hit-testing goes through `Helpers.GetUVCoords` on that mesh — the player is literally pointing
   at a prop in the world.

2. **It carries no widgets.** `/gui?screen=UIHackingScreen&depth=6` returns **`1 panels`** — the
   screen and nothing else. Every word on it goes into Flash through `MC.FunctionOp` /
   `Movie.ActionScriptVoid`. This is the tactical-HUD problem in its purest form, so the
   read-the-widget recipe pattern does **not** transfer.

3. **The entire decision is one int.** `X2HackRewardTemplateManager.AcquireHackRewards` reads
   `HackingScreen.SelectedHackRewardOption` **off the live screen object**
   (`X2Ability_DefaultAbilitySet.uc:1791`, `X2HackRewardTemplateManager.uc:122`) and compares
   `AbilityContext.ResultContext.StatContestResult` against `GetHackChance(selected)`. Under the
   threshold → the chosen reward; at or over → slot 0 only. Nothing else the player does on this
   screen matters.

Live state on the captured screen:

```
0 UIHackingScreen  0x00007FF42FB75D00  0x279 inited focused visible
1 UIReplay / 2 UISpecialMissionHUD / 3 UITurnOverlay / 4 UITacticalHUD / ...
stack: GATED (inputBlocked=0 screensHidden=0 cinematic=1)
tile-reader: inactive (blocked=1 blocker=UIHackingScreen)
HackOffense = 5   HackDefense = 60   SelectedHackRewardOption = 1   HackRewards = 5 entries
```

---

## 2. What a sighted player sees

| On screen | Source |
|---|---|
| `ГАРАНТИРОВАНО` over card 0 | `GuaranteedHeader`; becomes `ОБРАТНАЯ СВЯЗЬ` (`FeedbackHeader`) when slot 0 is `bBadThing` |
| Card 0 — "Открыть дверь / Открыть эту дверь. / ШАНС ОТКРЫТЬ: 100%" | `ObjectiveDoor_T0`, `MinHackSuccess=0` → `GetHackChance` returns exactly `100.0` |
| `ВЫБОР НАГРАДЫ ЗА ВЗЛОМ` over cards 1–2 | `ChooseRewardText` |
| Card 1 (green border = selected) — "Измененные сигналы … 6%" | `DisguisedSignals_T1`, `MinHackSuccess=23` → `(100−23)·5/60 = 6.42` |
| Card 2 — "Неуязвимость … 2%" | `Override_T2`, `MinHackSuccess=67` → `(100−67)·5/60 = 2.75` |
| Red strip on cards 1–2, "Неудача: нет эффекта" | `GuaranteedFailText`; flips to `FeedbackFailText` ("Неудача: обратная связь") when slot 0 is a bad thing |
| Bottom meter with a green `6%` marker near the **right** edge | inverted: `AS_SetMeterRolledScore(100 − StatContestResult)`, so the marker sits at `100 − chance`. The fill must pass the marker to win |
| Bottom left — "НОВОБРАНЕЦ / JANE KELLY / ТЕХ. УРОВЕНЬ 5" | `PopulateSoldierInfo` — rank, full name, nickname, class icon, `HackOffense` |
| Bottom right, red — "ТЕХ. УРОВЕНЬ 60 / ПРОЧНАЯ ДВЕРЬ" | `PopulateEnemyInfo` → `ObjectState.GetLootingName()` + `HackDefense` (= `LockStrength`) |
| `ВЗЛОМАТЬ` / `ОТМЕНИТЬ` | `strHackButtonLabel`; cancel hidden when `CanCancel()` is false |

The chance formula, once, so nobody re-derives it wrong:

```
GetHackChance(i):
  bBadThing        -> 100 - GetHackChance(SelectedHackRewardOption)   // the FAILURE chance
  MinHackSuccess=0 -> 100
  otherwise        -> Clamp((100 - (MinHackSuccess + RollMod)) * HackOffense / HackDefense, 0, 100)
```

**Never re-implement it.** `GetHackChance` is a plain script function on the screen; call it.
A mod, a sitrep (`X2SitRepEffect_ModifyHackDefenses`) or a Gremlin's `HackingAttemptBonus` all
move the inputs, and the Highlander could move the formula.

Displayed percentages are **truncated**, not rounded: 6.42 → `6%`, 2.75 → `2%`. Mirror that.

### On commit

The meter animates ~3 s (`m_hackDuration`), panels unlock one at a time
(`OnCommand("UnlockReward")` → `m_rewardsTaken++`, a different Wwise sting per unlock), then
`AS_UpdateHackComplete` shows **Успех!/Провал!** plus the reward name, its description and
"Нажмите любую клавишу". Flash signals the end with `OnCommand("HackEnded")`, which sets
`bWaitingForInput = true` — that flag is the reveal timestamp, and it is a readable script
property.

### Two quirks that are the game's, not ours

- **`HackRewards` held 5 entries but only 3 are ever shown.** `RefreshPreviewState` hard-loops
  `Index < 3` and `OnUnrealCommand` only ever writes 1 or 2, so `ResistanceBroadcast_T1` and
  `EnemyProtocol_T2` are unreachable on this door. (`AcquireHackRewards` even assumes "best hack
  is last", which with 5 entries is never true.) The array is `object.HackRewards` **plus**
  `BattleData.TacticalHackRewards` (`XComGameState_InteractiveObject.uc:861`); the usual case
  sums to exactly 3. **Mirror the panels, not the array: clamp to `Min(Length, 3)`.**
- Descriptions can arrive with a hole where a number belongs ("Доход от текущего региона
  повышается на .") — `GetDescription` expands `XGParam` tags that are only populated in the
  strategy layer. Speak it as-is; that is what the card shows.

---

## 3. What a blind player gets today: nothing — and worse

`/speech` ends at `Взлом, 4 of 5. Door` (the ability bar). The screen then opened and the ring
stayed silent. Not one word.

And the harder finding, **proved live**:

```
before: SelectedHackRewardOption = 1
$ /input?keys=Right,Right          (SendInput scancodes, game focused)
after:  SelectedHackRewardOption = 1
```

`UIHackingScreen.OnUnrealCommand` handles only `FXS_DPAD_LEFT/RIGHT` (356 / 352) and
`FXS_VIRTUAL_LSTICK_*` — **controller commands**. A keyboard arrow arrives as `FXS_ARROW_LEFT`
(503), falls through to `super.OnUnrealCommand` → `Navigator.OnUnrealCommand`, and the navigator
has no targets because the screen has no child panels. `bAutoSelectFirstNavigable` finds nothing
to select.

So on mouse+keyboard the reward choice is reachable **only by clicking a card on a small 3D quad
floating in the world**. A blind player cannot make that choice by any means; a sighted
keyboard-only player cannot either. `Enter`/`Space` (hack) and `Escape` (cancel) do work.

---

## 4. Architecture

Bespoke module in the shape of `tactical_hud.cpp` + `X2AccessTacticalReader.uc` — **not** a
`menu_navigator` recipe.

| Layer | Owns |
|---|---|
| `src/hacking_screen.cpp` | detection, key claims, poll cadence, speech |
| `mods/.../X2AccessHackingReader.uc` | every word, every read, and the selection write |

Why the split falls this way even though the rule of thumb says "on screen → C++ reads it": the
screen holds no widget text at all (§1.2), so there is nothing for `game_ui.cpp` to transcribe.
Everything spoken is either a script property of the screen or a `X2HackRewardTemplate` field —
script-natural work. That is the same conclusion the tactical HUD reached.

### 4.1 Zero new offsets, and why that is load-bearing here

Reading from script means **no new field offsets and no new `static_assert`s** — and this is the
first screen in the project where that is a correctness argument rather than a convenience one.
The Highlander interleaves two new bools into the same bitfield at `+0x450`:

```
vanilla:      m_SkullJacking 0x01  m_SkullMining 0x02  m_hackStarted 0x04  bWaitingForInput 0x08
highlander:   m_UsingSkulljackScreen 0x01  m_SkullJacking 0x02  m_SkullMining 0x04
              m_hackStarted 0x08  m_hackCanceled 0x10  bWaitingForInput 0x20
```

A hand-transcribed `bWaitingForInput` mask would read `m_hackCanceled` on a vanilla install. This
is the project's first case where a Highlander difference moves a **mask** rather than an offset,
and `static_assert`s pin offsets, not masks. Reading by name from script sidesteps it entirely.

### 4.2 Detection — reuse the tile reader's own gate

`GameUI::ReadTacticalGate()` already walks the stack top-down over the passive-overlay whitelist
and reports the first non-passive screen by name. On this screen it returns
`blocked=1 blocker=UIHackingScreen` — so detection is `gate.blocked && blocker == "UIHackingScreen"`,
with no new stack walk and, more importantly, **the two surfaces cannot disagree**: the tile
cursor stands down at exactly the instant the hacking layer stands up.

### 4.3 ⛔ Do not gate on `stack.Interactive()`

The stack reads `cinematic=1` while this screen is live. `UIScreenStack.HideUIForCinematics()`
sets `bCinematicMode` and calls `Screen.HideForCinematics()` on every screen — which is a **no-op
for any screen with `bShowDuringCinematic = true`**, and `UIHackingScreen` sets exactly that in
its defaultproperties. So cinematic mode does **not** mean "nothing is navigable" here;
`IsInputBlocked` (0x1) is the flag that means that, and it is clear.

This is a documented refinement of the CLAUDE.md hard rule "any of them set means nothing is
navigable". The rule stays right for `MenuNavigator` as written, because no current recipe covers
a `bShowDuringCinematic` screen — but `MenuNavigator::OnTick`'s blanket
`movie || !stack.Interactive()` gate is why this screen could not be a recipe today. If a
navigable in-cinematic screen ever needs one, the fix is to judge the cinematic gate **per
screen** against `bShowDuringCinematic`, not to drop it.

### 4.4 Writing the selection — the game's own handler, verbatim

```unrealscript
HackScreen.OnUnrealCommand(class'UIUtilities_Input'.const.FXS_DPAD_LEFT,   // or _RIGHT
                           class'UIUtilities_Input'.const.FXS_ACTION_PRESS);
```

This is the controller path exactly: it sets `SelectedHackRewardOption`, sets
`HighlightedHackRewardOption`, and calls `RefreshPreviewState()`, so the Flash cards re-render and
the meter marker moves. A sighted person watching over the player's shoulder sees the right
thing, and we are not the ones deciding the outcome — `AcquireHackRewards` reads the same int it
always reads.

⛔ **Guard it on `!m_hackStarted`.** `OnUnrealCommand`'s first act is
`if (bWaitingForInput) HackEnded();` — feeding it *any* command after the reveal closes the
screen. The game's own direction cases are guarded the same way (bsg-crobinson, 5.17.17).

### 4.5 Keys

Claimed only while the hacking screen is up **and** `!m_hackStarted`, so that once the game is
waiting for "any key" every key falls through to it:

| Key | Does |
|---|---|
| `Left` / `Right` | move the card cursor across the (up to) 3 cards; landing on card 1 or 2 also makes it the game's selection |
| `Delete` | re-announce: header + current card |

`Enter`, `Space` and `Escape` are **not** claimed — they already work and they are the documented
exits. No contention with anything else: the tile cursor's `Delete` and arrows are gated off by
`blocker=UIHackingScreen`, the shot HUD's `Delete`/`H` need `m_isMenuRaised`, and the menu
navigator is detached.

The **3-card cursor** (rather than a 2-way toggle mirroring the D-pad) is the spatially honest
model — the screen really is three cards left to right, and the guaranteed card is the leftmost
one. Card 0 is not selectable, so landing on it reads the card and appends where the gamble still
stands, in one clause, so the state can never be ambiguous.

---

## 5. Parity rules

- **⛔ Read the SCREEN, never the history — even where both would answer.** The pad looks
  identical in the tutorial and in a real mission, because Flash is driven identically; what
  differs is the *simulation underneath*, which the tutorial never runs (§8). A sighted player
  cannot tell the two apart, so a readout that can is not parity — it is a second, worse game.
  Every value spoken must therefore come from the same place the pad got it: the screen's own
  properties, its own localized strings, its own `GetHackChance`, and the state objects the
  screen itself cached in `InitScreen` (`UnitState`, `TargetUnit`, `ObjectState`,
  `HackTargetObject`) — never a fresh `XCOMHISTORY` lookup, never a submitted context, never an
  event. This is what makes the feature work in the tutorial for free rather than needing a
  tutorial-shaped special case bolted on, and it is why phases 1–2 contain no history access at
  all. It also binds phase 3: see §7.
- **Only the first `Min(HackRewards.Length, 3)` rewards exist.** See §2.
- **Never speak the roll before the reveal.** The game state is submitted the instant `ВЗЛОМАТЬ`
  is pressed and `StatContestResult` is readable from history ~3 s before the meter finishes.
  Announcing early is the same class of violation as reading a replay's recorded `ResultContext`.
- **Say the failure line every time, and say it differently when slot 0 is `bBadThing`.**
  "Неудача: обратная связь" means failing gets you *punished*, not merely nothing, and slot 0's
  "chance" is then the chance of being punished (`100 − chance(selected)`). That flips the whole
  risk calculation and it is on screen.
- **Use the game's own localized strings** for game content (`GuaranteedHeader`, `FeedbackHeader`,
  `GuaranteedFailText`, `FeedbackFailText`, `strHackAbilityLabel`, template names and
  descriptions) with English scaffolding around them — the same mix the shot reader and tile
  reader already speak.

---

## 6. Variants and edge cases

- **Skulljack / skullmine.** `m_SkullJacking` / `m_SkullMining` → the target is a
  `XComGameState_Unit`, so the header wants faction (`m_strAdventInfoTitle` /
  `m_strAlienInfoTitle`) + `GetName(eNameType_Full)` + `eStat_HackDefense`, and `CanCancel()` is
  false. Highlander issue #330 also `Hide()`s a 2D skulljack screen for 3.3 s before showing it —
  hence the poll rather than a one-shot announce on the rising edge.
- **In the tutorial, cancel is disabled too**: `CanCancel()` returns false whenever
  `` `REPLAY.bInReplay ``, which includes `XComTutorialMgr`. Call `CanCancel()`; never assume.
- **Autoplay / AI hacks** set `X2Action_Hack.bSkipUIInput` and drive `InitiateHack()` directly.
  A replay that is not the tutorial auto-hacks from `StartScaryComputerLoopAkEvent`. Both leave
  the screen up but out of the player's hands — the `m_hackStarted` guard covers them.
- **Fewer than 3 rewards** is possible in principle; `RefreshPreviewState` would index out of
  range. Clamp on our side.

---

## 7. Phasing

| Phase | Scope | Status |
|---|---|---|
| 1 | detect + announce on open, `Delete` re-announce, read all three cards | **built 2026-08-20** |
| 2 | `Left`/`Right` card cursor + selection through `OnUnrealCommand(DPAD)` | **built 2026-08-20** |
| 3 | reveal narration — poll `bWaitingForInput` / `m_rewardsTaken`, speak the outcome **from UI state only**; plus a cancel confirmation | **built 2026-08-20** |
| 4 | pre-commit odds: `XComGameState_Ability.GetUISummary_HackingBreakdown` on the hack ability, the spoken form of `UITacticalHUD_HackingTooltip` (mouse-hover-only today) | open |

### Phase 3, as built (designed against §5)

The obvious implementation — read `ResultContext.StatContestResult` off the submitted
`FinalizeHack` context, or `ObjectState.bHasBeenHacked` + `UserSelectedHackReward` out of the
history — is **wrong**, and wrong in the way §5 describes: none of it is ever written in the
tutorial, where the pad nonetheless animates and lights up exactly as it does on Gatecrasher. It
would produce a feature that silently does nothing on the one mission a new blind player starts
with.

The screen retains two Flash-driven signals, and they are enough:

- **`bWaitingForInput`** — set from `OnCommand("HackEnded")`, i.e. the moment the meter finishes
  and the result card is on screen. That is the reveal timestamp, and it is the *only* honest
  one: the game state is submitted ~3 s earlier.
- **`m_rewardsTaken`** — incremented once per `OnCommand("UnlockReward")`, i.e. once per card
  that lit up. Cards `0 .. m_rewardsTaken-1` are what the player is looking at.

So the line is "these cards unlocked", read off the count against the card list phase 1 already
describes — `strRewardUnlockedLabel` (the pad's own "РАЗБЛОКИРОВАНО") plus the names, then the
gamble that did not land named with its own failure strip, then the game's own
`HackCompleteAnyKeyToContinue`. Naming the miss matters most in the case that is easy to forget:
when slot 0 is `bBadThing`, missing does not mean "nothing happened".

Cancelling gets a line too, and it has to come from the C++ side: `OnCancel` closes the screen
immediately, so by the time we notice there is nothing left to read. The falling edge is enough —
a pad that goes away without ever having been committed was cancelled, because that is the only
other way out of this screen, and `gHackStarted` still holds the last polled state at that
moment. Note this is not a compromise — it is *better* than the title Flash prints:
`AS_UpdateHackComplete` picks `HackRewards[RewardIndex].bBadThing ? HackFailedLabel :
HackSuccessLabel`, so with an ordinary (non-`bBadThing`) slot 0 the pad says **Успех!** even when
the gamble missed, because slot 0 was still awarded. The lit cards are the truth; the banner is
not. Neither the title nor `RewardIndex` is retained in script anyway.

⚠ **One inference to confirm live before shipping the wording**: that the unlock order is card
order. It follows from the meter — card 0's threshold is `100 − 100 = 0` and thresholds ascend as
chances fall — and from the script side firing three distinct stings for the first three unlocks,
but Flash owns the decision and nothing in `SrcOrig` states it. Check `m_rewardsTaken` at reveal
against a screenshot of which cards are lit.

If the count ever proves insufficient, the remaining UI-state route is
`GFxMoviePlayer.GetVariableString` against the screen's own movie — reading the Flash text fields
directly, which CLAUDE.md already lists as the answer for Flash-only screens. It needs the
movie's symbol paths, which nothing in `SrcOrig` gives us.

### Phase 3's one hazard, recorded now

`FinalizeHackAbility_BuildVisualization` (`X2Ability_DefaultAbilitySet.uc:2050`) already emits a
flyover carrying `EnemyHackAttemptSuccessString` / `...FailureString` expanded with the reward's
friendly name — through `UIWorldMessageMgr`, which `tutorial_reader.cpp` already polls **inside
the tutorial**. So in the tutorial, a phase-3 reveal line and that flyover will collide. Measure
it with `/tutorial` (it prints exactly what would be spoken) before writing the line; the flyover
may simply *be* the outcome narration.

There are now **three** possible voices on one event, and they do not all fire on the same
mission, which is exactly how a double-read gets shipped unnoticed:

| Voice | Fires on |
|---|---|
| phase-3 reveal line | everywhere (it reads the screen — that is the point) |
| `UIWorldMessageMgr` flyover poll (`tutorial_reader.cpp`) | tutorial only, today |
| `X2AccessTacticalEvents.OnAbilityActivated` / `X2AccessReplayNarration` | normal missions / replays respectively |

So check both halves separately: whether `FinalizeHack` passes `OnAbilityActivated`'s filter on
Gatecrasher (`Hack` itself does, via `IsAbilityInputTriggered`), and whether
`X2AccessReplayNarration` picks the same frame up in the tutorial.

---

## 8. Test fixture — and what the tutorial cannot test

The tutorial reaches this screen deterministically: `Tutorial_01`, replay frame ~1534, right
after the "все солдаты способны взламывать…" narration. `/props?screen=UIHackingScreen` is the
whole debugging surface (it prints `SelectedHackRewardOption`, `HackOffense`, `HackDefense`,
`m_hackStarted`, `bWaitingForInput` and the reward template pointers with live offsets), so no
`/hack` endpoint is warranted.

⚠ **The tutorial proves the reading and the write, never the consequence.** The tutorial is a
replay: `X2GameRuleset.SubmitGameStateContext` routes every context to
`XComTutorialMgr.HandleSubmittedGameStateContext`, which discards the player's context and just
advances `CurrentHistoryFrame`. `ContextBuildGameState` never runs — so
`FinalizeHackAbility_BuildGameState`, and with it `AcquireHackRewards`, is never reached, and
**inside the tutorial the selection has no gameplay effect at all**. What the tutorial *does*
prove, and prove well:

- phase 1: the whole readout, against the live values `/props` shows;
- phase 2: that `OnUnrealCommand(DPAD)` lands — watch `SelectedHackRewardOption` flip 1 ↔ 2 in
  `/props`, which is the same field `AcquireHackRewards` reads on a real mission.

Phase 3, and any check that the chosen reward is actually granted, must be run on **Gatecrasher**
(new campaign, tutorial checkbox off). Same reason the combat-narration listeners had to be.
