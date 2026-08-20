# Free aim and AoE preview — recon

_Live-verified 2026-08-20 against the running game (tutorial mission `Tutorial_01`, Jane Kelly
holding a frag grenade, `X2TargetingMethod_Grenade_1` active, shot HUD raised). Every offset and
count below was read out of the running process through the dev harness (`:8774`) or a read-only
Frida pass; no writes, no patches. Script claims cite
`Development/SrcOrig/XComGame/Classes/` unless the Highlander's rebuild differs._

---

## 1. What it is

"Free aim" is the game's name for an ability whose target is **a point in the world**, not an
entry in a target list. `AvailableAction.bFreeAim` is set, `AvailableTargets` is **empty**, and
the aim point comes from wherever the 3D cursor happens to be.

Four targeting methods share the mechanism, and all four route through the same `XCom3DCursor`:

| Method | Aim shape | Cursor role |
|---|---|---|
| `X2TargetingMethod_Grenade` (+ `_GrenadePerkWeapon`, `_MECMicroMissile`, `_Fuse`, …) | ball of radius R at a point | `GrenadePath.GetEndPosition()` — the arc's landing point |
| `X2TargetingMethod_BlasterLauncher` | same, with a steerable path | same |
| `X2TargetingMethod_Cone` | cone from the shooter toward a point | `Cursor.GetCursorFeetLocation()` (`:183`) |
| `X2TargetingMethod_Line` | line from the shooter through a point | `Cursor.GetCursorFeetLocation()` (`:68`) |

`UITacticalHUD_AbilityContainer.IsTargetingMethodActivated()` names exactly this set
(`:182–192`). **One solution covers all four**, because all four answer "where am I aiming?" from
the same cursor object and expose the same three read-back functions
(`GetTargetLocations`, `GetAdditionalTargets`, `ValidateTargetLocations`).

Live snapshot at the time of writing:

```
UITacticalHUD.m_isMenuRaised = true            m_kAbilityHUD.m_iCurrentIndex = 3
m_arrAbilities[3] = { AbilityObjectRef 706, AvailableTargets = 0, bFreeAim = TRUE,
                      AvailableCode 'AA_Success', ShotHUDPriority 1300 }
TargetingMethod  = X2TargetingMethod_Grenade  @0x7FF3F8FF1300
  Ability        = XComGameState_Ability 706  m_TemplateName 'ThrowGrenade'
                   OwnerStateObject 97, SourceWeapon 100
  MarkedTargets  = 4 actors                   CachedTargetLocation (1391, 1103, 19.5)
  bFriendlyFireAgainstUnits = TRUE            bFriendlyFireAgainstObjects = false
  GrenadePath    = XComPrecomputedPath @0x7FF40A184100, iNumKeyframes 62,
                   akKeyframes[61].vLoc = (1391, 1103, 23), TouchEvents = 2
XCom3DCursorMouse  Location (1391, 1103, 150.5)   ChainedPawn = XComHumanPawn (the thrower)
                   m_fMaxChainedDistance = 960     m_fMinChainedDistance = 0
```

960 units = **15 m = 10 tiles** (96 units/tile). That is `METERSTOUNITS(GetAbilityCursorRangeMeters())`,
written by `X2TargetingMethod_Grenade.Init` (`:43`).

---

## 2. What a sighted player sees

Nothing in the widget tree — and this is worse than the tactical HUD's usual write-only-to-Flash
problem, because most of the information here is **rendered into the 3D world, not into the UI at
all**:

| On screen | Drawn by | Retained where |
|---|---|---|
| A ring of highlighted tiles under the blast | `X2TargetingMethod.DrawAOETiles(Tiles)` (native) → `AOEMeshActor` | nowhere — instanced meshes |
| A translucent blast sphere, blue / **amber when it would hit a friendly** | `DrawSplashRadius()` → `ExplosionEmitter` | nowhere — a particle system |
| Red outlines on every unit and object inside the blast | `MarkTargetedActors()` (native) | **`MarkedTargets` — readable** |
| The thrown grenade's arc, drawn as a ribbon | `GrenadePath.DrawPath()` | **`akKeyframes[0..iNumKeyframes-1]` — readable** |
| The cursor itself, sitting on the aim tile | `XCom3DCursor` | **`Location` — readable** |
| Ability name, help text, damage range, "will end turn", "will break concealment" | `UITacticalHUD_ShotHUD.AS_SetShotInfo` / `AddDamage` | nowhere (Flash) — but re-derivable |

Two things a sighted player conspicuously does **not** get, which bounds what we may say (§9):

