// X2AccessDropshipReader
// The Skyranger load screen (UIDropShipBriefing_MissionStart / _MissionEnd) — the words half.
// Same contract as X2AccessSaveReader and X2AccessAvengerReader: the C++ layer owns the gate,
// the key and the speech, calls DescribeBriefing() on this class's default object via
// ProcessEvent (game thread, from the frame pump) and reads NativeResult back through reflection
// right after the call returns. Nothing here runs on its own — no listeners, no timers.
//
// WHY THE WORDS LIVE HERE. UIDropShipBriefing_MissionStart.UpdateBriefingScreen resolves the
// operation name, location, briefing paragraph and up to three objective lines into LOCALS,
// pushes all thirteen strings through one MC.BeginFunctionOp("updatePreBriefing") and retains
// none of them. What the SCREEN does keep is the four localized LABELS and the current tip, so
// native reads those directly and this class supplies only the values — re-derived from the two
// places the screen itself read them from.
//
// ⛔ THE LABELS COME FROM THE SCREEN, NOT FROM THIS CLASS. m_strOpLabel / m_strLocationLabel /
// m_strBriefingLabel / m_strObjectiveLabel are read off the live screen object below rather than
// hardcoded or re-declared here. Re-declaring a localized var on our own class would put it in
// OUR loc file, which ships English only — the same failure that had native speaking "Press
// right mouse button" three words from the game's own [ПРАВАЯ КНОПКА МЫШИ] (see
// X2AccessHelper.GetInputVocabulary). The game already owns every one of these words in every
// language it ships.
//
// ⛔ AND THE OBJECTIVE COUNT COMES FROM THE TEMPLATE, NOT FROM THE ARRAY. UpdateBriefingScreen
// asks GetNumObjectives() and passes "" for each slot past it; GetObjectiveText() answers an
// out-of-range line with the literal string "Error: Invalid objective text line requested."
// Reading three lines unconditionally would speak that sentence at a player on every one- or
// two-objective mission.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessDropshipReader extends Object;

// Written by the entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The screen draws at most three objective lines (UpdateBriefingScreen has exactly three
// QueueString slots for them), so this is the game's own ceiling and not a budget of ours.
const MAX_OBJECTIVES = 3;

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// One pre-joined sentence block, in SPEECH order: operation, location, objectives, briefing.
// The screen draws the briefing paragraph ABOVE the objectives; speech puts it after them,
// because the objectives are what the player acts on and speech is serial — the same reasoning
// that moved the save row's timestamp to the end of its line. Native appends the loading line or
// the launch prompt after this, and the tip after that.
//
// Empty when there is no dropship briefing on the stack, which native treats as "say only what
// the screen itself retains" rather than as a failure.
function DescribeBriefing()
{
	local UIScreenStack Stack;
	local UIDropShipBriefing_MissionStart StartScreen;
	local UIDropShipBriefing_MissionEnd EndScreen;

	NativeResult = "";

	Stack = `SCREENSTACK;
	if (Stack == none)
		return;

	StartScreen = UIDropShipBriefing_MissionStart(
		Stack.GetFirstInstanceOf(class'UIDropShipBriefing_MissionStart'));
	if (StartScreen != none)
	{
		NativeResult = DescribeMissionStart(StartScreen);
		return;
	}

	EndScreen = UIDropShipBriefing_MissionEnd(
		Stack.GetFirstInstanceOf(class'UIDropShipBriefing_MissionEnd'));
	if (EndScreen != none)
		NativeResult = DescribeMissionEnd(EndScreen);
}

// ---------------------------------------------------------------------------------------
// The outbound briefing
// ---------------------------------------------------------------------------------------

private function string DescribeMissionStart(UIDropShipBriefing_MissionStart Screen)
{
	local XComGameState_BattleData BattleData;
	local XComGameState_HeadquartersXCom XComHQ;
	local X2MissionTemplate MissionTemplate;
	local GeneratedMissionData GeneratedMission;
	local string Result, ObjectiveText;
	local int i, NumObjectives;

	BattleData = XComGameState_BattleData(
		`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_BattleData', true));
	if (BattleData == none)
		return "";

	// The operation name and the location are the screen's own two BattleData reads. They stand
	// on their own even if the mission template lookup below fails — a briefing that names the
	// operation and the city is still worth speaking.
	AppendLabelled(Result, Screen.m_strOpLabel, BattleData.m_strOpName);
	AppendLabelled(Result, Screen.m_strLocationLabel, BattleData.m_strLocation);

	// AllowNULL, unlike the screen's own call. GetSingleGameStateObjectForClass ASSERTS when the
	// object is missing and AllowNULL is false, and this runs on a poll rather than once from
	// InitScreen — a readout must never be the thing that redscreens a mission launch.
	XComHQ = class'UIUtilities_Strategy'.static.GetXComHQ(true);
	if (XComHQ == none)
		return Result;

	GeneratedMission = XComHQ.GetGeneratedMissionData(BattleData.m_iMissionID);
	MissionTemplate = class'X2MissionTemplateManager'.static.GetMissionTemplateManager()
		.FindMissionTemplate(GeneratedMission.Mission.MissionName);
	if (MissionTemplate == none)
		return Result;

	// The quest-item name is a PARAMETER of the objective text, not decoration: GetObjectiveText
	// expands `XGParam into the line, so passing '' would leave the objective saying "Recover
	// the" with the thing to recover missing.
	NumObjectives = Min(MissionTemplate.GetNumObjectives(), MAX_OBJECTIVES);
	for (i = 0; i < NumObjectives; ++i)
	{
		ObjectiveText = MissionTemplate.GetObjectiveText(i, GeneratedMission.MissionQuestItemTemplate);
		// One label for the list, then the lines under it — repeating "OBJECTIVE:" before each
		// of three lines is how a sighted player reads a heading exactly once.
		AppendLabelled(Result, i == 0 ? Screen.m_strObjectiveLabel : "", ObjectiveText);
	}

	AppendLabelled(Result, Screen.m_strBriefingLabel, MissionTemplate.Briefing);
	return Result;
}

