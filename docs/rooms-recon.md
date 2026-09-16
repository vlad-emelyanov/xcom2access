# Rooms and exits — recon

Measured 2026-09-16 from the WOTC SDK's `Development/SrcOrig/XComGame/Classes/` and the installed
Community Highlander beta, with the game DOWN — so every claim here is source-derived and the
live-verification debt it creates is §8, filed in `next-steps.md` item 13. Ghidra was tried and
abandoned: the binary carries no symbols for the world-data builder, and every question it could
have answered is answered faster by one `GetTileData` read on a running mission.

The question, asked by the player after a Chosen Stronghold where the objective was unreachable and
every scanner ring went silent: *can we port the WH40KRTAccess room model — `RoomMap`, the `V` exit
cycle — so a blind player can hear "this room has three ways out" instead of feeling along a wall
one tile at a time?*

Short answer: yes, and the expensive half of RT's implementation does not need porting, because
XCOM 2 gives us the thing RT had to infer. What replaces it is a single unmeasured question, §3.

## 1. What RT's RoomMap is, and which of its two laws transfer

RT's model is documented in its own memories (`rt-room-classifier`, `rt-exit-cycle-rework`,
`rt-unexplored-frontier`) and splits in two: `src/Access.Core/Rooms/RoomSegmenter.cs` (BCL-pure —
clearance transform, watershed, merges, boundary scan) and `RoomMap.cs` (the engine side).
It earned two hard laws.

**Law 1 — passability comes from the engine's own per-edge bit, never from geometry — TRANSFERS,
onto a better oracle.** RT reads `CustomGridNodeBase.HasConnectionInDirection`, a single gridFlags
bit. XCOM 2's equivalent is first-class and richer (§2.1). The corollary transfers with it: any two
of our subsystems that disagree about where walls are, one of them is not asking the engine.

**Law 2 — on a coarse raster, clearance DEPTH cannot decide where a room ends, so opening
NARROWNESS must — DOES NOT TRANSFER, because the problem does not exist here.** RT needed a
persistence watershed with `PersistCells`, `MergeWideOpenings`, `MaxOpeningCells`,
`MaxOpeningFraction` and furniture-island bridging **only because its engine had no wall map at
doorway granularity** — walls were geometry, and rooms had to be recovered from free-space shape.
XCOM 2 bakes walls into explicit per-edge bits. A room is then a connected component of floor tiles
under "no boundary between them": a plain flood fill, of the kind `AddCover` and `AddFrontier`
already run in `X2AccessObjectScanner`. **The whole of `RoomSegmenter.cs` is unnecessary here.**

⚠ Do not read that as "this is easy". The work RT spent on the watershed, XCOM 2 spends on §3:
deciding which blocked edges are *walls* and which are merely *cover*.

## 2. The inventory — what XCOM 2 actually offers

### 2.1 Per-edge pathing and visibility bits — `TileData.StaticFlags`

`XComWorldData.uc:405`, in `cpptext`, declares `TileStaticFlags`. The relevant bits:

| bit | value | meaning |
|---|---|---|
| `TileDataBlocksPathingXNeg/XPos/YNeg/YPos/ZNeg/ZPos` | `1<<1` .. `1<<6` | movement blocked across that face |
| `TileDataBlocksVisibilityXNeg/…/ZPos` | `1<<7` .. `1<<12` | sight blocked across that face |
| `TileDataIsFloorTile` | `1<<13` | |
| `TileDataIsRampTile` | `1<<14` | ramps are Ramp AND Floor |
| `TileDataOccupied` / `TileDataOccupiedPartial` | `1<<15` / `1<<16` | |
| `TileDataPathBlocking` | `1<<17` | coarser than Occupied, no valid cover required |
| `TileDataCanEndMoveOnTile` | `1<<21` | |
| `TileDataIsBorderTileXPos/XNeg/YPos/YNeg` | `1<<22` .. `1<<25` | edge of the level volume |

Reachable from script as `GetTileData(Tile, Data)` → `Data.StaticFlags` (`XComWorldData.uc:884`).
**The companion reads none of this today** — the whole surface is new.

