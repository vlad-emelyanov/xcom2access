// X2AccessTileProbe
// ⛔ RECON INSTRUMENTATION, NOT A FEATURE. Nothing speaks through this class and nothing in the mod
// calls it; it exists so the dev harness can ask the questions in docs/rooms-recon.md §8, which
// `/call` cannot ask on its own — every tile-data native takes a TTile, and the endpoint REFUSES
// struct arguments (docs/dev-harness.md: "a zeroed object pointer is a null deref inside script in
// the very session it exists to observe"). So each function here takes three plain ints, builds the
// TTile itself, and writes a flat string to NativeResult the way X2AccessTileReader does.
//
// DELETE THIS CLASS once the rooms recon is answered, or promote the parts that earned their place.
// Until then it is dead weight in a shipping package and that is a deliberate, temporary trade.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessTileProbe extends Object;

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// XComWorldData.uc:405, cpptext `TileStaticFlags`. ⛔ HAND-TRANSCRIBED from C++ that script cannot
// see, so these constants have NO static_assert net and no compiler check of any kind — which is
// exactly what recon §8.5 exists to test: DecodeStatic below is compared against the engine's own
// GetTileDebugString_StaticFlags on the same tile, and a disagreement means this block is wrong.
const SF_BlocksPathingXNeg    = 0x00000002;
const SF_BlocksPathingXPos    = 0x00000004;
const SF_BlocksPathingYNeg    = 0x00000008;
const SF_BlocksPathingYPos    = 0x00000010;
const SF_BlocksPathingZNeg    = 0x00000020;
const SF_BlocksPathingZPos    = 0x00000040;
const SF_BlocksVisibilityXNeg = 0x00000080;
const SF_BlocksVisibilityXPos = 0x00000100;
const SF_BlocksVisibilityYNeg = 0x00000200;
const SF_BlocksVisibilityYPos = 0x00000400;
const SF_BlocksVisibilityZNeg = 0x00000800;
const SF_BlocksVisibilityZPos = 0x00001000;
const SF_IsFloorTile          = 0x00002000;
const SF_IsRampTile           = 0x00004000;
const SF_Occupied             = 0x00008000;
const SF_OccupiedPartial      = 0x00010000;
const SF_PathBlocking         = 0x00020000;
const SF_LargeUnitClearance   = 0x00080000;
const SF_IsLedgeTile          = 0x00100000;
const SF_CanEndMoveOnTile     = 0x00200000;
const SF_IsBorderTileXPos     = 0x00400000;
const SF_IsBorderTileXNeg     = 0x00800000;
const SF_IsBorderTileYPos     = 0x01000000;
const SF_IsBorderTileYNeg     = 0x02000000;
const SF_IsWaterTile          = 0x04000000;
const SF_FloorTileOccupier    = 0x08000000;

// ---------------------------------------------------------------------------
// Entry points (called from native, one int per coordinate)
// ---------------------------------------------------------------------------

