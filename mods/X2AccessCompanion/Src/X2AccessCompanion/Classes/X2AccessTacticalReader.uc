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

	HUD = GetTacticalHUD();
	if (HUD == none || !HUD.m_isMenuRaised)
	{
		// Lowered (or gone): forget what was announced so the next raise reads in full.
		if (bWasRaised)
		{
			bWasRaised = false;
			LastAbilityID = 0;
			LastTargetID = 0;
		}
		return;
	}
	bWasRaised = true;

	if (!ResolveShot(HUD, Action, AbilityState, TargetRef))
		return;

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

// "Overwatch, 3 of 7" — plus the game's own reason string when the ability is not usable
// (the same string the info panel puts under the name in place of the help text).
private function string DescribeAbility(UITacticalHUD HUD, AvailableAction Action,
	XComGameState_Ability AbilityState)
{
	local string Result;

	Result = AbilityState.GetMyFriendlyName();
	if (Action.bFreeAim)
		Result = "Free aim:" @ Result;

	if (HUD.m_kAbilityHUD != none && HUD.m_kAbilityHUD.m_arrAbilities.Length > 0)
		Result $= "," @ string(HUD.m_kAbilityHUD.m_iCurrentIndex + 1) @ "of"
			@ string(HUD.m_kAbilityHUD.m_arrAbilities.Length);

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
		return "No target";

	TargetUnit = XComGameState_Unit(`XCOMHISTORY.GetGameStateForObjectID(TargetRef.ObjectID));
	if (TargetUnit != none)
		Result = TargetUnit.GetName(eNameType_Full);
	else
		Result = DescribeNonUnitTarget(TargetRef);

	if (GetBreakdown(AbilityState, TargetRef, Breakdown))
	{
		AppendComma(Result, "hit" @ string(HitChanceOf(Breakdown)) $ " percent");
		if (Breakdown.ResultTable[eHit_Crit] > 0)
			AppendComma(Result, "crit" @ string(Breakdown.ResultTable[eHit_Crit]) $ " percent");
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
		if (IObj.IsDoor())
			return "Door";
		if (IObj.MustBeHacked())
			return "Hackable object";
		return "Interactive object";
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

// ---------------------------------------------------------------------------
// Shared plumbing (mirrors X2AccessTileReader's)
// ---------------------------------------------------------------------------

private function AppendPart(out string Result, string Part)
{
	if (Part == "")
		return;
	if (Result != "")
		Result $= ". ";
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
