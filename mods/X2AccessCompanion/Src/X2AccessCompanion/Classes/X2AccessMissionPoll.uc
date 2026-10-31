// X2AccessMissionPoll
// The tactical HUD facts that arrive with NO event the mod can hear, spoken by polling what the
// HUD itself retains: the orientation line once a mission has loaded, the reinforcement countdown
// panel, and the objective list's completed and failed rows.
//
// WHY A POLL. A loaded save runs no turn-begin event and the tutorial runs no event at all, so a
// listener cannot orient a player on either; and nothing is triggered when the countdown panel
// ticks or when a list row flips its tick box. All three are STATE THE SCREEN KEEPS — the panel is
// a child widget with a visibility flag, the list rows keep the ObjectiveDisplayInfo they were
// built from (docs/surfaces/tactical.md, objectives list) — so reading the screen is the
// read-from-the-screen rule applied, and it works in the tutorial for the same reason Ctrl+X does.
//
// HEARTBEAT. Native calls X2AccessReplayNarration.PollReplay every 100 ms on every world,
// the shell included, and that function calls Poll() first. Everything here therefore bails on the
// cheapest test first (is this a tactical world) and does no history walk unless a panel is up.
//
// SESSION. This is a CDO, so its memos outlive a mission. A session ends when the world is no
// longer tactical OR when WorldInfo.TimeSeconds runs backwards: TimeSeconds restarts with every
// level, and a load from the in-game menu replaces the tactical world with another tactical world,
// which the first test alone would never see.
//
// GC rule: value-typed state only, NEVER object references. The config specifier is the only route to per-class
// storage from a static function (see X2AccessTacticalEvents); no ini section is shipped and
// SaveConfig is never called, so every value starts at its declared default each launch.
class X2AccessMissionPoll extends Object config(Game);

// The orientation line has been given for this session.
var config bool bBriefed;

// UIMissionIntro was on the screen stack at some point this session: this load is a NEW mission,
// the only case in which the game draws the operation name.
var config bool bIntroSeen;

// Level time at the last poll; a smaller value now means a different world.
var config float LastSeenTime;

// Polls spent waiting for the objective list to fill before the orientation line is given.
var config int BriefWaits;

// "key~=~S~|~" per objective row, S being 0 pending, 1 completed, 2 failed.
var config string ObjectiveMemo;

// The objective baseline has been taken: the list was visible with rows.
var config bool bObjBaselined;

// "|ObjectID=Countdown|" per pending reinforcement group already spoken at that count.
var config string CountdownMemo;

const MEMO_SEP = "~|~";
const MEMO_EQ = "~=~";
const BRIEF_WAIT_LIMIT = 30;

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

