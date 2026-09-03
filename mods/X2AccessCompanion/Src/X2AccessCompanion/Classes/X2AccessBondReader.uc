// X2AccessBondReader
// The soldier bond screens — the words half of UISoldierBondScreen and UISoldierBondConfirmScreen,
// plus the verbs vanilla leaves on the mouse. Same contract as X2AccessPersonnelReader: C++ owns
// the gate, the keys and the speech, calls an entry point here on this class's default object via
// ProcessEvent from the recipe's build, and reads NativeResult back right after the call returns.
//
// ⛔ THE ROWS RETAIN ONLY TWO REFS. UISoldierBondListItem extends UIButton but its UpdateData
// resolves everything into locals and pushes them through MC.BeginFunctionOp("UpdateData"),
// bypassing SetText — Text is EMPTY on a row visibly showing a soldier. What the row keeps is
// UnitRef and ScreenUnitRef, so every cell below is re-derived from the pair exactly the way
// UpdateData derives it: rank abbreviation, class, compatibility word, cohesion as the fraction
// the bar is filled to. The header (SetPlayerInfo / SetBondMateInfo) is re-derived from the
// screen's own UnitRef and the HasSoldierBond call RefreshHeader makes.
//
// WHAT VANILLA LEAVES UNREACHABLE on mouse+keyboard, and what this class hands back:
//   * CREATE BOND — the row's ActivateBondButton calls DisableNavigation(); OnConfirmBond is its
//     mouse handler or FXS_BUTTON_A. ConfirmBond(int) calls the screen's own OnConfirmBond, whose
//     CanHaveBondAtLevel guard stays the game's.
//   * Sorting — FXS_DPAD_LEFT/RIGHT and FXS_BUTTON_L3, controller constants all. ClickBondSort
//     presses the header the way a mouse does (X2AccessPersonnelReader's route, for its reasons).
//   * Switch-to-soldier IS on Enter in vanilla and the cycle is Tab / LeftShift — both gated on
//     !IsInSquadScreen(), which is the screen's own function and is asked, never re-derived.
//
// ⛔ NO Caps(). The rows and the header upper-case names for Flash; a reader spelling out all-caps
// words turns every soldier into an acronym.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

// dependson: SoldierBond lives inside X2StrategyGameRulesetDataStructures, ESoldierBondSortType
// inside UISoldierBondScreen and EAdjacencyRequirement inside X2Condition_Bondmate — none in a
// global header, so a class in another package needs them in scope by name.
class X2AccessBondReader extends Object
	dependson(X2StrategyGameRulesetDataStructures, UISoldierBondScreen, X2Condition_Bondmate);

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The wire format's field separator (see X2AccessWire); records are appended through it.
const FLD_SEP = "~:~";

// ---------------------------------------------------------------------------------------
// UISoldierBondScreen
// ---------------------------------------------------------------------------------------

// Records, in the order the recipe wants stops:
//   kind=header   title subtitle bondlabel     (subtitle = the soldier, so the screen is named with them)
//   kind=soldier  rank name cycle=1 switch=1    (cycle/switch: the game's own gates for those verbs)
//   kind=bondmate title level name nick rank class compat cohesion   (title alone = NoBond)
//   kind=sort     index label selected=1 desc=1 (the header's own UIFlipSortButtons)
//   kind=cols     col col col col col           (RANK, CLASS, COMPATIBILITY, COHESION, "" — positional)
//   kind=row      index name cell cell cell cell (positional, empties KEPT; cohesion is a bare percentage)
//
// Every index is the GAME's — the list's own item index — because that is what SetSelectedIndex
// and our own verbs are expressed in.
function DescribeBonds()
{
	local UISoldierBondScreen Screen;
	local XComGameState_Unit Unit;

	NativeResult = "";

	Screen = UISoldierBondScreen(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;

	// The one history lookup the screen itself makes in RefreshHeader, for the same ref.
	Unit = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(Screen.UnitRef.ObjectID));

	AppendHeader(Screen, Unit);
	AppendSoldier(Screen, Unit);
	AppendBondmate(Screen, Unit);
	AppendSortColumns(Screen);
	AppendColumns(Screen);
	AppendRows(Screen, Unit);
}

