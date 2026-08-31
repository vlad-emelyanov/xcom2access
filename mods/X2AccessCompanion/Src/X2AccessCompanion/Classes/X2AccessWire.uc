// The wire format's UnrealScript half — the one implementation of the record and field
// appenders every CDO reader used to carry its own copy of (thirteen Emit/Push bodies in two
// spellings, five AddFields, seventeen separator declarations).
//
// The GRAMMAR's authority is the native parser, src/wire_record.hpp: records joined by REC_SEP,
// fields within a record by FLD_SEP, each field key=value, separators PRINTABLE on purpose (the
// same reason X2AccessSaveReader uses Chr(10) rather than "\n"). The consts here are that
// grammar's script-side declaration; readers that reference a separator inline still declare a
// local copy of the same literal, because an UnrealScript const cannot alias another class's —
// a reader's local pair must match these two characters for characters.
//
// Storage cannot move here: native reads NativeResult off the CDO it called, so the accumulator
// stays a property of each reader class and everything below is `static` over `out string` —
// the same pattern X2AccessHelper.AppendJoined proves works from a CDO.
class X2AccessWire extends Object abstract;

const REC_SEP = "~|~";
const FLD_SEP = "~:~";

// Append one finished record to a NativeResult accumulator.
static function AddRecord(out string Result, string Rec)
{
	if (Result != "")
		Result $= REC_SEP;
	Result $= Rec;
}

// Append key=value to a record. Empty values are DROPPED rather than written as an empty field:
// a missing key means "the game is not showing this", which is exactly the distinction the
// recipes need — and what keeps an empty label from voicing a bare separator. Markup is left
// in: native runs every value through GameUI::CleanText on the way out, so there is exactly one
// implementation of "strip the HTML the game colours its text with".
static function AddField(out string Rec, string Key, string Value)
{
	if (Value == "")
		return;
	Rec $= FLD_SEP $ Key $ "=" $ Value;
}

// ⛔ The POSITIONAL variant: emits the field even when empty. A repeating field (a card's util
// slots) is decoded by position, so dropping an empty one shifts every value after it — an
// empty pocket in front of a grenade made the grenade answer for the pocket (squad select,
// first live pass). Emit the blank and let native decide what to say about it.
static function AddPositionalField(out string Rec, string Key, string Value)
{
	Rec $= FLD_SEP $ Key $ "=" $ Value;
}
