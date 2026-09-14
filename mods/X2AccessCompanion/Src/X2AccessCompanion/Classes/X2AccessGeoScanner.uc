// X2AccessGeoScanner
// The geoscape ring's game-state half — the model. C++ (src/geo_scanner.cpp) owns the keys and the
// speech; it calls these functions on this class's default object via ProcessEvent (game thread,
// from the tick drain) and reads NativeResult back through reflection right after the call
// returns. Nothing here runs on its own — no listeners, no timers, no polling. Sibling of
// X2AccessObjectScanner, deliberately built to the same shape so the two feel like one instrument.
//
// ⛔ WHY THIS EXISTS AT ALL. The mission TRAY at the bottom of the geoscape is the game's own list
// of what is actionable RIGHT NOW, and the tray recipe already reads it. But a region only enters
// that tray while it is actively scannable — XComGameState_WorldRegion.CanBeScanned() is
// `(!HaveMadeContact() && bCanScanForContact) || (ResistanceLevel == eResLevel_Contact &&
// bCanScanForOutpost)` — so the tray holds the two or three you could scan this minute and never
// "the seventeen regions". Everything about the REST of the world — which regions you have
// contact with, what each pays, where the continents are, which sites you have not travelled to —
// is drawn on the globe and reachable by no keyboard at all.
//
// ⚠ THIS IS AN AID, NOT PARITY, and the distinction decides its whole shape. It hands a blind
// player a navigable list of things a sighted player has to find by dragging the globe around, so
// by the project's stop-list rule it must NOT be a Tab stop — it lives on its own keys
// (PageUp/PageDown) beside the tray recipe, exactly as the tactical object scanner lives beside
// the tile cursor. And it must never say more than the globe shows: every region line is a mirror
// of UIStrategyMapItem_Region.GenerateTooltip, gate for gate, so a region we describe is one the
// player could have hovered.
//
// THE SORT ORIGIN IS THE GEOSCAPE'S CURSOR. This is the organising idea and it is not a
// convenience: the globe has no cursor for a mouse+keyboard player at all, because pin selection
// by proximity is per-frame and `ISCONTROLLERACTIVE-gated. A flat list of sites therefore strips
// the globe of PLACE — you learn what exists and never where. So the ring sorts by distance from a
// point, that point defaults to the Avenger, and Home moves it to whatever you just heard. Then
// "next nearest" means something and there is somewhere to stand.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessGeoScanner extends Object;

// One candidate. Local to a scan; rebuilt from the history on every keypress, never stored — the
// same NO REGISTRY reasoning as the tactical scanner: an ObjectID is a stable int over a versioned
// history, so only the SELECTION needs to persist.
struct AccessGeoEntry
{
	var int Key;          // the entity's ObjectID — stable identity across rebuilds
	var string Label;     // what gets spoken first, already localized
	var string Detail;    // the second clause: income, resistance state, days remaining
	var Vector2D Loc;     // normalized earth coords, 0..1, X wrapping at the antimeridian
	var int Miles;        // from the sort origin, via the game's own conversion
	var string Bearing;   // compass word, or "" when the entry IS the origin
};

// Categories. None has a dedicated key: unlike tactical — where markers, enemies and squad are
// pressed every turn and earn one each — the strategy layer is browsed rather than reacted to, so
// every category is reached through the ring and the key budget stays untouched.
const CAT_REGIONS = 0;
const CAT_MISSIONS = 1;
const CAT_SCANSITES = 2;
const CAT_FACILITIES = 3;
const CAT_FACTIONS = 4;
const CAT_CONTINENTS = 5;
const CAT_COUNT = 6;
const NUM_CATEGORIES = 8;   // headroom; a static array, so the slack costs nothing

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The selection, per category, so stepping through regions and then through missions loses your
// place in neither. Keyed by ObjectID rather than by index — the lists are re-sorted whenever the
// origin moves, so an index would silently point at a different place.
var int SelKeyByCategory[8];

// Which ring the un-prefixed keys address. It lives HERE rather than in native because the ring
// step has to know which categories are EMPTY, and only this side can answer that.
var int CurCategory;

// The last thing actually announced, in any category. Home makes THIS the sort origin and `/`
// takes the camera to it — both acting on what you just heard rather than on a notion of "current
// category", which is the same call the tactical scanner's `/` makes.
var int LastKey;
var float LastX, LastY;
var bool bLastValid;
// The LABEL as it was SPOKEN, not a fresh lookup — and the reason there is no LookupEntityName in
// this class. Naming the origin with the exact words the player just heard is what makes
// "Sorting from X" recognisably about the thing they were standing on; and there is no generic
// name accessor to fall back on anyway, since GetDisplayName is declared per-SUBCLASS in this
// hierarchy (WorldRegion and City have one, GeoscapeEntity and MissionSite do not).
var string LastLabel;

// The sort origin. bOriginIsAvenger is the default and the fallback; when it is false the origin
// is a fixed point in earth coords plus the ObjectID it came from, kept only so the announcement
// can name it and so a vanished origin can be detected.
var bool bOriginIsAvenger;
var int OriginKey;
var float OriginX, OriginY;
var string OriginName;

// ⚠ UNVERIFIED, AND THE ONE CONSTANT THAT CAN BE 180 DEGREES WRONG.
//
// Earth coordinates are normalized 0..1 (measured live: v2ViewLoc = {0.500, 0.518}) and X wraps at
// the antimeridian, but NOTHING in the shipped sources maps either axis to latitude or longitude —
// there is no lat/long conversion anywhere in XComEarth or XGGeoscape to read the sign off. So
// this is an inference, not a measurement: these coordinates drive screen placement directly
// (CAMLookAtEarth feeds EARTH.SetViewLocation, and UIStrategyMapItem positions pins from the same
// space), and UE3's UI space runs +Y DOWNWARD, which makes increasing Y southward.
//
// It is a single named constant precisely so a live measurement can flip it in one character —
// the tile cursor's compass shipped 180 degrees out once, and the fix there was likewise one
// calibration constant rather than scattered signs. Verify by reading two regions whose real-world
// north/south order is obvious (Europe against Southern Africa) and checking the sign of the
// difference.
const Y_IS_SOUTH = true;

