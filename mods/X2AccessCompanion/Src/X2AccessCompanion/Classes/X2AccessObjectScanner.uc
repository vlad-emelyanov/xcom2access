// X2AccessObjectScanner
// The object scanner's game-state half — the model. C++ (src/object_scanner.cpp) owns the keys
// and the speech; it calls these functions on this class's default object via ProcessEvent (game
// thread, from the tick drain) and reads NativeResult back through reflection right after the call
// returns. Nothing here runs on its own — no listeners, no timers, no polling.
//
// Eleven categories. Four carry a dedicated cycle key on the native side; the rest are reached
// through the ring. Each keeps its own remembered position:
//   markers               — the world-space things the game points the player AT. Three
//                           contributors in one ring: the tutorial's required-action marker
//                           (XComTutorialMgr.NextPlayerAbilityContext), objective-carrying objects
//                           (XComGameState_ObjectiveInfo components) and the evac zone. A tutorial
//                           waypoint and an objective glint are the same kind of thing to a player
//                           working out where to go, and folding them together is what stops the
//                           tutorial being a special case.
//   units.enemies         — visible aliens, and the Lost as ONE summary entry however many there
//                           are (a swarm is 10-20 bodies with one shared name; stepping through
//                           them individually is unusable and would bury the ADVENT pod behind
//                           them). They share this ring rather than holding one of their own
//                           because they ARE enemies — the enemy key is where a player looks for
//                           them — and because a category empty on most missions still costs a
//                           ring step on all of them.
//   units.squad           — your own soldiers.
//   ground.loot           — timed loot drops, with the turns left before they evaporate.
//   ground.reinforcements — pending drop points, with the turns left before they land.
//   ground.evac           — the evac RECTANGLE: its size, and how many of the squad are standing
//                           in it. Promoted out of `markers` rather than listed in both, so one
//                           thing is never named by two rings.
//   units.civilians       — civilians and VIPs, including the ones you have to carry out.
//   hazards               — fire / smoke / poison / acid, one entry per TYPE with the nearest tile
//                           and how far the blob spans. The tile cursor answers "is this tile on
//                           fire"; without this there is no way to FIND the fire.
//   objects.interact      — everything with a live interaction: doors, chests, terminals,
//                           elevators. Deliberately ONE ring where the recon doc had six — since
//                           InteractiveLabel names each object by its own interact ability, the
//                           split bought nothing but extra presses to reach the chest.
//   objects.towers        — ADVENT towers and their detection radius, shown ONLY while something
//                           of ours is concealed, because that is the only time the radius means
//                           anything. The ring's empty-skip makes a mode-dependent category free.
//   world.connectors      — the ramps and ladders between Z levels. The only category that is
//                           SWEPT rather than enumerated (no global ramp list exists), and so the
//                           only one whose empty text names a scope rather than the map.
//   ground.cover          — the tiles you could STAND ON to be behind something, named by the
//                           side(s) they cover, in or out of this turn's reach. Ported from
//                           WH40KRTAccess's J cycle (src/RTAccess/Exploration/CoverModel.cs),
//                           where the same verb answers the same question. Like the connectors it
//                           is local rather than map-wide, and its empty text names that scope.
//
// The last two exist because they are where the gap is WIDEST: the game gives a sighted player a
// countdown (a number on the HUD counter, a number on the loot waypoint) and a persistent glowing
// mark on the ground, and gives a blind player nothing at all. Every entry in both is gated on the
// game's own rule for drawing that mark, so the ring can never name something the screen does not.
//
// NO REGISTRY (and this is a deliberate departure from the RT scanner it ports). RT needs a ticked
// WorldModel because Unity entities are mutable references; XCOM's ObjectID is a stable int over a
// versioned history, so the candidate list is rebuilt from scratch on every keypress and the only
// thing that persists is the selection — an int key plus its tile. Cheaper to reason about, and it
// cannot go stale.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessObjectScanner extends Object;

// One candidate. Local to a scan; never stored (see NO REGISTRY above).
struct AccessMarker
{
	var int Key;        // stable identity across rebuilds: a state ObjectID, or KEY_* below
	var string Label;   // what gets spoken first
	var string Detail;  // optional second clause: health, cover, a countdown
	var TTile Tile;     // where it is
	var int Dist;       // tiles from the scan origin, for the sort

	// Is this somewhere the soldier would WALK to? Movement reachability is the wrong question
	// for a marker that points at something to shoot — an enemy 8 tiles away is perfectly
	// shootable from here, and saying "out of range" about it reads as "you cannot do this".
	var bool bIsDestination;
};

// Synthetic keys for the contributors that have no state ObjectID of their own. Negative so they
// can never collide with one (ObjectIDs are positive).
const KEY_TUTORIAL = -101;
const KEY_LOST_SWARM = -102;
const KEY_HAZARD = -200;    // minus the hazard type index — see HazardWord
// Minus a LINEAR TILE INDEX, so every connector tile on the map gets its own key and the
// remembered selection survives a rebuild. Far below the others: the index can run to the
// hundreds of thousands on a big grid.
const KEY_CONNECTOR = -300000000;
// The same scheme for cover spots, minus the LOWEST linear tile index in the cluster — which is
// origin-independent, so the selection survives the scan window sliding with the cursor where a
// key built from the nearest member would not. The 100-million gap below KEY_CONNECTOR is the
// collision budget: the two ranges cannot meet until a map has 100 million tiles.
const KEY_COVER = -400000000;

// Categories. Each has its own remembered position, so checking an enemy and then checking a
// marker does not lose your place in either.
//
// ⛔ FOUR INDICES ARE MIRRORED IN NATIVE — 0, 1, 2 AND 10. object_scanner.cpp hardcodes
// kCatMarkers 0, kCatEnemies 1, kCatSquad 2 and kCatCover 10 for its four cycle keys; every other
// category is reached only through StepCategory, which is relative and index-agnostic. So
// renumbering at 3..9 is free — as removing CAT_EVAC was — and renumbering any of the mirrored
// four is a two-file change or a silent wrong-ring bug. Change both halves or neither.
// ⚠ CAT_COVER sits at the END rather than beside the other keyed rings precisely to keep that
// free range contiguous: a new category appended after it would take 11 and shift nothing.
const CAT_MARKERS = 0;
const CAT_ENEMIES = 1;
const CAT_SQUAD = 2;
const CAT_LOOT = 3;
const CAT_REINFORCEMENTS = 4;
const CAT_CIVILIANS = 5;
const CAT_HAZARDS = 6;
const CAT_INTERACT = 7;
const CAT_TOWERS = 8;
const CAT_CONNECTORS = 9;
const CAT_COVER = 10;
const CAT_COUNT = 11;       // how many are REAL — what the category ring wraps around
const NUM_CATEGORIES = 16;  // headroom for the rest; a static array, so the slack costs nothing

// Hazard types, in the order HazardWord/HazardClass/TileHasHazard agree on.
const HAZARD_COUNT = 4;

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The selection. Value types only (CDO rule). X2AccessTileReader.CursorGoToMarker reads these
// through `class'X2AccessObjectScanner'.default.*` — a cross-class default READ, which is legal;
// writing another class's defaults is not, which is why the plant lives over there and not here.
// Per-category, so each cycle key resumes where it left off. A STATIC array: fixed size, value
// typed, no allocation — the CDO rule again.
var int SelKeyByCategory[16];

// Which ring the un-prefixed keys address. It lives HERE rather than in native because the ring
// step has to know which categories are empty, and only this side can answer that — so putting the
// cursor next to that answer keeps one source of truth instead of two that can drift.
var int CurCategory;

// The last thing actually announced, in any category. This is what `/` plants the tile cursor on,
// which is the right semantics: it acts on what you just heard, not on some notion of "current
// category".
var int SelX, SelY, SelZ;
var bool bSelValid;

// ---------------------------------------------------------------------------
// Entry points (called from native)
// ---------------------------------------------------------------------------

// Advance the selection within one NAMED category by Delta and speak it — what the dedicated
// cycle keys call. Delta 0 re-announces without moving, which is what makes a one-entry ring
// answerable without cycling through it.
//
// Selection survives a rebuild by KEY, not by index: entries appear and vanish as objectives
// complete, enemies die and the tutorial advances, so an index would silently point at a
// different thing. It is remembered PER CATEGORY, so alternating between two cycle keys does not
// reset either of them.
function ScanCategory(int Category, int Delta)
{
	local TTile Origin;

	NativeResult = "";

	if (Category < 0 || Category >= NUM_CATEGORIES)
	{
		NativeResult = "Bad category" @ string(Category);
		return;
	}

	if (!ScanOrigin(Origin))
	{
		NativeResult = "No scan origin";
		return;
	}

	// A dedicated key moves the ring cursor too, so PageUp/PageDown afterwards continue in the
	// category you last asked about rather than in some remembered other one.
	CurCategory = Category;

	NativeResult = AnnounceCategory(Category, Origin, Delta, false);
	if (NativeResult == "")
	{
		bSelValid = false;
		NativeResult = EmptyText(Category);
	}
}

// The same, in whichever category the ring cursor is on — PageDown / PageUp, and Delta 0 for the
// re-announce key.
function ScanCurrent(int Delta)
{
	ScanCategory(CurCategory, Delta);
}

