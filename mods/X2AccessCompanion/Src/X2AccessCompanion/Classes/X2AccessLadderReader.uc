// X2AccessLadderReader
// The Tactical Legacy Pack's MISSION-SIDE screens - the words their widgets do not retain - plus
// the one narration the score HUD needs. Two roles in one class, on purpose:
//
// 1. A CDO reader, the X2AccessSaveReader contract: C++ owns the gate, the keys and the speech,
//    calls one Describe*() below on this class's default object via ProcessEvent (game thread,
//    from a graph recipe's build) and reads NativeResult back through reflection right after the
//    call returns. Nothing in that half runs on its own.
//
// 2. An X2EventListener for 'ChallengeModeScoreChange', the X2AccessTacticalEvents contract:
//    the score HUD (UIChallengeModeHUD / UILadderModeHUD) is a screen nobody acts on, so it gets
//    no recipe; what it DRAWS is a banner per scoring event and a running total, and those are
//    narrated here the moment the banner plays. Same class because an X2EventListener is an
//    Object with a CDO, which is all the reader half needs.
//
// WHY THE WORDS LIVE HERE. Every screen in this family pushes its content into Flash at OnInit
// and retains the localized LABELS alone; the figures - scores, thresholds, the squad's changes,
// the two upgrade offers - are read out of the history and the challenge manager and kept
// nowhere. Native reads the labels off the instance; each function below re-runs the screen's own
// OnInit arithmetic over the same state objects, and nothing else. Where the game's own text is
// a template with XGParam tags (the upgrade lines), it is expanded through the game's own
// XEXPAND, exactly as PopulateUpgradePanel does.
//
// No backtick and no slash-star inside a comment here (the preprocessor rule in CLAUDE.md).
//
// GC rule: this object is a CDO - value-typed state only, NEVER object references.

class X2AccessLadderReader extends X2EventListener;

// Written by every Describe entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// ---------------------------------------------------------------------------------------
// The score HUD - narrate-only (docs/tle-mission-recon.md, section 1)
// ---------------------------------------------------------------------------------------

static function array<X2DataTemplate> CreateTemplates()
{
	local array<X2DataTemplate> Templates;

	Templates.AddItem(CreateLadderNarrationListener());

	return Templates;
}

static function CHEventListenerTemplate CreateLadderNarrationListener()
{
	local CHEventListenerTemplate Template;

	`CREATE_X2TEMPLATE(class'CHEventListenerTemplate', Template, 'X2Access_LadderNarration');
	Template.RegisterInTactical = true;
	Template.RegisterInStrategy = false;

	// The HUD's own registration window: UIChallengeModeHUD takes this event on
	// ELD_OnVisualizationBlockCompleted, and the banner itself is X2Action_ChallengeScoreUpdate on
	// the same context's visualization - so at Completed the banner has just played, which is when
	// a sighted player has just read it. Earlier (StateSubmitted) would speak the score before the
	// kill it rewards had been narrated.
	Template.AddCHEvent('ChallengeModeScoreChange', OnChallengeModeScoreChange, ELD_OnVisualizationBlockCompleted, 40);

	`log("X2Access: ladder narration listener created", , 'X2Access');

	return Template;
}

