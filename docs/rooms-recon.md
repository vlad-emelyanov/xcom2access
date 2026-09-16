# Rooms and exits — recon

Scoped 2026-09-16 from the WOTC SDK's `Development/SrcOrig/XComGame/Classes/` and the installed
Community Highlander beta, then **measured live the same day** on a running mission through
`X2AccessTileProbe` — §8 says which claims are which, and two of the outstanding items are still
source-derived. Ghidra was tried and abandoned: the binary carries no symbols for the world-data
builder, and every question it could have answered was answered faster by one `GetTileData` read on
a running mission. That was the right call twice over — the live reads also corrected a conclusion
the source alone had led to (§2.3).

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

✅ **`COVER_ClimbOnto_*` and `COVER_ClimbOver_*` are LIVE** — measured 2026-09-16 (§8.1). They are
declared and used nowhere in shipped script, so this was the one thing the whole design rested on
and it could have gone the other way. Six tiles, checked against what the object scanner's cover
ring says independently: **every LOW ("half cover") edge carries a climb bit; no HIGH ("full cover")
edge carries one.**

| tile | cover ring says | `CoverFlags` |
|---|---|---|
| (11,7,1) | Half cover south + west | `S:low+onto  W:low+over` |
| (13,10,1) | Half cover north | `N:low+onto` |
| (12,6,1) | Half cover east | `E:low+onto` |
| (10,10,1) | Full cover north + east | `N:high E:high` |
| (14,10,1) | Full cover north | `N:high` |
| (10,5,1) | Full cover west | `W:high` |

✅ **`TileData.CoverFlags` and `XComCoverPoint.Flags` are the SAME WORD** (§8.2) — bit-identical on
every tile tried (`0x010482AA`, `0x00004105`), and the engine's own `GetTileDebugString_StaticFlags`
reports the same value again. So the cover ring, the tile cursor and any room map share ONE oracle
and cannot drift apart; the "make our own surfaces agree" rule is satisfied by construction rather
than by discipline.

### 2.3 The direct edge query — `IsAdjacentTileBlocked`

`native function bool IsAdjacentTileBlocked(TTile SourceTile, const out TTile DestinationTile) const`
(`:792`). One shipped caller, `X2Condition_Wrath.DefaultMeleeVisibility`, which passes two tiles
adjacent in X/Y at differing Z — so it is the general query rather than a cardinal-only one.

⛔ **It is DIRECTED, not an edge predicate: it answers "can a unit move A→B", and it folds in B's
OWN occupancy.** Measured 2026-09-16 (§8.3) by asking every edge both ways round. An obstacle tile
answers `0` outward on all four sides and `1` inward on all four, because nothing stops you leaving
it and everything stops you entering it. That is correct behaviour and NOT the `HasFenceWithNode`
trap it first looked like — the first read of this compared a floor tile against an obstacle tile
and mistook a directed query for a one-sided one.

### 2.3.1 Obstacle TILES and barrier EDGES are different things, and only one of them is a wall

The distinction the measurements turned on, and the one a segmenter has to get right:

- **A barrier on an EDGE** (a wall, a fence, a sandbag) is recorded SYMMETRICALLY. Both tiles carry
  the mirrored face bits and both receive cover from it. The wall between (10,10,1) and (10,11,1):
  ```
  (10,10,1)  [N:high]  path:x--Yz-  vis:---Y--
  (10,11,1)  [S:high]  path:--y-z-  vis:--y---
  ```
  and `IsAdjacentTileBlocked` reads `1/1` both ways. **This is what a room boundary looks like.**
- **An obstacle TILE** (a crate, a car, a pillar) carries `Occupied` + `PathBlocking` and `CoverFlags
  = 0` — no cover of its own, because cover is a property of standing somewhere and nobody stands
  there. Its neighbours record `BlocksPathing<face>` toward it and it does not mirror them, because
  the block is not on the edge, it IS the tile. `IsAdjacentTileBlocked` is asymmetric across every
  one of its edges.

✅ **So the asymmetry is a TELL, not a problem**: an edge where the forward and reverse answers
disagree is an edge into an obstacle tile. Segment over standable floor only and obstacle tiles drop
out as holes — which is what §3 predicted from first principles before any of this was measured —
and between two standable tiles the query is symmetric and safe to use as an edge predicate.

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

