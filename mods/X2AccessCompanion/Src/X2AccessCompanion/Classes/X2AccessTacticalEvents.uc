// X2AccessTacticalEvents
// Combat narration: the spoken replacement for the flyovers, notices and banners a blind player
// cannot read.
//
// WHY THIS IS SCRIPT AND NOT C++ (verified live 2026-08-19, "T1"): the ASI's ProcessEvent hook
// sees only NATIVE->script dispatch, and UIWorldMessageMgr.Message / UIEventNotices.Notify /
// UITacticalHUDMessageBanner.SetBanner are all reached script->script, through CallFunction, which
// never touches the vtable slot we patch. A full HUD refresh mid-mission produced not one UI
// script function in the hook. So the cheap tap does not exist and the game's own event bus is the
// path: these listeners resolve out of XCOMHISTORY, which is script-natural work anyway.
//
// DEFERRAL is a deliberate per-event choice, not a default:
//   ELD_OnVisualizationBlockStarted for the ACTION the player watches begin (the ability line) —
//   an event listener otherwise fires at state-submission time, which can be several seconds ahead
//   of the animation, and narration that runs ahead of the picture reads as a bug.
//   ELD_OnVisualizationBlockCompleted for that action's OUTCOME (damage, death), which is when the
//   flyover and the ragdoll appear. Splitting the two windows is also the only thing that gets
//   cause spoken before consequence — see the block at AddCHEvent below for why.
//   ELD_OnStateSubmitted for structural facts (turn handover, concealment, evac). These are not
//   tied to one animation, and a state that never builds a visualization block would drop either
//   visualization-window listener on the floor entirely.
//
// PARITY: every line here corresponds to something a sighted player is shown. Events about units
// XCOM cannot see are dropped (IsNarratable) so narration never leaks position or composition —
// with one deliberate exception, documented at OnUnitDied: a kill YOU caused is always announced,
// because the game shows it too.
//
// All output goes through X2AccessNarrator, which owns channel and queueing.

// `config(Game)` is not a preference, it is the only route to per-class storage: a static function
// has nowhere else to remember anything, and the compiler rejects `default.X = ...` outright unless
// X is a config property ("Can't change default values of non-config properties") — the same reason
// X2AccessNarrator carries the specifier. No XComGame.ini section is shipped for this class, so the
// values below start at their declared defaults every launch and nothing is ever written back
// (SaveConfig is never called).
//
// GC rule: this class's default object holds VALUE-TYPED state only. The damage memo is an int and
// a string of ints precisely so it never has to hold a unit or a game state.
class X2AccessTacticalEvents extends X2EventListener config(Game);

// The damage memo. NarratedDamageFrame is the HistoryIndex the claimed set belongs to — unique per
// game state and assigned long before visualization runs — and NarratedDamageUnits holds the
// ObjectIDs already spoken for it, each wrapped in pipes so a search for |3| cannot match |37|.
// Read only through ClaimDamageNarration, which is where the reasoning lives.
//
// Both are `config` for the reason CLAUDE.md records — `default.X = ...` does not compile
// otherwise — and that ALSO costs them a defaultproperties initialiser: the compiler refuses one
// on a config property ("Import failed for 'NarratedDamageFrame': property is config") and
// leaves the value at zero, silently. So NarratedDamageFrame starts at 0, not at a sentinel, and
// the first damage of a session may well share its frame number. That is harmless, but only
// because of the OTHER var: the claimed set starts EMPTY, so the first line is claimed and spoken
// whether or not the frame comparison happened to reset it. The empty set is the safety here;
// the frame number never was.
var config int NarratedDamageFrame;
var config string NarratedDamageUnits;

// The spawn memo: ObjectIDs (pipe-wrapped like the damage set) of units the REINFORCEMENT line has
// already named, so the reveal that follows the drop does not name them a second time. Cleared on
// every turn handover — the drop and its reveal are one event chain inside one turn, and a memo
// that outlived the turn would silence a Chosen's real reveal turns later. Read only through
// ClaimSpawnNarration / WasSpawnNarrated, where the reasoning lives.
var config string NarratedSpawnUnits;

// The sightings memo: "|id@x,y,z|" per enemy the squad could see at the last evaluation, at the
// tile it stood on. Shared pipes, so the whole memo is "|a|b|c|". It is the identity the sighting
// lines claim on: a unit absent from it has just come into view, a unit present at another tile
// has moved, a unit present that can no longer be seen has left. Cleared when a mission session
// ends (ResetSightings); the orientation line baselines it, so a load recites nothing.
var config string SightingMemo;

// "|id|" per unit a pod reveal named THIS TURN. The reveal line and the scamper that follows it
// own those units; the sighting lines stand down for them. Cleared on every turn handover
// (ResetSpawnNarration), like the spawn memo, for the same err-on-the-short-side reason.
var config string NarratedRevealUnits;

// The turn-banner memo: which banner the game last drew, as one of the BANNER_ kinds. The game
// draws a banner only when the kind CHANGES (XComPresentationLayer.UIEndTurn compares against
// m_lastTurnOverlay, which starts at "local"), so this is the same one-int memo and 0 is the
// game's own starting value — which is why a config var with no initialiser is right here. Cleared
// when a mission session ends (X2AccessMissionPoll.EndSession) and set to local on every XCom turn.
var config int LastTurnBanner;

const BANNER_LOCAL = 0;       // the XCom turn: the game draws NO banner for it
const BANNER_ALIEN = 1;
const BANNER_LOST = 2;
const BANNER_CHOSEN = 3;
const BANNER_RESISTANCE = 4;

// How many history frames past a move to search for the pod reveal it triggered.
const REVEAL_LOOKAHEAD = 64;

static function array<X2DataTemplate> CreateTemplates()
{
	local array<X2DataTemplate> Templates;

	Templates.AddItem(CreateTacticalNarrationListener());

	return Templates;
}

