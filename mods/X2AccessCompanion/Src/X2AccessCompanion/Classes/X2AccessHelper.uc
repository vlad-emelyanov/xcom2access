// X2AccessHelper
// Shared static utility functions for accessibility screen listeners.
// Provides common helpers for collecting nav help text, reading widget
// values from UIMechaListItem, etc.

class X2AccessHelper extends Object;

// Written by the native-callable functions at the bottom of this file and reflected out by the
// ASI right after the synchronous ProcessEvent returns — the same pattern X2AccessTileReader uses.
// CDO rule: value-typed state only, never object references.
var string NativeResult;

// Must match kVocabSep in src/tutorial_reader.cpp.
const VOCAB_SEP = "~|~";

// ---------------------------------------------------------------------------
// Joining spoken phrases — THE one implementation
// ---------------------------------------------------------------------------
//
// ⛔ NEVER join two spoken phrases with a bare separator. The GAME ends plenty of its own strings
// with '.', '!', '?', ':' or ';', and blindly adding ours voices a doubled terminator at the
// player — which most screen readers pause on twice and some read as an ellipsis. This is the
// same rule X2Access::Graph::AppendPhrase enforces natively (src/Graph/GraphAnnouncer.cpp, four
// kernel tests) with the SAME terminator set, so a string reads alike whichever layer composed
// it; CLAUDE.md states it for every surface that composes speech outside a recipe.
//
// It lives here, on the shared oracle, because it was rediscovered LIVE three times first —
// "5 дн.. 1 of 1 staff slots filled" on the ant farm (2026-08-21), "получает маскировку..
// Неудача" on the hacking pad (2026-08-20) and "ЗАДАЧА:," on the Skyranger briefing — and each
// time the fix went into one function while its siblings kept the naive body. Every companion
// copy now delegates here, so the next surface inherits the rule instead of rediscovering it.
//
// ⛔ The test is on the ACCUMULATED result's tail, never on the incoming part: the separator goes
// BETWEEN them, so it is the left-hand side that decides whether one is needed at all.
static function bool EndsPhrase(string Text)
{
	local string Last;

	if (Text == "")
		return false;

	Last = Right(Text, 1);
	return Last == "." || Last == "!" || Last == "?" || Last == ":" || Last == ";";
}

// A self-punctuated part takes a plain SPACE; everything else takes the separator. An empty part
// is dropped WITHOUT its separator — CLAUDE.md's "join after cleaning, never before": a nickname
// that CleanText reduces to nothing must not leave a comma standing behind it.
static function AppendJoined(out string Result, string Part, string Separator)
{
	if (Part == "")
		return;
	if (Result != "")
		Result $= EndsPhrase(Result) ? " " : Separator;
	Result $= Part;
}

// Sentence join — stacking independent facts into one readout ("Impassable. Full cover north").
static function AppendPart(out string Result, string Part)
{
	AppendJoined(Result, Part, ". ");
}

// Clause join — parts of ONE sentence ("Sectoid, 4 tiles, 2 north").
static function AppendComma(out string Result, string Part)
{
	AppendJoined(Result, Part, ", ");
}

// Collect all visible button labels from a UINavigationHelp bar.
// Returns a period-separated string of button text.
static function string CollectNavHelp(UINavigationHelp NavHelp)
{
	local string Result;

	if (NavHelp == none)
		return "";

	Result = CollectContainerButtons(NavHelp.LeftContainer);

	if (NavHelp.CenterContainer != none)
	{
		if (Result != "")
			Result $= ". ";
		Result $= CollectContainerButtons(NavHelp.CenterContainer);
	}

	if (NavHelp.RightContainer != none)
	{
		if (Result != "")
			Result $= ". ";
		Result $= CollectContainerButtons(NavHelp.RightContainer);
	}

	return Result;
}

// Collect text from all UIButton children in a container panel.
static function string CollectContainerButtons(UIPanel Container)
{
	local string Result;
	local int i;
	local UIButton Btn;

	if (Container == none)
		return "";

	for (i = 0; i < Container.ChildPanels.Length; i++)
	{
		Btn = UIButton(Container.ChildPanels[i]);
		if (Btn != none && Btn.Text != "")
		{
			if (Result != "")
				Result $= ". ";
			Result $= Btn.Text;
		}
	}

	return Result;
}

