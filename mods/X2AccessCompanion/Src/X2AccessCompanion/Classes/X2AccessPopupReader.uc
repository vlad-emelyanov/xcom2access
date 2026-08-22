// X2AccessPopupReader
// The modal popup family — the words half. Same contract as X2AccessRoomReader and
// X2AccessSaveReader: C++ owns the gate, the keys and the speech, calls DescribePopup() on this
// class's default object via ProcessEvent from the graph recipe's build, and reads NativeResult
// back through reflection right after the call returns. No listeners, no timers.
//
// WHAT THIS COVERS. docs/popup-inventory.md §1-3 catalogues every transient/modal surface in the
// game. The ones handled here are the MODALS: a message and up to a couple of buttons, pushed on
// the screen stack, dismissed with Enter or Escape. They have nothing in common structurally —
// different classes, different layouts, different state objects — so this is a switch, one
// branch per class, each re-deriving what its screen resolved and threw into Flash.
//
// ⛔ UIAlert IS NOT HERE, and deliberately. Its 118 variants come from 106 separate
// Build*Alert functions, none of which retain a string; re-deriving them would be thousands of
// lines drifting from the game's own source. Native reads those out of the Flash panel instead
// (GameUI::ReadAlertText; docs/popup-inventory.md §6 has the recovered field map). What THIS
// class supplies for an alert is the three things only script can reach — eAlertName,
// GetLibraryID() and LibraryPanel.MCPath — which is what turns that read from a blind sweep into
// one lookup per field the panel actually has.
//
// ⛔ NO Caps(). Several of these screens upper-case a name before handing it to Flash; that is a
// visual style, not information, and a reader that spells out all-caps words turns every proper
// noun into an acronym. Same rule as X2AccessPersonnelReader's rows.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

// dependson: SoldierBond is declared INSIDE X2StrategyGameRulesetDataStructures and
// UICombatLoseType inside X2TacticalGameRuleset — neither lives in a global header, so a class in
// another package needs both in scope by name. Same reason X2AccessRoomReader dependsons the
// first of them.
class X2AccessPopupReader extends Object
	dependson(X2StrategyGameRulesetDataStructures, X2TacticalGameRuleset);

