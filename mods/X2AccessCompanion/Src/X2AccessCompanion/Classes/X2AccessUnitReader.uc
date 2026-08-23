// X2AccessUnitReader
// The SELECTED unit — who the player is controlling right now, and what shape they are in.
// Same contract as X2AccessTileReader and X2AccessTacticalReader: the C++ layer owns the gate,
// the key and the speech, calls these functions on this class's default object via ProcessEvent
// (game thread, from the frame pump) and reads NativeResult back through reflection right after
// the call returns. Nothing here runs on its own — no listeners, no timers, no polling.
//
// TWO SURFACES, one subject. PollSelection is the automatic half: it says who you just switched
// to. AnnounceUnit is the on-demand half (semicolon): the whole stat block, whenever you ask.
//
// WHY THE SWITCH IS POLLED AND TAB IS NOT CLAIMED. Selecting a soldier submits no game state
// and triggers no event — it is a VISUALIZER change on XComTacticalController, so there is
// nothing for X2AccessTacticalEvents to listen to and X2AccessReplayNarration's frame walk
// never sees it either. Watching the controller instead of claiming Tab leaves the vanilla
// binding exactly where the player's config put it, and it catches every OTHER way the
// selection moves, which a Tab claim never could: clicking a soldier, the start of your turn,
// and the game re-selecting for you when the unit you were on is knocked out or falls.
//
// SIGHTED PARITY. Every value spoken here is drawn on screen and readable nowhere else, because
// both surfaces are write-only pipes into Flash:
//   name, class                -> UITacticalHUD_SoldierInfo.SetStats (the soldier panel)
//   health, shield, armour     -> UIUnitFlag.RealizeStats — literally the three values
//                                 X2AccessTacticalReader.DescribeTargetHealth already reads, so
//                                 a soldier of yours and an enemy in the reticle are worded the
//                                 same and the two surfaces cannot drift apart
//   action points              -> UIUnitFlag.RealizeMoves (the move pips)
//   overwatch held             -> UIUnitFlag.AS_SetOverwatchIcon
//   concealment                -> the flag's own concealment icon
//   enemies visible            -> UITacticalHUD_Enemies, the strip of enemy icons beside the
//                                 reticle, which is per-SOLDIER ("currently visible aliens to
//                                 the active soldier" is Firaxis' own header comment) and
//                                 therefore changes under the player on every Tab. Counted
//                                 rather than listed: the object scanner's enemy key is where
//                                 the individual enemies are read out, and repeating them here
//                                 would make every switch a paragraph
//   bonus / penalty effects    -> the soldier panel's buff arrows. SetStats shows only WHETHER
//                                 there are any; the names live in the tooltip a mouse player
//                                 opens by hovering — so naming them on a key is the same
//                                 on-demand parity the H shot breakdown is, not extra sight.
//
// ⛔ ACTION POINTS ARE A FRIENDLY-ONLY READOUT. UIUnitFlag.RealizeMoves draws the move pips
// inside a bIsFriendly branch — "Only show moves for friendly units" is Firaxis' own comment on
// it — so how many actions an ALIEN has left is a fact the game shows nobody. That is the whole
// reason this reader is scoped to the SELECTED unit rather than to whatever the object scanner
// or the tile cursor is pointing at: the selected unit is always one of yours, so the parity
// question never arises. Do NOT generalize these functions to take an arbitrary unit without
// splitting the action-point clause off first.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessUnitReader extends Object;

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// Change detection for the automatic poll. An ObjectID only (a value type — CDO rule).
// 0 means "nothing announced yet", which is also the reset state.
var int LastSelectedID;

// ---------------------------------------------------------------------------
// Entry points (called from native)
// ---------------------------------------------------------------------------

// Forget whoever was selected, so the next poll reads the selection out in full. Native calls
// this once per MISSION, when the tactical HUD reaches the screen stack — not every time the
// layer becomes usable again. Object IDs live in the campaign's history and a soldier keeps hers
// from one mission to the next, so without this the first selection of the next mission could
// match the last of the previous one and never be spoken; resetting on modals instead would
// re-announce the same soldier every time a dialogue box closed.
function ResetSelection()
{
	NativeResult = "";
	LastSelectedID = 0;
}

