// X2AccessHackingReader
// The hacking screen's game-state half — phases 1-3 of the hacking layer. Same contract as
// X2AccessTileReader and X2AccessTacticalReader: the C++ layer (src/hacking_screen.cpp) owns the
// keys and the speech, calls these functions on this class's default object via ProcessEvent
// (game thread, from the frame pump or the arbiter's tick drain) and reads NativeResult back
// through reflection right after the call returns. Nothing here runs on its own.
//
// WHY THE WORDS LIVE HERE AND NOT IN C++: UIHackingScreen carries no widgets at all — /gui on it
// prints "1 panels", the screen and nothing else. Every word goes into Flash through
// MC.FunctionOp / Movie.ActionScriptVoid and is retained nowhere, so there is no widget for
// game_ui.cpp to transcribe. Everything spoken below is a script property of the screen or a
// field of an X2HackRewardTemplate.
//
// It also means ZERO new field offsets, which here is a correctness argument and not just a
// convenience: the Highlander interleaves m_UsingSkulljackScreen and m_hackCanceled into the same
// bitfield at +0x450, so bWaitingForInput's MASK is 0x20 under the Highlander and 0x08 in
// vanilla. static_asserts pin offsets, not masks — reading by name sidesteps the whole problem.
//
// SIGHTED PARITY: every value spoken here is on the pad in the soldier's hands.
//   reward name / description / unlock chance / failure strip -> the three reward cards
//       (UIHackingScreen.PopulateResultsInfo -> AS_SetResultInfo, RefreshPreviewState)
//   soldier rank, name and tech level                          -> PopulateSoldierInfo
//   target name and tech level                                 -> PopulateEnemyInfo
// Nothing here reads anything a sighted player is not shown. In particular only the first three
// rewards are ever mentioned: the Flash movie has three panels, RefreshPreviewState hard-loops
// Index < 3, and OnUnrealCommand only ever writes 1 or 2 — so on a hackable whose reward array is
// longer (the tutorial door offers five) the tail is unreachable for everyone.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessHackingReader extends Object;

// The Flash movie has three reward panels and the screen's own refresh loop stops at three.
const MAX_CARDS = 3;

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// A second reflected-out property, read by native after PollHack: "" when no hacking screen is
// up, "open" while the player still owns the decision, "started" once the hack has been
// committed. C++ drops its key claims on "started" so that every key falls through to the game —
// the reveal ends with "press any key", and a key we swallow would not dismiss it.
var string NativeState;

// Our read cursor over the cards, 0-based. Cards 1..n are the gambles and moving onto one also
// makes it the game's selection; card 0 is the guaranteed/feedback slot and is not selectable,
// which is exactly what the screen does.
var int CursorIndex;

// Announce-once keys. LastHackTargetID is the ObjectID of the thing being hacked, so a second
// hack in the same mission re-announces even if C++ misses the falling edge; bAnnouncedIntro
// carries the "already spoken" flag on its own, so a hackable that somehow reports ObjectID 0
// cannot loop the opening burst forever. EndHack() clears all three.
var int LastHackTargetID;
var bool bAnnouncedIntro;
var bool bAnnouncedResult;

// ---------------------------------------------------------------------------
// Entry points (called from native)
// ---------------------------------------------------------------------------