// Written by the entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The wire format: records joined by REC_SEP, fields within a record by FLD_SEP, each field
// key=value. PRINTABLE separators, matching every other companion reader's.
const REC_SEP = "~|~";
const FLD_SEP = "~:~";

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// Records, in the order the recipe wants stops:
//   kind=popup   class, title                    (always first; `title` names the screen)
//   kind=line    text                            (one per MESSAGE LINE — these are the tab stops)
//   kind=button  index, label, cmd               (cmd is the input constant that presses it)
//   kind=alert   name, symbol, mcpath            (UIAlert only — native reads the words itself)
//
// One line record per LINE rather than one per popup: speech is serial, and a bond alert or a
// Chosen reveal is several paragraphs. Splitting them is what lets a player step through with
// Tab instead of sitting through the whole thing to reach the part they wanted — the same
// argument the commodity card's per-line split makes.
function DescribePopup()
{
	local UIScreen Screen;

	NativeResult = "";

	Screen = `SCREENSTACK.GetCurrentScreen();
	if (Screen == none)
		return;

	switch (Screen.Class.Name)
	{
	case 'UIAlert':
	case 'UIAlert_DLC_Day60':
	case 'UIAlert_DLC_Day90':
		DescribeAlert(UIAlert(Screen));
		break;
	case 'UIFactionPopup':
		DescribeFaction(UIFactionPopup(Screen));
		break;
	case 'UIAbilityPopup':
		DescribeAbility(UIAbilityPopup(Screen));
		break;
	case 'UISoldierBondAlert':
		DescribeBond(UISoldierBondAlert(Screen));
		break;
	case 'UIChosenFragmentRecovered':
		DescribeFragment(UIChosenFragmentRecovered(Screen));
		break;
	case 'UIChosenLevelUp':
		DescribeChosenLevelUp(UIChosenLevelUp(Screen));
		break;
	case 'UIChosenReveal':
		DescribeChosenReveal(UIChosenReveal(Screen));
		break;
	case 'UIUFOAttack':
		DescribeUFOAttack(UIUFOAttack(Screen));
		break;
	case 'UIProgressDialogue':
		DescribeProgress(UIProgressDialogue(Screen));
		break;
	case 'UICombatLose':
		DescribeCombatLose(UICombatLose(Screen));
		break;
	case 'UISitRepInformation':
		DescribeSitRep(UISitRepInformation(Screen));
		break;
	case 'UIAdventOperations':
		DescribeAdventOps(UIAdventOperations(Screen));
		break;
	// ⛔ UIRedScreen gets NO branch and cannot get one: m_strErrorMsg and RedscreenLines are both
	// declared `private`, so no other class can read them. It stays in native's popup table
	// anyway — announcing THAT a script error overlay is up, and offering the key that dismisses
	// it, is worth more than treating the screen as invisible. Reading its text would need a
	// native offset, which this whole family is otherwise free of.
	default:
		// A popup native recognises but this class has no branch for. Still emit the header
		// record: the recipe then announces THAT a modal is up and offers its accept/cancel
		// keys, which beats swallowing the screen entirely.
		PushHeader(string(Screen.Class.Name), "");
		break;
	}
}

// ---------------------------------------------------------------------------------------
// UIAlert — the handles native needs, not the words
// ---------------------------------------------------------------------------------------

// Three facts only script can reach. GetLibraryID() is what picks the Flash panel, so it is also
// what picks the field list; MCPath is the root every field hangs off, composed the way
// UIMCController composes it. Both are cheap and neither touches XCOMHISTORY.
private function DescribeAlert(UIAlert Alert)
{
	local string Rec;

	if (Alert == none)
		return;

	PushHeader("UIAlert", "");

	Rec = "kind=alert";
	Rec $= FLD_SEP $ "name=" $ string(Alert.eAlertName);
	Rec $= FLD_SEP $ "symbol=" $ string(Alert.GetLibraryID());
	if (Alert.LibraryPanel != none)
		Rec $= FLD_SEP $ "mcpath=" $ Alert.LibraryPanel.MCPath;
	Push(Rec);

	// The objective alert is the ONE variant that retains its text outright, in three plain
	// script vars the builder fills before pushing to Flash. Emitting them means the most
	// common tutorial-era alert reads correctly even if the Flash sweep finds nothing.
	if (Alert.ObjectiveUIHeaderText != "")
		PushLine(Alert.ObjectiveUIHeaderText);
	if (Alert.ObjectiveUIBodyText != "")
		PushLine(Alert.ObjectiveUIBodyText);
}

// ---------------------------------------------------------------------------------------
// The bespoke popups
// ---------------------------------------------------------------------------------------

// Faction met / influence gained. The screen keeps FactionRef and bInfluenceIncreased and picks
// among its own localized titles; this mirrors that choice rather than inventing wording.
private function DescribeFaction(UIFactionPopup Popup)
{
	local XComGameState_ResistanceFaction Faction;

	if (Popup == none)
		return;

	Faction = XComGameState_ResistanceFaction(`XCOMHISTORY.GetGameStateForObjectID(Popup.FactionRef.ObjectID));

	PushHeader("UIFactionPopup",
		Popup.bInfluenceIncreased ? Popup.m_strFactionInfluenceGainedTitle : Popup.m_strFactionMetTitle);

	if (Faction != none)
	{
		PushLine(Faction.GetFactionName());
		PushLine(Faction.GetFactionTitle());
	}

	// What the influence actually bought. The screen shows these as a list of unlocks; each is
	// its own line so the player can step through them.
	if (Popup.bInfluenceIncreased)
	{
		PushLine(Popup.m_strFactionOrderSlotUnlocked);
		PushLine(Popup.m_strFactionOrderUnlocked);
		PushLine(Popup.m_strFactionCovertActionUnlocked);
	}
}

// A soldier learned something. UnitRef is all the screen keeps, so the ability comes from the
// unit the way the screen's own populate does.
private function DescribeAbility(UIAbilityPopup Popup)
{
	local XComGameState_Unit Unit;

	if (Popup == none)
		return;

	PushHeader("UIAbilityPopup", "");

	Unit = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(Popup.UnitRef.ObjectID));
	if (Unit != none)
		PushLine(Unit.GetName(eNameType_RankFull));
}