static function Poll()
{
	local WorldInfo WI;
	local XComTacticalGRI GRI;
	local UIScreenStack Stack;
	local UITacticalHUD HUD;

	WI = class'WorldInfo'.static.GetWorldInfo();
	if (WI != none)
		GRI = XComTacticalGRI(WI.GRI);

	if (GRI == none)
	{
		EndSession();
		return;
	}

	if (WI.TimeSeconds < default.LastSeenTime)
		EndSession();
	default.LastSeenTime = WI.TimeSeconds;

	Stack = `SCREENSTACK;
	if (Stack == none)
		return;

	// The intro is a timed screen shown during the dropship cinematic, before the HUD is
	// usable; seeing it is the only way to tell a new mission from a loaded one.
	if (!default.bBriefed && Stack.GetFirstInstanceOf(class'UIMissionIntro') != none)
		default.bIntroSeen = true;

	HUD = UITacticalHUD(Stack.GetFirstInstanceOf(class'UITacticalHUD'));
	if (HUD == none || !HUD.bIsInited)
		return;

	if (!default.bBriefed)
	{
		if (!HudReady(GRI))
			return;

		// The mission heading is read off the objective list, which fills a moment after the
		// HUD is ready. Wait for its first populated read (100 ms a poll); a mission that
		// draws no list at all is briefed without a heading after BRIEF_WAIT_LIMIT polls.
		if (!ListPopulated(HUD) && default.BriefWaits < BRIEF_WAIT_LIMIT)
		{
			default.BriefWaits++;
			return;
		}

		default.bBriefed = true;
		Brief(Stack, HUD);

		// A load must not recite history: whatever the list already shows is the baseline.
		default.bObjBaselined = PollObjectives(HUD, true);
		PollCountdown(HUD);
		return;
	}

	PollCountdown(HUD);
	// Until the list is visible with rows the baseline is retaken silently every poll.
	if (!default.bObjBaselined)
		default.bObjBaselined = PollObjectives(HUD, true);
	else
		PollObjectives(HUD, false);
}

// Forget the session. The first call after the last tactical world is the only one that does any
// work; LastSeenTime < 0 marks "already ended", and the shell polls this ten times a second.
static function EndSession()
{
	if (default.LastSeenTime < 0)
		return;

	default.LastSeenTime = -1;
	default.bBriefed = false;
	default.bIntroSeen = false;
	default.ObjectiveMemo = "";
	default.bObjBaselined = false;
	default.BriefWaits = 0;
	default.CountdownMemo = "";
	class'X2AccessTacticalEvents'.static.ResetTurnBanner();
	class'X2AccessTacticalEvents'.static.ResetSpawnNarration();
}

// True once the orientation line has been given. The concealment-ENTERED events stand down until
// then: a mission that starts concealed fires them before the HUD exists, and the orientation line
// already says it (X2AccessTacticalEvents.OnSquadConcealmentEntered).
static function bool IsBriefed()
{
	return default.bBriefed;
}

// The point at which the player has the keyboard: the HUD is built, the rules are in the unit
// actions phase with a soldier selected, and nothing is still being visualized. The tutorial
// never stops visualizing, so there the busy test is skipped, as it is for every replay.
static function bool HudReady(XComTacticalGRI GRI)
{
	local X2TacticalGameRuleset Rules;

	if (!ReplayRunning(GRI) && class'XComGameStateVisualizationMgr'.static.VisualizerBusy())
		return false;

	Rules = `TACTICALRULES;
	if (Rules == none || Rules.GetStateName() != 'TurnPhase_UnitActions')
		return false;

	return class'X2AccessTileReader'.static.ActiveUnitState() != none;
}

static function bool ReplayRunning(XComTacticalGRI GRI)
{
	return GRI != none && GRI.ReplayMgr != none && GRI.ReplayMgr.bInReplay;
}

// The history frame the player has been SHOWN, or -1 for "now". In a replay the whole script is
// already in the history, so reading the latest state would read the future.
static function int ShownIndex()
{
	local XComTacticalGRI GRI;

	GRI = XComTacticalGRI(class'WorldInfo'.static.GetWorldInfo().GRI);
	if (!ReplayRunning(GRI) || GRI.VisualizationMgr == none)
		return -1;
	if (GRI.VisualizationMgr.LastStateHistoryVisualized <= 0)
		return -1;
	return GRI.VisualizationMgr.LastStateHistoryVisualized;
}

// ---------------------------------------------------------------------------
// The orientation line
// ---------------------------------------------------------------------------