// Shift+Tab: select the PREVIOUS soldier — vanilla's own meaning, re-provided because we now
// swallow LeftShift outright (object_scanner.cpp explains why). Key_Tab's prev-soldier branch
// reads Input.PressedKeys, which cannot contain a key the engine never saw pressed, so without
// this Shift+Tab would quietly become NEXT soldier.
//
// The guard is XComTacticalInput.IsManualUnitSwitchAllowed's, copied rule for rule (:1850): it is
// declared inside state ActiveUnit_Moving and so is unreachable by name from here, but both of its
// tests are plain script. The visualizer-busy flag is set the same way PrevUnit does it (:2165) —
// the controller reads it when the visualization finishes to decide whether to re-select.
//
// SILENT on purpose. The selected-unit reader (src/unit_info.cpp) POLLS the controller for
// selection changes, so it announces this one like every other — including the mouse click and
// the turn start that no key claim could see. Speaking here too would name the soldier twice.
//
// And note what this claim is NOT: it does not break "Tab stays the game's, in both of its
// meanings". Bare Tab is untouched and still the player's `eTBC_NextUnit`. Shift+Tab is taken
// only because WE took Shift, which is what removed the LeftShift entry from Input.PressedKeys
// that Key_Tab's prev-soldier branch reads — so this restores the vanilla meaning rather than
// displacing it. Give Shift back and this claim goes with it.
function PrevSoldier()
{
	local XComTacticalController TC;
	local XComGameStateVisualizationMgr VisMgr;

	NativeResult = "";

	TC = class'X2AccessTileReader'.static.TacCtrl();
	if (TC == none)
		return;

	if (class'XComGameState_Cheats'.static.GetVisualizedCheatsObject().DisableUnitSwitching)
		return;

	VisMgr = `XCOMVISUALIZATIONMGR;
	if (VisMgr == none)
		return;
	if (VisMgr.VisualizerBlockingAbilityActivation(true))
		return;
	if (class'XComGameStateVisualizationMgr'.static.VisualizerBusy())
		TC.bManuallySwitchedUnitsWhileVisualizerBusy = true;

	// NativeResult stays empty whatever this returns — see the SILENT note above.
	TC.Visualizer_SelectPreviousUnit();
}

// Move the ring cursor to the next category that HAS something in it, and announce that something
// with the category named in front of it.
//
// Skipping empties is the whole point: eight slots exist and a typical mission populates three, so
// a ring that stopped on every empty one would spend most of its presses saying "no loot". The cost
// is one BuildCategory per skipped category — a handful of history iterations, on a keypress.
function StepCategory(int Delta)
{
	local TTile Origin;
	local string Line;
	local int Tries, Cat;

	NativeResult = "";

	if (!ScanOrigin(Origin))
	{
		NativeResult = "No scan origin";
		return;
	}

	Cat = CurCategory;
	for (Tries = 0; Tries < CAT_COUNT; Tries++)
	{
		Cat += Delta;
		if (Cat >= CAT_COUNT)
			Cat = 0;
		else if (Cat < 0)
			Cat = CAT_COUNT - 1;

		Line = AnnounceCategory(Cat, Origin, 0, true);
		if (Line != "")
		{
			CurCategory = Cat;
			NativeResult = Line;
			return;
		}
	}

	// Every ring is empty, the one we started in included — so the cursor stays put and says so
	// rather than silently doing nothing.
	bSelValid = false;
	NativeResult = "Nothing to scan";
}

// Select within a category and render the line, or "" if the category is empty. Shared by the
// dedicated keys and the ring so there is exactly one place that decides what a selection means.
private function string AnnounceCategory(int Category, TTile Origin, int Delta, bool bPrefixName)
{
	local array<AccessMarker> Entries;
	local string Line;
	local int Idx;

	BuildCategory(Category, Origin, Entries);
	if (Entries.Length == 0)
		return "";

	Idx = IndexOfKey(Entries, SelKeyByCategory[Category]);
	if (Idx == INDEX_NONE)
	{
		// Nothing selected yet, or what was selected is gone: enter the ring at the nearest
		// entry rather than wherever the old index happened to land.
		Idx = (Delta < 0) ? Entries.Length - 1 : 0;
	}
	else if (Delta != 0)
	{
		Idx = (Idx + Delta) % Entries.Length;
		if (Idx < 0)
			Idx += Entries.Length;
	}

	SelKeyByCategory[Category] = Entries[Idx].Key;
	SelX = Entries[Idx].Tile.X;
	SelY = Entries[Idx].Tile.Y;
	SelZ = Entries[Idx].Tile.Z;
	bSelValid = true;

	Line = FormatMarker(Entries[Idx], Origin, Idx + 1, Entries.Length);

	// Only the ring names the category. A dedicated key already told the player which one they
	// pressed, and repeating it every time would be a word in front of every reading.
	if (bPrefixName)
		Line = CategoryName(Category) $ "." @ Line;

	return Line;
}

// ---------------------------------------------------------------------------
// Building the ring
// ---------------------------------------------------------------------------

// Insertion-sorted by distance as each contributor adds, so every ring reads nearest-first without
// needing a sort delegate on a CDO. The lists are a handful of entries — this is not a hot path.
private function BuildCategory(int Category, TTile Origin, out array<AccessMarker> Entries)
{
	switch (Category)
	{
	case CAT_MARKERS:
		AddTutorialMarker(Origin, Entries);
		AddObjectiveMarkers(Origin, Entries);
		// The evac zone rides the MARKERS ring as well as its own, on exactly the Lost's
		// reasoning below: the markers key is where a player looks for "where do I have to
		// GO", and a destination that lives only in a ring you must STEP to is one you find
		// by accident or never. Confirmed live 2026-08-23 on LostAndAbandonedB, whose
		// "move the squad to the fallback route" stage IS an evac zone — AddEvac had been
		// reading it correctly all along and the answer simply never reached the player,
		// while the objective anchor beside it could only say "Objective: object".
		// Sharing is safe for the same reason AddLost's is: ONE entry, however big the zone.
		// The separate CAT_EVAC ring is GONE rather than kept beside this: it had no key of
		// its own, it is empty on most missions, and the Lost comment's rule applies whole —
		// a category empty on most missions costs a ring step on all of them.
		AddEvac(Origin, Entries);
		break;
	case CAT_ENEMIES:
		AddEnemies(Origin, Entries);
		// The Lost ride the ENEMY ring rather than one of their own. They are enemies, so the
		// enemy key is where a player looks for them, and a category that is empty on most
		// missions costs a ring step on all of them. Sharing is only safe because AddLost
		// contributes ONE entry — see there.
		AddLost(Origin, Entries);
		break;
	case CAT_SQUAD:
		AddSquad(Origin, Entries);
		break;
	case CAT_LOOT:
		AddLoot(Origin, Entries);
		break;
	case CAT_REINFORCEMENTS:
		AddReinforcements(Origin, Entries);
		break;
	case CAT_CIVILIANS:
		AddCivilians(Origin, Entries);
		break;
	case CAT_HAZARDS:
		AddHazards(Origin, Entries);
		break;
	case CAT_INTERACT:
		AddInteractables(Origin, Entries);
		break;
	case CAT_TOWERS:
		AddTowers(Origin, Entries);
		break;
	case CAT_CONNECTORS:
		AddConnectors(Origin, Entries);
		break;
	case CAT_COVER:
		AddCover(Origin, Entries);
		break;
	}
}

// What an empty category says. Worth spelling out per category rather than saying "nothing here":
// "No enemies visible" carries the FOG rule in the wording, so a player is not left wondering
// whether the map is clear or the scanner is broken.
private function string EmptyText(int Category)
{
	switch (Category)
	{
	case CAT_MARKERS: return "No markers";
	case CAT_ENEMIES: return "No enemies visible";
	case CAT_SQUAD: return "No squad";
	case CAT_LOOT: return "No loot";
	// Worth saying out loud rather than staying silent: "none pending" is the answer a player
	// checks for every turn, and silence would be indistinguishable from a dead key.
	case CAT_REINFORCEMENTS: return "No reinforcements incoming";
	case CAT_CIVILIANS: return "No civilians visible";
	case CAT_HAZARDS: return "No hazards";
	case CAT_INTERACT: return "Nothing to interact with";
	// Says WHY it is empty. "No towers" while revealed would read as "this map has none", which is
	// a different and much more dangerous claim than "the radius does not apply right now".
	case CAT_TOWERS: return "Towers shown only while concealed";
	// Names the SCOPE, not the map: the sweep is deliberately local, so "none" here means none
	// nearby rather than none at all, and saying so keeps the reading honest.
	case CAT_CONNECTORS: return "No ramps or ladders nearby";
	// Same rule, and it carries the FLOOR too: the sweep is one Z band around the origin, so a
	// player standing in an open yard below a walled roof must not hear "this map has no cover".
	case CAT_COVER: return "No cover nearby on this floor";
	}
	return "Nothing here";
}

// Spoken by the ring, in front of the entry it landed on.
private function string CategoryName(int Category)
{
	switch (Category)
	{
	case CAT_MARKERS: return "Markers";
	case CAT_ENEMIES: return "Enemies";
	case CAT_SQUAD: return "Squad";
	case CAT_LOOT: return "Loot";
	case CAT_REINFORCEMENTS: return "Reinforcements";
	case CAT_CIVILIANS: return "Civilians";
	case CAT_HAZARDS: return "Hazards";
	case CAT_INTERACT: return "Objects";
	case CAT_TOWERS: return "Towers";
	case CAT_CONNECTORS: return "Level connectors";
	case CAT_COVER: return "Cover";
	}
	return "Category" @ string(Category);
}

// Visible enemies. Units are NEVER fog-latched (docs/object-scanner-recon.md §3.1) — unlike the
// statics, an enemy that stepped out of sight leaves the ring entirely, exactly as its unit flag
// vanishes for a sighted player. Latching them would hand a blind player information the game
// deliberately takes away.
//
// bIsDestination is false throughout: an enemy tile is something to shoot, not somewhere to stand,
// and movement reachability spoken about it reads as "you cannot do this".
private function AddEnemies(TTile Origin, out array<AccessMarker> Entries)
{
	local XComGameStateHistory History;
	local XComGameState_Unit UnitState, ActiveUnit;
	local X2CharacterTemplate Template;
	local StateObjectReference ShooterRef;
	local array<StateObjectReference> MineToSee, ViaSquadsight;
	local AccessMarker M;

	History = `XCOMHISTORY;
	ActiveUnit = class'X2AccessTileReader'.static.ActiveUnitState();
	if (ActiveUnit != none)
	{
		ShooterRef = ActiveUnit.GetReference();

		// What the SELECTED soldier can see, and (if they are a sniper) what they reach through
		// squadsight — the two lists UITacticalHUD_Enemies builds for the enemy strip. The ring
		// itself stays on SQUAD visibility, which is what the unit FLAGS on the map show; these
		// two only decide which clause the entry gets.
		class'X2TacticalVisibilityHelpers'.static.GetAllVisibleEnemyTargetsForUnit(
			ActiveUnit.ObjectID, MineToSee);
		if (ActiveUnit.HasSquadsight())
			class'X2TacticalVisibilityHelpers'.static.GetAllSquadsightEnemiesForUnit(
				ActiveUnit.ObjectID, ViaSquadsight);
	}

	foreach History.IterateByClassType(class'XComGameState_Unit', UnitState)
	{
		if (UnitState.GetTeam() != eTeam_Alien || UnitState.IsDead())
			continue;
		// The squad ring's phantom in its mirror image: a GREMLIN takes its owner's team, so a
		// mind-controlled or hacked Specialist would put a nameless drone in the ENEMY ring the
		// same way. The tile reader already refuses cosmetics here (X2AccessTileReader.uc:536).
		Template = UnitState.GetMyTemplate();
		if (Template != none && Template.bIsCosmetic)
			continue;
		if (!class'X2TacticalVisibilityHelpers'.static.CanXComSquadSeeTarget(UnitState.ObjectID))
			continue;

		M.Key = UnitState.ObjectID;
		M.Tile = UnitState.TileLocation;
		M.bIsDestination = false;
		M.Label = UnitState.GetName(eNameType_Full);

		M.Detail = "";

		// Without this clause the first unshootable target reads as a bug in US rather than as a
		// rule of the game: the squad can see it, so it is in the ring, but the soldier holding
		// the gun cannot.
		class'X2AccessHelper'.static.AppendComma(M.Detail,
			SightWords(UnitState.ObjectID, ActiveUnit, MineToSee, ViaSquadsight));

		// The same words the shot readout uses, from the same functions — see
		// X2AccessTacticalReader.DescribeTargetCover.
		class'X2AccessHelper'.static.AppendComma(M.Detail,
			class'X2AccessTacticalReader'.static.DescribeTargetCover(UnitState, ShooterRef));
		class'X2AccessHelper'.static.AppendComma(M.Detail,
			class'X2AccessTacticalReader'.static.DescribeTargetHealth(UnitState));

		InsertByDistance(Origin, M, Entries);
	}
}

