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
	case 'UIChosenEncountered':
		DescribeChosenEncountered(UIChosenEncountered(Screen));
		break;
	case 'UISoldierCaptured':
		DescribeSoldierCaptured(UISoldierCaptured(Screen));
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
// UIChosenReveal.OnInit's own two branches, per trait, in its order.
//
// ⚠ The screen is pushed DURING the reveal cinematic (bShowDuringCinematic), which is why it
// went unread for so long — see the navigator's cinematic gate, not this class.
private function DescribeChosenReveal(UIChosenReveal Popup)
{
	local XComGameState_Unit UnitState;
	local XComGameState_AdventChosen Chosen;
	local XComGameState_HeadquartersAlien AlienHQ;
	local XComGameState_ResistanceFaction Faction;
	local array<X2AbilityTemplate> Traits;
	local string Line;
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

	// The three slots SetChosenName pushes, in its order: class, name, nickname — one line, the
	// same shape the encountered popup uses for the same three words.
	Line = Chosen.GetChosenClassName();
	class'X2AccessHelper'.static.AppendComma(Line, Chosen.GetChosenName());
	class'X2AccessHelper'.static.AppendComma(Line, Chosen.GetChosenNickname());
	PushLine(Line);

	PushLine(Popup.ChosenStrengthTitle);
	Traits = Chosen.GetChosenStrengths();
	for (i = 0; i < Traits.Length; i++)
		PushTrait(Popup, Chosen, Traits[i]);

	PushLine(Popup.ChosenWeaknessTitle);
	// The screen deliberately asks for ALL weaknesses and colours them, rather than filtering.
	Traits = Chosen.GetChosenTraits(Chosen.Weaknesses);
	for (i = 0; i < Traits.Length; i++)
		PushTrait(Popup, Chosen, Traits[i]);

	// The rival faction is the last WEAKNESS row on screen — "Adversary: Templars" — drawn as one
	// row from SetWeaknessFaction(RivalFactionTitle, FactionName), so it is one line here too.
	// AppendComma is the join because the title ends in its own colon and must not take another
	// separator (X2AccessHelper.AppendJoined's terminator test).
	Faction = Chosen.GetRivalFaction();
	if (Faction != none)
	{
		Line = Popup.RivalFactionTitle;
		class'X2AccessHelper'.static.AppendComma(Line, Faction.GetFactionName());
		PushLine(Line);
	}

	// ONE button, because the screen draws one: its NavHelp offers CONFIRM and nothing else, and
	// Escape merely closes the same way. The generic Continue/Dismiss fallback would invent a
	// second verb that does the same thing.
	PushButton(0, class'UIUtilities_Text'.default.m_strGenericConfirm,
		class'UIUtilities_Input'.const.FXS_KEY_ENTER);
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

// A Chosen's first appearance on the map, right after the reveal cinematic
// (docs/chosen-bond-recon.md §1.2). ⛔ ENTER AND SPACE ONLY — the screen has no Escape branch —
// so this pushes ONE button, and the recipe's generic accept/cancel pair (which would offer a
// Dismiss that does nothing) is never reached. The body carries an XGParam token for the Chosen's
// name, filled as BuildScreen fills it. The level badge is a bare digit with no label anywhere on
// the screen and is not spoken.
private function DescribeChosenEncountered(UIChosenEncountered Popup)
{
	local XComGameState_AdventChosen Chosen;
	local XGParamTag ParamTag;
	local string Line;

	if (Popup == none)
		return;

	PushHeader("UIChosenEncountered", Popup.ChosenEncounteredTitle);
	PushLine(Popup.ChosenEncounteredLabel);

	Chosen = XComGameState_AdventChosen(`XCOMHISTORY.GetGameStateForObjectID(Popup.ChosenRef.ObjectID));
	if (Chosen != none)
	{
		Line = Chosen.GetChosenClassName();
		class'X2AccessHelper'.static.AppendComma(Line, Chosen.GetChosenName());
		class'X2AccessHelper'.static.AppendComma(Line, Chosen.GetChosenNickname());
		PushLine(Line);

		ParamTag = XGParamTag(`XEXPANDCONTEXT.FindTag("XGParam"));
		ParamTag.StrValue0 = Chosen.GetChosenName();
		PushLine(`XEXPAND.ExpandString(Popup.ChosenEncounteredBody));
	}

	PushButton(0, class'UIUtilities_Text'.default.m_strGenericContinue,
		class'UIUtilities_Input'.const.FXS_KEY_ENTER);
}

// A soldier carried off by a Chosen — after the mission, or as a covert-action risk
// (docs/chosen-bond-recon.md §1.3). Same Enter-only contract, and OnContinue is what fires the
// SoldierCapturedClosed event, so the key must reach the screen rather than a CloseScreen poke.
// ⚠ The screen puts the Chosen's NAME in its name slot and its CLASS in its nickname slot
// ("CAPTURED BY <name> <class>"), and that order is kept.
private function DescribeSoldierCaptured(UISoldierCaptured Popup)
{
	local XComGameState_AdventChosen Chosen;
	local XComGameState_Unit Unit;
	local X2SoldierClassTemplate SoldierClass;
	local XGParamTag ParamTag;
	local string Line;

	if (Popup == none)
		return;

	Chosen = XComGameState_AdventChosen(`XCOMHISTORY.GetGameStateForObjectID(Popup.ChosenRef.ObjectID));
	Unit = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(Popup.TargetRef.ObjectID));

	PushHeader("UISoldierCaptured", Popup.SoldierCapturedTitle);
	PushLine(Popup.SoldierCapturedLabel);

	if (Chosen != none && Unit != none)
	{
		ParamTag = XGParamTag(`XEXPANDCONTEXT.FindTag("XGParam"));
		ParamTag.StrValue0 = Chosen.GetChosenName();
		ParamTag.StrValue1 = Unit.GetFullName();
		PushLine(`XEXPAND.ExpandString(Popup.SoldierCapturedBody));
	}

	if (Unit != none)
	{
		Line = Unit.GetName(eNameType_RankFull);
		SoldierClass = Unit.GetSoldierClassTemplate();
		if (SoldierClass != none)
			class'X2AccessHelper'.static.AppendComma(Line, SoldierClass.DisplayName);
		PushLine(Line);
	}

	if (Chosen != none)
	{
		Line = Popup.SoldierCapturedBy;
		class'X2AccessHelper'.static.AppendJoined(Line, Chosen.GetChosenName(), " ");
		class'X2AccessHelper'.static.AppendComma(Line, Chosen.GetChosenClassName());
		PushLine(Line);
	}

	PushButton(0, class'UIUtilities_Text'.default.m_strGenericContinue,
		class'UIUtilities_Input'.const.FXS_KEY_ENTER);
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

// The ADVENT operations / dark event board (UIAdventOperations), the page the player opens each
// month. The screen retains no row text (the cards are pushed straight into Flash), but it
// retains what the cards were BUILT from: ChosenDarkEvents and ActiveDarkEvents hold the
// references BuildDarkEventPanel wrote for every card it drew, and TitleHelp holds the heading
// line. So this re-runs the screen's own getters over those references and says exactly what
// BuildDarkEventPanel handed Flash, no more.
//
// PARITY. A hidden (secret) dark event is drawn as UpdateDarkEventCardLocked: the concealed title
// GetDisplayName answers while bSecretEvent is set, the reveal cost, and the unlock caption and
// button only when the player can pay. It is NOT drawn its summary or quote, so none is read.
// GetDisplayName and GetCost both answer the concealed words themselves, which is the whole gate:
// the template's real name and text are never asked for. Nothing here reads an expiry, a
// remaining duration or a doom tick, because the board draws none (only the facility and
// retaliation countdowns in weeks, which the title panel does draw).
//
// Records, in the order the recipe lays them out:
//   kind=popup    class, title                  (as every popup)
//   kind=advent   mode (report, active, pending), help, canflip, flip,
//                 fac_label, fac_value, fac_est, ret_label, ret_value, ret_est
//   kind=card     index, state (locked, open), title, cost, unlock, canreveal, reveal,
//                 status, summary, quote, author
private function DescribeAdventOps(UIAdventOperations Popup)
{
	local XComGameState_HeadquartersAlien AlienHQ;
	local XComGameState_HeadquartersXCom XComHQ;
	local StateObjectReference CardRef;
	local string Mode;
	local int Idx, Total;
	local bool bActive;

	if (Popup == none)
		return;

	PushHeader("UIAdventOperations", Popup.m_strTitle);

	AlienHQ = class'UIUtilities_Strategy'.static.GetAlienHQ();
	XComHQ = class'UIUtilities_Strategy'.static.GetXComHQ();
	if (AlienHQ == none)
		return;

	if (Popup.bResistanceReport)
		Mode = "report";
	else if (Popup.bShowActiveEvents)
		Mode = "active";
	else
		Mode = "pending";

	DescribeAdventTitlePanel(Popup, AlienHQ, Mode);

	// The same two sources and the same guard BuildScreen walks: pending cards are
	// ChosenDarkEvents, active cards ActiveDarkEvents, and the resistance report shows no active
	// cards at all.
	bActive = Popup.bShowActiveEvents;
	if (!bActive)
		Total = AlienHQ.ChosenDarkEvents.Length;
	else if (!Popup.bResistanceReport)
		Total = AlienHQ.ActiveDarkEvents.Length;
	else
		Total = 0;

	for (Idx = 0; Idx < Total; Idx++)
	{
		// The screen's own cache first (what it drew from), the headquarters list when the
		// cache is shorter than the card count. BuildScreen rewrites the cache entry for every
		// card it draws, so the two agree whenever both exist.
		if (bActive)
		{
			if (Idx < Popup.ActiveDarkEvents.Length)
				CardRef = Popup.ActiveDarkEvents[Idx];
			else
				CardRef = AlienHQ.ActiveDarkEvents[Idx];
		}
		else
		{
			if (Idx < Popup.ChosenDarkEvents.Length)
				CardRef = Popup.ChosenDarkEvents[Idx];
			else
				CardRef = AlienHQ.ChosenDarkEvents[Idx];
		}
		DescribeAdventCard(Popup, XComHQ, CardRef, Idx, bActive);
	}
}

// The title panel: the screen's own heading line, the two countdowns BuildTitlePanel draws in
// weeks, and the facts the recipe needs to offer the flip. The weeks arithmetic is the screen's
// own (whole weeks, a week, then the imminent word).
private function DescribeAdventTitlePanel(UIAdventOperations Popup, XComGameState_HeadquartersAlien AlienHQ, string Mode)
{
	local XComGameStateHistory History;
	local XComGameState_MissionCalendar CalendarState;
	local TDateTime RetalDate;
	local string Rec;
	local bool bCanFlip;

	History = `XCOMHISTORY;
	CalendarState = XComGameState_MissionCalendar(History.GetSingleGameStateObjectForClass(class'XComGameState_MissionCalendar'));

	bCanFlip = !Popup.bResistanceReport && AlienHQ.ActiveDarkEvents.Length > 0;

	Rec = "kind=advent";
	Rec $= FLD_SEP $ "mode=" $ Mode;
	class'X2AccessWire'.static.AddField(Rec, "help", Popup.TitleHelp);
	if (bCanFlip)
	{
		Rec $= FLD_SEP $ "canflip=1";
		class'X2AccessWire'.static.AddField(Rec, "flip",
			Popup.bShowActiveEvents ? Popup.m_strShowPendingButton : Popup.m_strShowActiveButton);
	}

	// Alien facility countdown: the screen draws it only under this exact condition.
	if (AlienHQ.bHasSeenFacility && AlienHQ.bBuildingFacility && AlienHQ.AIMode != "Lose")
	{
		class'X2AccessWire'.static.AddField(Rec, "fac_label", Popup.m_strAlienFacility);
		class'X2AccessWire'.static.AddField(Rec, "fac_value", WeeksText(Popup,
			class'X2StrategyGameRulesetDataStructures'.static.DifferenceInDays(AlienHQ.FacilityBuildEndTime, `STRATEGYRULES.GameTime) / 7));
		class'X2AccessWire'.static.AddField(Rec, "fac_est", Popup.m_strEstimated);
	}

	// Retaliation countdown: drawn only once the player has seen a retaliation mission and the
	// calendar has one scheduled.
	if (AlienHQ.bHasSeenRetaliation && CalendarState != none
		&& CalendarState.GetNextDateForMissionSource('MissionSource_Retaliation', RetalDate))
	{
		class'X2AccessWire'.static.AddField(Rec, "ret_label", Popup.m_strRetaliation);
		class'X2AccessWire'.static.AddField(Rec, "ret_value", WeeksText(Popup,
			class'X2StrategyGameRulesetDataStructures'.static.DifferenceInDays(RetalDate, `STRATEGYRULES.GameTime) / 7));
		class'X2AccessWire'.static.AddField(Rec, "ret_est", Popup.m_strEstimated);
	}

	Push(Rec);
}

// Whole weeks the way BuildTitlePanel words them, without the colour tags.
private function string WeeksText(UIAdventOperations Popup, int Weeks)
{
	if (Weeks > 1)
		return string(Weeks) @ Popup.m_strWeeks;
	if (Weeks == 1)
		return string(Weeks) @ Popup.m_strWeek;
	return Popup.m_strImminent;
}

// One card, as BuildDarkEventPanel drew it. State objects are re-fetched by ObjectID, never held.
private function DescribeAdventCard(UIAdventOperations Popup, XComGameState_HeadquartersXCom XComHQ,
	StateObjectReference CardRef, int Idx, bool bActive)
{
	local XComGameState_DarkEvent DarkEvent;
	local array<StrategyCostScalar> CostScalars;
	local string Rec;

	DarkEvent = XComGameState_DarkEvent(`XCOMHISTORY.GetGameStateForObjectID(CardRef.ObjectID));
	if (DarkEvent == none)
		return;

	Rec = "kind=card" $ FLD_SEP $ "index=" $ string(Idx);

	if (DarkEvent.bSecretEvent)
	{
		// The locked card: concealed title, reveal cost, and the unlock caption and button only
		// when the reveal is affordable (the same test, the same cost scalars).
		Rec $= FLD_SEP $ "state=locked";
		class'X2AccessWire'.static.AddField(Rec, "title", DarkEvent.GetDisplayName());
		class'X2AccessWire'.static.AddField(Rec, "cost", DarkEvent.GetCost());
		CostScalars.Length = 0;
		if (XComHQ != none && XComHQ.CanAffordAllStrategyCosts(DarkEvent.RevealCost, CostScalars))
		{
			class'X2AccessWire'.static.AddField(Rec, "unlock", Popup.m_strUnlockButton);
			// OnRevealClicked always indexes ChosenDarkEvents, so a card in the active list
			// must not offer the verb: its index would reveal a different pending card.
			if (!bActive)
			{
				Rec $= FLD_SEP $ "canreveal=1";
				class'X2AccessWire'.static.AddField(Rec, "reveal", Popup.m_strReveal);
			}
		}
	}
	else
	{
		Rec $= FLD_SEP $ "state=open";
		class'X2AccessWire'.static.AddField(Rec, "title", DarkEvent.GetDisplayName());
		class'X2AccessWire'.static.AddField(Rec, "status",
			Popup.m_strStatusLabel @ (bActive ? Popup.m_strActive : Popup.m_strPreparing));
		class'X2AccessWire'.static.AddField(Rec, "summary", DarkEvent.GetSummary());
		class'X2AccessWire'.static.AddField(Rec, "quote", DarkEvent.GetQuote());
		class'X2AccessWire'.static.AddField(Rec, "author", DarkEvent.GetQuoteAuthor());
	}

	Push(Rec);
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
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}
