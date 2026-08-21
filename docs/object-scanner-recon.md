# Object scanner — recon

_Recon only, no code. Reference implementation: RTAccess (`E:/Games/modding/WH40KRTAccess`),
itself a port of WrathAccess. All XCOM claims cite the SDK's
`Development/SrcOrig/XComGame/Classes/` unless stated._

---

## 1. What the RT scanner actually is

Not "a list of stuff". Four separable pieces, and only three of them port:

| Piece | RT | Ports to XCOM? |
|---|---|---|
| **Registry** — one live proxy per world entity | `WorldModel.cs` (per-frame diff, stable proxy identity) | **No — and that's good news.** See §4.1 |
| **Taxonomy** — node keys per thing, one state-aware `Primary` | `ScanTaxonomy.cs` (17 keys), `ProxyMapObject.NodeSet()` | Yes, re-derived from XCOM's own predicates |
| **Browse** — category ring × distance-sorted item ring | `Scanner.cs` `Categories[]` + `StepItem`/`StepCategory` | Yes, nearly verbatim |
| **Act on selection** — interact/loot/travel from anywhere | `Scanner.Interact()` → `ClickMapObjectHandler` | **No.** XCOM interaction is adjacency-gated. See §4.3 |

Three hard-won lessons in RT's own audit (`WH40KRTAccess/docs/scanner-vs-wrathaccess-parity.md`,
`.claude/memory/rt-scanner-consistency.md`) that should be designed in from day one rather than
rediscovered:

1. **One visibility predicate, or you get three.** RT shipped category browse gated on `IsVisible`,
   the review cycle on `IsVisible && CurrentlySeen`, and the tile cursor on a live proximity query.
   A revealed-but-fogged object therefore appeared in the browse, vanished from the cycle, and was
   unreachable by the cursor — a playtester reported it as "the scanner shows things that don't
   exist". Fix: one `ScanItem.DetectableFrom(cursor)` used by every surface.
2. **Statics latch, creatures don't** — and the engine decides that, not the mod. RT got it right
   only once it read `IsRevealed` for map objects and `IsVisibleForPlayer` for units, because
   the engine refuses to latch units. XCOM hands us the same split for free (§3.1).
3. **Skip empty categories.** Without `NextNonEmptyCategory`, stepping the category ring lands
   the player on dead "…, empty" stops. Cheap to build, annoying to retrofit.

A fourth, from the same audit: **11 of 16 items in the first live dump were phantoms** — invisible
script volumes, secret doors, emptied containers. The gate on "is this a thing a sighted player can
see and act on" is the whole game.

---

## 2. The inventory — what XCOM 2 tactical actually contains

Everything below is a `XComGameState_*` object reachable from `XCOMHISTORY.IterateByClassType`,
so the companion can enumerate all of it. C++ cannot: the tactical widget tree holds no world
objects at all (CLAUDE.md, *The tactical HUD holds no text*).

### 2.1 Interactive objects — `XComGameState_InteractiveObject`

Extends `XComGameState_Destructible`, implements `Lootable` + `Hackable`. One class covering doors,
crates, terminals, ADVENT towers, objective devices. Classification comes from predicates, **not**
from a type field — `XComInteractiveLevelActor.ETypeOfActor` has exactly two values
(`Type_Default`, `Type_AdventTower`, `XComInteractiveLevelActor.uc:86`), so it is useless as a
taxonomy.

The usable classifiers:

| Signal | Where | Means |
|---|---|---|
| `IsDoor()` (native) | `XComGameState_InteractiveObject.uc` | door |
| `MustBeHacked()` / `bHasBeenHacked` / `LockStrength` | same | hackable terminal, and how hard |
| `HasLoot()` / `HasAvailableLoot()` | same | crate; *available* = already opened, ready to loot |
| `DetectionRange > 0 && !bHasBeenHacked` | `.uc:52`; ring rendered via `bHasDisplayRadius`/`kRangeMesh` (`XComInteractiveLevelActor.uc:169`) | ADVENT tower — breaks concealment inside the radius (`XComGameState_Unit.uc:8979`) |
| `XComGameState_ObjectiveInfo` component | attached in `XComTacticalMissionManager.CreateObjective_Interact` (`.uc:840`) | **this is a mission objective** |
| `GetRequiresObjectiveGlint()` | `XComGameState_InteractiveObject.uc` | Kismet-flagged objective shader |
| `InteractionAbilityTemplateName` / `HackAbilityTemplateName` | `XComInteractiveLevelActor.uc:216,219` | **the best name source we have** |

That last one is the find of this pass. Every interactive object names the ability that operates it,
and the ability templates are real, localized, and enumerable:
`Interact_OpenDoor`, `Interact_OpenChest`, `Interact_TakeVial`, `Interact_StasisTube`,
`Interact_PlantBomb`, `Interact_MarkSupplyCrate`, `Interact_ActivateAscensionGate`,
`Interact_ActivateSpark`, `Interact_AtmosphereComputer`, `Interact_UseElevator`,
`GatherEvidence`, `PlantExplosiveMissionDevice`, `Hack`, `Hack_Chest`, `Hack_ElevatorControl`
(`X2Ability_DefaultAbilitySet.uc:61-104,2157-2217,3525-3548`,
`X2Ability_DLC_Day90DefaultAbilitySet.uc:17-43`).
So the spoken label can be the game's own `X2AbilityTemplate.LocFriendlyName` — the same words the
ability button shows a sighted player — with `XComInteractiveLevelActor.FriendlyDisplayName`
(`localized string`, `.uc:197`) as the noun.

Note `Interact_UseElevator`: XCOM **does** have level changes, so RT's `levelchanges` category has a
real analogue rather than being a Rogue-Trader-ism.