// Recon §8.1, §8.3, §8.5. Everything about one tile in one line: the raw flag words, our decode of
// both, the engine's own decode of the static half, and the four cardinal edges as
// IsAdjacentTileBlocked answers them.
function ProbeTile(int X, int Y, int Z)
{
	local XComWorldData WorldData;
	local TileData Data;
	local TTile T;
	local string R;

	NativeResult = "";
	WorldData = `XWORLD;
	if (WorldData == none)
	{
		NativeResult = "no world data";
		return;
	}
	T.X = X;
	T.Y = Y;
	T.Z = Z;
	if (WorldData.IsTileOutOfRange(T))
	{
		NativeResult = "tile out of range (grid is" @ string(WorldData.NumX) $ "x"
			$ string(WorldData.NumY) $ "x" $ string(WorldData.NumZ) $ ")";
		return;
	}

	WorldData.GetTileData(T, Data);

	// ⛔ EVERY LINE THIS CLASS EMITS STAYS UNDER ~150 CHARACTERS. `/props` prints a string value
	// through gui_dump.cpp's kValueChars = 160 cap and marks the cut with an ellipsis, so a longer
	// line loses its TAIL — and the tail is where the answer was, every time. One question per
	// function, raw words plus one decode, nothing else. (Measured 2026-09-16: the first version of
	// ProbeTile put the engine string and the adjacency test last and never once showed either.)
	R = string(X) $ "," $ string(Y) $ "," $ string(Z);
	R $= " st=0x" $ ToHex(Data.StaticFlags);
	R $= " cv=0x" $ ToHex(Data.CoverFlags);
	R $= " dy=0x" $ ToHex(Data.DynamicWorldFlags);
	R $= " ip=" $ string(Data.InteractPoints.Length);
	R $= " tr=" $ string(Data.TraversalStarts.Length);
	R $= " [" $ DecodeCover(Data.CoverFlags) $ "]";
	R $= " path:" $ Faces(Data.StaticFlags, SF_BlocksPathingXNeg);
	R $= " vis:" $ Faces(Data.StaticFlags, SF_BlocksVisibilityXNeg);
	NativeResult = R;
}

// Recon §8.3, and the question that turned out to matter most. The per-face StaticFlags bits are
// ASYMMETRIC — measured 2026-09-16: tile (10,10,1) carries BlocksPathingXNeg for the edge toward
// (9,10,1), and (9,10,1) does NOT carry BlocksPathingXPos for the same edge. That is RT's
// HasFenceWithNode trap in a new costume (rt-room-classifier, law 1): a one-sided query dressed as
// a wall query. So the question is whether IsAdjacentTileBlocked is the SYMMETRIC oracle —
// XCOM's IsConnectionCut — and the only way to know is to ask it both ways round.
//
// Prints, per cardinal: the forward answer, the reverse answer, and the two tiles' own face bits.
// `!` marks a disagreement between forward and reverse, which is the thing to look for.
function ProbeAdjacent(int X, int Y, int Z)
{
	local XComWorldData WorldData;
	local TTile T;
	local string R;

	NativeResult = "";
	WorldData = `XWORLD;
	if (WorldData == none)
	{
		NativeResult = "no world data";
		return;
	}
	T.X = X;
	T.Y = Y;
	T.Z = Z;

	R = string(X) $ "," $ string(Y) $ "," $ string(Z) $ " fwd/rev:";
	Add(R, EdgePair(WorldData, T, -1, 0, "xNeg"));
	Add(R, EdgePair(WorldData, T, 1, 0, "xPos"));
	Add(R, EdgePair(WorldData, T, 0, -1, "yNeg"));
	Add(R, EdgePair(WorldData, T, 0, 1, "yPos"));
	NativeResult = R;
}

// One edge, asked from both ends. "1/1" is agreement, "1/0" or "0/1" is the asymmetry, and the
// trailing "!" makes it impossible to miss while skimming four of these on one line.
private function string EdgePair(XComWorldData WorldData, TTile T, int DX, int DY, string Label)
{
	local TTile N;
	local bool bFwd, bRev;
	local string S;

	N = T;
	N.X = T.X + DX;
	N.Y = T.Y + DY;
	if (WorldData.IsTileOutOfRange(N))
		return Label $ ":edge";
	bFwd = WorldData.IsAdjacentTileBlocked(T, N);
	bRev = WorldData.IsAdjacentTileBlocked(N, T);
	// ⛔ `bFwd != bRev` does not compile — UnrealScript has no bool-to-bool `!=` and reports it as
	// "Const mismatch with '!='". (`X != 0` on an int is fine, which is why the rest of this file
	// gets away with it.) Spell the disagreement out.
	S = Label $ ":" $ (bFwd ? "1" : "0") $ "/" $ (bRev ? "1" : "0");
	if ((bFwd && !bRev) || (!bFwd && bRev))
		S $= "!";
	return S;
}

