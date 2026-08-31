# Concealment recon — how the game warns about detection, and what we read

Measured 2026-08-31 from the WOTC SDK's `Development/SrcOrig/XComGame/Classes/` (source-derived —
the live-verification debt this creates is §6 and is filed in `next-steps.md`). The question, asked
by the player mid-mission: *how does a sighted player know which tiles would break concealment —
does the game warn about windows on the path, does it show enemy detection areas?* The answer is
five distinct channels, and the build that follows from it lives in `X2AccessHelper` /
`X2AccessTileReader` (companion-only; no native changes).

## 1. The five sighted channels

1. **The red tile overlay** — while a concealed unit is selected, every tile that would break its
   concealment is painted red (`XComPathingPawn.ConcealmentRenderingComponent`, an instanced mesh,
   `XComPathingPawn.uc:172`). Computed natively, cached per unit (`ConcealmentCache`, a native
   `TMap` at `.uc:80` — not script- or layout-readable), invalidated on the
   `UnitConcealmentEntered`/`UnitConcealmentBroken` events (`.uc:420`).
2. **Path preview markers** — icons pinned to the exact path tiles where something bad happens:
   `ConcealmentMarkers` (path crosses into detection), `NoiseMarkers` (movement smashes a
   window/door), plus `HazardMarkers`, `LaserScopeMarkers`, `KillZoneMarkers`
   (`XComPathingPawn.uc:202-206`). All five are **`private` script vars** — the companion cannot
   read them (native reflection could, by property name; see §5). Noise and concealment share one
   icon glyph (`MaterialIconIndex=6`, `DefaultGameCore.ini:2001-2002`).
3. **A warning sound** — `UpdatePuckAudio` (`XComPathingPawn.uc:1402`) plays AK event
   `SoundTacticalUI.Concealment_Warning` on the edge where the hovered path first contains ANY
   marker of the five kinds, re-arming when the path goes clean. ✅ Because our tile cursor pushes
   the real 3D cursor, this already fires for keyboard users — the binary "this path is dangerous"
   ping predates this build; what it lacks is *why* and *where*.
4. **The shot HUD flag** — `WillBreakConcealment = MayBreakConcealmentOnActivation(target)` pushed
   into Flash per readied ability (`UITacticalHUD_ShotHUD.uc:234-237`), plus the Reaper Shadow HUD's
   %-loss meter (`UITacticalHUD.RealizeConcealmentStatus`, `.uc:1120`). Covered by
   `free-aim-recon.md`; not this build.
5. **Status indicators** — HUD banner strings (`m_strConcealed` / `m_strSquadConcealed` /
   `m_strReaperConcealed` / `m_strRevealed`, `UITacticalHUD.uc:114-117`, mode enum at +0x418), the
   unit-flag eye icon (`UIUnitFlag.SetConcealmentState`), the full-screen shader
   (`XComPresentationLayer.UpdateConcealmentShader`), and on an actual break the per-soldier world
   messages + toast (already narrated — `surfaces/tactical.md`, `popup-inventory.md` T4).

A sixth, invisible aid: the pathfinder itself carries `ConcealmentBreakingBias`
(`X2PathSolver.uc:72`), so the auto-drawn path bends around red tiles when a safe route exists.
That one needs no narration — our player inherits it through the game's own path.

Vanilla shows **no** line-of-sight overlay for a revealed squad — the red paint exists only for
concealment. Sighted parity therefore forbids inventing a general LOS readout.

## 2. The break rules (what the red paint means)

All in `XComGameState_Unit.uc`, `OnUnitEnteredTile`:

- **Destruction breaks concealment outright** (`:8940`): a concealed move whose
  `bPathCausesDestruction` is set — smashing a window, kicking through a door — reveals,
  unconditionally, no enemy required (challenge mode exempt).
- **Interactive detectors** (`:8977-8990`): any unhacked `XComGameState_InteractiveObject` with
  `DetectionRange > 0` (ADVENT towers), pure radius, **no LOS test**, unless the unit has an
  ability in `X2AbilityTemplateManager.AbilityRetainsConcealmentVsInteractives` (`:8963`).
- **Enemy units** (`:9035-9058`): break when the detector has basic sight (`bVisibleBasic` — clear
  LOS within sight radius) AND squared distance ≤ `Square(GetConcealmentDetectionDistance(Detector))`
  (`:9507`, native: detector sight radius scaled by the mover's detection modifier — Phantom/Shadow
  shrink it). Who counts as a detector at all is `UnitBreaksConcealment(Other)` (`:9607`, native,
  callable — team, mind control, Faceless are all inside it). Some detectors ignore distance
  entirely (`DoesUnitBreaksConcealmentIgnoringDistance`, `:13476`, effect-driven).