Liveness gate (the analogue of RT's `IsScannable`): `IsEnabled && !bRemovedFromPlay &&
(MaxInteractionCount < 0 || InteractionCount < MaxInteractionCount)`. That is what
`CanInteractNormally` checks before the visualizer test, and it is what drops spent objects.

### 2.2 Destructibles — `XComGameState_Destructible`

Cars, fuel tanks, generators — the things you shoot to make an explosion. The game's own
"should the player see this" rule is in the unit-flag layer and is precise:

- `UIUnitFlagManager.uc:76,123` — a flag is created only for `DestructibleState.IsTargetable()`.
- `UIUnitFlag.uc:382` — hidden unless `CanXComSquadSeeTarget(ObjectID)`.
- `UIUnitFlag.uc:1568` / `UISimpleFlag.uc:387` — hidden when it is *not* a mission objective and
  `DestructibleActor.TotalHealth <= 1`.

`IsTargetable()` is native and reads `XComDestructibleActor_Toughness.TargetableBy`
(`None/XCom/Aliens/All`, `.uc:4-20`).

### 2.3 Ground loot — `XComGameState_LootDrop`

`TileLocation`, `LootableItemRefs`, `LootExpirationTurnsRemaining`, `GetLootingName()` (which
resolves through the source `Lootable`, falling back to `LastLootOwnerName`). The 3D marker's own
visibility gate is exact and quotable:

```unrealscript
LootDropMarker.SetVisible = (LootExpirationTurnsRemaining > 0 && HasAvailableLoot());
```
(`XComGameState_LootDrop.uc:131`)

So the parity rule and the "expires in N turns" detail line come from the same two fields. This is
one of the highest-value categories: the timer is invisible to a blind player and the loot is gone
in three turns.

### 2.4 Evac zone — `XComGameState_EvacZone`

`CenterLocation`, `Team`, `GetEvacZone(eTeam_XCom)`, `GetEvacMinMax2D(Center, out Min, out Max)`,
`IsUnitInEvacZone(Unit)` (`.uc:22,68,95,122`). A rectangle, not a point — so distance should report
the nearest edge (RT's `Bounds`/`NearestPoint` idea earns its keep here).

### 2.5 Reinforcements — `XComGameState_AIReinforcementSpawner`

`Countdown` (turns) and `SpawnInfo.SpawnLocation` (vector). Sighted twin: the flare + the counter in
`UIObjectiveList.uc:93-115` and `UITacticalHUD_Countdown.uc:22-41`. The location is the part a blind
player currently cannot get at all.

### 2.6 Hazards — `XComGameState_WorldEffectTileData`

One state object per effect instance: `WorldEffectClassName` plus `StoredTileData` (an array of
`{Tile, Data, ...}`) — `.uc:35-37`. This is the true analogue of RT's `ProxyAreaEffect`: a named blob
with a real extent, so "Fire, 12 tiles, 4 tiles across, north-east" is derivable. The existing
per-tile helper (`X2AccessHelper.DescribeHazards` → `TileContainsFire/Poison/Smoke/Acid`) answers a
different question and should stay for the tile cursor.

### 2.7 Vantage points — `X2GrapplePuck`

```unrealscript
native static function bool HasGrappleLocations(XComGameState_Unit Unit, optional out array<GrappleTarget> OutGrappleLocations);
```
(`X2GrapplePuck.uc:59`; `GrappleTarget` = `{Tile, TileLocation, OverhangLocation, WillBreakWindow}`)

Every roof/ledge the selected soldier can grapple to, from the game itself, only when that soldier
has the ability — i.e. exactly the blue pucks a sighted player sees. Parity-clean by construction.

### 2.8 Units — `XComGameState_Unit`

Predicates are abundant and native: `IsSoldier/IsCivilian/IsAlien/IsAdvent/IsRobotic/IsTurret`,
`IsAlive/IsDead/IsBleedingOut/IsUnconscious/IsIncapacitated`, `IsMindControlled`, `IsPanicked`,
`GetTeam()` (`eTeam_XCom / eTeam_Alien / eTeam_Neutral / eTeam_TheLost / eTeam_Resistance`),
`bRemovedFromPlay`, `GetMyTemplate().bIsCosmetic` (excludes the Gremlin) and
`.bDisplayUIUnitFlag` / `.bCanBeCarried` (`X2CharacterTemplate.uc:70,80,86`).

The sighted-parity oracle for "which enemies does the player know about" is the game's own
enemy-strip builder:

```unrealscript
GetUISummary_TargetableUnits(out arrVisibleUnits, out arrSSEnemies, out arrCurrentlyAffectable, CurrentAbility, HistoryIndex)
```
(`XComGameState_Unit.uc:12562`, consumed by `UITacticalHUD_Enemies.UpdateVisibleEnemies`,
`.uc:301-353`)

### 2.9 Deliberately *not* items

- `XComGameState_SquadVisiblePoint` — invisible scripted trigger volumes. RT's phantom problem
  in its purest form.
- `XComGameState_SquadViewer` — a vision source, not a thing.
- `XComGameState_IndicatorArrow` — the game's own off-screen pointer (`Location`/`Unit`, `Icon`,
  `CounterValue`). Tempting, but it *duplicates* whatever it points at; better as a tiebreak
  ("this is the one the game is pointing at") than a category.
- `XComGameState_WorldNarrativeActor` — narrative trip-points. Unclear whether a sighted player
  perceives them at all; leave out until someone looks at one on screen.
- `XComPathingPawn`'s marker arrays (`NoiseMarkers`, `ConcealmentMarkers`, `LaserScopeMarkers`,
  `KillZoneMarkers`, `.uc:202-206`) — these are per-*path* preview markers (overwatch cones,
  killzones, noise), all `private`. Real and valuable, but they belong to a future "read the path
  I'm about to walk" aid, not to an object scanner.
- Initiative order — cheat-gated in retail (already in CLAUDE.md).

---

## 3. Proposed taxonomy

Ordered as the category ring would be. Each row: what produces it, the parity gate, the label
source, the detail line.

### 3.1 The two visibility gates (design these once)

XCOM hands us RT's hard-won statics-latch/units-don't split for free:

- **Statics** (interactive objects, destructibles, loot drops, hazards) → the fog memory of their
  tile: `XComWorldData.GetTileFOWValue(Tile, out FOWTileStatus, out flags)` (`.uc:869`).
  `NeverSeen` → not listed; `HaveSeen` → listed (the player remembers the crate); `Seen` → listed
  with live state. This is already the tile reader's rule, so the two surfaces agree by
  construction — which is precisely what RT failed to do.
- **Units** → the visibility manager, never latched:
  `X2TacticalVisibilityHelpers.CanXComSquadSeeTarget(id)` (`.uc:81`) or
  `GetAllVisibleObjectsForPlayer(playerId, out refs, , , IncludeNonUnits)` (`.uc:169`).

Exception, and it is the game's own: an object carrying an `XComGameState_ObjectiveInfo` component
is shown regardless (`UIUnitFlag.uc:1568`, `UISimpleFlag.uc:387`). Objectives are never fog-gated.

### 3.2 The categories

| # | Key | Source | Gate | Label | Detail |
|---|---|---|---|---|---|
| 1 | `units.squad` | `XComGameState_Unit`, `GetTeam()==eTeam_XCom` | always | `GetName(eNameType_Full)` | HP, action points, status effects |
| 2 | `units.enemies` | units on `eTeam_Alien` | `CanXComSquadSeeTarget`; squadsight enemies flagged as such (mirrors `m_arrSSEnemies`) | name + rank | HP, cover vs. me, flanked, overwatching |
| 3 | `units.lost` | `eTeam_TheLost` | same | "Lost" | count-heavy — announce as a swarm, see §5 |
| 4 | `units.allies` | friendly non-squad: `eTeam_Resistance`, mind-controlled, ADVENT turned | same | name | — |
| 5 | `units.civilians` | `eTeam_Neutral` / `IsCivilian()` | same | name / "Civilian" | VIP, hostage |
| 6 | `units.down` | `bCanBeCarried && (IsBleedingOut() ‖ IsUnconscious() ‖ IsIncapacitated())` | same | name | "bleeding out, N turns" — a body to recover |
| 7 | `markers` | objective objects, glints, highlights **and tutorial required-action markers** — see §3.4 | **ungated** | ability `LocFriendlyName` / the required action | hacked/interacted state; "move here" / "throw a grenade here" |
| 8 | `objects.doors` | `IsDoor()` | FOW `HaveSeen` | "Door" | open/closed, locked (`LockStrength`) |
| 9 | `objects.hackable` | `MustBeHacked() && !bHasBeenHacked` | FOW | "Hack" / `Hack_Chest` name | `LockStrength`, reward count |
| 10 | `objects.crates` | `HasLoot()`, live interaction | FOW | `Interact_OpenChest` name | opened / sealed |
| 11 | `objects.interact` | remaining live `InteractiveObject` | FOW | ability `LocFriendlyName` | uses remaining |
| 12 | `objects.towers` | `DetectionRange > 0 && !bHasBeenHacked` | FOW | "ADVENT tower" | detection radius in tiles — **only meaningful while concealed** |
| 13 | `objects.levelchange` | `InteractionAbilityTemplateName == 'Interact_UseElevator'` etc. | FOW | ability name | — |
| 14 | `objects.destructibles` | `Destructible.IsTargetable()` | `CanXComSquadSeeTarget` **and** (objective ‖ `TotalHealth > 1`) | archetype name | HP |
| 15 | `ground.loot` | `XComGameState_LootDrop` | `LootExpirationTurnsRemaining > 0 && HasAvailableLoot()` | `GetLootingName()` | **"expires in N turns"** |
| 16 | `ground.evac` | `XComGameState_EvacZone` (XCom) | placed | "Evac zone" | size, how many of the squad are inside |
| 17 | `ground.reinforcements` | `XComGameState_AIReinforcementSpawner` | exists | "Reinforcements" | **"landing in N turns"** |
| 18 | `hazards` | `XComGameState_WorldEffectTileData` grouped by `WorldEffectClassName` | tiles at FOW `Seen` | "Fire" / "Smoke" / "Poison" / "Acid" | extent in tiles |
| 19 | `vantage` | `X2GrapplePuck.HasGrappleLocations(activeUnit)` | ability present | "Grapple point" | height above me, `WillBreakWindow` |

Nineteen is a lot to ring through. `NextNonEmptyCategory` collapses it hard in practice — a typical
mission populates maybe six. If it still reads long, the fallback is RT's second surface: dedicated
cycle keys for the two or three that get used constantly (enemies, objectives) with the ring for the
rest. Recommend shipping the ring first and measuring.

### 3.3 The `markers` group (category 7) — waypoints, not furniture

Per the tutorial investigation (`docs/plans/guided-whispering-lattice.md`, *Object scanner
integration*), category 7 is not "objective objects" but a **markers / waypoints** group: the
world-space things the game points the player at. Members:

- interactive objects carrying an `XComGameState_ObjectiveInfo` component;
- `SeqAct_SetObjectiveShaderEnabled` glints and `SeqAct_HighlightObjectiveAbility` highlights
  (×1 and ×4 respectively in `Tutorial_01` alone, so the group generalises past the tutorial);
- the evac zone (also its own category — a marker entry is the *pointer*, category 16 is the zone);
- **the tutorial's required-action marker.**

Being one group is the point: a tutorial waypoint and an objective glint are the same kind of
thing to a player trying to find where to go, and folding them together is what stops the
tutorial being a special case. Gets `B` as its dedicated cycle key (§4.5).

#### Reading the tutorial marker — four rules, all established live 2026-08-20

1. **Scan the CONTEXT, never the mesh components.** The six `StaticMeshComponent`s on
   `XComTutorialMgr` are a presentation artifact and are routinely hidden at `(0,0,0)` while an
   action is still required — verified with `NextPlayerAbilityContext != none` and five of six
   markers hidden. `UseTurnBeginTargetPrompt` is **false** in shipped `GameCore`, so markers exist
   only where a map author dropped a `SeqAct_ShowTutorialTarget` (20 of those against 55
   tutorial-box beats in this map). The authoritative source is
   `XComTutorialMgr.NextPlayerAbilityContext`.
2. **Gate every read on `CurrentHistoryFrame`.** `XComTutorialMgr extends XComReplayMgr` — the
   tutorial is a *replay*, so the entire future of the mission is already in the history at mission
   start. An ungated read speaks the future.
3. **Parity guard.** Read only `AbilityTemplateName`, `SourceObject`, `PrimaryTarget`,
   `TargetLocations`, `MovementPaths[0]` — the fields the marker actually renders. The same context
   carries `ResultContext`, the recorded hit/miss of scripted shots; speaking that leaks the outcome
   of a shot the player has not taken.
4. **The scanner is not the only route.** A marker is an *imperative*; the scanner is a *pull*
   surface. Phase 1/2 of the tutorial plan adds the push and a `T` re-state key separately — the
   scanner is where you go to *re-find* the marker, not to learn it exists. Coordinate so the two
   do not double-speak.

`AbilityTemplateName` is what lets an entry say *"move here"* / *"throw a grenade here"* /
*"shoot that unit"* instead of "unknown marker". Which marker to render follows the ability's
targeting method, mirroring `SetTargetLocationMarkers`:

| Targeting method | Read from |
|---|---|
| *(move)* | `MovementPaths[0]`: `MovementTiles[last]`, `WaypointTiles[0..1]` |
| `X2TargetingMethod_Grenade` | `TargetLocations[0]` |
| `X2TargetingMethod_OverTheShoulder` | `PrimaryTarget` unit's `TileLocation` |
| `X2TargetingMethod_EvacZone` | `TargetLocations[0]`, snapped to tile |

**This needs no offsets.** The plan doc verified C++ offsets against the running class
(`NextPlayerAbilityContext` at `+0x0338`, `CurrentHistoryFrame` at `+0x0274`), and those stay
useful as a cross-check — but the companion can reach all of it in script:
`` `TUTORIAL `` is `XComTutorialMgr(XComTacticalGRI(WorldInfo.GRI).ReplayMgr)`
(`Core/Globals.uci:193`), `NextPlayerAbilityContext` is a plain public `var`
(`XComTutorialMgr.uc:40`), and `CurrentHistoryFrame` is `protectedwrite`, i.e. publicly readable
(`XComReplayMgr.uc:13`). So this category obeys §4.2 like the rest — model in script, no new
`static_assert`s.

### 3.4 The spoken line

Reuse what the tile reader already says, so the two surfaces sound like one mod:

```
<label>[, <detail>], <N> tiles, <compass offset>[, <reach>]
```

`X2AccessTileReader.DescribeOffset` already produces world-absolute compass words
("3 north, 2 east, 1 up") and `DescribeReachability` already produces "1 action" / "Dash" /
"Out of range" from the active unit's `m_kReachableTilesCache`. Both lift straight into
`X2AccessHelper` for shared use.

---

## 4. Architecture fit

### 4.1 No registry. XCOM's history replaces it.

RT needs `WorldModel.Tick()` — a per-frame diff holding one stable proxy per entity — because Unity
entities are mutable references and a per-call snapshot would make fresh proxies each frame,
destroying enter/exit detection.

XCOM needs none of that. A `StateObjectReference`/`ObjectID` is a **stable integer** and the history
is versioned: the correct way to touch an object is already
`XCOMHISTORY.GetGameStateForObjectID(id)` on every access. So:

- Selection state = one `int SelectedObjectID` + one `int CategoryIndex`. Both value-typed —
  which also satisfies the CDO rule (never hold object references on a CDO; GC).
- The candidate list is rebuilt per keypress from `IterateByClassType`. No tick, no cache, no
  stale-pointer class of bug. This is strictly simpler than the reference implementation, and it
  is the same reason CLAUDE.md's "never cache UObject pointers across frames" rule stops biting.

### 4.2 Which layer owns what

Same split as the tile cursor, for the same reason (the map is not a widget tree):

- **Companion** (`X2AccessObjectScanner.uc`, new): the whole model. Enumerate, filter, classify,
  sort, format. Writes `NativeResult`, exactly like `X2AccessTileReader`.
- **C++** (`src/object_scanner.cpp`, new): the keys and the speech. One outbound
  `GameUI::FindCompanionCDO` + `CallScriptFunction` per press, read `NativeResult` back. Gating
  copied from `TileReader::ComputeActive` — world mode only, stand down while `m_isMenuRaised`
  (targeting owns the keys) or a graph recipe is attached.
- **No new offsets, no new `static_assert`s.** Like the targeting layer (CLAUDE.md, *Targeting mode
  is covered*), everything lives next to the data in script, so an SDK regeneration or a Highlander
  bump cannot silently break it.

Shared state with the tile cursor: the scan origin. RT's `ScanFrom()` is "the planted cursor, else
the anchor unit". Ours should be the same — tile cursor if planted this mission, else the active
soldier's tile — so browsing and stepping describe the same world from the same place.

### 4.3 The action verb is "go there", not "interact"

RT's `I` key interacts with the selection across the room, because
`ClickMapObjectHandler` walks a unit over and acts. XCOM does not work that way:
`X2Condition_Interactive` requires an interaction point from the unit's **current** tile
(`X2Condition_UnitInteractions.GetUnitInteractionPoints` →
`XComWorldData.GetInteractionPoints(pos, 8.0, 90.0, out)`, `XComWorldData.uc:896`), and the
`Interact` ability costs an action point and appears on the ability bar only once the soldier is
standing there.

So the scanner's action on a selection is **plant the tile cursor on it** — and the existing
machinery takes over: Backspace's two-press move-to walks the soldier, the ability bar (tactical HUD
layer) then offers `Interact`/`Hack` because the game itself decided they are available. That is
better parity than RT's version, not worse: a sighted player also has to walk over.