// ---------------------------------------------------------------------------
// Entry points (called from native)
// ---------------------------------------------------------------------------

// Step the selection within one NAMED category and speak it. Delta 0 re-announces without moving,
// which is what makes a one-entry ring answerable without cycling through it.
function ScanCategory(int Category, int Delta)
{
	local Vector2D Origin;

	NativeResult = "";

	if (Category < 0 || Category >= CAT_COUNT)
	{
		NativeResult = "Bad category" @ string(Category);
		return;
	}

	if (!ResolveOrigin(Origin))
	{
		NativeResult = "No geoscape";
		return;
	}

	CurCategory = Category;
	NativeResult = AnnounceCategory(Category, Origin, Delta);
	if (NativeResult == "")
	{
		NativeResult = EmptyText(Category);
	}
}

// The ring keys: step within whatever category the cursor is on.
function ScanCurrent(int Delta)
{
	ScanCategory(CurCategory, Delta);
}

// Move the ring cursor to the next NON-EMPTY category and announce its name with the first entry.
// Skipping empties is what makes a category that is legitimately empty most of the campaign — the
// alien facilities before you find one — cost nothing to carry.
function StepCategory(int Delta)
{
	local Vector2D Origin;
	local int Cat, Tries;
	local string Line;

	NativeResult = "";

	if (!ResolveOrigin(Origin))
	{
		NativeResult = "No geoscape";
		return;
	}

	Cat = CurCategory;
	for (Tries = 0; Tries < CAT_COUNT; Tries++)
	{
		Cat = Cat + Delta;
		if (Cat >= CAT_COUNT)
		{
			Cat = 0;
		}
		else if (Cat < 0)
		{
			Cat = CAT_COUNT - 1;
		}

		Line = AnnounceCategory(Cat, Origin, 0);
		if (Line != "")
		{
			CurCategory = Cat;
			NativeResult = CategoryName(Cat) $ ". " $ Line;
			return;
		}
	}

	NativeResult = "Nothing on the map";
}

// Home: make whatever was last announced the point everything sorts from.
//
// It also takes the CAMERA there, and that is load-bearing rather than cosmetic: a pin that has
// never been on screen holds class defaults (UIStrategyMapItem.UpdateVisuals gates
// UpdateFromGeoscapeEntity on bOnScreen), so parking the camera on the area being studied is what
// makes those pins readable at all.
function SetOriginToSelection()
{
	NativeResult = "";

	if (!bLastValid)
	{
		NativeResult = "Nothing selected";
		return;
	}

	bOriginIsAvenger = false;
	OriginKey = LastKey;
	OriginX = LastX;
	OriginY = LastY;
	OriginName = LastLabel;

	LookAt(LastX, LastY);

	NativeResult = "Sorting from" @ OriginName;
}