⛔ **These constants are C++-only and must be hand-transcribed into UnrealScript with no
`static_assert` net.** `GetTileDebugString_StaticFlags(Tile)` (`:823`) is the live cross-check and
the transcription is not to be trusted until it has been run against that string on real tiles.

⚠ `TileDataIsBorderTile*` is the fix waiting for the `world.unexplored` map-edge case
(`next-steps.md`, live-run debt) if that turns out to be real.

### 2.2 Per-edge cover and climb bits — `TileData.CoverFlags`

`XComWorldData.uc:21-45`. Per direction: `COVER_North/South/East/West` (`0x1`..`0x8`), the LOW
qualifier `COVER_NLow/SLow/ELow/WLow` (`0x10`..`0x80`), peek bits, `COVER_Diagonal` (`0x10000` —
the tile's cover is rotated 45°, local N = world NE), and then the two sets that matter most here:

- `COVER_ClimbOnto_N/S/E/W` — `0x20000`..`0x100000`
- `COVER_ClimbOver_N/S/E/W` — `0x200000`..`0x1000000`

The game's own reading of the first two groups is `X2AbilityToHitCalc_StandardAim.uc:908-945`: cover
exists in a direction if `CoverFlags & COVER_<dir>`, and it is HIGH if the matching `…Low` bit is
absent. That is the pattern to copy.

⛔ **`COVER_ClimbOnto_*` and `COVER_ClimbOver_*` are DECLARED AND USED NOWHERE in shipped script.**
Their layout is given but their *population* is unverified — they may be dead constants like
`XCom3DCursor.m_kHeightCylinder`. The entire ranking in §3 depends on them being live, so §8.1 is
the first measurement and the rest of this document is provisional until it comes back.

`XComCoverPoint.Flags` (`XGTacticalGameCoreNativeBase.uc:116`) is an `int` that appears to use the
same bit space — `X2AccessTileReader.CoverSignature` already masks its low byte. Whether
`GetCoverPoints`' flags and `TileData.CoverFlags` are identical is §8.2; if they are, the cover ring
and the room map share one oracle for free and the "make our own surfaces agree" rule is satisfied
by construction.

### 2.3 The direct edge query — `IsAdjacentTileBlocked`

`native function bool IsAdjacentTileBlocked(TTile SourceTile, const out TTile DestinationTile) const`
(`:792`). One shipped caller, `X2Condition_Wrath.DefaultMeleeVisibility`, which passes two tiles
adjacent in X/Y at differing Z — so it is the general "is there a barrier between these two
neighbours" query rather than a cardinal-only one. It is the convenient form of §2.1 and needs no
transcription; whether it agrees bit-for-bit with the `StaticFlags` faces is §8.3.

### 2.4 Traversals and the actor at the threshold

- `TileData.TraversalStarts` — `var init array<TTile>` (`:187`). Declared, referenced NOWHERE in
  shipped script. Semantics unknown: it may be the tiles a traversal leads to, or the tiles a
  traversal arrives from. §8.4.
- `GetPathObjectFromTraversalToNeighbor(StartTile, EndTile, bUnitIsLarge)` → `Actor` (`:806`).
  **This is the thing RT never had.** One shipped caller, `XComTacticalController.uc:1230`, which
  casts the result to `XComDestructibleActor` to find what a path step breaks. Given a threshold
  edge it hands back the actor sitting in it — so a doorway names its door and a window names
  itself, instead of RT's ±1.5 m proximity probe guessing which door belongs to which opening.
  The companion already calls this, in `X2AccessTileReader`'s noise clause.
- `ETraversalType` (`XGTacticalGameCoreNativeBase.uc:73`): `Normal`, `ClimbOver`, `ClimbOnto`,
  `ClimbLadder`, `DropDown`, `Grapple`, `Landing`, `BreakWindow`, `KickDoor`, `WallClimb`, `JumpUp`,
  `Ramp`, `BreakWall`, `Phasing`, plus flight and `Unreachable`. The exit vocabulary comes from here.
- `CollectLadderLandingTiles` (`:855`) is already used by `world.connectors`.

### 2.5 Buildings — the coarse room notion we already speak

`XComBuildingVolume` / `XComFloorVolume` / `XComMegaBuildingVolume` / `XComModularBuildingVolume`,
read by `X2AccessTileReader.DescribeIndoor` through the game's own rule
(`XComPawnIndoorOutdoorInfo.CheckForFloorVolumeEvents`): inside means "in a floor volume", a nested
`m_bIsInternalBuilding` wins, the floor is the highest `FloorNumber` touched. Rules in
`surfaces/tactical.md`.

⛔ **A segmenter must AGREE with this, not compete with it.** Two surfaces answering "where am I"
differently is the exact failure `X2AccessHelper` exists to prevent. The natural arrangement: the
BUILDING (and its floor) stays `DescribeIndoor`'s answer and remains the outer name; a ROOM is a
subdivision within it, and outdoors a room is whatever the segmenter finds bounded.

### 2.6 What does not exist

**XCOM 2 has no tactical room concept at all.** Every `*Room*` class in the SDK is the Avenger
(`XComGameState_HeadquartersRoom`, `UIRoom`, `UIFacility_RoomFunc`,
`SeqEvent_OnStrategyRoomEntered`) or a character-doll artifact (`StaticRoom`, `DollRoom*`). Same
starting position RT was in: rooms are mod-invented, and nothing in the game will ever name one for
us. This also means there is no localized vocabulary to borrow — a room's NAME can only ever be
descriptive ("room, 6 by 4"), never "the kitchen".

## 3. The segmentation predicate — the one real question

Every blocked edge is a candidate boundary, and the problem is that **XCOM 2's chest-high cover is
boundary geometry**: a sandbag line, a low wall, a railing all sit ON an edge and all block ordinary
movement (you cross them with `eTraversal_ClimbOver`). Flood-filling on "blocked" alone would cut an
open street into a dozen "rooms" at every sandbag — RT's over-segmentation failure arriving by a
different road.

Objects that sit IN a tile rather than on an edge — cars, crates, pillars — are not part of this
problem. They set `TileDataOccupied` on their own tiles, so a flood fill simply flows around them
and they become holes in a room, not walls across it.

Three candidate predicates, all computable from the SAME `GetTileData` call, so the recon can render
all three over one map and compare:

**(a) Climb-gated — `blocked && has cover && !ClimbOver` — provisionally preferred.**
The engine's own answer to "is this a barrier or just something to hide behind": if the game says you
can vault it, it is not a wall. Directly the RT law-1 shape — ask the engine's bit, never infer.
⛔ Rests entirely on the ClimbOver bits being populated (§2.2, §8.1). If they are dead, (a) dies.

**(b) Sight-gated — `BlocksPathing && BlocksVisibility`.**
"You cannot walk through it and you cannot see through it" is a fair definition of a wall and maps
onto what a sighted player perceives: a full wall hides the far side, a sandbag does not. Needs no
unverified bits — `StaticFlags` is used by the game constantly. Weaker where a chain fence or a
railing blocks movement without blocking sight, which (b) would call an opening and (a) would too,
so they may well agree in practice.

**(c) Cover-height-gated — `blocked && has cover && !Low`.**
The fallback if ClimbOver is dead: HIGH cover on an edge is wall-like, LOW is not. Uses only bits
the game itself reads (`X2AbilityToHitCalc_StandardAim`). Weakest of the three, because "high cover"
is a shooting fact and not a topology fact.

**They differ on exactly one real case: a low wall between two genuinely separate spaces.** (a) and
(c) merge them into one room; (b) splits them. Which is right for a blind player is a judgement
call that should be made while listening to both, not now.

⛔ **No predicate ships until it has been rendered over at least three maps of different character**
— a dense urban plot, an interior-heavy plot (the Chosen Stronghold that started this), and an open
wilderness plot. The failure mode of getting this wrong is not a wrong word; it is a room map that
quietly disagrees with the player's mental model everywhere, which is worse than no room map.

## 4. Exits

An exit is an edge, or a short run of edges, where you can leave the current room. Kinds, and where
each comes from:

| kind | source | spoken as |
|---|---|---|
| doorway (bare opening) | an unblocked edge between two rooms | "opening" |
| door | `GetPathObjectFromTraversalToNeighbor` on the edge, or an `XComGameState_InteractiveObject` with `IsDoor()` at the tile | the object's own `InteractiveLabel` |
| window | `eTraversal_BreakWindow` | "window" |
| ladder | `eTraversal_ClimbLadder`, `CollectLadderLandingTiles` | "ladder" |
| ramp / stairs | `IsRampTile`, `eTraversal_Ramp` | "ramp" |
| drop-down | `eTraversal_DropDown` | "drop" |

Two things fall out of the engine that RT had to hand-build:

- **The door/opening merge is free.** RT's `CycleExit` merges door items with bare `ProxyRoomExit`
  openings using a 2.5 m `ExitCoverSq` proximity test, and its own memory flags that as loose.
  `GetPathObjectFromTraversalToNeighbor` answers per EDGE, so a doorway with a door in it simply IS
  that door — no proximity guess, no double-listing.
- **A closed door is still an edge.** RT's old exit cycle missed doors entirely because a closed
  door cut its grid at build time and no `Exit` record was ever created. Here the tile persists and
  the door is an interactive object with a `TileLocation`, so the exit exists whether it is open
  or shut — and "closed" / "locked" is detail the scanner's `objects.interact` ring already words.

Contiguous exit edges merge into ONE exit, the way `AddCover` merges a sandbag run and `AddFrontier`
merges an opening. Its spoken tile is the member nearest the scan origin; its key is the lowest
linear tile index in the run (the `KEY_COVER` / `KEY_FRONTIER` scheme, next free base
`-600000000`).

Each exit says **where it leads**: the room on the far side, or — fog-gated, §5 — "leads to
unexplored".

## 5. Parity

⛔ **A room map is a floor plan, and a floor plan of unexplored ground is exactly the X-ray the
project forbids.** The rules, all inherited and all already proven here:

1. **Segment over `HaveSeen`/`Seen` tiles only.** Never-seen tiles are painted opaque
   (`FogColor` R=5,G=5,B=25 at A=255 — the proof is in `surfaces/tactical.md`), so a room built
   through them would be a room nobody can see. This is RT's own `rt-visual-parity` rule and it
   binds harder here than it did there.
2. **An exit into never-seen ground reads "leads to unexplored"**, never the far room's shape or
   size. Same rule RT settled for `ExitDestination`.
3. **A room's own extent is `HaveSeen` data and is therefore fair** — static layout is what the
   50%-grey tint shows.
4. ⚠ **The reach clause is the one thing safe to speak about fogged ground**, because
   `XComMovementGridComponent` carries `bTranslucentIgnoreFOW` (`X2AccessTileReader.VerticalDrawn`
   has the full list of what the fog exempts).
5. A room is an AID, not parity — the game names no rooms for anyone. It is admissible under the
   aid rule because every FACT in it (walls, doors, floors) is drawn; what the aid supplies is the
   summary, which is the RANGE/effort half the rule permits.

## 6. Keys

`V` — **the player's decision, 2026-09-16** — cycles the current room's exits, matching RT's `V`.

⛔ This is the first time the mod DISPLACES a vanilla binding rather than taking a free key. The
game binds `V` to the Interact ability (`AddInteractAbility` sets `DefaultKeyBinding = FXS_KEY_V`;
`XComTacticalController:2628` routes `Key_V` → `ActivateAbilityByHotKey(FXS_KEY_V)`). Read
`docs/input-map.md` before claiming it. Two things that make it affordable:

- UE3 matches modifiers LENIENTLY, so a bare claim on `V` leaves the vanilla Interact reachable at
  `Ctrl+V` with no config and no rebind — the same trade already taken for other bare letters.
- Interact is also reachable from the ability bar, which the tactical recipe already reads.

`Shift+V` for the previous exit, per the scanner's own convention. The room readout itself ("what
room am I in") should ride the tile cursor's existing re-announce rather than take a second key.

## 7. Where it lives

Companion-side, a new class beside `X2AccessObjectScanner` — the scanner's own model shape (rebuild
per keypress, NO REGISTRY, selection is an int key plus a tile) applies unchanged and there is no
reason to invent a second lifecycle. The rebuild cost is the open perf question: a room map is
`NumX × NumY` per level, the same order as `AddFrontier`'s FOW sweep, and unlike RT's there is no
ticked model to amortise it. If per-keypress proves too slow, the cache key is the tile-data
version, NOT a timer.

