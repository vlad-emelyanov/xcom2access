// X2AccessTacticalReader
// The shot HUD's game-state half — Phase 1 of the tactical layer. Same contract as
// X2AccessTileReader: the C++ layer owns the keys and speech, calls these functions on this
// class's default object via ProcessEvent (game thread, from the frame pump) and reads
// NativeResult back through reflection right after the call returns. Nothing here runs on its
// own — no listeners, no timers, no polling.
//
// WHY THE WORDS LIVE HERE AND NOT IN C++: the tactical HUD retains no text. Every panel of
// UITacticalHUD is a write-only pipe into Flash (UITacticalHUD_InfoPanel.Update resolves the
// shot name, hit chance, crit chance and damage out of the game state and hands them straight
// to MC.BeginFunctionOp, keeping nothing), so there is no widget to read them back from. The
// widget tree supplies STRUCTURE and SELECTION — which ability index, which target index — and
// everything spoken below is re-resolved from XCOMHISTORY exactly the way the panel does it.
//
// SIGHTED PARITY: every value spoken here is on screen while the shot HUD is raised.
//   ability name + help/unavailable reason, damage range, hit % and crit %
//       -> UITacticalHUD_InfoPanel.Update (the shot info panel)
//   the per-modifier breakdown (H)
//       -> UITacticalHUD_ShotWings.RefreshData, which a mouse player opens by clicking the
//          wing buttons — so it is on demand for them too
//   target name, health, armour, shield
//       -> the target's unit flag and the enemy preview
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessTacticalReader extends Object;

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// Change detection for the automatic poll. Object IDs only (value types — CDO rule).
// LastAbilityID 0 means "nothing announced yet", which is also the reset state.
var int LastAbilityID;
var int LastTargetID;
var bool bWasRaised;

// Which MODE the shot HUD is in, reflected out to native next to NativeResult (the same pattern
// the hacking screen uses for its own NativeState). "freeaim" while a cursor-targeted ability is
// up, empty otherwise. Rewritten by every PollTargeting call, so a stale value cannot linger.
var string NativeState;

// The authoritative aim tile for free aim. STATE-FIRST for the same reason the tile cursor is:
// the cursor pawn sits at body-center heights and is repositioned natively, so it is pushed and
// never stepped from. Valid only while bAimPlanted, and only for AimAbilityID.
var int AimX, AimY, AimZ;
var bool bAimPlanted;
var int AimAbilityID;

// Change detection for the "it does not land where you aimed" report, so a blocked throw is
// announced once rather than several times a second.
var int LastLandX, LastLandY;
var bool bLandWarned;

// One divergent reading is not evidence. The arc resolves a frame AFTER our push, so the first
// poll following any aim move can legitimately still describe the previous aim point — live
// 2026-08-20 that produced "Blocked, lands 6 north, 1 west" about a throw that then landed
// perfectly, because the reading was the pre-jump arc pointing back at the shooter. A divergence
// therefore has to survive two consecutive polls before it is spoken: a stale frame cannot, a
// real obstruction always does.
var int PendingLandX, PendingLandY;
var bool bLandPending;

// ---------------------------------------------------------------------------
// Entry points (called from native)
// ---------------------------------------------------------------------------

// The automatic narrator, called on the C++ tick while the tactical HUD is usable. Silent
// unless something the player would have SEEN change actually changed: raising the shot HUD
// announces ability and target in full, cycling abilities re-announces both (the target set
// changes with the ability), cycling targets announces the target alone.
//
// Deliberately silent — not "nothing selected" — whenever it cannot resolve a shot: this runs
// several times a second, and a poll that talks about its own failures is unusable. The
// on-demand keys below are the ones that always answer.
function PollTargeting()
{
	local UITacticalHUD HUD;
	local AvailableAction Action;
	local XComGameState_Ability AbilityState;
	local StateObjectReference TargetRef;
	local int TargetIndex;
	local string Result;

	NativeResult = "";
	NativeState = "";

	HUD = GetTacticalHUD();
	if (HUD == none || !HUD.m_isMenuRaised)
	{
		// Lowered (or gone): forget what was announced so the next raise reads in full.
		if (bWasRaised)
		{
			bWasRaised = false;
			LastAbilityID = 0;
			LastTargetID = 0;
			ResetAim();
		}
		return;
	}
	bWasRaised = true;

	if (!ResolveShot(HUD, Action, AbilityState, TargetRef, TargetIndex))
		return;

	// Free aim is a MODE of this screen rather than a screen of its own, so it is reported from
	// here and narrated on its own terms. Native claims the aim keys off this flag; keeping the
	// decision next to the data is what stops a regenerated SDK from silently breaking it.
	if (CursorMethod(HUD) != none)
	{
		NativeState = "freeaim";
		NativeResult = PollFreeAim(HUD, Action, AbilityState);
		return;
	}

	if (Action.AbilityObjectRef.ObjectID != LastAbilityID)
	{
		// A new ability brings its own target list, so both halves are news.
		Result = DescribeAbility(HUD, Action, AbilityState);
		AppendPart(Result, DescribeTarget(HUD, Action, AbilityState, TargetRef, TargetIndex, false));
	}
	else if (TargetRef.ObjectID != LastTargetID)
	{
		Result = DescribeTarget(HUD, Action, AbilityState, TargetRef, TargetIndex, false);
	}
	else
	{
		return;
	}

	LastAbilityID = Action.AbilityObjectRef.ObjectID;
	LastTargetID = TargetRef.ObjectID;
	NativeResult = Result;
}

// Re-announce the whole shot on demand (Delete), mirroring the tile cursor's re-announce key.
// Unlike the poll this ALWAYS says something — a pressed key must never be silent.
function AnnounceShot()
{
	local UITacticalHUD HUD;
	local AvailableAction Action;
	local XComGameState_Ability AbilityState;
	local StateObjectReference TargetRef;
	local int TargetIndex;
	local string Result;

	NativeResult = "";

	HUD = GetTacticalHUD();
	if (HUD == none || !HUD.m_isMenuRaised)
	{
		NativeResult = class'X2AccessVocab'.default.NotTargeting;
		return;
	}
	if (!ResolveShot(HUD, Action, AbilityState, TargetRef, TargetIndex))
	{
		NativeResult = class'X2AccessVocab'.default.NoAbilitySelected;
		return;
	}

	// Free aim: the aim POINT is the subject, not a target-list entry. Delete stays ONE claim
	// that branches here rather than two claims racing for the same chord.
	if (CursorMethod(HUD) != none)
	{
		Result = DescribeAbility(HUD, Action, AbilityState);
		AppendPart(Result, DescribeAimNow(HUD, AbilityState, true));
		AppendPart(Result, AbilityState.GetMyHelpText());
		LastAbilityID = Action.AbilityObjectRef.ObjectID;
		LastTargetID = 0;
		NativeResult = Result;
		return;
	}

	Result = DescribeAbility(HUD, Action, AbilityState);
	AppendPart(Result, DescribeTarget(HUD, Action, AbilityState, TargetRef, TargetIndex, true));
	AppendPart(Result, AbilityState.GetMyHelpText());

	// Keep the poll quiet about what was just spoken.
	LastAbilityID = Action.AbilityObjectRef.ObjectID;
	LastTargetID = TargetRef.ObjectID;
	NativeResult = Result;
}

// The full to-hit breakdown on demand (H) — every ShotModifierInfo the shot wings would list,
// hit modifiers then crit modifiers, in the game's own wording and sign convention.
function AnnounceBreakdown()
{
	local UITacticalHUD HUD;
	local AvailableAction Action;
	local XComGameState_Ability AbilityState;
	local StateObjectReference TargetRef;
	local ShotBreakdown Breakdown;
	local int TargetIndex;
	local string Result;

	NativeResult = "";

	HUD = GetTacticalHUD();
	if (HUD == none || !HUD.m_isMenuRaised)
	{
		NativeResult = class'X2AccessVocab'.default.NotTargeting;
		return;
	}
	if (!ResolveShot(HUD, Action, AbilityState, TargetRef, TargetIndex))
	{
		NativeResult = class'X2AccessVocab'.default.NoAbilitySelected;
		return;
	}
	// A free-aimed ability has no per-target to-hit table to break down, and the game shows none
	// either: while a cursor ability is up the enemy strip is empty and the reticle reads no aim
	// percentage. So this is parity rather than a gap — say so, and give the aim instead.
	if (CursorMethod(HUD) != none)
	{
		Result = class'X2AccessVocab'.default.NoFreeAimBreakdown;
		AppendPart(Result, DescribeAimNow(HUD, AbilityState, true));
		NativeResult = Result;
		return;
	}

	if (!GetBreakdown(AbilityState, TargetRef, Breakdown))
	{
		NativeResult = class'X2AccessVocab'.default.NoShotBreakdown;
		return;
	}

	Result = class'UITacticalHUD_ShotHUD'.default.m_sShotChanceLabel @
		class'X2AccessVocab'.static.Fill1(class'X2AccessVocab'.default.PercentValue,
			HitChanceOf(Breakdown));
	AppendPart(Result, DescribeModifiers(Breakdown, eHit_Success));
	AppendPart(Result, class'UITacticalHUD_ShotHUD'.default.m_sCritChanceLabel @
		class'X2AccessVocab'.static.Fill1(class'X2AccessVocab'.default.PercentValue,
			Breakdown.ResultTable[eHit_Crit]));
	AppendPart(Result, DescribeModifiers(Breakdown, eHit_Crit));
	NativeResult = Result;
}

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------