// Recon §8.5, split out of ProbeTile because the two together never fit under the cap. The engine's
// own decode of the static word beside ours — the ONLY check the hand-transcribed constants have.
function ProbeStaticDecode(int X, int Y, int Z)
{
	local XComWorldData WorldData;
	local TileData Data;
	local TTile T;

	NativeResult = "";
	WorldData = `XWORLD;
	if (WorldData == none)
	{
		NativeResult = "no world data";
		return;
	}
	T.X = X;
	T.Y = Y;
	T.Z = Z;
	WorldData.GetTileData(T, Data);
	// Engine FIRST. If the cap bites, the half that gets cut must be the half we could have
	// recomputed from the raw word ourselves — never the oracle.
	NativeResult = "engine[" $ WorldData.GetTileDebugString_StaticFlags(T) $ "] ours["
		$ DecodeStatic(Data.StaticFlags) $ "]";
}

// Recon §8.4. What TraversalStarts actually holds — declared in XComWorldData.uc:187 and referenced
// NOWHERE in shipped script, so whether it is "tiles a traversal leads to", "tiles one arrives
// from", or editor-time residue is unknown. Print them as offsets from the probed tile, which makes
// the answer obvious at a glance.
function ProbeTraversals(int X, int Y, int Z)
{
	local XComWorldData WorldData;
	local TileData Data;
	local TTile T;
	local string R;
	local int i;

	NativeResult = "";
	WorldData = `XWORLD;
	if (WorldData == none)
	{
		NativeResult = "no world data";
		return;
	}
	T.X = X;
	T.Y = Y;
	T.Z = Z;
	WorldData.GetTileData(T, Data);

	R = "tile" @ string(X) $ "," @ string(Y) $ "," @ string(Z)
		@ "traversalStarts=" $ string(Data.TraversalStarts.Length);
	for (i = 0; i < Data.TraversalStarts.Length && i < 12; i++)
	{
		R $= " |" @ string(Data.TraversalStarts[i].X) $ "," $ string(Data.TraversalStarts[i].Y)
			$ "," $ string(Data.TraversalStarts[i].Z)
			@ "(offset" @ string(Data.TraversalStarts[i].X - X) $ ","
			$ string(Data.TraversalStarts[i].Y - Y) $ ","
			$ string(Data.TraversalStarts[i].Z - Z) $ ")";
	}
	NativeResult = R;
}

// Recon §8.2. Do TileData.CoverFlags and XComCoverPoint.Flags live in the same bit space? If they
// do, the cover ring and any room map share ONE oracle and cannot drift apart; if they do not, the
// room map must say which it used and why. GetCoverPointAtFloor is the call X2AccessTileReader
// already makes, so this compares our existing oracle against the new one on the same tile.
function ProbeCoverAgreement(int X, int Y, int Z)
{
	local XComWorldData WorldData;
	local TileData Data;
	local XComCoverPoint CP;
	local TTile T;
	local vector TilePos;
	local string R;

	NativeResult = "";
	WorldData = `XWORLD;
	if (WorldData == none)
	{
		NativeResult = "no world data";
		return;
	}
	T.X = X;
	T.Y = Y;
	T.Z = Z;
	WorldData.GetTileData(T, Data);
	TilePos = WorldData.GetPositionFromTileCoordinates(T);

	R = "tile" @ string(X) $ "," @ string(Y) $ "," @ string(Z);
	R $= " | tiledata.CoverFlags=0x" $ ToHex(Data.CoverFlags) @ "[" $ DecodeCover(Data.CoverFlags) $ "]";
	if (WorldData.GetCoverPointAtFloor(TilePos, CP))
		R $= " | coverpoint.Flags=0x" $ ToHex(CP.Flags) @ "[" $ DecodeCover(CP.Flags) $ "]";
	else
		R $= " | coverpoint: NONE at this floor";
	NativeResult = R;
}

// Recon §8.6, the cheap half — the whole point being to render a CROSS-SECTION in one call rather
// than twenty. It walks +X and prints one cell per EDGE (tile i to tile i+1), because a boundary
// lives on an edge and the candidate predicates are all edge predicates.
//
// FOUR characters per edge, and no legend in the output — the cap is 160 characters and a legend
// would eat half of it. The legend lives in docs/rooms-recon.md:
//   1st  B blocked (IsAdjacentTileBlocked, asked forward) . open  ! forward and reverse disagree
//   2nd  H high cover across it   L low cover   - none
//   3rd  O climb-over   N climb-onto   - neither
//   4th  V blocks visibility across the same face   - sees through
// So "B--V" is a wall, "BLO-" is a vaultable sandbag, "..--" is open floor, and a run of "B--V" is
// the thing a room boundary is made of. The fourth character is what makes predicate (b) readable
// off a cross-section at all — without it a row could score (a) and (c) and not the third.
function ProbeRow(int X, int Y, int Z, int Count)
{
	local XComWorldData WorldData;
	local TileData Data;
	local TTile T, N;
	local string R;
	local int i;
	local bool bFwd, bRev;

	NativeResult = "";
	WorldData = `XWORLD;
	if (WorldData == none)
	{
		NativeResult = "no world data";
		return;
	}
	if (Count <= 0 || Count > 32)
		Count = 24;

	R = string(X) $ "," $ string(Y) $ "," $ string(Z) $ "+" $ string(Count) $ ":";
	for (i = 0; i < Count; i++)
	{
		T.X = X + i;
		T.Y = Y;
		T.Z = Z;
		N = T;
		N.X = T.X + 1;
		if (WorldData.IsTileOutOfRange(T) || WorldData.IsTileOutOfRange(N))
		{
			R $= " ..";
			continue;
		}
		// ⛔ Read the NEARER tile's WEST face, not the farther tile's. West is +X in our compass
		// (X2AccessTileReader's NORTH_DX/EAST_DX table), so N at x+1 lies to the WEST of T, and a
		// barrier between them shows up as T's COVER_West. Reading N's COVER_West describes the
		// edge beyond N — one edge too far — which is how the first version put a sandbag's
		// `LO` on the edge before the one it actually sits in (measured 2026-09-16 against
		// (11,7,1), whose W:low+over is the barrier toward (12,7,1)). The blocked character was
		// always right: IsAdjacentTileBlocked is asked about the T-N pair directly.
		WorldData.GetTileData(T, Data);
		bFwd = WorldData.IsAdjacentTileBlocked(T, N);
		bRev = WorldData.IsAdjacentTileBlocked(N, T);

		R $= " ";
		// Same bool-comparison limit as EdgePair; spelled out for the same reason.
		if ((bFwd && !bRev) || (!bFwd && bRev))
			R $= "!";
		else
			R $= bFwd ? "B" : ".";
		if ((Data.CoverFlags & class'XComWorldData'.const.COVER_West) != 0)
			R $= (Data.CoverFlags & class'XComWorldData'.const.COVER_WLow) != 0 ? "L" : "H";
		else
			R $= "-";
		if ((Data.CoverFlags & class'XComWorldData'.const.COVER_ClimbOver_W) != 0)
			R $= "O";
		else if ((Data.CoverFlags & class'XComWorldData'.const.COVER_ClimbOnto_W) != 0)
			R $= "N";
		else
			R $= "-";
		// The visibility half of predicate (b), on the same face as the cover bits above: +X is
		// WEST, so the face is XPos. Same axis rule as the fix comment.
		R $= (Data.StaticFlags & SF_BlocksVisibilityXPos) != 0 ? "V" : "-";
	}
	NativeResult = R;
}