// "" when the selected soldier sees it themselves, which is the ordinary case and needs no words.
private function string SightWords(int TargetID, XComGameState_Unit ActiveUnit,
	out array<StateObjectReference> MineToSee, out array<StateObjectReference> ViaSquadsight)
{
	if (ActiveUnit == none)
		return "";
	if (MineToSee.Find('ObjectID', TargetID) != INDEX_NONE)
		return "";
	if (ViaSquadsight.Find('ObjectID', TargetID) != INDEX_NONE)
		return "via squadsight";
	return "spotted by the squad";
}

// Your own soldiers. Named, unlike the aliens: GetName(eNameType_Full) gives a soldier their real
// name, which is the thing that makes this ring navigable at all.
//
// No fog gate — they are yours. bIsDestination is false: a soldier's own tile is occupied, so
// "1 action" about it would answer a question nobody asked.
//
// Detail is held to what the unit FLAG shows above their head — status, cover, health, shield,
// armour. Action points are deliberately absent: the HUD shows those for the selected soldier and
// the tile cursor answers for a tile, so putting them here would be a third voice on one fact.
private function AddSquad(TTile Origin, out array<AccessMarker> Entries)
{
	local XComGameStateHistory History;
	local XComGameState_Unit UnitState, ActiveUnit;
	local X2CharacterTemplate Template;
	local StateObjectReference NoShooter;
	local AccessMarker M;
	local int ActiveID;

	History = `XCOMHISTORY;
	ActiveUnit = class'X2AccessTileReader'.static.ActiveUnitState();
	ActiveID = (ActiveUnit != none) ? ActiveUnit.ObjectID : -1;

	foreach History.IterateByClassType(class'XComGameState_Unit', UnitState)
	{
		// bRemovedFromPlay is what evacuating sets, so a soldier already on the Skyranger stops
		// being somewhere on the map.
		if (UnitState.GetTeam() != eTeam_XCom || UnitState.IsDead() || UnitState.bRemovedFromPlay)
			continue;

		// A Specialist's GREMLIN is a live eTeam_XCom unit parked on its owner's own tile, and
		// without this it entered the ring as a NAMELESS "1 of 1 health, 0 tiles" entry — a
		// seventh squad member on a six-soldier squad, with no counterpart a sighted player can
		// see. The parity source is the flag manager's own list of who gets a flag
		// (UIUnitFlagManager.CheckForMissingFlags: HP > 0, not cosmetic, not civilian, not
		// removed from play), which this loop otherwise already matches clause for clause; the
		// GREMLIN templates set bIsCosmetic AND bDisplayUIUnitFlag=false, so no flag is drawn
		// over it and nothing about it is on screen to read.
		//
		// Same guard the tile reader and the shot readout have carried all along
		// (X2AccessTileReader.uc:536, X2AccessTacticalReader.uc:580) — the squad ring was the one
		// place it was missing.
		Template = UnitState.GetMyTemplate();
		if (Template != none && Template.bIsCosmetic)
			continue;

		M.Key = UnitState.ObjectID;
		M.Tile = UnitState.TileLocation;
		M.bIsDestination = false;
		M.Label = UnitState.GetName(eNameType_Full);

		M.Detail = "";
		if (UnitState.ObjectID == ActiveID)
			class'X2AccessHelper'.static.AppendComma(M.Detail, "selected");
		class'X2AccessHelper'.static.AppendComma(M.Detail, StatusWords(UnitState));

		// An EMPTY shooter ref on purpose: IsFlanked with no flanker asks "flanked by anything
		// visible", which is exactly what the cover shield on a soldier's own flag reports.
		class'X2AccessHelper'.static.AppendComma(M.Detail,
			class'X2AccessTacticalReader'.static.DescribeTargetCover(UnitState, NoShooter));
		class'X2AccessHelper'.static.AppendComma(M.Detail,
			class'X2AccessTacticalReader'.static.DescribeTargetHealth(UnitState));

		InsertByDistance(Origin, M, Entries);
	}
}

// What is WRONG with this soldier — the question this key is pressed to answer, and the one it
// used to leave unanswered: a stunned, disoriented, burning or bound soldier read exactly like a
// healthy one, because the old version returned a single hand-written English word from a list of
// four and stopped at the first match. A turn that ends with half the squad dazed is a turn where
// the ring said nothing about any of it.
//
// Everything the FLAG shows as a status icon, now, in the game's OWN localized words. Two sources,
// because neither covers the set alone:
//
//   - the penalty half of the unit's own UI summary — disoriented, burning, poisoned, acid,
//     bound, mind controlled, marked, ruptured, unconscious, bleeding out, and any mod-added
//     effect that called SetDisplayInfo, each under its own FriendlyName. Same list, same filter
//     (bDisplayInUI && IsEffectCurrentlyRelevant) the soldier panel draws its red arrows from,
//     and the same composer X2AccessUnitReader uses for the `;` readout, so an effect is named
//     identically wherever we name it.
//   - stun and panic, which that list CANNOT carry — X2AccessHelper.UnlistedPenaltyWords holds
//     the why and the words, and holds them for the `;` sheet and the Targets stop too, so the
//     three can never disagree about the same soldier.
//
// Order is by what changes your next decision: dying first, then whether the soldier can act at
// all, then everything else. Bleeding out and unconscious are ALSO in the penalty list (both get
// SetDisplayInfo), so whichever leads is remembered and skipped there rather than said twice.
// Concealment is not a problem and comes last, after whatever is.
private function string StatusWords(XComGameState_Unit UnitState)
{
	local array<UISummary_UnitEffect> Penalties;
	local string Result, Lead;
	local int i;

	if (UnitState.IsBleedingOut())
		Lead = class'X2StatusEffects'.default.BleedingOutFriendlyName;
	else if (UnitState.IsUnconscious())
		Lead = class'X2StatusEffects'.default.UnconsciousFriendlyName;

	class'X2AccessHelper'.static.AppendComma(Result, Lead);
	class'X2AccessHelper'.static.AppendComma(Result,
		class'X2AccessHelper'.static.UnlistedPenaltyWords(UnitState));

	Penalties = UnitState.GetUISummary_UnitEffectsByCategory(ePerkBuff_Penalty);
	for (i = 0; i < Penalties.Length; i++)
	{
		// An effect with no FriendlyName is one that set bDisplayInUI without setting a word —
		// skipped rather than emitted, because a bare separator reads as a missing fact.
		if (Penalties[i].Name == "" || Penalties[i].Name == Lead)
			continue;
		class'X2AccessHelper'.static.AppendComma(Result, Penalties[i].Name);
	}

	if (UnitState.IsConcealed())
		class'X2AccessHelper'.static.AppendComma(Result,
			class'X2StatusEffects'.default.ConcealedFriendlyName);

	return Result;
}

// Timed loot drops.
//
// The filter is the loot marker's OWN visibility rule, copied from
// XComGameState_LootDrop.BuildVisualizationForLootTicked: SetVisible = (turns > 0 && has loot). So
// an entry exists here exactly when a waypoint is drawn on the ground there, and an emptied or
// expired drop leaves both at the same instant.
//
// Not fog-gated, for the same reason the objective markers are not: the marker is a UI_3D waypoint
// billboard, the same family the game keeps visible when everything else is hidden. It is moot in
// practice anyway — loot only ever appears where the squad just killed something.
private function AddLoot(TTile Origin, out array<AccessMarker> Entries)
{
	local XComGameStateHistory History;
	local XComGameState_LootDrop Drop;
	local AccessMarker M;

	History = `XCOMHISTORY;

	foreach History.IterateByClassType(class'XComGameState_LootDrop', Drop)
	{
		if (Drop.LootExpirationTurnsRemaining <= 0 || !Drop.HasAvailableLoot())
			continue;

		M.Key = Drop.ObjectID;
		M.Tile = Drop.GetLootLocation();
		M.bIsDestination = true;

		// LastLootOwnerName rather than GetLootingName(): the cached string was written from the
		// source at drop time, whereas GetLootingName re-enters the source object — and for an
		// interactive-object source that walks into the unguarded GetVisualizer() deref this class
		// already sidesteps in InteractiveLabel.
		M.Label = "Loot";
		if (Drop.LastLootOwnerName != "")
			M.Label $= ":" @ Drop.LastLootOwnerName;

		// Psi loot gets its own marker mesh on screen, so naming it here is parity, not a hint.
		M.Detail = "";
		if (Drop.HasPsiLoot())
			class'X2AccessHelper'.static.AppendComma(M.Detail, "psi loot");
		class'X2AccessHelper'.static.AppendComma(M.Detail,
			"expires" @ TurnWords(Drop.LootExpirationTurnsRemaining));

		InsertByDistance(Origin, M, Entries);
	}
}

// Pending reinforcement drops: where they land and how long you have.
//
// bIsDestination is false — this is the one place on the map you are trying NOT to be, and
// "1 action" would read as an invitation.
private function AddReinforcements(TTile Origin, out array<AccessMarker> Entries)
{
	local XComGameStateHistory History;
	local XComGameState_AIReinforcementSpawner Spawner;
	local AccessMarker M;
	local vector SpawnPos;

	History = `XCOMHISTORY;

	foreach History.IterateByClassType(class'XComGameState_AIReinforcementSpawner', Spawner)
	{
		// Countdown 0 means the group has already landed (or was told to spawn at once); the HUD
		// counter hides itself on exactly this test.
		if (Spawner.Countdown <= 0)
			continue;
		if (!HasSpawnFlare(Spawner.SpawnVisualizationType))
			continue;

		// Via a local: GetTileCoordinatesFromPosition takes its vector as `out`.
		SpawnPos = Spawner.SpawnInfo.SpawnLocation;

		M.Key = Spawner.ObjectID;
		M.Tile = `XWORLD.GetTileCoordinatesFromPosition(SpawnPos);
		M.bIsDestination = false;
		M.Label = ReinforcementsLabel();
		M.Detail = TurnWords(Spawner.Countdown);
		InsertByDistance(Origin, M, Entries);
	}
}