private function AppendHeader(UISoldierBondScreen Screen, XComGameState_Unit Unit)
{
	local string Rec;

	Rec = "kind=header";
	class'X2AccessWire'.static.AddField(Rec, "title", Screen.BondTitle);
	if (Unit != none)
		class'X2AccessWire'.static.AddField(Rec, "subtitle", RankName(Unit) @ Unit.GetName(eNameType_FullNick));
	// The CREATE BOND label is the row class's own localized string, sent once here rather than
	// once per row.
	class'X2AccessWire'.static.AddField(Rec, "bondlabel", class'UISoldierBondListItem'.default.BondButtonLabel);
	Emit(Rec);
}

// RefreshHeader's SetPlayerInfo: the rank name and the full name with nickname. The two gates
// ride along because the recipe's soldier node is a spinner only while the game itself would
// offer the cycle (UpdateNavHelp's own test), and Enter switches only where vanilla's does.
private function AppendSoldier(UISoldierBondScreen Screen, XComGameState_Unit Unit)
{
	local string Rec;
	local bool bSquadScreen;

	if (Unit == none)
		return;

	bSquadScreen = Screen.IsInSquadScreen();

	Rec = "kind=soldier";
	class'X2AccessWire'.static.AddField(Rec, "rank", RankName(Unit));
	class'X2AccessWire'.static.AddField(Rec, "name", Unit.GetName(eNameType_FullNick));
	if (!bSquadScreen
		&& class'UIUtilities_Strategy'.static.HasSoldiersToCycleThrough(Screen.UnitRef, CanCycleTo))
		Rec $= FLD_SEP $ "cycle=1";
	if (!bSquadScreen)
		Rec $= FLD_SEP $ "switch=1";
	Emit(Rec);
}

// The eligibility delegate HasSoldiersToCycleThrough wants, answering with the screen's own
// static test rather than a copy of it.
function bool CanCycleTo(XComGameState_Unit Unit)
{
	return class'UISoldierBondScreen'.static.CanCycleTo(Unit);
}

// RefreshHeader's SetBondMateInfo, or NoBond with the panel hidden (level -1 hides it).
private function AppendBondmate(UISoldierBondScreen Screen, XComGameState_Unit Unit)
{
	local XComGameState_Unit Bondmate;
	local StateObjectReference BondmateRef;
	local SoldierBond BondData;
	local X2SoldierClassTemplate SoldierClass;
	local string Rec;

	Rec = "kind=bondmate";

	if (Unit != none && Unit.HasSoldierBond(BondmateRef, BondData))
	{
		Bondmate = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(BondmateRef.ObjectID));
		if (Bondmate != none)
		{
			SoldierClass = Bondmate.GetSoldierClassTemplate();

			class'X2AccessWire'.static.AddField(Rec, "title", Screen.BondMateTitle);
			class'X2AccessWire'.static.AddField(Rec, "level", string(BondData.BondLevel));
			class'X2AccessWire'.static.AddField(Rec, "name", Bondmate.GetName(eNameType_Full));
			class'X2AccessWire'.static.AddField(Rec, "nick", Bondmate.GetName(eNameType_Nick));
			class'X2AccessWire'.static.AddField(Rec, "rank", RankAbbrev(Bondmate));
			class'X2AccessWire'.static.AddField(Rec, "class", SoldierClass != none ? SoldierClass.DisplayName : "");
			class'X2AccessWire'.static.AddField(Rec, "compat",
				class'X2StrategyGameRulesetDataStructures'.static.GetSoldierCompatibilityLabel(BondData.Compatibility));
			class'X2AccessWire'.static.AddField(Rec, "cohesion", string(CohesionPercent(BondData)));
			Emit(Rec);
			return;
		}
	}

	class'X2AccessWire'.static.AddField(Rec, "title", Screen.NoBond);
	Emit(Rec);
}

// UpdateData's own bar arithmetic — cohesion over the NEXT level's threshold — as a whole
// percentage, so the bar the game draws becomes one number. Clamped to the bar: past the
// threshold the level-up is available and the bar is simply full.
private function int CohesionPercent(SoldierBond BondData)
{
	local array<int> Thresholds;
	local float Max;

	Thresholds = class'X2StrategyGameRulesetDataStructures'.default.CohesionThresholds;
	if (Thresholds.Length == 0)
		return 0;

	Max = float(Thresholds[Clamp(BondData.BondLevel + 1, 0, Thresholds.Length - 1)]);
	if (Max <= 0)
		return 0;

	return Clamp(Round(float(BondData.Cohesion) / Max * 100.0), 0, 100);
}