// C: origin back to the Avenger, which is also the camera verb the game itself binds to the
// controller's right stick (UIStrategyMap.LookAtAvenger).
function ResetOrigin()
{
	local XComGameState_HeadquartersXCom XComHQ;

	bOriginIsAvenger = true;
	OriginKey = 0;
	OriginName = "";

	XComHQ = `XCOMHQ;
	if (XComHQ != none)
	{
		LookAt(XComHQ.Get2DLocation().X, XComHQ.Get2DLocation().Y);
	}

	NativeResult = "Sorting from the Avenger";
}

// `/`: take the camera to the last announced entry WITHOUT moving the sort origin. The two are
// deliberately separate verbs — looking at something is not the same as deciding to measure
// everything from it.
function LookAtSelection()
{
	NativeResult = "";

	if (!bLastValid)
	{
		NativeResult = "Nothing selected";
		return;
	}

	LookAt(LastX, LastY);
	NativeResult = LastLabel;
}

// ---------------------------------------------------------------------------
// Region actions — the one PARITY surface in this file
// ---------------------------------------------------------------------------

// ⛔ EVERYTHING ELSE HERE IS AN AID. THIS IS NOT, AND THAT IS WHAT EARNS IT A TAB STOP.
//
// UIStrategyMapItem_Region draws two hotlink buttons on a region pin, and between them they ARE
// the resistance economy: ContactButton ('contactButtonMC', captioned m_strButtonMakeContact) and
// OutpostButton ('towerButtonMC' — captioned by NOTHING, InitButton is called with no label at
// all and the radio-tower art is the whole caption). A mouse+keyboard player can reach neither.
// The pin's own OnUnrealCommand answers FXS_BUTTON_A and nothing else (UIStrategyMapItem_Region
// :657), the screen's Enter/Space go to ToggleScan (UIStrategyMap:1663), and the screen-level pair
// that would have fixed it is commented out (UIStrategyMap.uc:112-116). The resistance screen
// those buttons were once meant to open is dead code too — UIResistance is never pushed, its only
// call site commented out (XComGameState_WorldRegion.uc:1313).
//
// So: the ant-farm shape once more. The game has the verb, has drawn the button, and wired it to
// no key. The tray never covers this — a region enters the tray only once it is ALREADY
// scannable, i.e. only after the button has been pressed and paid for.
//
// ⛔ The two verbs are one surface, not two. They are the same button family, mutually exclusive
// by resistance level (contact at eResLevel_Unlocked, relay at eResLevel_Contact), and they are
// two halves of one loop: contact a region, then install a relay in it. Splitting them into
// separate stops would have made the second half look like a different feature.
const VERB_CONTACT = 0;
const VERB_RELAY = 1;

// The wire format: records joined by REC_SEP, fields within a record by FLD_SEP, each field
// key=value. PRINTABLE separators, matching every other reader's.
const REC_SEP = "~|~";
const FLD_SEP = "~:~";

// One record per region whose pin is CURRENTLY drawing one of the two buttons:
//
//   kind=action  id  verb=contact|relay  region  label  income
//                costlabel  cost  timelabel  time  afford=0|1  comms=0|1
//
// Every WORD is the game's own — the button caption, the alert's own cost and time labels, and
// the "+N supplies" figure UpdateFlyoverText puts beside the button as HoverInfo. The two FLAGS
// are facts a sighted player reads off a colour or off a sentence inside the alert behind the
// button (the alert reddens a cost it cannot pay and appends m_strContactNeedResComms), so they
// cross as flags and native turns them into words.
function DescribeRegionActions()
{
	local XComGameState_WorldRegion Region;
	local XComGameState_HeadquartersXCom XComHQ;

	NativeResult = "";

	XComHQ = `XCOMHQ;
	if (XComHQ == none)
	{
		return;
	}

	foreach `XCOMHISTORY.IterateByClassType(class'XComGameState_WorldRegion', Region)
	{
		// The same fog-parity predicate every category here uses, and it is NOT redundant with
		// the two gates below: ShouldBeVisible opens with `if(!ResistanceActive()) return false`,
		// a single global flag (XComGameState_HeadquartersResistance.bInactive) that hides EVERY
		// region pin at once. With the network switched off there is no button on screen to offer.
		if (!Region.ShouldBeVisible())
		{
			continue;
		}

		if (CanOfferRelay(Region, XComHQ))
		{
			EmitAction(Region, XComHQ, VERB_RELAY);
		}
		else if (CanOfferContact(Region, XComHQ))
		{
			EmitAction(Region, XComHQ, VERB_CONTACT);
		}
	}
}

// Fire the verb. ⛔ THE GATE IS RE-CHECKED HERE and that is not belt-and-braces: the geoscape
// clock is the one screen in the game that keeps RUNNING under our stop, so a contact scan can
// complete between the render that spoke a line and the Enter that acts on it — moving the region
// from Unlocked to Contact and turning "make contact" into "install a relay" under the player's
// finger. Identity plus verb, both re-tested, is what makes that a silent no-op instead.
//
// ⚠ Note what this does NOT touch: SelectedMapItem. Both entry points take the region as a
// PARAMETER (UIStrategyMapItem_Region.OnContactClicked/OnOutpostClicked are one line each and
// pass GetRegion()), so this whole surface sidesteps the screen-accept trap that cost the tray's
// Enter two sessions — there is no selection for our idea of the target to disagree with.
function ActivateRegionAction(int RegionID, int Verb)
{
	local XComGameState_WorldRegion Region;
	local XComGameState_HeadquartersXCom XComHQ;

	NativeResult = "";

	XComHQ = `XCOMHQ;
	Region = XComGameState_WorldRegion(`XCOMHISTORY.GetGameStateForObjectID(RegionID));
	if (Region == none || XComHQ == none || `HQPRES == none)
	{
		return;
	}

	// The listing's own visibility gate, re-applied: acting on a pin the globe has stopped
	// drawing is the same mistake as listing one.
	if (!Region.ShouldBeVisible())
	{
		return;
	}

	// The game's own handlers, one level up from the button — OnOutpostClicked IS
	// `HQPRES.UIBuildOutpost(GetRegion()) and nothing else. Everything downstream stays the
	// game's: the alert, its affordability check, the payment, the auto-flight.
	if (Verb == VERB_RELAY && CanOfferRelay(Region, XComHQ))
	{
		`HQPRES.UIBuildOutpost(Region);
	}
	else if (Verb == VERB_CONTACT && CanOfferContact(Region, XComHQ))
	{
		`HQPRES.UIMakeContact(Region);
	}
}

// UIStrategyMapItem_Region.ShowContactButton() / ShowOutpostButton(), mirrored gate for gate.
// ⛔ Nothing is added to them — not affordability, not comms capacity, not "is the pin on screen".
// The game shows both buttons regardless of whether you can pay, and says why only inside the
// alert; refusing the stop here would hand a blind player LESS than the globe shows and hide the
// explanation with it.
private function bool CanOfferContact(XComGameState_WorldRegion Region,
	XComGameState_HeadquartersXCom XComHQ)
{
	return XComHQ.IsContactResearched()
		&& Region.ResistanceLevel == eResLevel_Unlocked
		&& !Region.bCanScanForContact;
}

private function bool CanOfferRelay(XComGameState_WorldRegion Region,
	XComGameState_HeadquartersXCom XComHQ)
{
	return XComHQ.IsOutpostResearched()
		&& Region.ResistanceLevel == eResLevel_Contact
		&& !Region.bCanScanForOutpost;
}