**(a) Climb-gated — `blocked && has cover && !(ClimbOver | ClimbOnto)` — PREFERRED, and the bits it
rests on are now measured live (§2.2).** The engine's own answer to "is this a barrier or just
something to hide behind": if the game says you can vault it or climb onto it, it is not a wall.
Directly the RT law-1 shape — ask the engine's bit, never infer. Note it is BOTH climb bits, not
just `ClimbOver`: the sample had three `+onto` edges to one `+over`, and both mean "passable to a
soldier", which is what a boundary is not.

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

`V` — **the player's decision, 2026-09-16, and CLAIMED the same day** — cycles the exits, matching
RT's `V`. `Shift+V` is the previous one.

⚠ **This paragraph originally called `V` "the first time the mod DISPLACES a vanilla binding rather
than taking a free key". That was wrong when written, and the correction matters more than the
claim did.** The game binds `V` to the Interact ability (`AddInteractAbility` sets
`DefaultKeyBinding = FXS_KEY_V`; `XComTacticalController:2628` routes `Key_V` →
`ActivateAbilityByHotKey(FXS_KEY_V)`) — but that is a generic `X_Key_Press` forwarder, which is
exactly what `B`, `O` and **`J`** already displace, `J` being an ability hotkey slot itself and
taken by the cover ring before this. So `V` sets no precedent; it follows one, and the ⚠ below
about "the claim is reversible, the habit is not" was worrying about a habit the mod already had.
`docs/input-map.md` is the authority and now says so. Two things that make it affordable:

- UE3 matches modifiers LENIENTLY, so a bare claim on `V` leaves the vanilla Interact reachable at
  `Ctrl+V` with no config and no rebind — the same trade already taken for other bare letters.
- Interact is also reachable from the ability bar, which the tactical recipe already reads.

`Shift+V` for the previous exit, per the scanner's own convention. The room readout itself ("what
room am I in") should ride the tile cursor's existing re-announce rather than take a second key.

## 7. Where it lives

**SETTLED 2026-09-16, and not where this section first said.** The exits half is a THIRTEENTH
scanner category, `world.exits` at `CAT_EXITS = 12`, inside `X2AccessObjectScanner` — the fifth
mirrored `CAT_*` index and therefore a two-file change (`object_scanner.cpp` + the enum), unlike
`CAT_UNEXPLORED`.

⛔ **A new class beside the scanner would have been the wrong call and the tell was the `/` key.**
The plant verb reads `class'X2AccessObjectScanner'.default.Sel*`, so a selection living anywhere
else could not be planted on, walked to, or re-announced without re-plumbing all three. Inside the
scanner, every piece of that plumbing is free and already proven: per-category remembered
selection, `IndexOfKey` identity across rebuilds, `InsertByDistance`'s one-key-one-entry guard,
`FormatMarker`'s compass and `ReachWords`' reach clause, and the ring's empty-skip. What this
section was actually reasoning about was the ROOM MAP's lifecycle, which is a different object and
still unbuilt.

The ROOM half, if §8.6 ever lets it ship, keeps this section's original answer: the rebuild cost is
the open question — a room map is `NumX × NumY` per level, the same order as `AddFrontier`'s FOW
sweep, and unlike RT's there is no ticked model to amortise it. If per-keypress proves too slow,
the cache key is the tile-data version, NOT a timer.

## 8. Live measurements

Run 2026-09-16 on a live mission through `X2AccessTileProbe` — recon instrumentation, not a feature
(the class says so at the top). ⛔ It exists because **nothing else can ask these questions**: every
tile-data native takes a `TTile`, and `/call` REFUSES struct arguments, so each probe takes three
plain ints and writes one flat line to `NativeResult`.

⛔ **Every probe line stays under ~150 characters.** `/props` prints a string through
`gui_dump.cpp`'s `kValueChars = 160` cap and marks the cut with an ellipsis. The first version of
`ProbeTile` put the engine cross-check and the adjacency test at the END of the line and neither was
ever seen — one question per call, and where a line must carry both an oracle and our own decode,
**the oracle goes first**, because the half that gets cut must be the half we could recompute.