private function UITacticalHUD GetTacticalHUD()
{
	local UIScreenStack Stack;

	Stack = `SCREENSTACK;
	if (Stack == none)
		return none;
	return UITacticalHUD(Stack.GetFirstInstanceOf(class'UITacticalHUD'));
}

// The selected ability and the target the reticle is on, resolved exactly the way
// UITacticalHUD_InfoPanel.Update and UITacticalHUD_ShotWings.RefreshData do: the targeting
// method owns the target INDEX (it is what Tab and Shift cycle), and the index is into the
// selected action's own target list. Returns false when there is no ability to talk about;
// a valid ability with no target is a true result with TargetRef.ObjectID left 0.
private function bool ResolveShot(UITacticalHUD HUD, out AvailableAction Action,
	out XComGameState_Ability AbilityState, out StateObjectReference TargetRef,
	out int TargetIndex)
{
	local X2TargetingMethod Method;

	TargetIndex = 0;
	if (HUD.m_kAbilityHUD == none)
		return false;

	Action = HUD.m_kAbilityHUD.GetSelectedAction();
	if (Action.AbilityObjectRef.ObjectID <= 0)
		return false;

	AbilityState = XComGameState_Ability(
		`XCOMHISTORY.GetGameStateForObjectID(Action.AbilityObjectRef.ObjectID));
	if (AbilityState == none)
		return false;

	// Index 0 is the fallback the shot wings use when there is no targeting method to ask.
	// X2TargetingMethod's own GetTargetIndex returns -1 (the base class tracks no index; only
	// the aimed subclasses override it), so an unindexed method falls back the same way rather
	// than reading as "no target".
	Method = HUD.m_kAbilityHUD.GetTargetingMethod();
	if (Method != none)
		TargetIndex = Max(Method.GetTargetIndex(), 0);
	if (TargetIndex < Action.AvailableTargets.Length)
		TargetRef = Action.AvailableTargets[TargetIndex].PrimaryTarget;

	return true;
}

// The game's own to-hit table for this ability against this target. Routed through
// LookupShotBreakdown with an EXPLICIT UseAbility, which is the branch that cannot RedScreen
// on us (the empty-ability branch hunts for the primary weapon's display ability and
// complains when it finds none). False when the game itself would show no numbers.
private function bool GetBreakdown(XComGameState_Ability AbilityState,
	StateObjectReference TargetRef, out ShotBreakdown Breakdown)
{
	local X2AbilityTemplate Template;

	Template = AbilityState.GetMyTemplate();
	if (Template == none || Template.AbilityToHitCalc == none)
		return false;
	if (AbilityState.iCooldown > 0)
		return false;

	AbilityState.LookupShotBreakdown(AbilityState.OwnerStateObject, TargetRef,
		AbilityState.GetReference(), Breakdown);
	if (Breakdown.HideShotBreakdown)
		return false;
	return HitChanceOf(Breakdown) > -1;
}

private function int HitChanceOf(ShotBreakdown Breakdown)
{
	if (Breakdown.bIsMultishot)
		return Min(Breakdown.MultiShotHitChance, 100);
	return Min(Breakdown.FinalHitChance, 100);
}

// ---------------------------------------------------------------------------
// Description
// ---------------------------------------------------------------------------

// "Overwatch, 3 of 7, 2 charges" — everything UITacticalHUD_Ability.UpdateData puts ON the icon,
// plus the game's own reason string when the ability is not usable (the same string the info panel
// puts under the name in place of the help text).
//
// The icon carries FOUR things a sighted player reads at a glance, and the first three were
// missing until 2026-08-21:
//   - the friendly name, as antenna text (`SetAntennaText`);
//   - CHARGES, `m_strChargePrefix $ GetCharges()` — how many grenades are left. Shown only when
//     the ability is not cooling down, and only when GetCharges() >= 0 (an ability with no charge
//     system answers -1), so both conditions are mirrored here rather than guessed;
//   - COOLDOWN REMAINING, `m_strCooldownPrefix $ GetCooldownRemaining()`. ⛔ The availability
//     reason is NOT a substitute: GetDisplayStringForAvailabilityCode returns one fixed localized
//     string per code, so 'AA_CooldownNotMet' says an ability is cooling down and never says for
//     how long. The NUMBER was sighted-only;
//   - the OBJECTIVE HIGHLIGHT, `BattleData.IsAbilityObjectiveHighlighted` — the icon is recoloured
//     to mark the ability that completes the mission objective. That one is a real signal, unlike
//     the perk/psionic/debuff source colours, which only restate where an ability came from.
// The index/count is ours, but it is parity: m_arrAbilities is already filtered by
// ShouldShowAbilityIcon and sorted by SortAbilities, so it is exactly the row on screen.
private function string DescribeAbility(UITacticalHUD HUD, AvailableAction Action,
	XComGameState_Ability AbilityState)
{
	local XComGameState_BattleData BattleData;
	local X2AbilityTemplate Template;
	local string Result;
	local int Charges;

	Result = AbilityState.GetMyFriendlyName();
	if (Action.bFreeAim)
		Result = class'X2AccessVocab'.default.FreeAimLabel @ Result;

	if (HUD.m_kAbilityHUD != none && HUD.m_kAbilityHUD.m_arrAbilities.Length > 0)
		Result $= "," @ class'X2AccessVocab'.static.Fill2(
			class'X2AccessVocab'.default.PositionOf,
			HUD.m_kAbilityHUD.m_iCurrentIndex + 1, HUD.m_kAbilityHUD.m_arrAbilities.Length);

	if (AbilityState.IsCoolingDown())
	{
		AppendComma(Result, class'X2AccessVocab'.static.Count(
			AbilityState.GetCooldownRemaining(),
			class'X2AccessVocab'.default.Cooldown_One,
			class'X2AccessVocab'.default.Cooldown_Few,
			class'X2AccessVocab'.default.Cooldown_Many));
	}
	else
	{
		Charges = AbilityState.GetCharges();
		if (Charges >= 0)
			AppendComma(Result, class'X2AccessVocab'.static.Count(Charges,
				class'X2AccessVocab'.default.Charges_One,
				class'X2AccessVocab'.default.Charges_Few,
				class'X2AccessVocab'.default.Charges_Many));
	}

	Template = AbilityState.GetMyTemplate();
	BattleData = XComGameState_BattleData(
		`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_BattleData'));
	if (Template != none && BattleData != none && BattleData.IsAbilityObjectiveHighlighted(Template))
		AppendComma(Result, class'X2AccessVocab'.default.AbilityObjective);

	if (Action.AvailableCode != 'AA_Success')
		AppendPart(Result, class'X2AbilityTemplateManager'.static
			.GetDisplayStringForAvailabilityCode(Action.AvailableCode));

	return Result;
}

// "Sectoid, 1 of 3, hit 73 percent, crit 20 percent, damage 4 to 6, 5 of 8 health, armour 1".
// Free aim has no unit to name and no breakdown to give, so it says so rather than reading
// as a shot with a missing target. bVerbose adds what Delete is for: the blast area's size when
// the target carries a blast at all.
private function string DescribeTarget(UITacticalHUD HUD, AvailableAction Action,
	XComGameState_Ability AbilityState, StateObjectReference TargetRef, int TargetIndex,
	bool bVerbose)
{
	local XComGameState_Unit TargetUnit;
	local ShotBreakdown Breakdown;
	local WeaponDamageValue MinDamage, MaxDamage;
	local int AllowsShield;
	local string Result;

	if (Action.bFreeAim)
		return class'X2AccessVocab'.default.FreeAimNoTarget;

	if (TargetRef.ObjectID <= 0)
	{
		// An unusable ability has already said WHY in the game's own localized words ("Нет
		// доступных целей."). Adding an English "No target" after it is the same fact in two
		// languages — and it was the shape the 2026-08-21 run produced most often at the shot HUD.
		if (Action.AvailableCode != 'AA_Success')
			return "";
		return class'X2AccessVocab'.default.NoTarget;
	}

	// ⛔ A SELF-TARGETED ability has no target worth naming. Overwatch, Reload and Hunker Down
	// all resolve to the acting soldier, so this read out the player's own soldier as if they
	// were a target — "Наблюдение, 2 of 5. Shreya Bose, in full cover, 6 of 6 health" — which
	// drops an English name into an otherwise localized line, restates who is selected, and
	// describes cover the game draws no reticle over. The ability name is the whole readout.
	if (TargetRef.ObjectID == AbilityState.OwnerStateObject.ObjectID)
		return "";

	TargetUnit = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(TargetRef.ObjectID));
	if (TargetUnit != none)
		Result = TargetUnit.GetName(eNameType_Full);
	else
		Result = DescribeNonUnitTarget(TargetRef);

	// WHERE IN THE CYCLE this target sits, worded like the ability icon's own index one clause
	// earlier. TargetIndex is X2TargetingMethod.GetTargetIndex(), i.e. the index Tab and
	// Shift+Tab move (X2TargetingMethod_OverTheShoulder.NextTarget is DirectSetTarget(LastTarget
	// + 1)), and the list is the selected action's own AvailableTargets — the same pair
	// GetTargetedObjectID() reads. So the count is not a survey of the map but the length of the
	// ring the player is actually walking, which is the question "is there another one?" that a
	// sighted player answers off the reticles and the enemy strip at a glance. Spoken even at 1
	// of 1: knowing there is nothing else to cycle to is the same fact, and it is the press the
	// player is spared.
	AppendComma(Result, class'X2AccessVocab'.static.Fill2(
		class'X2AccessVocab'.default.PositionOf,
		TargetIndex + 1, Action.AvailableTargets.Length));

	if (GetBreakdown(AbilityState, TargetRef, Breakdown))
	{
		// The game's OWN localized captions rather than English words of ours — the exact words a
		// sighted player reads beside the two percentages.
		//
		// ⛔ READ THEM OFF UITacticalHUD_ShotHUD, NOT UITacticalHUD_InfoPanel. Both classes
		// declare m_sShotChanceLabel and m_sCritChanceLabel, and the two panels use them
		// identically (InfoPanel:176-177, ShotHUD:290-291) — but XComGame.<loc> ships the values
		// under [UITacticalHUD_ShotHUD] ONLY, so the InfoPanel's copies are EMPTY at runtime.
		// Measured live 2026-08-21 with /props?cdo=: every localized string on
		// UITacticalHUD_InfoPanel read "", while ShotHUD's read "ХИТ" / "КРИТ" / "УРОН:".
		// Reading the empty pair cost more than a missing caption: with no label to tell them
		// apart, a sniper shot whose hit and crit both sat at 60 spoke as
		// "Сектоид,  60 percent,  60 percent" — the same number twice, meaning two different
		// things, with the give-away double space where the caption should have been.
		AppendComma(Result, class'UITacticalHUD_ShotHUD'.default.m_sShotChanceLabel
			@ class'X2AccessVocab'.static.Fill1(class'X2AccessVocab'.default.PercentValue,
				HitChanceOf(Breakdown)));
		if (Breakdown.ResultTable[eHit_Crit] > 0)
		{
			// SpecialCritLabel renames the crit row for abilities where "crit" is the wrong word
			// (a Reaper's Executioner, for one); the panel prefers it whenever it is set, so we do.
			AppendComma(Result, (Breakdown.SpecialCritLabel != "" ? Breakdown.SpecialCritLabel
				: class'UITacticalHUD_ShotHUD'.default.m_sCritChanceLabel)
				@ class'X2AccessVocab'.static.Fill1(class'X2AccessVocab'.default.PercentValue,
					Breakdown.ResultTable[eHit_Crit]));
		}
	}

	AbilityState.GetDamagePreview(TargetRef, MinDamage, MaxDamage, AllowsShield);
	if (MaxDamage.Damage > 0)
	{
		if (MinDamage.Damage == MaxDamage.Damage)
			AppendComma(Result, class'UITacticalHUD_ShotHUD'.default.m_sDamageRangeLabel
				@ string(MinDamage.Damage));
		else
			AppendComma(Result, class'UITacticalHUD_ShotHUD'.default.m_sDamageRangeLabel
				@ class'X2AccessVocab'.static.Fill2(class'X2AccessVocab'.default.NumberRange,
					MinDamage.Damage, MaxDamage.Damage));
	}

	AppendComma(Result, DescribeTargetCover(TargetUnit, AbilityState.OwnerStateObject));
	AppendComma(Result, DescribeTargetHealth(TargetUnit));
	AppendPart(Result, DescribeTargetBlast(HUD, AbilityState, TargetRef, bVerbose));
	return Result;
}

// ---------------------------------------------------------------------------
// The blast a TARGETED thing carries
// ---------------------------------------------------------------------------
//
// Two target-list abilities put a blast on screen without being free-aimed, and the game draws
// both the same way — the area's tiles on the ground and a red or green mark on every unit
// standing in them — so a sighted player reads the blast off the reticle:
//   - a standard shot whose target is an EXPLOSIVE OBJECT (a car, a gas tank). When the reticle
//     lands on an XComDestructibleActor, X2TargetingMethod_OverTheShoulder.DirectSetTarget
//     (:200) asks the actor for its own GetRadialDamageTiles — the box its RadialDamage
//     destruction event will hurt — and marks and draws them when there is more than one;
//   - the Reaper's Remote Start, whose X2TargetingMethod_RemoteStart.DirectSetTarget (:104)
//     asks the ability's own target style (GetValidTilesForActor, the multiplied radius) for
//     the tiles and marks and draws them likewise.
// A unit carrying a Homing Mine is the third case in the same OverTheShoulder branch
// (GetEffectAOETiles, the mine's X2Effect_HomingMine.GetAOETiles), and rides along.
//
// The tiles are re-asked of the SAME functions the method calls, rather than read off its
// MarkedTargets cache: this class has never measured what the native MarkTargetedActors keeps
// there, and the calls are public, cheap and answer synchronously. Who stands in them is
// XComWorldData.GetUnitsOnTile per tile — the unit half of the native GetTargetedActorsInTiles
// the method marks with. Objects in the area are NOT listed: the native marks with
// bUnitsOnly=false, but which objects it chooses is unmeasured, and a list of "Object, Object"
// over every wall fragment in the box would say less than nothing.
//
// The clause is silent, not "Blast: nothing", for any target that carries no blast at all —
// that is nearly every shot, and the line is built to be short. Once there IS an area drawn,
// an empty one is spoken, because "the area is clear" is the fact the marks answer.
private function string DescribeTargetBlast(UITacticalHUD HUD, XComGameState_Ability AbilityState,
	StateObjectReference TargetRef, bool bVerbose)
{
	local array<TTile> Tiles;
	local array<StateObjectReference> Victims;
	local XComGameState_Unit Shooter;
	local string Result, Contents, Warning;

	if (!TargetBlastTiles(HUD, AbilityState, TargetRef, Tiles))
		return "";

	Shooter = XComGameState_Unit(
		`XCOMHISTORY.GetGameStateForObjectID(AbilityState.OwnerStateObject.ObjectID));
	if (Shooter == none)
		return "";

	// The primary target is already the subject of this line; a unit wearing the mine would
	// otherwise be named twice.
	Victims = UnitsInTiles(Tiles, TargetRef.ObjectID);
	Contents = DescribeVictims(Victims, Shooter, Warning);
	if (Contents == "")
		Result = class'X2AccessVocab'.default.BlastEmpty;
	else
		Result = class'X2AccessVocab'.default.BlastLabel @ Contents;
	AppendPart(Result, Warning);

	if (bVerbose)
		AppendPart(Result, class'X2AccessVocab'.default.BlastArea @ BlastExtentWords(Tiles));

	return Result;
}