- The reverse case — a PATROL moving near your stationary concealed soldier (`:9009-9011`) — adds
  a condition the forward case lacks: the concealed unit is revealed only when it has **no cover**
  from the mover (`TargetCover == CT_None`). The red overlay answers the forward question ("what if
  I move here"), so our per-tile test mirrors the forward branch and takes no cover term.

The per-step destruction test the game runs at submission is
`MoveAbility_StepCausesDestruction` (`X2Ability.uc:1540`, native, needs a filled
`AbilityInputContext`) — but the primitive underneath is public and per-edge:
`XComWorldData.GetPathObjectFromTraversalToNeighbor(TileA, TileB, bUnitIsLarge)`
(`XComWorldData.uc:806`), exactly how `XComTacticalController` marks path destructibles at click
time (`.uc:1230`): a returned `XComDestructibleActor` in state `'_Pristine'` is a thing this
traversal will smash.

## 3. What we built

Companion-only; the C++ layer is untouched.

- **`X2AccessHelper.TileBreaksConcealment(ConcealedUnit, Tile, out Kind)`** — the shared per-tile
  oracle (the red-paint question), from the §2 forward rules: per enemy,
  `UnitBreaksConcealment` → squad-visibility gate (§4) → distance gate (cheap, first) →
  `CanSeeTileToTile` LOS trace (`XComWorldData.uc:861`, expensive, last); per tower, radius only,
  FOW-latched, `AbilityRetainsConcealmentVsInteractives`-gated. `Kind` distinguishes enemy from
  tower-only. `DescribeDetection` is the word form: "Detection zone" / "Tower detection".
- **`X2AccessTileReader.DescribeTile`** speaks it per tile, after cover and before hazards, gated
  on the active unit being concealed — the game hides the overlay when revealed, and so do we.
- **`X2AccessTileReader.CursorMoveTo`** (first press) speaks the path-marker facts, only while the
  moving unit is concealed (`IsConcealed()` is per-unit, so a Phantom Ranger keeps them after the
  squad is revealed): walks `PathingPawn.PathTiles` pairwise through
  `GetPathObjectFromTraversalToNeighbor` for "noise", and through the per-tile oracle for "breaks
  concealment", with destruction implying the break directly (§2). Revealed, a window on the path
  is something a sighted player simply sees — the tile readout already names it — so no push.
  Whether the game's native `UpdateNoiseMarkerInfo` still pins the sound-wave icon for a revealed
  unit is UNVERIFIED (the script calls it unconditionally; the gate, if any, is native — §5's
  marker-array read answers it); if it does, that is a deliberate divergence, chosen for brevity.

Why recompute instead of reading the game's own marker arrays: they are `private` to script (§1.2),
and at `CursorStep` time they are also one tile STALE — the pathing pawn rebuilds its path on its
own Tick after our cursor push, so the markers describe the previous hover at the moment we compose
the step's line. The per-tile oracle answers synchronously. At `CursorMoveTo` time the path HAS
settled (that function already relies on `LastDestinationTile` agreeing with the cursor), so the
native-read upgrade in §5 would be sound there.

## 4. Parity assumptions to verify live (§6 is the checklist)

1. **The squad-visibility gate on unit detectors.** We paint red only for enemies
   `CanXComSquadSeeTarget` answers for — the same gate the object scanner's enemy ring uses. Belief:
   the native overlay is likewise limited to seen enemies (walking into an unseen pod's range
   reveals you with no warning — that is the sighted experience too). If a live comparison shows
   the game painting tiles for enemies the squad cannot see, widen ours to match — the overlay is
   the parity target, not our guess about it.
2. **`CanSeeTileToTile` as the LOS term.** The native cache's trace may differ (peek tiles, height).
   Expected divergence: edge tiles near cover corners.
3. **Overlay over fog.** We speak detection on explored-but-not-visible tiles (never-seen tiles
   already return "Unexplored" before the oracle runs). Belief: the game paints there too.
4. **Tower FOW latch.** Tower zones spoken only once the tower's tile has been seen — matching the
   scanner's tower category, not a measured overlay fact.

## 5. The exact-parity upgrade (queued, not built)

Native CAN read the five marker arrays: UE3 reflection resolves properties by name regardless of
script access (`UE3Reflect` reads `NativeResult` the same way) — no offsets, no `static_assert`s
needed for a by-name `TArray<TTile>` read. That read is the audit tool for §4 (dump markers vs our
words on the same hover) and the only route to `LaserScopeMarkers` / `KillZoneMarkers`, which warn
about overwatch-like death zones our recompute does not attempt. It belongs where the path is
settled: `CursorMoveTo`, or a `/tile` dev endpoint. Filed in `next-steps.md`.

## 6. First-pass checklist (Gatecrasher, squad concealed)

- Sweep the cursor across a visible pod's red zone: "Detection zone" must appear on entry and stop
  on exit; ask the sighted check whether the boundary matches the paint (assumptions §4.1/§4.2).
- Preview a move THROUGH a window: first Backspace press must say "breaks concealment, noise"
  while concealed; the same path revealed must say neither (path risks are concealment-gated).
- Preview a safe move inside a red zone's reach but out of LOS: must stay silent on both facts.
- A tower mission (any city map): "Tower detection" inside the ring, silence after hacking it.
- Shape-count the ring afterwards (`CLAUDE.md`, the distribution rule): "Detection zone" must be
  neither absent nor on every tile, and `grep -P '  '` stays part of the audit.
- Break concealment mid-run: every per-tile detection line must stop instantly (the readout is
  gated on `IsConcealed`, not on squad state).