| # | question | verdict |
|---|---|---|
| 8.1 | Are `COVER_ClimbOver_*` / `ClimbOnto_*` populated? | ✅ **YES** — 6/6 against the cover ring (§2.2). Predicate (a) survives. |
| 8.2 | Do `TileData.CoverFlags` and `XComCoverPoint.Flags` agree? | ✅ **Bit-identical**, and the engine's debug string reports the same word (§2.2). |
| 8.3 | Is `IsAdjacentTileBlocked` a symmetric edge predicate? | ⚠️ **Directed, not symmetric** — and that is correct, not a trap (§2.3, §2.3.1). |
| 8.4 | What is in `TraversalStarts`? | ✅ **Live**: 13–16 neighbour tiles per tile, at X/Y offsets −1..+1, with duplicates where one pair carries two traversal types. |
| 8.5 | Does our `TileStaticFlags` transcription match the engine's? | ✅ on every bit visible before the cap — cover word exact on 4 tiles, pathing faces agree. ⚠ PARTIAL, see below. |
| 8.6 | Render the predicates over whole maps | ❌ **Outstanding.** |
| 8.7 | Does a room map agree with `DescribeIndoor`? | ❌ **Outstanding.** |

**On 8.4.** `TraversalStarts` is an adjacency LIST, and a useful one: an open floor tile carried 16
entries (8 neighbours × 2 traversal types), while (10,10,1) with two full-cover edges carried 13 and
(10,11,1) behind the wall carried 11. So it encodes connectivity directly, with no bit
transcription — but it does NOT say which traversal TYPE, so it can tell you two tiles are joined
and not whether they are joined by *vaulting*. The cover/climb bits remain the wall-vs-cover call.

**On 8.5, and it is a real limit.** `GetTileDebugString_StaticFlags` prints one
`TileDataBlocksPathingXNeg : <yes/no>` line per flag, so it blows the 160-char cap after about two
flags, and **it is LOCALIZED** — this machine returned `Да`/`Нет`. Any future parse of it is
language-dependent and must not assume English. What was checked agrees; the tail of the
transcription is still unverified, and the constants still have no `static_assert` net.

