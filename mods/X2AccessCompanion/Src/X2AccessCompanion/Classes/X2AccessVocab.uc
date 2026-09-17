// X2AccessVocab
// Every word of OURS that reaches a player, in one place, on the vanilla localization system.
//
// The game's words stay the game's: a cover type, a hit result, a unit's name, an ability's name,
// an objective sentence are all read off the template or the screen that owns them. This class
// holds ONLY what the game has nowhere — the tile cursor's compass, the scanner's category names,
// "Press again to move". Before adding a key here, look for the game's own word first.
//
// ONE class, deliberately. A localized var declared on two classes and populated on one fails
// silently (docs/engine-notes.md, the double-space tell), so every localized var the companion
// owns is declared here and nowhere else, and every reader reads it through default. Native reads
// the same values off this class's CDO by property name (docs/localization.md).
//
// The values live in Localization/X2AccessCompanion.<lang> under [X2AccessVocab]. The tracked
// files are UTF-8; X2ModBuildCommon rewrites them to UTF-16 in staging, which is what the engine
// reads. A key missing from a language file reads as "" — the readers treat "" as "say nothing",
// so a missing key is a silent gap, and the .int file is the reference every other language is
// checked against.
//
// WORD ORDER is the translator's. A template carries vanilla's own tags — <XGParam:IntValue0/>,
// <XGParam:IntValue1/>, <XGParam:StrValue0/> — and the Fill functions below expand them through the
// game's XGParamTag exactly as UIArmory or SeqAct_ExpandLocalizedString do. The translator moves
// a tag, never the code.
//
// PLURALS. Vanilla has no rule (393 Singular/Plural pairs picked by == 1), so this class has one:
// a counted noun is THREE keys, <Name>_One, <Name>_Few and <Name>_Many, each a template over
// IntValue0, and Count() picks by PluralRule. Under the default English rule One is for exactly 1
// and everything else is Many; under "slavic" the last two digits decide (1, 21, 31 One; 2-4,
// 22-24 Few; 5-20, 25-30 and every 11-14 Many). English fills One and Many and leaves Few empty;
// an empty form falls to Many, and an empty Many falls to One, so a half-filled file still speaks.
//
// GC rule: this object is a CDO — value-typed state only, never object references. It holds no
// state at all; everything is static.

class X2AccessVocab extends Object;

// "" or "english": One is exactly 1. "slavic": the last-two-digits rule. Set once per language
// file, not per word.
var localized string PluralRule;

// ---------------------------------------------------------------------------
// Shared by the tile cursor and the object scanner
// ---------------------------------------------------------------------------

var localized string NoUnitSelected;

// A count of tiles, the distance clause of both surfaces.
var localized string Tiles_One;
var localized string Tiles_Few;
var localized string Tiles_Many;

// The reachability words — the blue/yellow move highlight, spoken. Same three words on a tile and
// on a marker, so the two surfaces agree by construction.
var localized string ReachOneAction;
var localized string ReachDash;
var localized string ReachOutOfRange;
var localized string ReachYourTile;

var localized string ViaSquadsight;
var localized string Flanked;

// A bearing from the soldier or the scan origin, one leg each; the legs are comma-joined.
var localized string OffsetNorth;
var localized string OffsetSouth;
var localized string OffsetEast;
var localized string OffsetWest;
var localized string OffsetUp;
var localized string OffsetDown;

// Interactive objects the game left unnamed, and their state.
var localized string Door;
var localized string HackableObject;
var localized string InteractiveObject;
var localized string Passage;
var localized string LockedStrength;
var localized string Hacked;
var localized string HasLoot;

var localized string Loot;
var localized string LootFrom;
var localized string PsiLoot;
var localized string LootExpires;
var localized string InTurns_One;
var localized string InTurns_Few;
var localized string InTurns_Many;

// Hazards on a tile, by type. The scanner's per-type ring and the cursor's per-tile line share them.
var localized string Fire;
var localized string Smoke;
var localized string Poison;
var localized string Acid;
var localized string Hazard;

var localized string EvacZone;
var localized string EvacZoneNoClearance;
var localized string EvacSize;
var localized string EvacInside;

var localized string DetectionZone;
var localized string TowerDetection;

var localized string Ally;
var localized string Enemy;

var localized string Ramp;
var localized string Ladder;
var localized string Water;

// ---------------------------------------------------------------------------
// The tile cursor
// ---------------------------------------------------------------------------

var localized string MapEdge;
var localized string TopFloor;
var localized string BottomFloor;
var localized string CursorOn;
var localized string CursorJumped;
var localized string NoMarkerSelected;

var localized string NoMoveAvailable;
var localized string AlreadyHere;
var localized string CannotStandThere;
var localized string CannotStopThere;
var localized string Nearest;
var localized string PressAgainToMove;
var localized string Moving;
var localized string CannotMoveThere;

var localized string Unexplored;
var localized string Impassable;
var localized string CannotStandHere;
var localized string Clear;
var localized string TileCoords;
var localized string SeenBy;

var localized string NotTargetable;
var localized string NoTargets;
var localized string TargetsHeading;