static function CHEventListenerTemplate CreateTacticalNarrationListener()
{
	local CHEventListenerTemplate Template;

	`CREATE_X2TEMPLATE(class'CHEventListenerTemplate', Template, 'X2Access_TacticalNarration');
	Template.RegisterInTactical = true;
	Template.RegisterInStrategy = false;

	// Watched-and-visualized, and SPLIT ACROSS THE TWO VISUALIZATION WINDOWS — which is what puts
	// cause before consequence.
	//
	// ⛔ Priority orders listeners for the SAME event only. Between different events the order is
	// the order the GAME triggers them, and that order is the reverse of the one a player needs:
	// XComGameState_Unit triggers 'UnitTakeEffectDamage' (:5874) and 'UnitDied' (:6022) while the
	// ability's effects are being applied, and XComGameStateContext_Ability only reaches
	// 'AbilityActivated' afterwards, under its own "Trigger post-activation events" comment (:191).
	// Registering all three on one window therefore spoke the outcome before the attack, verified
	// live 2026-08-21 on Gatecrasher — "ADVENT Soldier killed." then "Jane Kelly: Slash at ADVENT
	// Soldier, hit.", and worse for damage: you learned you had been shot before you learned by
	// whom. All three landed in the same 100 ms drain, so the narrator could not reorder what it
	// could not tell apart.
	//
	// The fix is the engine's own two-phase model rather than a queue of ours: the ability line is
	// the action, so it belongs at block START (where a sighted player sees the shooter raise the
	// weapon); damage and death are its OUTCOME, so they belong at block COMPLETE (where the
	// flyover and the ragdoll are). Blocks run sequentially, so this also keeps two abilities
	// resolving in one burst from interleaving. It is what the game's own HUD does — UIUnitFlagManager
	// (:55) and UITacticalHUD_Enemies (:68) both take 'AbilityActivated' and 'UnitDied' on Completed.
	Template.AddCHEvent('AbilityActivated', OnAbilityActivated, ELD_OnVisualizationBlockStarted, 60);
	Template.AddCHEvent('UnitTakeEffectDamage', OnUnitTakeEffectDamage, ELD_OnVisualizationBlockCompleted, 55);
	Template.AddCHEvent('UnitDied', OnUnitDied, ELD_OnVisualizationBlockCompleted, 50);

	// Sightings. Both cutscenes are visualization actions on a state that carries exactly what the
	// camera shows, and both are watched at block START so the line lands as the camera pans, not at
	// submission seconds earlier. 'ScamperBegin' is fired by XComGameStateContext_RevealAI
	// .ContextBuildGameState (:121) with the reveal state, whose block is X2Action_RevealAIBegin —
	// the pod reveal. 'SpawnReinforcementsComplete' is fired by XComGameState_AIReinforcementSpawner
	// .SpawnReinforcements (:268) with the spawn state, whose block is the transport / psi gate /
	// drop-in matinee (BuildVisualizationForUnitSpawning). The game's own enemy strip takes
	// 'ScamperBegin' on Completed (UITacticalHUD_Enemies:67), which is why the heads appear only
	// after the cutscene; the words belong during it.
	Template.AddCHEvent('ScamperBegin', OnScamperBegin, ELD_OnVisualizationBlockStarted, 60);
	Template.AddCHEvent('SpawnReinforcementsComplete', OnSpawnReinforcementsComplete, ELD_OnVisualizationBlockStarted, 60);

	// Enemies coming into view, moving in view and leaving view. 'UnitMoveFinished' is fired by
	// X2Ability_DefaultAbilitySet.MoveAbility_BuildGameState (:286) only for a move's FINAL build
	// state, never for an interrupt step, and by the special movers (Archon, Cyberus, teleports)
	// by hand. Watched at block COMPLETED, the one window for this: the walk IS the block, so
	// completion is the instant the unit has arrived and a sighted player has seen where it stands,
	// and a line at block START would name a position the picture has not reached. It is also
	// after the move's own reveal chain has been submitted, which is what lets the line check
	// whether a pod reveal is about to claim the unit (BuildSightingLine).
	Template.AddCHEvent('UnitMoveFinished', OnUnitMoveFinished, ELD_OnVisualizationBlockCompleted, 40);

	// Structural.
	Template.AddCHEvent('PlayerTurnBegun', OnPlayerTurnBegun, ELD_OnStateSubmitted, 50);
	// The turn BANNER is a per-group decision (XGPlayer.OnBeginTurnVisualized), so it rides the
	// group event, not the player's. Fired from X2TacticalGameRuleset.SetupUnitActionsForGroupTurnBegin
	// only for a group whose summoning sickness is cleared, which is half of the banner's own test.
	Template.AddCHEvent('UnitGroupTurnBegun', OnUnitGroupTurnBegun, ELD_OnStateSubmitted, 50);
	Template.AddCHEvent('SquadConcealmentBroken', OnSquadConcealmentBroken, ELD_OnStateSubmitted, 50);
	Template.AddCHEvent('UnitConcealmentBroken', OnUnitConcealmentBroken, ELD_OnStateSubmitted, 45);
	// The ENTERED pair. A squad entering fires the squad event once AND the unit event once per
	// soldier (XComGameState_Player.SetSquadConcealmentNewGameState calls SetIndividualConcealment on
	// each), so the unit handler stands down inside a squad change, the way the game's own
	// BuildVisualizationForConcealmentChanged gives the squad ONE instigator flyover.
	Template.AddCHEvent('SquadConcealmentEntered', OnSquadConcealmentEntered, ELD_OnStateSubmitted, 50);
	Template.AddCHEvent('UnitConcealmentEntered', OnUnitConcealmentEntered, ELD_OnStateSubmitted, 45);
	Template.AddCHEvent('UnitPanicked', OnUnitPanicked, ELD_OnStateSubmitted, 45);
	Template.AddCHEvent('ShieldsExpended', OnShieldsExpended, ELD_OnStateSubmitted, 40);
	Template.AddCHEvent('UnitEvacuated', OnUnitEvacuated, ELD_OnStateSubmitted, 40);

	// Registration is the one failure mode with no symptom other than silence: an X2EventListener
	// that the template manager never discovers looks exactly like a mission where nothing
	// happened. This line in Launch.log is the difference between the two.
	`log("X2Access: tactical narration listener created", , 'X2Access');

	return Template;
}

// ---------------------------------------------------------------------------
// Turn handover
// ---------------------------------------------------------------------------

static function EventListenerReturn OnPlayerTurnBegun(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	local XComGameState_Player PlayerState;

	PlayerState = XComGameState_Player(EventData);
	if (PlayerState == none)
		return ELR_NoInterrupt;

	// Every handover, every team — the memo's lifetime is "this turn", not "this banner".
	ResetSpawnNarration();

	// Only the XCom turn is spoken here. The game draws NO banner for it
	// (XComPresentationLayer.UIEndTurn, eTurnOverlay_Local: "do not show any banner for the start
	// of the XCom turn"), so this line is an AID, not parity: the HUD coming back is what a sighted
	// player sees, and the turn counter's "Your Turn" is the nearest word the game owns. The enemy
	// side is not spoken here at all, because its banners are decided per initiative GROUP, not per
	// player — OnUnitGroupTurnBegun mirrors that.
	if (PlayerState.GetTeam() == eTeam_XCom)
	{
		// The game's memo returns to "local" with this turn, which re-arms the next alien banner.
		default.LastTurnBanner = BANNER_LOCAL;
		class'X2AccessNarrator'.static.Alert(class'UISpecialMissionHUD_TurnCounter'.default.m_strPlayerTurnLabel);
	}

	return ELR_NoInterrupt;
}

// The enemy turn banners. The game DOES draw one for the Lost, the Chosen and the Resistance as
// well as the aliens — XGPlayer.OnBeginTurnVisualized (:866-889) picks eTurnOverlay_Alien, _Chosen,
// _TheLost or _Resistance and calls UIEndTurn, which shows it. (This file used to say the game
// draws nothing for the Lost. It does.)
//
// WHEN it fires, mirrored: OnBeginTurnVisualized runs for each initiative group's turn begin and
// acts only on "the first initiative group with valid members" — bSummoningSicknessCleared and
// GetLivingMembers — and UIEndTurn then draws only when the KIND differs from the last one drawn,
// which ClaimTurnBanner holds. The group event is submitted only for a group with its sickness
// cleared; the living-members test is repeated here. An engaged Chosen as the group's lead is the
// game's own special case and gets the Chosen banner.
static function EventListenerReturn OnUnitGroupTurnBegun(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	local string Word;

	Word = ClaimTurnBanner(GroupBannerKind(XComGameState_AIGroup(EventData), false));
	if (Word != "")
		class'X2AccessNarrator'.static.Alert(Word);

	return ELR_NoInterrupt;
}