// Mirrors the guard in XComGameState_AIReinforcementSpawner.OnReinforcementSpawnerCreated. For
// these visualization types the game never runs BuildVisualizationForSpawnerCreation, so there is
// no flare, no psi gate and no revealed area on the ground — nothing for a sighted player to see,
// and so nothing for the ring to name. (The Lost swarm is the one that matters: it announces itself
// with noise and a banner, not with a mark on the map.)
private function bool HasSpawnFlare(name VisType)
{
	return VisType != ''
		&& VisType != 'TheLostSwarm'
		&& VisType != 'ChosenSpecialNoReveal'
		&& VisType != 'ChosenSpecialTopDownReveal';
}

// The game's OWN words for this counter, so the ring and the HUD agree in whatever language the
// player runs — "REINFORCEMENTS" / "INCOMING" in English. Falls back to English only if the
// localization is missing outright.
private function string ReinforcementsLabel()
{
	local string Title, Body;

	Title = class'UITacticalHUD_Countdown'.default.m_strReinforcementsTitle;
	Body = class'UITacticalHUD_Countdown'.default.m_strReinforcementsBody;

	if (Title == "")
		return "Reinforcements";
	if (Body == "")
		return Title;
	return Title @ Body;
}

// Always carries the number, because the number is precisely what the sighted counter shows.
private function string TurnWords(int Turns)
{
	return "in" @ string(Turns) @ (Turns == 1 ? "turn" : "turns");
}