// One line per scoring state in the submitted frame, in the banner's own words: the label the
// HUD draws for the scoring type, the points, the HUD's POINTS word, then the running total under
// the HUD's SCORE label. The early-bird bonus rides in the same state and the game draws it as a
// message banner of its own, so it is spoken as its own line in the game's own two strings.
static function EventListenerReturn OnChallengeModeScoreChange(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	local XComGameState_ChallengeScore Scoring;
	local XComGameState_LadderProgress LadderData;
	local XComChallengeModeManager ChallengeModeManager;
	local XGParamTag Tag;
	local string Line, Total;
	local int TotalScore;
	local bool bAnyLine;

	if (GameState == none)
		return ELR_NoInterrupt;

	// The action that draws the banner skips it inside a replay; so does this.
	if (`REPLAY.bInReplay)
		return ELR_NoInterrupt;

	foreach GameState.IterateByClassType(class'XComGameState_ChallengeScore', Scoring)
	{
		if (Scoring.ScoringType != CMPT_None && Scoring.ScoringType != CMPT_TotalScore)
		{
			Line = "";
			class'X2AccessHelper'.static.AppendJoined(Line, class'UIChallengeModeHUD'.default.m_BannerLabels[Scoring.ScoringType], ", ");
			class'X2AccessHelper'.static.AppendJoined(Line, string(Scoring.AddedPoints) @ class'UIChallengeModeHUD'.default.m_PointsLabel, ", ");
			class'X2AccessNarrator'.static.Combat(Line);
			bAnyLine = true;
		}

		if (Scoring.LadderBonus > 0)
		{
			Tag = XGParamTag(`XEXPANDCONTEXT.FindTag("XGParam"));
			Tag.IntValue0 = Scoring.LadderBonus;
			Line = "";
			class'X2AccessHelper'.static.AppendJoined(Line, class'XComGameState_LadderProgress'.default.EarlyBirdTitle, ", ");
			class'X2AccessHelper'.static.AppendJoined(Line, `XEXPAND.ExpandString(class'XComGameState_LadderProgress'.default.EarlyBirdBonus), ", ");
			class'X2AccessNarrator'.static.Combat(Line);
			bAnyLine = true;
		}
	}

	if (!bAnyLine)
		return ELR_NoInterrupt;

	// The total the HUD draws after the banner: the ladder's cumulative score, or the challenge
	// manager's, exactly the two sources the two HUDs read.
	LadderData = XComGameState_LadderProgress(`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_LadderProgress', true));
	if (LadderData != none)
	{
		TotalScore = LadderData.CumulativeScore;
	}
	else
	{
		ChallengeModeManager = `CHALLENGEMODE_MGR;
		if (ChallengeModeManager == none)
			return ELR_NoInterrupt;
		TotalScore = ChallengeModeManager.GetTotalScore();
	}
	Total = "";
	class'X2AccessHelper'.static.AppendJoined(Total, class'UIChallengeModeHUD'.default.m_ScoreLabel, ", ");
	class'X2AccessHelper'.static.AppendJoined(Total, string(TotalScore), ", ");
	class'X2AccessNarrator'.static.Combat(Total);

	return ELR_NoInterrupt;
}

// The HUD's two readable facts for the tactical HUD graph's stop: the running score under the
// HUD's own label, and - in a challenge with a timer - the time left as the HUD draws it. Records:
//   kind=score  label  value
//   kind=timer  label  value        (only while a challenge-mode timer exists)
function DescribeHudScore()
{
	local XComGameState_LadderProgress LadderData;
	local XComChallengeModeManager ChallengeModeManager;
	local XComGameState_TimerData Timer;
	local string Rec, Minutes, Seconds;
	local int TotalSeconds;

	NativeResult = "";

	LadderData = XComGameState_LadderProgress(`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_LadderProgress', true));
	ChallengeModeManager = `CHALLENGEMODE_MGR;

	Rec = "kind=score";
	AddField(Rec, "label", class'UIChallengeModeHUD'.default.m_ScoreLabel);
	if (LadderData != none)
		AddField(Rec, "value", string(LadderData.CumulativeScore));
	else if (ChallengeModeManager != none)
		AddField(Rec, "value", string(ChallengeModeManager.GetTotalScore()));
	else
		return; // neither mode: nothing on screen to describe
	Emit(Rec);

	// The challenge HUD's Tick pushes minutes and seconds off this timer every frame while it
	// runs; the ladder HUD overrides Tick to do nothing and has no timer to read.
	Timer = XComGameState_TimerData(`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_TimerData', true));
	if (Timer != none && Timer.bIsChallengeModeTimer)
	{
		TotalSeconds = Timer.GetCurrentTime();
		if (TotalSeconds >= 0)
		{
			Minutes = string(TotalSeconds / 60);
			Seconds = string(TotalSeconds % 60);
			if (Len(Seconds) < 2)
				Seconds = "0" $ Seconds;
			Rec = "kind=timer";
			AddField(Rec, "label", class'UIChallengeModeHUD'.default.m_BannerLabels[CMPT_TimeRemaining]);
			AddField(Rec, "value", Minutes $ ":" $ Seconds);
			Emit(Rec);
		}
	}
}

// ---------------------------------------------------------------------------------------
// The scoring dialogs (UILadderModeScoringDialog, UIChallengeModeScoringDialog)
// ---------------------------------------------------------------------------------------
//
// Records, in the order the dialog draws them:
//   kind=row    index  label  points     (one per addScoreRow the dialog's OnInit made)
//   kind=total  label  value
//
// Which dialog is up decides the loop: the ladder dialog skips the two soldier rows of the table
// and appends them as NEGATIVE penalty rows from the soldier scoring instead; the challenge dialog
// draws every table row with a positive maximum. Both are OnInit's own loops, line for line.
function DescribeScoring()
{
	local UIScreenStack Stack;
	local UILadderModeScoringDialog Ladder;
	local UIChallengeModeScoringDialog Challenge;
	local XComChallengeModeManager ChallengeModeManager;
	local array<ScoreTableEntry> ScoreTable;
	local ChallengeSoldierScoring SoldierScoring;
	local string Rec;
	local int idx, count, maxscore;

	NativeResult = "";

	Stack = `SCREENSTACK;
	if (Stack == none)
		return;
	Ladder = UILadderModeScoringDialog(Stack.GetFirstInstanceOf(class'UILadderModeScoringDialog'));
	Challenge = UIChallengeModeScoringDialog(Stack.GetFirstInstanceOf(class'UIChallengeModeScoringDialog'));
	if (Ladder == none && Challenge == none)
		return;

	ChallengeModeManager = `CHALLENGEMODE_MGR;
	if (ChallengeModeManager == none)
		return;

	ScoreTable.Length = 0;
	maxscore = ChallengeModeManager.GetChallengeMaxPossibleScoreTable(ScoreTable);

	count = 0;
	for (idx = 0; idx < CMPT_MAX; ++idx)
	{
		if (idx >= ScoreTable.Length)
			break;
		if (Ladder != none && (idx == CMPT_UninjuredSoldiers || idx == CMPT_AliveSoldiers))
			continue;
		if (ScoreTable[idx].ScoreMax > 0)
		{
			Rec = "kind=row";
			AddField(Rec, "index", string(count++));
			if (Ladder != none)
				AddField(Rec, "label", Ladder.m_ScoreLabels[idx]);
			else
				AddField(Rec, "label", Challenge.m_ScoreLabels[idx]);
			AddField(Rec, "points", string(ScoreTable[idx].ScoreMax));
			Emit(Rec);
		}
	}

	if (Ladder != none)
	{
		ChallengeModeManager.GetSoldierScoring(SoldierScoring);
		if (SoldierScoring.UninjuredBonus > 0)
		{
			Rec = "kind=row";
			AddField(Rec, "index", string(count++));
			AddField(Rec, "label", Ladder.m_ScoreLabels[CMPT_UninjuredSoldiers]);
			AddField(Rec, "points", string(-SoldierScoring.UninjuredBonus));
			Emit(Rec);
		}
		if (SoldierScoring.WoundedBonus > 0)
		{
			Rec = "kind=row";
			AddField(Rec, "index", string(count++));
			AddField(Rec, "label", Ladder.m_ScoreLabels[CMPT_AliveSoldiers]);
			AddField(Rec, "points", string(-SoldierScoring.WoundedBonus));
			Emit(Rec);
		}
	}

	Rec = "kind=total";
	if (Ladder != none)
		AddField(Rec, "label", Ladder.m_TotalLabel);
	else
		AddField(Rec, "label", Challenge.m_TotalLabel);
	AddField(Rec, "value", string(maxscore));
	Emit(Rec);
}

// ---------------------------------------------------------------------------------------
// The title block four of the ladder screens share
// ---------------------------------------------------------------------------------------
//
//   kind=header  title  name  label  count
//
// SetScreenTitle's four operands: MISSION COMPLETE, the ladder's name (a narrative ladder's
// localized name, a procedural one's generated name), the MISSION: label and "r/size". The
// upgrade screen draws the CURRENT rung (the next mission's number); the other three draw the
// one just finished (rung - 1) - bCompleted picks.
private function EmitTitleBlock(XComGameState_LadderProgress LadderData, bool bCompleted)
{
	local string Rec;
	local int Rung;

	if (LadderData == none)
		return;

	Rung = bCompleted ? LadderData.LadderRung - 1 : LadderData.LadderRung;

	Rec = "kind=header";
	AddField(Rec, "title", class'UIMissionSummary'.default.m_strMissionComplete);
	AddField(Rec, "name", LadderName(LadderData));
	AddField(Rec, "label", class'UILadderSoldierInfo'.default.m_MissionLabel);
	AddField(Rec, "count", string(Rung) $ "/" $ string(LadderData.LadderSize));
	Emit(Rec);
}

private function string LadderName(XComGameState_LadderProgress LadderData)
{
	if (LadderData.LadderIndex < 10)
	{
		if (LadderData.LadderIndex >= 0 && LadderData.LadderIndex < LadderData.NarrativeLadderNames.Length)
			return LadderData.NarrativeLadderNames[LadderData.LadderIndex];
		return "";
	}
	return LadderData.LadderName;
}

// ---------------------------------------------------------------------------------------
// The ladder upgrade picker (UILadderUpgradeScreen)
// ---------------------------------------------------------------------------------------
//
//   kind=header ...                                   (EmitTitleBlock, current rung)
//   kind=card   index  title  desc  line ...          (line repeats; at most six, empties dropped
//                                                      because the game fills the unused slots
//                                                      with "" and draws nothing for them)
//
// Everything below PopulateUpgradePanel's SetUpgradePanel and SetUpgradeDetailRow pushes, run
// twice over the two offers exactly as OnInit does, XEXPAND included.
function DescribeUpgrade()
{
	local UIScreenStack Stack;
	local UILadderUpgradeScreen Screen;
	local XComGameState_LadderProgress LadderData;
	local XComGameState_HeadquartersXCom XComHQ;
	local X2LadderUpgradeTemplateManager TemplateManager;
	local X2LadderUpgradeTemplate Upgrade;
	local X2SoldierClassTemplateManager ClassManager;
	local X2ItemTemplateManager ItemManager;
	local int Rung;

	NativeResult = "";

	Stack = `SCREENSTACK;
	if (Stack == none)
		return;
	Screen = UILadderUpgradeScreen(Stack.GetFirstInstanceOf(class'UILadderUpgradeScreen'));
	if (Screen == none)
		return;

	LadderData = XComGameState_LadderProgress(`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_LadderProgress'));
	XComHQ = XComGameState_HeadquartersXCom(`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_HeadquartersXCom'));
	if (LadderData == none || XComHQ == none)
		return;

	EmitTitleBlock(LadderData, false);

	TemplateManager = class'X2LadderUpgradeTemplateManager'.static.GetLadderUpgradeTemplateManager();
	ClassManager = class'X2SoldierClassTemplateManager'.static.GetSoldierClassTemplateManager();
	ItemManager = class'X2ItemTemplateManager'.static.GetItemTemplateManager();

	Rung = LadderData.LadderRung - 1;
	if (Rung < 0)
		return;

	if (Rung < LadderData.ProgressionChoices1.Length)
	{
		Upgrade = TemplateManager.FindUpgradeTemplate(LadderData.ProgressionChoices1[Rung]);
		DescribeUpgradeCard(0, Upgrade, XComHQ, ClassManager, ItemManager);
	}
	if (Rung < LadderData.ProgressionChoices2.Length)
	{
		Upgrade = TemplateManager.FindUpgradeTemplate(LadderData.ProgressionChoices2[Rung]);
		DescribeUpgradeCard(1, Upgrade, XComHQ, ClassManager, ItemManager);
	}
}

// PopulateUpgradePanel, minus the pushes: the same walk over the template's Upgrades against the
// squad, the same replace-or-gain decision per class, the same six-line cap. Anything this says
// differently from the card is a bug in this function, not a design choice.
private function DescribeUpgradeCard(int Index, X2LadderUpgradeTemplate Upgrade, XComGameState_HeadquartersXCom XComHQ, X2SoldierClassTemplateManager ClassManager, X2ItemTemplateManager ItemManager)
{
	local int i, j, k, DescIdx;
	local XGParamTag ParamTag;
	local string ExpandedString, Rec;
	local X2SoldierClassTemplate ClassTemplate;
	local X2EquipmentTemplate ItemTemplate;
	local X2WeaponUpgradeTemplate ItemUpgradeTemplate;
	local array<X2WeaponUpgradeTemplate> equippedUpgrades;
	local name FilterName;
	local array<name> DisplayedClasses;
	local XComGameState_Item primaryWeaponItem, replacementItem;
	local array<XComGameState_Item> utilItems;
	local bool bReplace, bAddLine, bAdded;
	local XComGameState_Unit Soldier;
	local X2WeaponTemplate WeaponTemplate;

	if (Upgrade == none)
		return;

	Rec = "kind=card";
	AddField(Rec, "index", string(Index));
	AddField(Rec, "title", Upgrade.ShortDescription);
	AddField(Rec, "desc", Upgrade.FullDescription);

	ParamTag = XGParamTag(`XEXPANDCONTEXT.FindTag("XGParam"));

	DescIdx = 0;
	for (i = 0; (i < Upgrade.Upgrades.Length) && (DescIdx < 6); ++i)
	{
		bAddLine = true;

		FilterName = name(Upgrade.Upgrades[i].ClassGroup != "" ? Upgrade.Upgrades[i].ClassGroup : Upgrade.Upgrades[i].ClassFilter);

		if (DisplayedClasses.Find(FilterName) != INDEX_NONE)
			continue;

		bAdded = false;
		for (j = 0; j < XComHQ.Squad.Length && !bAdded; j++)
		{
			Soldier = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(XComHQ.Squad[j].ObjectID));
			if (Soldier != none && Soldier.GetSoldierClassTemplateName() == name(Upgrade.Upgrades[i].ClassFilter))
			{
				bAdded = true;
				ClassTemplate = ClassManager.FindSoldierClassTemplate(FilterName);
				DisplayedClasses.AddItem(FilterName);

				if (Upgrade.Upgrades[i].EquipmentNames.Length == 0)
					continue;

				ItemTemplate = X2EquipmentTemplate(ItemManager.FindItemTemplate(Upgrade.Upgrades[i].EquipmentNames[0]));

				bReplace = false;
				if (ItemTemplate != none)
				{
					if (ItemTemplate.InventorySlot != eInvSlot_Utility)
					{
						if (ItemTemplate.InventorySlot == eInvSlot_HeavyWeapon)
						{
							bAddLine = Soldier.HasHeavyWeapon();
						}
						replacementItem = Soldier.GetItemInSlot(ItemTemplate.InventorySlot);

						bReplace = replacementItem != none;
						if (bReplace)
							ParamTag.StrValue2 = replacementItem.GetMyTemplate().GetItemFriendlyNameNoStats();
					}
					else
					{
						utilItems = Soldier.GetAllItemsInSlot(eInvSlot_Utility);
						for (k = 0; k < utilItems.Length; k++)
						{
							if ((utilItems[k].GetMyTemplate().ItemCat == 'grenade' && ItemTemplate.ItemCat == 'grenade') ||
								(utilItems[k].GetMyTemplate().ItemCat == 'pcs'     && ItemTemplate.ItemCat == 'pcs'    ) ||
								(utilItems[k].GetMyTemplate().ItemCat != 'grenade' && ItemTemplate.ItemCat != 'grenade' && utilItems[k].GetMyTemplateName() != 'XPad' && utilItems[k].GetMyTemplate().ItemCat != 'heal' ))
							{
								bReplace = true;
								ParamTag.StrValue2 = utilItems[k].GetMyTemplate().GetItemFriendlyNameNoStats();
							}
						}
					}

					ParamTag.StrValue0 = ClassTemplate != none ? ClassTemplate.DisplayName : "";
					ParamTag.StrValue1 = ItemTemplate.GetItemFriendlyNameNoStats();
				}
				else
				{
					ItemUpgradeTemplate = X2WeaponUpgradeTemplate(ItemManager.FindItemTemplate(Upgrade.Upgrades[i].EquipmentNames[0]));
					if (ItemUpgradeTemplate == none)
						continue;

					primaryWeaponItem = Soldier.GetItemInSlot(eInvSlot_PrimaryWeapon);
					if (primaryWeaponItem == none)
						continue;
					equippedUpgrades = primaryWeaponItem.GetMyWeaponUpgradeTemplates();
					if (equippedUpgrades.Length > 0)
					{
						bReplace = true;
						ParamTag.StrValue2 = equippedUpgrades[0].GetItemFriendlyNameNoStats();
					}
					WeaponTemplate = X2WeaponTemplate(primaryWeaponItem.GetMyTemplate());

					bAddLine = WeaponTemplate != none && WeaponTemplate.NumUpgradeSlots > 0 && ItemUpgradeTemplate.CanApplyUpgradeToWeapon(primaryWeaponItem);

					ParamTag.StrValue0 = ClassTemplate != none ? ClassTemplate.DisplayName : "";
					ParamTag.StrValue1 = ItemUpgradeTemplate.GetItemFriendlyNameNoStats();
				}

				// The replace line carries a warning icon in front of it on screen; the icon is
				// the "replace" wording's own emphasis and the sentence says it.
				if (bReplace)
					ExpandedString = `XEXPAND.ExpandString(class'UILadderUpgradeScreen'.default.m_strReplaceFormat);
				else
					ExpandedString = `XEXPAND.ExpandString(class'UILadderUpgradeScreen'.default.m_strUpgradeFormat);

				if (bAddLine)
				{
					AddField(Rec, "line", ExpandedString);
					++DescIdx;
				}
			}
		}
	}

	Emit(Rec);
}

// ---------------------------------------------------------------------------------------
// The ladder squad review (UILadderSoldierInfo)
// ---------------------------------------------------------------------------------------
//
//   kind=header  ...                                  (EmitTitleBlock, completed rung)
//   kind=soldier slot  name  class  rank  header  dead
//   kind=ability slot  name
//   kind=item    slot  name
//
// The page is the screen's own: m_SoldierStartIndex and the two arrays it RETAINS (PrevStates,
// CurrentStates - the one screen in this family that keeps its state objects). The diff of items
// and abilities is UpdateData's, line for line, including its two exclusions (the XPad, paired
// tertiary weapons) and the modular-weapon upgrade lines it appends.
function DescribeSoldierPage()
{
	local UIScreenStack Stack;
	local UILadderSoldierInfo Screen;
	local XComGameState_LadderProgress LadderData;
	local XComGameState_Unit UnitState, PreviousUnitState;
	local int i, j, k, slot;
	local bool bisDead, bFoundPreviousUnit;
	local array<XComGameState_Item> SoldierItems, PreviousSoldierItems;
	local XComGameState_Item CurrentItem, PreviousItem;
	local array<SoldierClassAbilityType> SoldierAbilities, PreviousSoldierAbilities;
	local SoldierClassAbilityType CurrentAbility, PreviousAbility;
	local name UpgradeName, ItemName;
	local X2LadderUpgradeTemplate UpgradeTemplate;
	local Upgrade Entry;
	local X2LadderUpgradeTemplateManager TemplateManager;
	local string Rec;

	NativeResult = "";

	Stack = `SCREENSTACK;
	if (Stack == none)
		return;
	Screen = UILadderSoldierInfo(Stack.GetFirstInstanceOf(class'UILadderSoldierInfo'));
	if (Screen == none || Screen.m_LadderData == none)
		return;

	LadderData = XComGameState_LadderProgress(`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_LadderProgress'));
	EmitTitleBlock(LadderData, true);

	TemplateManager = class'X2LadderUpgradeTemplateManager'.static.GetLadderUpgradeTemplateManager();
	if (Screen.m_LadderData.ProgressionUpgrades.Length > 0)
	{
		UpgradeName = Screen.m_LadderData.ProgressionUpgrades[Screen.m_LadderData.ProgressionUpgrades.Length - 1];
		UpgradeTemplate = TemplateManager.FindUpgradeTemplate(UpgradeName);
	}

	slot = 0;
	for (i = Screen.m_SoldierStartIndex; (i < Screen.m_SoldierStartIndex + 3) && (i < Screen.CurrentStates.Length); i++)
	{
		UnitState = Screen.CurrentStates[i];

		bisDead = false;
		bFoundPreviousUnit = false;
		PreviousSoldierItems.Remove(0, PreviousSoldierItems.Length);
		PreviousSoldierAbilities.Remove(0, PreviousSoldierAbilities.Length);

		if (UnitState == none)
		{
			UnitState = Screen.PrevStates[i];
			if (UnitState == none)
			{
				++slot;
				continue;
			}
			bisDead = true;
			PreviousSoldierItems = UnitState.GetAllInventoryItems(UnitState.GetParentGameState());
			PreviousSoldierAbilities = UnitState.GetEarnedSoldierAbilities();
		}
		else if (Screen.PrevStates[i] != none)
		{
			bFoundPreviousUnit = true;
			PreviousUnitState = Screen.PrevStates[i];
			PreviousSoldierItems = PreviousUnitState.GetAllInventoryItems(PreviousUnitState.GetParentGameState());
			PreviousSoldierAbilities = PreviousUnitState.GetEarnedSoldierAbilities();
		}

		SoldierItems = UnitState.GetAllInventoryItems();
		SoldierAbilities = UnitState.GetEarnedSoldierAbilities();

		// SetSoldierImage's words (the portrait is the one operand with no words), then
		// SetSoldierHeader's: KIA, NEW SOLDIER, or RANK UP.
		Rec = "kind=soldier";
		AddField(Rec, "slot", string(slot));
		if (LadderData != none && LadderData.LadderIndex < 10)
			AddField(Rec, "name", class'XComGameState_LadderProgress'.static.GetUnitName(i, LadderData.LadderIndex));
		else
			AddField(Rec, "name", UnitState.GetFullName());
		if (UnitState.GetSoldierClassTemplate() != none)
			AddField(Rec, "class", UnitState.GetSoldierClassTemplate().DisplayName);
		// The rank ICON is drawn; its name is the word a sighted player reads off it.
		AddField(Rec, "rank", class'X2ExperienceConfig'.static.GetRankName(UnitState.GetRank(), UnitState.GetSoldierClassTemplateName()));
		if (bisDead)
		{
			AddField(Rec, "header", Screen.m_KIA);
			AddField(Rec, "dead", "1");
		}
		else if (!bFoundPreviousUnit)
			AddField(Rec, "header", Screen.m_NewSoldier);
		else
			AddField(Rec, "header", Screen.m_RankUp);
		Emit(Rec);

		// UpdateData's item diff: drop what the previous state already carried and every
		// size-zero item, then the XPad and paired tertiary weapons.
		for (k = 0; k < PreviousSoldierItems.Length; k++)
		{
			PreviousItem = PreviousSoldierItems[k];
			for (j = SoldierItems.Length - 1; j >= 0; j--)
			{
				CurrentItem = SoldierItems[j];
				if (CurrentItem.GetMyTemplateName() == PreviousItem.GetMyTemplateName() || CurrentItem.GetMyTemplate().iItemSize <= 0)
					SoldierItems.Remove(j, 1);
			}
		}
		for (k = SoldierItems.Length - 1; k >= 0; --k)
		{
			if (SoldierItems[k].GetMyTemplateName() == 'XPad')
				SoldierItems.Remove(k, 1);
			else if ((X2WeaponTemplate(SoldierItems[k].GetMyTemplate()) != none) && (X2WeaponTemplate(SoldierItems[k].GetMyTemplate()).InventorySlot == eInvSlot_TertiaryWeapon))
				SoldierItems.Remove(k, 1);
		}

		for (j = 0; j < SoldierItems.Length && !bisDead; j++)
			EmitItem(slot, SoldierItems[j].GetMyTemplateName());

		if (UpgradeTemplate != none && InStr(UpgradeTemplate.ImagePath, "Modular_Weapons") != -1)
		{
			foreach UpgradeTemplate.Upgrades(Entry)
			{
				if ((Entry.ClassFilter != "") && (Entry.ClassFilter != string(UnitState.GetSoldierClassTemplate().DataName)))
					continue;

				foreach Entry.EquipmentNames(ItemName)
					EmitItem(slot, ItemName);
			}
		}

		// UpdateData's ability diff.
		for (k = 0; k < PreviousSoldierAbilities.Length; k++)
		{
			PreviousAbility = PreviousSoldierAbilities[k];
			for (j = SoldierAbilities.Length - 1; j >= 0; j--)
			{
				CurrentAbility = SoldierAbilities[j];
				if (CurrentAbility.AbilityName == PreviousAbility.AbilityName)
					SoldierAbilities.Remove(j, 1);
			}
		}

		for (j = 0; j < SoldierAbilities.Length && !bisDead; j++)
			EmitAbility(slot, SoldierAbilities[j].AbilityName);

		++slot;
	}
}

private function EmitAbility(int Slot, name AbilityName)
{
	local X2AbilityTemplate AbilityTemplate;
	local string Rec;

	AbilityTemplate = class'X2AbilityTemplateManager'.static.GetAbilityTemplateManager().FindAbilityTemplate(AbilityName);
	if (AbilityTemplate == none)
		return;

	Rec = "kind=ability";
	AddField(Rec, "slot", string(Slot));
	AddField(Rec, "name", class'UIUtilities_Text'.static.CapsCheckForGermanScharfesS(AbilityTemplate.LocFriendlyName));
	Emit(Rec);
}

private function EmitItem(int Slot, name ItemName)
{
	local X2ItemTemplate ItemTemplate;
	local string Rec;

	ItemTemplate = class'X2ItemTemplateManager'.static.GetItemTemplateManager().FindItemTemplate(ItemName);
	if (ItemTemplate == none)
		return;

	Rec = "kind=item";
	AddField(Rec, "slot", string(Slot));
	AddField(Rec, "name", class'UIUtilities_Text'.static.CapsCheckForGermanScharfesS(ItemTemplate.GetItemFriendlyName()));
	Emit(Rec);
}

// ---------------------------------------------------------------------------------------
// The medal screen (UITLELadderMedalScreen)
// ---------------------------------------------------------------------------------------
//
//   kind=header  ...                                  (EmitTitleBlock, completed rung)
//   kind=medal   score  threshold  medal
//   kind=rewards label  nextlabel  nextname  unlock  unlockdesc  greyed
//
// OnInit's own medal arithmetic (strictly greater than each threshold), its own threshold read
// (MedalThresholds[iMedal - 1], which for no medal is the engine's out-of-range zero), and its own
// three-way rewards block - greyed when the profile had already unlocked them, present when this
// run earned them, absent otherwise. The filled MEDAL ACHIEVED line is read natively: OnInit
// writes it back into the instance's own field.
function DescribeMedal()
{
	local XComGameState_LadderProgress LadderData;
	local int iMedal, i, ladderHighScore;
	local string Rec;

	NativeResult = "";

	LadderData = XComGameState_LadderProgress(`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_LadderProgress'));
	if (LadderData == none)
		return;

	EmitTitleBlock(LadderData, true);

	iMedal = 0;
	for (i = 0; i < LadderData.MedalThresholds.Length; i++)
	{
		if (LadderData.CumulativeScore > LadderData.MedalThresholds[i])
			iMedal = i + 1;
	}

	Rec = "kind=medal";
	AddField(Rec, "score", string(LadderData.CumulativeScore));
	if (iMedal >= 1 && iMedal - 1 < LadderData.MedalThresholds.Length)
		AddField(Rec, "threshold", string(LadderData.MedalThresholds[iMedal - 1]));
	else
		AddField(Rec, "threshold", "0");
	if (iMedal >= 0 && iMedal < class'UITLE_LadderModeMenu'.default.m_MedalTypes.Length)
		AddField(Rec, "medal", class'UITLE_LadderModeMenu'.default.m_MedalTypes[iMedal]);
	Emit(Rec);

	ladderHighScore = `XPROFILESETTINGS.Data.GetLadderHighScore(LadderData.LadderIndex);

	if (LadderData.MedalThresholds.Length > 0 && ladderHighScore >= LadderData.MedalThresholds[0])
		EmitRewards(LadderData, true);
	else if (iMedal >= 1 && LadderData.LadderIndex <= 4)
		EmitRewards(LadderData, false);
}

private function EmitRewards(XComGameState_LadderProgress LadderData, bool bGreyed)
{
	local string Rec;

	Rec = "kind=rewards";
	AddField(Rec, "label", class'UITLELadderMedalScreen'.default.m_RewardsLabel);
	AddField(Rec, "nextlabel", class'UITLELadderMedalScreen'.default.m_UnlockedNextLadder);
	if (LadderData.LadderIndex + 1 < LadderData.NarrativeLadderNames.Length)
		AddField(Rec, "nextname", LadderData.NarrativeLadderNames[LadderData.LadderIndex + 1]);
	if (LadderData.LadderIndex >= 0 && LadderData.LadderIndex < class'UITLELadderMedalScreen'.default.m_NarrativeLadderUnlocks.Length)
		AddField(Rec, "unlock", class'UITLELadderMedalScreen'.default.m_NarrativeLadderUnlocks[LadderData.LadderIndex]);
	AddField(Rec, "unlockdesc", class'UITLELadderMedalScreen'.default.m_LadderUnlockDescription);
	if (bGreyed)
		AddField(Rec, "greyed", "1");
	Emit(Rec);
}

// ---------------------------------------------------------------------------------------
// The medal progress screen (UITLELadderMedalProgressScreen)
// ---------------------------------------------------------------------------------------
//
//   kind=header   ...                                 (EmitTitleBlock, completed rung)
//   kind=progress label  medallabel  rung  size
//   kind=marker   index  label  threshold  reached
//   kind=meter    score  pct
//
// UpdateData's pushes: the timeline (one node per mission - complete, current, locked - which the
// rung count states), the three medal markers with the ladder menu's medal words and their
// thresholds, and the meter filled to score over the gold threshold.
function DescribeMedalProgress()
{
	local XComGameState_LadderProgress LadderData;
	local int i, goldMedalProgress, currentMedalProgress;
	local string Rec;

	NativeResult = "";

	LadderData = XComGameState_LadderProgress(`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_LadderProgress'));
	if (LadderData == none)
		return;

	EmitTitleBlock(LadderData, true);

	Rec = "kind=progress";
	AddField(Rec, "label", class'UITLE_LadderModeMenu'.default.m_ProgressLabel);
	AddField(Rec, "medallabel", class'UITLE_LadderModeMenu'.default.m_MedalProgress);
	AddField(Rec, "rung", string(LadderData.LadderRung));
	AddField(Rec, "size", string(LadderData.LadderSize));
	Emit(Rec);

	goldMedalProgress = LadderData.GetLadderMedalThreshold(LadderData.LadderIndex, 2);
	for (i = 0; i < 3; i++)
	{
		currentMedalProgress = LadderData.GetLadderMedalThreshold(LadderData.LadderIndex, i);
		Rec = "kind=marker";
		AddField(Rec, "index", string(i));
		if (i + 1 < class'UITLE_LadderModeMenu'.default.m_MedalTypes.Length)
			AddField(Rec, "label", class'UITLE_LadderModeMenu'.default.m_MedalTypes[i + 1]);
		AddField(Rec, "threshold", string(currentMedalProgress));
		if (LadderData.CumulativeScore >= currentMedalProgress)
			AddField(Rec, "reached", "1");
		Emit(Rec);
	}

	Rec = "kind=meter";
	AddField(Rec, "score", string(LadderData.CumulativeScore));
	if (goldMedalProgress > 0)
		AddField(Rec, "pct", string((100 * LadderData.CumulativeScore) / goldMedalProgress));
	Emit(Rec);
}

// ---------------------------------------------------------------------------------------
// The end screen (UITLELadderEndScreen)
// ---------------------------------------------------------------------------------------
//
//   kind=header ...                                   (EmitTitleBlock, completed rung)
//   kind=stat   index  value                          (six, the analytics metrics OnInit reads)
//
// The captions are the screen's localized array and native reads them; only the values are here.
function DescribeEndStats()
{
	local XComGameState_LadderProgress LadderData;
	local XComGameState_Analytics Analytics;
	local array<string> Metrics;
	local string Rec;
	local int i;

	NativeResult = "";

	LadderData = XComGameState_LadderProgress(`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_LadderProgress'));
	EmitTitleBlock(LadderData, true);

	Analytics = XComGameState_Analytics(`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_Analytics'));
	if (Analytics == none)
		return;

	Metrics.AddItem("ACC_UNIT_KILLS");
	Metrics.AddItem("TLE_LADDER_BONUS");
	Metrics.AddItem("BATTLES_LOST");
	Metrics.AddItem("TLE_INJURIES");
	Metrics.AddItem("UNITS_LOST");
	Metrics.AddItem("TURN_COUNT");

	for (i = 0; i < Metrics.Length; i++)
	{
		Rec = "kind=stat";
		AddField(Rec, "index", string(i));
		AddField(Rec, "value", Analytics.GetValueAsString(Metrics[i]));
		Emit(Rec);
	}
}

// ---------------------------------------------------------------------------------------
// The challenge result (UIChallengePostScreen)
// ---------------------------------------------------------------------------------------
//
//   kind=result  gamertag  time  score  maxlabel  max
//
// setScreenData's operands that are not the screen's own labels: the player's name (the replay's
// recorded user in a replay), the timestamp, the two scores off the challenge manager, and the
// MAXIMUM POINTS label the screen borrows from the scoring dialog. The timestamp is re-derived at
// the moment of asking, as OnInit derived it at the moment of showing; the minute can differ.
function DescribeChallengePost()
{
	local XComChallengeModeManager ChallengeModeManager;
	local WorldInfo LocalWorldInfo;
	local int Year, Month, DayOfWeek, Day, Hour, Minute, Second, Millisecond;
	local string TimeString, GamerTag, Rec;
	local XComTacticalGRI GRI;

	NativeResult = "";

	ChallengeModeManager = `CHALLENGEMODE_MGR;
	if (ChallengeModeManager == none)
		return;

	LocalWorldInfo = class'Engine'.static.GetCurrentWorldInfo();
	GRI = XComTacticalGRI(LocalWorldInfo.GRI);
	if (GRI != none && GRI.ReplayMgr != none && GRI.ReplayMgr.bInReplay)
		GamerTag = `ONLINEEVENTMGR.m_sReplayUserID;
	else if (LocalWorldInfo.GetALocalPlayerController() != none && LocalWorldInfo.GetALocalPlayerController().PlayerReplicationInfo != none)
		GamerTag = LocalWorldInfo.GetALocalPlayerController().PlayerReplicationInfo.PlayerName;

	LocalWorldInfo.GetSystemTime(Year, Month, DayOfWeek, Day, Hour, Minute, Second, Millisecond);
	`ONLINEEVENTMGR.FormatTimeStampSingleLine12HourClock(TimeString, Year, Month, Day, Hour, Minute);

	Rec = "kind=result";
	AddField(Rec, "gamertag", GamerTag);
	AddField(Rec, "time", TimeString);
	AddField(Rec, "score", string(ChallengeModeManager.GetTotalScore()));
	AddField(Rec, "maxlabel", class'UIChallengeModeScoringDialog'.default.m_TotalLabel);
	AddField(Rec, "max", string(ChallengeModeManager.GetMaxChallengeScore()));
	Emit(Rec);
}

// ---------------------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------------------

private function Emit(string Rec)
{
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

// Empty values are DROPPED rather than written as an empty field: a missing key means "the
// screen is not showing this", which is exactly the distinction the recipes need.
private function AddField(out string Rec, string Key, string Value)
{
	class'X2AccessWire'.static.AddField(Rec, Key, Value);
}

defaultproperties
{
	NativeResult = ""
}
