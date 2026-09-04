#pragma once

// GameUI's PRIVATE toolbox — the SEH-guarded raw primitives, the screen-stack walkers, the
// bounded array reads and the outbound-call plumbing that game_ui.cpp implements and every
// game_ui_*.cpp family TU builds its readers out of.
//
// ⛔ WHO MAY INCLUDE THIS: the game_ui*.cpp family TUs and gui_dump.cpp — the TUs whose job is
// reading engine memory through explicit offsets. Recipes, feature TUs and everything else stay
// on the public headers (game_ui.h and its family splits): handles there are opaque void*, and
// that opacity is the interface. A new consumer of a raw primitive outside the reader layer is a
// design smell, not an include away.
//
// Everything declared here is defined in game_ui.cpp (the core TU) and is GAME THREAD ONLY, for
// the same reason the public API is: these read live engine memory and make outbound
// ProcessEvent calls.
//
// ⛔ Include this BEFORE any `#pragma pack(push, 4)` region in the including TU: ScreenFinder is
// defined here and must have the same (default) packing in every TU that sees it.

#include <cstdint>
#include <string>
#include <vector>

class UObject;
class UFunction;
class UField;

namespace GameUI {

// ---- generic UIPanel / UIButton / UIList field offsets ----
//
// Only the offsets MORE THAN ONE family TU needs live here; a screen-specific offset belongs in
// the one family TU that reads that screen, next to the reader it feeds. Provenance and pinning
// are unchanged: reflection-dump comments, validated live, static_asserts in
// sdk_layout_checks.cpp.
constexpr int32_t kPanelFlagsOffset = 0x284;    // UIPanel flag dword
constexpr uint32_t kPanelInited = 0x10;
constexpr uint32_t kPanelVisible = 0x20;
constexpr uint32_t kPanelFocused = 0x40;
// bIsRemoved. A removed screen KEEPS its inited/visible/focused bits — the engine never clears
// them — so this is the only flag that tells a corpse from the live screen.
constexpr uint32_t kPanelRemoved = 0x80;

constexpr int32_t kButtonTextOffset = 0x3D8;    // UIButton::Text (FString)
constexpr int32_t kButtonFlagsOffset = 0x400;   // UIButton flag dword
constexpr uint32_t kButtonDisabled = 0x01;      // IsDisabled
constexpr uint32_t kButtonAttention = 0x80;     // bNeedsAttention (the "!" badge)

constexpr int32_t kScrollTextOffset = 0x3D8;    // UIScrollingText::Text (FString)
constexpr int32_t kScrollHtmlOffset = 0x3E8;    // UIScrollingText::HtmlText (FString)

constexpr int32_t kListSelectedIndexOffset = 0x404; // UIList::SelectedIndex (int)

// ---- generic UIList / UIPanel traversal ----
constexpr int32_t kListItemContainerOffset = 0x3D8; // UIList::ItemContainer (UIPanel*)
constexpr int32_t kPanelChildPanelsOffset = 0x2E8;  // UIPanel::ChildPanels (TArray<UIPanel*>)

// UIListItemString — the plain text row several families read (room actions, commodity rows,
// covert-action rows, facility choices).
constexpr int32_t kListItemStringTextOffset = 0x3F4;  // UIListItemString::Text
constexpr int32_t kListItemStringFlagsOffset = 0x414; // bDisabled/bIsBad/bIsGood/bIsWarning
constexpr uint32_t kListItemDisabled = 0x1;
constexpr uint32_t kListItemBad = 0x2;
constexpr uint32_t kListItemGood = 0x4;
constexpr uint32_t kListItemWarning = 0x8;

// The Movie → PRES chain, walked by the tooltip plumbing (core), the world-message finder
// (tactical) and the Flash readers (shell, popup).
constexpr int32_t kPanelMovieOffset = 0x2B8;    // UIPanel::Movie (UIMovie*)
constexpr int32_t kMoviePresOffset = 0x204;     // UIMovie::PRES (XComPresentationLayerBase*)

// ---- modal dialog: UIDialogueBox ----
// Shared between the core tracked-screen table (DialogIsShowingRaw) and the shell reader.
constexpr int32_t kDialogDataOffset = 0x434;        // m_arrData (TArray<FTDialogueBoxData>)
constexpr int32_t kDialogAcceptOffset = 0x444;      // strAccept (FString) — the LIVE label
constexpr int32_t kDialogCancelOffset = 0x454;      // strCancel (FString)
constexpr int32_t kDlgDataStride = 0xD0;            // sizeof(FTDialogueBoxData)
constexpr int32_t kDlgDataTitleOffset = 0x08;       // FTDialogueBoxData::strTitle
constexpr int32_t kDlgDataTextOffset = 0x18;        // FTDialogueBoxData::strText

// ---- shared caps ----
constexpr int kMaxStackScreens = 32;
// The tallest of the generic UILists is an options tab; 64 is well past what any of them draws.
constexpr int kMaxListRows = 64;

// ---- raw, SEH-safe primitives (no C++ objects with destructors inside __try) ----

// The only WRITE primitive into engine memory. Everything else here reads; a write is reserved
// for a field the GAME itself treats as a plain cursor (the hero promotion screen's column and
// panel indices), never for state the engine derives or reference-counts.
void WriteI32Raw(void* obj, int32_t offset, int32_t value);

uint32_t ReadU32Raw(const void* obj, int32_t offset);
int32_t ReadI32Raw(const void* obj, int32_t offset);
uint8_t ReadU8Raw(const void* obj, int32_t offset);
float ReadF32Raw(const void* obj, int32_t offset);
void* ReadPtrRaw(const void* obj, int32_t offset);

// UE3 FString on Windows is TArray<wchar_t> despite the SDK declaring char:
// [wchar_t* data (0x00)] [int32 count (0x08)] [int32 max (0x0C)].
int CopyFStringRaw(const void* fstr, wchar_t* buf, int bufWchars);

// SEH-safe: the object's own ANSI name into `buf`; false on any fault or wide/missing entry.
bool ObjNameAnsiRaw(UObject* obj, char* buf, int bufLen);

// A CLASS object: the object named `className` whose own class is named "Class". Class
// objects persist for the whole session, so these resolve once.
UObject* FindClassRaw(const char* className);

// The name of an object's CLASS, for matching without having resolved a UClass pointer first.
bool ObjClassNameAnsiRaw(void* obj, char* buf, int bufLen);

// A live INSTANCE: class matches by POINTER; its name is the class's display name, which
// excludes the class default object ("Default__<name>") for free.
bool IsInstanceRaw(UObject* obj, UObject* classObj, const char* className);

// The instance of `classObj` that `usable` accepts. See game_ui.cpp for why "newest first" is
// only a tie-break and the predicate is what decides.
UObject* ScanInstanceRaw(UObject* classObj, const char* className, bool (*usable)(void*));

// The unique object named exactly `objName` (one full GObjects pass, oldest first) — how a CDO
// is found by its "Default__<Class>" name.
void* ScanForObjectNamedRaw(const char* objName);

// Inited + focused + not removed, straight off the UIPanel flag dword. bIsVisible is
// DELIBERATELY not tested — the game leaves it false on screens it is plainly showing (see the
// hard rule and the definition's comment).
bool IsFocusedRaw(void* screen);

// Inited + visible + not removed — the readiness test for a screen nobody focuses (the loading
// overlay is shown over whatever is transitioning out).
bool IsShownRaw(void* screen);

// Is `obj` an instance of `baseClass`, or of anything derived from it? Walks the instance's own
// class chain (bounded), so it needs no GObjects pass — the finder for a 22-subclass family.
bool IsKindOfRaw(void* obj, const char* baseClass);

// The object's class is named exactly `className` (no inheritance walk — used where the
// concrete class is known).
bool ObjectClassNamedRaw(void* obj, const char* className);

// SEH-safe: the ANSI text of the FName stored AT `addr` (entry id, then instance number), the
// way the engine prints it — instance N shows as "_N-1". Distinct from ObjNameAnsiRaw, which
// reads an OBJECT's own name.
bool FNameAtAnsiRaw(const void* addr, char* buf, int bufLen);

// Bumped by HintScreenChange; screen finders compare it to skip their rate limit right after a
// known screen change.
extern uint64_t gScanEpoch;

// The cached find-a-screen pattern shared by every screen finder: resolve the class object once,
// keep the newest USABLE instance, rescan GObjects at most once a second. See the definition in
// game_ui.cpp for the type-revalidation-is-not-enough story this design paid for.
struct ScreenFinder {
    const char* className;
    bool (*usable)(void*) = IsFocusedRaw;
    UObject* classObj = nullptr;
    void* cached = nullptr;
    uint64_t lastScanMs = 0;
    uint64_t scanEpoch = 0;
    int emptyScans = 0;

