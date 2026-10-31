// X2AccessReplayNarration
// Combat narration for the TUTORIAL — and for any other replay — where the event bus is dead.
//
// ⛔ WHY THIS EXISTS. A replay does not build game states, it plays back states that are already
// in the history. `X2GameRuleset.SubmitGameStateContext` routes every context to
// `XComTutorialMgr.HandleSubmittedGameStateContext` instead of `SubmitGameStateContext_Internal`
// while `REPLAY.bInTutorial; on a matching action that handler calls ResumeReplay() /
// StepReplayForward() and returns true, DISCARDING the player's context. StepReplayForward then
// only advances CurrentHistoryFrame and calls BuildVisualizationFrame on a frame that was in the
// history before the mission started. So ContextBuildGameState never runs, X2EventManager
// .TriggerEvent is never reached, and NOT ONE listener fires — ours or the game's own. Verified
// live 2026-08-20: a full tutorial run produced zero lines from X2AccessTacticalEvents with the
// listener provably registered.
//
// So in a replay the history IS the event stream, and this walks it. Every line comes from the
// same builders X2AccessTacticalEvents uses, so a shot reads identically whether it was narrated
// from an event or from a frame — there is exactly one place that decides wording.
//
// THE GATE IS THE POINT: this runs precisely when `REPLAY.bInReplay is true, which is precisely
// when the listeners cannot fire. The two paths can never both narrate the same action.
//
// SIGHTED PARITY: never read past what the player has been SHOWN. The whole tutorial script —
// every enemy move, every hit, the ending — is in the history at mission start, so the raw history
// end is the FUTURE. The cursor here is XComGameStateVisualizationMgr.LastStateHistoryVisualized,
// the same "which frame is on screen" the game's own unit flags use, clamped again to the replay's
// CurrentHistoryFrame. That also syncs narration to the animation, which is what
// ELD_OnVisualizationBlockStarted buys the live listeners.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessReplayNarration extends Object;

// The last history frame we have spoken. Written through `self` (native calls PollReplay on the
// CDO), which is why this needs no `config` marker — only STATIC writes through `default.` do.
var int LastNarratedFrame;

// ⛔ A RUNAWAY GUARD, NOT A PACING ONE. It was 24 for one run and that was badly wrong: normal
// tutorial playback advances the visualized cursor in steps of 25-50 frames, because most history
// frames carry no visualization of their own and LastStateHistoryVisualized jumps to the end of a
// block when that block completes. Measured live 2026-08-20: 17 "seeks" in one tutorial run,
// 597 frames dropped — among them an overwatch shot the player watched happen and never heard.
// Ordinary play must always be narrated in full; the only real seek is StartReplay's own
// JumpReplayToFrame, and the first-poll branch below already covers that. So this is now sized to
// catch nothing but a genuine runaway, and it still says so in the log when it fires.
const MAX_CATCHUP_FRAMES = 512;

// ---------------------------------------------------------------------------
// Native entry point — one call per drain
// ---------------------------------------------------------------------------