Exits are a natural twelfth-and-thirteenth scanner category (`world.exits`) reachable from the ring,
with `V` as its dedicated key — which makes it the fifth mirrored `CAT_*` index and therefore a
two-file change (`object_scanner.cpp` + the enum), unlike `CAT_UNEXPLORED`.

## 8. Live-measurement checklist — nothing above is settled without these

Needs a running mission with the dev harness armed. ⛔ Nothing here is readable through the existing
endpoints: no companion function exposes `GetTileData`, so **the first step is a throwaway probe
function on a companion CDO** (`X2AccessTileReader` already has `NativeResult` and the
`/call`+`/props` pattern), removed or left dev-only afterwards.

1. **Are `COVER_ClimbOver_*` populated?** Stand beside a sandbag line or low wall. Dump
   `CoverFlags` for the tile and its neighbour. Expect `COVER_<dir>` set, `COVER_<dir>Low` set, and
   `COVER_ClimbOver_<dir>` set. If the ClimbOver nibble is always zero across a dozen obstacles,
   predicate (a) is dead and §3 falls back to (b).
2. **Do `TileData.CoverFlags` and `XComCoverPoint.Flags` agree?** Same tile, both paths
   (`GetTileData` vs `GetCoverPointAtFloor`). If yes, one oracle serves the cover ring and the room
   map.