// The automatic half, called on the C++ tick while the tactical HUD is usable. Silent unless the
// selection actually MOVED — this runs several times a second, and a poll that talks about its
// own failures is unusable. AnnounceUnit is the one that always answers.
function PollSelection()
{
	local XComGameState_Unit UnitState;

	NativeResult = "";

	// The tile cursor's resolver, not one of ours: GetActiveUnit() returns the controller's
	// ControllingUnitVisualizer var, which lags or sits empty in stretches of a mission, and
	// ActiveUnitState() already falls back to the durable ControllingUnit reference for exactly
	// that reason. One resolver means the readout and the cursor can never name different units.
	UnitState = class'X2AccessTileReader'.static.ActiveUnitState();
	if (UnitState == none)
		return;

	// Whose soldier is this? The controller's selection is normally one of yours, and a
	// mind-controlled ADVENT unit joins eTeam_XCom and is legitimately announced. Anything else
	// reaching this var is the visualizer following a unit the player does not command, and
	// narrating that would hand out enemy turn order the game shows nobody.
	//
	// ⛔ Tested BEFORE the change check, and LastSelectedID is deliberately left alone here: a
	// unit that is not ours is not "the selection" as far as this reader is concerned. Recording
	// it would make every transient visualizer follow during the enemy turn look like a change
	// BACK to your own soldier the moment it ended, and re-announce them turn after turn.
	if (UnitState.GetTeam() != eTeam_XCom)
		return;

	if (UnitState.ObjectID == LastSelectedID)
		return;
	LastSelectedID = UnitState.ObjectID;

	NativeResult = DescribeSelection(UnitState);
}

// The on-demand half (semicolon): the selected unit's whole stat block. Unlike the poll this
// ALWAYS says something — a pressed key must never be silent.
function AnnounceUnit()
{
	local XComGameState_Unit UnitState;

	NativeResult = "";

	UnitState = class'X2AccessTileReader'.static.ActiveUnitState();
	if (UnitState == none)
	{
		NativeResult = "No unit selected";
		return;
	}

	// Keep the poll quiet about what was just spoken, the same discipline
	// X2AccessTacticalReader.AnnounceShot follows.
	LastSelectedID = UnitState.ObjectID;

	NativeResult = DescribeUnit(UnitState);
}

// ---------------------------------------------------------------------------
// Wording
// ---------------------------------------------------------------------------

// What a switch is worth saying: who, in what shape, can they still act, and what can they see.
// Deliberately shorter than the full block — the player is usually cycling, and the next press
// interrupts this line anyway (native sends it on the Focus channel). The class, the conditions
// and the effect names wait for the key.
//
// The enemy count earns its place on the SHORT line because it is the one value here that
// changes with the switch rather than describing the soldier: health and actions are facts a
// player can ask for later, while "does this one see anything?" is the reason they pressed Tab.
// ⛔ But ZERO IS SILENT here — most switches on most missions happen with nothing in sight, so a
// spoken zero would be the clause heard most often and the least often wanted. The usual reason
// to speak an empty answer does not apply: this line always names the soldier, so the player
// still hears it fire. `;` passes bSpeakZero and gets the explicit "No enemies visible".
private function string DescribeSelection(XComGameState_Unit UnitState)
{
	local string Result;

	Result = class'X2AccessTacticalEvents'.static.UnitName(UnitState);
	AppendPart(Result, class'X2AccessTacticalReader'.static.DescribeTargetHealth(UnitState));
	AppendPart(Result, DescribeActions(UnitState));
	AppendPart(Result, class'X2AccessTacticalReader'.static.DescribeVisibleEnemies(UnitState));
	return Result;
}

// Everything the soldier panel and the unit flag show about this unit, in one line.
private function string DescribeUnit(XComGameState_Unit UnitState)
{
	local string Result;

	Result = class'X2AccessTacticalEvents'.static.UnitName(UnitState);
	AppendPart(Result, DescribeType(UnitState));
	AppendPart(Result, class'X2AccessTacticalReader'.static.DescribeTargetHealth(UnitState));
	AppendPart(Result, DescribeActions(UnitState));
	AppendPart(Result, DescribeConcealment(UnitState));
	// true: this one was ASKED for, so "No enemies visible" is the answer rather than a clause
	// the player did not want. The switch line takes the silent form — see DescribeSelection.
	AppendPart(Result,
		class'X2AccessTacticalReader'.static.DescribeVisibleEnemies(UnitState, true));
	AppendPart(Result, DescribeEffects(UnitState));
	return Result;
}