// Return "Checked" or "Unchecked" for a UIMechaListItem with a checkbox.
static function string GetCheckboxState(UIMechaListItem Item)
{
	if (Item != none && Item.Checkbox != none)
	{
		if (Item.Checkbox.bChecked)
			return "Checked";
		return "Unchecked";
	}
	return "";
}

// Return slider value as "X percent" string.
// UE3/Scaleform sliders store percent as 0-100 (not 0.0-1.0).
static function string GetSliderPercent(UIMechaListItem Item)
{
	if (Item != none && Item.Slider != none)
		return string(int(Item.Slider.percent)) $ " percent";
	return "";
}

// Return current spinner display value.
static function string GetSpinnerValue(UIMechaListItem Item)
{
	if (Item != none && Item.Spinner != none)
		return Item.Spinner.Value;
	return "";
}

// Return current dropdown selection text.
static function string GetDropdownValue(UIMechaListItem Item)
{
	if (Item != none && Item.Dropdown != none)
	{
		if (Item.Dropdown.SelectedItem >= 0 && Item.Dropdown.SelectedItem < Item.Dropdown.Items.Length)
			return Item.Dropdown.Items[Item.Dropdown.SelectedItem];
	}
	return "";
}

// Auto-detect widget type and return its current value as a TTS string.
static function string GetItemValueString(UIMechaListItem Item)
{
	if (Item == none)
		return "";

	// Check each sub-widget; only the active one is non-none after UpdateData*
	if (Item.Slider != none)
		return GetSliderPercent(Item);
	if (Item.Checkbox != none)
		return GetCheckboxState(Item);
	if (Item.Spinner != none)
		return GetSpinnerValue(Item);
	if (Item.Dropdown != none)
		return GetDropdownValue(Item);

	return "";
}

// ---------------------------------------------------------------------------
// The soldier identity block both mission bookends share
// ---------------------------------------------------------------------------
//
// Squad select and after action describe the same soldiers, and until this block existed the
// sameness was maintained by a COMMENT in each file promising to match the other. One
// implementation is the two-surfaces-must-agree rule (the cover-readout lesson) applied to
// people. Tactical health is deliberately NOT this: X2AccessTacticalReader words HP off
// GetMaxStat, a different source answering a different screen.

// The name line, falling back to the first name exactly as the cards' UpdateData does — an
// empty line here would cost the join its field.
static function string DescribeSoldierName(XComGameState_Unit Unit)
{
	local string NameStr;

	NameStr = Unit.GetName(eNameType_Last);
	if (NameStr == "")
		NameStr = Unit.GetName(eNameType_First);
	return NameStr;
}

// The word for the psi-gift markup. That icon has no caption anywhere, so the honest label is
// the game's own name for what it means — the Psi Operative class — rather than a coinage of
// ours.
static function string PsiWord()
{
	local X2SoldierClassTemplate Psi;

	Psi = class'X2SoldierClassTemplateManager'.static.GetSoldierClassTemplateManager()
		.FindSoldierClassTemplate('PsiOperative');
	return Psi != none ? Psi.DisplayName : "";
}

// The health bar, in the same numbers UIUtilities_Strategy hands the bar itself.
static function string DescribeSoldierHealth(XComGameState_Unit Unit)
{
	local int Cur, Max;

	Cur = class'UIUtilities_Strategy'.static.GetUnitCurrentHealth(Unit);
	Max = class'UIUtilities_Strategy'.static.GetUnitMaxHealth(Unit);
	if (Max <= 0)
		return "";

	return string(Cur) @ "of" @ string(Max) @ "health";
}

// The will bar plus the mental-state label (the persistent form of the walk-up's tired/shaken
// flyover — AddWillStateFlyovers speaks GetMentalStateLabel for BelowReadyWillState).
// `bSkipDeadOrCaptured` is after action's reading, where a KIA or captured soldier's will bar
// means nothing; squad select never shows such a card and passes false.
static function string DescribeSoldierWill(XComGameState_Unit Unit, bool bSkipDeadOrCaptured)
{
	local string Result;

	if (!Unit.UsesWillSystem())
		return "";
	if (bSkipDeadOrCaptured && (!Unit.IsAlive() || Unit.bCaptured))
		return "";

	Result = string(class'UIUtilities_Strategy'.static.GetUnitWillPercent(Unit)) $ "% will";
	if (Unit.BelowReadyWillState())
		AppendComma(Result, Unit.GetMentalStateLabel());

	return Result;
}