function PollReplay()
{
	local XComGameReplicationInfo GRI;
	local XComReplayMgr Replay;
	local XComGameStateVisualizationMgr VisMgr;
	local XComGameStateHistory History;
	local int Target, Frame;

	// This function is the companion's 100 ms heartbeat on every world, so the HUD polls ride it
	// (orientation line, reinforcement countdown, objective ticks). They are not replay
	// narration; they are here because native already calls this on the cadence they need, and
	// they read the screen, which a replay drives exactly as a live mission.
	class'X2AccessMissionPoll'.static.Poll();

	// Deliberately NOT the `REPLAY / `XCOMVISUALIZATIONMGR macros: both dereference the GRI
	// unconditionally, and native polls this ten times a second everywhere — including the shell,
	// where there is no tactical GRI at all. That is two "Accessed None" script warnings per poll
	// burying the log we actually read. One cast, one null check, both managers.
	GRI = XComGameReplicationInfo(class'WorldInfo'.static.GetWorldInfo().GRI);
	Replay = GRI != none ? GRI.ReplayMgr : none;

	if (Replay == none || !Replay.bInReplay)
	{
		// Not a replay: the listeners own narration. Forget our place so the next replay starts
		// clean rather than trying to catch up across a mission boundary.
		LastNarratedFrame = -1;
		return;
	}

	VisMgr = GRI.VisualizationMgr;
	if (VisMgr == none)
		return;

	Target = VisMgr.LastStateHistoryVisualized;
	if (Target <= 0)
		return;

	// Never past the replay's own cursor. LastStateHistoryVisualized normally lags it, but a
	// clamp here is what guarantees we can only ever describe frames the player has reached.
	if (Target > Replay.CurrentHistoryFrame)
		Target = Replay.CurrentHistoryFrame;

	// Nothing new, or the cursor moved backwards (the replay UI can step back; the tutorial does
	// not, but adopting a lower Target would re-narrate everything between here and there).
	if (Target <= LastNarratedFrame)
		return;

	if (LastNarratedFrame < 0 || Target - LastNarratedFrame > MAX_CATCHUP_FRAMES)
	{
		// Never silently: a skip is coverage we chose not to give, and it must be visible in the
		// log rather than look like a stretch where nothing happened. The first adopt of a mission
		// is not a skip — there is nothing behind us to have missed.
		if (LastNarratedFrame >= 0)
		{
			`log("X2Access: replay narration skipped frames" @ string(LastNarratedFrame + 1) $ "-"
				$ string(Target) @ "(seek, not play)", , 'X2Access');
		}

		LastNarratedFrame = Target;
		return;
	}

	History = `XCOMHISTORY;

	for (Frame = LastNarratedFrame + 1; Frame <= Target; Frame++)
	{
		NarrateFrame(History, Frame);
	}

	LastNarratedFrame = Target;
}

// ---------------------------------------------------------------------------
// One history frame
// ---------------------------------------------------------------------------

private function NarrateFrame(XComGameStateHistory History, int Frame)
{
	local XComGameState GameState;
	local XComGameStateContext Context;

	GameState = History.GetGameStateFromHistory(Frame);
	if (GameState == none)
		return;

	Context = GameState.GetContext();
	if (Context == none)
		return;

	NarrateTurnChange(History, Context, Frame);
	NarrateReinforcements(GameState, Context);
	NarrateReveal(Context, Frame);
	NarrateAbility(History, Context, Frame);
	NarrateMovement(Context, Frame);

	// Order matters: the shot, then what it did, then who it killed. A frame carries all three at
	// once, so reading the units after the ability is what keeps "X shoots Y" ahead of "Y takes 4
	// damage" ahead of "Y killed". Walking the frame ourselves gets that for free — the LIVE path
	// had to be split across the two visualization windows to reach the same order, because the
	// game triggers damage and death BEFORE the ability that caused them (see
	// X2AccessTacticalEvents.CreateTacticalNarrationListener).
	NarrateUnits(History, GameState, Context, Frame);
	NarrateConcealment(History, GameState, Frame);
}

private function NarrateTurnChange(XComGameStateHistory History, XComGameStateContext Context, int Frame)
{
	local XComGameStateContext_TacticalGameRule RuleContext;
	local XComGameState_Player PlayerState;
	local string Word;

	RuleContext = XComGameStateContext_TacticalGameRule(Context);
	if (RuleContext == none || RuleContext.GameRuleType != eGameRule_PlayerTurnBegin)
		return;

	PlayerState = XComGameState_Player(History.GetGameStateForObjectID(RuleContext.PlayerRef.ObjectID, , Frame));
	if (PlayerState == none)
		return;

	// The spawn memo lives one turn on this path too (see X2AccessTacticalEvents.ResetSpawnNarration).
	class'X2AccessTacticalEvents'.static.ResetSpawnNarration();

	// The same two decisions the live listeners make, through the same functions: the XCom turn is
	// the aid line (the game draws no banner for it) and re-arms the banner memo; any other team
	// is a banner only when its kind differs from the last one drawn. The tutorial has no group
	// events to mirror, so the player's own turn-begin frame stands in for the first group.
	if (PlayerState.GetTeam() == eTeam_XCom)
	{
		class'X2AccessTacticalEvents'.static.ResetTurnBanner();
		class'X2AccessNarrator'.static.Alert(class'UISpecialMissionHUD_TurnCounter'.default.m_strPlayerTurnLabel);
		return;
	}

	Word = class'X2AccessTacticalEvents'.static.ClaimTurnBanner(
		class'X2AccessTacticalEvents'.static.TeamBannerKind(PlayerState.GetTeam()));
	if (Word != "")
		class'X2AccessNarrator'.static.Alert(Word);
}

// The reinforcement drop: a change container whose ChangeInfo is the spawner's own literal for the
// state it spawns the units in (XComGameState_AIReinforcementSpawner.SpawnReinforcements, :247),
// the same test X2Action_RevealAIBegin.ShouldPlayRevealMatinee uses to recognise the spawn. The
// frame's game state IS the spawn state, so the shared builder reads it exactly as the live
// listener reads the event's.
private function NarrateReinforcements(XComGameState GameState, XComGameStateContext Context)
{
	local XComGameStateContext_ChangeContainer Container;
	local string Line;

	Container = XComGameStateContext_ChangeContainer(Context);
	if (Container == none)
		return;
	if (Container.ChangeInfo != class'XComGameState_AIReinforcementSpawner'.default.SpawnReinforcementsChangeDesc)
		return;

	Line = class'X2AccessTacticalEvents'.static.BuildReinforcementLine(GameState);
	if (Line != "")
		class'X2AccessNarrator'.static.Combat(Line);
}

// The pod reveal: the frame whose context is the reveal context itself. The builder rejects the
// End context on its own, so every reveal frame is handed over.
private function NarrateReveal(XComGameStateContext Context, int Frame)
{
	local string Line;

	Line = class'X2AccessTacticalEvents'.static.BuildRevealLine(XComGameStateContext_RevealAI(Context), Frame);
	if (Line != "")
		class'X2AccessNarrator'.static.Combat(Line);
}

private function NarrateAbility(XComGameStateHistory History, XComGameStateContext Context, int Frame)
{
	local XComGameStateContext_Ability AbilityContext;
	local XComGameState_Ability AbilityState;
	local string Line;

	AbilityContext = XComGameStateContext_Ability(Context);
	if (AbilityContext == none)
		return;

	AbilityState = XComGameState_Ability(History.GetGameStateForObjectID(AbilityContext.InputContext.AbilityRef.ObjectID, , Frame));

	Line = class'X2AccessTacticalEvents'.static.BuildAbilityLine(AbilityState, AbilityContext);
	if (Line != "")
		class'X2AccessNarrator'.static.Combat(Line);
}

// A finished move, the replay's UnitMoveFinished: the move ability context that is not an
// interrupt step and carries a path. The live listener fires once per final build state, and this
// is the frame that build produced. The sighting lines judge the frame the player has been shown.
private function NarrateMovement(XComGameStateContext Context, int Frame)
{
	local XComGameStateContext_Ability AbilityContext;

	AbilityContext = XComGameStateContext_Ability(Context);
	if (AbilityContext == none || AbilityContext.InterruptionStatus == eInterruptionStatus_Interrupt)
		return;
	if (AbilityContext.InputContext.MovementPaths.Length == 0)
		return;

	class'X2AccessTacticalEvents'.static.NarrateSightings(
		AbilityContext.InputContext.SourceObject.ObjectID, Frame, true, false);
}

// Damage and deaths, read off the unit versions the frame itself carries — a delta frame holds
// exactly the objects that changed, which is the same set the live listeners would have been
// handed one at a time.
private function NarrateUnits(XComGameStateHistory History, XComGameState GameState, XComGameStateContext Context, int Frame)
{
	local XComGameState_Unit Unit, PrevUnit;
	local string Line;

	foreach GameState.IterateByClassType(class'XComGameState_Unit', Unit)
	{
		Line = class'X2AccessTacticalEvents'.static.BuildDamageLine(Unit, Context);
		if (Line != "")
			class'X2AccessNarrator'.static.Combat(Line);

		if (Unit.IsAlive())
			continue;

		// A dead unit stays dead in every later frame, so "not alive" alone would re-announce the
		// same corpse for the rest of the mission. The transition is the event.
		PrevUnit = XComGameState_Unit(History.GetGameStateForObjectID(Unit.ObjectID, , Frame - 1));
		if (PrevUnit == none || !PrevUnit.IsAlive())
			continue;

		class'X2AccessTacticalEvents'.static.NarrateDeath(Unit, Context, GameState, false);
	}
}

private function NarrateConcealment(XComGameStateHistory History, XComGameState GameState, int Frame)
{
	local XComGameState_Player PlayerState, PrevPlayer;

	foreach GameState.IterateByClassType(class'XComGameState_Player', PlayerState)
	{
		if (PlayerState.GetTeam() != eTeam_XCom)
			continue;

		PrevPlayer = XComGameState_Player(History.GetGameStateForObjectID(PlayerState.ObjectID, , Frame - 1));
		if (PrevPlayer == none || PrevPlayer.bSquadIsConcealed == PlayerState.bSquadIsConcealed)
			continue;

		// The squad flag flipped in this frame: broken, or entered (the HUD banner's word, the
		// same one the live listener speaks).
		if (PlayerState.bSquadIsConcealed)
			class'X2AccessNarrator'.static.Alert(class'X2AccessTacticalEvents'.static.SquadConcealedWord());
		else
			class'X2AccessNarrator'.static.Alert(class'X2AccessVocab'.default.SquadConcealmentBroken);
	}
}

defaultproperties
{
	LastNarratedFrame = -1
}