// The tiles the targeting method drew for this target, from the functions it drew them with.
// False when the method is not one that draws a blast for a listed target, or when it would
// have drawn nothing (OverTheShoulder's own threshold is more than one tile).
private function bool TargetBlastTiles(UITacticalHUD HUD, XComGameState_Ability AbilityState,
	StateObjectReference TargetRef, out array<TTile> Tiles)
{
	local X2TargetingMethod Method;
	local X2AbilityTemplate Template;
	local X2AbilityTarget_RemoteStart RemoteStyle;
	local Actor TargetActor;
	local XComDestructibleActor Destructible;
	local XComGameState_Unit TargetUnit, SourceUnit;
	local XComGameState_Effect EffectState;
	local StateObjectReference EffectRef;
	local XComGameStateHistory History;

	Tiles.Length = 0;
	if (HUD == none || HUD.m_kAbilityHUD == none || TargetRef.ObjectID <= 0)
		return false;
	Method = HUD.m_kAbilityHUD.GetTargetingMethod();
	if (Method == none)
		return false;

	History = `XCOMHISTORY;
	TargetActor = History.GetVisualizer(TargetRef.ObjectID);
	if (TargetActor == none)
		return false;

	if (Method.IsA('X2TargetingMethod_RemoteStart'))
	{
		Template = AbilityState.GetMyTemplate();
		if (Template != none)
			RemoteStyle = X2AbilityTarget_RemoteStart(Template.AbilityTargetStyle);
		if (RemoteStyle == none)
			return false;
		RemoteStyle.GetValidTilesForActor(AbilityState, TargetActor, Tiles);
		return Tiles.Length > 0;
	}

	if (!Method.IsA('X2TargetingMethod_OverTheShoulder'))
		return false;

	Destructible = XComDestructibleActor(TargetActor);
	if (Destructible != none)
	{
		Destructible.GetRadialDamageTiles(Tiles);
	}
	else
	{
		TargetUnit = XComGameState_Unit(History.GetGameStateForObjectID(TargetRef.ObjectID));
		if (TargetUnit != none)
		{
			foreach TargetUnit.AffectedByEffects(EffectRef)
			{
				EffectState = XComGameState_Effect(History.GetGameStateForObjectID(EffectRef.ObjectID));
				if (EffectState == none)
					continue;
				SourceUnit = XComGameState_Unit(History.GetGameStateForObjectID(
					EffectState.ApplyEffectParameters.SourceStateObjectRef.ObjectID));
				if (SourceUnit != none)
					EffectState.GetX2Effect().GetAOETiles(SourceUnit, TargetUnit, Tiles);
			}
		}
	}
	return Tiles.Length > 1;
}

// Every unit standing on any of the tiles, once each — a large unit covers several tiles and
// GetUnitsOnTile answers per tile — minus the one whose ID is passed.
private function array<StateObjectReference> UnitsInTiles(const out array<TTile> Tiles,
	int ExcludeID)
{
	local XComWorldData WorldData;
	local array<StateObjectReference> OnTile, Result;
	local int i, j;

	WorldData = `XWORLD;
	if (WorldData == none)
		return Result;

	for (i = 0; i < Tiles.Length; i++)
	{
		OnTile = WorldData.GetUnitsOnTile(Tiles[i]);
		for (j = 0; j < OnTile.Length; j++)
		{
			if (OnTile[j].ObjectID <= 0 || OnTile[j].ObjectID == ExcludeID)
				continue;
			if (Result.Find('ObjectID', OnTile[j].ObjectID) != INDEX_NONE)
				continue;
			Result.AddItem(OnTile[j]);
		}
	}
	return Result;
}