### 4.4 Keys — PageUp/PageDown are claimable; Shift is the modifier to avoid

**The key set is the union of two ini sections, not one.** `XComTacticalInput extends XComInputBase
extends PlayerInput`, and UE3 inherits `config(Input) array<KeyBind> Bindings` down the class chain
while the tactical section's `.Bindings=` lines *append*. So tactical is governed by
`[Engine.PlayerInput]` (`DefaultInput.ini:21-248`) **plus** `[XComGame.XComTacticalInput]`
(`:249-415`). Reading only the second section under-counts.

Bound across both:

> A-G, J-P, Q-Z, **U**, 0-9, NumPad0-9, F1-F12, arrows, Enter, Space, Escape, Tab, Backspace,
> Delete, Insert, Home, End, PageUp, PageDown, Pause, LeftAlt, LeftControl, LeftShift,
> RightControl, RightAlt, RightShift, numpad + − × ÷, mouse buttons.

Genuinely unbound: **H and I** — the only two free letters — and every punctuation key: comma,
period, semicolon, apostrophe, brackets, slash, backslash, minus, equals, tilde. (`Period` appears
once, in a `-Bindings=` line that *removes* the editor's Ctrl+Shift+Period shader recompile, so it
is free.) This also confirms independently why `H` was the right pick for the shot breakdown, and
corrects an earlier draft that listed `U` as free — `U_Key_Press` is bound under DEBUG UI TRIGGERS
at `DefaultInput.ini:84`.

#### Modifier chords are first-class — but matching is LENIENT (settled live 2026-08-20)

`Engine/Classes/Input.uc:12-34`:

```unrealscript
struct native KeyBind
{
    var config name   Name;
    var config string Command;
    var config bool   Control, Shift, Alt;
    var config bool   bIgnoreCtrl, bIgnoreShift, bIgnoreAlt;
    ...
}
```

The `FKeyBind()` ctor sets all six bools `FALSE`. **`Control`/`Shift`/`Alt` are the modifiers a
chord REQUIRES. Modifiers the player merely happens to hold are NOT checked.** The `bIgnore*` trio
is the opt-in suppressor, and `Input.uc` says so in the struct itself:

```unrealscript
/** if true, the bind will not be activated if the corresponding key is held down */
var config bool bIgnoreCtrl, bIgnoreShift, bIgnoreAlt;
```

So the default is lenient: a bare binding fires under any modifier. The whole shipped game config
uses `bIgnore*` exactly twice — `R` carries `bIgnoreShift=True` so `Shift+R` reaches ErrorReport
without also reloading, and `F4` carries `bIgnoreAlt=True` so Alt+F4 does not also fire the game
action. Those two entries are easy to misread as evidence of STRICT matching; they are the exact
opposite, because suppression is only necessary when matching is lenient.

**Verified live, three ways, in a tutorial mission 2026-08-20:** `Shift+Z` and `Ctrl+Z` both zoomed
the camera exactly as bare `Z` does; `Ctrl+E` rotated the camera though `E` carries no modifier
binding; and — the one that matters for the design — bare `Left` drove the tile cursor with no
camera movement while `Ctrl+Left` scrolled the camera, because the arbiter claims the bare key and
lets the Ctrl chord through.

⇒ Two consequences, and the second is a standing rule:

- `Ctrl+PageUp`, `Shift+PageUp`, `Ctrl+Shift+PageUp` (and the PageDown equivalents) still collide
  with nothing — but now because bare PageUp/PageDown are **unbound**, so there is no vanilla action
  to leak through. The conclusion survives; the reason changed.