// ---------------------------------------------------------------------------
// The armory header — shared by every UIArmory screen that reads it
// ---------------------------------------------------------------------------
//
// UISoldierHeader pushes its name line and its stat column into Flash and retains none of it,
// so each armory reader has to compose them. ONE implementation, so the loadout and the hub
// name a soldier identically (the two-surfaces-must-agree rule). Mirrors
// UISoldierHeader.PopulateData: the base stat plus what abilities give, then the inventory's
// contribution as the signed bonus the screen prints beside it; psi only for a psi operative,
// exactly as the header gates it.

// Full name with nickname, then the rank name and the class for anyone past rookie.
static function string DescribeArmorySoldier(XComGameState_Unit Unit)
{
	local string Result;

	Result = Unit.GetName(eNameType_FullNick);
	if (Unit.GetRank() > 0)
	{
		AppendComma(Result, class'X2ExperienceConfig'.static.GetRankName(Unit.GetRank(),
			Unit.GetSoldierClassTemplateName()));
		if (Unit.GetSoldierClassTemplate() != none)
			AppendComma(Result, Unit.GetSoldierClassTemplate().DisplayName);
	}
	return Result;
}

// The header's stat column, in its own order. CheckGameState is the screen's — the loadout
// previews an equip through it, the hub passes its own (usually none).
static function string DescribeArmoryStats(XComGameState_Unit Unit, XComGameState CheckGameState)
{
	local string Result;

	AppendComma(Result, ArmoryStatLine(class'UISoldierHeader'.default.m_strAimLabel, Unit, CheckGameState,
		eStat_Offense));
	AppendComma(Result, ArmoryStatLine(class'UISoldierHeader'.default.m_strHealthLabel, Unit, CheckGameState,
		eStat_HP));
	AppendComma(Result, ArmoryStatLine(class'UISoldierHeader'.default.m_strMobilityLabel, Unit,
		CheckGameState, eStat_Mobility));
	AppendComma(Result, ArmoryStatLine(class'UISoldierHeader'.default.m_strWillLabel, Unit, CheckGameState,
		eStat_Will));
	AppendComma(Result, ArmoryStatLine(class'UISoldierHeader'.default.m_strArmorLabel, Unit, CheckGameState,
		eStat_ArmorMitigation));
	AppendComma(Result, ArmoryStatLine(class'UISoldierHeader'.default.m_strDodgeLabel, Unit, CheckGameState,
		eStat_Dodge));
	AppendComma(Result, ArmoryStatLine(class'UISoldierHeader'.default.m_strTechLabel, Unit, CheckGameState,
		eStat_Hacking));
	if (Unit.IsPsiOperative())
		AppendComma(Result, ArmoryStatLine(class'UISoldierHeader'.default.m_strPsiLabel, Unit,
			CheckGameState, eStat_PsiOffense));

	return Result;
}

static function string ArmoryStatLine(string Label, XComGameState_Unit Unit, XComGameState CheckGameState,
	ECharStatType Stat)
{
	local int Base, Bonus;
	local string Result;

	Base = int(Unit.GetCurrentStat(Stat)) + Unit.GetUIStatFromAbilities(Stat);
	Bonus = Unit.GetUIStatFromInventory(Stat, CheckGameState);

	Result = Label @ string(Base);
	if (Bonus > 0)
		Result $= " +" $ string(Bonus);
	else if (Bonus < 0)
		Result $= " " $ string(Bonus);
	return Result;
}

// ---------------------------------------------------------------------------
// Tactical tile description helpers
// ---------------------------------------------------------------------------

// Return period-separated hazard names for a tile (e.g. "Fire. Poison.").
static function string DescribeHazards(XComWorldData WorldData, TTile Tile)
{
	local string Result;

	if (WorldData.TileContainsFire(Tile))
		Result = "Fire";
	if (WorldData.TileContainsPoison(Tile))
	{
		if (Result != "")
			Result $= ". ";
		Result $= "Poison";
	}
	if (WorldData.TileContainsSmoke(Tile))
	{
		if (Result != "")
			Result $= ". ";
		Result $= "Smoke";
	}
	if (WorldData.TileContainsAcid(Tile))
	{
		if (Result != "")
			Result $= ". ";
		Result $= "Acid";
	}

	return Result;
}