// Two soldiers can bond. Both refs and every string are retained, so this is a straight read —
// including the branch that says WHY the bond cannot be acted on yet (no GTS, no recovery
// centre), which is the part a sighted player reads off a greyed button.
private function DescribeBond(UISoldierBondAlert Popup)
{
	local XComGameState_Unit UnitA, UnitB;
	local SoldierBond BondData;
	local XGParamTag kTag;
	local string Names;

	if (Popup == none)
		return;

	UnitA = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(Popup.UnitRef1.ObjectID));
	UnitB = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(Popup.UnitRef2.ObjectID));

	// ⛔ The screen's own BondLevel var is PRIVATE, so this takes the route it took to fill it:
	// Unit1.GetBondData(UnitRef2, …). Offsetting by one is Firaxis' — "bond level zero is weird
	// to read, player-facing" is their comment — and it is kept so our number matches theirs.
	if (UnitA != none)
	{
		UnitA.GetBondData(Popup.UnitRef2, BondData);
		kTag = XGParamTag(`XEXPANDCONTEXT.FindTag("XGParam"));
		kTag.StrValue0 = string(BondData.BondLevel + 1);
	}

	PushHeader("UISoldierBondAlert", `XEXPAND.ExpandString(Popup.BondAlertTitle));

	if (UnitA != none && UnitB != none)
	{
		// The screen's own joining word, so this reads the same in every language.
		Names = UnitA.GetName(eNameType_RankFull) @ Popup.BondAlertLabelBetweenSoldiers_And
			@ UnitB.GetName(eNameType_RankFull);
		PushLine(Names);
	}

	PushLine(`XEXPAND.ExpandString(Popup.BondAlertDescription));
	PushLine(`XEXPAND.ExpandString(Popup.BondAlertDetails));
}

// A Chosen stronghold fragment. FragmentLevel decides whether this was the LAST one, which is
// the difference between "keep going" and "the stronghold is findable now".
//
// ⛔ The three ParamTag slots are not optional. Every string on this screen carries %-tokens
// that XEXPAND resolves against them, so reading the localized var straight would speak the
// markup — the tutorial reader's bug, in the strategy layer. The slots are filled exactly as
// UIChosenFragmentRecovered.UpdateData fills them, in the same order.
private function DescribeFragment(UIChosenFragmentRecovered Popup)
{
	local XComGameState_AdventChosen Chosen;
	local XComGameState_ResistanceFaction Faction;
	local XGParamTag ParamTag;

	if (Popup == none)
		return;

	Chosen = XComGameState_AdventChosen(`XCOMHISTORY.GetGameStateForObjectID(Popup.ChosenRef.ObjectID));
	if (Chosen != none)
	{
		Faction = Chosen.GetRivalFaction();
		ParamTag = XGParamTag(`XEXPANDCONTEXT.FindTag("XGParam"));
		ParamTag.StrValue0 = Chosen.GetChosenName();
		if (Faction != none)
		{
			ParamTag.StrValue1 = Faction.GetFactionTitle();
			ParamTag.StrValue2 = Faction.GetInfluenceString();
		}
	}

	PushHeader("UIChosenFragmentRecovered", `XEXPAND.ExpandString(Popup.m_strFragmentReceivedTitle));

	// The game's own branch, threshold included (UpdateData: `FragmentLevel < 3 ? … : …`).
	if (Popup.FragmentLevel < 3)
	{
		PushLine(`XEXPAND.ExpandString(Popup.m_strFragmentReceivedNextSteps));
		// ⛔ Only below the threshold. At level 3 the screen passes "" for this row, so speaking
		// it would tell the player their influence rose on the one occasion it did not.
		PushLine(`XEXPAND.ExpandString(Popup.m_strFactionInfluenceIncreased));
	}
	else
	{
		PushLine(`XEXPAND.ExpandString(Popup.m_strFinalFragmentReceived));
	}
}

// A Chosen levelled up. The strengths it gained are the whole point of the popup.
private function DescribeChosenLevelUp(UIChosenLevelUp Popup)
{
	local XComGameState_AdventChosen Chosen;
	local XGParamTag ParamTag;

	if (Popup == none)
		return;

	Chosen = XComGameState_AdventChosen(`XCOMHISTORY.GetGameStateForObjectID(Popup.ChosenRef.ObjectID));
	if (Chosen != none)
	{
		// UpdateData's slot: the Chosen's CLASS name, not its personal name — the body text
		// reads "the %s has grown stronger", so the wrong one here produces a wrong sentence.
		ParamTag = XGParamTag(`XEXPANDCONTEXT.FindTag("XGParam"));
		ParamTag.StrValue0 = Chosen.GetChosenClassName();
	}

	PushHeader("UIChosenLevelUp", `XEXPAND.ExpandString(Popup.ChosenLevelUpTitle));

	if (Chosen != none)
		PushLine(Chosen.GetChosenName());

	PushLine(`XEXPAND.ExpandString(Popup.ChosenLevelUpBody));
	PushLine(`XEXPAND.ExpandString(Popup.StrengthsLabel));
	PushLine(`XEXPAND.ExpandString(Popup.AbilityList));
}