// ---------------------------------------------------------------------------
// Recon §8.6 — the segmentation predicate, SCORED rather than eyeballed
// ---------------------------------------------------------------------------
//
// ⛔ §8.6 as written says "render all three predicates over a raster and judge by eye". That is the
// wrong instrument for the only reader this project has: a screenshot shows one camera angle of an
// isometric map and cannot be compared against a 41-wide grid of characters with any confidence.
// So the raster stays (ProbeRoomRow, for shape) but the JUDGEMENT moves onto numbers that a wrong
// predicate cannot survive — the same move the cover-distribution rule makes in CLAUDE.md, where
// counting found what listening never would.
//
// The three predicates differ in ONE clause and this isolates it: all three agree that a boundary
// must BLOCK (IsAdjacentTileBlocked, the measured oracle of §8.3), and they disagree only about
// what makes a blocking edge a WALL rather than something to hide behind.
//   0 = (a) climb-gated    — has cover, and the game offers neither ClimbOver nor ClimbOnto
//   1 = (b) sight-gated    — blocks visibility across the same face
//   2 = (c) cover-gated    — has cover, and it is not Low
// ⚠ (b) is implemented as "blocked AND blocks visibility" rather than §3's literal
// "BlocksPathing && BlocksVisibility", deliberately: sharing the blocked half with (a) and (c)
// leaves exactly one variable between the three readings, which is what makes the counts
// comparable. A disagreement between IsAdjacentTileBlocked and the BlocksPathing bit would be its
// own finding and ProbeRow prints both.
//
// ⚠ FOG IS IGNORED HERE, on purpose and unlike anything that ships. This measures GEOMETRY; the
// parity rule that a room may never be built through never-seen tiles (§5.1) belongs to the
// shipping segmenter, and applying it here would confound the comparison with wherever the squad
// happened to have walked.
const ROOM_RADIUS_MAX = 30;
const ROOM_TINY = 4;        // cells; a "room" smaller than this is over-segmentation, not a room