// ---------------------------------------------------------------------------
// The two conditions the unit effect summary CANNOT report
// ---------------------------------------------------------------------------
//
// ⛔ **`GetUISummary_UnitEffectsByCategory` does not carry stun or panic, and never has.** Neither
// `X2StatusEffects.CreateStunnedStatusEffect` nor any of the four `X2Effect_Panicked` constructors
// (Panicked, Berserk, Obsessed, Shattered) calls `SetDisplayInfo`, so both effects keep
// `bDisplayInUI` false and an EMPTY `FriendlyName` and can never land in either buff category. The
// game does not read them from there either: the unit flag draws its stun icon straight off
// `StunnedActionPoints` (`XComGameState_Unit.GetUISummary_UnitStatusIcons`). Everything else a
// flag shows IS in that summary, which is why this pair — and only this pair — is carried by hand.
//
// It lives HERE rather than in either caller because the two surfaces that answer "what is wrong
// with this unit" already disagreed once: the `,` squad ring said "Оглушение: 2" while the `;`
// soldier sheet, sourcing purely from the summary, said nothing about the same stunned soldier.
// Same family as the cover disagreement that made this class a shared oracle in the first place.
static function string UnlistedPenaltyWords(XComGameState_Unit UnitState)
{
	local string Result;

	if (UnitState == none)
		return "";

	AppendComma(Result, StunWords(UnitState));
	if (UnitState.IsPanicked())
		AppendComma(Result, class'UIEventNoticesTactical'.default.PanickedTitle);

	return Result;
}