// The automatic narrator, called on the C++ tick while UIHackingScreen owns the tactical surface.
// Announces the screen ONCE per hack and is silent afterwards; the on-demand keys below are the
// ones that always answer.
function PollHack()
{
	local UIHackingScreen HackScreen;
	local int TargetID;

	NativeResult = "";
	NativeState = "";

	HackScreen = GetHackScreen();
	if (HackScreen == none)
	{
		LastHackTargetID = 0;
		bAnnouncedIntro = false;
		bAnnouncedResult = false;
		return;
	}

	NativeState = HackScreen.m_hackStarted ? "started" : "open";

	// Deliberate exception to "never gate screen readiness on bIsVisible": on THIS screen the
	// flag is a real signal, because the Highlander (issue #330) calls Hide() on a 2D skulljack
	// screen and shows it 3.3 s later to sync with the melee animation. Announcing into those
	// 3.3 s would talk about a pad that is not on screen yet. Verified live 2026-08-20: a normal
	// in-world hack reads bIsVisible = true from the moment it is pushed.
	if (!HackScreen.bIsInited || !HackScreen.bIsVisible)
		return;

	if (HackScreen.HackTargetObject != none)
		TargetID = HackScreen.HackTargetObject.ObjectID;

	// First sight of this hack: the opening burst.
	if (!bAnnouncedIntro || TargetID != LastHackTargetID)
	{
		bAnnouncedIntro = true;
		bAnnouncedResult = false;
		LastHackTargetID = TargetID;
		CursorIndex = Clamp(HackScreen.SelectedHackRewardOption, 0,
			Max(CardCount(HackScreen) - 1, 0));

		NativeResult = DescribeHeader(HackScreen);
		AppendPart(NativeResult, DescribeGuaranteedSummary(HackScreen));
		AppendPart(NativeResult, DescribeCursor(HackScreen));
		AppendPart(NativeResult, DescribeControls(HackScreen));
		return;
	}

	// Phase 3: the reveal. bWaitingForInput is set from OnCommand("HackEnded"), i.e. the frame
	// Flash finishes the meter and puts the result on the pad — the ONLY honest moment to speak
	// it. The game state was submitted the instant the player pressed Enter, about three seconds
	// earlier, and narrating from there would call the outcome while the meter is still climbing.
	if (HackScreen.m_hackStarted && HackScreen.bWaitingForInput && !bAnnouncedResult)
	{
		bAnnouncedResult = true;
		NativeResult = DescribeResult(HackScreen);
	}
}

// Re-announce on demand (Delete): the header plus whichever card the cursor is on. Unlike the
// poll this ALWAYS says something — a pressed key must never be silent.
function AnnounceHack()
{
	local UIHackingScreen HackScreen;

	NativeResult = "";

	HackScreen = GetHackScreen();
	if (HackScreen == none)
	{
		NativeResult = "No hacking screen";
		return;
	}

	SyncCursor(HackScreen);

	NativeResult = DescribeHeader(HackScreen);
	AppendPart(NativeResult, DescribeCursor(HackScreen));
	if (HackScreen.m_hackStarted)
		AppendPart(NativeResult, "Hack in progress");
}

// Left arrow: step the cursor one card toward the guaranteed slot.
function SelectPrev()
{
	StepCursor(-1);
}

// Right arrow: step the cursor one card away from the guaranteed slot.
function SelectNext()
{
	StepCursor(1);
}

// Called by native when the screen goes away, so the next hack announces even when it is the same
// object (a cancelled hack retried on the same door).
function EndHack()
{
	NativeResult = "";
	NativeState = "";
	LastHackTargetID = 0;
	CursorIndex = 0;
	bAnnouncedIntro = false;
	bAnnouncedResult = false;
}

// ---------------------------------------------------------------------------
// Cursor and selection
// ---------------------------------------------------------------------------

// Move the cursor and, when it lands on a selectable card, make that the game's selection.
// Stops at the ends and re-reads the card there rather than wrapping: with three cards a wrap
// is more disorienting than an edge.
private function StepCursor(int Delta)
{
	local UIHackingScreen HackScreen;
	local int NewIndex;

	NativeResult = "";

	HackScreen = GetHackScreen();
	if (HackScreen == none)
	{
		NativeResult = "No hacking screen";
		return;
	}

	// The hack is away: the screen's own OnUnrealCommand would treat any command as the
	// "press any key to continue" dismissal (its first act calls HackEnded whenever
	// bWaitingForInput is set), and the selection no longer means anything anyway.
	if (HackScreen.m_hackStarted)
	{
		NativeResult = "Hack in progress";
		return;
	}

	SyncCursor(HackScreen);

	NewIndex = Clamp(CursorIndex + Delta, 0, Max(CardCount(HackScreen) - 1, 0));
	if (NewIndex != CursorIndex)
	{
		CursorIndex = NewIndex;
		SelectCard(HackScreen, CursorIndex);
	}

	NativeResult = DescribeCursor(HackScreen);
}

// Hand the screen the command its own controller path uses, rather than writing
// SelectedHackRewardOption behind its back: this sets the selection AND the highlight AND calls
// RefreshPreviewState, so the Flash cards re-render and the meter marker moves with us. It is
// also the value X2HackRewardTemplateManager.AcquireHackRewards reads back off this very screen
// object when the hack resolves, so there is exactly one source of truth.
//
// The screen offers no command for card 0 — it is not selectable there either.
private function SelectCard(UIHackingScreen HackScreen, int Index)
{
	local int Cmd;

	if (Index == 1)
		Cmd = class'UIUtilities_Input'.const.FXS_DPAD_LEFT;
	else if (Index == 2)
		Cmd = class'UIUtilities_Input'.const.FXS_DPAD_RIGHT;
	else
		return;

	HackScreen.OnUnrealCommand(Cmd, class'UIUtilities_Input'.const.FXS_ACTION_PRESS);
}