// "5 by 5 tiles" — the drawn area's footprint, which is what a sighted player sees on the
// ground. Extents rather than a radius because the tiles are the game's, and only the
// destructible box is guaranteed to be a square.
private function string BlastExtentWords(const out array<TTile> Tiles)
{
	local int i, MinX, MaxX, MinY, MaxY;

	if (Tiles.Length == 0)
		return "";
	MinX = Tiles[0].X;
	MaxX = Tiles[0].X;
	MinY = Tiles[0].Y;
	MaxY = Tiles[0].Y;
	for (i = 1; i < Tiles.Length; i++)
	{
		MinX = Min(MinX, Tiles[i].X);
		MaxX = Max(MaxX, Tiles[i].X);
		MinY = Min(MinY, Tiles[i].Y);
		MaxY = Max(MaxY, Tiles[i].Y);
	}
	return class'X2AccessVocab'.static.Fill2(class'X2AccessVocab'.default.BlastSize,
		MaxX - MinX + 1, MaxY - MinY + 1);
}

// The cover state the reticle draws over the target: the shield icon, and whether this shooter
// breaks it. Both come from the game's own answers — GetCoverTypeFromLocation is the cover the
// target is standing in, and IsFlanked(shooter) is the same test the to-hit calc runs, so this
// can never disagree with the flanking bonus the breakdown key reads out.
// Public and static so the object scanner describes an enemy in exactly the words the shot
// readout uses. Two surfaces disagreeing about whether something is flanked would be worse
// than either of them being terse.
static function string DescribeTargetCover(XComGameState_Unit TargetUnit,
	StateObjectReference ShooterRef)
{
	if (TargetUnit == none || !TargetUnit.CanTakeCover())
		return "";

	if (TargetUnit.IsFlanked(ShooterRef))
		return class'X2AccessVocab'.default.Flanked;

	return CoverWords(TargetUnit.GetCoverTypeFromLocation());
}

// The cover words themselves, one table for every surface that speaks a target's cover — the
// shot readout above, the scanner's enemy ring through it, and the tile cursor's target preview,
// which reads the cover a target WOULD have from a tile the soldier is not on yet. Split out so a
// fourth caller cannot spell "half cover" a new way.
static function string CoverWords(ECoverType Cover)
{
	switch (Cover)
	{
	case CT_Standing: return class'XLocalizedData'.default.TargetHighCover;
	case CT_MidLevel: return class'XLocalizedData'.default.TargetLowCover;
	}
	return class'X2AccessVocab'.default.NoCover;
}

// Health, shield and armour — what the target's unit flag shows over its head.
static function string DescribeTargetHealth(XComGameState_Unit TargetUnit)
{
	local int Armor, Shield;
	local string Result;

	if (TargetUnit == none)
		return "";

	Result = class'X2AccessVocab'.static.Fill2(class'X2AccessVocab'.default.TargetHealth,
		int(TargetUnit.GetCurrentStat(eStat_HP)), int(TargetUnit.GetMaxStat(eStat_HP)));

	Shield = int(TargetUnit.GetCurrentStat(eStat_ShieldHP));
	if (Shield > 0)
		AppendComma(Result, class'X2AccessVocab'.static.Fill1(
			class'X2AccessVocab'.default.TargetShield, Shield));

	Armor = int(TargetUnit.GetArmorMitigationForUnitFlag());
	if (Armor > 0)
		AppendComma(Result, class'X2AccessVocab'.static.Fill1(
			class'X2AccessVocab'.default.TargetArmor, Armor));

	return Result;
}