// "Stunned: 2" — the word AND how many actions the stun still owes, which is the half that decides
// whether this unit is worth walking to or shooting at.
//
// Copied clause for clause from `X2StatusEffects.GetStunnedFlyoverText`, robotic branch included:
// `<Ability:STUNNEDACTIONPOINTS>` expands against the unit (`X2AbilityTag.uc:347`, which sums
// `StunnedActionPoints` and `StunnedThisTurn`), and a SPARK or a hacked MEC reads "Shutdown"
// instead — the same word its own flyover used. Gated on `StunnedActionPoints > 0`, which is the
// flag's gate, so the tag's own zero-case fallback (a `GetPreviousGameStateForObject` deref) is
// never reached.
static function string StunWords(XComGameState_Unit UnitState)
{
	local X2AbilityTag AbilityTag;
	local string Result;

	if (UnitState == none || UnitState.StunnedActionPoints <= 0)
		return "";

	AbilityTag = X2AbilityTag(`XEXPANDCONTEXT.FindTag("Ability"));
	AbilityTag.ParseObj = UnitState;
	Result = `XEXPAND.ExpandString(UnitState.IsRobotic()
		? class'X2StatusEffects'.default.RoboticStunnedPerActionFriendlyName
		: class'X2StatusEffects'.default.StunnedPerActionFriendlyName);
	AbilityTag.ParseObj = none;

	return Result;
}

// ---------------------------------------------------------------------------
// Concealment detection — the red tile overlay, recomputed (docs/concealment-recon.md)
// ---------------------------------------------------------------------------

// Would moving ConcealedUnit onto Tile break its concealment? The spoken form of the game's red
// detection overlay, recomputed from the same rules OnUnitEnteredTile enforces
// (XComGameState_Unit.uc:8960-9058) because the native tile cache and the pathing pawn's marker
// arrays are both unreachable from script. OutKind: bit 1 = an enemy unit detects, bit 2 = an
// unhacked tower's radius. Kept as ONE oracle so the tile readout and the move preview can never
// disagree about which tiles are red.
//
// Sighted parity, not mechanics, picks the detector set: only enemies the SQUAD can see
// contribute (the same CanXComSquadSeeTarget gate as the object scanner's enemy ring) — walking
// into an unseen pod's range reveals you with no warning for a sighted player too. Which units
// count as detectors at all is the game's own UnitBreaksConcealment; the distance is the game's
// own GetConcealmentDetectionDistance (called on the CONCEALED unit, detector as argument — the
// orientation :9017 uses). The LOS trace runs LAST: it is the expensive term, and the cheap
// distance gate rejects most of the map first.
static function bool TileBreaksConcealment(XComGameState_Unit ConcealedUnit, TTile Tile,
	out int OutKind)
{
	local XComGameStateHistory History;
	local XComWorldData WorldData;
	local XComGameState_Unit Detector;
	local XComGameState_InteractiveObject Obj;
	local GameRulesCache_VisibilityInfo VisInfo;
	local FOWTileStatus FOW;
	local byte GameplayFlags;
	local vector TilePos, DetectorPos;
	local name RetainName;
	local bool bRetainVsInteractives;

	OutKind = 0;
	if (ConcealedUnit == none || !ConcealedUnit.IsConcealed())
		return false;

	History = `XCOMHISTORY;
	WorldData = `XWORLD;
	TilePos = WorldData.GetPositionFromTileCoordinates(Tile);

	foreach History.IterateByClassType(class'XComGameState_Unit', Detector)
	{
		if (Detector.ObjectID == ConcealedUnit.ObjectID)
			continue;
		if (!Detector.IsAlive() || Detector.bRemovedFromPlay)
			continue;
		if (!ConcealedUnit.UnitBreaksConcealment(Detector))
			continue;
		if (!class'X2TacticalVisibilityHelpers'.static.CanXComSquadSeeTarget(Detector.ObjectID))
			continue;

		if (!Detector.DoesUnitBreaksConcealmentIgnoringDistance())
		{
			DetectorPos = WorldData.GetPositionFromTileCoordinates(Detector.TileLocation);
			if (VSizeSq(TilePos - DetectorPos)
				> Square(ConcealedUnit.GetConcealmentDetectionDistance(Detector)))
				continue;
		}
		if (!WorldData.CanSeeTileToTile(Detector.TileLocation, Tile, VisInfo))
			continue;

		OutKind = OutKind | 1;
		break;
	}

	// Towers: pure radius, no LOS (XComGameState_Unit.uc:8983), void for a unit carrying an
	// AbilityRetainsConcealmentVsInteractives ability (:8963). FOW-latched like the scanner's
	// tower category: an unseen tower detects mechanically, but its ring is invisible to a
	// sighted player too, so speaking it would leak the map.
	foreach class'X2AbilityTemplateManager'.default.AbilityRetainsConcealmentVsInteractives(RetainName)
	{
		if (ConcealedUnit.HasSoldierAbility(RetainName))
		{
			bRetainVsInteractives = true;
			break;
		}
	}
	if (!bRetainVsInteractives)
	{
		foreach History.IterateByClassType(class'XComGameState_InteractiveObject', Obj)
		{
			if (Obj.bRemovedFromPlay || Obj.bHasBeenHacked || Obj.DetectionRange <= 0)
				continue;
			WorldData.GetTileFOWValue(Obj.TileLocation, FOW, GameplayFlags);
			if (FOW == eFOWTileStatus_NeverSeen)
				continue;
			DetectorPos = WorldData.GetPositionFromTileCoordinates(Obj.TileLocation);
			if (VSizeSq(TilePos - DetectorPos) <= Square(Obj.DetectionRange))
			{
				OutKind = OutKind | 2;
				break;
			}
		}
	}

	return OutKind != 0;
}

// The word form of TileBreaksConcealment — "" when nothing detects (or the unit is not
// concealed, matching the game hiding the overlay the moment concealment is gone). An enemy
// outranks a tower in the wording because the tower is the escapable half (hack it).
static function string DescribeDetection(XComGameState_Unit ConcealedUnit, TTile Tile)
{
	local int Kind;

	if (!TileBreaksConcealment(ConcealedUnit, Tile, Kind))
		return "";
	if ((Kind & 1) != 0)
		return "Detection zone";
	return "Tower detection";
}

// Return description of units on a tile (e.g. "Enemy: Sectoid" or "Ally: Sgt. Kelly").
static function string DescribeUnitsOnTile(TTile Tile)
{
	local XComWorldData WorldData;
	local array<StateObjectReference> UnitRefs;
	local XComGameState_Unit UnitState;
	local XComGameStateHistory History;
	local string Result;
	local string Prefix;
	local int i;

	WorldData = `XWORLD;
	History = `XCOMHISTORY;
	UnitRefs = WorldData.GetUnitsOnTile(Tile);

	for (i = 0; i < UnitRefs.Length; i++)
	{
		UnitState = XComGameState_Unit(History.GetGameStateForObjectID(UnitRefs[i].ObjectID));
		if (UnitState == none)
			continue;
		if (!UnitState.IsAlive())
			continue;

		if (UnitState.IsFriendlyToLocalPlayer())
			Prefix = "Ally: ";
		else
			Prefix = "Enemy: ";

		if (Result != "")
			Result $= ". ";
		Result $= Prefix $ UnitState.GetName(eNameType_Full);
	}

	return Result;
}

// ---------------------------------------------------------------------------
// Speech vocabulary — the game's OWN words, handed to native once per session
// ---------------------------------------------------------------------------
//
// Native writes text into the tutorial's markup ("<img src='rmb_icon'>", "%KEY:LMB%") and has to
// speak SOMETHING for it. Its built-in table is English, which is wrong the moment the game is not
// — a Russian instruction that says "Нажмите right mouse button" is our wording intruding on the
// game's. These two functions hand over the strings the game itself displays, so the spoken form
// and the printed form agree.
//
// Both are called on the CDO and answer in NativeResult, like the rest of the native-callable
// surface. NativeResult lives here rather than in a caller-specific class because both answers are
// plain localized lookups with no state behind them.


// key=value pairs, VOCAB_SEP-joined. Keys are the markup token native actually sees: an <img>
// tag's src for a glyph, or "KEY:NAME" for a %KEY:NAME% token. Keyed rather than positional so a
// missing entry degrades to native's built-in word instead of shifting the whole table.
//
// ⚠️ %KEY:LMB% is the one that MUST be here. X2MissionNarrativeTemplate.KeyBindingMarkups resolves
// only RMB, ENTER, TAB, Q, E and P; %KEY:LMB% is substituted solely by
// UIUtilities_Input.InsertPCIcons, which the blade path never calls — so it reaches speech RAW
// ("Нажмите [ENTER] или %KEY:LMB% на кнопке действия", observed live 2026-08-20).
function GetInputVocabulary()
{
	local string Result;

	Result = Pair("lmb_icon", eLKN_LeftMouseButton);
	Result $= VOCAB_SEP $ Pair("rmb_icon", eLKN_RightMouseButton);
	Result $= VOCAB_SEP $ Pair("PC_mouseWheelClick", eLKN_MiddleMouseButton);
	Result $= VOCAB_SEP $ Pair("PC_mouseLeft4", eLKN_ThumbMouseButton);
	Result $= VOCAB_SEP $ Pair("PC_mouseLeft5", eLKN_ThumbMouseButton2);
	Result $= VOCAB_SEP $ Pair("PC_arrowLEFT", eLKN_Left);
	Result $= VOCAB_SEP $ Pair("PC_arrowRIGHT", eLKN_Right);
	Result $= VOCAB_SEP $ Pair("PC_arrowUP", eLKN_Up);
	Result $= VOCAB_SEP $ Pair("PC_arrowDOWN", eLKN_Down);

	Result $= VOCAB_SEP $ Pair("KEY:LMB", eLKN_LeftMouseButton);
	Result $= VOCAB_SEP $ Pair("KEY:RMB", eLKN_RightMouseButton);
	Result $= VOCAB_SEP $ Pair("KEY:ENTER", eLKN_Enter);
	Result $= VOCAB_SEP $ Pair("KEY:TAB", eLKN_Tab);

	// PC_mouseWheelScroll has no single enum entry (the game names the two directions separately),
	// so it is deliberately absent and native keeps its own word for it.

	NativeResult = Result;
}

// KeyName is an index into XComKeybindingData's LocalizedKeyNames enum. Typed as int rather
// than the enum itself so this class needs no dependson on a UI class it otherwise ignores;
// the eLKN_* literals are visible package-wide, exactly as UIUtilities_Input uses them.
private function string Pair(string Token, int KeyName)
{
	return Token $ "=" $ class'XComKeybindingData'.default.m_arrLocalizedKeyNames[KeyName];
}

// The result words the game floats over a unit when an attack resolves, VOCAB_SEP-joined.
//
// Native suppresses a world flyover matching one of these while the replay narrator is running,
// because that narrator already says the result WITH attribution ("Osei: Fire at ADVENT Trooper,
// Missed.") where the flyover is a bare word floating over someone. Note a miss arrives as an
// ordinary world message, not a damage one: UIWorldMessageMgr.QueueWorldMessage only stamps
// eHUDMessage_Damage when m_iDamagePrimary > 0, and a miss carries zero.
function GetResultFlyovers()
{
	local string Result;
	local int i;

	Result = class'XLocalizedData'.default.MissedMessage;

	for (i = 0; i < ArrayCount(class'X2TacticalGameRulesetDataStructures'.default.m_aAbilityHitResultStrings); i++)
	{
		if (class'X2TacticalGameRulesetDataStructures'.default.m_aAbilityHitResultStrings[i] != "")
			Result $= VOCAB_SEP $ class'X2TacticalGameRulesetDataStructures'.default.m_aAbilityHitResultStrings[i];
	}

	NativeResult = Result;
}