// Cover on a tile's edge, one key per edge and height so a translator declines the direction
// freely. The words are FULL/HALF, the game's UI vocabulary, not the engine's high/low flags.
var localized string NoCover;
var localized string FullCoverNorth;
var localized string FullCoverSouth;
var localized string FullCoverEast;
var localized string FullCoverWest;
var localized string HalfCoverNorth;
var localized string HalfCoverSouth;
var localized string HalfCoverEast;
var localized string HalfCoverWest;

var localized string BreaksConcealment;
var localized string Noise;

var localized string HeightUp;
var localized string HeightDown;
var localized string FloorOf;

var localized string Outside;
var localized string OnRoof;
var localized string InsideUFO;
var localized string InsideDropship;
var localized string InsideBuilding;
var localized string BuildingFloor;
var localized string BuildingFloorOf;
var localized string BuildingFloorChanged;
var localized string BuildingFloorChangedOf;

// ---------------------------------------------------------------------------
// The object scanner
// ---------------------------------------------------------------------------

var localized string NoScanOrigin;
var localized string NothingToScan;

var localized string CatMarkers;
var localized string CatEnemies;
var localized string CatSquad;
var localized string CatLoot;
var localized string CatReinforcements;
var localized string CatCivilians;
var localized string CatHazards;
var localized string CatObjects;
var localized string CatTowers;
var localized string CatConnectors;
var localized string CatCover;
var localized string CatUnexplored;
var localized string CatExits;

var localized string EmptyMarkers;
var localized string EmptyEnemies;
var localized string EmptySquad;
var localized string EmptyLoot;
var localized string EmptyReinforcements;
var localized string EmptyCivilians;
var localized string EmptyHazards;
var localized string EmptyObjects;
var localized string EmptyTowers;
var localized string EmptyConnectors;
var localized string EmptyCover;
var localized string EmptyUnexplored;
var localized string EmptyExits;
var localized string EmptyGeneric;

var localized string SpottedBySquad;
var localized string Selected;
var localized string Reinforcements;
var localized string Tutorial;
var localized string TutorialVerb;
var localized string Objective;
var localized string ObjectiveNamed;
var localized string MarkedByAdvent;
var localized string Marked;
var localized string Used;
var localized string CanBeCarried;
var localized string LostSwarm;
var localized string CountVisible;
var localized string TilesAcross_One;
var localized string TilesAcross_Few;
var localized string TilesAcross_Many;
var localized string TilesWide_One;
var localized string TilesWide_Few;
var localized string TilesWide_Many;
var localized string DetectsWithin_One;
var localized string DetectsWithin_Few;
var localized string DetectsWithin_Many;
var localized string UnexploredSpace;
var localized string PositionOf;

// The Chosen Avenger Defense's Mobile Power Generator, an XComInteractiveLevelActor whose
// FriendlyDisplayName Firaxis copy-pasted from the stronghold's sarcophagus. The game owns no noun
// for it anywhere but inside the objective SENTENCE, and a mod loc file under the map package's own
// name does not override the key (tried 2026-09-17, heard unchanged), so the word is ours.
var localized string ChosenADGeneratorName;

// ---------------------------------------------------------------------------
// Expansion
// ---------------------------------------------------------------------------

// One counted noun: the form PluralRule picks for N, with N filled in. A blank form falls through
// so a language file that filled only Many still speaks.
static function string Count(int N, string One, string Few, string Many)
{
	local string Template;

	switch (PluralForm(N))
	{
	case 0: Template = One; break;
	case 1: Template = Few; break;
	}
	if (Template == "")
		Template = Many;
	if (Template == "")
		Template = One;
	return Fill1(Template, N);
}

// 0 = One, 1 = Few, 2 = Many.
static function int PluralForm(int N)
{
	local int Tens, Units;

	N = Abs(N);
	if (default.PluralRule ~= "slavic")
	{
		Units = N % 10;
		Tens = N % 100;
		if (Units == 1 && Tens != 11)
			return 0;
		if (Units >= 2 && Units <= 4 && (Tens < 12 || Tens > 14))
			return 1;
		return 2;
	}
	return (N == 1) ? 0 : 2;
}

static function string Fill1(string Template, int A)
{
	return Expand(Template, A, 0, 0, "");
}

static function string Fill2(string Template, int A, int B)
{
	return Expand(Template, A, B, 0, "");
}

static function string Fill3(string Template, int A, int B, int C)
{
	return Expand(Template, A, B, C, "");
}

static function string FillS(string Template, string S)
{
	return Expand(Template, 0, 0, 0, S);
}

// The game's own expander over its own shared tag object. Every slot a template may name is
// written, the unused ones cleared, so a stale value from another caller can never leak into a
// line of ours. A template with no tags comes back unchanged; an empty one stays empty.
static function string Expand(string Template, int A, int B, int C, string S)
{
	local XGParamTag Tag;

	if (Template == "")
		return "";
	Tag = XGParamTag(`XEXPANDCONTEXT.FindTag("XGParam"));
	if (Tag == none)
		return Template;
	Tag.IntValue0 = A;
	Tag.IntValue1 = B;
	Tag.IntValue2 = C;
	Tag.StrValue0 = S;
	return `XEXPAND.ExpandString(Template);
}