private function EmitAction(XComGameState_WorldRegion Region,
	XComGameState_HeadquartersXCom XComHQ, int Verb)
{
	local string Rec;
	local StrategyCost Cost;
	local array<StrategyCostScalar> Scalars;
	local int MinDays, MaxDays, Income;
	local bool bRelay;

	bRelay = (Verb == VERB_RELAY);

	if (bRelay)
	{
		Cost = Region.CalcOutpostCost();
		Scalars = Region.OutpostCostScalars;
		Region.BuildHavenETA(MinDays, MaxDays);
		// GetSupplyDropReward's two flags are POSITIONAL — "as if contacted" then "as if
		// relayed" — and the pin asks it once per button for exactly this figure
		// (PotentialSuppliesWithContact / PotentialSuppliesWithOutpost).
		Income = Region.GetSupplyDropReward(, true);
	}
	else
	{
		Cost = Region.CalcContactCost();
		Scalars = Region.ContactCostScalars;
		Region.MakeContactETA(MinDays, MaxDays);
		Income = Region.GetSupplyDropReward(true);
	}

	Rec = "kind=action";
	Rec $= FLD_SEP $ "id=" $ string(Region.ObjectID);
	Rec $= FLD_SEP $ "verb=" $ (bRelay ? "relay" : "contact");
	Rec $= FLD_SEP $ "region=" $ Region.GetDisplayName();

	// The caption. The relay button has none of its own, so the words come from the alert that
	// button opens — which is the next thing the player hears either way, so the verb they choose
	// and the verb they confirm are worded identically.
	if (bRelay)
	{
		Rec $= FLD_SEP $ "label=" $ class'UIAlert'.default.m_strOutpostConfirm;
		Rec $= FLD_SEP $ "timelabel=" $ class'UIAlert'.default.m_strOutpostTimeLabel;
	}
	else
	{
		Rec $= FLD_SEP $ "label=" $ class'UIStrategyMapItem_Region'.default.m_strButtonMakeContact;
		Rec $= FLD_SEP $ "timelabel=" $ class'UIAlert'.default.m_strContactTimeLabel;
	}

	Rec $= FLD_SEP $ "income=" $ "+"
		$ class'UIUtilities_Strategy'.default.m_strCreditsPrefix $ string(Income);

	// m_strContactCostLabel for both: the alert reuses it for the relay too, and says so in a
	// comment of its own ("reusing string here", UIAlert.BuildOutpostAlert).
	Rec $= FLD_SEP $ "costlabel=" $ class'UIAlert'.default.m_strContactCostLabel;
	Rec $= FLD_SEP $ "cost="
		$ class'UIUtilities_Strategy'.static.GetStrategyCostString(Cost, Scalars);
	Rec $= FLD_SEP $ "time=" $ ETAWords(MinDays, MaxDays, !bRelay);
	Rec $= FLD_SEP $ "afford=" $ (XComHQ.CanAffordAllStrategyCosts(Cost, Scalars) ? "1" : "0");

	// Comms capacity gates CONTACT only, and it is the reason a contact button most often
	// refuses: UIAlert.CanMakeContact() is `GetRemainingContactCapacity() > 0` and nothing else.
	if (!bRelay)
	{
		Rec $= FLD_SEP $ "comms=" $ (XComHQ.GetRemainingContactCapacity() > 0 ? "1" : "0");
	}

	Emit(Rec);
}