- ⛔ **Any modifier chord we advertise on a key that DOES carry a vanilla bare binding must be
  claimed by the arbiter, or it double-fires.** `Ctrl+Tab` is not "free": it reaches
  `eTBC_NextUnit` unless we swallow it. It is free *because* we claim it.

#### Bare PageUp/PageDown do nothing in tactical

`PageUp_Key_Press` (`XComPlayerController.uc:1074`) → `XComInputBase.InputEvent(FXS_KEY_PAGEUP)`
(`.uc:615`) → `Key_PageUp(ActionMask)`, declared body-less at `XComInputBase.uc:1114` with **no
override in any of the 15 `SrcOrig` packages**. The only consumers of `FXS_KEY_PAGEUP` anywhere are
`UIKeybindingsPCScreen` (tab switching) and `UIOptionsPCScreen` — both shell screens, and our claims
already stand down while a graph recipe is attached.

The one residual use is `IsKeyPressed(eTBC_CursorUp/eTBC_CursorDown)`, read out of
`Input.PressedKeys` as a **held modifier** that flips Arrow Up/Down from camera scroll to 3D-cursor
height (`XComTacticalInput.uc:756,776` and four more states). Whichever key that resolves to — the
mapping is native, via `GetCommandStringForTacticalAction` — it is already inert for us: X2Access
claims the bare arrows in world mode, so the game's `ArrowUp`/`ArrowDown` never runs, and
`Shift+,`/`Shift+.` replaced that function. Claiming PageUp/PageDown therefore costs a blind player
nothing they could still use.

#### ⚠ Shift has a side effect in the active tactical state; Ctrl has a small one

`InputArbiter` deliberately never claims a bare modifier (`input_arbiter.cpp:111` — *"bare modifiers
are never claimed; claims use the mask"*), so while we swallow `Shift+X`, the **Shift key itself
still reaches the game**. In the active state (`state ActiveUnit_Moving`, which despite the name is
the normal soldier-control state — it spans `:1742` to end of file and contains the
`PostProcessCheckGameLogic` at `:2978` that the mouse gate targets):

- `Key_Left_Shift` — **fires `PrevUnit()` on RELEASE** (`XComTacticalInput.uc:2147`). So every
  Shift-modified chord we claim ends by cycling to the previous soldier. This already applies to the
  tile cursor's shipped `Shift+,` / `Shift+.` floor keys and should be checked live before any new
  Shift chord is added. (`Key_Left_Shift` is a harmless `return false` in `state InReplayPlayback`,
  `:587` — so the behaviour is state-dependent, which is exactly why it may not have been noticed.)
- `Key_Left_Control` — toggles `m_kPathingPawn.SetWaypointModifyMode(true/false)` on press/release
  (`.uc:2488`). Balanced and inert without a click, but it does touch the pathing pawn the mouse
  gate already manages.

⇒ **Prefer bare and Ctrl chords; treat Shift as suspect.**

#### RT's scanner keys, mapped onto XCOM

The authoritative list of what a key actually does in tactical is not the ini — it is which
`Key_*` the **active state** (`state ActiveUnit_Moving`, `XComTacticalInput.uc:1742`→EOF) overrides.
Everything else falls through to `XComInputBase`'s declarations, which are body-less stubs except
for four. Overridden in the active state:

> A, Backspace, C, D, E, End, F, F1-F10, G, Home, J, K, L, M, N, P, Q, R, S, Space, T, Tab, U, V,
> W, X, Y, Z, arrows, Enter, Escape, LeftControl, LeftShift.

Absent from that list, and therefore inert in tactical: **B, H, I, O\*, Delete, PageUp, PageDown,
F11, F12** and every punctuation key. (\*`Key_O` is one of the four non-stub bodies in
`XComInputBase.uc:1155` — it opens the Chosen Info screen when a Chosen is active, so O is taken.)

| RT key | RT meaning (`InputBindings.cs`) | XCOM tactical |
|---|---|---|
| `,` / `Shift+,` | cycle party (`Scanner.ReviewParty`, `:209`) | **free** |
| `.` / `Shift+.` | cycle enemies (`Scanner.ReviewEnemies`, `:213`) | **free** — already ours for tile-cursor floors |
| `/` | cursor → selection, aliased with `Home` (`Scanner.CursorToSelection`, `:245`) | **free** — but `Home` is taken (`Key_Home` = centre camera on active unit), so no alias |
| `V` / `Shift+V` | cycle the current room's exits (`Scanner.ExitNext`, `:237`) | **taken** — `Key_V` → `ActivateAbilityByHotKey(FXS_KEY_V)` (`:2628`), i.e. the **Interact** ability (`AddInteractAbility` sets `DefaultKeyBinding = FXS_KEY_V`). Same *idea* as RT's V, owned by the game |
| `M` / `Shift+M` | cycle objects of interest (`Scanner.ReviewObjects`, `:221`) | **taken** — `Key_M` → `ActivateCommandAbility(3)` (`:2263`). The five command slots are P=0, L=1, K=2, M=3, N=4 |
| `B` | free in-area; local-map pin cycle (`LocalMapReview.CycleMarkers`, `:442`) | **bound but inert in single-player** — `Key_B` is push-to-talk (`XComInputBase.uc:1117`), gated on `ProfileSettings.Data.m_bPushToTalk` |

RT's other scanner keys fare worse: `N` (neutrals), `O` (re-announce), `U` (summary), `X` (where am
I), `P` (party), `Y` (inspect selection) are **all taken** in XCOM tactical. What survives intact is
`,` `.` `/` `'` `;` `I` `H`, plus PageUp/PageDown and every Ctrl chord.

Two lessons from RT's own binding comments, both load-bearing here:

- **RT converged on the ring and retired its dedicated cycles.** The comments at `:225-231` record
  landmarks (V/B) and area effects (Z) being folded into the Ctrl+PageUp/Down category browse —
  *"V and B are therefore free"*, *"Z is therefore free"* — after which V was reused for room exits.
  Direct evidence for shipping the XCOM ring first and only promoting a category to its own key once
  it proves high-frequency.
- **`Shift` is RT's universal "reverse" modifier**, on every cycle key. That idiom does **not**
  port: `Key_Left_Shift` fires `PrevUnit()` on release in XCOM's active state (see the warning
  above). Reverse needs a different modifier here — `Ctrl` is the safe one.

### 4.5 Vacating the game's letter keys onto Ctrl+letter

> ⛔ **SUPERSEDED 2026-08-20, by the live test in §4.4. There is nothing to build here.**
> Leniency means every vanilla bare-key action is *already* reachable at `Ctrl+key`: the arbiter
> claims the bare key and swallows it, the Ctrl chord is not claimed so it passes through, and the
> engine matches the vanilla binding with Ctrl held. **No config snippet, no rebind, no merge to
> rely on.** `mods/X2AccessCompanion/Config/XComInput.ini` was written for this section and has
> been deleted unused. The subsections below are kept only for the source reading in them
> (`XComKeybindingData.GetKeyString`, the four input layers, the per-key cost table in *What to
> vacate*, and the keymap in *The resulting keymap*) — the **mechanism** they describe is wrong,
> because it rests on strict matching. Read them for the inventory, not the plan.
>
> One genuine bonus of getting here by claim rather than by config: the vanilla key keeps working
> unmodified wherever our claim stands down (menus, strategy layer, targeting raised), and nothing
> whatsoever outlives an uninstall.

Decision (2026-08-20): **the accessibility verbs outrank the vanilla bindings**, so the game's own
keys move to `Ctrl+letter` and X2Access takes the bare letters. This is the same call RT made — see
`RTAccess/src/RTAccess/Input/GameKeybinds.cs`, which vacates ten service-window openers plus
`ChangeWeaponSet` onto Ctrl+letter through the game's own rebind path.

#### The mechanism: one bool per binding

XCOM has four input layers, and only the first needs touching:

1. `PlayerInput.Bindings` — physical key → exec command (`V_Key_Press`). **← flip `Control` here**
2. `XComInputBase.InputEvent` — an FXS key id → `Key_V()`
3. `Key_V()` body — the hard-coded action
4. `TacticalBindableCommands` / `IsKeyPressed` — a *separate*, small rebindable surface

Setting `Control=True` on the existing `KeyBind` entry for `V` does everything at once: bare `V`
matches nothing (strict modifier matching, §4.4), `Ctrl+V` runs the same exec command, and — this is
the part that makes it honest — **every key hint updates itself**, because
`XComKeybindingData.GetKeyString` renders the modifier:

```unrealscript
if(kKey.Control && (kKey.Name != 'LeftControl' && kKey.Name != 'RightControl'))
    result $= m_strControlKeyPreprocessor;
...
result $= (result != "") ? " + " $ strDisplayName : strDisplayName;
```
(`XComKeybindingData.uc`, inside `GetKeyString`)

Every `%OPEN_DOOR`-style hint resolves through `GetKeyStringForAction` →
`GetBoundKeyForAction` → `GetKeyString`, so the tooltip that said "V" starts saying "Ctrl + V" on
its own. No string patching, no lying hints.