⛔ **`ProbeRow` had an axis bug and the fix is the lesson, not the bug.** It read the cover of the
FARTHER tile's west face, which is the edge BEYOND that tile — so a sandbag's `LO` landed one edge
early, measured against (11,7,1) whose `W:low+over` faces (12,7,1). West is **+X** in this project's
compass (`X2AccessTileReader`'s `NORTH_DX`/`EAST_DX` table), so the nearer tile's west face is the
shared edge. The blocked character was right throughout, because `IsAdjacentTileBlocked` was asked
about the pair directly. Fixed, **not re-run** — so no `ProbeRow` output in this document is
trustworthy and §8.6 starts from scratch.

### What 8.6 still needs

⛔ **"Judge by eye" was the wrong instrument and has been replaced.** This project's only reader
cannot compare a screenshot of an isometric camera against a 41-wide grid of characters with any
confidence, and a predicate that renders *plausibly* is exactly the failure this section exists to
catch. So the judgement moved onto NUMBERS a wrong predicate cannot survive — the same move the
cover-distribution rule makes in `CLAUDE.md`, where counting found what listening never would. The
instrument is `X2AccessTileProbe.ProbeRooms` / `ProbeRoomRow` (2026-09-16), and it needs no map
load, no console trick and no special plot: **any live mission is a sample.**

`ProbeRooms(X, Y, Z, Predicate, Radius)` runs the whole flood and answers in one line:

```
p=a-climb r=20 w=41 cells=1201 rooms=23 max=380 med=12 tiny=9 edges w/o=418/766
```

Run it three times, once per predicate (`0` = a-climb, `1` = b-sight, `2` = c-height), and the
shape of the failure is in the numbers:

- **Over-segmentation** — high `rooms`, high `tiny`, low `med`. The sandbag-cuts-the-street failure
  §3 predicts for a naive "blocked" predicate.
- **Under-segmentation** — `max` near `cells`, `rooms` in single figures. One room swallowing the
  building and the street outside it.
- **`edges w/o`** is the wall/open census over standable pairs, counted once per edge, so it says
  directly what fraction of the passable geometry each predicate calls wall.

`ProbeRoomRow(X, Y, Z, Predicate, Radius, Row)` keeps the SHAPE half — one character per cell,
`a`..`z` cycling per room id, `#` where the ground is not standable, row 0 at the north edge. The
numbers cannot show a room leaking down a corridor into the street; this can.

Two deliberate departures from what ships, both noted in the class:

- **4-WAY flood, never 8-way** — the opposite of `AddFrontier`, and for a reason: a boundary lives
  on a cardinal EDGE, so a diagonal step has no edge to test and would leak a room through the
  corner of a wall. A fog seam has no edges, which is why the frontier fill can afford 8.
- **Fog is IGNORED.** This measures geometry; §5.1's rule that a room may never be built through
  never-seen tiles belongs to the shipping segmenter, and applying it here would confound the
  comparison with wherever the squad happened to have walked.

⚠ All three predicates share the BLOCKED half (`IsAdjacentTileBlocked`, §8.3's measured oracle) and
differ only in what makes a blocking edge a wall. (b) is therefore "blocked AND blocks visibility"
rather than §3's literal `BlocksPathing && BlocksVisibility` — one variable between the three
readings is what makes the counts comparable, and `ProbeRow` prints both halves if they ever
disagree.

Still three maps minimum (§3): a dense urban plot, an interior-heavy plot, an open wilderness plot.

The `ProbeRow` cross-section legend, kept here because the output has no room for it:

```
1st  B blocked (IsAdjacentTileBlocked forward)   . open   ! forward/reverse disagree → obstacle tile
2nd  H high cover across the edge   L low   - none
3rd  O climb-over   N climb-onto   - neither
4th  V blocks visibility across the same face    - sees through
```

So `B--V` is a wall, `BLO-` / `BLN-` is a vaultable obstacle, `..--` is open floor, and a run of
`B--V` is what a room boundary is made of. A `!` pair marks an obstacle tile between them. The
fourth character is what makes predicate (b) readable off a cross-section at all.

## 9. What could sink this

- ~~**ClimbOver dead**~~ — retired 2026-09-16, §8.1 answered it. The fallback below stands on its
  own merits regardless: **ship EXITS without ROOMS** if the partition renders badly at §8.6. An
  exit ring keyed off doors, windows, ladders and ramps near the scan origin needs no partition at
  all, and it is where most of the value sits anyway.
  - ✅ **TAKEN 2026-09-16, before §8.6 rather than after it** — not because the partition failed
    but because it did not have to succeed first. `world.exits` ships on `V`; §8.6 now gates only
    the "leads to \<room\>" clause and the room readout, not the feature.
  - ⚠ **Two of the four kinds in §4's table did NOT survive contact.** Windows and drop-downs are
    unlistable: script cannot ask an arbitrary edge what traversal it would take
    (`MaybeModifyTraversalForWindow` is cpptext, `XComWorldData.uc:510`) and a destructible carries
    no localized name, so a window, a fence and a parked car are one indistinguishable thing with
    no word between them. §4's table was source-derived and this is the half of it that was wrong.
    What shipped is doors, ramps and ladders.
  - ⚠ **The overlap §9 never priced**: ramps and ladders were ALREADY `world.connectors` and
    hackable doors already `objects.interact`, so two thirds of the fallback's content existed
    before it did. That is answered by sharing the contributors rather than by dropping them
    (`surfaces/tactical.md`), but it means the exits ring's genuinely NEW content is the doors
    `objects.interact` misses — the open ones, the ones with no live interaction — plus the
    framing and the sort. Worth knowing before judging the live run.
  - ❌ **And the overlap is what bit, on the very first live run** (2026-09-16, prison map, driven
    through `/call`). From the squad's position the ring read **8 of 8 ladders** — `world.exits`
    was `world.connectors` under another name, because the nearest door was 43 tiles off and the
    threshold sweep reaches 12. The model is right (planted beside that door it named it, in the
    player's own language, off the edge); the SCOPE is what has no answer without a room.
    **So this section's fallback works and is thinner than it promised**, and the run is the first
    evidence FOR the partition rather than against it: a door 43 tiles away is not "the way out of
    this room", and only a partition knows the difference. Cost is not the obstacle — the sweep
    measured indistinguishable from the cheapest category in the scanner (`next-steps.md`).
- **Cost.** A per-level partition per keypress on an 80×80×12 map, on top of the frontier sweep.
- **Procedural parcels.** XCOM 2 assembles maps from parcels at runtime; a "room" that straddles a
  parcel seam may be shaped in ways a hand-authored level never would be. Unknown until §8.6.
- ⚠ **The V claim is reversible, the habit is not.** Displacing a vanilla binding once makes it
  easier to argue for the next one. If exits ship without rooms, re-ask whether they deserve `V` or
  belong on the ring with no key at all.