// The whole partition in one line. Run it three times, once per predicate, and the numbers say
// which one is describing the building: over-segmentation shows up as a high room count with a
// large `tiny`, under-segmentation as one room swallowing most of the cells.
function ProbeRooms(int X, int Y, int Z, int Predicate, int Radius)
{
	local array<int> Room;
	local array<int> Sizes;
	local int W, Count, Cells, Walls, Open, Tiny, Largest, Median;
	local int i;

	if (!Partition(X, Y, Z, Predicate, Radius, Room, W, Count, Cells, Walls, Open))
		return;

	Sizes.Length = Count;
	for (i = 0; i < Room.Length; i++)
	{
		if (Room[i] >= 0)
			Sizes[Room[i]] = Sizes[Room[i]] + 1;
	}
	for (i = 0; i < Sizes.Length; i++)
	{
		if (Sizes[i] > Largest)
			Largest = Sizes[i];
		if (Sizes[i] < ROOM_TINY)
			Tiny++;
	}
	Sizes.Sort(CompareInts);
	if (Sizes.Length > 0)
		Median = Sizes[Sizes.Length / 2];

	NativeResult = "p=" $ PredicateName(Predicate) $ " r=" $ string(Radius)
		$ " w=" $ string(W)
		$ " cells=" $ string(Cells) $ " rooms=" $ string(Count)
		$ " max=" $ string(Largest) $ " med=" $ string(Median)
		$ " tiny=" $ string(Tiny)
		$ " edges w/o=" $ string(Walls) $ "/" $ string(Open);
}

// ⛔ THE COLUMN FIRST, ALWAYS — a partition is a HORIZONTAL SLICE and a map is not flat. The
// 2026-09-17 run took Z from the tile cursor, found one room, and concluded the predicates were
// indistinguishable; the cursor was standing on a plaza, and the building with eight rooms in it
// was four Z-levels up. A floor the slice misses reads as `#` — identical to solid rock — so a
// single-Z reading can call a building a wall and never look wrong.
//
// One line per call: every Z with standable ground, its cell count and its room count, so the
// floors announce themselves before anything is concluded from one of them.
function ProbeRoomsColumn(int X, int Y, int Radius)
{
	local array<int> Room;
	local int Z, W, Count, Cells, Walls, Open;
	local string R;

	R = string(X) $ "," $ string(Y) $ " r=" $ string(Radius) $ ":";
	for (Z = 0; Z < 32; Z++)
	{
		if (!Partition(X, Y, Z, 0, Radius, Room, W, Count, Cells, Walls, Open))
			break;
		if (Cells == 0)
			continue;
		R $= " z" $ string(Z) $ "=" $ string(Cells) $ "/" $ string(Count);
		// The cap is 160 and the tail is the half that would be cut, so stop while it is still ours.
		if (Len(R) > 140)
		{
			R $= " ...";
			break;
		}
	}
	NativeResult = R;
}

// One row of the partition, as one character per cell: `a`..`z` cycling per room id, `#` where the
// ground is not standable. Room ids are assigned in scan order, so two cells sharing a letter in
// the same neighbourhood really are one room — the cycle only lies across 26 rooms apart, which on
// a 41-wide window is a shape a reader can still follow.
//
// This is the SHAPE half, and it stays because numbers alone cannot show a room leaking down a
// corridor into the street. Row 0 is the top (Y = Y0 + Radius, i.e. NORTH-most, since north is +Y).
function ProbeRoomRow(int X, int Y, int Z, int Predicate, int Radius, int Row)
{
	local array<int> Room;
	local int W, Count, Cells, Walls, Open, i, Id;
	local string R;

	if (!Partition(X, Y, Z, Predicate, Radius, Room, W, Count, Cells, Walls, Open))
		return;
	if (Row < 0 || Row >= W)
	{
		NativeResult = "row out of range (0.." $ string(W - 1) $ ")";
		return;
	}

	R = PredicateName(Predicate) $ " y" $ string(Y + Radius - Row) $ ":";
	for (i = 0; i < W; i++)
	{
		Id = Room[(W - 1 - Row) * W + i];
		R $= (Id < 0) ? "#" : Mid("abcdefghijklmnopqrstuvwxyz", Id % 26, 1);
	}
	NativeResult = R;
}

