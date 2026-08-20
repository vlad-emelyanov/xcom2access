// X2AccessTutorial
// The tutorial's one UNREADABLE text surface, recovered from the state that produced it.
//
// Three of the tutorial's four text surfaces are readable from native: the modal popup keeps its
// text in real script properties, the anchored blade is retained in UIWorldMessageMgr.m_arrMsgs,
// and the 3D markers are data on XComTutorialMgr. The fourth — the blade pinned to the bottom
// right of the HUD — is not: CreateHUDBladeMessage spawns a bare UIPanel and pushes the words
// straight into Flash with MC.FunctionString("SetText", ...), retaining nothing. C++ can see that
// the panel exists and no more.
//
// But the words were computed from a game state, and that state is still in the history:
// X2Action_ShowTutorialPopup resolves them as NarrativeTemplate.GetObjectiveText(
// Context.ObjectiveTextIndex) off an XComGameStateContext_TutorialBox. So this walks back to that
// context and resolves the text exactly the way the action did — which also means every future
// change to the substitution (quest item names, the player's own key bindings) comes along for
// free.
//
// ⛔ THE TUTORIAL IS A REPLAY, so the WHOLE script — every popup to the end of the mission — is
// already in the history at mission start. The walk starts at XComTutorialMgr.CurrentHistoryFrame
// and never looks past it. Reading the raw history end would narrate the future.
//
// Same contract as X2AccessTileReader and X2AccessTacticalReader: C++ owns the keys and the
// speech, calls these on this class's default object via ProcessEvent (game thread, from the
// frame pump), and reads NativeResult back through reflection right after the call returns.
// Nothing here runs on its own — no listeners, no timers.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessTutorial extends Object;

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// Change detection for PollBlade. Both value types (CDO rule). LastBladeText is the comparison
// key rather than the message id, because an id can be reused with new text and because an
// unnamed blade has no id at all.
var string LastBladeText;

// How far back the walk looks. The blade that is up was raised within a handful of frames of the
// current one; this bound exists so a long mission cannot turn a twice-a-second poll into a walk
// over thousands of states. If a blade is ever found to survive longer than this, raise it —
// silence here is a missing line, never a wrong one.
const MAX_HISTORY_LOOKBACK = 600;

// ---------------------------------------------------------------------------
// Entry points (called from native)
// ---------------------------------------------------------------------------

// The poll, called about twice a second while a HUD blade is up. Silent unless the text CHANGED,
// which is the only thing native cannot detect for itself: CreateHUDBladeMessage reuses the same
// panel and just calls SetText again, so a replaced blade looks identical from outside.
//
// Deliberately silent — not "no tutorial message" — when it cannot resolve one. This runs on a
// timer, and a poll that talks about its own failures is unusable. AnnounceBlade is the one that
// always answers.
function PollBlade()
{
	local string Text;

	NativeResult = "";

	Text = FindCurrentBladeText();
	if (Text == "" || Text == LastBladeText)
		return;

	LastBladeText = Text;
	NativeResult = Text;
}

// The on-demand read, behind the re-read key. Always answers with whatever blade is up, whether
// or not it has been spoken before — the player pressed a key precisely because they want it
// again. An empty result means there is genuinely nothing up; native says so.
function AnnounceBlade()
{
	NativeResult = FindCurrentBladeText();
	if (NativeResult != "")
		LastBladeText = NativeResult;
}

