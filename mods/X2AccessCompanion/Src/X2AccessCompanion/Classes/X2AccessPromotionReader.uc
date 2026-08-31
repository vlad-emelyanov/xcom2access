// X2AccessPromotionReader
// The promotion / level-up tree — the words half. Same contract as X2AccessPersonnelReader and
// X2AccessCommodityReader: the C++ layer owns the gate, the keys and the speech, calls an entry
// point here on this class's default object via ProcessEvent from the graph recipe's build, and
// reads NativeResult back through reflection right after the call returns. No listeners, no
// timers, no polling.
//
// THREE SCREENS, ONE READER. ShowPromotionUI picks between UIArmory_Promotion (the 2-wide rank
// list), UIArmory_PromotionHero (7 rank columns x 4 branches, bought with AP) and
// UIArmory_PromotionPsiOp (the classic layout, trained rather than chosen). Hero and PsiOp both
// EXTEND UIArmory_Promotion, so one cast covers all three and the differences are two booleans.
//
// ⛔ THE ROWS RETAIN ONLY FNames. UIArmory_PromotionItem keeps AbilityName1/AbilityName2 and
// UIArmory_PromotionHeroColumn an AbilityNames array; every visible word — the ability's name,
// its description, the rank heading, the branch heading, the AP cost — is resolved into a local
// and pushed through MC.BeginFunctionOp. Same house style as the personnel rows.
//
// ⛔ A LOCKED RANK'S ABILITY NAMES MUST NOT BE SPOKEN, and this is the reason the names are
// resolved HERE rather than natively. The widgets hold the real FName at every rank, but
// PopulateData / UpdateAbilityIcons substitute UnknownAbilityIcon and m_strAbilityLockedTitle
// for anything at `Rank >= Unit.GetRank()`. A sighted player cannot read those names, so neither
// may we — sighted parity cuts both ways, and here the data is sitting right there.
//
// ⛔ NO Caps(). Every screen calls Caps() on names before handing them to Flash; that is a visual
// style, not information, and a screen reader spells all-caps words out letter by letter.
//
// ⛔ RANK IS AN ABILITY-TREE INDEX. Both screens set row.Rank = i and draw GetRankName(i + 1):
// ClassRowItem.Rank is 0 while showing "Squaddie". GetRankAbilities() takes the tree index, so
// every `rank` on the wire is that index and the +1 happens only when naming the row.
//
// ⛔ Descriptions are EXPANDED, never raw. GetMyLongDescription substitutes the unit's own
// numbers; LocLongDescription read straight off the template still has its tags in it.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessPromotionReader extends Object
	dependson(UIArmory_Promotion);

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The wire format: records joined by REC_SEP, fields within a record by FLD_SEP, each field
// key=value. PRINTABLE separators, matching every other reader's.
const REC_SEP = "~|~";
const FLD_SEP = "~:~";

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// Records:
//   kind=screen  hero  psi  class  classdesc  unit  header  sharedap  soldierap
//                aplabel  sharedlabel  soldierlabel  combatintel  costlabel
//   kind=cols    col=<branch 0 heading>  col=<branch 1 heading> ...   (empties KEPT)
//   kind=row     rank=<tree index>  name=<rank heading>  promote=1
//   kind=cell    rank  branch  name  state  cost  desc  prereq  exclusive
//   kind=perk    name  desc                       (the ability-summary strip along the bottom)
//
// ⛔ `col` is a REPEATING field and so is emitted INCLUDING its empty values — dropping one
// would shift every heading after it onto the wrong branch. `cell` records carry rank and
// branch explicitly, so they are addressed rather than positional and the ones the tree does
// not fill are simply absent.
function DescribePromotion()
{
	local UIArmory_Promotion Screen;
	local XComGameState_Unit Unit;

	NativeResult = "";

	// GetScreen matches subclasses, and unlike GetCurrentScreen it still answers while the
	// confirmation dialog this screen raises is sitting on top of it.
	Screen = UIArmory_Promotion(`SCREENSTACK.GetScreen(class'UIArmory_Promotion'));
	if (Screen == none)
		return;

	Unit = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(Screen.UnitReference.ObjectID));
	if (Unit == none)
		return;

	AppendScreen(Screen, Unit);
	AppendColumns(Screen, Unit);
	AppendTree(Screen, Unit);
	AppendSummary(Unit);
}

// ---------------------------------------------------------------------------------------
// Screen-wide facts
// ---------------------------------------------------------------------------------------

private function AppendScreen(UIArmory_Promotion Screen, XComGameState_Unit Unit)
{
	local UIArmory_PromotionHero Hero;
	local string Rec;

	Hero = UIArmory_PromotionHero(Screen);

	Rec = "kind=screen";
	if (Hero != none)
		Rec $= FLD_SEP $ "hero=1";
	if (UIArmory_PromotionPsiOp(Screen) != none)
		Rec $= FLD_SEP $ "psi=1";

	Rec $= FLD_SEP $ "class=" $ Unit.GetSoldierClassDisplayName();
	Rec $= FLD_SEP $ "classdesc=" $ Unit.GetSoldierClassSummary();
	Rec $= FLD_SEP $ "unit=" $ Unit.GetName(eNameType_RankFull);

	// The screen's own heading, and it is state rather than decoration: PopulateData swaps
	// m_strAbilityHeader for m_strSelectAbility exactly when there is a pick waiting.
	if (Unit.GetRank() != 1 && Unit.HasAvailablePerksToAssign())
		Rec $= FLD_SEP $ "header=" $ Screen.m_strSelectAbility;
	else
		Rec $= FLD_SEP $ "header=" $ Screen.m_strAbilityHeader;

	if (Hero != none)
	{
		// The AP wallet, which on this screen is the whole economy: an ability costs from the
		// soldier's own points first and the shared pool after.
		Rec $= FLD_SEP $ "sharedap=" $ string(Hero.GetSharedAbilityPoints());
		Rec $= FLD_SEP $ "soldierap=" $ string(Unit.AbilityPoints);
		Rec $= FLD_SEP $ "sharedlabel=" $ Hero.m_strSharedAPLabel;
		Rec $= FLD_SEP $ "soldierlabel=" $ Hero.m_strSoldierAPLabel;
		Rec $= FLD_SEP $ "aplabel=" $ Hero.m_strAPLabel;
		Rec $= FLD_SEP $ "costlabel=" $ Hero.m_strCostLabel;
		// Combat intelligence sets how fast this soldier earns AP, so it belongs beside them.
		Rec $= FLD_SEP $ "combatintel=" $ Unit.GetCombatIntelligenceLabel();
	}

	Emit(Rec);
}

// The branch headings the screen draws once across the top. The hero screen has four
// (AbilityTreeTitles); the classic one has the two older single-value fields.
private function AppendColumns(UIArmory_Promotion Screen, XComGameState_Unit Unit)
{
	local X2SoldierClassTemplate ClassTemplate;
	local string Rec;
	local int i;

	ClassTemplate = Unit.GetSoldierClassTemplate();
	if (ClassTemplate == none)
		return;

	Rec = "kind=cols";

	if (UIArmory_PromotionHero(Screen) != none)
	{
		// ⛔ Emitted INCLUDING empties: a class whose third tree has no title must still occupy
		// the third slot, or every heading after it lands on the wrong branch.
		for (i = 0; i < ClassTemplate.AbilityTreeTitles.Length; i++)
			Rec $= FLD_SEP $ "col=" $ ClassTemplate.AbilityTreeTitles[i];
	}
	else
	{
		Rec $= FLD_SEP $ "col=" $ ClassTemplate.LeftAbilityTreeTitle;
		Rec $= FLD_SEP $ "col=" $ ClassTemplate.RightAbilityTreeTitle;
	}

	Emit(Rec);
}

// ---------------------------------------------------------------------------------------
// The tree
// ---------------------------------------------------------------------------------------

// One row per rank in the UNIT's own tree, and one cell per ability in it. Native decides which
// of these rows the screen is actually drawing; matching is by the `rank` field rather than by
// position, so a row the screen omits is simply never claimed.
private function AppendTree(UIArmory_Promotion Screen, XComGameState_Unit Unit)
{
	local array<SoldierClassAbilityType> AbilityTree;
	local string Rec;
	local int Rank, Branch;

	for (Rank = 0; Rank < Unit.AbilityTree.Length; Rank++)
	{
		AbilityTree = Unit.GetRankAbilities(Rank);

		Rec = "kind=row";
		Rec $= FLD_SEP $ "rank=" $ string(Rank);
		// The rank the row DRAWS is one above its tree index — see the header note.
		Rec $= FLD_SEP $ "name=" $ Unit.GetSoldierRankName(Rank + 1);
		// "A choice is waiting at this rank", which the screen carries as a highlight and the
		// m_strNewRank banner. Only meaningful on the classic screen, where the pick is free.
		if (Rank == Unit.GetRank() - 1 && Unit.HasAvailablePerksToAssign())
			Rec $= FLD_SEP $ "promote=1";
		Emit(Rec);

		for (Branch = 0; Branch < AbilityTree.Length; Branch++)
			AppendCell(Screen, Unit, Rank, Branch, AbilityTree[Branch].AbilityName);
	}
}

private function AppendCell(UIArmory_Promotion Screen, XComGameState_Unit Unit,
	int Rank, int Branch, name AbilityName)
{
	local X2AbilityTemplateManager Manager;
	local X2AbilityTemplate Template;
	local UIArmory_PromotionHero Hero;
	local string Rec;
	local bool bLocked;

	Manager = class'X2AbilityTemplateManager'.static.GetAbilityTemplateManager();
	Template = Manager.FindAbilityTemplate(AbilityName);
	if (Template == none)
		return; // an empty slot in a ragged tree — the screen hides its icon too

	Hero = UIArmory_PromotionHero(Screen);
	bLocked = (Rank >= Unit.GetRank());

	Rec = "kind=cell";
	Rec $= FLD_SEP $ "rank=" $ string(Rank);
	Rec $= FLD_SEP $ "branch=" $ string(Branch);

	if (bLocked)
	{
		// ⛔ The game's own placeholder, not the template's name. See the header note.
		Rec $= FLD_SEP $ "name=" $ Screen.m_strAbilityLockedTitle;
		Rec $= FLD_SEP $ "state=locked";
		Rec $= FLD_SEP $ "desc=" $ Screen.m_strAbilityLockedDescription;
		Emit(Rec);
		return;
	}

	Rec $= FLD_SEP $ "name=" $ Template.LocFriendlyName;
	Rec $= FLD_SEP $ "state=" $ CellState(Screen, Hero, Unit, Rank, Branch, AbilityName);

	// Cost is a hero-screen fact. The classic screen's rank pick is free, so a cost there would
	// be a number that means nothing.
	if (Hero != none && !Unit.HasSoldierAbility(AbilityName))
		Rec $= FLD_SEP $ "cost=" $ string(Hero.GetAbilityPointCost(Rank, Branch));

	if (Template.HasLongDescription())
		Rec $= FLD_SEP $ "desc=" $ Template.GetMyLongDescription(, Unit);

	AppendPrerequisites(Rec, Template, Hero, Unit);

	Emit(Rec);
}

// owned / available / unaffordable / prereq / spent. The hero screen decides with its own
// CanPurchaseAbility (rank, affordability and prerequisites all at once); the classic screen has
// no cost, so the only question is whether this rank still has its free pick.
private function string CellState(UIArmory_Promotion Screen, UIArmory_PromotionHero Hero,
	XComGameState_Unit Unit, int Rank, int Branch, name AbilityName)
{
	if (Unit.HasSoldierAbility(AbilityName))
		return "owned";

	if (Hero != none)
	{
		if (Hero.CanPurchaseAbility(Rank, Branch, AbilityName))
			return "available";
		// ⛔ Both of these read as eUIPromotionState_Normal on screen and are told apart ONLY by
		// the icon's foreground colour, which is exactly the channel speech loses. Splitting
		// them here is what stops an unaffordable ability sounding buyable.
		if (!Unit.MeetsAbilityPrerequisites(AbilityName))
			return "prereq";
		return "unaffordable";
	}

	// Classic: the free pick belongs to the most recent unassigned rank and to no other.
	if (Rank == Unit.GetRank() - 1 && Unit.HasAvailablePerksToAssign())
		return "available";
	return "spent";
}

// The prerequisite warning PreviewAbility puts above the description, plus the Highlander's
// mutually-exclusive variant. Both are load-bearing when choosing a branch: one says "you cannot
// take this yet", the other "taking this closes that off".
private function AppendPrerequisites(out string Rec, X2AbilityTemplate Template,
	UIArmory_PromotionHero Hero, XComGameState_Unit Unit)
{
	local X2AbilityTemplateManager Manager;
	local X2AbilityTemplate PrereqTemplate;
	local string Missing, Exclusive, Prefix;
	local name PrereqName;

	if (Hero == none || Template.PrerequisiteAbilities.Length == 0)
		return;
	if (Unit.HasSoldierAbility(Template.DataName))
		return;

	Manager = class'X2AbilityTemplateManager'.static.GetAbilityTemplateManager();
	Prefix = class'UIArmory_PromotionHero'.default.MutuallyExclusivePrefix;

	foreach Template.PrerequisiteAbilities(PrereqName)
	{
		// Highlander issue #128 encodes "cannot be taken alongside" as a NOT_-prefixed entry in
		// the same array. Matched by prefix exactly as UIArmory_PromotionHero.PreviewAbility does.
		if (Prefix != "" && InStr(string(PrereqName), Prefix, , true) == 0)
		{
			PrereqTemplate = Manager.FindAbilityTemplate(
				name(Mid(string(PrereqName), Len(Prefix))));
			if (PrereqTemplate != none)
			{
				if (Exclusive != "")
					Exclusive $= ", ";
				Exclusive $= PrereqTemplate.LocFriendlyName;
			}
			continue;
		}

		PrereqTemplate = Manager.FindAbilityTemplate(PrereqName);
		if (PrereqTemplate != none && !Unit.HasSoldierAbility(PrereqName))
		{
			if (Missing != "")
				Missing $= ", ";
			Missing $= PrereqTemplate.LocFriendlyName;
		}
	}

	// The game's own label carries the sentence ("Requires:"), so the value alone would be a
	// bare list of names with nothing saying what they are.
	if (Missing != "")
		Rec $= FLD_SEP $ "prereq=" $ Hero.m_strPrereqAbility @ Missing;
	if (Exclusive != "")
		Rec $= FLD_SEP $ "exclusive=" $ Hero.m_strMutuallyExclusiveAbility @ Exclusive;
}

// ---------------------------------------------------------------------------------------
// The ability-summary strip
// ---------------------------------------------------------------------------------------

// Everything the soldier already has, drawn along the bottom of every armory screen. This is
// sighted parity rather than an aid: PopulateAbilitySummary puts it on screen, so it earns a
// stop. ⛔ bDontDisplayInAbilitySummary is the game's own "not worth showing" flag and is
// honoured here for the same reason.
private function AppendSummary(XComGameState_Unit Unit)
{
	local array<SoldierClassAbilityType> Earned;
	local X2AbilityTemplateManager Manager;
	local X2AbilityTemplate Template;
	local string Rec;
	local int i;

	if (Unit.GetRank() == 0)
		return; // the game hides the whole list for a rookie

	Manager = class'X2AbilityTemplateManager'.static.GetAbilityTemplateManager();
	Earned = Unit.GetEarnedSoldierAbilities();

	for (i = 0; i < Earned.Length; i++)
	{
		Template = Manager.FindAbilityTemplate(Earned[i].AbilityName);
		if (Template == none || Template.bDontDisplayInAbilitySummary)
			continue;

		Rec = "kind=perk";
		Rec $= FLD_SEP $ "name=" $ Template.LocFriendlyName;
		if (Template.HasLongDescription())
			Rec $= FLD_SEP $ "desc=" $ Template.GetMyLongDescription(, Unit);
		Emit(Rec);
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