// UIAlert.GetOutpostTimeString / GetContactTimeString, with the day WORD taken from
// UIUtilities_Text.GetDaysString rather than the alert's own m_strDays.
// ⚠ Deliberate, and it is the localized-var trap from CLAUDE.md: m_strDays is declared on
// UIX2SimpleScreen and the loc file populates it under [UIX2SimpleScreen], NOT under [UIAlert] —
// so `class'UIAlert'.default.m_strDays` is a value this class has no business assuming. The
// utility pair ([UIUtilities_Text] m_strDay / m_strDays) is populated where it is declared, and
// it also gets the singular right, which the alert's own format does not.
private function string ETAWords(int MinDays, int MaxDays, bool bContact)
{
	local XComGameState_HeadquartersXCom XComHQ;

	if (bContact)
	{
		XComHQ = `XCOMHQ;
		if (XComHQ != none && XComHQ.bInstantContacts)
		{
			return XComHQ.strETAInstant;
		}
	}

	if (MinDays == MaxDays)
	{
		return string(MinDays) @ class'UIUtilities_Text'.static.GetDaysString(MinDays);
	}
	return string(MinDays) @ "-" @ string(MaxDays)
		@ class'UIUtilities_Text'.static.GetDaysString(MaxDays);
}

private function Emit(string Rec)
{
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

// ---------------------------------------------------------------------------
// The Avatar meter — what UIStrategyMap_HUD does not retain (parity, a read-only stop)
// ---------------------------------------------------------------------------

// kind=doom  shown=0|1  lose=0|1  current=N  max=M
//
// The HUD panel keeps the pip count it last drew (CachedDoom, read natively) and nothing else. The
// MAXIMUM is the loop bound of the Flash push and lives only in GetMaxDoom() — which Second Wave's
// Extended Avatar Project scales — and whether the meter is drawn at all is UpdateData's own
// three-way test, mirrored here branch for branch: the countdown when the AI is in Lose mode at
// max doom, the pips once doom has ever been above zero (bHasSeenDoomMeter latches on the first
// such frame), nothing before that. `current` is GetCurrentDoom() as the screen itself calls it,
// emitted so native can log this and the pips answering differently — two of our own surfaces on
// one fact must agree.
function DescribeDoom()
{
	local XComGameState_HeadquartersAlien AlienHQ;
	local string Rec;
	local bool bLose, bShown;

	NativeResult = "";

	AlienHQ = class'UIUtilities_Strategy'.static.GetAlienHQ(true);
	if (AlienHQ == none)
	{
		return;
	}

	bLose = (AlienHQ.AIMode == "Lose" && AlienHQ.AtMaxDoom());
	bShown = bLose || AlienHQ.GetCurrentDoom(true) > 0 || AlienHQ.bHasSeenDoomMeter;

	Rec = "kind=doom";
	class'X2AccessWire'.static.AddField(Rec, "shown", bShown ? "1" : "0");
	class'X2AccessWire'.static.AddField(Rec, "lose", bLose ? "1" : "0");
	class'X2AccessWire'.static.AddField(Rec, "current", string(AlienHQ.GetCurrentDoom()));
	class'X2AccessWire'.static.AddField(Rec, "max", string(AlienHQ.GetMaxDoom()));
	Emit(Rec);
}

// kind=timer  days=D  hours=H  minutes=M  seconds=S
//
// UpdateLoseTimer's own four values, off the same GetTimerDisplayValues call it makes, for the
// countdown that replaces the pips once the project completes. The unit WORDS are the panel's own
// localized strings and native reads them off the panel; only the numbers cross here. Read at
// announce time — the clock runs under the stop.
function DescribeDoomTimer()
{
	local XComGameState_HeadquartersAlien AlienHQ;
	local int Days, Hours, Minutes, Seconds;
	local string Rec;

	NativeResult = "";

	AlienHQ = class'UIUtilities_Strategy'.static.GetAlienHQ(true);
	if (AlienHQ == none)
	{
		return;
	}

	AlienHQ.GetTimerDisplayValues(Days, Hours, Minutes, Seconds);

	Rec = "kind=timer";
	class'X2AccessWire'.static.AddField(Rec, "days", string(Days));
	class'X2AccessWire'.static.AddField(Rec, "hours", string(Hours));
	class'X2AccessWire'.static.AddField(Rec, "minutes", string(Minutes));
	class'X2AccessWire'.static.AddField(Rec, "seconds", string(Seconds));
	Emit(Rec);
}

// ---------------------------------------------------------------------------
// The ring
// ---------------------------------------------------------------------------

// Build the category, move its remembered cursor by Delta, and format the entry. Returns "" when
// the category is empty, which is what the ring's skip and the empty text both key off.
private function string AnnounceCategory(int Category, Vector2D Origin, int Delta)
{
	local array<AccessGeoEntry> Entries;
	local int Index;

	Entries = BuildCategory(Category, Origin);
	if (Entries.Length == 0)
	{
		return "";
	}

	// Resume by KEY. The list is re-sorted whenever the origin moves, so the remembered index
	// would point at a different place; the key survives both a re-sort and a rebuild.
	Index = IndexOfKey(Entries, SelKeyByCategory[Category]);
	if (Index == INDEX_NONE)
	{
		// First visit, or the remembered entry is gone (a mission expired, a POI was consumed).
		// Delta -1 lands on the last entry so a first backward press is not silently a forward
		// one; anything else starts at the nearest, which with the Avenger as origin is where the
		// player already is.
		Index = (Delta < 0) ? Entries.Length - 1 : 0;
	}
	else
	{
		Index = Index + Delta;
		if (Index >= Entries.Length)
		{
			Index = 0;
		}
		else if (Index < 0)
		{
			Index = Entries.Length - 1;
		}
	}

	SelKeyByCategory[Category] = Entries[Index].Key;

	LastKey = Entries[Index].Key;
	LastX = Entries[Index].Loc.X;
	LastY = Entries[Index].Loc.Y;
	LastLabel = Entries[Index].Label;
	bLastValid = true;

	return FormatEntry(Entries[Index], Index, Entries.Length);
}

// "Western Africa, contact established, +12 supplies. 1,400 miles north-east. 3 of 17"
private function string FormatEntry(AccessGeoEntry Entry, int Index, int Total)
{
	local string Line;

	Line = Entry.Label;
	Line = Join(Line, Entry.Detail);
	Line = Join(Line, DistanceWords(Entry));
	Line = Join(Line, string(Index + 1) @ "of" @ string(Total));
	return Line;
}

// ⛔ Never a bare ", ". The game ends plenty of its own strings with '.', '!' or ':' and blind
// joining voices "5 дн.., " at the player — the rule X2AccessHelper.EndsPhrase now owns for the
// whole companion, and the kernel's AppendPhrase for the native side.
//
// The one local exception is a trailing COMMA, which the shared test deliberately does not treat
// as self-punctuation: a geoscape detail line can already end in one (an entry that lists two
// things), and there the comma IS the separator, so adding a second would stutter. Kept here
// rather than widened into the shared set, because everywhere else a trailing comma means a part
// was joined with nothing after it and the next part should still get its own separator.
private function string Join(string Head, string Tail)
{
	if (Tail == "")
	{
		return Head;
	}
	if (Head == "")
	{
		return Tail;
	}

	if (class'X2AccessHelper'.static.EndsPhrase(Head) || Right(Head, 1) == ",")
	{
		return Head @ Tail;
	}
	return Head $ ", " $ Tail;
}

// "1,400 miles north-east", or "here" when the entry IS the origin. The distance is the game's own
// conversion so our number is whatever the game would have said, and the bearing only appears once
// there is a real direction to give.
private function string DistanceWords(AccessGeoEntry Entry)
{
	if (Entry.Bearing == "")
	{
		return "here";
	}
	return string(Entry.Miles) @ "miles" @ Entry.Bearing;
}

private function int IndexOfKey(array<AccessGeoEntry> Entries, int Key)
{
	local int i;

	for (i = 0; i < Entries.Length; i++)
	{
		if (Entries[i].Key == Key)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

// ---------------------------------------------------------------------------
// The sort origin
// ---------------------------------------------------------------------------

// Resolve the origin to a point, falling back to the Avenger and SAYING SO when the remembered
// origin has gone. Missions expire and POIs are consumed, and silence there would leave the player
// sorting from a ghost without ever being told the ground moved.
private function bool ResolveOrigin(out Vector2D Origin)
{
	local XComGameState_HeadquartersXCom XComHQ;
	local XComGameState_GeoscapeEntity Entity;

	if (!bOriginIsAvenger)
	{
		Entity = XComGameState_GeoscapeEntity(`XCOMHISTORY.GetGameStateForObjectID(OriginKey));
		if (Entity != none)
		{
			Origin.X = OriginX;
			Origin.Y = OriginY;
			return true;
		}

		// The origin is gone. Fall back and prepend the fact to whatever is about to be said.
		bOriginIsAvenger = true;
		OriginKey = 0;
		OriginName = "";
		NativeResult = "Origin gone, sorting from the Avenger. ";
	}

	XComHQ = `XCOMHQ;
	if (XComHQ == none)
	{
		return false;
	}

	Origin = XComHQ.Get2DLocation();
	return true;
}

// The game's own wrap-aware distance, in the game's own unit.
//
// ⛔ Earth X wraps at the antimeridian, so a naive subtraction reports a site one step across the
// date line as maximally far. XGGeoscape.GetShortestDist is what the game itself uses and it
// already handles that; XComEarth.ConvertEarthCoordsToMiles is the matching unit conversion, so a
// number we speak is a number the game would have spoken.
private function int MilesBetween(Vector2D From, Vector2D To)
{
	local XGGeoscape Geoscape;
	local XComEarth Earth;
	local Vector2D Delta;
	local float Magnitude;

	Geoscape = `GAME.GetGeoscape();
	Earth = `EARTH;
	if (Geoscape == none || Earth == none)
	{
		return 0;
	}

	Delta = Geoscape.GetShortestDist(From, To);
	Magnitude = Sqrt(Delta.X * Delta.X + Delta.Y * Delta.Y);
	return Earth.ConvertEarthCoordsToMiles(Magnitude);
}

// Eight-point compass from the origin. Returns "" for a point on top of the origin, which is what
// DistanceWords turns into "here".
private function string BearingFrom(Vector2D From, Vector2D To)
{
	local XGGeoscape Geoscape;
	local Vector2D Delta;
	local float NorthAmount, EastAmount, AbsN, AbsE;
	local string Vertical, Horizontal;

	Geoscape = `GAME.GetGeoscape();
	if (Geoscape == none)
	{
		return "";
	}

	Delta = Geoscape.GetShortestDist(From, To);

	EastAmount = Delta.X;
	// The one calibration constant — see Y_IS_SOUTH above.
	NorthAmount = Y_IS_SOUTH ? -Delta.Y : Delta.Y;

	AbsN = Abs(NorthAmount);
	AbsE = Abs(EastAmount);

	// Below a fiftieth of the map in both axes is "here" rather than a bearing nobody could act on.
	if (AbsN < 0.02 && AbsE < 0.02)
	{
		return "";
	}

	Vertical = (NorthAmount > 0) ? "north" : "south";
	Horizontal = (EastAmount > 0) ? "east" : "west";

	// Only name the minor axis when it is a real component: a site 1,400 miles east and 40 north
	// is east, and calling it north-east would point the player at a place they cannot reach.
	if (AbsN < AbsE * 0.4)
	{
		return Horizontal;
	}
	if (AbsE < AbsN * 0.4)
	{
		return Vertical;
	}
	return Vertical $ "-" $ Horizontal;
}