// The sort header's five UIFlipSortButtons, which RETAIN everything worth speaking — Label, plus
// bArrowVisible for "this is the sort column" and bArrowFlipped for its direction. So unlike the
// rows, no re-derivation is needed here.
private function AppendSortColumns(UISoldierBondScreen Screen)
{
	local array<UIPanel> Buttons;
	local UIFlipSortButton SortButton;
	local string Rec;
	local int i;

	if (Screen.m_kSoldierSortHeader == none)
		return;

	Screen.m_kSoldierSortHeader.GetChildrenOfType(class'UIFlipSortButton', Buttons);

	for (i = 0; i < Buttons.Length; i++)
	{
		SortButton = UIFlipSortButton(Buttons[i]);
		if (SortButton == none || SortButton.Label == "")
			continue;

		Rec = "kind=sort" $ FLD_SEP $ "index=" $ string(i);
		class'X2AccessWire'.static.AddField(Rec, "label", SortButton.Label);
		if (SortButton.bArrowVisible)
		{
			Rec $= FLD_SEP $ "selected=1";
			if (SortButton.bArrowFlipped)
				Rec $= FLD_SEP $ "desc=1";
		}
		Emit(Rec);
	}
}

// THE ROSTER IS A TABLE, and native builds it as one (GraphSheet): column 0 is the soldier and
// every other column is one fact under a heading. So the wire is positional — one `col=` per
// heading, then one `cell=` per row in the SAME ORDER, EMPTY VALUES INCLUDED. The headings are
// the game's own sort labels, in the order the row draws its cells, plus a BLANK fifth for the
// CREATE BOND column the screen draws no heading over.
private function AppendColumns(UISoldierBondScreen Screen)
{
	local string Rec;

	Rec = "kind=cols";
	class'X2AccessWire'.static.AddPositionalField(Rec, "col", Screen.m_strButtonLabels[eSoldierBondSortType_Rank]);
	class'X2AccessWire'.static.AddPositionalField(Rec, "col", Screen.m_strButtonLabels[eSoldierBondSortType_Class]);
	class'X2AccessWire'.static.AddPositionalField(Rec, "col", Screen.m_strButtonLabels[eSoldierBondSortType_Compatibility]);
	class'X2AccessWire'.static.AddPositionalField(Rec, "col", Screen.m_strButtonLabels[eSoldierBondSortType_Cohesion]);
	class'X2AccessWire'.static.AddPositionalField(Rec, "col", "");
	Emit(Rec);
}

// UISoldierBondListItem.UpdateData, re-derived per row from the two refs it kept. Whether the
// CREATE BOND button is showing is native's read (the button's own visibility), so it is not
// repeated here.
private function AppendRows(UISoldierBondScreen Screen, XComGameState_Unit ScreenUnit)
{
	local UISoldierBondListItem Row;
	local XComGameState_Unit Unit;
	local X2SoldierClassTemplate SoldierClass;
	local SoldierBond BondData;
	local string Rec;
	local int i;

	if (Screen.List == none || ScreenUnit == none)
		return;

	for (i = 0; i < Screen.List.ItemCount; i++)
	{
		Row = UISoldierBondListItem(Screen.List.GetItem(i));
		if (Row == none)
			continue;

		Unit = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(Row.UnitRef.ObjectID));
		if (Unit == none)
			continue;

		ScreenUnit.GetBondData(Row.UnitRef, BondData);
		SoldierClass = Unit.GetSoldierClassTemplate();

		Rec = "kind=row" $ FLD_SEP $ "index=" $ string(i);
		class'X2AccessWire'.static.AddField(Rec, "name", Unit.GetName(eNameType_FullNick));
		class'X2AccessWire'.static.AddPositionalField(Rec, "cell", RankAbbrev(Unit));
		class'X2AccessWire'.static.AddPositionalField(Rec, "cell", SoldierClass != none ? SoldierClass.DisplayName : "");
		class'X2AccessWire'.static.AddPositionalField(Rec, "cell",
			class'X2StrategyGameRulesetDataStructures'.static.GetSoldierCompatibilityLabel(BondData.Compatibility));
		class'X2AccessWire'.static.AddPositionalField(Rec, "cell", string(CohesionPercent(BondData)));
		Emit(Rec);
	}
}

// `GET_RANK_STR and `GET_RANK_ABBRV — the header's and the row's rank forms. Guarded on the class
// as X2AccessPersonnelReader is: the generic ladder answers when there is none.
private function string RankName(XComGameState_Unit Unit)
{
	local X2SoldierClassTemplate SoldierClass;

	SoldierClass = Unit.GetSoldierClassTemplate();
	return class'X2ExperienceConfig'.static.GetRankName(Unit.GetRank(),
		SoldierClass != none ? SoldierClass.DataName : '');
}