// The shared flood. Returns false (and writes the reason) if the world is not there.
//
// 4-WAY, never 8-way — and this is the one structural difference from AddFrontier's fill, which is
// 8-way because a fog seam has no edges. A boundary here LIVES on a cardinal edge, so a diagonal
// step has no edge to test and would leak a room straight through the corner of a wall.
private function bool Partition(int X0, int Y0, int Z, int Predicate, int Radius,
	out array<int> Room, out int W, out int Count, out int Cells, out int Walls, out int Open)
{
	local XComWorldData WorldData;
	local array<int> Stack;
	local TTile T, N;
	local int i, DX, DY, Cell, NIdx, CX, CY;

	NativeResult = "";
	WorldData = `XWORLD;
	if (WorldData == none)
	{
		NativeResult = "no world data";
		return false;
	}
	if (Radius <= 0 || Radius > ROOM_RADIUS_MAX)
		Radius = 12;
	W = Radius * 2 + 1;

	// -1 unassigned-and-standable, -2 not standable. Filled as -2 first so the bounds cases and
	// the void both read the same to the flood.
	Room.Length = 0;
	Room.Length = W * W;
	T.Z = Z;
	for (i = 0; i < Room.Length; i++)
	{
		T.X = X0 - Radius + (i % W);
		T.Y = Y0 - Radius + (i / W);
		if (WorldData.IsTileOutOfRange(T) || !WorldData.IsFloorTileAndValidDestination(T))
			Room[i] = -2;
		else
		{
			Room[i] = -1;
			Cells++;
		}
	}

	for (i = 0; i < Room.Length; i++)
	{
		if (Room[i] != -1)
			continue;
		Room[i] = Count;
		Stack.Length = 0;
		Stack.AddItem(i);
		while (Stack.Length > 0)
		{
			Cell = Stack[Stack.Length - 1];
			Stack.Remove(Stack.Length - 1, 1);
			CX = Cell % W;
			CY = Cell / W;
			T.X = X0 - Radius + CX;
			T.Y = Y0 - Radius + CY;
			for (DY = -1; DY <= 1; DY++)
			{
				for (DX = -1; DX <= 1; DX++)
				{
					// Cardinal only: exactly one of the two offsets is non-zero.
					if ((DX == 0 && DY == 0) || (DX != 0 && DY != 0))
						continue;
					if (CX + DX < 0 || CY + DY < 0 || CX + DX >= W || CY + DY >= W)
						continue;
					NIdx = (CY + DY) * W + (CX + DX);
					if (Room[NIdx] != -1)
						continue;
					N = T;
					N.X = T.X + DX;
					N.Y = T.Y + DY;
					if (IsWallEdge(WorldData, T, N, Predicate))
						continue;
					Room[NIdx] = Count;
					Stack.AddItem(NIdx);
				}
			}
		}
		Count++;
	}

	// The edge census, counted ONCE per edge (+X and +Y only) over standable pairs, so the two
	// numbers say what fraction of the passable geometry this predicate is calling wall.
	for (i = 0; i < Room.Length; i++)
	{
		if (Room[i] < 0)
			continue;
		CX = i % W;
		CY = i / W;
		T.X = X0 - Radius + CX;
		T.Y = Y0 - Radius + CY;
		if (CX + 1 < W && Room[i + 1] >= 0)
		{
			N = T;
			N.X = T.X + 1;
			if (IsWallEdge(WorldData, T, N, Predicate)) Walls++; else Open++;
		}
		if (CY + 1 < W && Room[i + W] >= 0)
		{
			N = T;
			N.Y = T.Y + 1;
			if (IsWallEdge(WorldData, T, N, Predicate)) Walls++; else Open++;
		}
	}
	return true;
}

// The one clause the three predicates disagree about. Everything before the switch is shared.
//
// ⛔ BOTH ENDS, ALWAYS — asking this forward only was a real bug and it survived a whole live run.
// IsAdjacentTileBlocked is DIRECTED (§2.3) and ONE-WAY EDGES ARE REAL between two STANDABLE
// tiles: (27,32,9) on the 2026-09-17 map could be left and not entered, because the query folds in
// the DESTINATION's own occupancy and a unit was standing there. A forward-only test made the whole
// partition SCAN-ORDER DEPENDENT — whichever region reached the shared tile first kept it, and
// reversing the order would have merged two rooms into one. Room counts taken that way are not
// reproducible, which is the one thing a measuring instrument may not be.
//
// The per-face bits get the same treatment for the same reason: §8.3 measured them asymmetric, so a
// wall recorded on one tile and not its neighbour is the normal case, not the exception.
private function bool IsWallEdge(XComWorldData WorldData, TTile T, TTile N, int Predicate)
{
	if (!WorldData.IsAdjacentTileBlocked(T, N) && !WorldData.IsAdjacentTileBlocked(N, T))
		return false;
	return FaceIsWall(WorldData, T, N.X - T.X, N.Y - T.Y, Predicate)
		|| FaceIsWall(WorldData, N, T.X - N.X, T.Y - N.Y, Predicate);
}

// One tile's own face, in the direction given. Split out of IsWallEdge so the symmetric test above
// reads as one line per end rather than as two copies of the switch.
private function bool FaceIsWall(XComWorldData WorldData, TTile T, int DX, int DY, int Predicate)
{
	local TileData Data;
	local int Side, LowBit, OverBit, OntoBit, VisBit;

	WorldData.GetTileData(T, Data);
	EdgeBits(DX, DY, Side, LowBit, OverBit, OntoBit, VisBit);

	switch (Predicate)
	{
	case 1:
		return (Data.StaticFlags & VisBit) != 0;
	case 2:
		return (Data.CoverFlags & Side) != 0 && (Data.CoverFlags & LowBit) == 0;
	}
	return (Data.CoverFlags & Side) != 0 && (Data.CoverFlags & (OverBit | OntoBit)) == 0;
}

// The per-face constants for one cardinal step, all five of them together so a transposition is
// visible at a glance rather than hidden across five switch statements.
//
// ⛔ THE COMPASS IS NOT THE OBVIOUS ONE. X2AccessTileReader's table is NORTH_DY=+1 and EAST_DX=-1,
// so **+X is WEST and +Y is NORTH** — the mapping that ProbeRow got wrong the first time and had
// to be measured against a sandbag to fix.
private function EdgeBits(int DX, int DY, out int Side, out int LowBit, out int OverBit,
	out int OntoBit, out int VisBit)
{
	if (DX > 0)        // +X is WEST
	{
		Side = class'XComWorldData'.const.COVER_West;
		LowBit = class'XComWorldData'.const.COVER_WLow;
		OverBit = class'XComWorldData'.const.COVER_ClimbOver_W;
		OntoBit = class'XComWorldData'.const.COVER_ClimbOnto_W;
		VisBit = SF_BlocksVisibilityXPos;
	}
	else if (DX < 0)   // -X is EAST
	{
		Side = class'XComWorldData'.const.COVER_East;
		LowBit = class'XComWorldData'.const.COVER_ELow;
		OverBit = class'XComWorldData'.const.COVER_ClimbOver_E;
		OntoBit = class'XComWorldData'.const.COVER_ClimbOnto_E;
		VisBit = SF_BlocksVisibilityXNeg;
	}
	else if (DY > 0)   // +Y is NORTH
	{
		Side = class'XComWorldData'.const.COVER_North;
		LowBit = class'XComWorldData'.const.COVER_NLow;
		OverBit = class'XComWorldData'.const.COVER_ClimbOver_N;
		OntoBit = class'XComWorldData'.const.COVER_ClimbOnto_N;
		VisBit = SF_BlocksVisibilityYPos;
	}
	else               // -Y is SOUTH
	{
		Side = class'XComWorldData'.const.COVER_South;
		LowBit = class'XComWorldData'.const.COVER_SLow;
		OverBit = class'XComWorldData'.const.COVER_ClimbOver_S;
		OntoBit = class'XComWorldData'.const.COVER_ClimbOnto_S;
		VisBit = SF_BlocksVisibilityYNeg;
	}
}

private function string PredicateName(int Predicate)
{
	switch (Predicate)
	{
	case 1: return "b-sight";
	case 2: return "c-height";
	}
	return "a-climb";
}

private function int CompareInts(int A, int B)
{
	return (A < B) ? -1 : ((A > B) ? 1 : 0);
}

// ---------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------

// Our transcription of TileStaticFlags. Compared against GetTileDebugString_StaticFlags by
// ProbeTile; if the two ever disagree, THIS is the thing that is wrong.
private function string DecodeStatic(int F)
{
	local string R;

	if ((F & SF_IsFloorTile) != 0)        Add(R, "floor");
	if ((F & SF_IsRampTile) != 0)         Add(R, "ramp");
	if ((F & SF_Occupied) != 0)           Add(R, "occupied");
	if ((F & SF_OccupiedPartial) != 0)    Add(R, "occupiedPartial");
	if ((F & SF_PathBlocking) != 0)       Add(R, "pathBlocking");
	if ((F & SF_CanEndMoveOnTile) != 0)   Add(R, "canEndMove");
	if ((F & SF_IsLedgeTile) != 0)        Add(R, "ledge");
	if ((F & SF_LargeUnitClearance) != 0) Add(R, "largeClear");
	if ((F & SF_IsWaterTile) != 0)        Add(R, "water");
	if ((F & SF_FloorTileOccupier) != 0)  Add(R, "floorOccupier");
	Add(R, "path:" $ Faces(F, SF_BlocksPathingXNeg));
	Add(R, "vis:" $ Faces(F, SF_BlocksVisibilityXNeg));
	if ((F & (SF_IsBorderTileXPos | SF_IsBorderTileXNeg | SF_IsBorderTileYPos | SF_IsBorderTileYNeg)) != 0)
		Add(R, "border");
	return R;
}

// The six per-face bits of one group, in the declared order XNeg XPos YNeg YPos ZNeg ZPos, as a
// six-character mask. Taking the group's FIRST constant and shifting keeps the two groups reading
// identically and makes a transposed transcription obvious.
private function string Faces(int F, int FirstBit)
{
	local string R;
	local int i;

	for (i = 0; i < 6; i++)
		R $= (F & (FirstBit << i)) != 0 ? Mid("xXyYzZ", i, 1) : "-";
	return R;
}

private function string DecodeCover(int F)
{
	local string R;

	CoverSide(R, F, class'XComWorldData'.const.COVER_North, class'XComWorldData'.const.COVER_NLow,
		class'XComWorldData'.const.COVER_ClimbOver_N, class'XComWorldData'.const.COVER_ClimbOnto_N, "N");
	CoverSide(R, F, class'XComWorldData'.const.COVER_South, class'XComWorldData'.const.COVER_SLow,
		class'XComWorldData'.const.COVER_ClimbOver_S, class'XComWorldData'.const.COVER_ClimbOnto_S, "S");
	CoverSide(R, F, class'XComWorldData'.const.COVER_East, class'XComWorldData'.const.COVER_ELow,
		class'XComWorldData'.const.COVER_ClimbOver_E, class'XComWorldData'.const.COVER_ClimbOnto_E, "E");
	CoverSide(R, F, class'XComWorldData'.const.COVER_West, class'XComWorldData'.const.COVER_WLow,
		class'XComWorldData'.const.COVER_ClimbOver_W, class'XComWorldData'.const.COVER_ClimbOnto_W, "W");
	if ((F & class'XComWorldData'.const.COVER_Diagonal) != 0)
		Add(R, "DIAGONAL(local N = world NE)");
	if (R == "")
		R = "none";
	return R;
}

// One side's four facts. The climb words are the whole point of the exercise: recon §3 prefers a
// segmentation predicate built on ClimbOver, and COVER_ClimbOver_* / COVER_ClimbOnto_* are declared
// in XComWorldData.uc and read by NOTHING in shipped script — so whether they are ever POPULATED is
// unverified, and this is what verifies it.
private function CoverSide(out string R, int F, int Bit, int LowBit, int OverBit, int OntoBit,
	string Side)
{
	local string S;

	if ((F & Bit) == 0 && (F & OverBit) == 0 && (F & OntoBit) == 0)
		return;
	S = Side $ ":";
	if ((F & Bit) != 0)
		S $= (F & LowBit) != 0 ? "low" : "high";
	else
		S $= "nocover";
	if ((F & OverBit) != 0)
		S $= "+over";
	if ((F & OntoBit) != 0)
		S $= "+onto";
	Add(R, S);
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

private function Add(out string R, string Part)
{
	if (Part == "")
		return;
	if (R != "")
		R $= " ";
	R $= Part;
}

// Hex formatting is the ENGINE's: Object.ToHex (Core/Object.uc:813) is `native static final`, so a
// hand-rolled one of the same name is not a helper but an override attempt, and the compiler says
// so — "Can't override a 'final' function". Nothing to write here; the calls above use it directly.
