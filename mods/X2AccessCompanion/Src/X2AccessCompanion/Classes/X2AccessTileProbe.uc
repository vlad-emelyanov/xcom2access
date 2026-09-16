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
// Three characters per edge, and no legend in the output — the cap is 160 characters and a legend
// would eat two thirds of it. The legend lives in docs/rooms-recon.md:
//   1st  B blocked (IsAdjacentTileBlocked, asked forward) . open  ! forward and reverse disagree
//   2nd  H high cover across it   L low cover   - none
//   3rd  O climb-over   N climb-onto   - neither
// So "B--" is a wall, "BLO" is a vaultable sandbag, "..-" is open floor, and a run of "B--" is the
// thing a room boundary is made of.
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
	}
	NativeResult = R;
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