// Fill in distance and bearing, then insertion-sort by distance. Sorting LAST, over the completed
// list, is what lets the origin move without any category needing to know it did.
private function SortByDistance(out array<AccessGeoEntry> Entries, Vector2D Origin)
{
	local int i, j;
	local AccessGeoEntry Tmp;

	for (i = 0; i < Entries.Length; i++)
	{
		Entries[i].Miles = MilesBetween(Origin, Entries[i].Loc);
		Entries[i].Bearing = BearingFrom(Origin, Entries[i].Loc);
	}

	for (i = 1; i < Entries.Length; i++)
	{
		Tmp = Entries[i];
		j = i - 1;
		while (j >= 0 && Entries[j].Miles > Tmp.Miles)
		{
			Entries[j + 1] = Entries[j];
			j--;
		}
		Entries[j + 1] = Tmp;
	}
}

// Drive the game's own camera. CAMLookAtEarth is what every pin's hover handler calls, so the
// swoop, the zoom and the interpolation are the ones the player already knows.
private function LookAt(float X, float Y)
{
	local Vector2D Loc;
	local XComHQPresentationLayer Pres;

	Pres = `HQPRES;
	if (Pres == none)
	{
		return;
	}

	Loc.X = X;
	Loc.Y = Y;
	Pres.CAMLookAtEarth(Loc, 0.5f);
}

// ---------------------------------------------------------------------------
// The categories
// ---------------------------------------------------------------------------

private function array<AccessGeoEntry> BuildCategory(int Category, Vector2D Origin)
{
	local array<AccessGeoEntry> Entries;

	switch (Category)
	{
	case CAT_REGIONS:     Entries = BuildRegions();    break;
	case CAT_MISSIONS:    Entries = BuildMissions(false); break;
	case CAT_FACILITIES:  Entries = BuildMissions(true);  break;
	case CAT_SCANSITES:   Entries = BuildScanSites(); break;
	case CAT_FACTIONS:    Entries = BuildFactions();  break;
	case CAT_CONTINENTS:  Entries = BuildContinents(); break;
	}

	SortByDistance(Entries, Origin);
	return Entries;
}

// Every region the globe is drawing. ⛔ The words mirror UIStrategyMapItem_Region.GenerateTooltip
// gate for gate — including its opening `if(!IsContactResearched()) return;` — so a region we
// describe is one the player could have hovered, and a region whose tooltip the game suppresses
// gets its state word and no income figure.
private function array<AccessGeoEntry> BuildRegions()
{
	local array<AccessGeoEntry> Entries;
	local AccessGeoEntry Entry;
	local XComGameState_WorldRegion Region;
	local XComGameState_HeadquartersXCom XComHQ;

	XComHQ = `XCOMHQ;

	foreach `XCOMHISTORY.IterateByClassType(class'XComGameState_WorldRegion', Region)
	{
		if (!Region.ShouldBeVisible())
		{
			continue;
		}

		Entry.Key = Region.ObjectID;
		Entry.Label = Region.GetDisplayName();
		Entry.Detail = RegionState(Region, XComHQ);
		Entry.Loc = Region.Get2DLocation();
		Entries.AddItem(Entry);
	}

	return Entries;
}

// The resistance state, plus the income figure the region pin draws as a coloured "+N" label.
// Both are what GenerateTooltip shows; the colour is dropped because the number carries it.
//
// ⛔ THE STATE WORD ALONE IS NOT THE STATE. "contact established" is true of a region you can
// install a relay in this minute, of one already building one, and of one whose relay tech is not
// researched yet — three different situations with three different next moves, and the pin draws
// them differently (a tower button, a scan meter, nothing). Collapsing them is the shape of bug
// CLAUDE.md's distribution rule is about: every line reads correctly and the readout is still
// wrong. The availability clause here and the actions stop in the recipe are the same fact said
// twice on purpose — the ring is where you BROWSE the world, the stop is where you act on it.
private function string RegionState(XComGameState_WorldRegion Region,
	XComGameState_HeadquartersXCom XComHQ)
{
	local string Line;
	local bool bContactResearched, bOutpostResearched;

	bContactResearched = (XComHQ != none) && XComHQ.IsContactResearched();
	bOutpostResearched = (XComHQ != none) && XComHQ.IsOutpostResearched();

	switch (Region.ResistanceLevel)
	{
	case eResLevel_Outpost:
		Line = "radio relay built";
		break;

	case eResLevel_Contact:
		if (Region.bCanScanForOutpost)
		{
			// The game's own composed status line — "BUILDING RADIO RELAY: 12 Days remaining" —
			// the same call the scan-sites category makes, so a region in progress reads
			// identically in both rings.
			Line = Region.GetUIButtonTooltipBody();
		}
		else
		{
			Line = "contact established";
			if (bOutpostResearched)
			{
				Line = Join(Line, "radio relay available");
			}
		}
		break;

	case eResLevel_Unlocked:
		if (Region.bCanScanForContact)
		{
			Line = Region.GetUIButtonTooltipBody();
		}
		else if (bContactResearched)
		{
			Line = "contact available";
		}
		else
		{
			// No tech, no button, and GenerateTooltip suppresses the whole tooltip before contact
			// is researched — so a sighted player cannot tell this from a locked region either.
			Line = "no contact";
		}
		break;

	case eResLevel_Locked:
		Line = "no contact";
		break;

	default:
		Line = "";
		break;
	}

	// The income figure appears only where the game shows it: GenerateTooltip suppresses the whole
	// tooltip until contact is researched, and again while the region is currently scannable (the
	// scan button is showing the player something better).
	if (bContactResearched && !(Region.bCanScanForContact || Region.bCanScanForOutpost))
	{
		Line = Join(Line, "+" $ string(Region.GetSupplyDropReward(true)) @ "supplies");
	}

	if (Region.HasMissionInRegion())
	{
		Line = Join(Line, "mission here");
	}

	return Line;
}

// Missions. ⛔ Split into two categories by the game's own UI fork: MissionSite.GetUIClass()
// returns UIStrategyMapItem_AlienNetworkComponent when MakesDoom() and UIStrategyMapItem_Mission
// otherwise, so "alien facility" versus "mission" is the game's distinction rather than ours — and
// facilities are the ones a player hunts for deliberately.
private function array<AccessGeoEntry> BuildMissions(bool bFacilities)
{
	local array<AccessGeoEntry> Entries;
	local AccessGeoEntry Entry;
	local XComGameState_MissionSite Mission;

	foreach `XCOMHISTORY.IterateByClassType(class'XComGameState_MissionSite', Mission)
	{
		if (!Mission.Available || !Mission.ShouldBeVisible())
		{
			continue;
		}
		if (Mission.MakesDoom() != bFacilities)
		{
			continue;
		}

		Entry.Key = Mission.ObjectID;
		// The objective line is what the mission IS ("Расследовать секретный объект"); the tooltip
		// title is the mission-source pin label the tray shows, and stands in when a mission
		// carries no objective text of its own.
		Entry.Label = Mission.GetMissionObjectiveText();
		if (Entry.Label == "")
		{
			Entry.Label = Mission.GetUIButtonTooltipTitle();
		}
		Entry.Detail = Mission.GetMissionDifficultyLabel();
		// A facility's pin draws its doom as a level (UIStrategyMapItem_AlienNetworkComponent
		// .UpdateFlyoverText → SetLevel(MissionState.Doom)), so the count is on screen; labelled
		// with the meter's own title, the word the game ties those blocks to.
		if (bFacilities && Mission.Doom > 0)
		{
			Entry.Detail = Join(Entry.Detail,
				class'UIStrategyMap_HUD'.default.m_strDoomTitle @ string(Mission.Doom));
		}
		Entry.Loc = Mission.Get2DLocation();
		Entries.AddItem(Entry);
	}

	return Entries;
}