// What KIND of unit this is. For a soldier that is the class the panel draws an icon for, in the
// game's own localized word. For anything else it is already said: GetName(eNameType_RankFull)
// gives a non-soldier its CHARACTER TEMPLATE name — "Сектоид" is both the name and the type — so
// repeating it would only make the line longer.
private function string DescribeType(XComGameState_Unit UnitState)
{
	local X2SoldierClassTemplate ClassTemplate;

	if (!UnitState.IsSoldier())
		return "";

	ClassTemplate = UnitState.GetSoldierClassTemplate();
	if (ClassTemplate == none)
		return "";

	return ClassTemplate.DisplayName;
}

// The move pips, plus the overwatch icon beside them. NumAllActionPoints() is the count
// RealizeMoves feeds Flash, not a re-derivation: it counts every action point TYPE, which is why
// a Momentum or move-only point still reads as an action the player has.
private function string DescribeActions(XComGameState_Unit UnitState)
{
	local int Points;
	local string Result;

	Points = UnitState.NumAllActionPoints();
	if (Points <= 0)
		Result = "No actions left";
	else if (Points == 1)
		Result = "1 action";
	else
		Result = string(Points) @ "actions";

	// The flag's own test (AS_SetOverwatchIcon), not a guess at what a reserve point means.
	if (UnitState.ReserveActionPoints.Find('Overwatch') > -1)
		class'X2AccessTileReader'.static.AppendComma(Result, "overwatch");

	return Result;
}

private function string DescribeConcealment(XComGameState_Unit UnitState)
{
	if (UnitState.IsConcealed())
		return "Concealed";
	return "";
}

// The buff and debuff arrows, named. Both lists come from the unit's own UI summary, so an
// effect appears here exactly when the panel draws an arrow for it:
// GetUISummary_UnitEffectsByCategory filters on the persistent effect's bDisplayInUI and
// IsEffectCurrentlyRelevant, which is what keeps a dormant or spent effect out of the line.
//
// This is also where panic, disorientation, burning and stun get spoken without a hand-written
// English word for any of them — Name is the effect's own localized FriendlyName.
// The two categories stay APART rather than merging into one list, because the panel keeps them
// apart: a bonus draws a green arrow and a penalty a red one, so which of the two an effect is
// counts as something on screen. The two lead-in words are ours and English — the game ships no
// localized label for either (its only "Effects" string is the graphics option) — which is the
// same open decision the Avenger's position phrases are, not a defect.
private function string DescribeEffects(XComGameState_Unit UnitState)
{
	local string Result, Bonuses, Penalties;

	Bonuses = EffectNames(UnitState, ePerkBuff_Bonus);
	if (Bonuses != "")
		Result = "Bonuses:" @ Bonuses;

	Penalties = EffectNames(UnitState, ePerkBuff_Penalty);
	if (Penalties != "")
		AppendPart(Result, "Penalties:" @ Penalties);

	return Result;
}

private function string EffectNames(XComGameState_Unit UnitState, EPerkBuffCategory Category)
{
	local array<UISummary_UnitEffect> Effects;
	local string Result;
	local int i;

	Effects = UnitState.GetUISummary_UnitEffectsByCategory(Category);
	for (i = 0; i < Effects.Length; i++)
		class'X2AccessTileReader'.static.AppendComma(Result, Effects[i].Name);

	return Result;
}

// ---------------------------------------------------------------------------
// Shared plumbing (mirrors X2AccessTacticalReader's)
// ---------------------------------------------------------------------------

private function AppendPart(out string Result, string Part)
{
	if (Part == "")
		return;
	if (Result != "")
		Result $= class'X2AccessTileReader'.static.EndsSentence(Result) ? " " : ". ";
	Result $= Part;
}