    void* Find();
};

// ---- plain array reads (no logging; -1 = the array itself could not be read) ----

// Any TArray<T*> at `offset`: copies up to maxItems pointers out, indices preserved. `clipped`,
// when asked for, says whether the array held MORE than maxItems.
int ReadPtrArrayRaw(const void* obj, int32_t offset, void** out, int maxItems,
    bool* clipped = nullptr);

// A TArray's own count at `offset`, without reading one element: -1 if it could not be read.
int32_t ReadArrayCountRaw(const void* obj, int32_t offset);

// Element `index` of a TArray<FString> at `offset` (16-byte elements).
int CopyFStringElemRaw(const void* obj, int32_t offset, int index, wchar_t* buf, int bufWchars);

// A UIList's items in index order (GetItem(i) IS ItemContainer.ChildPanels[i]).
int ReadListItemsRaw(void* list, void** out, int maxItems, bool* clipped = nullptr);

// A TArray<int32> at `offset`; same contract as ReadPtrArrayRaw.
int ReadIntArrayRaw(const void* obj, int32_t offset, int32_t* out, int maxItems,
    bool* clipped = nullptr);

// ---- bounded array reads that SAY SO when they clip ----
//
// ⛔ Size the buffer from the data, never the data to the buffer — and a cap that drops the tail
// must be LOUD. See the hard rule; `what` names the site, doubles as the once-per-site latch
// key, and must be a literal.

// Honour ReadPtrArrayRaw's -1 sentinel and log a dropped tail, once per site.
int NoteArrayRead(const char* what, int count, bool clipped, int maxItems);

// ReadPtrArrayRaw under that contract.
int TakeArrayRaw(const char* what, const void* obj, int32_t offset, void** out, int maxItems);

// ReadListItemsRaw under that contract.
int TakeListItemsRaw(const char* what, void* list, void** out, int maxItems);

// The WHOLE of a TArray<T*>, sized from the array's own count — for a list whose length the
// GAME decides and nothing bounds (the save list). An implausible count is refused with a line.
std::vector<void*> ReadPtrArrayAllRaw(const char* what, const void* obj, int32_t offset);

// ---- the game's own screen stack ----

// Screens, top first, into `out`; 0 when there is no live stack.
int WalkStackRaw(void** out, int max);

// ⛔ THE READINESS PREDICATE FOR A SCREEN ON THE STACK: bIsInited && !bIsRemoved, and NOT
// bIsVisible, and deliberately not bIsFocused either — position in the stack already answers
// "is it on top". See the definition's comment.
bool StackEntryLiveRaw(void* screen);

// The GetCurrentScreen rule: the TOPMOST non-null entry, and only when it matches and is live.
// A non-match on top is a REFUSAL, not a reason to keep looking. `matches` is handed the entry
// and its class name; `nameOut` receives that name when an entry is returned.
void* TopStackScreenRaw(bool (*matches)(void* screen, const char* className),
    char* nameOut = nullptr, int nameCap = 0);

// The topmost live screen of `className` anywhere on the stack, or null. Unlike
// TopStackScreenRaw this keeps LOOKING past a non-match.
void* FindStackScreenRaw(const char* className);

// Stack first, object scan second; the first time a class is only findable by the fallback it
// is logged once, so "not stack-managed" becomes a fact rather than an assumption.
void* PreferStack(const char* className, ScreenFinder& finder, bool& reportedFallback);

// ---- the shared tracked-screen lookup ----
//
// Every transient screen behind ONE rate-limited GObjects pass (independent finders would each
// scan once a second whenever their screen is absent, which is almost always). The table and
// its usability predicates live in game_ui.cpp; family TUs reach it through FindScreen.
enum TrackedScreen {
    kScrDifficulty,
    kScrIronMan,
    kScrNarrative,
    kScrSoundtrack,
    kScrDialog,
    kScrPauseMenu,
    kScrLoadScreen,
    kScrTacticalHUD,
    kScrTutorialBox,
    kScrArmoryLoadout,
    kScrSquadSelect,
    kScrAfterAction,
    kScrLoadGame,
    kScrSaveGame,
    kScrTextInput,
    kTrackedScreenCount,
};

// Stack first (FindStackScreenRaw), tracked-scan fallback — the same rule as PreferStack.
void* FindScreen(TrackedScreen which);

// The top entry of a dialog box's queue — m_arrData.Length - 1, which is where OnAccept /
// OnCancel and GetTopDialogBoxData look. Null when nothing is queued.
const uint8_t* TopDialogDataRaw(void* screen);

// Is the dialog box actually showing something? The box is PERMANENT — it exists from movie
// init and merely holds no data when idle — so this, not its presence, is the readiness test.
bool DialogIsShowingRaw(void* screen);

// ---- string conversion and FString field reads ----

std::wstring Utf8ToWide(const std::string& text);
std::string WcharToUtf8(const wchar_t* wbuf, int len);

// An FString field read straight to CLEANED UTF-8 (empty on fault or empty string).
std::string ReadFStringField(const void* obj, int32_t offset);

// The same read with NO cleaning, for a field that is an identifier rather than a phrase (a
// Flash path being matched, markup the caller must translate rather than drop). CleanText would
// strip <...> and collapse whitespace, so a cleaned value can never be compared against a raw
// one.
std::string ReadFStringFieldRaw(const void* obj, int32_t offset);

// ---- reading a Flash capture's operands by POSITION ----
//
// Every consumer of FlashShadow::Call::StringArgs() reads by index, because the capture's
// contract is positional (non-string operands are kept as empty slots so nothing after them
// shifts). These two are that read: an out-of-range index is an ordinary empty answer, not an
// error, since a panel legitimately pushes shorter argument lists on some branches.

// Operand `index`, cleaned. Empty when the call carried fewer operands than that.
std::string FlashArg(const std::vector<std::string>& args, size_t index);

// The same operand split into its lines, each cleaned, empties dropped. ⛔ Split FIRST, then
// clean: CleanText collapses whitespace, so cleaning the whole blob would fuse the paragraphs a
// "\n\n" break is there to separate.
std::vector<std::string> FlashArgLines(const std::vector<std::string>& args, size_t index);

// ---- generic widget → view-struct helpers ----
//
// MenuItem / OptionRow / OptionWidget are the public row shapes (game_ui_core.h, via the public
// umbrella the family TUs already include).

// One UIMechaListItem as an OptionRow. `index` is the row's position in its owning UIList, or
// -1 for rows parented to a plain panel. False = hidden or unlabelled; not a node.
struct OptionRow;
struct MenuItem;
bool MakeRow(void* item, int index, OptionRow* out);

// Every visible row of a UIList, indices preserved.
std::vector<OptionRow> ReadListRows(void* list);

// The same read sized from the list's own item array — for a list the GAME decides the length of
// (the character pool, a customization picker). `what` names the site for the refusal line.
std::vector<OptionRow> ReadListRowsAll(const char* what, void* list);

// A visible UIButton at `offset` on `screen` as a MenuItem; handle stays null when the button
// is absent, hidden, or unlabelled.
MenuItem ReadButtonField(void* screen, int32_t offset);

// UIList.SetSelectedIndex — the game's own row selection (fires the list's selection
// delegates, scrolls the view). -1 deselects.
bool SetListIndex(void* list, int index);

// ---- outbound calls ----

// Resolve `funcName` on the object's class chain (cached per class) and ProcessEvent it.
// `params` must match the UFunction's parameter layout, hand-rolled at pack(4).
bool CallNamedFunction(void* obj, const char* funcName, void* params);

// A `static native` script function, called on the CLASS object itself. See the definition for
// why FUNC_Native is never cleared around the call.
bool CallStaticNative(const char* className, const char* funcName, void* params);

// ---- GFxMoviePlayer path-API calls (the verification tool — see the Flash hard rule) ----
//
// The parameter block EVERY path function takes: an FString in at 0x00, the answer at 0x10
// (pinned in sdk_layout_checks.cpp).
constexpr int32_t kFlashRetOffset = 0x10;

bool FlashCallRaw(void* movie, const char* funcName, const std::string& path, uint8_t* block,
    size_t blockSize);

// Invoke or read a path that answers with a string. Empty on any failure — including AS2's own
// "null", which is what Selection.getFocus() says when no field has focus.
std::wstring FlashString(void* movie, const char* funcName, const std::string& path);

// ... and one that answers with an int. `fallback` is returned on a failed call.
int FlashInt(void* movie, const char* funcName, const std::string& path, int fallback);

} // namespace GameUI