// Everything the game itself considers scannable right now — the same call UpdateMissions builds
// the tray's middle section from, so the ring and the tray can never disagree about what is
// available. ⛔ Faction havens are filtered out here because they are their own category; listing
// one thing in two rings is the mistake the tactical scanner's evac promotion already corrected.
private function array<AccessGeoEntry> BuildScanSites()
{
	local array<AccessGeoEntry> Entries;
	local AccessGeoEntry Entry;
	local XComGameState_HeadquartersXCom XComHQ;
	local array<XComGameState_ScanningSite> Sites;
	local XComGameState_ScanningSite Site;
	local XComGameState_Haven Haven;
	local int i;

	XComHQ = `XCOMHQ;
	if (XComHQ == none)
	{
		return Entries;
	}

	Sites = XComHQ.GetAvailableScanningSites();
	for (i = 0; i < Sites.Length; i++)
	{
		Site = Sites[i];
		if (Site == none)
		{
			continue;
		}

		Haven = XComGameState_Haven(Site);
		if (Haven != none && Haven.IsFactionHQ())
		{
			continue;
		}

		Entry.Key = Site.ObjectID;
		Entry.Label = Site.GetUIButtonTooltipTitle();
		Entry.Detail = Site.GetUIButtonTooltipBody();
		Entry.Loc = Site.Get2DLocation();
		Entries.AddItem(Entry);
	}

	return Entries;
}

// The three faction headquarters. ⛔ A faction not yet MET is skipped outright rather than listed
// as unknown: the tray already carries a placeholder slot for each of those, a sighted player can
// see exactly three of them and learn nothing else, and naming one here — even generically — would
// tell a blind player which of the three slots is which.
private function array<AccessGeoEntry> BuildFactions()
{
	local array<AccessGeoEntry> Entries;
	local AccessGeoEntry Entry;
	local XComGameState_Haven Haven;

	foreach `XCOMHISTORY.IterateByClassType(class'XComGameState_Haven', Haven)
	{
		if (!Haven.IsFactionHQ() || !Haven.IsResistanceFactionMet())
		{
			continue;
		}

		Entry.Key = Haven.ObjectID;
		Entry.Label = Haven.GetUIButtonTooltipTitle();
		Entry.Detail = Haven.GetUIButtonTooltipBody();
		Entry.Loc = Haven.Get2DLocation();
		Entries.AddItem(Entry);
	}

	return Entries;
}