// The mouse can still move the selection under us (clicking a card routes through
// OnCommand("SelectReward")), so reconcile before every read or step. The cursor is allowed to
// sit on card 0 while the selection is elsewhere — that is the one legitimate divergence, and
// DescribeCursor says so out loud.
private function SyncCursor(UIHackingScreen HackScreen)
{
	local int MaxIndex;

	MaxIndex = Max(CardCount(HackScreen) - 1, 0);

	if (CursorIndex != 0 && CursorIndex != HackScreen.SelectedHackRewardOption)
		CursorIndex = HackScreen.SelectedHackRewardOption;

	CursorIndex = Clamp(CursorIndex, 0, MaxIndex);
}

// How many cards the player can actually see. The reward ARRAY can be longer: a Hackable's own
// rewards are concatenated with BattleData.TacticalHackRewards
// (XComGameState_InteractiveObject.GetHackRewards), and the tutorial door comes to five — but
// the movie has three panels and RefreshPreviewState stops at three, so entries past the third
// are invisible to everyone.
private function int CardCount(UIHackingScreen HackScreen)
{
	return Min(HackScreen.HackRewards.Length, MAX_CARDS);
}

// ---------------------------------------------------------------------------
// Description
// ---------------------------------------------------------------------------

// "Hacking. ПРОЧНАЯ ДВЕРЬ, ТЕХ. УРОВЕНЬ 60. Rookie Jane Kelly, ТЕХ. УРОВЕНЬ 5" — the two info
// blocks along the bottom of the pad, in the game's own label wording.
private function string DescribeHeader(UIHackingScreen HackScreen)
{
	local string Result, Target;

	Result = "Hacking";

	Target = DescribeTarget(HackScreen);
	if (Target != "")
		AppendPart(Result, Target);
	AppendComma(Result, HackScreen.strHackAbilityLabel @ string(int(HackScreen.HackDefense)));

	if (HackScreen.UnitState != none)
	{
		// GetSoldierRankName + eNameType_Full, exactly as PopulateSoldierInfo does it — NOT
		// eNameType_RankFull, which uses GET_RANK_ABBRV and speaks the pad's "НОВОБРАНЕЦ" as
		// "Нов." (heard live 2026-08-20). The Highlander's rank getter also resolves faction
		// rank tables, which the plain GET_RANK_STR path would get wrong for Reaper, Skirmisher
		// and Templar soldiers.
		AppendPart(Result, HackScreen.UnitState.GetSoldierRankName()
			@ HackScreen.UnitState.GetName(eNameType_Full));
		AppendComma(Result, HackScreen.strHackAbilityLabel @ string(int(HackScreen.HackOffense)));
	}

	return Result;
}

// Mirrors PopulateEnemyInfo's own branch: a skulljacked unit gets its faction title and full
// name, an interactive object gets the name the loot UI would use for it.
private function string DescribeTarget(UIHackingScreen HackScreen)
{
	local X2CharacterTemplate CharTemplate;
	local string Result;

	if (HackScreen.TargetUnit != none)
	{
		CharTemplate = HackScreen.TargetUnit.GetMyTemplate();
		if (CharTemplate != none)
		{
			if (CharTemplate.bIsAdvent)
				Result = HackScreen.m_strAdventInfoTitle;
			else if (CharTemplate.bIsAlien)
				Result = HackScreen.m_strAlienInfoTitle;
		}
		AppendComma(Result, HackScreen.TargetUnit.GetName(eNameType_Full));
		return Result;
	}

	if (HackScreen.ObjectState != none)
		return HackScreen.ObjectState.GetLootingName();

	return "";
}

// One clause naming the floor of the decision, spoken only in the opening burst: what you get
// even when the roll fails. The description is left for the player to arrow onto — dumping every
// card unprompted is exactly what this project does not do.
private function string DescribeGuaranteedSummary(UIHackingScreen HackScreen)
{
	local X2HackRewardTemplate Template;

	if (CardCount(HackScreen) < 1)
		return "";

	Template = HackScreen.HackRewards[0];
	if (Template == none)
		return "";

	return HeaderWordFor(HackScreen, Template) $ ":" @ Template.GetFriendlyName();
}