#### Deliver it as a mod config snippet, not a runtime poke

XCOM 2's modding framework merges a mod's `Config/*.ini` into the game's config. **Proven on this
machine, by this mod**: `mods/X2AccessCompanion/Config/XComEngine.ini` contains nothing but

```ini
[Engine.ScriptPackages]
+NonNativePackages=X2AccessCompanion
```

and that single line is what makes the companion package load at all. A workshop mod
(`623935276`) likewise ships `XComASGX_Settings_Defaults.ini` and the corresponding
`XComASGX_Settings.ini` appears in the user's generated config folder. The Highlander ships eight
config files, `XComGame.ini` and `XComUI.ini` among them, merging into existing game configs
exactly as we would.

And `XComInput.ini` is not an awkward target — it is the engine's own designated editable config:

```ini
[NonSignedConfigFilter]
+NonSigned=XComInput.ini
```
(`DefaultEngine.ini:5-6` — the only entry in that list)

So the whole rebind ships as `mods/X2AccessCompanion/Config/XComInput.ini`. Declarative, versioned
with the mod, gone when the mod is gone, and visible in the game's own Controls screen where a
player can put it back.

**This is strictly better than the in-memory poke, for a reason that only became clear once the
config route was on the table.** `UIKeybindingsPCScreen.ReadBindings()` reads live
`PlayerInput.Bindings` — so a runtime edit *shows up in the Controls screen*, and if the player
then presses Save and Exit there, our temporary edit is written into their profile permanently.
That is precisely the failure CLAUDE.md's no-rebind rule warns about, and an in-memory edit does
not avoid it; a declared config snippet does.

Three practical cautions:

1. **No local precedent for this exact file** — none of the seven installed workshop mods ships an
   `XComInput.ini`. The mechanism is general and `XComInput` is explicitly writable, but this
   specific merge is unverified here. Test before building on it.
2. **The generated user config may not pick it up immediately.** The user-side `XComInput.ini` is a
   flat, fully-expanded 591-line file; UE3 regenerates it only when the Default hierarchy changes,
   and XCOM 2 is notoriously reluctant to absorb new mod config entries without the user deleting
   their config folder. Since we also ship an ASI, the robust shape is: **config snippet as the
   mechanism, ASI verifies at startup that the bindings actually took, and logs (or applies the
   in-memory fallback) if they did not.**
3. ~~Prefer append-and-shadow over `-Bindings=`.~~ **Removal is not needed at all** — see below.

#### The config only ADDS. The arbiter is what vacates.

Worth stating plainly, because it dissolves the fiddliest part of this: we never have to remove or
shadow a vanilla binding. `InputArbiter` subclasses the game window's WNDPROC and swallows a
claimed chord before the engine sees it, so **bare `V` is already dead whenever the scanner claims
it** — no config change required. The snippet's only job is to give each vacated function a
`Ctrl+letter` home so it stays reachable while our layer owns the bare key.

So the file is pure `+Bindings=` additions. No `-Bindings=`, no exact-string matching against
inherited lines, no reverse-iteration assumptions, nothing to get wrong.

It is also *better* than removal would be: the claim stands down outside world mode (menus, the
strategy layer, while targeting is raised), and bare `V` keeps working normally there. A config
removal would have killed it everywhere.

The one thing this does not cover is §4.6's Shift edit, which genuinely needs the existing
`LeftShift` entry changed rather than supplemented — that stays a separate, later step with its own
verification.

#### This does not violate the no-rebind rule — it sidesteps both of its reasons

CLAUDE.md says *"We do NOT rebind the player's config: a rebind outlives an uninstall, and
re-exposing NextUnit from our own chord would leave the game's own hint text lying."* Both reasons
were about a different mechanism, and neither survives here:

- **Outlives an uninstall** — a mod's `Config/XComInput.ini` is merged only while the mod is
  installed, so removing the mod removes the rebind. (RT *had* to write the player's settings,
  because on Unity that write is the only thing that refreshes hints; XCOM gives us the hint refresh
  for free, so we get the benefit without the cost.)
- **Hint text lying** — `GetKeyString` renders modifiers, so hints self-correct.

One guard still worth porting from RT: **only shift a binding still on its expected bare key**, so a
player's own rebind is never clobbered (`GameKeybinds.Shift(..., ctrlTarget: true)`). With the
config route this becomes an ASI startup check rather than a mutation guard — read
`PlayerInput.Bindings`, confirm the entries look as expected, log loudly if not.

#### What to vacate, and what it costs

Established by reading which `Key_*` the active state overrides (§4.4) and what each body does:

**Tier 1 — take outright, no gameplay cost.** Camera-only or mouse-only, or already replaced:

| Key | Vanilla action | Why it's free |
|---|---|---|
| `T` / `G` | `ZoomCameraScroll(false/true)` | visual only |
| `Z` | `ZoomCameraOut/In` | visual only |
| `U` | `ClearAllWaypoints()` (`:2446`) | waypoints are added with Ctrl+RMB — mouse-only feature |
| `J` | `RemoveLastWaypoint()` (`:2455`) | same |
| `F` / `C` | `DPad_Up` / `DPad_Down` = 3D-cursor level up/down | already replaced by our floor keys; `C` is already claimed for recenter |

**Tier 2 — take, but re-home to `Ctrl+letter`.** Real gameplay a sighted player uses:

| Key | Vanilla action |
|---|---|
| `V` | `ActivateAbilityByHotKey(FXS_KEY_V)` — the **Interact** ability (`:2630`) |
| `Y` | `Y_Button` → `ActivateAbilityByHotKey(FXS_KEY_Y)` — **Overwatch** (`:3823`) |
| `R` | `ActivateAbilityByHotKey(FXS_KEY_R)` — **Reload** (`:2076`) |
| `X` | `X_Button` → ability hotkey (`:3787`) |
| `P` `L` `K` `M` `N` | `ActivateCommandAbility(0,1,2,3,4)` — the WOTC faction/command row (`:2235-2281`) |
| `O` | Chosen Info screen (`XComInputBase.uc:1155`) |
| `B` | push-to-talk, gated on `m_bPushToTalk` (`XComInputBase.uc:1117`) |

**Do not touch.** `Q` / `E` (camera rotate — our arrows are camera-relative, so the player needs
it), `Tab` / `Shift+Tab` (next/prev soldier), `End` (end turn), `1`-`0` (ability slots),
`W` `A` `S` `D` and the arrows (camera scroll; arrows are already ours in world mode).

#### The resulting keymap — RT muscle memory, 1:1

With Tier 1 + Tier 2 vacated, every RT scanner key is available and can keep its RT meaning:

| Chord | Action | RT equivalent |
|---|---|---|
| `PageDown` / `PageUp` | next / previous item in category | identical |
| `Ctrl+PageDown` / `Ctrl+PageUp` | next / previous category (skipping empties) | identical |
| `,` / `.` | cycle squad / cycle enemies | identical |
| `M` | cycle objects of interest | identical |
| `N` | cycle civilians & neutrals | identical (neutrals) |
| `V` | cycle ways out — doors, evac, level changes | identical (room exits) |
| `B` | cycle objective & waypoint markers | RT's local-map pin cycle |
| `/` | plant the tile cursor on the selection | identical (RT also aliases `Home`; `Home` is taken here) |
| `O` | re-announce the selection | identical |
| `U` | battlefield summary | identical |
| `X` | where am I | identical |
| `P` | read the squad | identical (party) |
| `I` | act on the selection | identical (already free) |
| `'` | inspect the tile cursor's occupant | identical (already free) |
| `Y` | inspect the selection | identical |
| `;` | vantage read | identical (already free) |

**Direction lives in the key pair for the ring; `Shift+key` reverses the single-key cycles** —
RT's idiom, available once §4.6 moves prev-soldier onto an explicit Shift+Tab. Without that edit
`Shift` is unsafe (`Key_Left_Shift` fires `PrevUnit()` on release) and `Ctrl+letter` is now the
vacated vanilla home, which would leave only `Alt` — so §4.6 is what keeps the keymap RT-shaped
rather than merely RT-adjacent.

`Shift+,` / `Shift+.` stay the tile cursor's floor keys. `H` remains unclaimed in world mode (it is
the shot-breakdown key while targeting is raised).

---

### 4.6 Freeing `Shift` — move prev-soldier onto an explicit Shift+Tab

