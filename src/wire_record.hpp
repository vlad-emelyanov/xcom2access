#pragma once

// The companion wire format: the record grammar every `X2Access*Reader.uc` writes into its CDO's
// `NativeResult` and every recipe reads back out.
//
// STL-only by the same contract `src/Graph/` holds to — no UE3, no generated SDK, no windows.h.
// That is the whole point of it having its own translation unit: the parse carries a ⛔ hard rule
// (an empty VALUE is kept, so positional repeating fields cannot shift) and until it lived here
// that rule was pinned by nothing, because `menu_navigator.cpp` includes windows.h and cannot be
// compiled by `tests/`. Text CLEANING stays out for the same reason and comes in as a parameter.

#include <string>
#include <utility>
#include <vector>

namespace X2Access::Wire
{

// The grammar, declared once on this side. The companion's producers declare the same two
// literals (`REC_SEP` / `FLD_SEP` in each `X2Access*Reader.uc`); changing either is changing both.
// Not per-screen despite what the old name said — every wire-backed recipe uses this pair.
inline constexpr const char* kRecordSep = "~|~";
inline constexpr const char* kFieldSep = "~:~";

/// One record, as ORDERED key/value pairs — ordered because some keys REPEAT (squad select's
/// `util`, once per utility slot; the personnel table's `cell`) and the order is the row order.
using WireRecord = std::vector<std::pair<std::string, std::string>>;

/// The first value stored under `key`, or "" when the record does not carry it.
std::string RecField(const WireRecord& rec, const char* key);

/// Every value stored under `key`, in emission order — the read for a repeating field.
std::vector<std::string> RecFieldAll(const WireRecord& rec, const char* key);

/// Split `text` on the literal `sep`. Always yields at least one part (an empty input is one
/// empty part) and never collapses adjacent separators: an empty run between two of them is a
/// part in its own right, which is what keeps a positional field positional.
std::vector<std::string> SplitOn(const std::string& text, const char* sep);

/// The transform applied to every VALUE as it is parsed. The host passes `GameUI::CleanText` —
/// one implementation of "strip the markup the game colours its text with", shared by every
/// screen; tests pass nothing. It is a parameter rather than a call inside this file because
/// CleanText lives in the engine-side offset TU, and this module's value is that it compiles
/// with no engine header in sight.
using FieldCleaner = std::string (*)(const std::string&);

/// Parse a whole `NativeResult` payload into records.
///
/// ⛔ A field with no `=`, or with an EMPTY KEY, is malformed and skipped — but a field with an
/// empty VALUE is KEPT. CLAUDE.md's rule: "a REPEATING field (`util`, a table's `cell=`) is
/// emitted including its empty values, because dropping one shifts every value after it". Four
/// of squad select's five first-run defects were that one bug in different clothes.
///
/// ⚠ The format has no escaping: a value that itself contains `~:~` or `~|~` splits the record.
/// No producer emits one today; nothing here can detect it if one starts.
std::vector<WireRecord> ParseRecords(const std::string& raw, FieldCleaner clean = nullptr);

} // namespace X2Access::Wire