// "3 enemies visible" — how many enemies THIS soldier can see, which is the count of icons on
// the enemy strip beside the reticle (UITacticalHUD_Enemies, whose own header calls itself
// "currently visible aliens to the active soldier").
//
// PER-SOLDIER, not squad-wide, and that is the whole point: the squad's answer is already on the
// map as unit flags, while the strip answers the question the player asks when they switch —
// "can THIS one shoot anything?". The two lists are the ones UITacticalHUD_Enemies builds via
// XComGameState_Unit.GetUISummary_TargetableUnits (:12601, :12606): what the unit sees, plus
// what a sniper reaches through squadsight. GetAllSquadsightEnemiesForUnit already excludes
// what the unit sees directly (X2TacticalVisibilityHelpers.uc:435), so the two never double-count.
//
// ⛔ Filtered to UNITS. The strip's array also carries hackable objects and destructibles
// (GetUISummary_TargetableUnits appends GetHackableObjectsInRangeOfUnit), so counting it raw
// would call a door an enemy. The alive/cosmetic pair is the game's own filter from the same
// function — a cosmetic unit is a Gremlin, and it is on nobody's threat list.
//
// ⛔ ZERO IS SILENT UNLESS ASKED FOR. The automatic line rides every soldier switch, and most
// switches on most missions happen with nothing in sight — so a spoken zero is a clause the
// player hears far more often than the number they wanted, and it lengthens the one line that
// exists to be short. Silence there is not the empty-answer trap it would be on a pressed key:
// the switch line always names the soldier, so the player still hears the readout fire and can
// tell "nothing to report" from "nothing happened". bSpeakZero is what the on-demand block
// passes, because a key that was pressed owes an answer to the question it was pressed about.
//
// Static and public so the object scanner's enemy ring and this count can never disagree about
// what "visible to this soldier" means; the ring itself stays on SQUAD visibility, which is a
// deliberate difference and documented there.
static function string DescribeVisibleEnemies(XComGameState_Unit UnitState,
	optional bool bSpeakZero)
{
	local array<StateObjectReference> Visible, ViaSquadsight;
	local XComGameState_Unit EnemyUnit;
	local X2CharacterTemplate Template;
	local int i, Count;

	if (UnitState == none)
		return "";

	class'X2TacticalVisibilityHelpers'.static.GetAllVisibleEnemyTargetsForUnit(
		UnitState.ObjectID, Visible);
	if (UnitState.HasSquadsight())
	{
		class'X2TacticalVisibilityHelpers'.static.GetAllSquadsightEnemiesForUnit(
			UnitState.ObjectID, ViaSquadsight);
		for (i = 0; i < ViaSquadsight.Length; i++)
			Visible.AddItem(ViaSquadsight[i]);
	}

	for (i = 0; i < Visible.Length; i++)
	{
		EnemyUnit = XComGameState_Unit(
			`XCOMHISTORY.GetGameStateForObjectID(Visible[i].ObjectID));
		if (EnemyUnit == none || !EnemyUnit.IsAlive())
			continue;
		Template = EnemyUnit.GetMyTemplate();
		if (Template != none && Template.bIsCosmetic)
			continue;
		Count++;
	}

	if (Count == 0)
		return bSpeakZero ? class'X2AccessVocab'.default.EmptyEnemies : "";
	return class'X2AccessVocab'.static.Count(Count,
		class'X2AccessVocab'.default.EnemiesVisible_One,
		class'X2AccessVocab'.default.EnemiesVisible_Few,
		class'X2AccessVocab'.default.EnemiesVisible_Many);
}

// The HUD graph's Targets stop: one wire record per head on the enemy strip
// (UITacticalHUD_Enemies, the row of heads at the top right), in the strip's OWN order — it sorts
// by hit chance, best shot leftmost, destructibles last (SortEnemies), and that order is what a
// sighted player reads the best shot off. Called by native once per graph rebuild.
//
// SIGHTED PARITY, per head, each fact from the exact place the strip draws it:
//   name          the head's glyph is the character template's own icon, i.e. the unit TYPE —
//                 GetName(eNameType_Full) is the same template name; objects through
//                 DescribeNonUnitTarget, as the shot HUD names them
//   team          the background colour (GetMyHUDIconColor): yellow = civilian, blue = an ally
//                 (a medkit's targets ride this strip too); red is the default and unspoken
//   hit           the number SetFocusedEnemy shows on the targeted head and on the head under the
//                 mouse — the strip's own GetHitChanceForObjectRef, which picks the selected
//                 ability, else the unit's default attack, and answers -1 when the breakdown is
//                 hidden. Captioned with the shot HUD's own localized label, as DescribeTarget is.
//   cover/health  the flanked marker (SetFlanked: IsFlanked by the ACTIVE soldier) and the unit
//                 flag's cover shield and bars, through the shot readout's own composers
//   sight         the squadsight badge (m_arrSSEnemies)
//   blocked       the greyed icon (SetDisabled: not in m_arrCurrentlyAffectable — visible, but
//                 the selected ability, or with none selected no icon-bearing ability, can reach it)
//   bonus/penalty the two badges (SetBonusAndPenalty), spoken as the effect NAMES the flag's icons
//                 and the Enemy Info screen show, through X2AccessUnitReader's own composer
//   targeted      the highlighted head (CurrentTargetIndex)
//   dist/bearing  the one AID the plan grants this stop — the scanner's own distance and compass
//                 words, so the two surfaces cannot disagree about where a target is
//
// Read off the SCREEN: the strip's arrays are the panel's retained state, and every fact above is
// asked of the same object the panel asked. The empty strip is an empty payload, and the graph
// builds no stop — the strip hides at zero heads too.
function BuildEnemyRecords()
{
	local UITacticalHUD HUD;
	local UITacticalHUD_Enemies Strip;
	local XComGameStateHistory History;
	local XComGameState_Unit ActiveUnit, TargetUnit;
	local XComGameState_BaseObject Target;
	local StateObjectReference ShooterRef;
	local string Recs, Rec, Names;
	local int i, Hit, Dist;

	NativeResult = "";

	HUD = GetTacticalHUD();
	if (HUD == none || HUD.m_kEnemyTargets == none)
		return;
	Strip = HUD.m_kEnemyTargets;
	ActiveUnit = class'X2AccessTileReader'.static.ActiveUnitState();
	if (ActiveUnit == none)
		return;
	ShooterRef = ActiveUnit.GetReference();
	History = `XCOMHISTORY;

	for (i = 0; i < Strip.m_arrTargets.Length; i++)
	{
		Target = History.GetGameStateForObjectID(Strip.m_arrTargets[i].ObjectID);
		if (Target == none)
			continue;
		TargetUnit = XComGameState_Unit(Target);

		Rec = "kind=head";
		class'X2AccessWire'.static.AddField(Rec, "oid", string(Target.ObjectID));
		class'X2AccessWire'.static.AddField(Rec, "index", string(i));

		if (TargetUnit != none)
		{
			class'X2AccessWire'.static.AddField(Rec, "name", TargetUnit.GetName(eNameType_Full));
			if (TargetUnit.IsCivilian())
				class'X2AccessWire'.static.AddField(Rec, "team", "civilian");
			else if (TargetUnit.IsFriendlyToLocalPlayer())
				class'X2AccessWire'.static.AddField(Rec, "team", "ally");
		}
		else
		{
			class'X2AccessWire'.static.AddField(Rec, "name",
				DescribeNonUnitTarget(Strip.m_arrTargets[i]));
		}

		Hit = Strip.GetHitChanceForObjectRef(Strip.m_arrTargets[i]);
		if (Hit >= 0)
		{
			class'X2AccessWire'.static.AddField(Rec, "hit",
				class'UITacticalHUD_ShotHUD'.default.m_sShotChanceLabel @
				class'X2AccessVocab'.static.Fill1(class'X2AccessVocab'.default.PercentValue, Hit));
		}

		class'X2AccessWire'.static.AddField(Rec, "cover", DescribeTargetCover(TargetUnit, ShooterRef));
		class'X2AccessWire'.static.AddField(Rec, "health", DescribeTargetHealth(TargetUnit));

		if (Strip.m_arrSSEnemies.Find('ObjectID', Target.ObjectID) != INDEX_NONE)
			class'X2AccessWire'.static.AddField(Rec, "sight", class'X2AccessVocab'.default.ViaSquadsight);
		if (Strip.m_arrCurrentlyAffectable.Find('ObjectID', Target.ObjectID) == INDEX_NONE)
			class'X2AccessWire'.static.AddField(Rec, "blocked", class'X2AccessVocab'.default.NotTargetable);

		if (TargetUnit != none)
		{
			Names = class'X2AccessUnitReader'.static.EffectNames(TargetUnit, ePerkBuff_Bonus);
			if (Names != "")
				class'X2AccessWire'.static.AddField(Rec, "bonus", class'X2AccessVocab'.default.BonusesLabel @ Names);
			Names = class'X2AccessUnitReader'.static.EffectNames(TargetUnit, ePerkBuff_Penalty);
			if (Names != "")
				class'X2AccessWire'.static.AddField(Rec, "penalty", class'X2AccessVocab'.default.PenaltiesLabel @ Names);
		}

		if (i == Strip.CurrentTargetIndex)
			class'X2AccessWire'.static.AddField(Rec, "targeted", class'X2AccessVocab'.default.Targeted);

		if (TargetUnit != none)
		{
			Dist = TileDistance(ActiveUnit.TileLocation, TargetUnit.TileLocation);
			class'X2AccessWire'.static.AddField(Rec, "dist",
				class'X2AccessVocab'.static.Count(Dist,
					class'X2AccessVocab'.default.Tiles_One,
					class'X2AccessVocab'.default.Tiles_Few,
					class'X2AccessVocab'.default.Tiles_Many));
			class'X2AccessWire'.static.AddField(Rec, "bearing",
				class'X2AccessTileReader'.static.OffsetWords(ActiveUnit.TileLocation,
					TargetUnit.TileLocation));
		}

		class'X2AccessWire'.static.AddRecord(Recs, Rec);
	}

	NativeResult = Recs;
}

// Enter on a head: the strip's own click. FXS_L_MOUSE_UP on an icon is SelectEnemyByIndex —
// DirectTargetObject (the selected ability if one is up, else the unit's default attack, raising
// the shot HUD on that target), the flag extension for the targeted unit, and the select sound —
// behind the one guard OnMouseEvent puts in front of it: no strip selection in the tutorial while
// soldier chatter is off, because the tutorial has its own idea of what gets shot next.
//
// Keyed on the OBJECT, not the icon index: the strip re-sorts on every refresh, so an index the
// graph held a moment ago may name a different head now. A target that has left the strip is a
// spoken refusal. DirectTargetObject's own false (a visualizer blocking activation, no ability
// able to reach the target) is silent in the game, so it is answered here by asking the HUD
// whether targeting actually raised — the poll speaks the ability and target when it did.
function SelectEnemyHead(int ObjectID)
{
	local UITacticalHUD HUD;
	local UITacticalHUD_Enemies Strip;
	local XGBattle Battle;
	local int Index;

	NativeResult = "";

	HUD = GetTacticalHUD();
	if (HUD == none || HUD.m_kEnemyTargets == none)
	{
		NativeResult = class'X2AccessVocab'.default.Unavailable;
		return;
	}
	Strip = HUD.m_kEnemyTargets;

	Battle = `BATTLE;
	if (Battle != none && Battle.m_kDesc != none
		&& Battle.m_kDesc.m_bIsTutorial && Battle.m_kDesc.m_bDisableSoldierChatter)
	{
		NativeResult = class'X2AccessVocab'.default.Unavailable;
		return;
	}

	Index = Strip.m_arrTargets.Find('ObjectID', ObjectID);
	if (Index == INDEX_NONE)
	{
		NativeResult = class'X2AccessVocab'.default.NoLongerVisible;
		return;
	}

	Strip.SelectEnemyByIndex(Index);
	if (!HUD.m_isMenuRaised)
		NativeResult = class'X2AccessVocab'.default.Unavailable;
}

// Targets that are not units: destructible cover, a hackable terminal, a door. Static and public
// so the tile cursor's target preview names them exactly as the shot HUD does.
static function string DescribeNonUnitTarget(StateObjectReference TargetRef)
{
	local XComGameState_InteractiveObject IObj;
	local XComGameState_Destructible Destructible;

	IObj = XComGameState_InteractiveObject(
		`XCOMHISTORY.GetGameStateForObjectID(TargetRef.ObjectID));
	if (IObj != none)
	{
		// The object scanner's oracle, not a category word of ours: it prefers the level
		// designer's own FriendlyDisplayName and falls back to the LOCALIZED name of the
		// object's interact ability. "Взлом, 3 of 7. Hackable object" was the shot HUD naming
		// in English something the scanner names in the player's language two keys away.
		return class'X2AccessObjectScanner'.static.InteractiveLabel(IObj);
	}

	Destructible = XComGameState_Destructible(
		`XCOMHISTORY.GetGameStateForObjectID(TargetRef.ObjectID));
	if (Destructible != none)
		return class'X2AccessVocab'.default.InteractiveObject;

	return class'X2AccessVocab'.default.TargetFallback;
}

// The modifier rows of one column of the shot wings, in the game's own sign convention
// (a positive value reads "plus 15", a negative one "minus 15").
private function string DescribeModifiers(ShotBreakdown Breakdown, int HitType)
{
	local string Result, Value;
	local int i;

	for (i = 0; i < Breakdown.Modifiers.Length; i++)
	{
		if (Breakdown.Modifiers[i].ModType != HitType)
			continue;
		if (Breakdown.Modifiers[i].Value >= 0)
			Value = class'X2AccessVocab'.static.Fill1(class'X2AccessVocab'.default.PlusValue,
				Breakdown.Modifiers[i].Value);
		else
			Value = class'X2AccessVocab'.static.Fill1(class'X2AccessVocab'.default.MinusValue,
				-Breakdown.Modifiers[i].Value);
		AppendPart(Result, Breakdown.Modifiers[i].Reason @ Value @
			class'X2AccessVocab'.default.PercentWord);
	}
	return Result;
}


// ===========================================================================
// Free aim
// ===========================================================================
//
// An ability whose target is a POINT rather than an entry in a target list:
// AvailableAction.bFreeAim is set, AvailableTargets is EMPTY, and the aim point is wherever the
// game's own XCom3DCursor happens to be. Grenades (and everything deriving from their method —
// teleport, mimic beacon, gremlin AOE), cones, lines, the blaster launcher and the evac zone all
// work this way.
//
// Vanilla gives a mouse+keyboard player NO way to move that point at all:
// UITacticalHUD_AbilityContainer.AbilityRequiresTargetingActivation opens by returning false
// unless a controller is active (:155), so the whole D-pad aiming branch is dead code there, and
// SetSelectionOnInputPress (:405, called on every PRESS) sends a bare arrow to
// CycleAbilitySelection instead — which ABANDONS the throw and picks another ability.
//
// So this half drives the cursor itself, exactly as X2AccessTileReader drives it in world mode,
// and reads the consequences back out of the ability. Two rules earned in recon
// (docs/free-aim-recon.md) shape everything below, and both are easy to get wrong:
//
//  * DO NOT zero the cursor's chain distances. X2AccessTileReader.SetCursorTile does exactly
//    that, and says in a comment it is safe because our keys stand down while the shot HUD is
//    raised — free aim is precisely the case that makes that sentence false.
//    X2TargetingMethod_Grenade.Init (:43) sets m_fMaxChainedDistance from
//    GetAbilityCursorRangeMeters (live: 960 units = 15 m = 10 tiles), and that clamp is the ONLY
//    thing holding the player inside throwing range. PushAim below leaves it alone and treats it
//    as the range check.
//
//  * DO NOT trust the pushed tile as the answer. For a thrown object the blast centre is
//    GrenadePath.GetEndPosition() — where it comes to REST — not the cursor, so a bounced
//    grenade lands somewhere else entirely. That resolution is a frame behind our push (the
//    method only recomputes inside its own Update), which is why a keypress speaks what it can
//    know synchronously and PollFreeAim reports the divergence when it appears.

// ---------------------------------------------------------------------------
// Entry points (called from native)
// ---------------------------------------------------------------------------

// Step the aim point one tile toward a compass cardinal. Cardinals and their world-axis deltas
// come from X2AccessTileReader, so the aim point and the tile cursor share ONE calibration of
// north and can never drift into disagreeing about which way an arrow points.
function FreeAimStep(int Cardinal)
{
	local UITacticalHUD HUD;
	local X2TargetingMethod Method;
	local XComGameState_Ability AbilityState;
	local XComGameState_Unit Shooter;
	local XCom3DCursor Cursor;
	local XComWorldData WorldData;
	local TTile Target;
	local int DX, DY;

	NativeResult = "";
	if (!GetAimContext(HUD, Method, AbilityState, Shooter, Cursor, WorldData))
		return;
	if (EnsureAimPlanted(AbilityState, Shooter, Method, Cursor, WorldData))
		return;

	if (!class'X2AccessTileReader'.static.CardinalDelta(Cardinal, DX, DY))
	{
		// Unreachable for the four cardinals, but a press must never be silent.
		NativeResult = "Bad direction" @ string(Cardinal);
		return;
	}
	Target.X = Clamp(AimX + DX, 0, WorldData.NumX - 1);
	Target.Y = Clamp(AimY + DY, 0, WorldData.NumY - 1);
	Target.Z = AimZ;
	if (Target.X == AimX && Target.Y == AimY)
	{
		NativeResult = class'X2AccessVocab'.default.MapEdge;
		return;
	}

	// Follow the terrain, exactly as the tile cursor does: aiming up a slope or off a roof edge
	// should track the ground rather than fly level off it.
	ResolveAimFloor(WorldData, Target);

	if (!PushAim(Cursor, WorldData, Target))
	{
		NativeResult = class'X2AccessVocab'.default.ReachOutOfRange;
		return;
	}
	NativeResult = DescribeAim(AbilityState, Shooter, Method, AimTile(), false);
}

// Move the aim point one floor level up (+1) or down (-1) — the spoken form of the F / C keys
// the game keeps live during targeting, and the reason a grenade can be dropped through a hole
// or lobbed onto a roof.
function FreeAimFloor(int Delta)
{
	local UITacticalHUD HUD;
	local X2TargetingMethod Method;
	local XComGameState_Ability AbilityState;
	local XComGameState_Unit Shooter;
	local XCom3DCursor Cursor;
	local XComWorldData WorldData;
	local TTile Probe;
	local int Z;

	NativeResult = "";
	if (!GetAimContext(HUD, Method, AbilityState, Shooter, Cursor, WorldData))
		return;
	if (EnsureAimPlanted(AbilityState, Shooter, Method, Cursor, WorldData))
		return;

	Probe.X = AimX;
	Probe.Y = AimY;
	for (Z = AimZ + Delta; Z >= 0 && Z < WorldData.NumZ; Z += Delta)
	{
		Probe.Z = Z;
		if (!WorldData.IsFloorTile(Probe))
			continue;
		if (!PushAim(Cursor, WorldData, Probe))
		{
			// A floor exists but the chain clamp will not reach it — height counts toward the
			// ability's cursor range too.
			NativeResult = class'X2AccessVocab'.default.ReachOutOfRange;
			return;
		}
		if (Delta > 0)
			NativeResult = class'X2AccessVocab'.default.AimUp @ DescribeAim(AbilityState, Shooter, Method, AimTile(), false);
		else
			NativeResult = class'X2AccessVocab'.default.AimDown @ DescribeAim(AbilityState, Shooter, Method, AimTile(), false);
		return;
	}
	if (Delta > 0)
		NativeResult = class'X2AccessVocab'.default.NoFloorAbove;
	else
		NativeResult = class'X2AccessVocab'.default.NoFloorBelow;
}

// Aim at the object scanner's current selection — the scanner's action verb while free-aiming,
// where world mode plants the walk cursor instead. This is the composition the whole feature is
// built around: the scanner already announces "throw a grenade here" for the tutorial's required
// action, and already knows where every visible enemy is, so "next enemy, aim, confirm" is three
// keys.
//
// The scanner owns the selection and this class owns the cursor, so the hand-off is a
// cross-class default READ — the one direction UnrealScript allows, and the same hand-off
// X2AccessTileReader.CursorGoToMarker makes for world mode.
function FreeAimGoToMarker()
{
	local UITacticalHUD HUD;
	local X2TargetingMethod Method;
	local XComGameState_Ability AbilityState;
	local XComGameState_Unit Shooter;
	local XCom3DCursor Cursor;
	local XComWorldData WorldData;
	local TTile Target;

	NativeResult = "";
	if (!GetAimContext(HUD, Method, AbilityState, Shooter, Cursor, WorldData))
		return;

	if (!class'X2AccessObjectScanner'.default.bSelValid)
	{
		NativeResult = class'X2AccessVocab'.default.NoMarkerSelected;
		return;
	}
	Target.X = Clamp(class'X2AccessObjectScanner'.default.SelX, 0, WorldData.NumX - 1);
	Target.Y = Clamp(class'X2AccessObjectScanner'.default.SelY, 0, WorldData.NumY - 1);
	Target.Z = Clamp(class'X2AccessObjectScanner'.default.SelZ, 0, WorldData.NumZ - 1);

	// A jump from a stale activation must not restore that activation's tile, so drop the plant
	// first and let PushAim treat this as a cold aim.
	if (AimAbilityID != AbilityState.ObjectID)
		ResetAim();

	if (!PushAim(Cursor, WorldData, Target))
	{
		NativeResult = class'X2AccessVocab'.default.ReachOutOfRange;
		return;
	}

	// Adopted only on SUCCESS, so a refused jump leaves the previous aim point — and its
	// activation — exactly as the player last heard it. A later arrow step then continues from
	// here rather than snapping back to the shooter's own tile.
	bAimPlanted = true;
	AimAbilityID = AbilityState.ObjectID;
	NativeResult = class'X2AccessVocab'.default.Aiming @ DescribeAim(AbilityState, Shooter, Method, AimTile(), false);
}

// ---------------------------------------------------------------------------
// Poll and resolution
// ---------------------------------------------------------------------------

// The free-aim half of PollTargeting. Announces a newly raised cursor ability, then stays quiet
// unless the ability would not actually land where the player aimed it.
private function string PollFreeAim(UITacticalHUD HUD, AvailableAction Action,
	XComGameState_Ability AbilityState)
{
	local X2TargetingMethod Method;
	local TTile Landing, Aim;
	local string Result;

	if (Action.AbilityObjectRef.ObjectID != LastAbilityID)
	{
		// A new cursor ability. The aim point is deliberately NOT planted here: planting engages
		// the native mouse gate, and until the player actually presses an aim key the mouse is
		// still the right thing to be driving the cursor. The first arrow plants on the
		// shooter's own tile and reads it out.
		LastAbilityID = Action.AbilityObjectRef.ObjectID;
		LastTargetID = 0;
		ResetAim();
		Result = DescribeAbility(HUD, Action, AbilityState);
		AppendPart(Result, class'X2AccessVocab'.default.ArrowsToAim);
		return Result;
	}

	if (!bAimPlanted || AimAbilityID != AbilityState.ObjectID)
		return "";

	Method = CursorMethod(HUD);
	if (!ResolvedLandingTile(Method, Landing))
		return "";

	// Compare on the horizontal only. A thrown object settles onto the floor beneath the aim
	// point (GetSplashRadiusCenter walks SnapTile.Z down until it finds one), so a Z difference
	// is routine and says nothing; a horizontal difference means the throw did not get there.
	if (Landing.X == AimX && Landing.Y == AimY)
	{
		bLandWarned = false;
		bLandPending = false;
		return "";
	}

	// Already said, and nothing has changed since.
	if (bLandWarned && Landing.X == LastLandX && Landing.Y == LastLandY)
		return "";

	// First sighting of this divergence: hold it and wait for the next poll to agree. See the
	// bLandPending declaration for why one reading is not evidence.
	if (!bLandPending || Landing.X != PendingLandX || Landing.Y != PendingLandY)
	{
		bLandPending = true;
		PendingLandX = Landing.X;
		PendingLandY = Landing.Y;
		return "";
	}

	bLandPending = false;
	bLandWarned = true;
	LastLandX = Landing.X;
	LastLandY = Landing.Y;
	Aim = AimTile();
	return class'X2AccessVocab'.static.FillS(class'X2AccessVocab'.default.BlockedLands,
		class'X2AccessTileReader'.static.OffsetWords(Aim, Landing));
}

// The live targeting method, but only when it is one that resolves its aim point from the 3D
// cursor. UITacticalHUD_AbilityContainer.IsTargetingMethodActivated (:182) names four families;
// IsA carries the subclasses along, which is what brings Teleport, MimicBeacon and GremlinAOE in
// through Grenade. EvacZone is added because it drives the same cursor even though the game's
// own list omits it.
//
// Deliberately NOT keyed on Action.bFreeAim alone: X2TargetingMethod_Fuse is free-aiming and
// derives from TopDown, which never touches the cursor, so pushing for it would move nothing
// while telling the player it had.
private function X2TargetingMethod CursorMethod(UITacticalHUD HUD)
{
	local X2TargetingMethod Method;

	if (HUD == none || HUD.m_kAbilityHUD == none)
		return none;
	Method = HUD.m_kAbilityHUD.GetTargetingMethod();
	if (Method == none)
		return none;
	if (Method.IsA('X2TargetingMethod_Grenade')
		|| Method.IsA('X2TargetingMethod_Cone')
		|| Method.IsA('X2TargetingMethod_Line')
		|| Method.IsA('X2TargetingMethod_BlasterLauncher')
		|| Method.IsA('X2TargetingMethod_EvacZone'))
		return Method;
	return none;
}

// Everything an aim key needs, or the reason it cannot act — spoken, because a pressed key must
// never be silent.
private function bool GetAimContext(out UITacticalHUD HUD, out X2TargetingMethod Method,
	out XComGameState_Ability AbilityState, out XComGameState_Unit Shooter,
	out XCom3DCursor Cursor, out XComWorldData WorldData)
{
	local XComTacticalController TC;
	local AvailableAction Action;
	local StateObjectReference TargetRef;
	local int TargetIndex;

	HUD = GetTacticalHUD();
	if (HUD == none || !HUD.m_isMenuRaised)
	{
		NativeResult = class'X2AccessVocab'.default.NotTargeting;
		return false;
	}
	Method = CursorMethod(HUD);
	if (Method == none)
	{
		NativeResult = class'X2AccessVocab'.default.NotFreeAiming;
		return false;
	}
	// TargetIndex is discarded here on purpose: free aim has no target list to be an index INTO
	// (bFreeAim leaves AvailableTargets empty), so the aim point is the whole subject.
	if (!ResolveShot(HUD, Action, AbilityState, TargetRef, TargetIndex))
	{
		NativeResult = class'X2AccessVocab'.default.NoAbilitySelected;
		return false;
	}

	// The SHOOTER, not the "active unit": they are the same today, but the ability owns the
	// authoritative answer and it is the unit the cursor's range chain is attached to.
	Shooter = XComGameState_Unit(
		`XCOMHISTORY.GetGameStateForObjectID(AbilityState.OwnerStateObject.ObjectID));
	TC = class'X2AccessTileReader'.static.TacCtrl();
	if (Shooter == none || TC == none)
	{
		NativeResult = class'X2AccessVocab'.default.NoUnitSelected;
		return false;
	}
	Cursor = TC.GetCursor();
	WorldData = `XWORLD;
	if (Cursor == none || WorldData == none)
	{
		NativeResult = class'X2AccessVocab'.default.NoCursor;
		return false;
	}
	return true;
}

// Plant the aim point on the SHOOTER'S OWN TILE on the first aim key of each activation, read it
// out, and consume the press (RT/tile-cursor parity: a cold key must never act blind). Returns
// true when this call did the planting.
//
// Player decision 2026-08-20: a predictable origin beats a convenient one, because every later
// step is then a known offset from a tile the player already knows. The obvious objection — that
// a reflexive confirm blasts your own squad — answers itself here rather than needing a policy:
// standing in your own blast makes the readout's own friendly-fire clause fire immediately,
// naming you, and the game's modal is still behind that.
private function bool EnsureAimPlanted(XComGameState_Ability AbilityState,
	XComGameState_Unit Shooter, X2TargetingMethod Method, XCom3DCursor Cursor,
	XComWorldData WorldData)
{
	local TTile Home;
	local vector Pos;

	if (bAimPlanted && AimAbilityID == AbilityState.ObjectID)
		return false;

	ResetAim();
	Home = Shooter.TileLocation;
	bAimPlanted = true;
	AimAbilityID = AbilityState.ObjectID;
	AimX = Home.X;
	AimY = Home.Y;
	AimZ = Home.Z;

	// Pushed directly rather than through PushAim: the shooter's own tile is by definition inside
	// the shooter's own range, so there is no clamp to test and no previous aim to restore.
	Pos = WorldData.GetPositionFromTileCoordinates(Home);
	Cursor.CursorSetLocation(Pos, false, false);

	NativeResult = class'X2AccessVocab'.default.Aiming @ DescribeAim(AbilityState, Shooter, Method, Home, false);
	return true;
}

// Push the cursor onto a tile and adopt it, or refuse and leave everything as it was. False
// means the engine's own chain clamp moved the push somewhere else — i.e. the tile is past the
// ability's cursor range.
//
// The chain distances are deliberately LEFT ALONE (see this section's header): here they ARE the
// range limit. bSnapToValidPosition stays FALSE for the same reason it does in world mode —
// GetClosestValidCursorPosition slid a floor-up push two tiles sideways (measured 2026-08-20),
// and the reader has to speak the tile the engine is actually aiming at.
private function bool PushAim(XCom3DCursor Cursor, XComWorldData WorldData, TTile Tile)
{
	local TTile Previous;
	local vector Pos, Feet, Drift;

	Pos = WorldData.GetPositionFromTileCoordinates(Tile);
	Cursor.CursorSetLocation(Pos, false, false);

	// Only the chain can have moved it, so only test when a chain is actually armed. Outside
	// targeting — and for any cursor ability with unrestricted range — m_fMaxChainedDistance is
	// 0 or -1 and ProcessChainedDistance is a pass-through, so accepting unconditionally there
	// is both correct and the safe failure mode: a misjudged range test would refuse every step
	// and strand the player, where accepting merely trusts the engine.
	if (Cursor.ChainedPawn != none && Cursor.m_fMaxChainedDistance > 0)
	{
		// Read the clamp result back — NOT the tile we asked for. GetCursorFeetLocation is the
		// exact inverse of the collision-height offset CursorSetLocation just added, so with
		// snapping off this is precisely ProcessChainedDistance's output. A single synchronous
		// read of what we just wrote, not the position round-trip the tile cursor warns against:
		// nothing else has moved the cursor in between, and the value is compared and discarded
		// rather than stepped from.
		//
		// Compared as a DISTANCE with half-a-tile of slack rather than as tile equality: the
		// clamp lands on the surface of a sphere and has no reason to respect the tile grid, so
		// the question is "did it move at all", and the tolerance absorbs float noise that tile
		// rounding would turn into a spurious refusal at a tile boundary. Horizontal only —
		// height is the floor keys' business.
		Feet = Cursor.GetCursorFeetLocation();
		Drift = Feet - Pos;
		Drift.Z = 0;
		if (VSize(Drift) > class'XComWorldData'.const.WORLD_StepSize * 0.5)
		{
			// Put the cursor back where the player last heard it, so our state and the game's
			// aim can never disagree about where this ability is pointed. Nothing to restore if
			// this activation was never planted — the next aim key plants on the shooter.
			if (bAimPlanted)
			{
				Previous = AimTile();
				Pos = WorldData.GetPositionFromTileCoordinates(Previous);
				Cursor.CursorSetLocation(Pos, false, false);
			}
			return false;
		}
	}

	AimX = Tile.X;
	AimY = Tile.Y;
	AimZ = Tile.Z;
	bLandWarned = false;
	bLandPending = false;
	return true;
}

// Where the ability would ACTUALLY land, asked of the targeting method itself — for a grenade
// GetTargetLocations returns the end of the thrown arc, floor-snapped, which is the same value
// ConfirmAbility submits. Lags a push by one frame, so only the poll reads it.
private function bool ResolvedLandingTile(X2TargetingMethod Method, out TTile Landing)
{
	local array<Vector> Locs;
	local vector Loc;

	if (Method == none)
		return false;
	Method.GetTargetLocations(Locs);
	if (Locs.Length == 0)
		return false;
	Loc = Locs[0];
	Landing = `XWORLD.GetTileCoordinatesFromPosition(Loc);
	return true;
}

// ---------------------------------------------------------------------------
// Aim description
// ---------------------------------------------------------------------------

// The spoken aim line. Short by default — this fires on every arrow press — with bearing,
// distance and who is in the blast, plus the friendly-fire clause only when it is true.
// Verbose adds what Delete is for: radius, damage band and whether the throw ends the turn.
private function string DescribeAim(XComGameState_Ability AbilityState,
	XComGameState_Unit Shooter, X2TargetingMethod Method, TTile Tile, bool bVerbose)
{
	local array<Vector> Locs;
	local name Code;
	local string Result, Bearing, Contents, Warning;
	local float Radius;
	local WeaponDamageValue MinDamage, MaxDamage;
	local StateObjectReference EmptyRef;
	local int AllowsShield;
	local TTile Home;
	local vector TilePos;
	local X2AbilityTemplate Template;

	Home = Shooter.TileLocation;
	Bearing = class'X2AccessTileReader'.static.OffsetWords(Home, Tile);
	if (Bearing == "")
		Result = class'X2AccessVocab'.default.OnOwnTile;
	else
		Result = Bearing $ "," @ class'X2AccessVocab'.static.Count(TileDistance(Home, Tile),
			class'X2AccessVocab'.default.Tiles_One,
			class'X2AccessVocab'.default.Tiles_Few,
			class'X2AccessVocab'.default.Tiles_Many);

	// Only abilities that actually hit an AREA get a blast clause. An evac zone or a teleport is
	// cursor-targeted too and hits nobody, and "Blast: nothing" about one of those would read as
	// a broken readout rather than as useful news. Note this cannot be keyed on GetAbilityRadius
	// instead: a line ability has a multi-target style but no radius, and would lose its list.
	Template = AbilityState.GetMyTemplate();
	if (Template != none && Template.AbilityMultiTargetStyle != none)
	{
		Contents = BlastContents(AbilityState, Shooter, Tile, Warning);
		if (Contents == "")
			AppendPart(Result, class'X2AccessVocab'.default.BlastEmpty);
		else
			AppendPart(Result, class'X2AccessVocab'.default.BlastLabel @ Contents);
		AppendPart(Result, Warning);
	}

	// The game's own legality answer for this point, in the game's own localized wording — the
	// squadsight restriction some cursor abilities carry lives here and nowhere else.
	TilePos = `XWORLD.GetPositionFromTileCoordinates(Tile);
	if (Method != none)
	{
		Locs.AddItem(TilePos);
		Code = Method.ValidateTargetLocations(Locs);
		if (Code != 'AA_Success')
			AppendPart(Result, class'X2AbilityTemplateManager'.static
				.GetDisplayStringForAvailabilityCode(Code));
	}

	if (bVerbose)
	{
		Radius = AbilityState.GetAbilityRadius();
		if (Radius > 0)
			AppendPart(Result, class'X2AccessVocab'.static.Count(
				int(Radius / class'XComWorldData'.const.WORLD_StepSize),
				class'X2AccessVocab'.default.BlastRadius_One,
				class'X2AccessVocab'.default.BlastRadius_Few,
				class'X2AccessVocab'.default.BlastRadius_Many));

		// The empty target reference is deliberate and is what UITacticalHUD_ShotHUD does for an
		// ability with no primary target: ONE damage band for the ability, which is all a sighted
		// player gets while free-aiming — there is no per-victim number on screen to match.
		AbilityState.GetDamagePreview(EmptyRef, MinDamage, MaxDamage, AllowsShield);
		if (MaxDamage.Damage > 0)
		{
			// Capitalised: AppendPart joins with ". ", so this starts a sentence.
			if (MinDamage.Damage == MaxDamage.Damage)
				AppendPart(Result, class'UITacticalHUD_ShotHUD'.default.m_sDamageRangeLabel
					@ string(MinDamage.Damage));
			else
				AppendPart(Result, class'UITacticalHUD_ShotHUD'.default.m_sDamageRangeLabel
					@ class'X2AccessVocab'.static.Fill2(class'X2AccessVocab'.default.NumberRange,
						MinDamage.Damage, MaxDamage.Damage));
		}
		if (AbilityState.WillEndTurn())
			AppendPart(Result, class'X2AccessVocab'.default.WillEndTurn);
	}

	return Result;
}

// Who this would hit, and whether any of them are on our side.
//
// The source is the game's OWN answer and the very list ConfirmAbility packs into the ability
// context: GatherAdditionalAbilityTargetsForLocation runs the multi-target style over a location
// and then drops everything CheckMultiTargetConditions rejects. Asked about OUR tile rather than
// read off the method's MarkedTargets cache, which is what makes it synchronous — the method
// only recomputes inside its own Update(), a frame after the push.
//
// Note the game's own friendly-fire modal is driven by a slightly WIDER list
// (CheckForFriendlyUnit runs over the visual GetTargetedActors set), so it remains the
// authority; this is the early warning that lets a blind player never reach it.
private function string BlastContents(XComGameState_Ability AbilityState,
	XComGameState_Unit Shooter, TTile Tile, out string Warning)
{
	local AvailableTarget Blast;
	local vector Loc;

	Loc = `XWORLD.GetPositionFromTileCoordinates(Tile);
	AbilityState.GatherAdditionalAbilityTargetsForLocation(Loc, Blast);
	return DescribeVictims(Blast.AdditionalTargets, Shooter, Warning);
}

// The spoken form of a list of things in a blast, and the friendly-fire clause it earns. ONE
// implementation for the free-aim blast and the targeted-object blast, so a squadmate reads the
// same way inside a grenade's ring and beside a car about to be shot.
private function string DescribeVictims(const out array<StateObjectReference> Victims,
	XComGameState_Unit Shooter, out string Warning)
{
	local XComGameState_Unit Victim;
	local XComGameStateHistory History;
	local string Result, Friends, Civilians, VictimName;
	local int i;

	Warning = "";
	History = `XCOMHISTORY;

	for (i = 0; i < Victims.Length; i++)
	{
		Victim = XComGameState_Unit(History.GetGameStateForObjectID(Victims[i].ObjectID));
		if (Victim == none)
		{
			AppendComma(Result, DescribeNonUnitTarget(Victims[i]));
			continue;
		}

		// Fog parity: a grenade thrown into the dark must not enumerate what is hiding there.
		// The gathered list is built natively and does not care what the squad can see, so this
		// filter is a genuine leak guard rather than a formality.
		if (!class'X2AccessTacticalEvents'.static.IsNarratable(Victim))
			continue;

		if (Victim.ObjectID == Shooter.ObjectID)
			VictimName = class'X2AccessVocab'.default.Yourself;
		else
			VictimName = class'X2AccessTacticalEvents'.static.UnitName(Victim);
		AppendComma(Result, VictimName);

		if (!Victim.IsAlive())
			continue;
		if (Victim.ObjectID == Shooter.ObjectID || Victim.GetTeam() == Shooter.GetTeam())
			AppendComma(Friends, VictimName);
		else if (Victim.IsCivilian())
			AppendComma(Civilians, VictimName);
	}

	if (Friends != "")
		Warning = class'X2AccessVocab'.static.FillS(
			class'X2AccessVocab'.default.FriendlyFireWarning, Friends);
	// Civilians get their own, softer clause rather than sharing that warning: the game turns the
	// blast sphere amber and raises a modal for our own troops and not for them, so this stays on
	// the parity side of the line — their unit flag inside the ring is visible either way.
	if (Civilians != "")
		AppendPart(Warning, class'X2AccessVocab'.static.FillS(
			class'X2AccessVocab'.default.CiviliansInBlast, Civilians));

	return Result;
}

// ---------------------------------------------------------------------------
// Aim plumbing
// ---------------------------------------------------------------------------

// The current aim point in full, or the reason there is nothing to say about it yet.
private function string DescribeAimNow(UITacticalHUD HUD, XComGameState_Ability AbilityState,
	bool bVerbose)
{
	local XComGameState_Unit Shooter;

	Shooter = XComGameState_Unit(
		`XCOMHISTORY.GetGameStateForObjectID(AbilityState.OwnerStateObject.ObjectID));
	if (Shooter == none)
		return "";
	if (!bAimPlanted || AimAbilityID != AbilityState.ObjectID)
		return class'X2AccessVocab'.default.NotAimedYet;
	return DescribeAim(AbilityState, Shooter, CursorMethod(HUD), AimTile(), bVerbose);
}