> ✅ **RESOLVED 2026-08-21 — and by neither option below. Both are superseded; Option B is
> actively DANGEROUS and must not be attempted.**
>
> What shipped: the arbiter now accepts a claim on a MODIFIER KEY, and `object_scanner.cpp`
> claims `{VK_LSHIFT, ModNone}` while the tactical layer owns input. The engine never sees
> LeftShift go down or up, so the bound command never runs and `Key_Left_Shift` is never reached.
> Our own chord matching is unaffected, because `GetKeyState` reflects the state the OS updated
> when the message came off the queue — not what the window procedure did with it afterwards.
> Side resolution comes from lParam's scan code (`MAPVK_VSC_TO_VK_EX`), since WM_KEYDOWN reports
> the generic `VK_SHIFT`. `RightShift` is left alone: it carries no tactical binding.
>
> ⛔ **Why Option B below is wrong.** The mouse-gate trick does NOT apply verbatim to
> `Key_Left_Shift`. That function takes an `int ActionMask` and returns a `bool`;
> `MouseGate::SwallowThunk` consumes a lone `EX_EndFunctionParms` byte and writes no result. Here
> it would meet the parameter EXPRESSION instead, decline to advance, leave the VM's `Code`
> pointer pointing into the middle of that expression for the caller to execute as its own
> bytecode, and hand `bConsume` (`XComInputBase.uc:610`) an unwritten return value. Making it
> correct means either popping the parameter through `FFrame::Step` (which needs `GNatives`) or
> swapping `UFunction::Script` for the replay state's identically-signatured `{ return false; }`
> body (which needs `UStruct::Script` — an offset this project has never measured; the generated
> SDK buries it inside `UStruct`'s `UnknownData01`). The swallow needs no engine offset at all.
>
> The one thing Option B got right and the swallow has to pay for: swallowing LeftShift costs
> `Input.PressedKeys` its entry, which is exactly what `Key_Tab`'s prev-soldier branch reads. So
> `Shift+Tab` is claimed too and re-provided by `X2AccessObjectScanner.PrevSoldier`, which
> reproduces `IsManualUnitSwitchAllowed`'s guard rule for rule (it is state-scoped and so
> unreachable by name, but both of its tests are plain script).
>
> Also settled, and it explains open question 3 below: the live test that "did not reproduce" ran
> in the TUTORIAL, which is a replay — and `InReplayPlayback`'s copy of `Key_Left_Shift`
> (`:587`) is a bare `return false`. The bug was real the whole time; the replay hid it.

> ⚠ **Rewritten 2026-08-20 after the leniency finding (§4.4). The premise below inverted.**
> Under lenient matching `Shift+Tab` DOES reach `Key_Tab`, so its
> `PressedKeys.Find('LeftShift')` branch is **live code, not dead** — prev-soldier on Shift+Tab
> already works natively today, and needs nothing built. What remains is only to stop
> `Key_Left_Shift` firing `PrevUnit()` on every bare Shift release.

The `PrevUnit()`-on-Shift-release problem (§4.4) is the single obstacle to using `Shift` as the
reverse-cycle modifier — and it is a **live bug in already-shipped behaviour**, because the tile
reader's `Shift+,` / `Shift+.` floor keys release Shift every time (§6.3).

#### What the source says

Three functions in the active state call `PrevUnit()`:

| `XComTacticalInput.uc` | Trigger |
|---|---|
| `:2142` `Bumper_Left` | gamepad LB |
| `:2152` `Key_Left_Shift` | **unconditional, on RELEASE** |
| `:2197` `Key_Tab` | on release, `if (PressedKeys.Find('LeftShift') != INDEX_NONE \|\| PressedKeys.Find('RightShift') != INDEX_NONE) PrevUnit(); else NextUnit();` |

So Shift+Tab → prev-soldier is *already written* inside `Key_Tab`. The `Tab` binding is
`(Name="Tab", Command="Tab_Key_Press | onrelease Tab_Key_Release")` with `Shift` omitted and no
`bIgnoreShift` (`DefaultInput.ini:344`) — and because matching is **lenient** (§4.4), that binding
matches with Shift held. **Shift+Tab therefore reaches `Key_Tab`, takes the `PressedKeys` branch,
and calls `PrevUnit()` today.** Nothing needs adding.

`LeftShift` is separately bound in `[XComGame.XComTacticalInput]`
(`Command="Left_Shift_Key_Press | onrelease Left_Shift_Key_Release", Shift=False`), so a *bare*
Shift tap ALSO calls `PrevUnit()` on release. Both routes are live; only the second is in our way.

`PressedKeys` is still worth confirming once — `Input.uc:39` calls it *"keys which this interaction
**handled** a pressed event for"*, which hints at binding-gated. `LeftShift` does have a binding, so
the `Key_Tab` branch should see it; that is the assumption to check, and it is now the only one
left.

#### Option A — the config edit (now the weaker option)

**Drop the release half** of the existing `LeftShift` entry: `Command` becomes
`"Left_Shift_Key_Press"`. `Key_Left_Shift` then only ever runs with a PRESS mask, and its body only
acts on RELEASE — so it becomes a no-op while the binding still exists and still matches, leaving
`IsShiftPressed()` and `PressedKeys` untouched whatever their semantics. Nothing needs adding for
Shift+Tab, which already works.

Two reasons this is no longer the recommendation. It is a **modifying** edit (`-Bindings=` by exact
string, then `+Bindings=`), not the purely additive one §4.5 assumed. And **it depends on a
mod-shipped `XComInput.ini` merging, which is still unproven** (§6.8) — where §4.5 used to give that
merge a second customer, it is now the only one, so the whole config route would exist for this
single edit.

#### Option B — the mouse-gate trick (recommended)

`Key_Left_Shift` is a *state* function (`XComTacticalInput.uc:2152`, inside `ActiveUnit_Moving`;
the other definition at `:587` belongs to `InReplayPlayback`), so the two are distinct `UFunction`s
and gating one does not touch the other. The **mouse-gate trick applies verbatim** — set
`FUNC_Native` and point `Func` at a thunk, restore on release (`src/mouse_gate.cpp`). Script→script
dispatch goes through exactly those two fields, so it works here too.

Cost: `GameUI::FindScriptFunction` currently resolves class-scope functions only
(`Mouse_CheckForPathing` is class-scope, `XComTacticalInput.uc:3204`), so this needs the lookup
extended to walk a `UState`'s own `Children`. That is the only new machinery, and it buys a fix that
mutates no config at all.

**Recommendation (revised 2026-08-20):** do Option B. It mutates no config, depends on no merge,
reuses a technique already proven in this codebase, and stands down exactly where our other gates
do. The only new machinery is teaching `GameUI::FindScriptFunction` to walk a `UState`'s `Children`
— worth having regardless, since every other `Key_*` we might ever need to neuter is state-scoped
too.

---

## 5. Judgement calls worth making before any code

1. **The Lost.** A Lost swarm is 10-20 units. Ringing through them individually is unusable and
   ringing past them buries the ADVENT pod behind them. Options: fold into `units.enemies` and
   announce as a group ("Lost swarm, 14, nearest 6 tiles north"), or keep the separate category and
   summarise on entry. Recommend the summary-on-entry form.
2. **Squadsight enemies.** The enemy strip shows them (`m_arrSSEnemies`), so they are parity-legal —
   but they are *not* visible to the selected soldier. They need a word ("via squadsight") or they
   will read as a bug the first time one is unshootable.
3. **Towers.** `DetectionRange` only matters while concealed. Listing them always is noise; listing
   them only while the squad is concealed is a mode-dependent category, which the ring's
   empty-skip handles gracefully. Recommend gating on concealment.
4. **How much detail on a unit.** The tactical HUD layer already reads HP/effects on the focused
   soldier. The scanner should say *less* — enough to identify and locate — and let the HUD layer
   be the deep read, or the two surfaces will diverge the way RT's three did.

---

## 6. Open questions — need the running game

The dev harness answers all of these (`just arm`, then `/props`, `/gui`, `/exec`). Shared-harness
rule applies: don't drive a session someone else is driving.

1. ✅ **Is `XComInteractiveLevelActor.FriendlyDisplayName` actually populated?** ANSWERED
   2026-08-20, and it is the second branch of the question: **often EMPTY**. `Tutorial_01`'s own
   objective read blank, so the scanner spoke "Objective: object". The words live on the ability
   instead — `XComInteractiveLevelActor.InteractionAbilityTemplateName` (`:216`, e.g.
   `Interact_OpenDoor`) resolves to a localized `LocFriendlyName`, which names the thing by what
   you DO with it. `X2AccessObjectScanner.InteractiveLabel` now cascades
   FriendlyDisplayName → interact ability → IsDoor → MustBeHacked → "object". Worth knowing that
   the targeting layer's `DescribeNonUnitTarget` uses only the last three, so its "Interactive
   object" has the same blind spot and the same fix available.
2. **`GetLootingName()` dereferences `GetVisualizer()` unguarded** — it will crash on an object
   whose visualizer has not been created. Our call site must guard; confirm how often that happens.
3. **Does a Shift release really cycle the soldier?** ⚠ **PARTLY ANSWERED 2026-08-20 — still open,
   and still the highest priority.** Confirmed from source: `XComTacticalInput.uc:2147`
   `Key_Left_Shift` calls `PrevUnit()` unconditionally on RELEASE in the active-unit state, the
   `LeftShift` binding really is present in `[XComGame.XComTacticalInput]`, and the arbiter never
   swallows a bare modifier (`input_arbiter.cpp:111`) — so the shipped `Shift+,` / `Shift+.` floor
   keys do release Shift into that path every time. **But it did not reproduce live**: `Shift+Z`,
   `Shift+.` and the bare Shift releases inside them all left the selected soldier as Peter Osei in
   `Tutorial_01`, with a second soldier plainly on the map. The tutorial is a replay and gates unit
   selection, which is almost certainly why. → **Retest in an ordinary mission** — that is the only
   place this can be settled. Fix is §4.6 Option B.