// The tactical first-encounter reveal: who this Chosen is, what it is strong against, what it is
// weak to, and which faction hunts it. Strengths and weaknesses are what a player plans the whole
// mission around.
//
// ⛔ An UNKNOWN trait is INFORMATION, not a gap to skip. The screen shows a question mark and the
// "unknown" wording for any trait whose AbilityRevealEvent has not fired yet — so "you do not
// know this one" is exactly what a sighted player is told, and substituting the real trait we
// could look up would hand a blind player intel the game is withholding. This mirrors
// UIChosenReveal.PopulateData's own two branches, per trait, in its order.
private function DescribeChosenReveal(UIChosenReveal Popup)
{
	local XComGameState_Unit UnitState;
	local XComGameState_AdventChosen Chosen;
	local XComGameState_HeadquartersAlien AlienHQ;
	local XComGameState_ResistanceFaction Faction;
	local array<X2AbilityTemplate> Traits;
	local int i;

	if (Popup == none)
		return;

	PushHeader("UIChosenReveal", "");

	// The screen's own resolution path: the ACTIVATED chosen, then its persistent state through
	// the alien HQ's template-group lookup.
	UnitState = class'XComGameState_Unit'.static.GetActivatedChosen();
	if (UnitState == none)
		return;

	AlienHQ = XComGameState_HeadquartersAlien(`XCOMHISTORY.GetSingleGameStateObjectForClass(
		class'XComGameState_HeadquartersAlien', true));
	if (AlienHQ == none)
		return;

	Chosen = AlienHQ.GetChosenOfTemplate(UnitState.GetMyTemplateGroupName());
	if (Chosen == none)
		return;

	PushLine(Chosen.GetChosenClassName());
	PushLine(Chosen.GetChosenName());

	PushLine(Popup.ChosenStrengthTitle);
	Traits = Chosen.GetChosenStrengths();
	for (i = 0; i < Traits.Length; i++)
		PushTrait(Popup, Chosen, Traits[i]);

	PushLine(Popup.ChosenWeaknessTitle);
	// The screen deliberately asks for ALL weaknesses and colours them, rather than filtering.
	Traits = Chosen.GetChosenTraits(Chosen.Weaknesses);
	for (i = 0; i < Traits.Length; i++)
		PushTrait(Popup, Chosen, Traits[i]);

	Faction = Chosen.GetRivalFaction();
	if (Faction != none)
	{
		PushLine(Popup.RivalFactionTitle);
		PushLine(Faction.GetFactionName());
	}
}

// One trait row, revealed or not — the reveal test is the screen's own.
private function PushTrait(UIChosenReveal Popup, XComGameState_AdventChosen Chosen,
	X2AbilityTemplate Trait)
{
	if (Trait == none)
		return;

	if (Trait.AbilityRevealEvent == '' || Chosen.RevealedChosenTraits.Find(Trait.DataName) != INDEX_NONE)
		PushLine(Trait.LocFriendlyName @ Trait.LocLongDescription);
	else
		PushLine(Popup.UnknownChosenTraitName @ Popup.UnknownChosenTraitDescription);
}

// The Avenger is under attack. Three real UIButtons here, unlike most of this family — but their
// labels still went through Flash, so the screen's own localized strings are the source.
private function DescribeUFOAttack(UIUFOAttack Popup)
{
	if (Popup == none)
		return;

	PushHeader("UIUFOAttack", Popup.m_strTitle);
	PushLine(Popup.m_strBody);
	PushLine(Popup.m_strUFOAttackObjective);
	PushLine(Popup.m_strUFOAttackObjectiveDesc);
}

// Saving / loading. ⛔ This one matters more than its size suggests: with nothing spoken, a
// blocking progress dialog is indistinguishable from a hang, and a blind player's only signal
// that the game is alive is that the music kept playing.
private function DescribeProgress(UIProgressDialogue Popup)
{
	if (Popup == none)
		return;

	PushHeader("UIProgressDialogue", Popup.m_kData.strTitle);
	PushLine(Popup.m_kData.strDescription);

	// An EMPTY abort label is the game's own way of saying "this cannot be cancelled" (the
	// struct's comment says so outright), so the button is emitted only when there is one.
	if (Popup.m_kData.strAbortButtonText != "")
		PushButton(0, Popup.m_kData.strAbortButtonText, class'UIUtilities_Input'.const.FXS_KEY_ESCAPE);
}

// Mission failed. ⛔ The highest-stakes popup in the game for a blind player: without it the
// mission is over, three choices are on screen, and nothing says so — there is no way to leave.
// The title/body pair is chosen by m_eType exactly as OnInit chooses it, and the three verbs are
// always spoken, because they are the only way out.
private function DescribeCombatLose(UICombatLose Popup)
{
	if (Popup == none)
		return;

	switch (Popup.m_eType)
	{
	case eUICombatLose_UnfailableObjective:
		PushHeader("UICombatLose", Popup.m_sObjectiveTitle);
		PushLine(Popup.m_sObjectiveBody);
		break;
	case eUICombatLose_UnfailableHQAssault:
		PushHeader("UICombatLose", Popup.m_sHQAssaultTitle);
		PushLine(Popup.m_sHQAssaultBody);
		break;
	case eUICombatLose_UnfailableCommanderKilled:
		PushHeader("UICombatLose", Popup.m_sCommanderKilledTitle);
		PushLine(Popup.m_sCommanderKilledBody);
		break;
	default:
		// eUICombatLose_UnfailableGeneric, and eUICombatLose_FailableGeneric — which OnInit's
		// switch does NOT handle, leaving the panel blank. The generic wording is the honest
		// answer there: a player who reached this screen has lost either way.
		PushHeader("UICombatLose", Popup.m_sGenericTitle);
		PushLine(Popup.m_sGenericBody);
		break;
	}

	PushLine(Popup.m_sSubtitle);

	// ⚠ These three are LABELS, not working buttons. AS_SetDisplay hands them to Flash and the
	// screen's own handlers are wired to mouse clicks on the movie, so the recipe cannot press
	// them — it offers Enter and Escape, which is what UIScreen answers. Speaking them anyway is
	// parity: a sighted player sees three options and knows what is on offer.
	PushLine(Popup.m_sRestart);
	PushLine(Popup.m_sReload);
	PushLine(Popup.m_sExitToMain);
}

// The sitrep detail popup, opened off a mission blade. The sitreps themselves are what the player
// came for — each one changes how the mission has to be played.
private function DescribeSitRep(UISitRepInformation Popup)
{
	local X2SitRepTemplate SitRep;
	local X2SitRepTemplateManager SitRepMgr;
	local name SitRepName;

	if (Popup == none)
		return;

	PushHeader("UISitRepInformation", Popup.m_strTitle);

	// The operation's own name and one-line brief, which the blade shows above the sitreps.
	PushLine(Popup.MissionData.BattleOpName);
	PushLine(Popup.MissionData.BattleDesc);

	SitRepMgr = class'X2SitRepTemplateManager'.static.GetSitRepTemplateManager();
	foreach Popup.MissionData.SitReps(SitRepName)
	{
		SitRep = SitRepMgr.FindSitRepTemplate(SitRepName);
		if (SitRep == none)
			continue;
		// ⛔ `Description`, not a getter — X2SitRepTemplate has GetFriendlyName() but no
		// description accessor at all. The var is `protectedwrite`, so reading it from here is
		// legal and only assignment is restricted.
		PushLine(SitRep.GetFriendlyName() @ SitRep.Description);
	}
}

// The ADVENT operations / dark event status popup. Only the screen's own headings are read here:
// the per-event rows are Flash-only and would need the dark-event states walked, which belongs
// with the geoscape work rather than in the popup family.
private function DescribeAdventOps(UIAdventOperations Popup)
{
	if (Popup == none)
		return;

	PushHeader("UIAdventOperations", Popup.m_strTitle);
	PushLine(Popup.m_strTitleHelp);
	PushLine(Popup.m_strStatusLabel);
}

// ---------------------------------------------------------------------------------------
// Wire
// ---------------------------------------------------------------------------------------

private function PushHeader(string ClassName, string Title)
{
	local string Rec;

	Rec = "kind=popup";
	Rec $= FLD_SEP $ "class=" $ ClassName;
	Rec $= FLD_SEP $ "title=" $ Title;
	Push(Rec);
}

// Blank lines are dropped here rather than at the recipe: a screen that leaves a localized var
// empty (the §13.7 trap — declared on one class, populated on another) would otherwise produce a
// tab stop with nothing in it, which reads as a broken control rather than as an absent one.
private function PushLine(string Text)
{
	if (Text == "")
		return;
	Push("kind=line" $ FLD_SEP $ "text=" $ Text);
}

private function PushButton(int Index, string Label, int Cmd)
{
	local string Rec;

	if (Label == "")
		return;

	Rec = "kind=button";
	Rec $= FLD_SEP $ "index=" $ string(Index);
	Rec $= FLD_SEP $ "label=" $ Label;
	Rec $= FLD_SEP $ "cmd=" $ string(Cmd);
	Push(Rec);
}

private function Push(string Rec)
{
	if (NativeResult != "")
		NativeResult = NativeResult $ REC_SEP;
	NativeResult = NativeResult $ Rec;
}