3. **Does `IsAdjacentTileBlocked` agree with the `StaticFlags` faces?** Sweep a row across a known
   wall and compare both answers per edge. A disagreement means one of them includes something the
   other does not (units, destructibles, large-unit clearance) and the choice between them matters.
4. **What is in `TraversalStarts`?** Dump it for a tile beside a door, a ladder, a window and a
   plain floor tile. If it is empty on all of them it is editor-time data and the exit design uses
   `GetPathObjectFromTraversalToNeighbor` alone.
5. **Transcription check.** Compare the hand-transcribed `TileStaticFlags` decode against
   `GetTileDebugString_StaticFlags` on twenty mixed tiles. This is the only net there is.
6. **Render all three predicates** over a raster of the whole level, one Z at a time, as ASCII.
   The 2026-09-16 raster scripts are a starting point but read the CURSOR, not tile data — a new
   sweep that dumps flags per tile is wanted. Three maps minimum (§3). Judge by eye against what the
   map actually is.
7. **Does a room map agree with `DescribeIndoor`?** Every tile the volumes call "inside building N,
   floor M" should land in a room, and no room should straddle two buildings.

## 9. What could sink this

- **ClimbOver dead AND sight-gating wrong.** If §8.1 fails and (b) renders badly on real maps, there
  is no third good predicate and the honest outcome is to ship EXITS without ROOMS: an exit ring
  keyed off doors, windows, ladders and ramps near the scan origin needs no partition at all, and it
  is where most of the value sits anyway.
- **Cost.** A per-level partition per keypress on an 80×80×12 map, on top of the frontier sweep.
- **Procedural parcels.** XCOM 2 assembles maps from parcels at runtime; a "room" that straddles a
  parcel seam may be shaped in ways a hand-authored level never would be. Unknown until §8.6.
- ⚠ **The V claim is reversible, the habit is not.** Displacing a vanilla binding once makes it
  easier to argue for the next one. If exits ship without rooms, re-ask whether they deserve `V` or
  belong on the ring with no key at all.
