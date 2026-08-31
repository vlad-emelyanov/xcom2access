// The AVENGER family's readers: the ant-farm gate, the rooms, choose-facility, the commodity
// and personnel families, and the research screens. Split out of game_ui.cpp along the
// recipe-family axis; the raw primitives stay in game_ui.cpp behind game_ui_internal.h.

#include "game_ui_avenger.h"
#include "game_ui_internal.h"
#include "log.h"
#include "flash_shadow.h"

#include <cstring>
#include <string>

// Hand-rolled ProcessEvent parameter blocks below must match the engine's pack(4) layout.
#ifdef _MSC_VER
#pragma pack(push, 0x4)
#endif

namespace GameUI {

AvengerGate ReadAvengerGate()
{
    AvengerGate gate;
    void* screens[kMaxStackScreens] = {};
    int count = WalkStackRaw(screens, kMaxStackScreens);

    // GetCurrentScreen is the FIRST non-none entry — it applies no inited/removed filter of its
    // own, so neither do we. Filtering here would let us call the grid "current" on a frame the
    // game's own OnUnrealCommand would refuse, which is the one disagreement that matters.
    bool sawTop = false;
    for (int i = 0; i < count; i++) {
        if (!screens[i])
            continue;
        char name[64];
        if (!ObjClassNameAnsiRaw(screens[i], name, sizeof(name)))
            continue;
        if (strcmp(name, "UIFacilityGrid") == 0) {
            gate.gridOnStack = true;
            gate.current = !sawTop;
            if (sawTop)
                break;
            // Still refuse a grid that is on the stack but torn down: the game reaches it
            // through GetCurrentScreen either way, but a removed screen's navigator is not
            // something to narrate.
            if (!StackEntryLiveRaw(screens[i]))
                gate.current = false;
            break;
        }
        // ⛔ ONE screen may sit on top of the grid without blocking it: UIBuildFacilities.
        // Its OnUnrealCommand ends by forwarding every unhandled key to
        // `HQPRES.m_kFacilityGrid.Navigator.OnUnrealCommand — so in build mode the arrows still
        // drive the ant farm, and treating it as a blocker went silent exactly where a player is
        // choosing a room to excavate or build in. This is the game's own delegation, read off
        // its own source, not an exception we invented.
        if (!sawTop && strcmp(name, "UIBuildFacilities") == 0) {
            gate.buildMode = true;
            continue;
        }
        if (!sawTop) {
            sawTop = true;
            strncpy_s(gate.blocker, name, _TRUNCATE);
        }
    }
    return gate;
}

// ---- inside a room (UIFacility and its 22 subclasses) ----
//
// The room SCREEN is not the room's content, and that is the whole shape of this reader.
// UIFacility carries a staff strip, optional room-function buttons and an upgrade button — and
// on the seven FIXED rooms it carries none of them: CommandersQuarters, CIC, PowerCore
// (Research), Hangar (Armory), LivingQuarters, BarMemorial and Storage (Engineering) each
// declare ZERO StaffSlotDefs, so their screens are empty and their navigators hold nothing.
// Measured live 2026-08-21 in the Research lab: 3 panels total, staff container hidden.
//
// The content is one screen DOWN. UIFacility.OnUnrealCommand returns false when it has nothing,
// and UIScreenStack.OnInput walks LIFO, so the keys fall through to UIAvengerHUD →
// Shortcuts.OnUnrealCommand → List. Entering a room auto-opens that list for the room's own
// category (SelectCategoryForFacilityScreen → ShowList → List.SetSelectedNavigation), which is
// where the Armory's "Soldiers / Recruit / Memorial" and the Lab's "New Research / View
// Scientists" actually live. Confirmed live 2026-08-21: two Downs moved List.SelectedIndex
// 2 → 0 → 1 with ItemCount 4 (three rows plus the padding row) — Next with wrap, padding skipped.
//
// Everything here is a RETAINED widget read: no XCOMHISTORY, no Flash. FacilityHeader keeps
// LocationField/RoomField, the staff container keeps Title/Icon/Skill, and each row keeps its
// own localized Text. Verified live: RoomField="Исследовательский отсек",
// Text="Показать ученых", Title="УЧЕНЫЕ", Skill="0".

namespace {

constexpr int32_t kAvengerHudShortcutsOffset = 0x43C;      // UIAvengerHUD::Shortcuts
constexpr int32_t kAvengerHudFacilityHeaderOffset = 0x454; // UIAvengerHUD::FacilityHeader
constexpr int32_t kScreenHeaderLocationOffset = 0x3D8;     // UIX2ScreenHeader::LocationField
constexpr int32_t kScreenHeaderRoomOffset = 0x3E8;         // UIX2ScreenHeader::RoomField
constexpr int32_t kShortcutsListOffset = 0x7F8;            // UIAvengerShortcuts::List
constexpr int32_t kShortcutsCategoryLabelsOffset = 0x3F0;  // ::CategoryLabels[5], stride 0x10
constexpr int32_t kShortcutsCurrentCategoryOffset = 0x83C; // ::CurrentCategory
constexpr int32_t kFacilityStaffContainerOffset = 0x608;   // UIFacility::m_kStaffSlotContainer
constexpr int32_t kFacilityRoomContainerOffset = 0x610;    // UIFacility::m_kRoomContainer
constexpr int32_t kFacilityUpgradeButtonOffset = 0x648;    // UIFacility::UpgradeButton
constexpr int32_t kRoomContainerFuncsOffset = 0x3DC;       // UIRoomContainer::RoomFuncs
constexpr int32_t kStaffContainerTitleOffset = 0x3D8;      // UIStaffContainer::Title
constexpr int32_t kStaffContainerSkillOffset = 0x408;      // UIStaffContainer::Skill
constexpr int32_t kStaffContainerSlotsOffset = 0x418;      // UIStaffContainer::StaffSlots

constexpr int kShortcutCategoryCount = 5;
constexpr int kMaxRoomActions = 16;
// Every shipped facility adds exactly one; the headroom is for a mod that adds more.
constexpr int kMaxRoomFuncs = 8;
// UIStaffContainer::StaffSlots. The shipped rooms top out at four; a staff-slot mod could add.
constexpr int kMaxStaffSlots = 16;

} // namespace

void* FindRoomScreen()
{
    // Topmost non-null entry only — the same GetCurrentScreen definition ReadAvengerGate uses.
    // A UIFacility further down the stack is one the player has navigated AWAY from (the
    // facility summary and the choose-research screen both push over it), and narrating that
    // would name the wrong room.
    return TopStackScreenRaw([](void* screen, const char*) {
        return IsKindOfRaw(screen, "UIFacility");
    });
}

RoomView ReadRoom(void* screen)
{
    RoomView v;
    if (!screen)
        return v;
    v.facility = screen;

    // The header and the shortcut bar both live on UIAvengerHUD, which sits UNDER the room
    // screen and under the grid. That is not a layering accident to route around — it is where
    // the game itself keeps the room's name and the room's menu.
    void* hud = FindStackScreenRaw("UIAvengerHUD");
    if (hud) {
        void* header = ReadPtrRaw(hud, kAvengerHudFacilityHeaderOffset);
        if (header) {
            v.location = ReadFStringField(header, kScreenHeaderLocationOffset);
            v.room = ReadFStringField(header, kScreenHeaderRoomOffset);
        }
        v.shortcuts = ReadPtrRaw(hud, kAvengerHudShortcutsOffset);
    }

    if (v.shortcuts) {
        v.list = ReadPtrRaw(v.shortcuts, kShortcutsListOffset);
        int cat = ReadI32Raw(v.shortcuts, kShortcutsCurrentCategoryOffset);
        // -1 is a room the shortcut bar does not carry (Living Quarters, the Bar). Legitimate,
        // and the reason the empty case names the room rather than reporting a fault.
        if (cat >= 0 && cat < kShortcutCategoryCount)
            v.category
                = ReadFStringField(v.shortcuts, kShortcutsCategoryLabelsOffset + cat * 0x10);
    }

    void* staff = ReadPtrRaw(screen, kFacilityStaffContainerOffset);
    if (staff) {
        uint32_t flags = ReadU32Raw(staff, kPanelFlagsOffset);
        // The one place a child widget's bIsVisible IS the right test (the CLAUDE.md exception
        // for children): UIFacilityStaffContainer.Refresh calls Hide() deliberately when the
        // facility has no slots, and Show() when at least one is visible. On the fixed rooms it
        // is hidden and still carries a title and a crew count, which are worth speaking.
        v.staffVisible = (flags & kPanelVisible) != 0;
        v.staffTitle = ReadFStringField(staff, kStaffContainerTitleOffset);
        v.staffSkill = ReadFStringField(staff, kStaffContainerSkillOffset);
        void* slots[kMaxStaffSlots] = {};
        v.staffSlots = TakeArrayRaw("Room: staff slots", staff, kStaffContainerSlotsOffset, slots,
            kMaxStaffSlots);
    }

    // The room's OWN button bar. Spawned lazily by AddFacilityButton, so a room with no buttons
    // has a null container rather than an empty one — which is the ordinary case, not a fault.
    void* roomContainer = ReadPtrRaw(screen, kFacilityRoomContainerOffset);
    if (roomContainer) {
        void* funcs[kMaxRoomFuncs] = {};
        int funcCount = TakeArrayRaw("Room: facility buttons", roomContainer,
            kRoomContainerFuncsOffset, funcs, kMaxRoomFuncs);
        for (int i = 0; i < funcCount; i++) {
            if (!funcs[i])
                continue;
            RoomFunc f;
            f.handle = funcs[i];
            f.index = i;
            v.funcs.push_back(f);
        }
    }

    // Only while the game is SHOWING it: ShowUpgradeButton spawns the button once and then
    // Hide()s it on every OnLoseFocus, so the pointer outlives the affordance. A child widget's
    // bIsVisible is the documented exception to the never-gate-on-bIsVisible rule, and this is
    // it — the game toggles this one deliberately.
    void* upgrade = ReadPtrRaw(screen, kFacilityUpgradeButtonOffset);
    if (upgrade && (ReadU32Raw(upgrade, kPanelFlagsOffset) & kPanelVisible)) {
        v.upgrade = upgrade;
        v.upgradeLabel = CleanText(ReadFStringField(upgrade, kButtonTextOffset));
        v.upgradeDisabled = (ReadU32Raw(upgrade, kButtonFlagsOffset) & kButtonDisabled) != 0;
    }

    if (v.list) {
        v.selected = ReadI32Raw(v.list, kListSelectedIndexOffset);
        void* rows[kMaxRoomActions] = {};
        int rowCount = TakeListItemsRaw("Room: action rows", v.list, rows, kMaxRoomActions);
        for (int i = 0; i < rowCount; i++) {
            if (!rows[i])
                continue;
            RoomAction a;
            a.label = ReadFStringField(rows[i], kListItemStringTextOffset);
            // DoShowList appends a plain UIPanel as bottom padding, with DisableNavigation and
            // no text. Skipping it by its EMPTINESS rather than by its position keeps every
            // remaining index equal to the game's, which is what SetSelectedIndex and the
            // OnItemClicked delegates are expressed in.
            if (a.label.empty())
                continue;
            a.handle = rows[i];
            a.index = i;
            uint32_t rowFlags = ReadU32Raw(rows[i], kListItemStringFlagsOffset);
            a.disabled = (rowFlags & kListItemDisabled) != 0;
            a.urgent = (rowFlags & kListItemBad) != 0;
            a.warning = (rowFlags & kListItemWarning) != 0;
            v.actions.push_back(std::move(a));
        }
    }
    return v;
}

bool SetRoomActionSelection(const RoomView& view, int index)
{
    return SetListIndex(view.list, index);
}

bool ActivateRoomAction(const RoomView& view, const RoomAction& action)
{
    if (!view.list || action.disabled)
        return false;
    // Select, then accept — and the selection step is load-bearing for the same reason it is on
    // the save rows: UIList.OnUnrealCommand fires OnItemClicked(self, SelectedIndex), ignoring
    // whatever row the caller had in mind. A stale render would otherwise open the wrong screen.
    SetListIndex(view.list, action.index);
    return SendRowCommand(view.list, kCmdEnter);
}

bool ActivateRoomFunc(const RoomView& view, int index)
{
    for (const RoomFunc& f : view.funcs) {
        if (f.index != index)
            continue;
        // ⛔ FXS_BUTTON_A, not Enter. UIFacility_RoomFunc.OnUnrealCommand tests that one
        // constant and falls through to UIPanel's native handler for everything else, so the
        // keyboard constants reach nothing here — which is exactly why these buttons are
        // mouse-or-gamepad in vanilla. Sending the game its own controller command runs its own
        // delegate, with every side effect (sound, screen push, camera) the game expects.
        return SendRowCommand(f.handle, kCmdButtonA);
    }
    return false;
}

// ---- an un-excavated room (UIRoom) — assign the engineer, clear the debris ----
//
// A different class from UIFacility and a different problem. A room with no facility gets
// UIRoom: BUILD slots (Room.BuildSlots, not Facility.StaffSlots), a status panel and an
// excavate button. ⛔ BOTH halves of the player's task dead-end on mouse+keyboard in vanilla:
//
//   * ExcavateButton calls DisableNavigation(), so no keyboard can focus it, and the thing Enter
//     and Space DO reach — UIRoom.OnAccept — is an empty function body. StartExcavation is wired
//     only to FXS_BUTTON_X. Clicking is the only way to start clearing a room.
//   * The staff slots move on FXS_DPAD_LEFT/RIGHT; the ARROW constants are not in that switch.
//   * ExcavationPanel calls DisableNavigation() too, so the status, the time estimate and the
//     loot are unreachable text even for a sighted keyboard player.
//
// So the recipe here is not narration over a working screen — it is what makes the screen
// operable. Native reads STRUCTURE (which widgets exist, which are visible, which are refused);
// X2AccessRoomReader supplies every word, because this screen retains almost none of them: the
// status panel is a pure Flash pipe, UIStaffSlot keeps only StaffSlotRef, and the drop-down's
// rows call AS_UpdateData directly, bypassing UIButton.SetText — so even the retained Text is
// empty on them.

namespace {

constexpr int32_t kRoomStaffContainerOffset = 0x608;  // UIRoom::m_kStaffSlotContainer
constexpr int32_t kRoomExcavateButtonOffset = 0x628;  // UIRoom::ExcavateButton
constexpr int32_t kStaffContainerDropDownOffset = 0x428; // UIStaffContainer::m_kPersonnelDropDown
constexpr int32_t kDropDownListOffset = 0x3E0;        // UIPersonnel_DropDown::m_kList
constexpr int kMaxBuildSlots = 8;

} // namespace

void* FindRoomBuildScreen()
{
    // Topmost non-null entry, the GetCurrentScreen rule again — and UIRoom's own OnUnrealCommand
    // is unguarded, so anything above it has already taken the keys.
    return TopStackScreenRaw([](void*, const char* className) {
        return strcmp(className, "UIRoom") == 0;
    });
}

RoomBuildView ReadRoomBuild(void* screen)
{
    RoomBuildView v;
    if (!screen)
        return v;
    v.screen = screen;

    void* staff = ReadPtrRaw(screen, kRoomStaffContainerOffset);
    if (staff) {
        void* slots[kMaxBuildSlots] = {};
        int n = TakeArrayRaw("RoomBuild: staff slots", staff, kStaffContainerSlotsOffset, slots,
            kMaxBuildSlots);
        for (int i = 0; i < n; i++) {
            if (slots[i])
                v.slots.push_back(slots[i]);
        }

        // The picker is a PANEL on the container, not a screen — it never reaches the screen
        // stack, so its visibility is the only thing that says whether it owns the keys. This is
        // the child-widget case where bIsVisible is meaningful: ShowDropDown/CloseMenu toggle it
        // deliberately, and UIStaffContainer.OnUnrealCommand routes input on exactly this test.
        void* drop = ReadPtrRaw(staff, kStaffContainerDropDownOffset);
        if (drop && (ReadU32Raw(drop, kPanelFlagsOffset) & kPanelVisible)) {
            v.dropDown = drop;
            v.dropList = ReadPtrRaw(drop, kDropDownListOffset);
            if (v.dropList)
                v.dropSelected = ReadI32Raw(v.dropList, kListSelectedIndexOffset);
        }
    }

    void* excavate = ReadPtrRaw(screen, kRoomExcavateButtonOffset);
    if (excavate && (ReadU32Raw(excavate, kPanelFlagsOffset) & kPanelVisible)) {
        v.excavate = excavate;
        v.excavateDisabled = (ReadU32Raw(excavate, kButtonFlagsOffset) & kButtonDisabled) != 0;
    }
    return v;
}

bool OpenStaffSlotPicker(void* slotWidget)
{
    if (!slotWidget)
        return false;
    // The widget's own HandleClick, which is what both the mouse path and UIStaffSlot's Enter
    // reach. It branches on IsLocked() to offer the facility upgrade instead of the picker, so
    // calling it rather than ShowDropDown keeps the locked case behaving as the game intends.
    int64_t zeros[2] = {};
    return CallNamedFunction(slotWidget, "HandleClick", zeros);
}

bool SetStaffPickerSelection(const RoomBuildView& view, int index)
{
    return SetListIndex(view.dropList, index);
}

bool ChooseStaffCandidate(const RoomBuildView& view, int index)
{
    if (!view.dropList)
        return false;
    // Same select-then-Enter shape as the room menu, for the same reason: UIList's accept path
    // fires OnItemClicked(self, SelectedIndex) — here UIPersonnel_DropDown.OnStaffSelected,
    // which indexes m_staff by that number — so the selection is the argument.
    SetListIndex(view.dropList, index);
    return SendRowCommand(view.dropList, kCmdEnter);
}

bool StartRoomExcavation(const RoomBuildView& view)
{
    if (!view.excavate || view.excavateDisabled)
        return false;
    // The button's own Click, i.e. the mouse path, which fires the StartExcavation delegate.
    // ⛔ Do NOT reach for UIRoom.OnAccept: it is an empty function. And do not call
    // StartClearRoom on the room state directly either — StartExcavation also closes the screen,
    // and a room left open over a started project is a state the game never produces.
    return ClickButton(view.excavate);
}

// ---- the research report (UIResearchReport) — the capture's second consumer ---------------------
//
// See the header for why the capture is the only route in. What is worth keeping HERE is how much
// less this needs than the alert reader: no symbol table, no per-panel field map, no asset-path
// heuristic, no button matcher, and no cache. The function name is unique to this screen and the
// argument order is fixed by the .uc, so the read is eight positions and nothing else — which is
// what the capture was supposed to feel like.
//
// No cache, deliberately: the words are already OUR map (the capture's), so re-reading them is a
// hash lookup and eight string copies per render. ReadAlertText caches because its fallbacks reach
// into Flash with GetVariableString, which UIMCController.uc itself calls "immediate and blocking
// … very expensive". Nothing here does.

namespace {

// AS_UpdateResearchReport's parameter order (UIResearchReport.uc:183-195).
//
// ⛔ READ BY POSITION, and that is the point of StringArgs() keeping non-string operands as empty
// entries: index 4 is an asset path that must never be spoken, and a consumer that dropped it
// instead of skipping it would shift the description into the unlocks and the greeble into the
// description. Same rule as the wire format's repeating fields.
enum ResearchReportArg {
    kReportHeader = 0,
    kReportProject = 1,
    kReportCode = 2,
    kReportDate = 3,
    kReportImage = 4, // TechState.GetImage() — never spoken
    kReportUnlocks = 5,
    kReportDescription = 6,
    kReportGreeble = 7,
};

std::string ReportArg(const std::vector<std::string>& args, size_t index)
{
    return index < args.size() ? CleanText(args[index]) : std::string();
}

// Split on newlines FIRST, then clean each line — see the header. Empty lines go: the description's
// paragraph break is "\n\n", which yields one, and a tech with no unlocks yields nothing at all.
std::vector<std::string> ReportArgLines(const std::vector<std::string>& args, size_t index)
{
    std::vector<std::string> lines;
    if (index >= args.size())
        return lines;
    const std::string& raw = args[index];
    size_t start = 0;
    for (;;) {
        const size_t hit = raw.find('\n', start);
        std::string line = CleanText(raw.substr(start, hit == std::string::npos ? hit : hit - start));
        if (!line.empty())
            lines.push_back(std::move(line));
        if (hit == std::string::npos)
            return lines;
        start = hit + 1;
    }
}

} // namespace

void* FindResearchReportScreen()
{
    return FindStackScreenRaw("UIResearchReport");
}

ResearchReportView ReadResearchReport(void* screen)
{
    ResearchReportView v;
    v.screen = screen;
    if (!screen)
        return v;

    // ⛔ EXACT CLASS, not the class chain, because that is what the screen's own test is:
    // UIScreenStack.GetScreen compares `ScreenClass == Screens[i].Class` (UIScreenStack.uc:613),
    // while the IsA matching everyone assumes lives two functions further down in
    // GetFirstInstanceOf. UIShadowChamberArchives extends UIChooseResearch and is NOT a
    // UIResearchArchives — so a report opened from the SHADOW archives closes with Enter, exactly
    // like the one that opens in the Labs, and its nav help says "Continue" too. FindStackScreenRaw
    // compares class-name strings, which reproduces that semantics for free.
    v.fromArchives = FindStackScreenRaw("UIResearchArchives") != nullptr;

    // The screen pushes through its OWN MC (AS_UpdateResearchReport calls MC.BeginFunctionOp
    // directly), unlike UIAlert, which paints a library panel — so the cache index is the screen's.
    const int32_t index = FlashShadow::PanelCacheIndex(screen);
    if (index < 0)
        return v;

    FlashShadow::Call call;
    if (!FlashShadow::GetCall(index, "UpdateResearchReport", call))
        return v;

    const std::vector<std::string> args = call.StringArgs();
    v.captured = true;
    v.header = ReportArg(args, kReportHeader);
    v.project = ReportArg(args, kReportProject);
    v.code = ReportArg(args, kReportCode);
    v.date = ReportArg(args, kReportDate);
    v.greeble = ReportArg(args, kReportGreeble);
    v.unlockLines = ReportArgLines(args, kReportUnlocks);
    v.descriptionLines = ReportArgLines(args, kReportDescription);

    // ⛔ LOG ON CHANGE, NEVER PER READ. This is called from Build() AND ScreenName(), so twice a
    // poll — about twenty times a second — and the first live run wrote the same line 20+ times
    // for one report. A log that floods is a log nobody can read a defect out of, and the ASI
    // keeps only three sessions.
    //
    // The screen pointer is COMPARED and never dereferenced (the WireCache rule), and the project
    // rides in the key beside it so a second report landing on a reused address still logs.
    static void* sLastScreen = nullptr;
    static std::string sLastProject;
    if (screen != sLastScreen || v.project != sLastProject) {
        sLastScreen = screen;
        sLastProject = v.project;
        LOG_INFO("ResearchReport: '%s' from cache index %d — %zu operand(s), %zu unlock line(s), "
                 "%zu paragraph(s)%s",
            v.project.c_str(), index, args.size(), v.unlockLines.size(), v.descriptionLines.size(),
            v.fromArchives ? ", archives (Escape closes)" : " (Enter closes)");
    }
    return v;
}

// ---- new research available (UIResearchUnlocked) — see the header and the recon -----------------

namespace {

// addResearch's parameter order (UIResearchUnlocked.uc:199-208). Read by POSITION, the capture's
// standing rule: arg 2 is an asset path that must never be spoken, and arg 4 is a boolean that
// StringArgs() renders as an empty slot so nothing after it shifts.
enum ResearchUnlockedArg {
    kUnlockedHeader = 0,
    kUnlockedTitle = 1,
    kUnlockedImage = 2, // TechState.GetImage() — never spoken
    kUnlockedDescription = 3,
    kUnlockedShadow = 4, // bShadowProject — a boolean, not a string
};

// The two ints the screen retains (recon §3); both pinned in sdk_layout_checks.cpp.
constexpr int32_t kUnlockedCurrentTechOffset = 0x460; // UIResearchUnlocked::CurrentTechIndex
constexpr int32_t kUnlockedNumTechsOffset = 0x464;    // UIResearchUnlocked::NumUnlockedTechs

} // namespace

void* FindResearchUnlockedScreen()
{
    return FindStackScreenRaw("UIResearchUnlocked");
}

ResearchUnlockedView ReadResearchUnlocked(void* screen)
{
    ResearchUnlockedView v;
    v.screen = screen;
    if (!screen)
        return v;

    v.total = ReadI32Raw(screen, kUnlockedNumTechsOffset);
    const int32_t shownIndex = ReadI32Raw(screen, kUnlockedCurrentTechOffset);

    const int32_t index = FlashShadow::PanelCacheIndex(screen);
    if (index < 0)
        return v;

    // ⛔ FILTER BY NAME over everything on the index: the screen's MC also pushes an
    // addTweenBetween (the deck's fade-in), and GetCall's "most recent" would hand back only the
    // LAST card of a multi-tech deck. GetCalls is oldest-first, which is PopulateData's loop
    // order, which is the deck order Flash flips through — so card i really is push i.
    for (const FlashShadow::Call& call : FlashShadow::GetCalls(index)) {
        if (call.name != "addResearch")
            continue;
        const std::vector<std::string> args = call.StringArgs();
        ResearchUnlockedCard card;
        card.title = ReportArg(args, kUnlockedTitle);
        card.descriptionLines = ReportArgLines(args, kUnlockedDescription);
        card.shadowProject = kUnlockedShadow < call.args.size()
            && call.args[kUnlockedShadow].type == FlashShadow::ValueType::Boolean
            && call.args[kUnlockedShadow].b;
        if (v.cards.empty())
            v.header = ReportArg(args, kUnlockedHeader);
        v.cards.push_back(std::move(card));
    }
    v.captured = !v.cards.empty();

    // CurrentTechIndex is the game's own safety counter and legitimately sits one past the last
    // card between the final flip and the NoMoreResearch close (recon §1) — clamp, don't trust.
    const int last = v.captured ? static_cast<int>(v.cards.size()) - 1 : 0;
    v.current = shownIndex < 0 ? 0 : (shownIndex > last ? last : shownIndex);

    // Log on change, never per read — this runs from Build() and ScreenName() every poll. The
    // shown index rides in the key so every flip logs once; the screen pointer is compared and
    // never dereferenced.
    static void* sLastScreen = nullptr;
    static int sLastShown = -1;
    if (screen != sLastScreen || v.current != sLastShown) {
        sLastScreen = screen;
        sLastShown = v.current;
        const char* title = v.captured ? v.cards[v.current].title.c_str() : "(nothing captured)";
        LOG_INFO("ResearchUnlocked: card %d of %d '%s' from cache index %d (%zu captured%s)",
            v.current + 1, v.total, title, index, v.cards.size(),
            v.total == static_cast<int>(v.cards.size()) ? "" : " — COUNT MISMATCH vs NumUnlockedTechs");
    }
    return v;
}

// ---- the commodity list (UIInventory and its dozen subclasses) ----
//
// The most reused screen shape in the strategy layer, and the one where the widget tree is
// least help: UIItemCard retains not one of its strings and UIInventory_ListItem.PopulateData
// bypasses SetText, so the row a player is looking at reads as empty text. Words come from
// X2AccessCommodityReader; this reads the three things native should own anyway — which rows
// exist, which one is selected, and how the game tinted each one.
//
// ONE OFFSET. UIInventory::List is the only field transcribed here: the tab strip's labels and
// its selected index are localized strings and an int the companion can read directly, the card
// is asked about through the companion too, and every activation goes through a function call
// rather than a poke. That is deliberate — this screen family is the one most likely to be
// rebuilt by a mod, and a recipe that hangs on a single pinned offset survives that.

namespace {

constexpr int32_t kInventoryListOffset = 0x498; // UIInventory::List (UIList*)

// Engineering's build list and the encyclopedia are the long ones. Bounded like every other
// array walk here, and a truncation is LOGGED rather than silently presented as the whole list
// — a cap that quietly drops the tail reads to the player as "the game has nothing more".
constexpr int kMaxCommodityRows = 256;

} // namespace

void* FindCommodityScreen()
{
    // Topmost non-null entry only, matching GetCurrentScreen — which is what the companion
    // resolves too. A UIInventory further down is one the player has pushed past (the black
    // market's confirmation and the build-item dialog both land on top of it), and reading that
    // would describe a screen nobody is on.
    return TopStackScreenRaw([](void* screen, const char*) {
        return IsKindOfRaw(screen, "UIInventory");
    });
}

CommodityView ReadCommodity(void* screen)
{
    CommodityView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.tabbed = IsKindOfRaw(screen, "UIInventory_BuildItems");
    v.list = ReadPtrRaw(screen, kInventoryListOffset);
    if (!v.list)
        return v;

    v.selected = ReadI32Raw(v.list, kListSelectedIndexOffset);

    // The cap-is-loud check that used to be hand-rolled here is now what every array read in
    // this file gets: TakeListItemsRaw logs once per site, and only when the list really held
    // MORE than the cap rather than merely as many.
    void* rows[kMaxCommodityRows] = {};
    int rowCount = TakeListItemsRaw("Commodity: item rows", v.list, rows, kMaxCommodityRows);

    for (int i = 0; i < rowCount; i++) {
        if (!rows[i])
            continue;
        // ⛔ The list holds things that are not rows. UIInventory_LootRecovered interleaves
        // UIInventory_HeaderListItem section dividers ("ARTIFACTS RECOVERED"), and those are
        // plain UIPanels — they have no bDisabled/bIsBad bitfield at all, so reading one would
        // report a colour the game never drew. Skipping them by CLASS while keeping i as the
        // index is what keeps every remaining number equal to the game's own, which is what
        // SetSelectedIndex is expressed in and what the companion stamps independently.
        if (!IsKindOfRaw(rows[i], "UIInventory_ListItem"))
            continue;
        CommodityRow r;
        r.handle = rows[i];
        r.index = i;
        // The colour channel, and it is the whole affordability story: RealizeBadState sets
        // bIsBad from CanAffordItem and RealizeDisabledState sets bDisabled from
        // MeetsItemReqs/IsItemPurchased, both refreshed on every PopulateData. Reading the bits
        // the game DREW is what keeps this from becoming a second opinion about the same facts.
        uint32_t flags = ReadU32Raw(rows[i], kListItemStringFlagsOffset);
        r.disabled = (flags & kListItemDisabled) != 0;
        r.bad = (flags & kListItemBad) != 0;
        r.good = (flags & kListItemGood) != 0;
        r.warning = (flags & kListItemWarning) != 0;
        v.rows.push_back(r);
    }
    return v;
}

bool SetCommoditySelection(const CommodityView& view, int index)
{
    // More than a highlight: UIInventory sets List.OnSelectionChanged = SelectedItemChanged in
    // BuildScreen, and that delegate is what REPOPULATES THE CARD. Without this the card stop
    // would describe whichever row the game was left on.
    return SetListIndex(view.list, index);
}

bool ActivateCommodityRow(const CommodityView& view, const CommodityRow& row)
{
    if (!view.screen || !view.list)
        return false;

    // Select first, then hand the Enter to the SCREEN — the top of the game's own routing, not
    // the bottom of it. ⛔ Handing it straight to the ROW looks equivalent and is not: the
    // SCREEN's OnUnrealCommand runs before the navigator gets a say, and two things live in
    // that gap. UIInventory_LootRecovered overrides it to CLOSE on Enter — its rows buy
    // nothing, so a row-targeted Enter would leave a blind player stuck on the post-mission
    // loot screen with no continue key. And UISimpleCommodityScreen uses the same window to
    // re-sync iSelectedItem off the list, which is the number its own purchase branch and its
    // nav help read. Everything else falls through UIScreen.OnUnrealCommand → Navigator →
    // ListContainer → List, and UIList answers Enter by calling the SELECTED row's
    // OnUnrealCommand first (:515) — so the row still gets it, by the game's own path.
    //
    // Selecting first is therefore load-bearing again, the same as on the save rows: from here
    // down, the index is the argument.
    //
    // A disabled row is passed through deliberately rather than refused — these screens
    // disagree about what disabled MEANS (UIInventory_Implants greys an implant it will still
    // let you inspect), so the refusal stays theirs, where it can be right.
    SetListIndex(view.list, row.index);
    return SendRowCommand(view.screen, kCmdEnter);
}

bool SwitchCommodityTab(const CommodityView& view, int tab)
{
    if (!view.screen || !view.tabbed)
        return false;
    // The screen's own SwitchTab, which is exactly what its FXS_ARROW_LEFT/RIGHT handler calls.
    // bForce stays false so switching to the tab already showing is the no-op the game makes it.
    struct {
        int32_t Tab;
        uint32_t bForce;
    } params{tab, 0};
    return CallNamedFunction(view.screen, "SwitchTab", &params);
}

// ---- the personnel roster (UIPersonnel and its ten subclasses) ----
//
// The crew list, wherever the game asks "which person?" — the Living Quarters' roster, the Bar's
// obituaries, the Armory's soldiers, choosing a scientist for a project, staffing a facility, a
// covert action's squad. The subclasses change which array is filled and which tabs exist; the
// layout, the row widgets and the input path all belong to the base class, so ONE OFFSET covers
// every door.
//
// ⛔ The rows retain only a UnitRef. UIPersonnel_ListItem extends UIButton, but UpdateData pushes
// the name, status and location through MC.BeginFunctionOp — bypassing SetText, so the retained
// Text is EMPTY on a row visibly showing a soldier. Words come from X2AccessPersonnelReader,
// which re-derives them from that reference. What native reads is what native should: the list,
// its selection, and the disabled bit the game refuses clicks on.
//
// Unlike the debris room this is not a rescue — UIPersonnel.OnUnrealCommand already walks the
// list on Up/Down, switches tabs on Left/Right, accepts on Enter and backs out on Escape for a
// mouse+keyboard player. It is a reader, plus the one verb vanilla leaves on the mouse: the sort
// columns, whose keyboard branches are all controller constants.

namespace {

constexpr int32_t kPersonnelListOffset = 0x45C; // UIPersonnel::m_kList
constexpr int kMaxPersonnelRows = 256;

} // namespace

void* FindPersonnelScreen()
{
    // Topmost non-null entry only, matching GetCurrentScreen — which is also what the companion
    // resolves. A UIPersonnel further down is one something else has been pushed over (an armory
    // screen, a confirmation), and its rows are not what the player is looking at.
    return TopStackScreenRaw([](void* screen, const char*) {
        return IsKindOfRaw(screen, "UIPersonnel");
    });
}

PersonnelView ReadPersonnel(void* screen)
{
    PersonnelView v;
    if (!screen)
        return v;
    v.screen = screen;

    v.list = ReadPtrRaw(screen, kPersonnelListOffset);
    if (!v.list)
        return v;

    v.selected = ReadI32Raw(v.list, kListSelectedIndexOffset);

    void* rows[kMaxPersonnelRows] = {};
    int rowCount = TakeListItemsRaw("Personnel: roster rows", v.list, rows, kMaxPersonnelRows);
    for (int i = 0; i < rowCount; i++) {
        if (!rows[i])
            continue;
        PersonnelRow r;
        r.handle = rows[i];
        r.index = i;
        // UIButton::IsDisabled, which is the exact bit every OnXSelected delegate tests before
        // doing anything — so a disabled row here really is one the game will refuse. The armory
        // sets it on soldiers away on covert actions; the tutorial sets it on everyone but the
        // one soldier the script wants.
        r.disabled = (ReadU32Raw(rows[i], kButtonFlagsOffset) & kButtonDisabled) != 0;
        v.rows.push_back(r);
    }
    return v;
}

bool SetPersonnelSelection(const PersonnelView& view, int index)
{
    return SetListIndex(view.list, index);
}

bool ActivatePersonnelRow(const PersonnelView& view, int index)
{
    if (!view.screen || !view.list)
        return false;
    // ⛔ Enter goes to the SCREEN, and the selection has to land first. UIPersonnel.OnAccept
    // calls OnButtonCallback, which reads m_kList.selectedIndex and ignores everything else —
    // the same trap UISaveGame.OnAccept sets, so the same guard. Sending Enter to the ROW would
    // reach UIButton's handler instead and never run the tab's selection delegate at all.
    SetListIndex(view.list, index);
    return SendRowCommand(view.screen, kCmdEnter);
}

// ---- choosing what to build in a room (UIChooseFacility) ----
//
// See the header for why this is not part of the commodity family despite looking exactly like
// one. Native reads the half that IS retained — the row names and their two state bits — and the
// companion re-derives the panel, which retains nothing at all.

namespace {

constexpr int32_t kChooseFacilityListOffset = 0x418; // UIChooseFacility::m_List
constexpr int32_t kListItemAttentionOffset = 0x440;  // UIListItemString::AttentionIcon
constexpr int kMaxFacilityChoices = 64;

} // namespace

void* FindChooseFacilityScreen()
{
    // Stack first, like every other screen-class finder. UIChooseFacility is PUSHED by the build
    // flow, so the stack has the answer without a GObjects pass — and the pass is the cost that
    // was actually being paid, once a second for the whole session in which this screen is not
    // up. The fallback's one-line report is the other half: it is how "this class is not
    // stack-managed" becomes a fact in the log rather than something we assumed.
    static ScreenFinder sFinder{"UIChooseFacility"};
    static bool sReported = false;
    void* screen = PreferStack("UIChooseFacility", sFinder, sReported);
    // PreferStack's two halves do not filter alike: the stack half takes inited/not-removed,
    // the scan half takes ScreenFinder's default IsFocusedRaw. The header promises the CURRENT
    // screen, so the focus test belongs here rather than in whichever half answered.
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

ChooseFacilityView ReadChooseFacility(void* screen)
{
    ChooseFacilityView v;
    if (!screen)
        return v;
    v.screen = screen;

    v.list = ReadPtrRaw(screen, kChooseFacilityListOffset);
    if (!v.list)
        return v;

    v.selected = ReadI32Raw(v.list, kListSelectedIndexOffset);

    void* rows[kMaxFacilityChoices] = {};
    int rowCount = TakeListItemsRaw("ChooseFacility: rows", v.list, rows, kMaxFacilityChoices);
    for (int i = 0; i < rowCount; i++) {
        if (!rows[i])
            continue;
        FacilityChoice c;
        c.handle = rows[i];
        c.index = i;
        // Unlike the commodity rows, PopulateList goes through UIListItemString.SetText — so the
        // name really is here and needs no companion.
        c.label = CleanText(ReadFStringField(rows[i], kListItemStringTextOffset));
        c.disabled = (ReadU32Raw(rows[i], kListItemStringFlagsOffset) & kListItemDisabled) != 0;
        // NeedsAttention spawns the icon lazily and then toggles its visibility, so the icon
        // EXISTING is not the same as it being shown — a facility that lost priority keeps a
        // hidden one. Both halves are the game's own.
        void* icon = ReadPtrRaw(rows[i], kListItemAttentionOffset);
        c.priority = icon && (ReadU32Raw(icon, kPanelFlagsOffset) & kPanelVisible) != 0;
        v.choices.push_back(std::move(c));
    }
    return v;
}

bool SetChooseFacilitySelection(const ChooseFacilityView& view, int index)
{
    return SetListIndex(view.list, index);
}

bool ActivateChooseFacility(const ChooseFacilityView& view, int index)
{
    if (!view.screen || !view.list)
        return false;
    // ⛔ Enter goes to the SCREEN after the selection lands. OnAccept reads the SCREEN's own
    // SelectedIndex — a field only UpdateSelection writes, and only UIList::SetSelectedIndex
    // calls that — so committing without selecting first would build a different facility from
    // the one just spoken. A disabled row is passed through: OnAccept re-tests CanBuildFacility
    // and plays its own refusal sound, which keeps the refusal where the game can be right about
    // why (supplies, power, or staffing).
    SetListIndex(view.list, index);
    return SendRowCommand(view.screen, kCmdEnter);
}

} // namespace GameUI

#ifdef _MSC_VER
#pragma pack(pop)
#endif