- **No hit percentage and no per-target damage.** The enemy strip is empty during free aim —
  live: `UITacticalHUD_Enemies.m_arrTargets = 0`, `CurrentTargetIndex = -1`,
  `UITargetingReticle.m_kTarget = null`, `m_fAimPercentage = -1`. The shot HUD shows one damage
  band for the ability, not per victim.
- **No named list of who is in the blast.** They see outlines in the world and count them by eye.

---

## 3. What a blind player gets today — and how it fails

The existing targeting layer (`src/tactical_hud.cpp` + `X2AccessTacticalReader.uc`) handles free
aim by *declining* to describe it. From this session's speech ring:

```
265: ! Free aim: Бросок осколочной гранаты, 4 of 5. Free aim, no target
270: ! Free aim: Бросок осколочной гранаты, 4 of 5. Free aim, no target
```

That is `DescribeTarget()`'s explicit `if (Action.bFreeAim) return "Free aim, no target";`
(`X2AccessTacticalReader.uc`) — correct as far as it goes, because `ResolveShot` indexes
`Action.AvailableTargets`, which is empty here.

The failure is not silence, it is **worse than silence**. Lines 266-267 of the same ring:

```
266: ! Dialog
267:   ВНИМАНИЕ: ОГОНЬ ПО СВОИМ. Вы собираетесь открыть огонь по своим! ...
```

The player pressed confirm and got the friendly-fire modal, because the aim point they could not
perceive was sitting on their own squad. Live confirmation of exactly that, read off the method
object at the moment of writing:

```
MarkedTargets[0] XGUnit_4  ObjectID 106  m_eTeam = eTeam_Neutral   (a civilian)
MarkedTargets[1] XGUnit_3  ObjectID 102  m_eTeam = eTeam_XCom      (their own soldier)
MarkedTargets[2] XComDestructibleActor_141
MarkedTargets[3] XComInteractiveLevelActor_0
bFriendlyFireAgainstUnits = TRUE
```