private function string RankAbbrev(XComGameState_Unit Unit)
{
	local X2SoldierClassTemplate SoldierClass;

	SoldierClass = Unit.GetSoldierClassTemplate();
	return class'X2ExperienceConfig'.static.GetShortRankName(Unit.GetRank(),
		SoldierClass != none ? SoldierClass.DataName : '');
}

// ---------------------------------------------------------------------------------------
// Verbs — UISoldierBondScreen
// ---------------------------------------------------------------------------------------

// The row's CREATE BOND, through the screen's own OnConfirmBond — whose CanHaveBondAtLevel guard
// decides, so a row the game would refuse is refused here too, as silently as its mouse click.
function ConfirmBond(int Index)
{
	local UISoldierBondScreen Screen;
	local UISoldierBondListItem Row;

	NativeResult = "";

	Screen = UISoldierBondScreen(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none || Screen.List == none || Index < 0 || Index >= Screen.List.ItemCount)
		return;

	Row = UISoldierBondListItem(Screen.List.GetItem(Index));
	if (Row == none)
		return;

	Screen.OnConfirmBond(Row);
}

// Vanilla's Enter: the whole page re-pointed at the row's soldier (and every armory screen under
// it, through CycleToSoldier). Gated exactly as vanilla gates it.
function SwitchSoldier(int Index)
{
	local UISoldierBondScreen Screen;
	local UISoldierBondListItem Row;

	NativeResult = "";

	Screen = UISoldierBondScreen(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none || Screen.List == none || Index < 0 || Index >= Screen.List.ItemCount)
		return;
	if (Screen.IsInSquadScreen())
		return;

	Row = UISoldierBondListItem(Screen.List.GetItem(Index));
	if (Row == none)
		return;

	Screen.OnSelectSoldier(Row.UnitRef);
}

// Vanilla's Tab / LeftShift. Same gate.
function CycleSoldier(int Direction)
{
	local UISoldierBondScreen Screen;

	NativeResult = "";

	Screen = UISoldierBondScreen(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none || Screen.IsInSquadScreen())
		return;

	if (Direction > 0)
		Screen.NextSoldier();
	else
		Screen.PrevSoldier();
}

// Press a sort header the way a mouse does. ⛔ Through UIFlipSortButton.OnMouseEvent rather than
// SetSortType: that handler deselects every other header, FLIPS the direction when the column was
// already selected, and calls RefreshData() on the screen afterwards.
function ClickBondSort(int Index)
{
	local array<UIPanel> Buttons;
	local array<string> NoArgs;
	local UISoldierBondScreen Screen;

	NativeResult = "";

	Screen = UISoldierBondScreen(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none || Screen.m_kSoldierSortHeader == none)
		return;

	Screen.m_kSoldierSortHeader.GetChildrenOfType(class'UIFlipSortButton', Buttons);
	if (Index < 0 || Index >= Buttons.Length)
		return;

	// OnMouseEvent's args are the Flash payload, which this handler never reads.
	NoArgs.Length = 0;
	Buttons[Index].OnMouseEvent(class'UIUtilities_Input'.const.FXS_L_MOUSE_UP, NoArgs);
}

// ---------------------------------------------------------------------------------------
// UISoldierBondConfirmScreen
// ---------------------------------------------------------------------------------------