// Which banner this group's turn would draw, or -1 when the game decides nothing for it. The
// replay narrator passes bReplay because XGPlayer skips the validity test inside the tutorial.
static function int GroupBannerKind(XComGameState_AIGroup GroupState, bool bReplay)
{
	local array<int> LivingIDs;
	local XComGameState_Unit Lead;

	if (GroupState == none)
		return -1;
	if (!bReplay && (!GroupState.bSummoningSicknessCleared || !GroupState.GetLivingMembers(LivingIDs)))
		return -1;

	switch (GroupState.TeamName)
	{
	case eTeam_Alien:
		if (GroupState.m_arrMembers.Length > 0)
			Lead = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(GroupState.m_arrMembers[0].ObjectID));
		if (Lead != none && Lead.IsChosen() && Lead.IsAlive() && !Lead.IsIncapacitated() && !Lead.bRemovedFromPlay)
			return BANNER_CHOSEN;
		return BANNER_ALIEN;
	case eTeam_TheLost:
		return BANNER_LOST;
	case eTeam_Resistance:
		return BANNER_RESISTANCE;
	}

	return BANNER_LOCAL;
}

// The banner kind for a whole player's turn, for the replay narrator, which sees the player's
// turn-begin frame and has no group event.
static function int TeamBannerKind(ETeam Team)
{
	switch (Team)
	{
	case eTeam_Alien:
		return BANNER_ALIEN;
	case eTeam_TheLost:
		return BANNER_LOST;
	case eTeam_Resistance:
		return BANNER_RESISTANCE;
	}

	return BANNER_LOCAL;
}

// UIEndTurn's own gate: a banner is drawn only when its kind differs from the last one. Returns the
// banner's text — UITurnOverlay's own strings, which the running game populates — or "" when
// nothing is drawn (the same kind again, no decision, or the local turn, which has no banner).
static function string ClaimTurnBanner(int Kind)
{
	if (Kind < 0 || Kind == default.LastTurnBanner)
		return "";

	default.LastTurnBanner = Kind;

	switch (Kind)
	{
	case BANNER_ALIEN:
		return class'UITurnOverlay'.default.m_sAlienTurn;
	case BANNER_LOST:
		return class'UITurnOverlay'.default.m_sTheLostTurn;
	case BANNER_CHOSEN:
		return class'UITurnOverlay'.default.m_sChosenTurn;
	case BANNER_RESISTANCE:
		return class'UITurnOverlay'.default.m_sXComTurn;
	}

	return "";
}

static function ResetTurnBanner()
{
	default.LastTurnBanner = BANNER_LOCAL;
}

// ---------------------------------------------------------------------------
// Shots
// ---------------------------------------------------------------------------

static function EventListenerReturn OnAbilityActivated(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	local string Line;

	if (GameState == none)
		return ELR_NoInterrupt;

	Line = BuildAbilityLine(XComGameState_Ability(EventData),
		XComGameStateContext_Ability(GameState.GetContext()));

	if (Line != "")
		class'X2AccessNarrator'.static.Combat(Line);

	return ELR_NoInterrupt;
}

// The spoken form of one ability activation, or "" when it is not worth a line. Shared with the
// REPLAY narrator (X2AccessReplayNarration), which has no event to hand it and must reach the same
// words from a history frame — so everything here is derived from the context and the history,
// never from the event's payload.
static function string BuildAbilityLine(XComGameState_Ability AbilityState, XComGameStateContext_Ability AbilityContext)
{
	local XComGameState_Unit SourceUnit, TargetUnit;
	local X2AbilityTemplate AbilityTemplate;
	local string Line, AbilityName;

	if (AbilityState == none || AbilityContext == none)
		return "";

	// The game re-triggers AbilityActivated for every interrupt step (that is how overwatch gets
	// its chance to break in), so the un-interrupted pass is the only one that describes what
	// actually resolved.
	if (AbilityContext.InterruptionStatus == eInterruptionStatus_Interrupt)
		return "";

	AbilityTemplate = AbilityState.GetMyTemplate();
	if (AbilityTemplate == none || AbilityTemplate.bIsPassive)
		return "";

	// Movement belongs to the tile cursor, which already speaks the destination.
	if (AbilityTemplate.Hostility == eHostility_Movement)
		return "";

	// The rest of the ability system fires constantly for bookkeeping the player is shown nothing
	// about. An ability is worth a line when it is an attack, or when the player pressed a key to
	// use it.
	if (AbilityTemplate.Hostility != eHostility_Offensive && !AbilityState.IsAbilityInputTriggered())
		return "";

	SourceUnit = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(AbilityContext.InputContext.SourceObject.ObjectID));
	if (SourceUnit == none)
		return "";

	TargetUnit = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(AbilityContext.InputContext.PrimaryTarget.ObjectID));

	if (!IsNarratable(SourceUnit) && !IsNarratable(TargetUnit))
		return "";

	// An ability with no localized name is one the game never names to anyone — the Chosen's
	// ChosenActivation / ChosenEngaged fire as "offensive" on every reveal — so it gets no line.
	// Its template name would be an internal identifier read aloud, in English, to every player.
	AbilityName = AbilityTemplate.LocFriendlyName;
	if (AbilityName == "")
		return "";

	Line = UnitName(SourceUnit) $ ": " $ AbilityName;

	if (TargetUnit != none && TargetUnit.ObjectID != SourceUnit.ObjectID)
		Line = Line @ class'X2AccessVocab'.static.FillS(
			class'X2AccessVocab'.default.AbilityAtTarget, UnitName(TargetUnit));

	// Only abilities that actually roll get a result word. Grenades, heals and overwatch use the
	// DeadEye calc and always "hit"; saying so would be noise a sighted player is not shown either.
	if (RollsToHit(AbilityTemplate))
		Line = Line $ "," @ HitResultWord(AbilityContext.ResultContext.HitResult);

	return Line $ ".";
}

// ---------------------------------------------------------------------------
// Damage
// ---------------------------------------------------------------------------

static function EventListenerReturn OnUnitTakeEffectDamage(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	local XComGameState_Unit Unit;
	local string Line;

	if (GameState == none)
		return ELR_NoInterrupt;

	Unit = XComGameState_Unit(EventData);
	if (Unit == none)
		return ELR_NoInterrupt;

	// ⛔ ONE line per (unit, game state), and only REMEMBERED state can enforce that — which is the
	// whole reason the memo exists. XComGameState_Unit.TakeDamage appends exactly one DamageResults
	// entry and fires exactly one 'UnitTakeEffectDamage' (:5844, :5874), so a grenade that lands as
	// three entries against one unit delivers three events. We take them on
	// ELD_OnVisualizationBlockCompleted, by which point all three are queued against the SAME unit
	// state object with its array already complete — so every argument this function receives is
	// identical on all three passes, and BuildDamageLine, a pure function of (unit, context),
	// re-sums the same totals into the same sentence three times. Nothing stateless can pick one of
	// three indistinguishable calls: in particular a "was I handed the LAST entry?" test is true on
	// all three and so selects none of them.
	//
	// Claiming BEFORE building is deliberate. The answer cannot differ between the passes, so a
	// claim means "this pair is decided" whether that decision turned out to be a line or silence.
	if (!ClaimDamageNarration(GameState.HistoryIndex, Unit.ObjectID))
		return ELR_NoInterrupt;

	Line = BuildDamageLine(Unit, GameState.GetContext());
	if (Line != "")
		class'X2AccessNarrator'.static.Combat(Line);

	return ELR_NoInterrupt;
}