// The tutorial's required-action marker.
//
// ⛔ Read the CONTEXT, never the mesh components. The six StaticMeshComponents on XComTutorialMgr
// are a presentation artifact and are routinely hidden at (0,0,0) while an action is still
// required — UseTurnBeginTargetPrompt is false in shipped GameCore, so markers exist only where a
// map author dropped a SeqAct_ShowTutorialTarget. NextPlayerAbilityContext is the authority.
//
// ⛔ PARITY GUARD: read only AbilityTemplateName, SourceObject, PrimaryTarget, TargetLocations and
// MovementPaths[0] — the fields the marker itself renders. The same context also carries
// ResultContext, the RECORDED hit/miss of a scripted shot; speaking that would leak the outcome of
// a shot the player has not taken yet.
//
// The replay gate is bIsTutorialAllowingControls rather than a CurrentHistoryFrame comparison: the
// tutorial is a replay, so the whole mission is already in the history, but NextPlayerAbilityContext
// is the manager's own live "what I am waiting for" var and is only meaningful while it has
// actually stopped and handed control over. Reading it mid-playback would describe the future.
private function AddTutorialMarker(TTile Origin, out array<AccessMarker> Markers)
{
	local XComTutorialMgr Tut;
	local XComGameStateContext_Ability Ctx;
	local X2AbilityTemplate Template;
	local XComGameState_Unit TargetUnit;
	local AccessMarker M;
	local string Verb;
	local vector TargetPos;
	local bool bHaveTile;

	Tut = TutorialMgr();
	if (Tut == none || !Tut.bIsTutorialAllowingControls)
		return;

	Ctx = Tut.NextPlayerAbilityContext;
	if (Ctx == none)
		return;

	Template = class'XComGameState_Ability'.static.GetMyTemplateManager()
		.FindAbilityTemplate(Ctx.InputContext.AbilityTemplateName);

	Verb = "";
	if (Template != none && Template.LocFriendlyName != "")
		Verb = Template.LocFriendlyName;
	else if (Ctx.InputContext.AbilityTemplateName != '')
		Verb = string(Ctx.InputContext.AbilityTemplateName);

	// Which marker the game would render follows the ability's targeting method — mirroring
	// XComTutorialMgr.SetMoveMarkers / SetTargetLocationMarkers exactly, movement first because a
	// move context carries a path regardless of what its targeting method says.
	bHaveTile = false;
	M.bIsDestination = true;
	if (Ctx.InputContext.MovementPaths.Length > 0
		&& Ctx.InputContext.MovementPaths[0].MovementTiles.Length > 0)
	{
		M.Tile = Ctx.InputContext.MovementPaths[0].MovementTiles[
			Ctx.InputContext.MovementPaths[0].MovementTiles.Length - 1];
		bHaveTile = true;
		if (Verb == "")
			Verb = "Move";
	}
	else if (Ctx.InputContext.PrimaryTarget.ObjectID > 0)
	{
		TargetUnit = XComGameState_Unit(
			`XCOMHISTORY.GetGameStateForObjectID(Ctx.InputContext.PrimaryTarget.ObjectID));
		if (TargetUnit != none)
		{
			M.Tile = TargetUnit.TileLocation;
			bHaveTile = true;
			// Something to shoot, not somewhere to stand.
			M.bIsDestination = false;
			Verb @= "-" @ TargetUnit.GetName(eNameType_Full);
		}
	}

	if (!bHaveTile && Ctx.InputContext.TargetLocations.Length > 0)
	{
		// Via a local: GetTileCoordinatesFromPosition takes its vector as `out`, and UnrealScript
		// refuses a dynamic-array element there.
		TargetPos = Ctx.InputContext.TargetLocations[0];
		M.Tile = `XWORLD.GetTileCoordinatesFromPosition(TargetPos);
		bHaveTile = true;
	}

	if (!bHaveTile)
		return;

	M.Key = KEY_TUTORIAL;
	M.Detail = "";
	M.Label = "Tutorial";
	if (Verb != "")
		M.Label $= ":" @ Verb;
	InsertByDistance(Origin, M, Markers);
}

// Objective-carrying objects. XComGameState_ObjectiveInfo is a COMPONENT — FindComponentObject
// with bSearchOwner walks from it back to the thing it is attached to, which is where the tile is.
//
// Deliberately NOT fog-gated. That is the game's own rule, not a shortcut: UIUnitFlag and
// UISimpleFlag both special-case an ObjectiveInfo component to keep the flag visible when
// everything else would hide it, so an objective marker is exactly the thing a sighted player can
// always see (see docs/object-scanner-recon.md §3.1).
//
// ⛔ ONE OBJECT CAN CARRY SEVERAL OF THESE, and this loop is the only thing in the mod that counts
// them. `SeqAct_MakeObjective.AddObjectiveInfo` creates a component and attaches it with NO check
// for one already there, so a mission that re-flags a thing when its stage advances stacks a
// second. Measured live 2026-09-04 on `Neutralize_Vehicle`: the carried VIP (unit 50206) held
// #50208 `AffectsLineOfPlay=true` (the class default — `CreateObjective_Pawn` at spawn) beside
// #51149 `AffectsLineOfPlay=false` (`SeqAct_MakeObjective`'s own defaultproperty — the Kismet
// re-flag when the objective became "evacuate him").
//
// The game never notices because NOTHING a player sees counts components: `Helpers.
// IsObjectiveTarget` is `FindComponentObject(...) != none`, the two flag classes gate on
// `GetGameStateComponentForObjectID(...) == none`, and `UIUnitFlagManager.AddFlag` carries its own
// «don't add duplicate units» loop over ObjectID. Only `SeqAct_GetObjectiveInfo` and
// `X2AutoPlayManager` iterate the components by class the way we do, and neither draws anything.
// So a sighted player sees ONE marker on that VIP, and the ring must too — which the shared
// one-key-one-entry guard in InsertByDistance delivers, the key here being the OWNER's ObjectID.
private function AddObjectiveMarkers(TTile Origin, out array<AccessMarker> Markers)
{
	local XComGameStateHistory History;
	local XComGameState_ObjectiveInfo Info;
	local XComGameState_Unit ObjUnit;
	local XComGameState_Destructible ObjDest;
	local XComGameState_InteractiveObject ObjInteract;
	local XComTacticalMissionManager MissionMgr;
	local string MissionType;
	local AccessMarker M;

	History = `XCOMHISTORY;
	MissionMgr = `TACTICALMISSIONMGR;
	MissionType = "";
	if (MissionMgr != none)
		MissionType = MissionMgr.ActiveMission.sType;

	foreach History.IterateByClassType(class'XComGameState_ObjectiveInfo', Info)
	{
		// An objective spawned for a different mission type is inert scenery here.
		if (Info.MissionType != "" && MissionType != "" && Info.MissionType != MissionType)
			continue;

		ObjUnit = XComGameState_Unit(Info.FindComponentObject(class'XComGameState_Unit', true));
		if (ObjUnit != none)
		{
			if (ObjUnit.IsDead())
				continue;
			M.Key = ObjUnit.ObjectID;
			M.Tile = ObjUnit.TileLocation;
			M.bIsDestination = true;
			M.Detail = "";
			M.Label = "Objective:" @ ObjUnit.GetName(eNameType_Full);
			InsertByDistance(Origin, M, Markers);
			continue;
		}

		// InteractiveObject extends Destructible, so ask for the more specific one first — it is
		// what carries "already used up" and the friendly name.
		ObjInteract = XComGameState_InteractiveObject(
			Info.FindComponentObject(class'XComGameState_InteractiveObject', true));
		if (ObjInteract != none)
		{
			// bRemovedFromPlay is not the whole story — see InteractiveGone.
			if (ObjInteract.bRemovedFromPlay || InteractiveGone(ObjInteract))
				continue;
			M.Key = ObjInteract.ObjectID;
			M.Tile = ObjInteract.TileLocation;
			M.bIsDestination = true;
			M.Detail = InteractiveMarkWords(ObjInteract);
			M.Label = "Objective:" @ InteractiveLabel(ObjInteract);
			InsertByDistance(Origin, M, Markers);
			continue;
		}

		ObjDest = XComGameState_Destructible(
			Info.FindComponentObject(class'XComGameState_Destructible', true));
		if (ObjDest != none)
		{
			M.Key = ObjDest.ObjectID;
			M.Tile = ObjDest.TileLocation;
			M.bIsDestination = true;
			M.Detail = "";
			M.Label = "Objective";
			InsertByDistance(Origin, M, Markers);
		}
	}
}

// A used-up objective object whose visualizer has LEFT THE SCREEN while its state stayed in play.
//
// Supply Extraction earned this (2026-09-16). Its Kismet (Obj_SupplyExtraction, read out of the
// cooked package) calls SeqAct_RemoveInteractiveObjectFromLevel only on the ADVENT-transport path
// and at mission end; a crate the Skyranger picks up is flown off by a matinee
// (XCOMCrateRecover01-06) and its XComGameState_InteractiveObject stays exactly as marking left
// it — InteractionCount 1, not removed, ObjectiveInfo still attached. So the STATE cannot tell
// "marked, waiting for pickup" from "picked up", and the ring kept saying 12 crates after the
// message that Firebrand had taken one. A sighted player can tell, because the crate is gone —
// so read what they read: the actor is hidden, or no longer standing on its tile.
//
// Gated on InteractionCount because the fog is not our business here: an objective marker is
// deliberately not fog-gated (see AddObjectiveMarkers), and this test must never learn to drop an
// untouched crate for whatever the engine does to an unseen actor. A crate has to be marked
// before anything can take it, so the gate costs nothing on the case it exists for.
private static function bool InteractiveGone(XComGameState_InteractiveObject Obj)
{
	local XComInteractiveLevelActor VisActor;
	local vector TileCenter;

	if (Obj.InteractionCount <= 0)
		return false;

	// Measured live 2026-09-16, three crates after three pickups: the pickup does not hide the
	// actor, it takes the VISUALIZER AWAY — GetVisualizer answers none for a state that is
	// still in play, which is why those three entries had fallen through InteractiveLabel to
	// "Objective: object". A used object once had an actor (something was interacted with), so
	// no actor now is the clearest "gone" there is. The InteractionCount gate above is what
	// keeps this from reading a not-yet-spawned crate the same way.
	VisActor = XComInteractiveLevelActor(`XCOMHISTORY.GetVisualizer(Obj.ObjectID));
	if (VisActor == none)
		return true;
	if (VisActor.bHidden)
		return true;
	if (VisActor.SkeletalMeshComponent != none && VisActor.SkeletalMeshComponent.HiddenGame)
		return true;

	// Two tiles of slack: an interaction animation can shift a prop within its footprint, a
	// matinee carries it hundreds of units away.
	TileCenter = `XWORLD.GetPositionFromTileCoordinates(Obj.TileLocation);
	return VSize(VisActor.Location - TileCenter) > 2 * class'XComWorldData'.const.WORLD_StepSize;
}

// "marked" / "marked by ADVENT" for a Supply Extraction crate, read off the crate's own COLOUR.
//
// Marking is what recolours the crate: the mission's Kismet runs SeqAct_SetMaterialGameState with
// SupplyExtractionCrate_XCOM or SupplyExtractionCrate_ADVENT (a _Premium_ variant of each), and
// that material on the mesh is exactly how a sighted player tells an untouched crate from one
// side's claim — ADVENT marks crates too, and can only be beaten to them. The swap component's
// list is private to its class, so the mesh is asked instead, which is the thing on screen
// anyway. The material is applied by the visualization, so it appears when the sighted player
// sees it appear, not a frame before.
//
// The fallback for a marking that changed no material names the ability the row would have
// offered — "marked" only where the interaction WAS Interact_MarkSupplyCrate, "used" otherwise —
// so a hacked objective workstation never claims to have been marked for pickup.
private static function string InteractiveMarkWords(XComGameState_InteractiveObject Obj)
{
	local XComInteractiveLevelActor VisActor;
	local MaterialInterface Mat;
	local string MatName;
	local int i;

	VisActor = XComInteractiveLevelActor(`XCOMHISTORY.GetVisualizer(Obj.ObjectID));
	if (VisActor != none && VisActor.SkeletalMeshComponent != none)
	{
		for (i = 0; i < VisActor.SkeletalMeshComponent.GetNumElements(); i++)
		{
			Mat = VisActor.SkeletalMeshComponent.GetMaterial(i);
			if (Mat == none)
				continue;
			MatName = string(Mat.Name);
			if (InStr(MatName, "_ADVENT") != INDEX_NONE)
				return "marked by ADVENT";
			if (InStr(MatName, "_XCOM") != INDEX_NONE)
				return "marked";
		}
	}

	if (Obj.InteractionCount <= 0)
		return "";
	if (VisActor != none && VisActor.InteractionAbilityTemplateName == 'Interact_MarkSupplyCrate')
		return "marked";
	return "used";
}

// The evac zone, as a RECTANGLE rather than a point.
//
// Three things follow from that and none of them work with a centre tile. The tile spoken is the
// nearest point INSIDE the zone (the origin clamped into the bounds), so the distance is the walk
// that actually matters, the bearing points at the edge you would enter by, and `/` plants the
// cursor somewhere you can stand instead of in the middle. The size is spoken because a sighted
// player can see how big the green patch is; the count inside is spoken because the game answers
// it with its own `IsUnitInEvacZone`, and "how many are in" is the question the Evac button exists
// to answer.
private function AddEvac(TTile Origin, out array<AccessMarker> Entries)
{
	local XComGameStateHistory History;
	local XComGameState_EvacZone Evac;
	local XComGameState_Unit UnitState;
	local AccessMarker M;
	local TTile MinT, MaxT;
	local int Inside, Total;

	Evac = class'XComGameState_EvacZone'.static.GetEvacZone(eTeam_XCom);
	if (Evac == none)
		return;

	class'XComGameState_EvacZone'.static.GetEvacMinMax2D(Evac.CenterLocation, MinT, MaxT);

	M.Tile = Evac.CenterLocation;
	M.Tile.X = Clamp(Origin.X, MinT.X, MaxT.X);
	M.Tile.Y = Clamp(Origin.Y, MinT.Y, MaxT.Y);

	History = `XCOMHISTORY;
	foreach History.IterateByClassType(class'XComGameState_Unit', UnitState)
	{
		if (UnitState.GetTeam() != eTeam_XCom || UnitState.IsDead() || UnitState.bRemovedFromPlay)
			continue;
		Total++;
		if (Evac.IsUnitInEvacZone(UnitState))
			Inside++;
	}

	M.Key = Evac.ObjectID;
	M.bIsDestination = true;
	M.Label = "Evac zone";
	M.Detail = string(MaxT.X - MinT.X + 1) @ "by" @ string(MaxT.Y - MinT.Y + 1) @ "tiles";
	if (Total > 0)
		class'X2AccessHelper'.static.AppendComma(M.Detail,
			string(Inside) @ "of" @ string(Total) @ "inside");

	InsertByDistance(Origin, M, Entries);
}

// Civilians and VIPs — and, deliberately, the bodies among them. The recon doc had `units.down` as
// a category of its own; the half of it that is not already covered is exactly this, because a
// downed SOLDIER already reads "unconscious" in the squad ring. So a carryable body belongs here
// rather than in a third place that would name the same person a third time.
//
// bIsDestination is true throughout: whether you are escorting, rescuing or picking one up, the
// answer is somewhere you walk to.
private function AddCivilians(TTile Origin, out array<AccessMarker> Entries)
{
	local XComGameStateHistory History;
	local XComGameState_Unit UnitState;
	local AccessMarker M;

	History = `XCOMHISTORY;

	foreach History.IterateByClassType(class'XComGameState_Unit', UnitState)
	{
		if (!UnitState.IsCivilian() || UnitState.IsDead() || UnitState.bRemovedFromPlay)
			continue;
		if (!class'X2TacticalVisibilityHelpers'.static.CanXComSquadSeeTarget(UnitState.ObjectID))
			continue;

		M.Key = UnitState.ObjectID;
		M.Tile = UnitState.TileLocation;
		M.bIsDestination = true;
		M.Label = UnitState.GetName(eNameType_Full);

		M.Detail = "";
		class'X2AccessHelper'.static.AppendComma(M.Detail, StatusWords(UnitState));
		if (UnitState.CanBeCarried() && (UnitState.IsUnconscious() || UnitState.IsBleedingOut()))
			class'X2AccessHelper'.static.AppendComma(M.Detail, "can be carried");

		InsertByDistance(Origin, M, Entries);
	}
}

// The Lost, as ONE entry however many there are. Contributed to CAT_ENEMIES, not a ring of its own.
//
// This is §5.1's summary-on-entry form and the reason is arithmetic: a swarm is 10-20 bodies with
// one shared name, so a per-unit ring would be twenty identical lines and would bury the ADVENT pod
// standing behind them. The count and the nearest one are the two facts a player acts on.
private function AddLost(TTile Origin, out array<AccessMarker> Entries)
{
	local XComGameStateHistory History;
	local XComGameState_Unit UnitState;
	local AccessMarker M;
	local TTile NearT;
	local int Count, NearDist, D;

	History = `XCOMHISTORY;

	foreach History.IterateByClassType(class'XComGameState_Unit', UnitState)
	{
		if (UnitState.GetTeam() != eTeam_TheLost || UnitState.IsDead())
			continue;
		if (!class'X2TacticalVisibilityHelpers'.static.CanXComSquadSeeTarget(UnitState.ObjectID))
			continue;

		D = TileDistance(Origin, UnitState.TileLocation);
		if (Count == 0 || D < NearDist)
		{
			NearT = UnitState.TileLocation;
			NearDist = D;
		}
		Count++;
	}

	if (Count == 0)
		return;

	M.Key = KEY_LOST_SWARM;
	M.Tile = NearT;
	M.bIsDestination = false;
	M.Label = (Count == 1) ? "Lost" : "Lost swarm";
	M.Detail = (Count == 1) ? "" : string(Count) @ "visible";
	InsertByDistance(Origin, M, Entries);
}

// Hazards, one entry per TYPE rather than per state object.
//
// Grouping matters here: fire SPREADS, and each spread step writes another
// XComGameState_WorldEffectTileData, so a per-object ring would grow a new "Fire" entry every turn
// for what the player sees as one fire. Four types means at most four entries, ever.
private function AddHazards(TTile Origin, out array<AccessMarker> Entries)
{
	local int TypeIdx;

	for (TypeIdx = 0; TypeIdx < HAZARD_COUNT; TypeIdx++)
		AddHazardOfType(TypeIdx, Origin, Entries);
}

private function AddHazardOfType(int TypeIdx, TTile Origin, out array<AccessMarker> Entries)
{
	local XComGameStateHistory History;
	local XComGameState_WorldEffectTileData TileData;
	local XComWorldData WorldData;
	local AccessMarker M;
	local TTile MinT, MaxT, NearT, T;
	local name WantClass;
	local int NearDist, D, Span, i;
	local bool bAny;

	WantClass = HazardClass(TypeIdx);
	History = `XCOMHISTORY;
	WorldData = `XWORLD;

	foreach History.IterateByClassType(class'XComGameState_WorldEffectTileData', TileData)
	{
		if (TileData.WorldEffectClassName != WantClass)
			continue;

		// Indexed rather than `foreach`, deliberately: the element type
		// (WorldEffectTileDataContainer) is declared inside XComGameState_WorldEffectTileData and
		// is referenced nowhere else in the whole SDK, so whether its name is even in scope here is
		// untested ground. Reaching straight for the member needs no local of that type.
		for (i = 0; i < TileData.StoredTileData.Length; i++)
		{
			T = TileData.StoredTileData[i].Tile;

			// The state object is an INDEX of where this effect was applied; the world is the
			// truth. A blob that has burned out keeps its state object, so every tile is re-checked
			// against the same sparse array the tile cursor reads — which is also why the two
			// surfaces can never disagree about whether a tile is on fire.
			if (!TileHasHazard(WorldData, TypeIdx, T))
				continue;

			// Hazards are live world state, so they get the strict gate, not the latching one:
			// remembering where a fire USED to be would be inventing information.
			if (!TileSeen(WorldData, T, false))
				continue;

			D = TileDistance(Origin, T);
			if (!bAny)
			{
				MinT = T;
				MaxT = T;
				NearT = T;
				NearDist = D;
				bAny = true;
				continue;
			}

			if (T.X < MinT.X)
				MinT.X = T.X;
			if (T.Y < MinT.Y)
				MinT.Y = T.Y;
			if (T.X > MaxT.X)
				MaxT.X = T.X;
			if (T.Y > MaxT.Y)
				MaxT.Y = T.Y;
			if (D < NearDist)
			{
				NearT = T;
				NearDist = D;
			}
		}
	}

	if (!bAny)
		return;

	Span = Max(MaxT.X - MinT.X, MaxT.Y - MinT.Y) + 1;

	M.Key = KEY_HAZARD - TypeIdx;
	M.Tile = NearT;
	M.bIsDestination = false;
	M.Label = HazardWord(TypeIdx);
	M.Detail = (Span > 1) ? (string(Span) @ "tiles across") : "";
	InsertByDistance(Origin, M, Entries);
}

// Everything with a live interaction left in it — one ring, not the recon doc's six.
//
// The six-way split (doors / hackable / crates / interact / level change / destructibles) was
// written before §6.1 was answered. Now that InteractiveLabel names each object by its own
// interact ability — "Open Door", "Hack", "Use Elevator", already localized — the split buys
// nothing except four more Ctrl+PageDown presses to reach the chest.
//
// Objects carrying an ObjectiveInfo are skipped because `markers` already has them. That is the
// same "one thing, one ring" rule the evac promotion follows.
private function AddInteractables(TTile Origin, out array<AccessMarker> Entries)
{
	local XComGameStateHistory History;
	local XComGameState_InteractiveObject Obj;
	local XComWorldData WorldData;
	local AccessMarker M;

	History = `XCOMHISTORY;
	WorldData = `XWORLD;

	foreach History.IterateByClassType(class'XComGameState_InteractiveObject', Obj)
	{
		if (Obj.bRemovedFromPlay)
			continue;

		// Used up. Same test the object's own Interacted() guards on.
		if (Obj.MaxInteractionCount >= 0 && Obj.InteractionCount >= Obj.MaxInteractionCount)
			continue;

		if (Obj.FindComponentObject(class'XComGameState_ObjectiveInfo') != none)
			continue;

		// Statics LATCH: an object stays in the ring once seen, exactly as its silhouette stays
		// drawn for a sighted player after the squad looks away.
		if (!TileSeen(WorldData, Obj.TileLocation, true))
			continue;

		M.Key = Obj.ObjectID;
		M.Tile = Obj.TileLocation;
		M.bIsDestination = true;
		M.Label = InteractiveLabel(Obj);

		M.Detail = "";
		if (Obj.bHasBeenHacked)
			class'X2AccessHelper'.static.AppendComma(M.Detail, "hacked");
		else if (Obj.LockStrength > 0)
			class'X2AccessHelper'.static.AppendComma(M.Detail,
				"locked, strength" @ string(Obj.LockStrength));
		if (Obj.HasLoot())
			class'X2AccessHelper'.static.AppendComma(M.Detail, "has loot");

		InsertByDistance(Origin, M, Entries);
	}
}

// ADVENT towers, and ONLY while something of ours is concealed.
//
// DetectionRange is meaningless once the squad is revealed, so listing towers then would be noise
// dressed up as information — §5.3. The gate is "any XCom unit concealed" rather than the player
// state's bSquadIsConcealed, because a lone Phantom ranger scouting ahead of a revealed squad is
// precisely the situation where the radius matters most.
private function AddTowers(TTile Origin, out array<AccessMarker> Entries)
{
	local XComGameStateHistory History;
	local XComGameState_InteractiveObject Obj;
	local XComWorldData WorldData;
	local AccessMarker M;
	local int RangeTiles;

	if (!AnyoneConcealed())
		return;

	History = `XCOMHISTORY;
	WorldData = `XWORLD;

	foreach History.IterateByClassType(class'XComGameState_InteractiveObject', Obj)
	{
		if (Obj.bRemovedFromPlay || Obj.bHasBeenHacked || Obj.DetectionRange <= 0)
			continue;
		if (!TileSeen(WorldData, Obj.TileLocation, true))
			continue;

		M.Key = Obj.ObjectID;
		M.Tile = Obj.TileLocation;
		M.bIsDestination = true;
		M.Label = InteractiveLabel(Obj);

		// DetectionRange is in unreal units; a tile is WORLD_StepSize across.
		RangeTiles = int(Obj.DetectionRange / class'XComWorldData'.const.WORLD_StepSize);
		M.Detail = "detects within" @ string(RangeTiles)
			@ ((RangeTiles == 1) ? "tile" : "tiles");

		InsertByDistance(Origin, M, Entries);
	}
}

private function bool AnyoneConcealed()
{
	local XComGameStateHistory History;
	local XComGameState_Unit UnitState;

	History = `XCOMHISTORY;
	foreach History.IterateByClassType(class'XComGameState_Unit', UnitState)
	{
		if (UnitState.GetTeam() == eTeam_XCom && !UnitState.IsDead()
			&& !UnitState.bRemovedFromPlay && UnitState.IsConcealed())
			return true;
	}
	return false;
}

// LEVEL CONNECTORS — the ramps and ladders that get you between Z levels.
//
// Why this is a category at all: every other readout in the mod answers "how far and which way",
// and on a multi-storey map the honest answer often ends in "2 up" — which a blind player cannot
// act on without knowing where the stairs are. A sighted player reads that off the geometry in a
// glance. The idea is WrathAccess's (Pathfinder: WotR), where a whole watershed segmentation of
// the navmesh exists mostly to make every staircase its own room so it can be cycled as an exit;
// XCOM keeps the same fact in the tile grid and hands it over for the asking.
//
// Two sources, because the engine offers exactly two:
//   * LADDERS have a global list — CollectLadderLandingTiles, the same one XGAIPlayer_Civilian
//     uses to route panicking civilians. Both landings of one ladder are separate entries, and
//     that is right: "1 up" and "2 down" at the same bearing is the shape of a ladder.
//   * RAMPS have no list, only the per-tile IsRampTile predicate (TileDataIsRampTile, "a special
//     type of floor tile, so ramp tiles are Ramp AND Floor tiles"). So they are swept out of a
//     bounded box instead — which is also the right SCOPE: a ramp 40 tiles away is not the one
//     you would take, and the tile cursor already says "Ramp" when you stand on one, so the two
//     surfaces agree by construction.
//
// The sweep costs CONNECTOR_RADIUS^2 * 3 native predicate calls per keypress — ~1900, each an
// array lookup. That is the same "rebuild on every press" bargain the whole scanner makes (see
// NO REGISTRY above), and it is why the box is bounded to one Z level either side: a connector
// two floors away is reached by taking the one on this floor first.
const CONNECTOR_RADIUS = 12;   // tiles, in X and Y, swept around the scan origin for ramps
const CONNECTOR_LADDER_MAX = 30; // tiles; the ladder list is map-wide, so it needs a horizon
const CONNECTOR_CLUSTER = 3;   // tiles; nearer than this and it is the same staircase
const CONNECTOR_MAX = 12;      // entries; a ring longer than this is a list, not a cue

private function AddConnectors(TTile Origin, out array<AccessMarker> Entries)
{
	local XComWorldData WorldData;
	local array<AccessMarker> Raw;
	local array<TTile> Ladders;
	local TTile T;
	local int i, j, DX, DY, DZ;
	local bool bClustered;

	WorldData = `XWORLD;
	if (WorldData == none)
		return;

	WorldData.CollectLadderLandingTiles(Ladders);
	for (i = 0; i < Ladders.Length; i++)
	{
		if (TileDistance(Origin, Ladders[i]) > CONNECTOR_LADDER_MAX)
			continue;
		AddConnectorCandidate(WorldData, Origin, Ladders[i], "Ladder", Raw);
	}

	for (DZ = -1; DZ <= 1; DZ++)
	{
		T.Z = Origin.Z + DZ;
		if (T.Z < 0 || T.Z >= WorldData.NumZ)
			continue;
		for (DY = -CONNECTOR_RADIUS; DY <= CONNECTOR_RADIUS; DY++)
		{
			T.Y = Origin.Y + DY;
			if (T.Y < 0 || T.Y >= WorldData.NumY)
				continue;
			for (DX = -CONNECTOR_RADIUS; DX <= CONNECTOR_RADIUS; DX++)
			{
				T.X = Origin.X + DX;
				if (T.X < 0 || T.X >= WorldData.NumX)
					continue;
				if (!WorldData.IsRampTile(T))
					continue;
				AddConnectorCandidate(WorldData, Origin, T, "Ramp", Raw);
			}
		}
	}

	// Cluster LAST, over the already-distance-sorted candidates. A staircase is many ramp tiles
	// and a player wants one entry for it, not eight — and because Raw is nearest-first, the tile
	// that survives a cluster is always the nearest one, i.e. the end of the staircase you would
	// actually walk to. Doing this while collecting would keep whichever tile the sweep happened
	// to reach first instead.
	for (i = 0; i < Raw.Length && Entries.Length < CONNECTOR_MAX; i++)
	{
		bClustered = false;
		for (j = 0; j < Entries.Length; j++)
		{
			if (Entries[j].Label == Raw[i].Label
				&& TileDistance(Entries[j].Tile, Raw[i].Tile) <= CONNECTOR_CLUSTER)
			{
				bClustered = true;
				break;
			}
		}
		if (!bClustered)
			Entries.AddItem(Raw[i]);
	}
}

// One candidate connector tile, fog-gated and keyed by its position.
//
// The gate is the LATCHING one: a staircase is static layout, so it stays known once seen —
// exactly the rule X2AccessTileReader.DescribeTile applies to cover and floors, and the opposite
// of what hazards get. bIsDestination is true because a connector is somewhere you WALK to, so
// "out of range" is a fact about it worth having rather than the nonsense it would be about an
// enemy.
private function AddConnectorCandidate(XComWorldData WorldData, TTile Origin, TTile T,
	string Label, out array<AccessMarker> Raw)
{
	local AccessMarker M;

	if (!TileSeen(WorldData, T, true))
		return;

	// A linear tile index, so the key is unique per tile and stable across rebuilds however big
	// the map is — a packed decimal would collide on a grid wider than its digit budget.
	M.Key = KEY_CONNECTOR - ((T.Z * WorldData.NumY + T.Y) * WorldData.NumX + T.X);
	M.Tile = T;
	M.Label = Label;
	M.Detail = "";
	M.bIsDestination = true;
	InsertByDistance(Origin, M, Raw);
}


// ---------------------------------------------------------------------------
// Cover positions (the J cycle)
// ---------------------------------------------------------------------------
//
// The WH40KRTAccess CoverModel port. A cover POSITION is a tile you could stand on to be behind
// something — not the thing itself — named by the side(s) it covers ("Full cover north"), so `/`
// plants the cursor on the very tile you would occupy and Backspace walks there. That is the whole
// reason the answer is a tile and not an object: cover is directional level geometry, and "the
// crate is 4 tiles north-east" does not tell a blind player which side of it to stand on.
//
// ⚠ OUT-OF-RANGE COVER IS INCLUDED, deliberately. bIsDestination is true, so FormatMarker appends
// ReachWords and an unreachable spot reads "out of range" — a NOTE, not a filter. Planning a
// position you cannot reach this turn is the point of the feature, and it is the half RT's own
// docs/feedback triage asked for by name.
//
// Two things this does NOT do, both on purpose:
//   * It does not re-ask whether cover is knowable. RT gates the whole surface on the game's own
//     cover overlay being TOGGLED UP; XCOM's is always on, so there is no mode to mirror — but the
//     overlay is real (XComActionIconManager draws UI_3D.Cover.CoverFull / CoverHalf per covered
//     edge, which is where our two words come from), and the tile cursor already names any explored
//     tile's cover unconditionally. So the FOG LATCH is the entire parity gate, and it is the same
//     latch (`TileSeen(bLatch=true)`) the cursor applies to the same fact. A ring over cover
//     therefore leaks nothing the arrow keys could not already reach one tile at a time; it is
//     drawn 2 tiles out (CoverIconTileRange) and swept 12 here, which is a RANGE difference and
//     not a parity breach — a sighted player mouse-sweeps the same map in a second.
//   * It does not rank by anything but distance, and MUST NOT LEARN TO. RT's J changes gear to
//     FiringPositions when an enemy is selected ("where can I stand and still shoot THAT"); that is
//     REFUSED here, not deferred. No XCOM surface shows a player which tiles can shoot a given
//     enemy — per-tile shot info exists only for the tile the cursor is ON — and the only thing in
//     the game that sweeps and scores candidate tiles for a shot is XGAIBehavior, the enemy's
//     planner. See surfaces/tactical.md; the general test is under Sighted parity in CLAUDE.md.
//
// ⛔ The sweep is ONE Z BAND (±1, the connectors' own bound) and the empty text says so. XCOM
// ground is not flat — kerbs, slopes and ramps move Z under a plain cardinal step, which is why
// the cursor follows the ground (X2AccessTileReader.LandingTile) — so an exact-Z filter would drop most of the cover beside
// any slope. Widening it further is what the floor keys are for.
const COVER_RADIUS = 12;   // tiles, in X and Y, swept around the scan origin
const COVER_MAX = 16;      // entries; a ring longer than this is a list, not a cue

private function AddCover(TTile Origin, out array<AccessMarker> Entries)
{
	local XComWorldData WorldData;
	local XComGameState_Unit ActiveUnit;
	local array<XComCoverPoint> Points;
	local array<int> Sig, CellZ, Stack, Members;
	local array<AccessMarker> Raw;
	local AccessMarker M;
	local TTile T, Best;
	local int Side, i, Idx, DX, DY, S, Start, Mine, Cell, Lowest, Linear, BestDist, Dist;

	WorldData = `XWORLD;
	if (WorldData == none)
		return;
	ActiveUnit = class'X2AccessTileReader'.static.ActiveUnitState();

	Side = COVER_RADIUS * 2 + 1;
	Sig.Length = Side * Side;      // growing a dynamic array zero-fills; 0 means "no cover cell"
	CellZ.Length = Side * Side;

	// The engine already keeps the cover points and will hand back the ones near a location, with
	// each one's TILE COORDINATES and cover flags — so unlike RT, which had to classify every cell
	// in a window by hand, there is no sweep to write. Radius is XY in world units and Height is Z
	// (XComTacticalCheatManager.TestCoverPoints passes 5 * 96 and 5 * 64 for five tiles of each),
	// so one WORLD_FloorHeight is the ±1 Z band, filtered exactly below.
	//
	// ⚠ bSortedByDist is asked for but NOTHING here depends on it: the grid is filled by tile
	// offset, each cluster picks its own nearest member, and the ring's order comes from
	// InsertByDistance. It buys exactly one thing — determinism in the column tie-break below,
	// where two cells of one column sit the same distance above and below the origin's floor.
	//
	// ⛔ BOTH EXTENTS ARE ASKED FOR WIDE AND NARROWED BY HAND, because the query's shape is not
	// this ring's shape and neither is measured. The radius is ROUND and the window is SQUARE, so
	// a bare 12-tile radius would answer for a cell 12 north and not for one 12 north-east — while
	// TileDistance, being Chebyshev, calls both "12 tiles". A ring that has data for some bearings
	// and not others at the same spoken distance is the truncation failure mode wearing a compass:
	// sqrt(2) is what puts the whole square inside the sphere. The height is doubled for the
	// cousin of that reason — whether the native reads it as a half-extent or a full one has never
	// been measured here, and asking for two floors costs nothing when the ±1 Z test below is the
	// authority either way. Both over-ask deliberately; the filters immediately after are the rule.
	WorldData.GetCoverPoints(WorldData.GetPositionFromTileCoordinates(Origin),
		COVER_RADIUS * class'XComWorldData'.const.WORLD_StepSize * 1.4143,
		2.0 * class'XComWorldData'.const.WORLD_FloorHeight,
		Points, true);

	// Pass 1: lay the surviving cover cells into a flat (2R+1)^2 grid keyed by their offset from
	// the origin, so pass 2 can flood-fill in O(cells) instead of rescanning the point list.
	for (i = 0; i < Points.Length; i++)
	{
		T.X = Points[i].X;
		T.Y = Points[i].Y;
		T.Z = Points[i].Z;

		DX = T.X - Origin.X;
		DY = T.Y - Origin.Y;
		if (DX < -COVER_RADIUS || DX > COVER_RADIUS || DY < -COVER_RADIUS || DY > COVER_RADIUS)
			continue;   // the radius is a sphere and the grid is a square; the corners fall out here
		if (Abs(T.Z - Origin.Z) > 1)
			continue;

		S = class'X2AccessTileReader'.static.CoverSignature(Points[i].Flags);
		if (S == 0)
			continue;

		Idx = (DY + COVER_RADIUS) * Side + (DX + COVER_RADIUS);
		// One cell per COLUMN. When both Z levels of a column carry cover, keep the one level
		// with the origin: a kerb one Z up must not hide the tile the player is standing on.
		if (Sig[Idx] != 0 && Abs(CellZ[Idx] - Origin.Z) <= Abs(T.Z - Origin.Z))
			continue;

		// Static layout, so the LATCHING fog rule — a wall stays known once seen, exactly as the
		// tile cursor treats the same tile's cover.
		if (!TileSeen(WorldData, T, true))
			continue;
		// The game's own "could a unit stand here" pair, from XGAIBehavior:3439.
		if (!WorldData.IsFloorTileAndValidDestination(T, ActiveUnit))
			continue;
		// Somebody is standing there — with the one exception every cover reader makes: you can
		// already BE in the best cover on the map, so the active soldier's own tile stays listable
		// (RT spells the same carve-out as `occupant != observer`).
		if (!WorldData.CanUnitsEnterTile(T)
			&& (ActiveUnit == none
				|| !class'X2AccessTileReader'.static.SameTile(T, ActiveUnit.TileLocation)))
			continue;

		Sig[Idx] = S;
		CellZ[Idx] = T.Z;
	}

	// Pass 2: flood-fill TOUCHING cells with an IDENTICAL signature into one spot. A ten-tile
	// sandbag line becomes one entry ("Full cover north, 10 tiles wide") while the four faces of
	// a car stay four — four faces are four different places to stand, and that is the choice the
	// player is actually making. Identical is the right join, not "compatible": the cell where a
	// run turns half into full, or a corner covered on two sides, is a genuinely different
	// position and earns its own entry.
	//
	// Sig doubles as the visited mask — a claimed cell is zeroed, and 0 never matches a live
	// signature. CellZ is left intact, so a member's Z is still readable after its cell is claimed.
	for (Start = 0; Start < Sig.Length; Start++)
	{
		Mine = Sig[Start];
		if (Mine == 0)
			continue;

		Members.Length = 0;
		Stack.Length = 0;
		Sig[Start] = 0;
		Stack.AddItem(Start);
		while (Stack.Length > 0)
		{
			Cell = Stack[Stack.Length - 1];
			Stack.Remove(Stack.Length - 1, 1);
			Members.AddItem(Cell);
			ClaimCoverCell(Sig, Side, (Cell % Side) + 1, Cell / Side, Mine, Stack);
			ClaimCoverCell(Sig, Side, (Cell % Side) - 1, Cell / Side, Mine, Stack);
			ClaimCoverCell(Sig, Side, Cell % Side, (Cell / Side) + 1, Mine, Stack);
			ClaimCoverCell(Sig, Side, Cell % Side, (Cell / Side) - 1, Mine, Stack);
		}

		// Two different tiles come out of one cluster and they answer different questions. The
		// KEY is the lowest linear tile index in it: canonical, origin-independent, and unique
		// because clusters are disjoint — so the remembered selection survives the window sliding
		// with the cursor. The spoken TILE is the member nearest the origin: the one you would
		// actually walk to, and the one `/` plants on.
		Lowest = MaxInt;
		BestDist = MaxInt;
		for (i = 0; i < Members.Length; i++)
		{
			Idx = Members[i];
			T.X = Origin.X + (Idx % Side) - COVER_RADIUS;
			T.Y = Origin.Y + (Idx / Side) - COVER_RADIUS;
			T.Z = CellZ[Idx];

			Linear = (T.Z * WorldData.NumY + T.Y) * WorldData.NumX + T.X;
			if (Linear < Lowest)
				Lowest = Linear;
			Dist = TileDistance(Origin, T);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Best = T;
			}
		}

		M.Key = KEY_COVER - Lowest;
		M.Tile = Best;
		M.Label = class'X2AccessTileReader'.static.CoverEdgeWords(Mine);
		// How far the SAME cover runs — how far you can shuffle along it without changing what
		// you are behind. Silent at one cell, where "1 tile wide" is a word for nothing.
		M.Detail = (Members.Length > 1) ? (string(Members.Length) @ "tiles wide") : "";
		M.bIsDestination = true;
		InsertByDistance(Origin, M, Raw);
	}

	// Cap LAST, over the already-distance-sorted list, so the entries that survive are the nearest
	// ones rather than whichever the grid scan reached first — the same order-then-trim the
	// connector clustering uses, and for the same reason.
	for (i = 0; i < Raw.Length && i < COVER_MAX; i++)
		Entries.AddItem(Raw[i]);
}

// Claim a neighbouring cell into the current cluster when its signature matches EXACTLY.
private function ClaimCoverCell(out array<int> Sig, int Side, int X, int Y, int Mine,
	out array<int> Stack)
{
	local int Idx;

	if (X < 0 || Y < 0 || X >= Side || Y >= Side)
		return;
	Idx = Y * Side + X;
	if (Sig[Idx] != Mine)
		return;
	Sig[Idx] = 0;
	Stack.AddItem(Idx);
}


// ---------------------------------------------------------------------------
// Naming
// ---------------------------------------------------------------------------

// The interactive object's own words if it has any. GetLootingName() would be the game's answer
// but it dereferences GetVisualizer() unguarded and an object whose visualizer has not been
// created yet is a crash — so this does the same read with the null check the game omits.
// Static and public: the shot HUD names the same objects when you target one, and a door that is
// "Взломать дверь" to the scanner and "Hackable object" to the targeting readout is two mods
// talking about one world. Uses no instance state, so sharing it costs nothing.
static function string InteractiveLabel(XComGameState_InteractiveObject Obj)
{
	local XComInteractiveLevelActor VisActor;
	local X2AbilityTemplate InteractTemplate;

	VisActor = XComInteractiveLevelActor(`XCOMHISTORY.GetVisualizer(Obj.ObjectID));
	if (VisActor != none)
	{
		// Authored words win when a level designer supplied them. Measured live 2026-08-20:
		// often EMPTY (the tutorial's own objective read blank), so it cannot be the only source
		// — which answers docs/object-scanner-recon.md §6.1.
		if (VisActor.FriendlyDisplayName != "")
			return VisActor.FriendlyDisplayName;

		// Otherwise name it by what you DO with it, which is how a player thinks of it anyway
		// and is already localized: Interact_OpenDoor -> "Open Door", Interact_HackDoor, and so
		// on. Strictly better than a bare category word, and free of any new string of ours.
		if (VisActor.InteractionAbilityTemplateName != '')
		{
			InteractTemplate = class'XComGameState_Ability'.static.GetMyTemplateManager()
				.FindAbilityTemplate(VisActor.InteractionAbilityTemplateName);
			if (InteractTemplate != none && InteractTemplate.LocFriendlyName != "")
				return InteractTemplate.LocFriendlyName;
		}
	}

	if (Obj.IsDoor())
		return "door";
	if (Obj.MustBeHacked())
		return "hackable";
	return "object";
}

// ---------------------------------------------------------------------------
// The spoken line
// ---------------------------------------------------------------------------

// `<label>, <N> tiles, <compass offset>, <reach>, <i of n>` — the same shape and the same compass
// words the tile cursor uses, so the two surfaces sound like one mod and describe one world.
private function string FormatMarker(AccessMarker M, TTile Origin, int Position, int Total)
{
	local string Result;

	Result = M.Label;
	AppendComma(Result, M.Detail);
	AppendComma(Result, string(M.Dist) @ (M.Dist == 1 ? "tile" : "tiles"));
	AppendComma(Result, class'X2AccessTileReader'.static.OffsetWords(Origin, M.Tile));
	if (M.bIsDestination)
		AppendComma(Result, ReachWords(M.Tile));
	if (Total > 1)
		AppendComma(Result, string(Position) @ "of" @ string(Total));
	return Result;
}

// Can the selected soldier get there this turn? Same source as the tile cursor's answer — the
// active unit's own reachable-tiles cache — so a marker and the tile under it never disagree.
private function string ReachWords(TTile Tile)
{
	local XComGameState_Unit UnitState;
	local XGUnit Unit;
	local float Cost;

	UnitState = class'X2AccessTileReader'.static.ActiveUnitState();
	if (UnitState == none)
		return "";
	Unit = class'X2AccessTileReader'.static.ActiveUnitVisualizer(UnitState);
	if (Unit == none || Unit.m_kReachableTilesCache == none)
		return "";
	Cost = Unit.m_kReachableTilesCache.GetPathCostToTile(Tile);
	if (Cost < 0)
		return "out of range";
	if (Cost > Unit.GetMobility())
		return "dash";
	return "1 action";
}

// ---------------------------------------------------------------------------
// Shared plumbing
// ---------------------------------------------------------------------------

// Where distances and bearings are measured from. Same rule as RT's ScanFrom(): the planted tile
// cursor if there is one this mission, else the active soldier — so browsing the ring and stepping
// the cursor describe the same world from the same place.
private function bool ScanOrigin(out TTile Origin)
{
	local XComGameState_Unit UnitState;

	if (class'X2AccessTileReader'.default.bPlanted
		&& class'X2AccessTileReader'.default.PlantedMissionID == CurrentMissionID())
	{
		Origin.X = class'X2AccessTileReader'.default.CurX;
		Origin.Y = class'X2AccessTileReader'.default.CurY;
		Origin.Z = class'X2AccessTileReader'.default.CurZ;
		return true;
	}

	UnitState = class'X2AccessTileReader'.static.ActiveUnitState();
	if (UnitState == none)
		return false;
	Origin = UnitState.TileLocation;
	return true;
}

// The one fog gate in this class. bLatch is the STATIC rule — a wall, a door, a crate stays known
// once seen — and its absence is the LIVE rule, for anything that can move or burn out. The two
// match X2AccessTileReader.DescribeTile, which reads static layout at HaveSeen and units and
// hazards only at Seen.
private function bool TileSeen(XComWorldData WorldData, TTile Tile, bool bLatch)
{
	local FOWTileStatus FOW;
	local byte GameplayFlags;

	WorldData.GetTileFOWValue(Tile, FOW, GameplayFlags);

	if (bLatch)
		return FOW != eFOWTileStatus_NeverSeen;
	return FOW == eFOWTileStatus_Seen;
}

// The four hazard types, by index. Three tables that must stay in step; keeping them adjacent is
// the enforcement.
private function name HazardClass(int TypeIdx)
{
	switch (TypeIdx)
	{
	case 0: return 'X2Effect_ApplyFireToWorld';
	case 1: return 'X2Effect_ApplySmokeToWorld';
	case 2: return 'X2Effect_ApplyPoisonToWorld';
	case 3: return 'X2Effect_ApplyAcidToWorld';
	}
	return '';
}

private function string HazardWord(int TypeIdx)
{
	switch (TypeIdx)
	{
	case 0: return "Fire";
	case 1: return "Smoke";
	case 2: return "Poison";
	case 3: return "Acid";
	}
	return "Hazard";
}

private function bool TileHasHazard(XComWorldData WorldData, int TypeIdx, TTile Tile)
{
	switch (TypeIdx)
	{
	case 0: return WorldData.TileContainsFire(Tile);
	case 1: return WorldData.TileContainsSmoke(Tile);
	case 2: return WorldData.TileContainsPoison(Tile);
	case 3: return WorldData.TileContainsAcid(Tile);
	}
	return false;
}

private function int CurrentMissionID()
{
	local XComGameState_BattleData BattleData;

	BattleData = XComGameState_BattleData(
		`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_BattleData', true));
	return (BattleData != none) ? BattleData.m_iMissionID : -1;
}

private function XComTutorialMgr TutorialMgr()
{
	local XComTacticalGRI GRI;

	GRI = XComTacticalGRI(class'WorldInfo'.static.GetWorldInfo().GRI);
	if (GRI == none)
		return none;
	return XComTutorialMgr(GRI.ReplayMgr);
}

// Straight-line tile distance, max-axis in the plane plus the height difference. Deliberately NOT
// path length: a path per candidate would mean a pathfind per marker per keypress, and the number
// a player wants here is "how far away is it", not "how far must I walk".
private function int TileDistance(TTile A, TTile B)
{
	local int DX, DY;

	DX = Abs(A.X - B.X);
	DY = Abs(A.Y - B.Y);
	return Max(DX, DY) + Abs(A.Z - B.Z);
}

// ⛔ ONE KEY, ONE ENTRY — and this is the ring's NAVIGABILITY, not merely its tidiness. Selection
// is remembered by key and resolved with IndexOfKey, which answers with the FIRST match, so a ring
// holding one key twice cannot be stepped past it: landing on the second copy stores a key that
// resolves back to the first, and every further step recomputes that same index. Everything after
// the duplicate is then unreachable going forward, which reads to a blind player as "the ring ends
// here" — the truncation failure mode, indistinguishable from the truth.
//
// Measured live 2026-09-04 on `Neutralize_Vehicle`: the carried VIP owns TWO
// XComGameState_ObjectiveInfo components (state IDs 50208 and 51149, both OwningObjectId 50206),
// so AddObjectiveMarkers emitted him twice under one ObjectID. The markers ring read "Objective:
// David Watson" at 1 of 3, then 2 of 3 forever, and never reached 3 of 3 — the EVAC ZONE, which
// AddEvac had been building correctly the whole time. Same shape as the LostAndAbandonedB finding
// AddEvac's own comment records: the evac was read and the answer never reached the player.
//
// The guard lives HERE rather than in the contributor because every contributor already keys one
// entry per state object or per synthetic slot; the invariant they all assume simply had no place
// that enforced it. The FIRST insertion wins, so the nearest copy is the one kept.
private function InsertByDistance(TTile Origin, out AccessMarker M, out array<AccessMarker> Markers)
{
	local int i;

	if (IndexOfKey(Markers, M.Key) != INDEX_NONE)
		return;

	M.Dist = TileDistance(Origin, M.Tile);
	for (i = 0; i < Markers.Length; i++)
	{
		if (M.Dist < Markers[i].Dist)
		{
			Markers.Insert(i, 1);
			Markers[i] = M;
			return;
		}
	}
	Markers.AddItem(M);
}

private function int IndexOfKey(out array<AccessMarker> Markers, int Key)
{
	local int i;

	for (i = 0; i < Markers.Length; i++)
	{
		if (Markers[i].Key == Key)
			return i;
	}
	return INDEX_NONE;
}

// Call-site sugar over X2AccessHelper.AppendComma, which owns the ⛔ never-a-bare-separator rule.
// A marker's label is a localized, designer-authored string (FriendlyDisplayName,
// LocFriendlyName, "Objective:" @ name), so this join is exactly the one that rule is about.
private function AppendComma(out string Result, string Part)
{
	class'X2AccessHelper'.static.AppendComma(Result, Part);
}
