// X2AccessNarrator
// Bridge v2's script half: a CHANNELLED, QUEUED narration path from UnrealScript to the ASI.
//
// The v1 bridge (a UIScreenListener writes AccessibilityData, then fires the empty X2Access_Speak
// sentinel on a timer) carries exactly one line and no priority. That is fine for a focus change
// and useless for combat: a burst of events overwrites itself before native ever sees it.
// Tactical narration needs a QUEUE — damage, kills and status are spoken in order, and turn
// changes and squad-level alerts ride the same queue so a burst stays coherent. Nothing on this
// path interrupts: native hands every line to the screen reader as queued text, and whether it
// cuts in is the reader's own decision.
//
// Transport is one delimited string rather than a dynamic array, for two reasons: C++ drains a
// whole burst in ONE outbound ProcessEvent (the call is the expensive part, not the payload), and
// mutating a dynamic array through `default.` from a static function is a pattern the shipped SDK
// sources use nowhere — a plain string append is unambiguous.
//
// Producers only ever call the two static entry points, so a listener never has to know how the
// native side reads this. Native only ever calls DrainNarration().
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

// `config` is not a preference here, it is the only way to hold state: the UnrealScript compiler
// rejects `default.X = ...` outright unless X is a config property ("Can't change default values
// of non-config properties"), and a static producer has no other route to per-class storage. No
// XComGame.ini section is shipped for this class, so every value below starts at its declared
// default each launch and nothing is ever written back (SaveConfig is never called).
class X2AccessNarrator extends Object config(Game);

// Channel marker: the FIRST character of an entry, stripped by native before speaking.
const CHANNEL_ALERT = "A";   // the game changed what you can DO: turn change, concealment broken, your soldier down
const CHANNEL_COMBAT = "C";  // queued: hits, damage, kills, status

// Entry separator. Three characters rather than one, none of which occur in localized game text
// (an alien name or a rank abbreviation may well contain a pipe or a tilde on its own).
const ENTRY_SEP = "~|~";

// How many entries may pile up before we start dropping. Native drains ~10 times a second, so
// reaching this means one game state produced two dozen narratable events — a big AoE, or a Lost
// horde resolving. The drop is REPORTED on the next drain, never silent.
const MAX_PENDING = 24;

// The pending burst, ENTRY_SEP-joined. Written by Push, cleared by DrainNarration.
var config string PendingText;
var config int PendingCount;
var config int DroppedCount;

// Written by DrainNarration; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// ---------------------------------------------------------------------------
// Producer side — what listeners call
// ---------------------------------------------------------------------------

// Something the player must hear NOW: it cuts whatever is being spoken. Reserve this for events
// that change what the player can DO (turn handover, concealment, a soldier going down).
static function Alert(string Text)
{
	Push(CHANNEL_ALERT, Text);
}

// Something that happened. Spoken in order behind whatever is already queued — the spoken
// equivalent of a flyover, which a sighted player also reads at their own pace.
static function Combat(string Text)
{
	Push(CHANNEL_COMBAT, Text);
}

static function Push(string Channel, string Text)
{
	if (Text == "")
		return;

	if (default.PendingCount >= MAX_PENDING)
	{
		default.DroppedCount = default.DroppedCount + 1;
		return;
	}

	if (default.PendingText != "")
		default.PendingText = default.PendingText $ ENTRY_SEP;

	default.PendingText = default.PendingText $ Channel $ Text;
	default.PendingCount = default.PendingCount + 1;
}

// ---------------------------------------------------------------------------
// Native side — one call per drain
// ---------------------------------------------------------------------------

// Hand the whole pending burst over and reset. An empty NativeResult means "nothing happened",
// which is the normal case on most drains.
function DrainNarration()
{
	NativeResult = default.PendingText;

	// A dropped burst is a fact about the mod, not about the game, so it goes out on the alert
	// channel: the player should know narration was lost rather than silently assume they heard
	// everything.
	if (default.DroppedCount > 0)
	{
		if (NativeResult != "")
			NativeResult = NativeResult $ ENTRY_SEP;
		NativeResult = NativeResult $ CHANNEL_ALERT $ string(default.DroppedCount) @ "narration lines dropped";
		default.DroppedCount = 0;
	}

	default.PendingText = "";
	default.PendingCount = 0;
}

// Throw away anything queued. Native calls this when the tactical layer goes away, so lines from
// a finished mission cannot be spoken over the mission summary.
function ClearNarration()
{
	default.PendingText = "";
	default.PendingCount = 0;
	default.DroppedCount = 0;
	NativeResult = "";
}