Note also line 259 — the object scanner already says **`Бросить гранату сюда`** ("throw a grenade
here"), reading the tutorial's required-action marker. So the player is told exactly where to
throw and has **no way whatsoever to aim there**. The verb is missing, not the information.

**There is no keyboard aiming path in vanilla at all.** `AbilityRequiresTargetingActivation()`
opens with `if (!` `` `ISCONTROLLERACTIVE ``  `) return false;` (`UITacticalHUD_AbilityContainer.uc:155`, function at `:151`),
so on mouse+keyboard the entire controller aiming flow — including the D-pad branch that would
move the cursor — is dead code. What the arrow keys actually do on KB+M while a grenade is up:

- `UITacticalHUD.OnUnrealCommand` calls `m_kAbilityHUD.SetSelectionOnInputPress(cmd)` on **press**,
  unconditionally (`:405`). With the controller branch skipped, Left/Right land in
  `CycleAbilitySelection(±1)` and Up/Down in `CycleAbilitySelectionRow(±1)` — i.e. **an arrow key
  abandons the grenade and selects a different ability.**
- Anything not consumed falls to `XComTacticalInput.state UsingTargetingMethod.ArrowLeft/…`, which
  scrolls the camera.

Neither moves the aim point. The aim point is the mouse, full stop.

---

## 4. The two engine facts that decide the architecture

### 4.1 The aim point *is* the 3D cursor — the object we already drive

`X2TargetingMethod_Grenade.GetSplashRadiusCenter()`:

```unrealscript
if (UseGrenadePath()) Center = GrenadePath.GetEndPosition();
else                  Center = Cursor.GetCursorFeetLocation();
if (SnapToTile && !SkipTileSnap) { ...walk SnapTile.Z down until GetFloorPositionForTile() ... }
```

and `GrenadePath` is itself re-solved toward the cursor every frame. So **`CursorSetLocation` is
the aim verb**, and `src/tile_reader.cpp` + `X2AccessTileReader.uc` already own a working,
live-verified implementation of it: compass stepping, floor jumps, terrain following, cover and
fog readout. Free aim is not a new navigation surface — it is the existing tile cursor, pointed at
a different consumer.

Three properties of targeting mode make this *easier* than world mode, not harder:

- **The cursor is range-clamped for free.** `Init` sets `m_fMaxChainedDistance` to the ability's
  cursor range and `ChainedPawn` is the thrower, so `ProcessChainedDistance` clamps every push to
  the throw sphere. In world mode this clamp is off and only the pathing pawn is limited.
- **Floor changes still work.** `BeginState` sets `m_bAllowCursorAscensionAndDescension = false`
  (live-confirmed: it reads `false` right now), but `XCom3DCursor.DisableFloorChanges()` is
  `return false;` with the flag check **dead below it** — "disabled for x2, but leaving this
  function here" (`:405-411`). `m_bCustomAllowCursorMovement` likewise only drives the gamepad
  ribbon UI. Neither flag blocks a push.
- **Confirm and cancel already have keys.** `Enter` / `Space` → `OnAccept()` → `ConfirmAbility()`;
  `Escape` → `CancelTargetingAction()` (`UITacticalHUD_AbilityContainer.OnUnrealCommand:363-395`).
  We need claim neither.

### 4.2 The mouse re-drives the cursor 60 times a second — a second mouse gate is required

`state UsingTargetingMethod.PostProcessCheckGameLogic` calls `Mouse_CheckForFreeAim()` every
frame (`:1104`), and that function pushes the mouse pick point — or the hovered unit's location —
straight through `CursorSetLocation`. It is the exact structural twin of `Mouse_CheckForPathing`,
which cost this project a full debugging cycle in world mode before `src/mouse_gate.cpp` landed.

**Measured live (read-only Frida, hook on `execSetLocation` at `base+0x474940`, filtered on the
cursor instance, 3.001 s sample):**

```
processEventCalls          32209      (positive control — hooks are live)
setLocationCallsAll          359
setLocationCallsOnCursor     180   ->  60.0 per second
distinctCursorLocations    ["1391.0,1103.0,150.5"]     <- ONE value; the mouse never moved
```

Exactly one push per frame, with a stationary mouse and an unchanging destination. A keyboard push
would be reverted within one frame. `GetMouseInterfaceTarget()` returning `none` (the function's
early-out) is **not** a reprieve — it plainly is not returning none here.

#### The gate target is state-scoped, and our finder cannot reach it

This is the one piece of new machinery the feature needs.

`Mouse_CheckForPathing` is declared at **class scope** (`XComTacticalInput.uc:3204`, column 0),
which is why `GameUI::FindScriptFunction` — a walk of `UClass::Children` plus supers — resolves it
today. `Mouse_CheckForFreeAim` is declared **inside two states** (`:927` in
`UsingSecondaryTargetingMethod`, `:1131` in `UsingTargetingMethod`, both tab-indented), so in the
object graph it hangs off a `UState`, one level deeper. Confirmed three ways:

- `generated_sdk/X2SDK/ObjectDump.txt` lists
  `State XComGame.XComTacticalInput.UsingTargetingMethod` and, under it,
  `Function XComGame.XComTacticalInput.UsingTargetingMethod.Mouse_CheckForFreeAim`.
- The generated SDK emits `UXComTacticalInput::Mouse_CheckForPathing()` and **no**
  `Mouse_CheckForFreeAim` anywhere.
- Live walk (Frida, read-only):

  ```
  XComTacticalInput UClass @0x7ff44623a200
    states: ActiveUnit_Moving, Cinematic, Multiplayer_GameOver, Multiplayer_Inactive,
            UsingTargetingMethod, UsingSecondaryTargetingMethod, InReplayPlayback,
            SinglePlayerLost, InTransition
    Mouse_CheckForFreeAim at CLASS scope ....... NOT FOUND
    Mouse_CheckForPathing at CLASS scope ....... 0x7ff40a917400   (== what MouseGate logged)
    UsingTargetingMethod          @0x7ff40ac0ec00
      Mouse_CheckForFreeAim       @0x7ff40a93cd00  flags 0x820102  Func 0x7ff7e4b13f70
    UsingSecondaryTargetingMethod @0x7ff40ac0ee80
      Mouse_CheckForFreeAim       @0x7ff40a93df00  flags 0x820102  Func 0x7ff7e4b13f70
  ```

`flags 0x820102` carries no `FUNC_Native` (0x400) and `Func` is `ProcessInternal`
(`0x7ff7e4b13f70`) — **byte-identical setup to the function the gate already flips.** So
`src/mouse_gate.cpp`'s technique transfers verbatim; only the *lookup* is new.

Two consequences:

1. `GameUI::FindScriptFunction` needs a state-aware sibling —
   `FindStateFunction(class, state, func)`: walk `UClass::Children` for a `UField` whose class is
   `State` and whose name matches, then walk **its** `Children` for the `Function`. Field offsets
   are already known and pinned (`UField::Next` +0x60, `UStruct::Children` +0x80).
2. **This was already on the roadmap.** `docs/object-scanner-recon.md` §4.6 recommends exactly this
   extension so `Key_Left_Shift` can be neutered ("the only new machinery ... worth having
   regardless, since every other `Key_*` we might ever need to neuter is state-scoped too"). Free
   aim is its second and more demanding customer, and unlike the Shift case it is not optional.

There are **two** copies to gate, one per state. `UsingSecondaryTargetingMethod` is the
secondary-movement variant (grenade after a move); gate whichever state the controller is in, or
simply gate both while free aim is ours — they are distinct `UFunction`s and flipping one does not
touch the other.

---

## 5. What is readable — the data inventory

Everything the readout needs already exists on live objects. Nothing here needs a hand-transcribed
offset in `game_ui.cpp`: the companion reads it from script, exactly as
`X2AccessTacticalReader` does.

### 5.1 On the targeting method (`UITacticalHUD.m_kAbilityHUD.TargetingMethod`)

| What | Source | Notes |
|---|---|---|
| **Where the blast will actually land** | `GetTargetLocations(out)` | For grenades this is the **arc's end**, not the cursor. See §5.4. |
| **Who is in it (authoritative)** | `GetAdditionalTargets(out AvailableTarget)` → `Ability.GatherAdditionalAbilityTargetsForLocation` | Object IDs, already filtered by `CheckMultiTargetConditions`. **This is what `ConfirmAbility` packs into the context**, so it is what will be hit. |
| **Who is highlighted (parity)** | `MarkedTargets` (array of `Actor`) | What the player *sees* outlined: units, destructibles, interactives. Superset of the above. |
| **Friendly fire** | `bFriendlyFireAgainstUnits` / `bFriendlyFireAgainstObjects` / `bSeriousConsequencesForAction` | Recomputed by `CheckForFriendlyUnit` on every cursor move. **Live-read `true` right now.** This is the amber sphere, before the modal. |
| **Is this aim point legal** | `ValidateTargetLocations(locs)` | `AA_Success` / `AA_NotVisible` (squadsight-restricted abilities) / `AA_NoTargets`. Feed through `GetDisplayStringForAvailabilityCode` for the game's own localized wording. |
| The arc | `GrenadePath.akKeyframes[0..iNumKeyframes-1]`, `TouchEvents` | 62 keyframes live. See §5.4. |

`GetTargetedActors` / `MarkTargetedActors` / `DrawAOETiles` are `protected native` — not callable
from outside the method. `MarkedTargets` is a plain `var`, so reading it is fine.

### 5.2 On the ability (`XComGameState_Ability`)

| What | Source |
|---|---|
| Blast radius (unreal units) | `GetAbilityRadius()` → `X2AbilityMultiTarget_Radius.GetTargetRadius(self)`; for a cone it returns `ConeLength` |
| Throw range (meters) | `GetAbilityCursorRangeMeters()` → `X2AbilityTarget_Cursor.GetCursorRangeMeters` |
| Damage band | `GetDamagePreview(TargetRef, out Min, out Max, out AllowsShield)` — same call `UITacticalHUD_ShotHUD` makes |
| Destruction | `GetEnvironmentDamagePreview()` (an `event int`) |
| Name / help | `GetMyFriendlyName()`, `GetMyHelpText()` |
| Turn cost | `WillEndTurn()`, `MayBreakConcealmentOnActivation(id)` |

The exact AoE **tile list** is available too — `X2AbilityMultiTarget_Radius.GetValidTilesForLocation(Ability, Location, out ValidTiles)`,
a public simulated native. Probably more than we want to speak, but it is there if a
"how many tiles across" phrasing is wanted.

### 5.3 On the cursor (`XCom3DCursor`)

`Location`, `m_fMaxChainedDistance` / `m_fMinChainedDistance` / `ChainedPawn`,
`m_iRequestedFloor` / `m_iLastEffectiveFloorIndex`, `CursorSetLocation`, `AscendFloor` /
`DescendFloor`, `GetCursorFeetLocation`. All already used by `X2AccessTileReader`.

### 5.4 ⚠ The blast centre is the arc's end, not the cursor

For grenades `UseGrenadePath()` is `true`, so `GetSplashRadiusCenter()` returns
`GrenadePath.GetEndPosition()` — where the thrown object *comes to rest* after the ballistic
solve — then snaps that down to a floor tile. Live right now the two agree exactly:

```
cursor         (1391, 1103, 150.5)   <- +CollisionHeight, as always
path end       (1391, 1103,  23  )   = akKeyframes[61].vLoc
blast centre   (1391, 1103,  19.5)   = CachedTargetLocation, floor-snapped
```

…because this throw is unobstructed. **When it is obstructed they must diverge** — that is the
whole point of a bounced grenade, and it is precisely the "obstacle" information the feature
needs. So the readout must **always report the resolved centre from `GetTargetLocations()`**, never
the tile we pushed. Same discipline as the tile reader's move-to guard, which commits only when
the pathing pawn's clamped destination equals the cursor tile.

> **Unverified, and the single most important thing to test first.** No obstructed throw has been
> observed yet. Aim past a wall and compare `GetTargetLocations()[0]` against the pushed tile; if
> they diverge, "lands N tiles short, against the wall" is derivable directly. `TouchEvents`
> currently holds 2 degenerate entries (start and end, `TravelDistance = 0`, no material, no
> normal), so it is **not** yet demonstrated to be a useful obstacle source — check whether a real
> bounce populates it before designing around it.

---

## 6. Architecture — who owns what

Straight down the project's existing seam, and mostly by *extension* rather than new modules.

**C++ (`src/`)**

- `src/mouse_gate.cpp` — a second gate target. Add `FindStateFunction` to `game_ui.cpp`, then gate
  `UsingTargetingMethod.Mouse_CheckForFreeAim` (and its `UsingSecondaryTargetingMethod` twin) on
  exactly the same engage/release discipline: engage on first free-aim key this activation,
  release when targeting ends, a recipe attaches, tactical ends, or the ASI unloads.
  The two gates are mutually exclusive by construction — `Mouse_CheckForPathing` is not called at
  all from `UsingTargetingMethod`, and `Mouse_CheckForFreeAim` is not called from the active-unit
  state — so the existing "release on targeting raised" rule stays correct and the new gate simply
  covers the other half of the mission.
- `src/tactical_hud.cpp` — owns the keys. It already computes the `menuRaised` gate and claims
  `Delete` / `H` there. Free-aim claims are conditional on the *method* being one of the four
  cursor-driven kinds, not merely on the HUD being raised.
- `src/tile_reader.cpp` — **`IsActive()` must stay false during targeting.** Do not re-enable it;
  the free-aim keys are a separate claim set with a separate gate, exactly as the object scanner
  shares `TileReader::IsActive()` rather than recomputing it.

**UnrealScript (`mods/X2AccessCompanion/`)**

- A new `X2AccessFreeAimReader.uc` (or an extension of `X2AccessTacticalReader.uc` — see §12),
  CDO-called, `NativeResult` out, value-typed state only. It owns: pushing the cursor, reading the
  method back, and every word spoken.
- Reuse, do not re-derive: `X2AccessTileReader.OffsetWords` (already static and shared, so there is
  exactly one calibration of north/east), `X2AccessHelper`'s cover/hazard/unit describers, and
  `X2AccessTacticalEvents.IsNarratable` / `UnitName` for fog parity and naming — so a unit named in
  the blast reads the same as one named in a combat line.

**The push itself must not reuse `SetCursorTile` unchanged.** See §10.1 — this is a real hazard.

---

## 7. Input

Free aim is a **third tactical mode**, and the CLAUDE.md rule "targeting mode: the GAME owns the
arrows, we claim nothing that steers" was written for over-the-shoulder shots and does not fit
here. In a discrete-target shot the arrows genuinely steer the game's own selection; in free aim
they cycle *away from* the ability the player just chose (§3). Claiming them costs the player
nothing they were using.

The tile reader stands down entirely while `menuRaised`, so **its whole keymap is free in this
mode**. Reusing the same keys for the same meanings is the point — one set of motor habits:

| Key | Free-aim meaning | What it does in vanilla targeting today |
|---|---|---|
| `↑ ↓ ← →` | move the aim point one tile (world-absolute compass, same as the tile cursor) | cycles the selected **ability** (`SetSelectionOnInputPress`), else scrolls the camera |
| `Shift+,` / `Shift+.` | aim point down / up one floor | — (`F` / `C` do this natively and keep working) |
| `Delete` | re-announce the full aim readout | already ours (`AnnounceShot`) — extend it |
| `H` | shot breakdown | already ours; for free aim, no breakdown exists — say so |
| `Enter` / `Space` | confirm | **do not claim** — the game's own confirm |
| `Escape` | cancel | **do not claim** — the game's own cancel |
| `F` / `C` | floor up / down | vanilla, leave alone (`C` is the tile cursor's recenter *in world mode only*, so no collision) |
| `1`–`9`, `Q` / `E` | ability hotkeys, camera yaw | vanilla, leave alone |

⚠ **`Shift` is not free**, and this is already-shipped debt: `Key_Left_Shift`'s *release* calls
`PrevUnit()` (`XComTacticalInput.uc:2152`), so the tile reader's existing `Shift+,` / `Shift+.`
floor keys cycle the soldier every time they are used. `docs/object-scanner-recon.md` §4.6 has the
fix — gate `Key_Left_Shift` with the same mouse-gate trick, which needs the same
`FindStateFunction` this feature needs. Worth landing together.

⚠ **Unverified:** whether `Ctrl+arrow` still reaches the *screen* (`OnUnrealCommand`) once the
arbiter claims bare arrows. The leniency finding (CLAUDE.md) is about `PlayerInput.Bindings`
matching; arrows reach the ability container through `UIMovie_2D.FilterButtonInput` instead, which
is a different path and may not consult modifiers at all. If `Ctrl+arrow` does *not* reach it,
ability cycling during targeting is still fully served by the `1`–`9` hotkeys, so nothing is lost
either way — but do not claim in the docs that Ctrl+arrow preserves it until it is measured.

### The composition worth building for

The object scanner's `/` already plants the tile cursor on the last-announced entry — and in this
very mission it announces `Бросить гранату сюда`. If `/` during free aim planted the **aim point**
instead, then "next marker, aim there, confirm" becomes three keys, and the tutorial's grenade
step is solved end to end with parts that already exist. Same for `B` (next marker) and `.` (next
enemy): aiming a grenade at an enemy is the single most common thing a player wants to do, and
the scanner already knows where every enemy is. Recommended for phase 2, called out here because
it should shape the phase-1 API (the push entry point wants to take a **tile**, not just a
direction).

---

## 8. The spoken line

Two lines, on the same split the tile cursor uses — a short line per step, the full picture on
demand.

**On every aim step** (must be short; this fires on every arrow press):

> `4 north, 2 east. 3 tiles from you. Blast: Sectoid, ADVENT Trooper.`

**Extra clause, only when true** — and this is the one that earns the feature:

> `… WARNING: Jane Kelly in blast.`

**On `Delete`** (full readout):

> `Throw Frag Grenade, 4 of 5. Aiming 4 north, 2 east, 3 tiles away, range 10.
> Blast radius 3 tiles: Sectoid, ADVENT Trooper, a crate. Damage 3 to 5.
> WARNING: friendly fire — Jane Kelly in blast. Will end turn.`

Composition rules:

- **Name the resolved centre, not the requested one** (§5.4). If they differ, say so first:
  *"blocked, lands 2 tiles short"*.
- **Friendly fire first among the warnings**, because it is the one that costs a soldier, and the
  game itself escalates it to a modal.
- **Empty blast is information**: *"Blast: nothing"* — a sighted player sees an empty ring
  instantly.
- Cover, hazards and fog for the aim tile come from the tile reader's existing describers, so the
  aim point reads like a tile the player already knows how to hear.
- Bearings via `X2AccessTileReader.OffsetWords`, already static and shared — there must remain
  exactly one definition of north.

---

## 9. Parity rules

1. **Say only what the world shows.** Marked outlines, the tile ring, the sphere colour and the arc
   are all on screen; naming their contents is transcription, not a cheat.
2. **No per-target hit chance or per-target damage.** The game shows none during free aim (`m_arrTargets = 0`,
   `m_fAimPercentage = -1`). One ability-level damage band, as the shot HUD shows, and nothing more.
3. **Fog-gate the blast contents** through `X2AccessTacticalEvents.IsNarratable`. A grenade thrown
   into fog must not enumerate what is hiding there. Note the asymmetry that makes this safe:
   `MarkedTargets` is populated by a native call the game makes regardless of visibility, so it is
   a genuine leak vector — filter it, do not trust it.
4. **Friendly fire is not a leak.** The game states it outright in a modal, and the sphere turns
   amber before that. Saying it earlier is parity with the colour, not extra information.
5. **Do not announce the AoE tile list by default.** A sighted player sees a shape, not a count.
   Radius in tiles is a fair summary of the same thing.

---

## 10. Hazards — each of these will bite

### 10.1 ⛔ `X2AccessTileReader.SetCursorTile` zeroes the throw-range clamp

```unrealscript
Cursor.m_fMaxChainedDistance = 0;
Cursor.m_fMinChainedDistance = 0;
Cursor.CursorSetLocation(WorldData.GetPositionFromTileCoordinates(Tile), false, false);
```

with the comment *"Our keys stand down while the shot HUD is raised, so this can never loosen an
active targeting method's own clamp."* **Free aim invalidates that sentence.** Calling this during
targeting would silently delete the 960-unit throw limit the method installed in `Init`, letting
the player aim — and possibly throw — past the grenade's range, with `ValidateTargetLocations`
none the wiser (it only checks squadsight). Either add a free-aim push path that preserves the
chain distances, or save/restore them around the push. **Update that comment either way**; it is
now load-bearing and wrong.

### 10.2 The clamp does not land on tile centres

`ProcessChainedDistance` clamps to a *sphere*, so a push past range lands at an arbitrary point on
it, not a tile centre — and then `GetSplashRadiusCenter`'s tile-snap walks it somewhere else again.
Never assume the pushed tile is the aimed tile; read it back (§5.4). Prefer clamping in *our*
state, in tiles, before pushing, so the spoken tile and the engine's tile agree by construction.

### 10.3 Two snap layers

The tile reader deliberately passes `bSnapToValidPosition = false` (snap-on slid a floor-up push
two tiles sideways, measured 2026-08-20). The grenade method then applies its **own**
`SnapToTile` walk-down-to-floor in `GetSplashRadiusCenter`. These compose; verify a floor-change
push still resolves to the floor the player was told about.

### 10.4 The tutorial will not exercise the confirm path honestly

The tutorial is a replay: `XComTutorialMgr.HandleSubmittedGameStateContext` discards the player's
context, so `ConfirmAbility`'s `ActivateAbility` returns through the
`bTutorialMgrInvalidSubmission` branch and nothing is submitted. **Aiming and reading are fully
testable here** — the targeting method, the cursor, `MarkedTargets` and the friendly-fire flags
are all driven by the UI layer, which behaves identically (this is the read-the-screen rule
earning its keep again). But "the grenade actually went where we said" needs Gatecrasher.

### 10.5 Three voices could land on one throw

The reveal of a grenade's damage can reach the player through `X2AccessTacticalEvents`
(live missions), `X2AccessReplayNarration` (the tutorial), and the `UIWorldMessageMgr` flyover poll
(tutorial only). Same caution as the hacking screen's phase 3: check each on the mission where it
actually fires.

### 10.6 `bFriendlyFireAgainstUnits` is stale between cursor moves

It is written by `CheckForFriendlyUnit` inside `Update`, and `Update` only recomputes when
`NewTargetLocation != CachedTargetLocation`. Read it *after* the push has been processed by at
least one frame, or the warning describes the previous aim point. The 150 ms poll already in
`tactical_hud.cpp` is comfortably slower than a frame, so polling is safe; a same-frame read
inside the keypress handler is not.

---

## 11. Phasing

**Phase 1 — see where you are aiming, and aim at things. BUILT AND VERIFIED LIVE 2026-08-20.**
`FindStateFunction` in `game_ui.cpp`; second `MouseGate` target; free-aim detection in
`tactical_hud.cpp` (method is one of the four); arrow + floor claims; a push entry point taking a
**tile** (not a direction), preserving the chain clamp; lazy plant on the thrower's tile; the
short step line with bearings, distance and blast contents; friendly-fire warning; scanner
retarget on `/`, `B`, `.`. This makes the tutorial's grenade step completable in three keys.

**Phase 2 — the full readout.**
`Delete` gains radius, damage band, destruction preview and availability code.

**Phase 3 — the obstructed throw.**
Only after §5.4 is measured. Detect and speak a short landing; decide whether `TouchEvents` names
what it hit or whether the divergence alone is the honest report.

**Phase 4 — cone and line.**
Same cursor, different shape word. Cheap once phase 1 exists; needs a Flamethrower or a
Sharpshooter, i.e. neither the tutorial nor Gatecrasher.

---

### What phase 1 actually shipped

| Piece | Where |
|---|---|
| State-aware function finder | `GameUI::FindStateFunction` (`src/game_ui.cpp`), plus `FindFieldInStructRaw` |
| Second + third mouse gate | `MouseGate::SetFreeAimSwallow` — both targeting states (`src/mouse_gate.cpp`) |
| Keys and gate lifecycle | `src/free_aim.cpp` — arrows, `Shift+,` / `Shift+.` |
| Mode flag | `X2AccessTacticalReader.NativeState`, published by `TacticalHUD::OnTick`'s existing poll |
| Aiming, blast readout, warnings | `X2AccessTacticalReader.uc` free-aim section |
| Scanner retarget | `object_scanner.cpp`: gate widened, `/` routes to `FreeAim::AimAtMarker` |

Both halves compile clean (0 errors, 0 warnings); the Graph kernel suite still passes.

**First live run, `Tutorial_01`, 2026-08-20 19:01** — the tutorial's grenade step was completed
with it, which is the thing that was impossible before. What the run proved, in order: the entry
line (*"Free aim: Бросок осколочной гранаты, 4 of 5. Arrows to aim"*); `FindStateFunction`
resolving BOTH copies of `Mouse_CheckForFreeAim` (`UsingTargetingMethod` and its secondary twin,
each `flags 0x00820102`, `Func` = `ProcessInternal` — exactly what the read-only Frida pass had
predicted); both gates engaging and releasing, with the world-mode gate restored in the SAME
millisecond free aim went active; `/` retargeting onto the scanner's marker with the spoken
bearing matching the scanner's own line tile-for-tile; arrow stepping; the `Delete` readout; and
finally *"Нов. Jane Kelly: Бросок гранаты, хит."* — the throw itself. `PushAim`'s range check
never misfired, which was the biggest open risk.

⚠ **One bug, found and fixed the same run.** The divergence report fired a FALSE *"Blocked, lands
6 north, 1 west"* about a throw that then landed perfectly: the arc resolves a frame AFTER the
push, so the first poll following an aim move can still be describing the previous aim point —
here the pre-jump arc, still pointing back at the shooter. 150 ms of poll interval is not the
safeguard §5.4 assumed. A divergence must now survive two consecutive polls before it is spoken.

⚠ **Still open: whether `Blast: nothing` at the tutorial marker was honest.** No damage line
followed the throw, so it may be exactly right about UNITS — but whether
`GatherAdditionalAbilityTargetsForLocation` includes destructible cover at all is unmeasured, and
if it does not, the readout is silently missing the thing a grenade is most often aimed at. The
test is a `/props` dump of the live method's `MarkedTargets` against what we speak, next time a
grenade is up.

Three implementation choices worth knowing when reading it:

- **`PushAim` is the range check.** It pushes, then reads `GetCursorFeetLocation()` back — the
  exact inverse of the collision-height offset `CursorSetLocation` adds, so with snapping off it
  is precisely `ProcessChainedDistance`'s output. Compared as a horizontal DISTANCE with half a
  tile of slack, not as tile equality: the clamp lands on a sphere and has no reason to respect
  the tile grid. Guarded on `ChainedPawn != none && m_fMaxChainedDistance > 0`, so an ability
  with unrestricted range accepts unconditionally — the safe failure mode, since a misjudged
  range test would refuse every step and strand the player.
- **Blast contents come from `GatherAdditionalAbilityTargetsForLocation` asked about OUR tile**,
  not from the method's `MarkedTargets` cache, which makes it synchronous — the method only
  recomputes inside its own `Update()`, a frame after the push. Fog-gated through
  `X2AccessTacticalEvents.IsNarratable`.
- **The divergence report lives on the poll, not the keypress**, because the arc resolves a frame
  late. It compares horizontal tiles only (the floor snap moves Z routinely and means nothing)
  and is change-detected, so a blocked throw is announced once per distinct landing tile. This is
  the phase-3 instrumentation landing early: the first live run will say whether §5.4 is real.

## 12. Decisions (taken 2026-08-20)

1. **Claim the bare arrow keys** for aim-point movement, world-absolute compass, identical to the
   tile cursor. What this displaces is `CycleAbilitySelection`, which on KB+M *abandons the
   grenade* — no loss. Ability cycling stays on the `1`–`9` hotkeys. The Ctrl+arrow question in §7
   remains open but is no longer load-bearing.

2. **The aim point starts on the thrower's own tile.** Chosen over "nearest visible enemy" for a
   fully predictable origin: every subsequent step is a known offset from a position the player
   already knows, and there is never a hidden starting state to interrogate.

   The obvious objection — a reflexive confirm blasts your own squad — is answered by decision 3
   rather than by policy. Planting on yourself makes `bFriendlyFireAgainstUnits` true immediately,
   so the *first* thing spoken on plant is the warning clause naming the thrower. The dangerous
   default announces itself, and the game's own modal is still behind it. Implementation notes:
   plant lazily on the first free-aim key of each activation (RT/tile-cursor parity — a cold key
   must never act blind), and re-plant on every fresh activation rather than persisting across
   them, since "predictable origin" is the whole point.

3. **Per-step line: bearing, distance, blast contents**, plus the friendly-fire clause only when
   true. e.g. `4 north, 2 east. 3 tiles. Blast: Sectoid, ADVENT Trooper.` The full readout stays
   on `Delete`. Naming the occupants on every step is what makes sweeping the aim point usable,
   and it is what makes decision 2 safe.

4. **Scanner integration is in phase 1**, not phase 2. `/` aims at the last-announced scanner
   entry; `B` / `.` step the marker and enemy rings and retarget. This is the reason the push
   entry point **must take a tile, not a direction** — settle that in the phase-1 API rather than
   retrofitting it.

Phase 2 in §11 therefore reduces to the full `Delete` readout (radius, damage band, destruction
preview, availability code); phases 3 and 4 are unchanged.
