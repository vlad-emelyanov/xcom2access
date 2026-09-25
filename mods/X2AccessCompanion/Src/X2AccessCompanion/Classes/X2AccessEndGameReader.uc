// Reads the two campaign endings and the Bar memorial detail card. Both screens
// push their text into Flash; the game-owned data and labels remain available here.
class X2AccessEndGameReader extends Object
	dependson(UIEndGameStats);

var string NativeResult;

function DescribeEndGame()
{
	local UIEndGameStats Screen;
	local array<UIEndGameStats.TEndGameStat> Rows;
	local string Rec;
	local int i;
	local bool HasWorld;

	NativeResult = "";
	Screen = UIEndGameStats(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none || Screen.Analytics == none || Screen.AnalyticsManager == none
		|| Screen.CurrentPage < 0 || Screen.CurrentPage >= class'UIEndGameStats'.const.NUM_PAGES)
		return;

	Rec = "kind=header";
	class'X2AccessWire'.static.AddField(Rec, "title", Screen.bGameWon ? Screen.Victory : Screen.Defeat);
	class'X2AccessWire'.static.AddField(Rec, "summary", Screen.GameSummary);
	class'X2AccessWire'.static.AddField(Rec, "difficulty", Screen.Difficulty);
	class'X2AccessWire'.static.AddField(Rec, "level", Caps(class'UIShellDifficulty'.default.m_arrDifficultyTypeStrings[`CAMPAIGNDIFFICULTYSETTING]));
	if (`GAME.m_bIronman)
		class'X2AccessWire'.static.AddField(Rec, "ironman", Screen.Ironman);
	class'X2AccessWire'.static.AddField(Rec, "date", Screen.Date);
	class'X2AccessWire'.static.AddField(Rec, "day", class'X2StrategyGameRulesetDataStructures'.static.GetDateString(class'UIUtilities_Strategy'.static.GetGameTime().CurrentTime, true));
	class'X2AccessWire'.static.AddField(Rec, "page", Screen.Page);
	class'X2AccessWire'.static.AddField(Rec, "number", string(Screen.CurrentPage + 1) $ "/" $ string(class'UIEndGameStats'.const.NUM_PAGES));
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);

	if (Screen.CurrentPage == 0)
	{
		Rec = "kind=doom";
		class'X2AccessWire'.static.AddField(Rec, "label", Screen.Doom);
		class'X2AccessWire'.static.AddField(Rec, "value", string(class'UIUtilities_Strategy'.static.GetAlienHQ().Doom));
		class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
	}

	switch (Screen.CurrentPage)
	{
	case 0: Rows = Screen.GetFirstPageStats(); break;
	case 1: Rows = Screen.GetXPackPageStats(); break;
	case 2: Rows = Screen.GetSecondPageStats(); break;
	case 3: Rows = Screen.GetThirdPageStats(); break;
	case 4: Rows = Screen.GetFourthPageStats(); break;
	}
	HasWorld = Screen.AnalyticsManager.WorldStatsAvailable();
	for (i = 0; i < Rows.Length; ++i)
	{
		Rec = "kind=stat";
		class'X2AccessWire'.static.AddField(Rec, "index", string(i));
		class'X2AccessWire'.static.AddField(Rec, "label", Rows[i].Label);
		class'X2AccessWire'.static.AddField(Rec, "youlabel", Screen.You);
		class'X2AccessWire'.static.AddField(Rec, "you", Rows[i].YouValue);
		if (HasWorld)
		{
			class'X2AccessWire'.static.AddField(Rec, "worldlabel", Screen.World);
			class'X2AccessWire'.static.AddField(Rec, "world", Rows[i].WorldValue != "" ? Rows[i].WorldValue : "--");
		}
		class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
	}
}

function DescribeMemorial()
{
	local UIBarMemorial_Details Screen;
	local XComGameState_Unit Unit;
	local X2SoldierClassTemplate SoldierClass;
	local string Rec;
	local int i, Days;

	NativeResult = "";
	Screen = UIBarMemorial_Details(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none || Screen.History == none || Screen.Analytics == none || Screen.Header == none)
		return;
	Unit = XComGameState_Unit(Screen.History.GetGameStateForObjectID(Screen.UnitReference.ObjectID));
	if (Unit == none)
		return;

	Rec = "kind=header";
	class'X2AccessWire'.static.AddField(Rec, "name", Unit.GetName(eNameType_FullNick));
	SoldierClass = Unit.GetSoldierClassTemplate();
	if (SoldierClass != none)
		class'X2AccessWire'.static.AddField(Rec, "class", SoldierClass.DisplayName);
	class'X2AccessWire'.static.AddField(Rec, "rank", `GET_RANK_STR(Unit.GetRank(), Unit.GetSoldierClassTemplateName()));
	class'X2AccessWire'.static.AddField(Rec, "dateLabel", Screen.Header.m_strDateKilledLabel);
	class'X2AccessWire'.static.AddField(Rec, "date", class'X2StrategyGameRulesetDataStructures'.static.GetDateString(Unit.GetKIADate()));
	class'X2AccessWire'.static.AddField(Rec, "missionsLabel", Screen.Header.m_strMissionsLabel);
	class'X2AccessWire'.static.AddField(Rec, "missions", string(Unit.GetNumMissions()));
	class'X2AccessWire'.static.AddField(Rec, "killsLabel", Screen.Header.m_strKillsLabel);
	class'X2AccessWire'.static.AddField(Rec, "kills", string(Unit.GetNumKills()));
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);

	Rec = "kind=section";
	class'X2AccessWire'.static.AddField(Rec, "label", Screen.m_strSoldierStats);
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
	for (i = 2; i <= 7; ++i)
	{
		Rec = "kind=stat";
		class'X2AccessWire'.static.AddField(Rec, "index", string(i));
		class'X2AccessWire'.static.AddField(Rec, "label", Screen.m_statLabels[i]);
		switch (i)
		{
		case 2:
			Days = int(Screen.Analytics.GetUnitFloatValue("ACC_UNIT_SERVICE_LENGTH", Screen.UnitReference) / 24.0f);
			class'X2AccessWire'.static.AddField(Rec, "value", string(Days) @ Screen.m_strDays);
			break;
		case 3: class'X2AccessWire'.static.AddField(Rec, "value", Unit.m_strCauseOfDeath); break;
		case 4:
			Days = int(Screen.Analytics.GetUnitFloatValue("ACC_UNIT_HEALING", Screen.UnitReference) / 24.0f);
			class'X2AccessWire'.static.AddField(Rec, "value", string(Days) @ Screen.m_strDays);
			break;
		case 5: class'X2AccessWire'.static.AddField(Rec, "value", Screen.Analytics.GetUnitValueAsString("ACC_UNIT_SUCCESSFUL_ATTACKS", Screen.UnitReference)); break;
		case 6: class'X2AccessWire'.static.AddField(Rec, "value", Screen.Analytics.GetUnitValueAsString("ACC_UNIT_DEALT_DAMAGE", Screen.UnitReference)); break;
		case 7: class'X2AccessWire'.static.AddField(Rec, "value", Screen.Analytics.GetUnitValueAsString("ACC_UNIT_ABILITIES_RECIEVED", Screen.UnitReference)); break;
		}
		class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
	}
	if (Screen.GetEpitaphText() != "")
	{
		Rec = "kind=epitaph";
		class'X2AccessWire'.static.AddField(Rec, "label", Screen.m_strSoldierEpitaph);
		class'X2AccessWire'.static.AddField(Rec, "value", Screen.GetEpitaphText());
		class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
	}
	Rec = "kind=button";
	class'X2AccessWire'.static.AddField(Rec, "action", "epitaph");
	class'X2AccessWire'.static.AddField(Rec, "label", Screen.m_strEpitaphButton);
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
	Rec = "kind=button";
	class'X2AccessWire'.static.AddField(Rec, "action", "pool");
	class'X2AccessWire'.static.AddField(Rec, "label", Screen.m_strPoolButtonText);
	class'X2AccessWire'.static.AddRecord(NativeResult, Rec);
}

function ActivateMemorial(int Action)
{
	local UIBarMemorial_Details Screen;
	NativeResult = "";
	Screen = UIBarMemorial_Details(`SCREENSTACK.GetCurrentScreen());
	if (Screen == none)
		return;
	if (Action == 0)
		Screen.OnSetEpitaph(none);
	else if (Action == 1)
		Screen.OnPoolButton(none);
}