// ONE line, once per session: what the HUD shows at a glance and the player cannot see. Only facts
// the game DRAWS, each under the condition it is drawn under:
//   the heading         the objective list's header row (UIObjectiveListItem.RefreshType: ShowHeader
//                       makes eUIODT_ClickableButton, SetLabelButton draws DisplayLabel). Drawn on
//                       every mission, loaded or new, so it leads the line and is never silent.
//   the operation name  UIMissionIntro, a new mission's intro screen only (UIMissionIntro.OnInit);
//                       the HUD draws no mission name, so a loaded save gets none.
//   concealment         the HUD's concealment banner for the selected soldier
//                       (UITacticalHUD.RealizeConcealmentStatus), nothing when not concealed.
//   the stage counters  UISpecialMissionHUD_TurnCounter, only while a panel is visible.
// The turn is not here: the game draws NO banner for the XCom turn (XComPresentationLayer.UIEndTurn,
// eTurnOverlay_Local), and the turn counter's "Your Turn" is one of the counters above when it
// is drawn at all. Objectives stay on Ctrl+X and the Objectives stop.
static function Brief(UIScreenStack Stack, UITacticalHUD HUD)
{
	local XComGameState_BattleData BattleData;
	local UISpecialMissionHUD Special;
	local string Line, Heading, OpLine;

	if (default.bIntroSeen)
	{
		BattleData = XComGameState_BattleData(`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_BattleData', true));
		if (BattleData != none && BattleData.m_strOpName != "")
		{
			if (class'UIMissionIntro'.default.m_strOperationTitle != "")
				OpLine = class'UIMissionIntro'.default.m_strOperationTitle @ BattleData.m_strOpName;
			else
				OpLine = BattleData.m_strOpName;
		}
	}

	// The heading leads. When the operation name already says the same words, say them once.
	Heading = MissionHeading(HUD);
	if (Heading != "" && PlainText(OpLine) == Heading)
		OpLine = "";
	class'X2AccessHelper'.static.AppendPart(Line, Heading);
	class'X2AccessHelper'.static.AppendPart(Line, OpLine);

	class'X2AccessHelper'.static.AppendPart(Line, ConcealmentWord(class'X2AccessTileReader'.static.ActiveUnitState()));

	Special = UISpecialMissionHUD(Stack.GetScreen(class'UISpecialMissionHUD'));
	if (Special != none)
	{
		class'X2AccessHelper'.static.AppendPart(Line, CounterWords(Special.m_kGenericTurnCounter));
		class'X2AccessHelper'.static.AppendPart(Line, CounterWords(Special.m_kTurnCounter2));
	}

	if (Line != "")
		class'X2AccessNarrator'.static.Alert(Line);
}

// The list has been filled: it is shown and holds at least one row. UIObjectiveList.
// RefreshObjectivesDisplay shows it only when an objectives state exists and adds the Title row
// whenever a regular objective is present, so a populated list always has rows.
static function bool ListPopulated(UITacticalHUD HUD)
{
	return HUD != none && HUD.m_kObjectivesControl != none
		&& HUD.m_kObjectivesControl.bIsVisible && HUD.m_kObjectivesControl.Items.Length > 0;
}

// The mission's heading as the objective list draws it: the label of the first header row of the
// objectives block. A header row is ShowHeader (UIObjectiveListItem.RefreshType makes it
// eUIODT_ClickableButton) and SetLabelButton pushes DisplayLabel into Flash, so DisplayLabel is
// the drawn word. Dark-event rows are the other block and are skipped. The list keeps the data
// the row was built from (Data = NewData ends every Set row), and is driven by the visualization
// observer, so this reads the same in the tutorial. "" when the list draws no header.
static function string MissionHeading(UITacticalHUD HUD)
{
	local UIObjectiveListItem Item;
	local string Label;
	local int i;

	if (!ListPopulated(HUD))
		return "";

	for (i = 0; i < HUD.m_kObjectivesControl.Items.Length; i++)
	{
		Item = HUD.m_kObjectivesControl.Items[i];
		if (Item == none || Item.Type != eUIODT_ClickableButton || Item.Data.bIsDarkEvent)
			continue;

		Label = PlainText(Item.Data.DisplayLabel);
		if (Label != "")
			return Label;
	}
	return "";
}

// The word the HUD's concealment banner shows for this soldier, in the order
// UITacticalHUD.RealizeConcealmentStatus tests: shadow, squad, individual. "" when not concealed,
// because the game draws no banner then (the revealed banner is a CHANGE, not a state).
static function string ConcealmentWord(XComGameState_Unit Unit)
{
	if (Unit == none || !Unit.IsConcealed())
		return "";
	if (Unit.IsSuperConcealed())
		return class'UITacticalHUD'.default.m_strReaperConcealed;
	if (Unit.IsSquadConcealed())
		return class'UITacticalHUD'.default.m_strSquadConcealed;
	return class'UITacticalHUD'.default.m_strConcealed;
}

// One counter panel, or "" when it is hidden, removed or wordless — the same test the native Ctrl+X
// reader applies (docs/surfaces/tactical.md, stage counters): every Set call ends in Show(), so a
// hidden panel is one the player is not being shown. The sub-label and the number are ONE phrase.
static function string CounterWords(UISpecialMissionHUD_TurnCounter Counter)
{
	local string Result, Tail;

	if (Counter == none || !Counter.bIsInited || Counter.bIsRemoved || !Counter.bIsVisible)
		return "";

	// The turn counter's own Your Turn label is the XCom turn line OnPlayerTurnBegun speaks.
	if (PlainText(Counter.m_sLabel) != PlainText(class'UISpecialMissionHUD_TurnCounter'.default.m_strPlayerTurnLabel))
		class'X2AccessHelper'.static.AppendPart(Result, PlainText(Counter.m_sLabel));

	class'X2AccessHelper'.static.AppendJoined(Tail, PlainText(Counter.m_sSubLabel), " ");
	class'X2AccessHelper'.static.AppendJoined(Tail, PlainText(Counter.m_sCounter), " ");
	if (Counter.m_bExpired)
		class'X2AccessHelper'.static.AppendComma(Tail, class'X2AccessVocab'.default.ObjectiveExpired);
	if (Counter.m_bLocked)
		class'X2AccessHelper'.static.AppendComma(Tail, class'X2AccessVocab'.default.ObjectiveLocked);
	if (Counter.m_bInfinity)
		class'X2AccessHelper'.static.AppendComma(Tail, class'X2AccessVocab'.default.ObjectiveUnlimited);
	class'X2AccessHelper'.static.AppendPart(Result, Tail);

	return Result;
}

// ---------------------------------------------------------------------------
// The reinforcement countdown panel
// ---------------------------------------------------------------------------

// UITacticalHUD_Countdown.RefreshCounter shows the panel with the group's turns when
// Countdown > 0 and hides it otherwise, and the panel is a child widget whose own flag is toggled
// deliberately, so "the panel is visible" is the game's rule and is tested first. The panel keeps
// the NUMBER only inside Flash, so the number comes from the spawner it was fed. The memo is per
// group and per count: spoken when a group's panel first appears and each time its count changes.
// The panel itself shows one group at a time; with two pending, each is spoken under its own
// identity rather than guessing which one the panel happens to hold.
static function PollCountdown(UITacticalHUD HUD)
{
	local XComGameStateHistory History;
	local XComGameState_AIReinforcementSpawner Spawner;
	local string Key, NewMemo, Line;
	local int ShownAt;

	if (HUD.m_kCountdown == none || !HUD.m_kCountdown.bIsVisible)
		return;

	History = `XCOMHISTORY;
	ShownAt = ShownIndex();

	foreach History.IterateByClassType(class'XComGameState_AIReinforcementSpawner', Spawner)
	{
		// Re-fetch by identity at the frame the player has been shown (-1 is the present).
		Spawner = XComGameState_AIReinforcementSpawner(History.GetGameStateForObjectID(Spawner.ObjectID, , ShownAt));
		if (Spawner == none || Spawner.Countdown <= 0)
			continue;

		Key = "|" $ string(Spawner.ObjectID) $ "=" $ string(Spawner.Countdown) $ "|";
		NewMemo $= Key;
		if (InStr(default.CountdownMemo, Key) != INDEX_NONE)
			continue;

		Line = class'X2AccessObjectScanner'.static.ReinforcementsLabel();
		class'X2AccessHelper'.static.AppendComma(Line, class'X2AccessObjectScanner'.static.TurnWords(Spawner.Countdown));
		class'X2AccessNarrator'.static.Alert(Line $ ".");
	}

	default.CountdownMemo = NewMemo;
}

// ---------------------------------------------------------------------------
// The objective list
// ---------------------------------------------------------------------------

// The route is POLLING WHAT THE LIST RETAINS, for three reasons. The game draws completion as a
// tick box and a colour and nothing else (UIObjectiveListItem.SetCheckboxRow, SetTimerRow), so
// there is no banner or event to tap; XComGameState_ObjectivesList is rewritten by Kismet with no
// event of its own; and the row keeps the ObjectiveDisplayInfo it was built from, which is exactly
// what the sighted player's picture is made of — and works in the tutorial, where the history is
// the future and no event fires.
//
// Identity is the game's own: UIObjectiveList finds a row by MissionType plus DisplayLabel
// (XComGameState_ObjectivesList.GetObjectiveDisplayInfoIndex). Only the two row kinds that DRAW a
// state are spoken — checkbox and timer — and only rows currently visible (a collapsed group
// hides its members). bBaseline records without speaking: the first read after a load is the
// state the player already knew.
static function bool PollObjectives(UITacticalHUD HUD, bool bBaseline)
{
	local UIObjectiveList List;
	local UIObjectiveListItem Item;
	local string Key, NewMemo, Label, Line;
	local int i, RowState, OldRowState;

	List = HUD.m_kObjectivesControl;
	if (List == none || !List.bIsVisible || List.Items.Length == 0)
		return false;

	for (i = 0; i < List.Items.Length; i++)
	{
		Item = List.Items[i];
		if (Item == none || Item.Data.MissionType == "TITLE" || Item.Data.MissionType == "DARKEVENTTITLE"
			|| Item.Data.bIsDarkEvent)
			continue;

		RowState = 0;
		if (Item.Data.ShowCompleted)
			RowState = 1;
		else if (Item.Data.ShowFailed)
			RowState = 2;

		Key = Item.Data.MissionType $ "/" $ Item.Data.DisplayLabel;
		NewMemo $= MEMO_SEP $ Key $ MEMO_EQ $ string(RowState);

		if (bBaseline || RowState == 0)
			continue;
		if (Item.Type != eUIODT_Checkbox && Item.Type != eUIODT_Timer)
			continue;
		if (!Item.bIsVisible)
			continue;

		OldRowState = MemoState(Key);
		if (OldRowState == RowState)
			continue;

		Label = PlainText(Item.Data.DisplayLabel);
		if (Label == "")
			continue;

		Line = Label;
		class'X2AccessHelper'.static.AppendComma(Line, RowState == 1
			? class'X2AccessVocab'.default.ObjectiveCompleted
			: class'X2AccessVocab'.default.ObjectiveFailed);
		class'X2AccessNarrator'.static.Alert(Line);
	}

	default.ObjectiveMemo = NewMemo;
	return true;
}

// The remembered state of one row, 0 when the row was not in the memo.
static function int MemoState(string Key)
{
	local int At;

	// Every entry is wrapped in separators on both sides of its key, so a key that is a suffix
	// or prefix of another cannot match.
	At = InStr(default.ObjectiveMemo, MEMO_SEP $ Key $ MEMO_EQ);
	if (At == INDEX_NONE)
		return 0;

	return int(Mid(default.ObjectiveMemo, At + Len(MEMO_SEP) + Len(Key) + Len(MEMO_EQ), 1));
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

// Game text with its markup and edge blanks removed: the tags a localized label may carry, and
// the single space the multiplayer counter writes into a sub-label it wants empty.
static function string PlainText(string Text)
{
	local int Open, Close;

	Open = InStr(Text, "<");
	while (Open != INDEX_NONE)
	{
		Close = InStr(Mid(Text, Open), ">");
		if (Close == INDEX_NONE)
			break;
		Text = Left(Text, Open) $ Mid(Text, Open + Close + 1);
		Open = InStr(Text, "<");
	}

	while (Len(Text) > 0 && Left(Text, 1) == " ")
		Text = Mid(Text, 1);
	while (Len(Text) > 0 && Right(Text, 1) == " ")
		Text = Left(Text, Len(Text) - 1);

	return Text;
}