// True the first time this unit is claimed within this game state, false on every later pass.
//
// ⛔ The key is (state, unit) and never the TEXT, which is exactly what makes it safe where a text
// dedup is not: two different ADVENT Troopers produce byte-identical lines and BOTH must be spoken,
// and they are told apart here because their ObjectIDs differ. The same unit hit again later in the
// mission is a different game state and claims afresh.
//
// A SET per state rather than one last-claimed slot, because nothing promises the deferred queue
// hands a state's events to us grouped by unit. A delivery order of A, B, A is enough to defeat a
// single slot: B evicts A's claim, and A's second event then reads as a first one and speaks the
// duplicate this function exists to stop. The set costs nothing to be right instead — it holds only
// the units one game state damaged, a handful even for a big AoE, and is retired whole the moment
// the frame changes.
//
// Pipes on BOTH sides of every id, so the accumulated "|3||37|" cannot answer a search for "|7|":
// each key both opens and closes with the delimiter, so a match can only ever land on a whole one.
//
// ⚠ KNOWN HOLE, written down because its failure mode is the SILENT one this project treats as
// unreportable — a line dropped is indistinguishable from nothing having happened. This memo lives
// on the CDO, so it lasts as long as the process, while HistoryIndex is restored WITH a save. Load
// a mid-mission save and repeat the same action and the identical (frame, unit) pair comes round a
// second time, already claimed, and its damage line is swallowed. The exposure is one frame wide —
// the set is retired on the first index that differs — so it needs the reloaded action to be the
// very next damage AND to reproduce the pre-load index, which save-scumming one shot does exactly.
// Nothing derived from the game state can separate the two, because the replayed state IS the same
// state; only a fact from outside it (level-session time, a load counter) could, and choosing which
// wants a live run behind it rather than a guess here.
static function bool ClaimDamageNarration(int HistoryIndex, int UnitID)
{
	local string Key;

	if (default.NarratedDamageFrame != HistoryIndex)
	{
		default.NarratedDamageFrame = HistoryIndex;
		default.NarratedDamageUnits = "";
	}

	Key = "|" $ string(UnitID) $ "|";
	if (InStr(default.NarratedDamageUnits, Key) != INDEX_NONE)
		return false;

	default.NarratedDamageUnits = default.NarratedDamageUnits $ Key;
	return true;
}

// What one unit took from ONE game state, or "" when that state did nothing to it. Shared with the
// replay narrator, which passes the unit version carried by the frame it is describing.
static function string BuildDamageLine(XComGameState_Unit Unit, XComGameStateContext Context)
{
	local int i, Damage, Blocked, Shielded;
	local string Line;

	if (Unit == none || Context == none)
		return "";
	if (!IsNarratable(Unit))
		return "";

	// DamageResults accumulates across the whole mission, so "the last entry" is not necessarily
	// this hit. The game stamps every entry with the context that caused it (XComGameState_Unit
	// .TakeEffectDamage), which is what makes summing this state's entries exact — and summing
	// matters: one grenade can land as several entries against the same unit.
	//
	// ⚠ Summing fixes what a line SAYS, never how many times it is said. Those same several entries
	// are several events, and each of them re-sums this identical total; the COUNT is a separate
	// bug with a separate fix, and it lives in OnUnitTakeEffectDamage's ClaimDamageNarration. The
	// replay narrator needs no such guard — it visits each unit of a frame once by construction.
	for (i = Unit.DamageResults.Length - 1; i >= 0; i--)
	{
		if (Unit.DamageResults[i].Context != Context)
			break;

		Damage += Unit.DamageResults[i].DamageAmount;
		Blocked += Unit.DamageResults[i].MitigationAmount;
		Shielded += Unit.DamageResults[i].ShieldHP;
	}

	if (Damage == 0 && Blocked == 0 && Shielded == 0)
		return "";

	Line = UnitName(Unit);

	if (Damage > 0)
		Line = Line $ "," @ class'X2AccessVocab'.static.Fill1(
			class'X2AccessVocab'.default.DamageDealt, Damage);
	if (Shielded > 0)
		Line = Line $ "," @ class'X2AccessVocab'.static.Fill1(
			class'X2AccessVocab'.default.ShieldAbsorbs, Shielded);
	if (Blocked > 0)
		Line = Line $ "," @ class'X2AccessVocab'.static.Fill1(
			class'X2AccessVocab'.default.ArmorBlocks, Blocked);

	// The health tail is what the unit flag shows a sighted player, and it is the number that
	// decides the next move — without it "5 damage" says nothing about whether the target is dead.
	if (Unit.IsAlive())
	{
		Line = Line $ "," @ class'X2AccessVocab'.static.Fill2(
			class'X2AccessVocab'.default.TargetHealth,
			int(Unit.GetCurrentStat(eStat_HP)), int(Unit.GetMaxStat(eStat_HP)));
	}

	return Line $ ".";
}

static function EventListenerReturn OnShieldsExpended(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	local XComGameState_Unit Unit;

	Unit = XComGameState_Unit(EventData);
	if (!IsNarratable(Unit))
		return ELR_NoInterrupt;

	class'X2AccessNarrator'.static.Combat(class'X2AccessVocab'.static.FillS(
		class'X2AccessVocab'.default.NoShieldLeft, UnitName(Unit)));

	return ELR_NoInterrupt;
}

// ---------------------------------------------------------------------------
// Deaths
// ---------------------------------------------------------------------------

static function EventListenerReturn OnUnitDied(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	if (GameState == none)
		return ELR_NoInterrupt;

	NarrateDeath(XComGameState_Unit(EventData), GameState.GetContext(), GameState, true);

	return ELR_NoInterrupt;
}

// Announce one death on the channel it belongs on, or say nothing. Unlike the two builders above
// this one owns its push: the channel IS the decision here, and UnrealScript forbids `out bool`,
// so handing the answer back to the caller would mean an int flag pretending to be a boolean.
// Shared with the replay narrator.
static function NarrateDeath(XComGameState_Unit Unit, XComGameStateContext Context,
	XComGameState GameState, bool bCheckCurrentLoot)
{
	local XComGameState_Unit Killer;
	local XComGameStateContext_Ability AbilityContext;
	local bool bAnnounce;

	if (Unit == none)
		return;

	bAnnounce = IsNarratable(Unit);

	// A unit stops being visible the moment it dies, which would silence the single most useful
	// line in the game. So a kill the squad CAUSED is always announced — that is parity, not a
	// leak: the game shows the player their own kill regardless of what is still in sight.
	if (!bAnnounce)
	{
		AbilityContext = XComGameStateContext_Ability(Context);
		if (AbilityContext != none)
		{
			Killer = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(AbilityContext.InputContext.SourceObject.ObjectID));
			bAnnounce = Killer != none && Killer.IsFriendlyToLocalPlayer();
		}
	}

	if (!bAnnounce)
		return;

	// Losing one of your own changes what you can do next, so it is an Alert; an enemy kill is
	// Combat. Both queue in order behind the damage line that explains them.
	if (Unit.IsFriendlyToLocalPlayer())
		class'X2AccessNarrator'.static.Alert(class'X2AccessVocab'.static.FillS(
			class'XComPresentationLayer'.default.m_strUnitDied, UnitName(Unit)));
	else
	{
		class'X2AccessNarrator'.static.Combat(class'X2AccessVocab'.static.FillS(
			class'XComPresentationLayer'.default.m_strUnitDied, UnitName(Unit)));
		NarrateLootAvailable(Unit, GameState, bCheckCurrentLoot);
	}
}

