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
var config int NarratedDamageFrame;
var config string NarratedDamageUnits;

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

	// Structural.
	Template.AddCHEvent('PlayerTurnBegun', OnPlayerTurnBegun, ELD_OnStateSubmitted, 50);
	Template.AddCHEvent('SquadConcealmentBroken', OnSquadConcealmentBroken, ELD_OnStateSubmitted, 50);
	Template.AddCHEvent('UnitConcealmentBroken', OnUnitConcealmentBroken, ELD_OnStateSubmitted, 45);
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

	// XCom and the aliens only. WOTC also runs turns for the Lost, civilians and the Resistance,
	// and the game gives those no turn banner either — announcing them would turn a Lost-heavy
	// mission into a metronome.
	switch (PlayerState.GetTeam())
	{
	case eTeam_XCom:
		class'X2AccessNarrator'.static.Alert("Your turn.");
		break;
	case eTeam_Alien:
		class'X2AccessNarrator'.static.Alert("Enemy turn.");
		break;
	}

	return ELR_NoInterrupt;
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

	AbilityName = AbilityTemplate.LocFriendlyName;
	if (AbilityName == "")
		AbilityName = string(AbilityTemplate.DataName);

	Line = UnitName(SourceUnit) $ ": " $ AbilityName;

	if (TargetUnit != none && TargetUnit.ObjectID != SourceUnit.ObjectID)
		Line = Line @ "at" @ UnitName(TargetUnit);

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
		Line = Line $ "," @ string(Damage) @ "damage";
	if (Shielded > 0)
		Line = Line $ ", shield absorbs" @ string(Shielded);
	if (Blocked > 0)
		Line = Line $ ", armour blocks" @ string(Blocked);

	// The health tail is what the unit flag shows a sighted player, and it is the number that
	// decides the next move — without it "5 damage" says nothing about whether the target is dead.
	if (Unit.IsAlive())
	{
		Line = Line $ "," @ string(int(Unit.GetCurrentStat(eStat_HP))) @ "of"
			@ string(int(Unit.GetMaxStat(eStat_HP))) @ "health";
	}

	return Line $ ".";
}

static function EventListenerReturn OnShieldsExpended(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	local XComGameState_Unit Unit;

	Unit = XComGameState_Unit(EventData);
	if (!IsNarratable(Unit))
		return ELR_NoInterrupt;

	class'X2AccessNarrator'.static.Combat(UnitName(Unit) @ "has no shield left.");

	return ELR_NoInterrupt;
}

// ---------------------------------------------------------------------------
// Deaths
// ---------------------------------------------------------------------------

static function EventListenerReturn OnUnitDied(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	if (GameState == none)
		return ELR_NoInterrupt;

	NarrateDeath(XComGameState_Unit(EventData), GameState.GetContext());

	return ELR_NoInterrupt;
}

// Announce one death on the channel it belongs on, or say nothing. Unlike the two builders above
// this one owns its push: the channel IS the decision here, and UnrealScript forbids `out bool`,
// so handing the answer back to the caller would mean an int flag pretending to be a boolean.
// Shared with the replay narrator.
static function NarrateDeath(XComGameState_Unit Unit, XComGameStateContext Context)
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

	// Losing one of your own changes what you can do next, so it cuts in; an enemy kill queues
	// behind the damage line that explains it.
	if (Unit.IsFriendlyToLocalPlayer())
		class'X2AccessNarrator'.static.Alert(UnitName(Unit) @ "is dead.");
	else
		class'X2AccessNarrator'.static.Combat(UnitName(Unit) @ "killed.");
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

	class'X2AccessNarrator'.static.Alert("Squad concealment broken.");

	return ELR_NoInterrupt;
}

static function EventListenerReturn OnUnitConcealmentBroken(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	local XComGameState_Unit Unit;

	Unit = XComGameState_Unit(EventData);
	if (Unit == none || !Unit.IsFriendlyToLocalPlayer())
		return ELR_NoInterrupt;

	class'X2AccessNarrator'.static.Alert(UnitName(Unit) @ "is revealed.");

	return ELR_NoInterrupt;
}

static function EventListenerReturn OnUnitPanicked(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	local XComGameState_Unit Unit;

	Unit = XComGameState_Unit(EventData);
	if (!IsNarratable(Unit))
		return ELR_NoInterrupt;

	if (Unit.IsFriendlyToLocalPlayer())
		class'X2AccessNarrator'.static.Alert(UnitName(Unit) @ "panicked.");
	else
		class'X2AccessNarrator'.static.Combat(UnitName(Unit) @ "panicked.");

	return ELR_NoInterrupt;
}

static function EventListenerReturn OnUnitEvacuated(Object EventData, Object EventSource, XComGameState GameState, Name Event, Object CallbackData)
{
	local XComGameState_Unit Unit;

	Unit = XComGameState_Unit(EventData);
	if (Unit == none)
		return ELR_NoInterrupt;

	class'X2AccessNarrator'.static.Combat(UnitName(Unit) @ "evacuated.");

	return ELR_NoInterrupt;
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

defaultproperties
{
	// -1 is not a real HistoryIndex, so the first damage of a session always resets the claimed set
	// rather than starting out sharing a frame with state 0.
	NarratedDamageFrame=-1
}