4. **Cost of the per-keypress enumeration.** Several `IterateByClassType` passes over the current
   history index, at key-repeat rate. `X2AccessTileReader.DescribeInteractives` already does one per
   arrow step and is fine, but that is one class, not eight. Measure before optimising.
5. **Do interactive objects appear in `GetAllVisibleObjectsForPlayer(..., IncludeNonUnits: true)`?**
   Their `cpptext` says `CanEverBeSeen() = TRUE`, so they should — but the FOW-tile gate is the
   better rule for statics anyway (§3.1), so this only matters if we want a second opinion.
6. **Scan origin during the enemy turn**, when there is no active unit. Fall back to the last active
   soldier's tile, or refuse.
7. **`XComGameState_WorldNarrativeActor`** — does a sighted player see anything there? Determines
   whether it is a category or a phantom.
8. **Does a mod-shipped `Config/XComInput.ini` actually merge?** ⚠ **STILL UNPROVEN — and the
   2026-08-20 attempt was an INVALID test.** `Ctrl+Z` did zoom the camera, but so did `Shift+Z`,
   and so did `Ctrl+E` on a key no config of ours touches: lenient matching (§4.4) means the vanilla
   bare-`Z` binding fires under any modifier, so the presence of a `Ctrl+Z` binding is
   indistinguishable from its absence. Two further facts from that session, both worth keeping:
   mod configs are merged **in memory only** — `+NonNativePackages=X2AccessCompanion` demonstrably
   loads the companion yet appears nowhere in the generated `XComEngine.ini` — so inspecting the
   user's generated config proves nothing either way; and the user's `XComInput.ini` was NOT
   regenerated by a launch that rewrote a dozen sibling configs.
   A **valid** test needs a key whose bare form does nothing, so leniency cannot fake a pass:
   `+Bindings=(Name="H", Command="Z_Key_Press | onrelease Z_Key_Release")` — bare `H` is unbound
   (§4.4), so if it zooms, the merge happened — plus
   `+Bindings=(Name="I", Command="Z_Key_Press | onrelease Z_Key_Release", Control=True)` to check
   that `Control=True` merges too and that bare `I` stays dead. **This now blocks only §4.6
   Option A**, which is no longer the recommended path, so it is optional.
9. **What is the reverse-cycle modifier?** Answered 2026-08-20: **`Ctrl` now, `Shift` once §4.6
   Option B lands.** `Ctrl` is safe today — `Key_Left_Control` only toggles the pathing pawn's
   waypoint mode on press and untoggles it on release, an effect scoped to the hold and
   self-reverting. `Shift` stays contaminated by `PrevUnit()`-on-release until §4.6 is fixed. `Alt`
   is untested; `Key_Left_Alt` has no override in `XComTacticalInput`.
10. **Two UE3 input facts that §4.6 rested on.** ✅ **The first is settled, and it went the other
    way.** Matching is **lenient**, not strict (§4.4, verified three ways live), so `Key_Tab`'s
    Shift branch is live code and Shift+Tab is already prev-soldier. The "Ctrl+PageUp is free"
    conclusion in §4.4 survives — but only because bare PageUp/PageDown are unbound, not because a
    modifier excludes a bare binding.
    Still open: *is `Input.PressedKeys` populated for a key with no matching binding?* `LeftShift`
    has one, so `Key_Tab`'s branch should see it. Confirm when §6.3 is retested.

### Adjacent, from the tutorial session — worth folding in

- ✅ **FIXED 2026-08-20 — `/props?instance=` could not reach a companion CDO.** `?cdo=<Class>`
  now selects a class default object explicitly, and `?instance=` falls back to one when no live
  instance exists (safe precisely because nothing else matched — the reason `FindInstances` skips
  CDOs is that one would otherwise shadow a real screen, and there is no screen to shadow here).
  `GameUI::FindDefaultObject` is the lookup. Original finding, kept for the reasoning: `X2AccessTileReader`,
  `X2AccessNarrator` and `X2AccessObjectScanner` all report *"no live instance"* while demonstrably
  loaded and working, so `FindAnyInstance` is not matching `Default__*` objects. Those CDOs are the
  single most useful thing to inspect in this subsystem — the whole scanner/tile-reader/narrator
  model lives on them — and there is currently NO way to read one from the harness. A `?cdo=<Class>`
  selector (or letting `instance=` fall back to the `Default__` name that `GameUI::FindCompanionCDO`
  already resolves) is a small dev-harness fix worth doing before slice 1 proper.
- **`/props` caps array dumps at `kMaxArrayElems = 8`** (`src/gui_dump.cpp:39`). That cap hid the
  one element that mattered during the tutorial investigation. Raising it (or making it a query
  parameter) is a one-line dev-harness fix and this recon's array-heavy reads —
  `StoredTileData`, `MovementTiles`, `PendingLoot.AvailableLoot` — will hit it too.
- **`UIWorldMessageMgr.m_arrMsgs` (+0x0450) retains every world message it is showing.** That is a
  readable source for world-anchored text near a marker, and it partially reopens the flyover tap
  CLAUDE.md had written off as unbuildable — worth knowing before designing marker detail lines.
- **`X2AccessTileReader.CursorMoveTo` says "Moving" on a move the tutorial vetoes**, because
  `ClickToPath` returns true before submission is even attempted. Pre-existing bug; it will read as
  a scanner bug the moment `/` + Backspace becomes the standard way to reach a marker.

---

## 7. Suggested first slice

Not a plan, a recommendation for where the line should fall:

**Slice 0 — DONE 2026-08-20, and it dissolved itself (§4.4, §4.5).** The premise was that the
letters had to be bought with a config merge. They do not: claiming the bare key vacates the vanilla
action onto `Ctrl+key` for free, proven live. `Config/XComInput.ini` was deleted unused. **Start at
slice 1.** The one input item still outstanding is §6.3, and it needs an ordinary mission, not a
tutorial.

**Slice 1a — BUILT AND VERIFIED LIVE 2026-08-20** (`Tutorial_01`, whole mission).** `markers` only, as the user needed it first to
keep playing: `X2AccessObjectScanner.uc` + `src/object_scanner.cpp`, keys `B` (next), `O`
(re-announce), `/` (plant the cursor on the selection). Tutorial marker, objective components and
evac zone on one ring, nearest first. `X2AccessTileReader` gained `OffsetWords`/`ActiveUnitState`
as static shared helpers and a `CursorGoToMarker` entry point; the scanner reads the tile reader's
defaults for the scan origin, and the tile reader reads the scanner's for the plant — cross-class
default READS, the one direction UnrealScript allows.
Live result: the full loop (`B` → `/` → `Backspace` ×2 → "Moving") carried a player through the
tutorial's move and shot beats repeatedly, markers sorted nearest-first, tutorial ability names
localized ("Движение", "Выстрел"), and the parity guard held — the shot marker named its target
and said nothing about the outcome, which then missed. Three defects it exposed, all now fixed:
`"2of 2"` (a `$` where a `@` was meant), movement reachability spoken about a marker that points
at something to SHOOT ("out of range" about a perfectly shootable enemy — reachability is now a
per-marker `bIsDestination` question), and the naming gap in item 1 above.

**Slice 1 — CODE COMPLETE 2026-08-20, five categories plus the ring; live-verified through
`units.enemies` only.** The category dimension landed first (`ScanCategory(Category, Delta)`,
per-category selection memory in a static array on the CDO, `EmptyText` per category so
"No enemies visible" carries the fog rule in its own wording) together with `units.enemies` on `.`
— gated by `CanXComSquadSeeTarget`, never fog-latched, and described in the shot readout's own
words by sharing `DescribeTargetCover`/`DescribeTargetHealth`. `units.squad`, `ground.loot` and
`ground.reinforcements` followed, and with five real categories the §3.2 gate for building the
**ring** was met, so it was built rather than spending the last two free punctuation keys.
Remaining in this slice: promoting `ground.evac` out of `markers` into a category of its own with
size and how many of the squad are inside.

*The three added categories, and where each one's parity gate comes from:*

| Category | Source | Gate — the game's OWN rule for drawing the mark | Detail |
|---|---|---|---|
| `units.squad` | `XComGameState_Unit`, `eTeam_XCom`, not dead, not `bRemovedFromPlay` | none (they are yours) | "selected", one status word, cover, health/shield/armour |
| `ground.loot` | `XComGameState_LootDrop` | `LootExpirationTurnsRemaining > 0 && HasAvailableLoot()` — verbatim from `BuildVisualizationForLootTicked` (`.uc:333`) | "psi loot" (its own marker mesh on screen), "expires in N turns" |
| `ground.reinforcements` | `XComGameState_AIReinforcementSpawner` | `Countdown > 0` (what `UITacticalHUD_Countdown.RefreshCounter` hides on) **and** a visualization type that actually builds the flare | "in N turns" |