// Names the unit a world flyover is pinned to, so native can prefix it onto the message.
//
// WHY THIS EXISTS: world messages are queued PER UNIT — X2Action_ConcealmentLost queues one at
// each Unit.GetLocation() — so a squad going concealed publishes the same word once per soldier,
// and a bare "Unconscious" says nothing about WHO. A sighted player never had that problem: the
// flyover floats over the soldier it belongs to, and the anchor IS the referent. Naming it is
// what puts that back, and it is also why de-duplicating identical flyover text would be wrong.
//
// Answers "" for anything that is not a unit (the anchor is a plain StateObjectReference and may
// point at an interactive object) and for a unit the squad cannot see. An empty answer is not an
// error: native then speaks the message unprefixed, exactly as before.
function DescribeMessageAnchor(int ObjectID)
{
	local XComGameState_Unit Unit;
	local XComTutorialMgr Tutorial;
	local int Frame;

	NativeResult = "";
	if (ObjectID <= 0)
		return;

	// In the tutorial the history holds the WHOLE recording, so an ungated lookup would answer
	// with the object as it will be, not as it is. -1 means "latest", which is right everywhere
	// except a replay.
	Frame = -1;
	Tutorial = `TUTORIAL;
	if (Tutorial != none)
		Frame = Tutorial.CurrentHistoryFrame;

	Unit = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(ObjectID, eReturnType_Reference, Frame));
	if (Unit == none)
		return;

	// Same fog rule and same name the combat narration uses, so a flyover line and an event line
	// agree about who they are talking about. (Note the visibility helper reads CURRENT
	// visibility even under a replay; the message itself is already team-filtered by the game
	// before it is displayed, so this is defence in depth rather than the only guard.)
	if (!class'X2AccessTacticalEvents'.static.IsNarratable(Unit))
		return;

	NativeResult = class'X2AccessTacticalEvents'.static.UnitName(Unit);
}

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------

// The text of the UNANCHORED (HUD) tutorial blade currently showing, or "".
//
// Walking BACKWARDS is what makes this correct with one pass and no bookkeeping: the first thing
// seen for a message id is the newest event for it, so a blade that has since been removed is
// recognised before its own creation frame is reached. Anchored blades are skipped — native reads
// those straight out of m_arrMsgs, and speaking them here as well would double every one.
private function string FindCurrentBladeText()
{
	local XComTutorialMgr Tutorial;
	local XComGameStateHistory History;
	local XComGameState HistoryState;
	local XComGameStateContext_TutorialBox Context;
	local array<string> RemovedIDs;
	local int Index, StopIndex, LastFrame;

	Tutorial = `TUTORIAL;
	if (Tutorial == none)
		return ""; // not the tutorial mission; nothing to resolve

	History = `XCOMHISTORY;
	if (History == none)
		return "";

	// The replay's playback position, NOT the end of the history — see the class header.
	LastFrame = Tutorial.CurrentHistoryFrame;
	if (LastFrame >= History.GetNumGameStates())
		LastFrame = History.GetNumGameStates() - 1;

	StopIndex = LastFrame - MAX_HISTORY_LOOKBACK;
	if (StopIndex < 0)
		StopIndex = 0;

	for (Index = LastFrame; Index >= StopIndex; Index--)
	{
		// Named HistoryState, not State: State is a reserved word in UnrealScript.
		HistoryState = History.GetGameStateFromHistory(Index, eReturnType_Reference);
		if (HistoryState == none)
			continue;

		Context = XComGameStateContext_TutorialBox(HistoryState.GetContext());
		if (Context == none)
			continue;

		// A removal carries the id and NOTHING else — RemoveBladeTutorialBoxFromHistory sets only
		// MessageID and Remove, so BoxType reads as its default (Modal) and cannot be filtered on.
		// Remember the id instead and let it veto the creation frame further back.
		if (Context.Remove)
		{
			if (RemovedIDs.Find(Context.MessageID) == INDEX_NONE)
				RemovedIDs.AddItem(Context.MessageID);
			continue;
		}

		if (Context.BoxType != TutorialBoxType_Blade)
			continue; // a modal; native reads those off the screen itself

		// BladeMessage's own test for which blade this is: no anchor object AND no anchor
		// location means the unanchored bottom-right HUD blade. Anything else is a world-anchored
		// blade, which is already in m_arrMsgs where native can read it.
		if (Context.ObjectToAnchorTo.ObjectID != 0 || Context.AnchorLocation != vect(0, 0, 0))
			continue;

		if (RemovedIDs.Find(Context.MessageID) != INDEX_NONE)
			continue; // this one has been taken down since; keep looking further back

		return ResolveBoxText(Context);
	}

	return "";
}

// Mirrors X2Action_ShowTutorialPopup.GetTutorialBoxText exactly, minus the title (the HUD blade
// shows body text only). Explicit text wins where a Kismet node supplied it; otherwise the index
// is resolved against the mission's own narrative template, which is also what performs the
// %QUESTITEM% and %KEY:...% substitutions — so the key names spoken are the player's real
// bindings, not the defaults.
private function string ResolveBoxText(XComGameStateContext_TutorialBox Context)
{
	local XComTacticalMissionManager MissionManager;
	local X2MissionNarrativeTemplateManager TemplateManager;
	local X2MissionNarrativeTemplate NarrativeTemplate;

	if (Context.ExplicitText != "")
		return Context.ExplicitText;

	MissionManager = `TACTICALMISSIONMGR;
	if (MissionManager == none)
		return "";

	TemplateManager = class'X2MissionNarrativeTemplateManager'.static.GetMissionNarrativeTemplateManager();
	if (TemplateManager == none)
		return "";

	NarrativeTemplate = TemplateManager.FindMissionNarrativeTemplate(
		MissionManager.ActiveMission.sType, MissionManager.MissionQuestItemTemplate);
	if (NarrativeTemplate == none)
		return "";

	// GetObjectiveText answers an out-of-range line with an error STRING rather than failing, and
	// speaking "Error: Invalid text line requested." would be worse than saying nothing at all.
	if (Context.ObjectiveTextIndex < 0)
		return "";

	return NarrativeTemplate.GetObjectiveText(Context.ObjectiveTextIndex);
}