private function ResetAim()
{
	bAimPlanted = false;
	AimAbilityID = 0;
	bLandWarned = false;
	bLandPending = false;
	LastLandX = 0;
	LastLandY = 0;
	PendingLandX = 0;
	PendingLandY = 0;
}

private function TTile AimTile()
{
	local TTile T;

	T.X = AimX;
	T.Y = AimY;
	T.Z = AimZ;
	return T;
}

// Snap a target tile to the floor of its column near the current height (at or ONE tile
// below), so aiming follows gentle terrain. The world-mode cursor used to step the same way
// and no longer does: X2AccessTileReader.LandingTile lands on the highest surface at or below
// the floor LEVEL, the mouse trace's rule, so a step off a roof reaches the ground. Targeting
// mode keeps the old one-tile search on purpose — whether the floor level means anything to a
// targeting method across floors is the open question in docs/next-steps.md, and this is the
// half that was deliberately left unchanged.
private function ResolveAimFloor(XComWorldData WorldData, out TTile Tile)
{
	local TTile Floor;
	local vector Pos;

	Pos = WorldData.GetPositionFromTileCoordinates(Tile);
	if (WorldData.GetFloorTileForPosition(Pos, Floor, false))
	{
		Tile.X = Clamp(Floor.X, 0, WorldData.NumX - 1);
		Tile.Y = Clamp(Floor.Y, 0, WorldData.NumY - 1);
		Tile.Z = Clamp(Floor.Z, 0, WorldData.NumZ - 1);
	}
}

// Chebyshev plus height, matching X2AccessObjectScanner.TileDistance exactly — the scanner and
// the aim readout describe the same world and must agree about how far away things are.
private function int TileDistance(TTile A, TTile B)
{
	local int DX, DY;

	DX = Abs(A.X - B.X);
	DY = Abs(A.Y - B.Y);
	return Max(DX, DY) + Abs(A.Z - B.Z);
}

// ---------------------------------------------------------------------------
// Shared plumbing (mirrors X2AccessTileReader's)
// ---------------------------------------------------------------------------

// Call-site sugar over X2AccessHelper's joiners, which own the ⛔ never-a-bare-separator rule and
// its terminator set. Nothing outside this class calls these: the object scanner used to reach
// AppendComma through here, and now asks the oracle directly.
private function AppendPart(out string Result, string Part)
{
	class'X2AccessHelper'.static.AppendPart(Result, Part);
}

private static function AppendComma(out string Result, string Part)
{
	class'X2AccessHelper'.static.AppendComma(Result, Part);
}