// Records:
//   kind=header   title                    (m_strTitle @ level — the screen's own composition)
//   kind=soldier  index rank class name
//   kind=benefits label
//   kind=benefit  index name desc
//   kind=button   index label cmd
function DescribeBondConfirm()
{
	local UISoldierBondConfirmScreen Screen;
	local StateObjectReference NoneRef;
	local SoldierBond BondData;
	local string Rec;

	NativeResult = "";

	Screen = UISoldierBondConfirmScreen(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none || Screen.Soldier1State == none || Screen.Soldier2State == none)
		return;

	// OnInit's own call, blank ref and all: soldier 1's CURRENT bond, level 0 for a first bond,
	// so the number below is the number in the title.
	Screen.Soldier1State.HasSoldierBond(NoneRef, BondData);

	Rec = "kind=header";
	class'X2AccessWire'.static.AddField(Rec, "title", Screen.m_strTitle @ string(BondData.BondLevel + 1));
	Emit(Rec);

	AppendConfirmSoldier(0, Screen.Soldier1State);
	AppendConfirmSoldier(1, Screen.Soldier2State);
	AppendBenefits(Screen, BondData.BondLevel + 1);

	// Enter is ConfirmClicked and Escape is Cancel() — the screen's own OnUnrealCommand. The
	// cancel word is the game's generic one: the screen has none of its own (its back button is a
	// nav-help icon).
	Rec = "kind=button" $ FLD_SEP $ "index=0";
	class'X2AccessWire'.static.AddField(Rec, "label", Screen.m_strConfirm);
	Rec $= FLD_SEP $ "cmd=" $ string(class'UIUtilities_Input'.const.FXS_KEY_ENTER);
	Emit(Rec);

	Rec = "kind=button" $ FLD_SEP $ "index=1";
	class'X2AccessWire'.static.AddField(Rec, "label", class'UIUtilities_Text'.default.m_strGenericCancel);
	Rec $= FLD_SEP $ "cmd=" $ string(class'UIUtilities_Input'.const.FXS_KEY_ESCAPE);
	Emit(Rec);
}

private function AppendConfirmSoldier(int Index, XComGameState_Unit Unit)
{
	local X2SoldierClassTemplate SoldierClass;
	local string Rec;

	SoldierClass = Unit.GetSoldierClassTemplate();

	Rec = "kind=soldier" $ FLD_SEP $ "index=" $ string(Index);
	class'X2AccessWire'.static.AddField(Rec, "rank", RankName(Unit));
	class'X2AccessWire'.static.AddField(Rec, "class", SoldierClass != none ? SoldierClass.DisplayName : "");
	class'X2AccessWire'.static.AddField(Rec, "name", Unit.GetName(eNameType_Full));
	Emit(Rec);
}

// OnInit's own template walk: every ability carrying an X2Condition_Bondmate — among its target
// conditions first, its shooter conditions second — whose MinBondLevel is the level being made
// and whose adjacency rule is EAR_AnyAdjacency. Same loop, same order, so the rows match the
// screen's.
private function AppendBenefits(UISoldierBondConfirmScreen Screen, int NewLevel)
{
	local X2AbilityTemplateManager AbilityMgr;
	local X2DataTemplate DataTemplate;
	local X2AbilityTemplate Ability;
	local X2Condition TestCondition;
	local X2Condition_Bondmate BondmateCondition;
	local string Rec, Description;
	local int i;

	Rec = "kind=benefits";
	class'X2AccessWire'.static.AddField(Rec, "label", Screen.m_strBenefits);
	Emit(Rec);

	i = 0;
	AbilityMgr = class'X2AbilityTemplateManager'.static.GetAbilityTemplateManager();
	foreach AbilityMgr.IterateTemplates(DataTemplate)
	{
		Ability = X2AbilityTemplate(DataTemplate);
		if (Ability == none)
			continue;

		BondmateCondition = none;
		foreach Ability.AbilityTargetConditions(TestCondition)
		{
			BondmateCondition = X2Condition_Bondmate(TestCondition);
			if (BondmateCondition != none)
				break;
		}
		if (BondmateCondition == none)
		{
			foreach Ability.AbilityShooterConditions(TestCondition)
			{
				BondmateCondition = X2Condition_Bondmate(TestCondition);
				if (BondmateCondition != none)
					break;
			}
		}

		if (BondmateCondition == none || BondmateCondition.MinBondLevel != NewLevel
			|| BondmateCondition.RequiresAdjacency != EAR_AnyAdjacency)
			continue;

		// GetUISummary_Ability's own two fields, taken through the same getters it uses — the
		// struct it returns is declared inside an interface in another package, and the two
		// strings are all the row draws. Its "MISSING NAME" debug fallbacks are NOT reproduced:
		// an empty field is dropped by the wire, which is the honest answer for a blank.
		Description = Ability.HasLongDescription() ? Ability.GetMyLongDescription()
													: Ability.GetMyHelpText();

		Rec = "kind=benefit" $ FLD_SEP $ "index=" $ string(i);
		class'X2AccessWire'.static.AddField(Rec, "name", Ability.LocFriendlyName);
		class'X2AccessWire'.static.AddField(Rec, "desc", Description);
		Emit(Rec);
		i++;
	}
}

// ---------------------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------------------

private function Emit(string Rec)
{
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

defaultproperties
{
	NativeResult = ""
}
