// X2AccessSaveReader
// The save and load screens (UISaveGame, UILoadGame) — the words half. Same contract as
// X2AccessSquadReader and X2AccessTacticalReader: the C++ layer owns the gate, the keys and the
// speech, calls DescribeSaves() on this class's default object via ProcessEvent (game thread,
// from the graph recipe's build) and reads NativeResult back through reflection right after the
// call returns. Nothing here runs on its own — no listeners, no timers, no polling.
//
// WHY THE WORDS LIVE HERE. UISaveLoadGameListItem.UpdateData resolves the date, the name and the
// mission into LOCALS, pushes them into Flash through one Invoke("updateData", ...) and retains
// none of them. What it DOES retain is the OnlineSaveGame struct it was built from — literally
// the data Flash was told — so every line below is re-derived from that struct exactly the way
// UpdateData derived it. Not one XCOMHISTORY access, and no ONLINEEVENTMGR.GetSaveGames
// re-query: the screen already asked, and asking again could answer differently.
//
// The one place that is not a pure re-format is the save screen's row 0, and it is not an
// exception: UpdateData itself calls FillInHeaderForSave for a row whose Filename is empty, so
// the "new save" row really does show the current date and the name the save is about to get.
// That call walks the history, which is why native caches these words on the ROW POINTERS (they
// change only when BuildMenu respawns the list) instead of re-asking ten times a second.
//
// ⛔ Chr(10), never "\n". UnrealScript string literals are not guaranteed to process escapes in
// this codebase's toolchain (see X2AccessNarrator's ENTRY_SEP note), and every separator here
// has to match a REAL newline the engine wrote into Header.Description.
//
// GC rule: this object is a CDO — value-typed state only, NEVER object references.

class X2AccessSaveReader extends Object;

// Written by every entry point; read by native immediately after the ProcessEvent returns.
var string NativeResult;

// The wire format: records joined by REC_SEP, fields within a record by FLD_SEP, each field
// key=value. PRINTABLE separators, matching X2AccessSquadReader's.
const REC_SEP = "~|~";
const FLD_SEP = "~:~";

// ---------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------

// One record per row of the screen's own m_arrListItems, in its own order. Each carries the
// row's INDEX rather than relying on array position: native walks the same array but skips
// entries that came back null, and matching on the field the row itself stamps means the two
// halves cannot drift apart over a hole in the list.
function DescribeSaves()
{
	local UIScreenStack Stack;
	local UISaveGame SaveScreen;
	local UILoadGame LoadScreen;
	local int i;

	NativeResult = "";

	Stack = `SCREENSTACK;
	if (Stack == none)
		return;

	// Save first, matching native's FindSaveLoadScreen. The two never coexist — the save screen
	// is reachable only from the pause menu, and the load screen pops it.
	SaveScreen = UISaveGame(Stack.GetFirstInstanceOf(class'UISaveGame'));
	if (SaveScreen != none)
	{
		for (i = 0; i < SaveScreen.m_arrListItems.Length; ++i)
			AppendRow(SaveScreen.m_arrListItems[i]);
		return;
	}

	LoadScreen = UILoadGame(Stack.GetFirstInstanceOf(class'UILoadGame'));
	if (LoadScreen != none)
	{
		for (i = 0; i < LoadScreen.m_arrListItems.Length; ++i)
			AppendRow(LoadScreen.m_arrListItems[i]);
	}
}

// ---------------------------------------------------------------------------------------
// One row
// ---------------------------------------------------------------------------------------

private function AppendRow(UISaveLoadGameListItem Row)
{
	local string Rec;

	if (Row == none)
		return;

	Rec = "index=" $ Row.Index;
	DescribeSave(Row, Rec);

	if (NativeResult != "")
		NativeResult $= REC_SEP;
	NativeResult $= Rec;
}

// Mirrors UISaveLoadGameListItem.UpdateData line for line: same source struct, same split, same
// three strings, same error-report fallback. Anything this says differently from the row is a
// bug in this function, not a design choice.
private function DescribeSave(UISaveLoadGameListItem Row, out string Rec)
{
	local SaveGameHeader Header;
	local string FriendlyName, NewLine, StrTime, StrDesc, StrName, StrMission;
	local array<string> Descriptions;

	NewLine = Chr(10);

	if (Row.SaveGame.Filename == "")
	{
		// The "new save" row. UpdateData fills a header for it too, so it shows the current
		// time and the description the save is about to be written with.
		`ONLINEEVENTMGR.FillInHeaderForSave(Header, FriendlyName);
		AddField(Rec, "new", "1");
	}
	else if (Row.SaveGame.SaveGames.Length > 0)
	{
		Header = Row.SaveGame.SaveGames[0].SaveGameHeader;
	}
	else
	{
		return; // no header to read: native falls back to the row's own button label
	}

	Descriptions = SplitString(Header.Description, NewLine);

	// For old save files that used "-" — UpdateData's own comment and its own fallback.
	if (Descriptions.Length < 2)
		Descriptions = SplitString(Header.Description, "-");

	if (Descriptions.Length < 4)
	{
		// The "custom" description shape the error reports write. The row draws it as one line
		// and flips its error-report flag; there is nothing else in it to name.
		StrDesc = Repl(Header.Time, NewLine, " - ") @ Header.Description;
	}
	else
	{
		// UpdateData concatenates the timestamp and the save description into ONE line
		// (`FormatTime(Header.Time) @ Descriptions[2]`). They are emitted separately here so the
		// recipe can put the NAME first and the timestamp last — which is a speech decision, not
		// a parity one: a sighted player takes the whole row in at a glance, while speech is
		// serial, and the name is what tells two saves apart.
		StrTime = FormatTime(Header.Time);
		StrDesc = Descriptions[2];
		StrName = Descriptions[3];
		StrMission = Descriptions.Length >= 5 ? Descriptions[4] : "";
		if (Descriptions.Length >= 6)
			StrMission $= " - " $ Descriptions[5];
	}

	AddField(Rec, "desc", StrDesc);
	AddField(Rec, "name", StrName);
	AddField(Rec, "mission", StrMission);
	AddField(Rec, "time", StrTime);
}

// UISaveLoadGameListItem.FormatTime, unchanged. The 12-hour conversion is a NATIVE function and
// is the whole reason this formatting is not done in C++: without it an English player's screen
// would say 2:22 PM while the speech said 14:22.
private function string FormatTime(string HeaderTime)
{
	local string Formatted;

	Formatted = HeaderTime;
	if (GetLanguage() == "INT")
		Formatted = class'XComOnlineEventMgr'.static.FormatTimeStampFor12HourClock(Formatted);

	return Repl(Formatted, Chr(10), " - ");
}

// ---------------------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------------------

// Empty values are DROPPED rather than written as an empty field: a missing key means "the row
// is not showing this", which is exactly the distinction the recipe needs.
private function AddField(out string Rec, string Key, string Value)
{
	class'X2AccessWire'.static.AddField(Rec, Key, Value);
}