// Continents, with their bonus. This is the coarse frame the other categories hang off — "where am
// I on the Earth" is a question the globe answers at a glance and speech otherwise cannot.
private function array<AccessGeoEntry> BuildContinents()
{
	local array<AccessGeoEntry> Entries;
	local AccessGeoEntry Entry;
	local XComGameState_Continent Continent;
	local XComGameState_StrategyCard BonusCard;

	foreach `XCOMHISTORY.IterateByClassType(class'XComGameState_Continent', Continent)
	{
		Entry.Key = Continent.ObjectID;
		Entry.Label = Continent.GetMyTemplate().DisplayName;
		Entry.Detail = "";

		BonusCard = Continent.GetContinentBonusCard();
		if (BonusCard != none)
		{
			Entry.Detail = BonusCard.GetDisplayName();
			// The bonus name is drawn in a COLOUR that says whether it is active
			// (UIStrategyMapItem_Continent.UpdateFlyoverText: eUIState_Good against
			// eUIState_Disabled), and the pin's tooltip spells the same thing out in words.
			// Speech has no colour, so it takes the words — the game's own, both of them.
			if (Continent.bContinentBonusActive)
			{
				Entry.Detail = Join(Entry.Detail,
					class'UIStrategyMapItem_Continent'.default.m_strBonusAchieved);
			}
			else
			{
				Entry.Detail = Join(Entry.Detail,
					class'UIStrategyMapItem_Continent'.default.m_strBonusHelp);
			}
		}

		// The two counters the pin draws as rows of pips, and the reason a relay is worth
		// building at all — the continent bonus needs every region contacted AND relayed.
		// ⛔ The arithmetic is UpdateFlyoverText's own, not a re-derivation: resistance level
		// counts 1 for a contact and 2 for a relay, so `level - towers` is regions contacted and
		// `maxLevel - maxTowers` is regions in the continent. Recomputing it from the region list
		// would be a second implementation to keep in step.
		Entry.Detail = Join(Entry.Detail, ContinentProgress(Continent));

		Entry.Loc = Continent.Get2DLocation();
		Entries.AddItem(Entry);
	}

	return Entries;
}

// "regions contacted 3 of 4, radio relays 1 of 4" — the four numbers SetContinentLevel pushes
// into Flash as two rows of pips, which is the only place the globe states how far a continent is
// from its bonus.
private function string ContinentProgress(XComGameState_Continent Continent)
{
	local int Towers, MaxTowers, Contacted, RegionCount;
	local string Line;

	Towers = Continent.GetNumRadioTowers();
	MaxTowers = Continent.GetMaxRadioTowers();
	Contacted = Continent.GetResistanceLevel() - Towers;
	RegionCount = Continent.GetMaxResistanceLevel() - MaxTowers;

	Line = "regions contacted" @ string(Contacted) @ "of" @ string(RegionCount);
	return Join(Line, "radio relays" @ string(Towers) @ "of" @ string(MaxTowers));
}

// ---------------------------------------------------------------------------
// Words
// ---------------------------------------------------------------------------

private function string CategoryName(int Category)
{
	switch (Category)
	{
	case CAT_REGIONS:    return "Regions";
	case CAT_MISSIONS:   return "Missions";
	case CAT_SCANSITES:  return "Scan sites";
	case CAT_FACILITIES: return "Alien facilities";
	case CAT_FACTIONS:   return "Faction headquarters";
	case CAT_CONTINENTS: return "Continents";
	}
	return "Category" @ string(Category);
}

// The empty line names WHY, not just that. "No alien facilities found yet" is a campaign state a
// player can act on; "empty" is not.
private function string EmptyText(int Category)
{
	switch (Category)
	{
	case CAT_REGIONS:    return "No regions on the map";
	case CAT_MISSIONS:   return "No missions available";
	case CAT_SCANSITES:  return "Nothing to scan right now";
	case CAT_FACILITIES: return "No alien facilities found yet";
	case CAT_FACTIONS:   return "No factions met yet";
	case CAT_CONTINENTS: return "No continents on the map";
	}
	return "Nothing here";
}

defaultproperties
{
	bOriginIsAvenger = true
	CurCategory = 0
}