// ---------------------------------------------------------------------------------------
// The flight home
// ---------------------------------------------------------------------------------------

// The post-mission briefing's HEADLINE only — operation name, mission type and outcome. The
// eight battle-statistic rows it also draws (shot percentage, average damage, who dealt the most
// damage, and so on) are not here: UIDropShipBriefing_ListItem retains nothing, so every one of
// them has to be recomputed out of XComGameState_Analytics, and that is a phase of its own.
// What this block covers is the part a player must not miss — whether the mission was won.
private function string DescribeMissionEnd(UIDropShipBriefing_MissionEnd Screen)
{
	local XComGameState_BattleData BattleData;
	local XComGameState_HeadquartersXCom XComHQ;
	local X2MissionTemplate MissionTemplate;
	local GeneratedMissionData GeneratedMission;
	local string Result;

	BattleData = XComGameState_BattleData(
		`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_BattleData', true));
	if (BattleData == none)
		return "";

	AppendPart(Result, BattleData.m_strOpName);

	// AllowNULL, unlike the screen's own call. GetSingleGameStateObjectForClass ASSERTS when the
	// object is missing and AllowNULL is false, and this runs on a poll rather than once from
	// InitScreen — a readout must never be the thing that redscreens a mission launch.
	XComHQ = class'UIUtilities_Strategy'.static.GetXComHQ(true);
	if (XComHQ != none)
	{
		GeneratedMission = XComHQ.GetGeneratedMissionData(BattleData.m_iMissionID);
		MissionTemplate = class'X2MissionTemplateManager'.static.GetMissionTemplateManager()
			.FindMissionTemplate(GeneratedMission.Mission.MissionName);
		if (MissionTemplate != none)
			AppendPart(Result, MissionTemplate.PostMissionType);
	}

	// The screen's own three branches, in its own order — and deliberately WITHOUT the
	// GetColoredText wrapper it runs them through. The colour is the sighted player's cue to the
	// same fact the word already carries, and stripping markup we chose to add would be work for
	// nothing.
	if (BattleData.bLocalPlayerWon && !BattleData.bMissionAborted)
		AppendPart(Result, class'UIMissionSummary'.default.m_strMissionComplete);
	else if (BattleData.bMissionAborted)
		AppendPart(Result, class'UIMissionSummary'.default.m_strMissionAbandoned);
	else
		AppendPart(Result, class'UIMissionSummary'.default.m_strMissionFailed);

	return Result;
}

// ---------------------------------------------------------------------------------------
// Joining
// ---------------------------------------------------------------------------------------

// ⛔ NEVER join two spoken phrases with a bare separator — the kernel's AppendPhrase rule, which
// graph recipes inherit and which a narrate-only surface has to apply itself. It bites hard on
// exactly this screen: EVERY label here ends in a colon ("LOCATION:", "ЗАДАЧА:"), so a blind
// join would voice "МЕСТОПОЛОЖЕНИЕ:, Москва" at the player. A self-punctuated part takes a plain
// space; everything else takes the separator.
private function bool EndsSentence(string Text)
{
	local string Last;

	if (Text == "")
		return false;

	Last = Right(Text, 1);
	return Last == "." || Last == "!" || Last == "?" || Last == ":" || Last == ";";
}

private function AppendPart(out string Result, string Part)
{
	if (Part == "")
		return;
	if (Result != "")
		Result $= EndsSentence(Result) ? " " : ". ";
	Result $= Part;
}

// A label is dropped when its value is empty — an "OBJECTIVE:" with nothing after it is worse
// than silence, because it sounds like a readout that failed rather than a screen with nothing
// there. An empty LABEL with a value is fine and expected: that is the second and third
// objective line.
private function AppendLabelled(out string Result, string Label, string Value)
{
	if (Value == "")
		return;
	AppendPart(Result, Label);
	AppendPart(Result, Value);
}