// The death state contains the drop only when the game created collectible items. Match the
// source ID rather than assuming every killed enemy drops loot. In live play a nearby soldier may
// collect it immediately, so check the current version before promising a pickup; a replay must
// use its visualized frame instead, since the history already contains the replay's future.
static function NarrateLootAvailable(XComGameState_Unit Unit, XComGameState GameState,
	bool bCheckCurrentLoot)
{
	local XComGameState_LootDrop Drop, CurrentDrop;

	if (GameState == none)
		return;

	foreach GameState.IterateByClassType(class'XComGameState_LootDrop', Drop)
	{
		if (Drop.LootSourceID != Unit.ObjectID || Drop.LootExpirationTurnsRemaining <= 0
			|| !Drop.HasAvailableLoot())
			continue;

		if (bCheckCurrentLoot)
		{
			CurrentDrop = XComGameState_LootDrop(`XCOMHISTORY.GetGameStateForObjectID(Drop.ObjectID));
			if (CurrentDrop == none || CurrentDrop.LootExpirationTurnsRemaining <= 0
				|| !CurrentDrop.HasAvailableLoot())
				continue;
		}

		class'X2AccessNarrator'.static.Combat(class'X2AccessVocab'.default.LootAvailable);
		return;
	}
}

// ---------------------------------------------------------------------------
// Concealment, panic, evac
// ---------------------------------------------------------------------------

static function EventListenerReturn OnSquadConcealmentBroken(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	local XComGameState_Player PlayerState;

	PlayerState = XComGameState_Player(EventData);
	if (PlayerState == none || PlayerState.GetTeam() != eTeam_XCom)
		return ELR_NoInterrupt;

	class'X2AccessNarrator'.static.Alert(class'X2AccessVocab'.default.SquadConcealmentBroken);

	return ELR_NoInterrupt;
}

static function EventListenerReturn OnUnitConcealmentBroken(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	local XComGameState_Unit Unit;

	Unit = XComGameState_Unit(EventData);
	if (Unit == none || !Unit.IsFriendlyToLocalPlayer())
		return ELR_NoInterrupt;

	class'X2AccessNarrator'.static.Alert(class'X2AccessVocab'.static.FillS(
		class'X2AccessVocab'.default.UnitRevealed, UnitName(Unit)));

	return ELR_NoInterrupt;
}

// Concealment ENTERED. Two facts the game draws and the broken pair never covered: the HUD banner
// for the selected soldier (UITacticalHUD.RealizeConcealmentStatus: "SQUAD CONCEALED" or
// "CONCEALED"), and the "Concealed" flyover over the instigator
// (XComGameState_Unit.BuildVisualizationForConcealmentChanged).
//
// ⛔ Silent until the orientation line has been given. A mission that BEGINS concealed enters
// concealment during setup, before the HUD exists, and the orientation line already says it
// (X2AccessMissionPoll.Brief); a loaded save fires nothing at all, so the line is its only voice.
static function EventListenerReturn OnSquadConcealmentEntered(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	local XComGameState_Player PlayerState;

	PlayerState = XComGameState_Player(EventData);
	if (PlayerState == none || PlayerState.GetTeam() != eTeam_XCom)
		return ELR_NoInterrupt;
	if (!class'X2AccessMissionPoll'.static.IsBriefed())
		return ELR_NoInterrupt;

	class'X2AccessNarrator'.static.Alert(SquadConcealedWord());

	return ELR_NoInterrupt;
}

static function EventListenerReturn OnUnitConcealmentEntered(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	local XComGameState_Unit Unit;

	Unit = XComGameState_Unit(EventData);
	if (Unit == none || !Unit.IsFriendlyToLocalPlayer())
		return ELR_NoInterrupt;
	if (!class'X2AccessMissionPoll'.static.IsBriefed())
		return ELR_NoInterrupt;

	// A squad entering is the squad event's line. The game's own test for it
	// (BuildVisualizationForConcealmentChanged): the player's squad flag differs from its previous
	// version in this very state.
	if (IsSquadConcealmentChange(GameState))
		return ELR_NoInterrupt;

	class'X2AccessNarrator'.static.Alert(UnitConcealedLine(Unit));

	return ELR_NoInterrupt;
}

// True when this game state flips a player's squad-concealed flag either way.
static function bool IsSquadConcealmentChange(XComGameState GameState)
{
	local XComGameState_Player PlayerState, PrevPlayer;

	if (GameState == none)
		return false;

	foreach GameState.IterateByClassType(class'XComGameState_Player', PlayerState)
	{
		PrevPlayer = XComGameState_Player(`XCOMHISTORY.GetGameStateForObjectID(PlayerState.ObjectID, eReturnType_Reference, GameState.HistoryIndex - 1));
		if (PrevPlayer != none && PrevPlayer.bSquadIsConcealed != PlayerState.bSquadIsConcealed)
			return true;
	}

	return false;
}

// The HUD banner's own word. Shared with the replay narrator.
static function string SquadConcealedWord()
{
	return class'UITacticalHUD'.default.m_strSquadConcealed;
}

// The flyover word over one soldier, under the name the other lines use.
static function string UnitConcealedLine(XComGameState_Unit Unit)
{
	if (Unit == none || class'X2StatusEffects'.default.ConcealedFriendlyName == "")
		return "";

	return UnitName(Unit) $ ":" @ class'X2StatusEffects'.default.ConcealedFriendlyName $ ".";
}

static function EventListenerReturn OnUnitPanicked(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	local XComGameState_Unit Unit;

	Unit = XComGameState_Unit(EventData);
	if (!IsNarratable(Unit))
		return ELR_NoInterrupt;

	if (Unit.IsFriendlyToLocalPlayer())
		class'X2AccessNarrator'.static.Alert(class'X2AccessVocab'.static.FillS(
			class'XComPresentationLayer'.default.m_strUnitPanicked, UnitName(Unit)));
	else
		class'X2AccessNarrator'.static.Combat(class'X2AccessVocab'.static.FillS(
			class'XComPresentationLayer'.default.m_strUnitPanicked, UnitName(Unit)));

	return ELR_NoInterrupt;
}

static function EventListenerReturn OnUnitEvacuated(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	local XComGameState_Unit Unit;

	Unit = XComGameState_Unit(EventData);
	if (Unit == none)
		return ELR_NoInterrupt;

	class'X2AccessNarrator'.static.Combat(class'X2AccessVocab'.static.FillS(
		class'X2AccessVocab'.default.UnitEvacuated, UnitName(Unit)));

	return ELR_NoInterrupt;
}

// ---------------------------------------------------------------------------
// Sightings: the pod reveal and the reinforcement drop
// ---------------------------------------------------------------------------
//
// Neither cutscene draws a word. The reveal is a camera pan, a matinee on one unit and voice; the
// drop is the transport matinee with the units in its slots. What the player SEES is the pod's
// count and composition, and the state driving the picture holds exactly that list —
// XComGameStateContext_RevealAI.RevealedUnitObjectIDs for the reveal (the units
// X2Action_RevealAIBegin pans to and force-shows), the units created in the spawn state for the
// drop (the loop X2Action_ATT.AddUnitsToMatinee runs to fill its slots). Reading those lists is
// the read-the-screen rule applied to a screen with no widgets. The two texts NEAR the cutscene
// are covered elsewhere: the first-sighting Central line is the commlink subtitle the transient
// reader speaks, and the "surprised" flyover is a world message the tutorial reader polls.
//
// ⛔ A drop is BOTH events. OnSpawnReinforcementsComplete ends in AlertAndScamperUnits, which
// reaches OnScamperBegin and a reveal context for the units that just landed, so without a guard
// the same three troopers are named twice a few seconds apart. The guard is the spawn memo: the
// drop line claims each unit it names, and the reveal line skips claimed units. Claiming by
// IDENTITY and never by text is what makes a second, different ADVENT pod revealed the same turn
// still speak. It is a memo and not a chain-index test because the chain is the wrong question —
// what matters is whether the SPAWN was shown: a Lost swarm is a reinforcement spawner too
// ('TheLostSwarm', SeqAct_CallLostReinforcements) but its spawn builds no camera and no
// ShowSpawnedUnit, so the player first sees a swarm at its reveal, and that is where it must be
// spoken. SpawnLabel is the table of which spawn kinds the player watches.
//
// Same units are collapsed ("2 ADVENT Trooper, ADVENT Officer") by their spoken name — the name a
// sighted player tells them apart by — with the count only when it is more than one, because the
// game has no plural strings and inventing them would break the moment the player is not English.
//
// Both lines are Combat, not Alert. The reveal often lands seconds after 'SquadConcealmentBroken'
// (an Alert at submission) for the same move, and the drop lands after "Enemy turn."; an Alert
// here would cut those short. The cutscene runs for seconds, so a queued line has the time.

static function EventListenerReturn OnScamperBegin(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	local string Line;

	if (GameState == none)
		return ELR_NoInterrupt;

	Line = BuildRevealLine(XComGameStateContext_RevealAI(GameState.GetContext()), GameState.HistoryIndex);
	if (Line != "")
		class'X2AccessNarrator'.static.Combat(Line);

	return ELR_NoInterrupt;
}

static function EventListenerReturn OnSpawnReinforcementsComplete(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	local string Line;

	if (GameState == none)
		return ELR_NoInterrupt;

	Line = BuildReinforcementLine(GameState);
	if (Line != "")
		class'X2AccessNarrator'.static.Combat(Line);

	return ELR_NoInterrupt;
}

// The spoken form of one pod reveal, or "" when every revealed unit was already named by the drop
// it followed. HistoryIndex is the reveal state's frame, so the replay narrator can hand it the
// frame it is describing and read the same unit versions the live path does. Shared with the
// replay narrator.
static function string BuildRevealLine(XComGameStateContext_RevealAI RevealContext, int HistoryIndex)
{
	local XComGameStateHistory History;
	local XComGameState_Unit Unit;
	local X2CharacterTemplate FirstSeen;
	local array<string> Names;
	local array<int> Counts;
	local string Line, Roster;
	local int i;

	if (RevealContext == none || RevealContext.RevealAIEvent != eRevealAIEvent_Begin)
		return "";

	History = `XCOMHISTORY;

	// The pod is claimed for the sighting lines first, named or not: the scamper that follows the
	// reveal is part of the cutscene, and "in view" or "moved" lines may not interleave with it.
	for (i = 0; i < RevealContext.RevealedUnitObjectIDs.Length; i++)
		ClaimRevealed(RevealContext.RevealedUnitObjectIDs[i]);

	for (i = 0; i < RevealContext.RevealedUnitObjectIDs.Length; i++)
	{
		Unit = XComGameState_Unit(History.GetGameStateForObjectID(RevealContext.RevealedUnitObjectIDs[i], , HistoryIndex));
		if (Unit == none || !Unit.IsAlive())
			continue;
		if (WasSpawnNarrated(Unit.ObjectID))
			continue;

		CountName(UnitName(Unit), Names, Counts);
	}

	Roster = JoinRoster(Names, Counts);
	if (Roster == "")
		return "";

	Line = class'X2AccessVocab'.static.FillS(class'X2AccessVocab'.default.EnemySighted, Roster);

	// The game marks the reveal that is this campaign's first look at a type and plays the matinee
	// on that unit; Central's line follows through the commlink. The template is what the context
	// stores, so read it there rather than re-asking the HQ, which has already been updated.
	FirstSeen = RevealContext.FirstEncounterCharacterTemplate;
	if (FirstSeen != none && FirstSeen.strCharacterName != "")
		class'X2AccessHelper'.static.AppendPart(Line, class'X2AccessVocab'.static.FillS(
			class'X2AccessVocab'.default.NewEnemyType, FirstSeen.strCharacterName));

	return Line;
}

// The spoken form of one reinforcement drop — the units CREATED in the spawn state, under the
// label of how they arrived — or "" when this kind of spawn shows the player nothing. Claims every
// unit it names in the spawn memo. Shared with the replay narrator, which passes the frame's own
// game state; the live path passes the event's, which is the same state.
static function string BuildReinforcementLine(XComGameState SpawnState)
{
	local XComGameStateHistory History;
	local XComGameState_AIReinforcementSpawner Spawner;
	local XComGameState_Unit Unit;
	local array<string> Names;
	local array<int> Counts;
	local string Label, Roster;

	if (SpawnState == none)
		return "";

	// SpawnReinforcements modifies the spawner into the same state it spawns the units in (:250),
	// so the state carries its own kind; the event's EventData is the same object, and using the
	// state's copy is what lets a history frame answer identically.
	foreach SpawnState.IterateByClassType(class'XComGameState_AIReinforcementSpawner', Spawner)
		break;
	if (Spawner == none)
		return "";

	Label = SpawnLabel(Spawner.SpawnVisualizationType);
	if (Label == "")
		return "";

	History = `XCOMHISTORY;

	foreach SpawnState.IterateByClassType(class'XComGameState_Unit', Unit)
	{
		if (Unit.IsFriendlyToLocalPlayer() || !Unit.IsAlive())
			continue;
		// A unit with a version before this state was not dropped by it. The spawn state should
		// hold nothing else, but the matinee slots are the only thing that assumes so.
		if (History.GetPreviousGameStateForObject(Unit) != none)
			continue;

		ClaimSpawnNarration(Unit.ObjectID);
		CountName(UnitName(Unit), Names, Counts);
	}

	Roster = JoinRoster(Names, Counts);
	if (Roster == "")
		return "";

	return Label @ Roster $ ".";
}

// How the player watched this spawn arrive, or "" for a spawn kind the player is not shown.
// Mirrors XComGameState_AIReinforcementSpawner.BuildVisualizationForUnitSpawning: every kind but
// 'TheLostSwarm' gets a look-at camera (:446), the ATT its transport matinee (:456), Dropdown its
// warning effect plus ShowSpawnedUnit (:530-545), PsiGate and the Chosen kinds ShowSpawnedUnit
// with their effect (:501-570). The blank kind gets the camera but NO ShowSpawnedUnit (:501), so
// its units stay under the fog rule, and a swarm gets nothing — both are left to the reveal.
static function string SpawnLabel(name SpawnVisualizationType)
{
	switch (SpawnVisualizationType)
	{
	case 'ATT':
		return class'X2AccessVocab'.default.ReinforcementsTransport;
	case 'PsiGate':
		return class'X2AccessVocab'.default.ReinforcementsPsiGate;
	case 'Dropdown':
		return class'X2AccessVocab'.default.ReinforcementsDrop;
	case 'ChosenSpecialNoReveal':
	case 'ChosenSpecialTopDownReveal':
		return class'X2AccessVocab'.default.ChosenArrives;
	}

	return "";
}

// Tally one spoken name. Parallel arrays because UnrealScript has no map; a pod is a handful.
static function CountName(string UnitLabel, out array<string> Names, out array<int> Counts)
{
	local int Index;

	if (UnitLabel == "")
		return;

	Index = Names.Find(UnitLabel);
	if (Index == INDEX_NONE)
	{
		Names.AddItem(UnitLabel);
		Counts.AddItem(1);
	}
	else
	{
		Counts[Index] = Counts[Index] + 1;
	}
}

// "2 ADVENT Trooper, ADVENT Officer" — the count only where it says something.
static function string JoinRoster(array<string> Names, array<int> Counts)
{
	local string Roster, Part;
	local int i;

	for (i = 0; i < Names.Length; i++)
	{
		Part = Names[i];
		if (Counts[i] > 1)
			Part = string(Counts[i]) @ Part;

		class'X2AccessHelper'.static.AppendComma(Roster, Part);
	}

	return Roster;
}

// The spawn memo. Pipes on both sides of every id, for the reason ClaimDamageNarration gives.
static function ClaimSpawnNarration(int UnitID)
{
	local string Key;

	Key = "|" $ string(UnitID) $ "|";
	if (InStr(default.NarratedSpawnUnits, Key) == INDEX_NONE)
		default.NarratedSpawnUnits = default.NarratedSpawnUnits $ Key;
}

static function bool WasSpawnNarrated(int UnitID)
{
	return InStr(default.NarratedSpawnUnits, "|" $ string(UnitID) $ "|") != INDEX_NONE;
}

// Called on every turn handover by both narration paths. The failure mode of clearing too EARLY
// is a duplicate line, which is audible; the failure mode of clearing too late is a reveal that
// never speaks, which is not — so this errs on the side of the memo being short-lived.
static function ResetSpawnNarration()
{
	default.NarratedSpawnUnits = "";
	default.NarratedRevealUnits = "";
}

// ---------------------------------------------------------------------------
// Sightings: enemies coming into view, moving in view, leaving view
// ---------------------------------------------------------------------------
//
// What a sighted player is shown: the enemy's unit flag appears over a unit the squad can see
// (UIUnitFlagManager), the enemy strip gains a head (UITacticalHUD_Enemies.RealizeTargets, on block
// complete), and the camera or the player's eye follows a visible unit walking. Nothing is shown
// for a unit the squad cannot see, so every line here is gated on the squad's own gameplay
// visibility (GameRulesCache_VisibilityInfo.bVisibleGameplay, the flag the game's own
// SeqEvent_UnitSawObject keys "became visible" on) read AT THE FRAME the move belongs to, and the
// left-view line carries only the name the player already had, never a position.
//
// TUTORIAL AND REPLAY: no event fires there, so X2AccessReplayNarration walks the move frames it
// has been shown and calls the same NarrateSightings with bReplay set. In a replay the history
// holds the whole script, so the "latest state" fallback is refused and a reveal check looks only
// at frames the replay itself will narrate.

static function EventListenerReturn OnUnitMoveFinished(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	local XComGameState_Unit Mover;

	Mover = XComGameState_Unit(EventData);
	if (Mover == none || GameState == none)
		return ELR_NoInterrupt;

	NarrateSightings(Mover.ObjectID, GameState.HistoryIndex, false, false);

	return ELR_NoInterrupt;
}

// Re-evaluate who the squad can see after one move. An ENEMY mover changes the picture for itself
// only, so only it is examined (a big enemy turn costs one unit per move, not the roster); a
// friendly mover can reveal or hide anyone, so every enemy is. bSilent records without speaking,
// which is how the orientation line baselines the memo. Frame is the history frame of the move, or
// -1 for "now" (only the baseline passes that).
static function NarrateSightings(int MoverID, int Frame, bool bReplay, bool bSilent)
{
	local XComGameStateHistory History;
	local XComGameState_Unit Mover, Unit;

	if (!class'X2AccessMissionPoll'.static.IsBriefed() && !bSilent)
		return;

	History = `XCOMHISTORY;

	Mover = XComGameState_Unit(History.GetGameStateForObjectID(MoverID, , Frame));
	if (Mover != none && IsSightingEnemy(Mover))
	{
		EvaluateSighting(Mover.ObjectID, Frame, MoverID, bReplay, bSilent);
		return;
	}

	// Silent baseline (MoverID -1) and friendly movers examine everyone; any other mover (a
	// civilian, a unit the squad does not fight) changes nothing the squad sees.
	if (!bSilent && (Mover == none || !Mover.IsFriendlyToLocalPlayer()))
		return;

	foreach History.IterateByClassType(class'XComGameState_Unit', Unit)
	{
		if (IsSightingEnemy(Unit))
			EvaluateSighting(Unit.ObjectID, Frame, MoverID, bReplay, bSilent);
	}
}

// Hostile and drawn: the enemy strip's population (aliens and the Lost), no cosmetic gremlins.
static function bool IsSightingEnemy(XComGameState_Unit Unit)
{
	local X2CharacterTemplate Template;

	if (Unit == none || Unit.IsFriendlyToLocalPlayer())
		return false;
	if (Unit.GetTeam() != eTeam_Alien && Unit.GetTeam() != eTeam_TheLost)
		return false;

	Template = Unit.GetMyTemplate();
	return Template != none && !Template.bIsCosmetic;
}

// One enemy against the memo. The unit is re-fetched by ObjectID at the frame being described,
// never taken from the event payload or an earlier loop, so it is the version the picture shows.
static function EvaluateSighting(int UnitID, int Frame, int MoverID, bool bReplay, bool bSilent)
{
	local XComGameState_Unit Cur;
	local string Old, Tile, Line;
	local bool bVisible;

	Cur = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(UnitID, , Frame));
	if (Cur == none)
		return;

	Old = SightingMemoEntry(UnitID);

	bVisible = Cur.IsAlive() && !Cur.bRemovedFromPlay && EnemyVisibleAtFrame(UnitID, Frame, bReplay);
	if (!bVisible)
	{
		if (Old == "")
			return;
		SetSightingMemo(UnitID, "");
		// A death is the death line's; only a unit that is still there and merely unseen is
		// spoken, and the line carries no position.
		if (!bSilent && Cur.IsAlive() && !Cur.bRemovedFromPlay && !WasRevealedThisTurn(UnitID))
			SpeakSighting(class'X2AccessVocab'.default.SightedLeft, Cur, false);
		return;
	}

	Tile = string(UnitID) $ "@" $ string(Cur.TileLocation.X) $ "," $ string(Cur.TileLocation.Y)
		$ "," $ string(Cur.TileLocation.Z);
	if (Old == Tile)
		return;

	SetSightingMemo(UnitID, Tile);
	if (bSilent || WasRevealedThisTurn(UnitID))
		return;

	if (Old == "")
	{
		// Seen for the first time: the pod reveal and the reinforcement drop each speak their own
		// units, so stand down for a unit either has named, or whose reveal is already in the
		// history ahead of this move and will be spoken as the camera pans.
		if (WasSpawnNarrated(UnitID) || IsRevealPending(UnitID, Frame))
			return;
		SpeakSighting(class'X2AccessVocab'.default.SightedInView, Cur, true);
	}
	else if (UnitID == MoverID)
	{
		// It was visible before and is visible now at another tile, and it is the unit whose move
		// just finished. A tile change with any other cause is recorded and not spoken.
		SpeakSighting(class'X2AccessVocab'.default.SightedMoved, Cur, true);
	}
}

// "Muton in view, 8 tiles, 3 north, 7 east" — the object scanner's enemy words (name, tile count,
// compass offset), measured from the selected soldier. Short on purpose: a big enemy turn queues
// one of these per visible move and every one is spoken.
static function SpeakSighting(string Template, XComGameState_Unit Unit, bool bWithPlace)
{
	local XComGameState_Unit Active;
	local string Line;
	local int Dist, DX, DY, DZ;

	Line = class'X2AccessVocab'.static.FillS(Template, Unit.GetName(eNameType_Full));

	Active = class'X2AccessTileReader'.static.ActiveUnitState();
	if (bWithPlace && Active != none)
	{
		DX = Abs(Active.TileLocation.X - Unit.TileLocation.X);
		DY = Abs(Active.TileLocation.Y - Unit.TileLocation.Y);
		DZ = Abs(Active.TileLocation.Z - Unit.TileLocation.Z);
		Dist = Max(DX, DY) + DZ;
		class'X2AccessHelper'.static.AppendComma(Line, class'X2AccessVocab'.static.Count(Dist,
			class'X2AccessVocab'.default.Tiles_One, class'X2AccessVocab'.default.Tiles_Few,
			class'X2AccessVocab'.default.Tiles_Many));
		class'X2AccessHelper'.static.AppendComma(Line,
			class'X2AccessTileReader'.static.OffsetWords(Active.TileLocation, Unit.TileLocation));
	}

	class'X2AccessNarrator'.static.Combat(Line $ ".");
}

// Can any soldier see the enemy at this history frame? The game's own per-pair visibility, asked
// for the frame, so a move that finished a few seconds behind the history is judged on what the
// picture showed then. When the manager holds no record for any pair (a frame it never cached) a
// live read falls back to the present, which is the answer every other line here is gated on; a
// replay answers "no", since its present is the script's future.
static function bool EnemyVisibleAtFrame(int EnemyID, int Frame, bool bReplay)
{
	local XComGameStateHistory History;
	local X2GameRulesetVisibilityManager VisMgr;
	local XComGameState_Unit Viewer, ShownViewer;
	local X2CharacterTemplate Template;
	local GameRulesCache_VisibilityInfo Info;
	local bool bFound;

	History = `XCOMHISTORY;
	VisMgr = `TACTICALRULES.VisibilityMgr;

	if (VisMgr != none)
	{
		foreach History.IterateByClassType(class'XComGameState_Unit', Viewer)
		{
			if (Viewer.GetTeam() != eTeam_XCom)
				continue;
			ShownViewer = XComGameState_Unit(History.GetGameStateForObjectID(Viewer.ObjectID, , Frame));
			if (ShownViewer == none || !ShownViewer.IsAlive() || ShownViewer.bRemovedFromPlay)
				continue;
			Template = ShownViewer.GetMyTemplate();
			if (Template != none && Template.bIsCosmetic)
				continue;

			if (VisMgr.GetVisibilityInfo(ShownViewer.ObjectID, EnemyID, Info, Frame))
			{
				bFound = true;
				if (Info.bVisibleGameplay)
					return true;
			}
		}
	}

	if (!bFound && !bReplay)
		return class'X2TacticalVisibilityHelpers'.static.CanXComSquadSeeTarget(EnemyID);

	return false;
}

// True when a pod reveal naming this unit sits in the history AFTER the move's frame. Submission
// runs ahead of visualization, so the reveal a move triggers is already recorded when the move's
// block completes, and the reveal line (spoken at the pan) will name the unit.
static function bool IsRevealPending(int UnitID, int Frame)
{
	local XComGameStateHistory History;
	local XComGameState FrameState;
	local XComGameStateContext_RevealAI RevealContext;
	local int i, LastFrame;

	History = `XCOMHISTORY;
	LastFrame = Min(History.GetCurrentHistoryIndex(), Frame + REVEAL_LOOKAHEAD);

	for (i = Frame + 1; i <= LastFrame; i++)
	{
		FrameState = History.GetGameStateFromHistory(i);
		if (FrameState == none)
			continue;
		RevealContext = XComGameStateContext_RevealAI(FrameState.GetContext());
		if (RevealContext != none && RevealContext.RevealAIEvent == eRevealAIEvent_Begin
			&& RevealContext.RevealedUnitObjectIDs.Find(UnitID) != INDEX_NONE)
			return true;
	}

	return false;
}

// Record who is visible right now without speaking, so the first move of a session (a load, the
// tutorial) recites nothing the player already knew. Called by the orientation line.
static function BaselineSightings(int Frame, bool bReplay)
{
	NarrateSightings(-1, Frame, bReplay, true);
}

static function ResetSightings()
{
	default.SightingMemo = "";
	default.NarratedRevealUnits = "";
}

// The memo entry "id@x,y,z" for one unit, or "".
static function string SightingMemoEntry(int UnitID)
{
	local string Rest;
	local int At, Stop;

	At = InStr(default.SightingMemo, "|" $ string(UnitID) $ "@");
	if (At == INDEX_NONE)
		return "";

	Rest = Mid(default.SightingMemo, At + 1);
	Stop = InStr(Rest, "|");
	if (Stop == INDEX_NONE)
		return "";

	return Left(Rest, Stop);
}

// Replace (or, with "", remove) one unit's entry. Entries share their pipes: "|a|b|".
static function SetSightingMemo(int UnitID, string Entry)
{
	local string Old;

	Old = SightingMemoEntry(UnitID);
	if (Old != "")
		default.SightingMemo = Repl(default.SightingMemo, "|" $ Old $ "|", "|");

	if (Entry == "")
		return;

	if (default.SightingMemo == "")
		default.SightingMemo = "|";
	default.SightingMemo = default.SightingMemo $ Entry $ "|";
}

static function ClaimRevealed(int UnitID)
{
	if (!WasRevealedThisTurn(UnitID))
		default.NarratedRevealUnits = default.NarratedRevealUnits $ "|" $ string(UnitID) $ "|";
}

static function bool WasRevealedThisTurn(int UnitID)
{
	return InStr(default.NarratedRevealUnits, "|" $ string(UnitID) $ "|") != INDEX_NONE;
}

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

// Fog parity. Our own units are always narratable — the player commands them and the game never
// hides their state. Anyone else has to be visible to the squad, the same test the game uses
// before it draws a unit flag or a flyover over them.
static function bool IsNarratable(XComGameState_Unit Unit)
{
	if (Unit == none)
		return false;
	if (Unit.IsFriendlyToLocalPlayer())
		return true;

	return class'X2TacticalVisibilityHelpers'.static.CanXComSquadSeeTarget(Unit.ObjectID);
}

// The same name the game's own combat log uses, so a spoken line and a logged line agree.
static function string UnitName(XComGameState_Unit Unit)
{
	if (Unit == none)
		return "";

	return Unit.GetName(eNameType_RankFull);
}

static function bool RollsToHit(X2AbilityTemplate AbilityTemplate)
{
	if (AbilityTemplate.AbilityToHitCalc == none)
		return false;

	return X2AbilityToHitCalc_DeadEye(AbilityTemplate.AbilityToHitCalc) == none;
}

// The game's own localized flyover wording ("MISSED", "GRAZED", "LIGHTNING REFLEXES"), so the
// spoken result matches what a sighted player reads and stays translated.
static function string HitResultWord(EAbilityHitResult Result)
{
	local string Word;

	Word = class'X2TacticalGameRulesetDataStructures'.default.m_aAbilityHitResultStrings[Result];
	if (Word != "")
		return Word;

	return string(Result);
}
