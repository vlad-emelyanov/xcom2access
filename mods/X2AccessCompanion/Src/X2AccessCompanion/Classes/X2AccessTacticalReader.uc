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

	if (!ResolveShot(HUD, Action, AbilityState, TargetRef))
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
		AppendPart(Result, DescribeTarget(Action, AbilityState, TargetRef));
	}
	else if (TargetRef.ObjectID != LastTargetID)
	{
		Result = DescribeTarget(Action, AbilityState, TargetRef);
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
	local string Result;

	NativeResult = "";

	HUD = GetTacticalHUD();
	if (HUD == none || !HUD.m_isMenuRaised)
	{
		NativeResult = "Not targeting";
		return;
	}
	if (!ResolveShot(HUD, Action, AbilityState, TargetRef))
	{
		NativeResult = "No ability selected";
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
	AppendPart(Result, DescribeTarget(Action, AbilityState, TargetRef));
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
	local string Result;

	NativeResult = "";

	HUD = GetTacticalHUD();
	if (HUD == none || !HUD.m_isMenuRaised)
	{
		NativeResult = "Not targeting";
		return;
	}
	if (!ResolveShot(HUD, Action, AbilityState, TargetRef))
	{
		NativeResult = "No ability selected";
		return;
	}
	// A free-aimed ability has no per-target to-hit table to break down, and the game shows none
	// either: while a cursor ability is up the enemy strip is empty and the reticle reads no aim
	// percentage. So this is parity rather than a gap — say so, and give the aim instead.
	if (CursorMethod(HUD) != none)
	{
		Result = "No breakdown while free aiming";
		AppendPart(Result, DescribeAimNow(HUD, AbilityState, true));
		NativeResult = Result;
		return;
	}

	if (!GetBreakdown(AbilityState, TargetRef, Breakdown))
	{
		NativeResult = "No shot breakdown";
		return;
	}

	Result = "Hit" @ string(HitChanceOf(Breakdown)) $ " percent";
	AppendPart(Result, DescribeModifiers(Breakdown, eHit_Success));
	AppendPart(Result, "Crit" @ string(Breakdown.ResultTable[eHit_Crit]) $ " percent");
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
	out XComGameState_Ability AbilityState, out StateObjectReference TargetRef)
{
	local X2TargetingMethod Method;
	local int TargetIndex;

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
		Result = "Free aim:" @ Result;

	if (HUD.m_kAbilityHUD != none && HUD.m_kAbilityHUD.m_arrAbilities.Length > 0)
		Result $= "," @ string(HUD.m_kAbilityHUD.m_iCurrentIndex + 1) @ "of"
			@ string(HUD.m_kAbilityHUD.m_arrAbilities.Length);

	if (AbilityState.IsCoolingDown())
	{
		AppendComma(Result, "cooldown" @ string(AbilityState.GetCooldownRemaining())
			@ (AbilityState.GetCooldownRemaining() == 1 ? "turn" : "turns"));
	}
	else
	{
		Charges = AbilityState.GetCharges();
		if (Charges >= 0)
			AppendComma(Result, string(Charges) @ (Charges == 1 ? "charge" : "charges"));
	}

	Template = AbilityState.GetMyTemplate();
	BattleData = XComGameState_BattleData(
		`XCOMHISTORY.GetSingleGameStateObjectForClass(class'XComGameState_BattleData'));
	if (Template != none && BattleData != none && BattleData.IsAbilityObjectiveHighlighted(Template))
		AppendComma(Result, "objective");

	if (Action.AvailableCode != 'AA_Success')
		AppendPart(Result, class'X2AbilityTemplateManager'.static
			.GetDisplayStringForAvailabilityCode(Action.AvailableCode));

	return Result;
}

// "Sectoid, hit 73 percent, crit 20 percent, damage 4 to 6, 5 of 8 health, armour 1".
// Free aim has no unit to name and no breakdown to give, so it says so rather than reading
// as a shot with a missing target.
private function string DescribeTarget(AvailableAction Action,
	XComGameState_Ability AbilityState, StateObjectReference TargetRef)
{
	local XComGameState_Unit TargetUnit;
	local ShotBreakdown Breakdown;
	local WeaponDamageValue MinDamage, MaxDamage;
	local int AllowsShield;
	local string Result;

	if (Action.bFreeAim)
		return "Free aim, no target";

	if (TargetRef.ObjectID <= 0)
	{
		// An unusable ability has already said WHY in the game's own localized words ("Нет
		// доступных целей."). Adding an English "No target" after it is the same fact in two
		// languages — and it was the shape the 2026-08-21 run produced most often at the shot HUD.
		if (Action.AvailableCode != 'AA_Success')
			return "";
		return "No target";
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
			@ string(HitChanceOf(Breakdown)) $ " percent");
		if (Breakdown.ResultTable[eHit_Crit] > 0)
		{
			// SpecialCritLabel renames the crit row for abilities where "crit" is the wrong word
			// (a Reaper's Executioner, for one); the panel prefers it whenever it is set, so we do.
			AppendComma(Result, (Breakdown.SpecialCritLabel != "" ? Breakdown.SpecialCritLabel
				: class'UITacticalHUD_ShotHUD'.default.m_sCritChanceLabel)
				@ string(Breakdown.ResultTable[eHit_Crit]) $ " percent");
		}
	}

	AbilityState.GetDamagePreview(TargetRef, MinDamage, MaxDamage, AllowsShield);
	if (MaxDamage.Damage > 0)
	{
		if (MinDamage.Damage == MaxDamage.Damage)
			AppendComma(Result, "damage" @ string(MinDamage.Damage));
		else
			AppendComma(Result, "damage" @ string(MinDamage.Damage) @ "to"
				@ string(MaxDamage.Damage));
	}

	AppendComma(Result, DescribeTargetCover(TargetUnit, AbilityState.OwnerStateObject));
	AppendComma(Result, DescribeTargetHealth(TargetUnit));
	return Result;
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
		return "flanked";

	switch (TargetUnit.GetCoverTypeFromLocation())
	{
	case CT_Standing: return "in full cover";
	case CT_MidLevel: return "in half cover";
	}
	return "no cover";
}

// Health, shield and armour — what the target's unit flag shows over its head.
static function string DescribeTargetHealth(XComGameState_Unit TargetUnit)
{
	local int Armor, Shield;
	local string Result;

	if (TargetUnit == none)
		return "";

	Result = string(int(TargetUnit.GetCurrentStat(eStat_HP))) @ "of"
		@ string(int(TargetUnit.GetMaxStat(eStat_HP))) @ "health";

	Shield = int(TargetUnit.GetCurrentStat(eStat_ShieldHP));
	if (Shield > 0)
		AppendComma(Result, "shield" @ string(Shield));

	Armor = int(TargetUnit.GetArmorMitigationForUnitFlag());
	if (Armor > 0)
		AppendComma(Result, "armour" @ string(Armor));

	return Result;
}

// Targets that are not units: destructible cover, a hackable terminal, a door.
private function string DescribeNonUnitTarget(StateObjectReference TargetRef)
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
		return "Object";

	return "Target";
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
			Value = "plus" @ string(Breakdown.Modifiers[i].Value);
		else
			Value = "minus" @ string(-Breakdown.Modifiers[i].Value);
		AppendPart(Result, Breakdown.Modifiers[i].Reason @ Value $ " percent");
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
		NativeResult = "Map edge";
		return;
	}

	// Follow the terrain, exactly as the tile cursor does: aiming up a slope or off a roof edge
	// should track the ground rather than fly level off it.
	ResolveAimFloor(WorldData, Target);

	if (!PushAim(Cursor, WorldData, Target))
	{
		NativeResult = "Out of range";
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
			NativeResult = "Out of range";
			return;
		}
		if (Delta > 0)
			NativeResult = "Up." @ DescribeAim(AbilityState, Shooter, Method, AimTile(), false);
		else
			NativeResult = "Down." @ DescribeAim(AbilityState, Shooter, Method, AimTile(), false);
		return;
	}
	if (Delta > 0)
		NativeResult = "No floor above";
	else
		NativeResult = "No floor below";
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
		NativeResult = "No marker selected";
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
		NativeResult = "Out of range";
		return;
	}

	// Adopted only on SUCCESS, so a refused jump leaves the previous aim point — and its
	// activation — exactly as the player last heard it. A later arrow step then continues from
	// here rather than snapping back to the shooter's own tile.
	bAimPlanted = true;
	AimAbilityID = AbilityState.ObjectID;
	NativeResult = "Aiming." @ DescribeAim(AbilityState, Shooter, Method, AimTile(), false);
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
		AppendPart(Result, "Arrows to aim");
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
	return "Blocked, lands" @ class'X2AccessTileReader'.static.OffsetWords(Aim, Landing);
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

	HUD = GetTacticalHUD();
	if (HUD == none || !HUD.m_isMenuRaised)
	{
		NativeResult = "Not targeting";
		return false;
	}
	Method = CursorMethod(HUD);
	if (Method == none)
	{
		NativeResult = "Not free aiming";
		return false;
	}
	if (!ResolveShot(HUD, Action, AbilityState, TargetRef))
	{
		NativeResult = "No ability selected";
		return false;
	}

	// The SHOOTER, not the "active unit": they are the same today, but the ability owns the
	// authoritative answer and it is the unit the cursor's range chain is attached to.
	Shooter = XComGameState_Unit(
		`XCOMHISTORY.GetGameStateForObjectID(AbilityState.OwnerStateObject.ObjectID));
	TC = class'X2AccessTileReader'.static.TacCtrl();
	if (Shooter == none || TC == none)
	{
		NativeResult = "No unit selected";
		return false;
	}
	Cursor = TC.GetCursor();
	WorldData = `XWORLD;
	if (Cursor == none || WorldData == none)
	{
		NativeResult = "No cursor";
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

	NativeResult = "Aiming." @ DescribeAim(AbilityState, Shooter, Method, Home, false);
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
		Result = "On your own tile";
	else
		Result = Bearing $ "," @ string(TileDistance(Home, Tile)) @ "tiles";

	// Only abilities that actually hit an AREA get a blast clause. An evac zone or a teleport is
	// cursor-targeted too and hits nobody, and "Blast: nothing" about one of those would read as
	// a broken readout rather than as useful news. Note this cannot be keyed on GetAbilityRadius
	// instead: a line ability has a multi-target style but no radius, and would lose its list.
	Template = AbilityState.GetMyTemplate();
	if (Template != none && Template.AbilityMultiTargetStyle != none)
	{
		Contents = BlastContents(AbilityState, Shooter, Tile, Warning);
		if (Contents == "")
			AppendPart(Result, "Blast: nothing");
		else
			AppendPart(Result, "Blast:" @ Contents);
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
			AppendPart(Result, "Blast radius" @ string(int(Radius
				/ class'XComWorldData'.const.WORLD_StepSize)) @ "tiles");

		// The empty target reference is deliberate and is what UITacticalHUD_ShotHUD does for an
		// ability with no primary target: ONE damage band for the ability, which is all a sighted
		// player gets while free-aiming — there is no per-victim number on screen to match.
		AbilityState.GetDamagePreview(EmptyRef, MinDamage, MaxDamage, AllowsShield);
		if (MaxDamage.Damage > 0)
		{
			// Capitalised: AppendPart joins with ". ", so this starts a sentence.
			if (MinDamage.Damage == MaxDamage.Damage)
				AppendPart(Result, "Damage" @ string(MinDamage.Damage));
			else
				AppendPart(Result, "Damage" @ string(MinDamage.Damage) @ "to"
					@ string(MaxDamage.Damage));
		}
		if (AbilityState.WillEndTurn())
			AppendPart(Result, "Will end turn");
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
	local XComGameState_Unit Victim;
	local XComGameStateHistory History;
	local vector Loc;
	local string Result, Friends, Civilians, VictimName;
	local int i;

	Warning = "";
	History = `XCOMHISTORY;
	Loc = `XWORLD.GetPositionFromTileCoordinates(Tile);
	AbilityState.GatherAdditionalAbilityTargetsForLocation(Loc, Blast);

	for (i = 0; i < Blast.AdditionalTargets.Length; i++)
	{
		Victim = XComGameState_Unit(
			History.GetGameStateForObjectID(Blast.AdditionalTargets[i].ObjectID));
		if (Victim == none)
		{
			AppendComma(Result, DescribeNonUnitTarget(Blast.AdditionalTargets[i]));
			continue;
		}

		// Fog parity: a grenade thrown into the dark must not enumerate what is hiding there.
		// The gathered list is built natively and does not care what the squad can see, so this
		// filter is a genuine leak guard rather than a formality.
		if (!class'X2AccessTacticalEvents'.static.IsNarratable(Victim))
			continue;

		if (Victim.ObjectID == Shooter.ObjectID)
			VictimName = "yourself";
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
		Warning = "WARNING: friendly fire," @ Friends @ "in blast";
	// Civilians get their own, softer clause rather than sharing that warning: the game turns the
	// blast sphere amber and raises a modal for our own troops and not for them, so this stays on
	// the parity side of the line — their unit flag inside the ring is visible either way.
	if (Civilians != "")
		AppendPart(Warning, "Civilians in blast:" @ Civilians);

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
		return "Not aimed yet. Arrows to aim";
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

// Snap a target tile to the floor of its column near the current height, so aiming follows
// terrain. Mirrors X2AccessTileReader.ResolveFloor; kept local because that one is private and
// the two differ in nothing but which state they belong to.
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

private function AppendPart(out string Result, string Part)
{
	if (Part == "")
		return;
	if (Result != "")
		Result $= class'X2AccessTileReader'.static.EndsSentence(Result) ? " " : ". ";
	Result $= Part;
}

static function AppendComma(out string Result, string Part)
{
	if (Part == "")
		return;
	if (Result != "")
		Result $= ", ";
	Result $= Part;
}