Three findings behind those rows, each from the shipped source:

1. **The reinforcement flare is persistent and its absence is enumerable.**
   `BuildVisualizationForSpawnerCreation` runs `X2Action_RevealArea` with `bDestroyViewer = false`
   and `X2Action_PlayEffect` with `bStopEffect = false` at `SpawnInfo.SpawnLocation`, so a sighted
   player has a glowing mark on the ground for the whole countdown — full parity cover for speaking
   the LOCATION, which the HUD counter alone does not give. But that whole call is skipped for
   `SpawnVisualizationType` in `{'', 'TheLostSwarm', 'ChosenSpecialNoReveal',
   'ChosenSpecialTopDownReveal'}`, so `HasSpawnFlare()` mirrors exactly that set. The Lost swarm is
   the one that matters: it announces itself with noise and a banner, not with a mark on the map.
2. **The countdown words are the game's own.** `UITacticalHUD_Countdown.m_strReinforcementsTitle` /
   `m_strReinforcementsBody` ("REINFORCEMENTS" / "INCOMING" in English) are read off the class
   defaults, so the ring and the HUD agree in whatever language the player runs. The turn count is
   always spoken as a NUMBER because a number is precisely what the sighted counter shows.
3. **Loot is labelled from `LastLootOwnerName`, not `GetLootingName()`.** The cached string was
   written from the source at drop time; `GetLootingName()` re-enters the source object, and for an
   interactive-object source that walks straight into the unguarded `GetVisualizer()` deref this
   class already sidesteps in `InteractiveLabel`.

**Slice 2 — CODE COMPLETE 2026-08-20, six more categories; NONE run live yet.** Eleven categories
now (`CAT_COUNT = 11`, `SelKeyByCategory[16]`), and **no native change was needed for any of them**
— that is the ring paying for itself.

| Category | Source | Gate | Notes |
|---|---|---|---|
| `ground.evac` | `XComGameState_EvacZone` | placed | Promoted OUT of `markers`, not listed in both. Spoken tile is the origin CLAMPED into the bounds, so distance is the walk that matters, the bearing points at the edge you enter by, and `/` plants somewhere you can stand. Detail: `W by H tiles`, `N of M inside` via the game's own `IsUnitInEvacZone`. |
| `units.civilians` | `IsCivilian()` | `CanXComSquadSeeTarget` | Absorbs the doc's `units.down`: a downed SOLDIER already reads "unconscious" in the squad ring, so only non-squad bodies were missing. "can be carried" when `CanBeCarried()` and unconscious/bleeding out. |
| `hazards` | `XComGameState_WorldEffectTileData` | tile at FOW `Seen` (strict — a remembered fire would be invented information) | ONE entry per TYPE, four maximum. |
| `objects.interact` | `XComGameState_InteractiveObject` | FOW `HaveSeen` (statics latch) | ONE ring, not six. |
| `units.lost` | `eTeam_TheLost` | `CanXComSquadSeeTarget` | ONE summary entry — §5.1's recommended form. |
| `objects.towers` | `DetectionRange > 0 && !bHasBeenHacked` | FOW `HaveSeen` **and** something of ours concealed | §5.3. |

Five decisions inside those rows worth not re-deriving:

1. **Hazards group by TYPE, not by state object.** Fire SPREADS, and each spread step writes another
   `XComGameState_WorldEffectTileData` — so a per-object ring would grow a new "Fire" entry every
   turn for what the player sees as one fire. Grouping caps it at four entries, ever.
2. **The hazard state object is an index; the WORLD is the truth.** A burned-out blob keeps its
   state object, so every stored tile is re-checked with `TileContainsFire/Smoke/Poison/Acid` —
   the same sparse array `X2AccessHelper.DescribeHazards` reads, so the ring and the tile cursor
   cannot disagree about whether a tile is on fire. `StoredTileData` is walked **by index**, not
   with `foreach`: its element type `WorldEffectTileDataContainer` is declared inside that class and
   referenced nowhere else in the entire SDK, so whether its name is even in scope elsewhere is
   untested — indexing to `.Tile` needs no local of that type.
3. **`objects.interact` is one ring because §6.1 got answered.** The six-way split predates the
   finding that `InteractiveLabel` names each object by its own interact ability ("Open Door",
   "Hack", "Use Elevator", already localized). With that, the split buys nothing but four more
   `Ctrl+PageDown` presses to reach the chest. Objects carrying an `ObjectiveInfo` are skipped —
   `markers` has them, and "one thing, one ring" is the same rule the evac promotion follows.
4. **The tower gate is "anyone concealed", not `bSquadIsConcealed`.** A lone Phantom ranger scouting
   ahead of a revealed squad is exactly when a detection radius matters most. `EmptyText` says
   *"Towers shown only while concealed"* rather than "No towers", because the latter is a claim
   about the MAP and would be false.
5. **Squadsight got its word, and a second one nobody had asked for.** The ring stays on SQUAD
   visibility, which is what the unit FLAGS show; but `UITacticalHUD_Enemies` builds the strip from
   `GetAllVisibleEnemyTargetsForUnit(activeUnit)` plus `GetAllSquadsightEnemiesForUnit` when the
   soldier `HasSquadsight()`. Comparing against both yields *"via squadsight"* and — the case
   §5.2 missed — *"spotted by the squad"*, for an enemy the squad sees and the soldier holding the
   gun does not. Without that clause the first unshootable target reads as a bug in us rather than
   as a rule of the game.

Deliberately still not built: `units.allies` (rare enough to sit inside civilians) and
`objects.destructibles` (noise).

*Keys, final for this slice.* Three dedicated cycle keys — the ones pressed every turn — and the
ring for everything else. This is §3.2's own fallback ("dedicated cycle keys for the two or three
that get used constantly, with the ring for the rest") and it is what keeps `;` and `'` free for
the vantage read and the inspect verb they are earmarked for in §4.5.

| Chord | Action |
|---|---|
| `B` | next marker |
| `.` | next enemy |
| `,` | next squad member |
| `PageDown` / `PageUp` | next / previous **in the current category** |
| `Ctrl+PageDown` / `Ctrl+PageUp` | next / previous **non-empty category**, announcing its name |
| `O` | re-announce, wherever the ring cursor is |
| `/` | plant the tile cursor on the last thing announced |

The ring cursor (`CurCategory`) lives on the **CDO, not in native**, unlike the per-keyboard state
it replaced: stepping it means knowing which categories are EMPTY, and only the model can answer
that. Native's `gLastCategory` is gone — one source of truth. Empty categories are skipped, which
is the whole point: eight slots exist and a typical mission populates three, so a ring that stopped
on each empty one would spend most of its presses saying "no loot".

VERIFIED LIVE 2026-08-20 (same tutorial run, deployed by hand): all three cover states spoke
(`Солдат "Адвента", flanked, 3 of 3 health, 12 tiles, 2 south, 12 west, 4 of 4`), wrapping worked
(`4 of 4` -> `1 of 4`), `No enemies visible` fired during concealment instead of going silent, no
bogus reachability on an enemy, and the ring re-sorted dynamically as the squad moved (an
objective at 4 tiles took `1 of 2` ahead of a tutorial marker at 5). Per-category memory confirmed
by reading the CDO itself: `SelKeyByCategory[0] = -101` (KEY_TUTORIAL) alongside
`SelKeyByCategory[1] = 603` (an enemy ObjectID). Zero warnings and no failed outbound calls.

⚠ **Known ceiling, not a defect: every alien shares one label.** `GetName(eNameType_Full)` returns
the CHARACTER TEMPLATE for a non-soldier, so six troopers all read `Солдат "Адвента"` and are
distinguishable only by distance, bearing and ring index. Do NOT "fix" this by inventing
"Trooper A/B/C" — a sighted player has no per-enemy identifier either, and §5's parity rule says
the scanner must never hand out information the screen does not. The ring index plus per-category
selection memory is what lets a player return to a specific one.

**Slice 1 (original scope)** — the ring itself with five categories that need no judgement calls and cover the most
lost ground: `markers`, `ground.loot`, `ground.evac`, `ground.reinforcements`, `units.enemies`.
Plus `/` to plant the cursor. That already gives a blind player the four pieces of information the
HUD shows a sighted player and currently gives us nothing about: where the objective is, that loot
is about to expire, where to evac, and that reinforcements are landing *there* in two turns.

Note that `markers` in slice 1 means the objective/glint/highlight members only. The tutorial
contributor (§3.3) is slice 1b — it is a small addition once the group exists, but it needs the
`CurrentHistoryFrame` gate and the `ResultContext` parity guard, and it must be coordinated with
the tutorial plan's push + `T` key so the two surfaces do not double-speak.

**Slice 2** — the remaining object categories, once §6.1/§6.2 are answered and naming is known-good.
Dedicated cycle keys (`M` objects, `N` civilians, `V` ways out, `B` markers) land here, since only
now is there evidence about which categories are used often enough to earn one.

**Slice 3** — `vantage`, `hazards` as blobs, and the battlefield summary.