// The card under the cursor, plus — on the non-selectable card 0 only — where the gamble still
// stands, so "which one am I actually betting on" is never ambiguous.
private function string DescribeCursor(UIHackingScreen HackScreen)
{
	local X2HackRewardTemplate Selected;
	local string Result;

	Result = DescribeCard(HackScreen, CursorIndex);

	if (CursorIndex == 0 && CardCount(HackScreen) > 1)
	{
		Selected = SelectedTemplate(HackScreen);
		if (Selected != none)
			AppendPart(Result, "Gamble stays on" @ Selected.GetFriendlyName() $ ","
				@ ChanceWords(HackScreen, HackScreen.SelectedHackRewardOption));
	}

	return Result;
}

// "2 of 3. Измененные сигналы, 6 percent. Солдат немедленно получает маскировку. Неудача: нет
// эффекта. Selected." — the card as printed, in card order.
private function string DescribeCard(UIHackingScreen HackScreen, int Index)
{
	local X2HackRewardTemplate Template;
	local string Result;

	if (Index < 0 || Index >= CardCount(HackScreen))
		return "No reward";

	Template = HackScreen.HackRewards[Index];
	if (Template == none)
		return "No reward";

	Result = string(Index + 1) @ "of" @ string(CardCount(HackScreen));

	if (Index == 0)
		AppendPart(Result, HeaderWordFor(HackScreen, Template) $ ":" @ Template.GetFriendlyName());
	else
		AppendPart(Result, Template.GetFriendlyName());

	AppendComma(Result, ChanceWords(HackScreen, Index));
	AppendPart(Result, Template.GetDescription(HackScreen.HackTargetObject));

	// The failure strip, and it is not decoration: "Неудача: обратная связь" means failing gets
	// the squad PUNISHED rather than merely getting nothing, which inverts the whole risk
	// calculation. The screen prints it on the gamble cards only.
	if (Index > 0)
		AppendPart(Result, FailTextFor(HackScreen));

	if (Index == HackScreen.SelectedHackRewardOption)
		AppendPart(Result, "Selected");

	return Result;
}

// The outcome, spoken the moment the meter stops — phase 3.
//
// SOURCED ENTIRELY FROM THE PAD, on purpose. The tempting reads —
// AbilityContext.ResultContext.StatContestResult, or bHasBeenHacked +
// UserSelectedHackReward out of the history — are never written in the tutorial, which replays
// recorded frames and never runs FinalizeHackAbility_BuildGameState. The pad animates and lights
// up there exactly as it does on a real mission, and a sighted player cannot tell the two apart,
// so neither may this. m_rewardsTaken is Flash's own count: the screen bumps it once per
// OnCommand("UnlockReward"), i.e. once per card that LIT, and Firaxis names the delegate
// parameter it hands out iRewardIndex.
//
// It also beats the banner Flash prints. AS_UpdateHackComplete picks its title from
// HackRewards[RewardIndex].bBadThing, and RewardIndex falls back to slot 0 on a miss — so with an
// ordinary (non-bBadThing) slot 0 the pad reads "Успех!" even when the gamble missed, because the
// floor was still awarded. The lit cards are the truth; the banner is not. Neither the banner nor
// RewardIndex is retained in script anyway.
private function string DescribeResult(UIHackingScreen HackScreen)
{
	local int i, Count, Selected;
	local string Result, Unlocked;
	local bool bSelectedUnlocked;

	Count = Min(HackScreen.m_rewardsTaken, CardCount(HackScreen));
	Selected = HackScreen.SelectedHackRewardOption;

	for (i = 0; i < Count; i++)
	{
		if (HackScreen.HackRewards[i] == none)
			continue;
		AppendComma(Unlocked, HackScreen.HackRewards[i].GetFriendlyName());
		if (i == Selected)
			bSelectedUnlocked = true;
	}

	if (Unlocked != "")
		Result = HackScreen.strRewardUnlockedLabel $ ":" @ Unlocked;
	else
		Result = "Nothing unlocked";

	// Naming the gamble that did not land, with the card's own failure strip — which is the line
	// that matters when slot 0 is a bad thing, because then missing does not mean "nothing".
	if (!bSelectedUnlocked && Selected > 0 && Selected < CardCount(HackScreen)
		&& HackScreen.HackRewards[Selected] != none)
	{
		AppendPart(Result, "Missed:" @ HackScreen.HackRewards[Selected].GetFriendlyName());
		AppendPart(Result, FailTextFor(HackScreen));
	}

	AppendPart(Result, HackScreen.HackCompleteAnyKeyToContinue);
	return Result;
}

// Two short verbs, spoken once when the screen opens. This is the one place the project puts
// navigation help in an announcement rather than behind a key: the screen is a modal with no
// other discovery surface, it holds no list to dump, and the game's own button labels
// (ВЗЛОМАТЬ / ОТМЕНИТЬ) are pure Flash.
private function string DescribeControls(UIHackingScreen HackScreen)
{
	local string Result;

	if (CardCount(HackScreen) > 1)
		Result = "Left and right to choose";

	AppendComma(Result, "Enter to hack");
	if (HackScreen.CanCancel())
		AppendComma(Result, "Escape to cancel");

	return Result;
}

// The game's own header word for the guaranteed slot: ГАРАНТИРОВАНО normally, ОБРАТНАЯ СВЯЗЬ
// when that slot is a bad thing (PopulateResultsInfo picks between exactly these two).
private function string HeaderWordFor(UIHackingScreen HackScreen, X2HackRewardTemplate Template)
{
	return Template.bBadThing ? HackScreen.FeedbackHeader : HackScreen.GuaranteedHeader;
}

// The red strip under a gamble card, chosen the way PopulateResultsInfo chooses it: by whether
// slot 0 is a bad thing, not by anything about this card.
private function string FailTextFor(UIHackingScreen HackScreen)
{
	if (HackScreen.HackRewards.Length < 1 || HackScreen.HackRewards[0] == none)
		return "";

	return HackScreen.HackRewards[0].bBadThing
		? HackScreen.FeedbackFailText : HackScreen.GuaranteedFailText;
}

private function X2HackRewardTemplate SelectedTemplate(UIHackingScreen HackScreen)
{
	local int Index;

	Index = HackScreen.SelectedHackRewardOption;
	if (Index < 0 || Index >= CardCount(HackScreen))
		return none;

	return HackScreen.HackRewards[Index];
}

// The unlock chance as the card prints it. Always the SCREEN'S OWN GetHackChance — a sitrep
// (X2SitRepEffect_ModifyHackDefenses), a Gremlin's HackingAttemptBonus, the Skullmining bonus and
// any mod all move the inputs, and re-deriving the formula here would be one more thing to keep
// in sync. Truncated rather than rounded, because that is what Flash displays: this screen's
// 6.42 and 2.75 print as 6% and 2%.
private function string ChanceWords(UIHackingScreen HackScreen, int Index)
{
	if (Index < 0 || Index >= CardCount(HackScreen))
		return "";

	return string(int(HackScreen.GetHackChance(Index))) $ " percent";
}

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------

// GetScreen, not GetFirstInstanceOf — the same lookup X2Ability_DefaultAbilitySet uses to find
// this screen when the hack resolves, so what we read can never be a different instance from the
// one whose SelectedHackRewardOption decides the outcome.
private function UIHackingScreen GetHackScreen()
{
	local UIScreenStack Stack;

	Stack = `SCREENSTACK;
	if (Stack == none)
		return none;

	return UIHackingScreen(Stack.GetScreen(class'UIHackingScreen'));
}

// ---------------------------------------------------------------------------
// Shared plumbing (mirrors X2AccessTacticalReader's)
// ---------------------------------------------------------------------------

// Joins with a full stop, EXCEPT after a part that already ends in one. Every reward
// Description is a complete sentence with its own terminator, so the naive join produced
// "получает маскировку.. Неудача" — audible as a stumble in most screen readers (heard live
// 2026-08-20).
private function AppendPart(out string Result, string Part)
{
	local string Tail;

	if (Part == "")
		return;

	if (Result != "")
	{
		Tail = Right(Result, 1);
		if (Tail == "." || Tail == "!" || Tail == "?")
			Result $= " ";
		else
			Result $= ". ";
	}

	Result $= Part;
}

private function AppendComma(out string Result, string Part)
{
	if (Part == "")
		return;
	if (Result != "")
		Result $= ", ";
	Result $= Part;
}
