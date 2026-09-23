// The AVENGER family's readers: the ant-farm gate, the rooms, choose-facility, the commodity
// and personnel families, and the research screens. Split out of game_ui.cpp along the
// recipe-family axis; the raw primitives stay in game_ui.cpp behind game_ui_internal.h.

#include "game_ui_avenger.h"
#include "game_ui_internal.h"
#include "log.h"
#include "flash_shadow.h"
#include "Graph/GraphAnnouncer.hpp" // AppendPhrase — the covert row's slot tooltips are joined here

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
        // ⛔ Tolerant, and that is what brings this gate's two halves BACK into agreement. The
        // script half (X2AccessAvengerReader.GetGrid) casts with `UIFacilityGrid(Current)`, which
        // UnrealScript makes tolerant of subclasses for free — so an override here would have had
        // native say "not the grid" ten times a second while script said "yes", the exact
        // half-a-rule failure the gate rule was written about. UIFacilityGrid has no vanilla
        // descendants, so nothing else can answer for it.
        if (ObjectClassNamedRaw(screens[i], "UIFacilityGrid", ClassMatch::Derived)) {
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
        //
        // ⛔ EXACT, unlike the grid above, because this entry runs in the PERMISSIVE direction:
        // it says "this screen on top does NOT block". Extending that forgiveness to a subclass
        // whose source we have not read means narrating the grid while the arrows drive something
        // else — and the whole justification here is one line of THIS class's OnUnrealCommand.
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

// ---- the resource strip (UIAvengerHUD.ResourceContainer) ---------------------------------------
//
// See the header for why this reads the capture rather than re-deriving. What belongs HERE is the
// batch problem: the strip is painted by N same-named pushes instead of one, so "the most recent
// batch" (GetCall) is one resource out of nine, and the only honest read walks back to where THIS
// repaint began. The alert family has the same problem in a different shape — several DIFFERENTLY
// named batches per panel, the last of them often a picture — and answers it by reading every
// batch (ReadAlertCaptured).
//
// `ClearResources` is that boundary, and it is retained for exactly this reason — see
// IsRepaintDelimiter in flash_shadow.cpp. Without it the walk would happily read a previous
// screen's resources: the strip is rebuilt on every screen change, and stepping from the ant farm
// (nine) into the Power Generator (one) would leave the eight before it looking current.

namespace {

// UIAvengerHUD::ResourceContainer, pinned in sdk_layout_checks.cpp with its neighbours.
constexpr int32_t kAvengerHudResourceContainerOffset = 0x420;

// AddResource's two operands (UIX2ResourceHeader.uc:35-42): label, then the coloured value.
constexpr size_t kResourceLabel = 0;
constexpr size_t kResourceValue = 1;

// The strip's own worst case is nine; the headroom is for a mod that adds resources, and the cap
// exists only so a capture that never saw a delimiter cannot grow without bound.
constexpr size_t kMaxResourceEntries = 32;

// UIUtilities_Colors' HTML constants, verbatim (UIUtilities_Colors.uc:45-60). Matched
// case-insensitively because the game's own table is not internally consistent about case —
// CASH_HTML_COLOR is "5CD16C" where BAD_HTML_COLOR is "bf1e2e", and both reach this string
// through the same GetColoredText.
//
// ⛔ Only the two states worth SAYING are listed. NORMAL/CASH/GOOD all mean "no comment", and
// mapping them to words would put a reassurance on the end of every one of nine resources.
struct StateColor {
    const char* hex;
    ResourceState state;
};
constexpr StateColor kStateColors[] = {
    {"bf1e2e", ResourceState::Critical}, // BAD_HTML_COLOR
    {"fdce2b", ResourceState::Warning},  // WARNING_HTML_COLOR
};

// The colour out of `<font color='#xxxxxx'>`, as a state. Anything unrecognised — including a
// value the game did not colour at all — is Normal, which is the right failure: a resource whose
// markup we cannot parse is read WITHOUT a state word rather than with a wrong one.
ResourceState StateFromMarkup(const std::string& raw)
{
    const size_t hit = raw.find("color='#");
    if (hit == std::string::npos)
        return ResourceState::Normal;
    const size_t at = hit + 8; // past "color='#"
    if (at + 6 > raw.size())
        return ResourceState::Normal;
    for (const StateColor& candidate : kStateColors) {
        if (_strnicmp(raw.c_str() + at, candidate.hex, 6) == 0)
            return candidate.state;
    }
    return ResourceState::Normal;
}

} // namespace

ResourceStripView ReadResourceStrip()
{
    ResourceStripView v;

    // ⛔ The HUD, never the screen asking. Every strategy screen that shows resources shows the
    // ONE strip on UIAvengerHUD underneath it — the same arrangement the room reader already
    // relies on for the facility header and the shortcut bar.
    // Tolerant, and in step with the two other UIAvengerHUD lookups (FindAvengerHud, the room
    // reader) and the objectives walker in the tactical TU: if they disagreed about whether the
    // HUD exists, the resource strip and the objective list would describe different layers.
    v.hud = FindStackScreenRaw("UIAvengerHUD", ClassMatch::Derived);
    if (!v.hud)
        return v;
    v.container = ReadPtrRaw(v.hud, kAvengerHudResourceContainerOffset);
    if (!v.container)
        return v;

    // A CHILD widget's bIsVisible, which the game toggles deliberately here — see the header.
    v.showing = (ReadU32Raw(v.container, kPanelFlagsOffset) & kPanelVisible) != 0;

    const int32_t index = FlashShadow::PanelCacheIndex(v.container);
    if (index < 0)
        return v;

    // Oldest first, which is UpdateResources' own call order, which is left-to-right on screen.
    // Everything before the LAST clearResources belongs to a repaint that is no longer on screen.
    const std::vector<FlashShadow::Call> calls = FlashShadow::GetCalls(index);
    size_t start = 0;
    for (size_t i = calls.size(); i-- > 0;) {
        if (calls[i].name == "clearResources") {
            start = i + 1;
            break;
        }
    }

    for (size_t i = start; i < calls.size(); i++) {
        if (calls[i].name != "addResource")
            continue;
        if (v.entries.size() >= kMaxResourceEntries)
            break;
        const std::vector<std::string> args = calls[i].StringArgs();
        ResourceEntry entry;
        entry.label = FlashArg(args, kResourceLabel);
        entry.value = FlashArg(args, kResourceValue);
        // ⛔ The state comes off the RAW operand: FlashArg cleans, and cleaning is what removes
        // the <font> the colour lives in. Read the markup first, then keep the cleaned words.
        entry.state = kResourceValue < args.size() ? StateFromMarkup(args[kResourceValue])
                                                   : ResourceState::Normal;
        // An unlabelled entry is not a resource — and dropping it here is safe precisely because
        // this list is not positional: each entry carries its own label.
        if (entry.label.empty() && entry.value.empty())
            continue;
        v.entries.push_back(std::move(entry));
    }

    // Log on change, never per read — this runs from every recipe's Build() and from the hotkey,
    // so several times a second. The container pointer is COMPARED and never dereferenced.
    static void* sLastContainer = nullptr;
    static size_t sLastCount = 0;
    static bool sLastShowing = false;
    if (v.container != sLastContainer || v.entries.size() != sLastCount || v.showing != sLastShowing) {
        sLastContainer = v.container;
        sLastCount = v.entries.size();
        sLastShowing = v.showing;
        LOG_INFO("ResourceStrip: %zu resource(s) from cache index %d, %s (%zu call(s) recorded, "
                 "batch starts at %zu)",
            v.entries.size(), index, v.showing ? "showing" : "HIDDEN", calls.size(), start);
    }
    return v;
}

bool RefreshResourceStrip(const ResourceStripView& view)
{
    // ⛔ The HUD, not the container: `UpdateResources` is UIAvengerHUD's, and it is the function
    // that owns the per-screen switch. `UIX2ResourceHeader` has no repaint of its own — it only
    // has the AddResource the HUD drives it with.
    if (!view.hud)
        return false;
    return CallNamedFunction(view.hud, "UpdateResources", nullptr);
}

// ---- the rest of the HUD: shortcuts, to-do strip, event queue -----------------------------------
//
// See the header for what each panel is and why it is a plain widget read. What is worth keeping
// here is the ACTIVATION discipline, which is the same for all three: never poke a field the
// game's own path would have written, call the function that path calls. The digit keys call
// SelectCategory; the mouse hover calls OnCategoryMouseEvent with the icon it hovered and the row
// click calls OnListItemCallback; R3 calls ToggleExpanded. Each of those is one CallNamedFunction
// with the game's own parameters, so every side effect — the camera swoop, the tooltip cleanup,
// the close timer, the row respawn — happens exactly as it would for a sighted player.

namespace {

constexpr int32_t kAvengerHudNavHelpOffset = 0x418;    // UIAvengerHUD::NavHelp
constexpr int32_t kAvengerHudEventQueueOffset = 0x42C; // UIAvengerHUD::EventQueue
constexpr int32_t kAvengerHudToDoOffset = 0x434;       // UIAvengerHUD::ToDoWidget
constexpr int32_t kAvengerHudShortcutsOffset = 0x43C;  // UIAvengerHUD::Shortcuts (the room
                                                       // reader below uses it too)
constexpr int32_t kShortcutsCategoryLabelsOffset = 0x3F0; // UIAvengerShortcuts::CategoryLabels[5],
                                                          // stride 0x10 (same)

// UIAvengerShortcuts::Categories — a TArray of UIAvengerShortcutMessageCategory, walked by
// stride. Only Button and bAlert are read; the messages inside are the ROOM's submenu, which the
// room recipe already reads and which the grid never shows (HideList on RealizeShortcuts).
constexpr int32_t kShortcutsCategoriesOffset = 0x82C;
constexpr int32_t kShortcutCatStride = 0x40;
constexpr int32_t kShortcutCatButton = 0x00;
constexpr int32_t kShortcutCatFlags = 0x3C; // bAlert bit 0
constexpr uint32_t kShortcutCatAlert = 0x1;
constexpr int kShortcutLabelCount = 5; // CategoryLabels[5]
constexpr int kMaxShortcutCategories = 8; // five shipped; the headroom is for a mod

// UINavigationHelp — the hologlobe button (digit 1) and the word the controller branch draws.
constexpr int32_t kNavHelpGeoscapeLabelOffset = 0x3F4;
constexpr int32_t kNavHelpFlagsOffset = 0x434; // bGeoscapeButton bit 0x4
constexpr uint32_t kNavHelpGeoscapeButton = 0x4;
// m_arrButtonClickDelegates (TArray<FScriptDelegate>): entry N belongs to the help button
// Flash knows as `buttonNavHelp_N`, which is also where AddLeftHelp registered its tooltip
// (GenerateTooltipPath: MCPath.leftBtnHelpContainer.buttonNavHelp_N).
constexpr int32_t kNavHelpButtonDelegatesOffset = 0x43C;
constexpr int32_t kDelegateStride = 0x20;
constexpr int32_t kDelegateFunctionOffset = 0x08; // FScriptDelegate::FunctionName
constexpr int kMaxNavHelpButtons = 16;
constexpr const char* kGeoscapeHotlinkFunction = "HotlinkToGeoscape";
constexpr const char* kNavHelpLeftButtonPath = ".leftBtnHelpContainer.buttonNavHelp_";
// A covert-action row's staff-slot tooltips: MCPath.slot0 … registered per staff slot only.
constexpr int kMaxCovertSlots = 8;

// UIToDoWidget
constexpr int32_t kToDoCategoryLabelsOffset = 0x3D8; // m_arrCategory_Labels (TArray<FString>)
constexpr int32_t kToDoListOffset = 0x780;
constexpr int32_t kToDoCategoriesOffset = 0x7AC; // Categories (TArray<UIToDoMessageCategory>)
constexpr int32_t kToDoCatStride = 0x18;
constexpr int32_t kToDoCatIcon = 0x00;
constexpr int32_t kToDoCatMessages = 0x08; // TArray<UIToDoMessage>
constexpr int32_t kToDoMsgStride = 0x5C;
constexpr int32_t kToDoMsgLabel = 0x00;
constexpr int32_t kToDoMsgDescription = 0x10;
constexpr int32_t kToDoMsgUrgency = 0x28; // a byte; read as the dword it pads
constexpr int kMaxToDoCategories = 16; // eUIToDoCat_MAX is 9
constexpr int kMaxToDoMessages = 32;   // per category; the staffing list is the widest shipped
// UIUtilities_Input.FXS_L_MOUSE_IN — the command OnCategoryMouseEvent takes as "hovered".
constexpr int32_t kInputMouseIn = 392;

// UIEventQueue and its rows
constexpr int32_t kEventQueueListOffset = 0x3D8;
constexpr int32_t kEventQueueExpandButtonOffset = 0x3F0;
constexpr int32_t kEventQueueExpandLabelOffset = 0x3F8;
constexpr int32_t kEventQueueShrinkLabelOffset = 0x408;
constexpr int32_t kEventQueueFlagsOffset = 0x418; // bIsExpanded bit 0
constexpr uint32_t kEventQueueExpanded = 0x1;
constexpr int32_t kEventItemTitleOffset = 0x3D8;
constexpr int32_t kEventItemDaysLabelOffset = 0x3E8;
constexpr int32_t kEventItemDaysValueOffset = 0x3F8;
constexpr int kMaxEventRows = 64; // eleven contributors; a long queue is a dozen rows

// RefreshDateTime's operands (UIEventQueue.uc:185-190): date, hours, minutes, suffix.
constexpr size_t kDateTimeDate = 0;
constexpr size_t kDateTimeHours = 1;
constexpr size_t kDateTimeMinutes = 2;
constexpr size_t kDateTimeSuffix = 3;

// Visible AND not removed, for the child widgets whose bIsVisible the game toggles on purpose —
// every panel read below is one (Show/Hide on the strip, the icons, the queue, the button).
bool ChildShowing(void* panel)
{
    if (!panel)
        return false;
    const uint32_t flags = ReadU32Raw(panel, kPanelFlagsOffset);
    return (flags & kPanelVisible) && !(flags & kPanelRemoved);
}

// The Data pointer and Count of a TArray at `offset`, for the struct arrays walked by stride.
// Count is -1 when the array could not be read; the callers treat that as empty.
const uint8_t* ArrayData(const void* obj, int32_t offset, int* count)
{
    *count = ReadArrayCountRaw(obj, offset);
    if (*count <= 0)
        return nullptr;
    return static_cast<const uint8_t*>(ReadPtrRaw(obj, offset));
}

} // namespace

void* FindAvengerHud()
{
    return FindStackScreenRaw("UIAvengerHUD", ClassMatch::Derived);
}

ShortcutBarView ReadShortcutBar()
{
    ShortcutBarView v;
    v.hud = FindAvengerHud();
    if (!v.hud)
        return v;
    v.shortcuts = ReadPtrRaw(v.hud, kAvengerHudShortcutsOffset);
    v.navHelp = ReadPtrRaw(v.hud, kAvengerHudNavHelpOffset);
    if (!v.shortcuts)
        return v;
    v.showing = ChildShowing(v.shortcuts);

    int count = 0;
    const uint8_t* data = ArrayData(v.shortcuts, kShortcutsCategoriesOffset, &count);
    if (data) {
        if (count > kMaxShortcutCategories) {
            LOG_WARN("ShortcutBar: %d categories, reading the first %d", count,
                kMaxShortcutCategories);
            count = kMaxShortcutCategories;
        }
        for (int i = 0; i < count; i++) {
            const uint8_t* cat = data + static_cast<size_t>(i) * kShortcutCatStride;
            void* button = ReadPtrRaw(cat, kShortcutCatButton);
            // The Shadow Chamber's button is spawned and hidden until the facility exists
            // (ShouldShowCurrentCategory) — the recon saw it inited and not visible. A hidden
            // button is one a sighted player has no way to press, so it gets no node.
            if (!ChildShowing(button))
                continue;
            ShortcutCategory c;
            c.index = i;
            // The button's own retained Text first (it was SetText'd from CategoryLabels), the
            // class-default label as the fallback for the five shipped slots.
            c.label = ReadFStringField(button, kButtonTextOffset);
            if (c.label.empty() && i < kShortcutLabelCount)
                c.label = ReadFStringField(v.shortcuts, kShortcutsCategoryLabelsOffset + i * 0x10);
            c.alert = (ReadU32Raw(cat, kShortcutCatFlags) & kShortcutCatAlert) != 0;
            c.disabled = (ReadU32Raw(button, kButtonFlagsOffset) & kButtonDisabled) != 0;
            c.button = button;
            v.categories.push_back(std::move(c));
        }
    }

    if (v.navHelp) {
        v.geoscapeButton = (ReadU32Raw(v.navHelp, kNavHelpFlagsOffset) & kNavHelpGeoscapeButton) != 0;
        v.geoscapeLabel = ReadFStringField(v.navHelp, kNavHelpGeoscapeLabelOffset);
        if (v.geoscapeButton) {
            // Which help button is the hologlobe: the one whose click delegate is
            // HotlinkToGeoscape. Its index names it in Flash, and the tooltip was registered
            // against that name — so this is the exact path, not a guess at a slot.
            int n = 0;
            const uint8_t* delegates = ArrayData(v.navHelp, kNavHelpButtonDelegatesOffset, &n);
            if (n > kMaxNavHelpButtons)
                n = kMaxNavHelpButtons;
            for (int i = 0; delegates && i < n; i++) {
                char fn[128] = "";
                if (!FNameAtAnsiRaw(delegates + static_cast<size_t>(i) * kDelegateStride
                            + kDelegateFunctionOffset,
                        fn, sizeof(fn))
                    || strcmp(fn, kGeoscapeHotlinkFunction) != 0)
                    continue;
                const std::string path = std::string(kNavHelpLeftButtonPath) + std::to_string(i);
                v.geoscapeTooltip = ReadTooltipByPath(v.navHelp, path.c_str());
                break;
            }
        }
    }
    return v;
}

bool SelectShortcutCategory(const ShortcutBarView& view, int index)
{
    if (!view.shortcuts || index < 0)
        return false;
    struct {
        int32_t iNewCat;
    } params{index};
    return CallNamedFunction(view.shortcuts, "SelectCategory", &params);
}

bool HotlinkToGeoscape(const ShortcutBarView& view)
{
    if (!view.navHelp)
        return false;
    return CallNamedFunction(view.navHelp, "HotlinkToGeoscape", nullptr);
}

ToDoView ReadToDoWidget()
{
    ToDoView v;
    v.hud = FindAvengerHud();
    if (!v.hud)
        return v;
    v.widget = ReadPtrRaw(v.hud, kAvengerHudToDoOffset);
    if (!v.widget)
        return v;
    v.showing = ChildShowing(v.widget);

    int labelCount = 0;
    const uint8_t* labels = ArrayData(v.widget, kToDoCategoryLabelsOffset, &labelCount);

    int count = 0;
    const uint8_t* data = ArrayData(v.widget, kToDoCategoriesOffset, &count);
    if (!data)
        return v;
    if (count > kMaxToDoCategories) {
        LOG_WARN("ToDo: %d categories, reading the first %d", count, kMaxToDoCategories);
        count = kMaxToDoCategories;
    }
    for (int i = 0; i < count; i++) {
        const uint8_t* cat = data + static_cast<size_t>(i) * kToDoCatStride;
        // The icon is what a sighted player sees, and UpdateCategories hides it for a category
        // with nothing to say. Both tests, because they are the same fact from two sides and a
        // disagreement between them is worth a look in the log.
        const bool iconShowing = ChildShowing(ReadPtrRaw(cat, kToDoCatIcon));
        int msgCount = 0;
        const uint8_t* msgs = ArrayData(cat, kToDoCatMessages, &msgCount);
        if (!msgs || msgCount <= 0) {
            if (iconShowing)
                LOG_DEBUG("ToDo: category %d icon showing with no messages", i);
            continue;
        }
        if (!iconShowing)
            continue;
        if (msgCount > kMaxToDoMessages) {
            LOG_WARN("ToDo: category %d holds %d messages, reading the first %d", i, msgCount,
                kMaxToDoMessages);
            msgCount = kMaxToDoMessages;
        }
        ToDoCategory c;
        c.index = i;
        if (labels && i < labelCount)
            c.label = ReadFStringField(labels, i * 0x10);
        for (int m = 0; m < msgCount; m++) {
            const uint8_t* msg = msgs + static_cast<size_t>(m) * kToDoMsgStride;
            ToDoMessage entry;
            entry.label = ReadFStringField(msg, kToDoMsgLabel);
            entry.description = ReadFStringField(msg, kToDoMsgDescription);
            entry.urgency = static_cast<int>(ReadU32Raw(msg, kToDoMsgUrgency) & 0xFF);
            // A message with neither word is not one the game could have drawn a row for.
            if (entry.label.empty() && entry.description.empty())
                continue;
            c.messages.push_back(std::move(entry));
        }
        if (!c.messages.empty())
            v.categories.push_back(std::move(c));
    }
    return v;
}

bool ActivateToDoMessage(const ToDoView& view, int category, int index)
{
    if (!view.widget || category < 0 || index < 0)
        return false;
    // The icon the mouse would have hovered. OnCategoryMouseEvent reads the category number off
    // the icon's OWN MCName ("CatImage_N"), which is why the game's widget is handed in rather
    // than a category index — and why nothing here writes CurrentCategory by hand.
    int count = 0;
    const uint8_t* data = ArrayData(view.widget, kToDoCategoriesOffset, &count);
    if (!data || category >= count)
        return false;
    void* icon = ReadPtrRaw(data + static_cast<size_t>(category) * kToDoCatStride, kToDoCatIcon);
    void* list = ReadPtrRaw(view.widget, kToDoListOffset);
    if (!icon || !list)
        return false;

    struct {
        void* Control;
        int32_t cmd;
    } hover{icon, kInputMouseIn};
    if (!CallNamedFunction(view.widget, "OnCategoryMouseEvent", &hover))
        return false;

    struct {
        void* ContainerList;
        int32_t ItemIndex;
    } click{list, index};
    return CallNamedFunction(view.widget, "OnListItemCallback", &click);
}

EventQueueView ReadEventQueue()
{
    EventQueueView v;
    v.hud = FindAvengerHud();
    if (!v.hud)
        return v;
    v.queue = ReadPtrRaw(v.hud, kAvengerHudEventQueueOffset);
    if (!v.queue)
        return v;
    v.showing = ChildShowing(v.queue);
    v.expanded = (ReadU32Raw(v.queue, kEventQueueFlagsOffset) & kEventQueueExpanded) != 0;
    void* expandButton = ReadPtrRaw(v.queue, kEventQueueExpandButtonOffset);
    v.canToggle = ChildShowing(expandButton);
    if (v.canToggle) {
        v.toggleLabel = ReadFStringField(v.queue,
            v.expanded ? kEventQueueShrinkLabelOffset : kEventQueueExpandLabelOffset);
    }

    // The date and time: pushed straight to Flash by RefreshDateTime, so the capture is the
    // only place the words exist. Keyed on the queue's own cache index, matched by name.
    const int32_t index = FlashShadow::PanelCacheIndex(v.queue);
    if (index >= 0) {
        FlashShadow::Call call;
        if (FlashShadow::GetCall(index, "RefreshDateTime", call)) {
            const std::vector<std::string> args = call.StringArgs();
            v.date = FlashArg(args, kDateTimeDate);
            const std::string hours = FlashArg(args, kDateTimeHours);
            const std::string minutes = FlashArg(args, kDateTimeMinutes);
            const std::string suffix = FlashArg(args, kDateTimeSuffix);
            if (!hours.empty() && !minutes.empty()) {
                v.time = hours + ":" + minutes;
                if (!suffix.empty())
                    v.time += " " + suffix;
            }
        }
    }

    void* list = ReadPtrRaw(v.queue, kEventQueueListOffset);
    if (!list)
        return v;
    void* rows[kMaxEventRows] = {};
    const int count = TakeListItemsRaw("event queue rows", list, rows, kMaxEventRows);
    for (int i = 0; i < count; i++) {
        if (!ChildShowing(rows[i]))
            continue;
        EventQueueEntry e;
        e.title = ReadFStringField(rows[i], kEventItemTitleOffset);
        e.daysLabel = ReadFStringField(rows[i], kEventItemDaysLabelOffset);
        e.daysValue = ReadFStringField(rows[i], kEventItemDaysValueOffset);
        e.covert = IsKindOfRaw(rows[i], "UIEventQueue_CovertActionListItem");
        if (e.title.empty())
            continue; // a row UpdateData has not reached yet
        if (e.covert) {
            // One tooltip per STAFF slot (AddSlotTooltip: the soldier's name, then the slot's
            // reward on a second line); a purchased cost slot has its tooltip removed. Probed
            // by index because the slot count lives on the action state, not the row; a gap
            // is a cost slot and the walk continues past it.
            for (int s = 0; s < kMaxCovertSlots; s++) {
                const std::string path = ".slot" + std::to_string(s);
                std::string tip = ReadTooltipByPath(rows[i], path.c_str());
                if (!tip.empty())
                    X2Access::Graph::AppendPhrase(e.tooltip, tip, ". ");
            }
        }
        v.entries.push_back(std::move(e));
    }
    return v;
}

bool ToggleEventQueue(const EventQueueView& view)
{
    if (!view.queue)
        return false;
    // R3's own call is ToggleExpanded(none) — the button parameter is unused by the body.
    struct {
        void* Button;
    } params{nullptr};
    return CallNamedFunction(view.queue, "ToggleExpanded", &params);
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

// kAvengerHudShortcutsOffset and kShortcutsCategoryLabelsOffset sit with the HUD-panel readers
// above — the shortcut bar is read from two sections now, and the TU's anonymous namespace
// is one namespace, so the earlier definition serves both.
constexpr int32_t kAvengerHudFacilityHeaderOffset = 0x454; // UIAvengerHUD::FacilityHeader
constexpr int32_t kScreenHeaderLocationOffset = 0x3D8;     // UIX2ScreenHeader::LocationField
constexpr int32_t kScreenHeaderRoomOffset = 0x3E8;         // UIX2ScreenHeader::RoomField
constexpr int32_t kShortcutsListOffset = 0x7F8;            // UIAvengerShortcuts::List
constexpr int32_t kShortcutsCurrentCategoryOffset = 0x83C; // ::CurrentCategory
constexpr int32_t kFacilityStaffContainerOffset = 0x608;   // UIFacility::m_kStaffSlotContainer
constexpr int32_t kFacilityRoomContainerOffset = 0x610;    // UIFacility::m_kRoomContainer
constexpr int32_t kFacilityUpgradeButtonOffset = 0x648;    // UIFacility::UpgradeButton
constexpr int32_t kRoomContainerFuncsOffset = 0x3DC;       // UIRoomContainer::RoomFuncs
constexpr int32_t kStaffContainerTitleOffset = 0x3D8;      // UIStaffContainer::Title
constexpr int32_t kStaffContainerSkillOffset = 0x408;      // UIStaffContainer::Skill
constexpr int32_t kStaffContainerSlotsOffset = 0x418;      // UIStaffContainer::StaffSlots
// The picker hangs off the same base class in a built room and in a debris room, so both
// readers below share these two.
constexpr int32_t kStaffContainerDropDownOffset = 0x428; // UIStaffContainer::m_kPersonnelDropDown
constexpr int32_t kDropDownListOffset = 0x3E0;           // UIPersonnel_DropDown::m_kList

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
    void* hud = FindStackScreenRaw("UIAvengerHUD", ClassMatch::Derived);
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
        // The slot widgets, and only while the game is SHOWING the strip: Refresh() Hide()s the
        // container for a facility with no slots or with bHideStaffSlots, and a slot read off a
        // hidden strip would be a control the player cannot see. The pointers are what the
        // recipe activates (HandleClick) and what keys its words; the words themselves come from
        // the companion, because UIStaffSlot.UpdateData pushes name and bonus into Flash and
        // keeps only StaffSlotRef.
        if (v.staffVisible) {
            void* slots[kMaxStaffSlots] = {};
            int n = TakeArrayRaw("Room: staff slots", staff, kStaffContainerSlotsOffset, slots,
                kMaxStaffSlots);
            for (int i = 0; i < n; i++) {
                if (slots[i])
                    v.staffSlots.push_back(slots[i]);
            }
        }

        // The picker, when it is up. Same read as the debris room's, because it is the same
        // widget on the same base class (UIStaffContainer::m_kPersonnelDropDown) — a facility
        // slot's ShowDropDown spawns exactly what a build slot's does. Its visibility is the
        // routing test the game itself makes (UIStaffContainer.OnUnrealCommand hands every key
        // to the picker while it shows), so it is the recipe's shape switch too.
        void* drop = ReadPtrRaw(staff, kStaffContainerDropDownOffset);
        if (drop && (ReadU32Raw(drop, kPanelFlagsOffset) & kPanelVisible)) {
            v.dropDown = drop;
            v.dropList = ReadPtrRaw(drop, kDropDownListOffset);
            if (v.dropList)
                v.dropSelected = ReadI32Raw(v.dropList, kListSelectedIndexOffset);
        }
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
constexpr int kMaxBuildSlots = 8;

} // namespace

void* FindRoomBuildScreen()
{
    // Topmost non-null entry, the GetCurrentScreen rule again — and UIRoom's own OnUnrealCommand
    // is unguarded, so anything above it has already taken the keys.
    // Tolerant, and topmost-only, so a match here can never reach past a screen that has already
    // taken the keys.
    return TopStackScreenRaw([](void* screen, const char*) {
        return ObjectClassNamedRaw(screen, "UIRoom", ClassMatch::Derived);
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

bool SetStaffPickerSelection(void* dropList, int index)
{
    return SetListIndex(dropList, index);
}

bool ChooseStaffCandidate(void* dropList, int index)
{
    if (!dropList)
        return false;
    // Same select-then-Enter shape as the room menu, for the same reason: UIList's accept path
    // fires OnItemClicked(self, SelectedIndex) — here UIPersonnel_DropDown.OnStaffSelected,
    // which indexes m_staff by that number — so the selection is the argument.
    SetListIndex(dropList, index);
    return SendRowCommand(dropList, kCmdEnter);
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

} // namespace

void* FindResearchReportScreen()
{
    return FindStackScreenRaw("UIResearchReport", ClassMatch::Derived);
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
    //
    // ⛔ So this one stays EXACT while its neighbours tolerate: the whole point is to MIRROR the
    // game's own exact compare, and a tolerant read here would answer differently from the screen
    // it is describing. Parity is defined against what the game does, not against what we can find.
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
    v.header = FlashArg(args, kReportHeader);
    v.project = FlashArg(args, kReportProject);
    v.code = FlashArg(args, kReportCode);
    v.date = FlashArg(args, kReportDate);
    v.greeble = FlashArg(args, kReportGreeble);
    v.unlockLines = FlashArgLines(args, kReportUnlocks);
    v.descriptionLines = FlashArgLines(args, kReportDescription);

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
    return FindStackScreenRaw("UIResearchUnlocked", ClassMatch::Derived);
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
        card.title = FlashArg(args, kUnlockedTitle);
        card.descriptionLines = FlashArgLines(args, kUnlockedDescription);
        card.shadowProject = kUnlockedShadow < call.args.size()
            && call.args[kUnlockedShadow].type == FlashShadow::ValueType::Boolean
            && call.args[kUnlockedShadow].b;
        if (v.cards.empty())
            v.header = FlashArg(args, kUnlockedHeader);
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
    v.implants = IsKindOfRaw(screen, "UIInventory_Implants");
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
    if (!SetListIndex(view.list, row.index))
        return false;
    if (view.implants) {
        // Enter in UIInventory_ListItem emits DOUBLE_UP and consumes the key, but the PCS
        // picker binds OnItemDoubleClicked only for controllers. Use the list's single-click
        // input path so OnItemClicked (including mod wrappers) and CanEquipImplant still run.
        // Layout pinned against UIList.OnChildMouseEvent in sdk_layout_checks.cpp.
        struct {
            void* control;
            int32_t cmd;
        } params{row.handle, 391}; // FXS_L_MOUSE_UP
        static_assert(sizeof(params) == 0x0C);
        return CallNamedFunction(view.list, "OnChildMouseEvent", &params);
    }
    bool handled = false;
    if (!SendRowCommand(view.screen, kCmdEnter, &handled))
        return false;
    if (handled)
        return true;

    // ⛔ The screen said it did NOT handle Enter — and on two screens of the family that is the
    // game's own keyboard bug, not a refusal. UIChoosePsiAbility.InitScreen and
    // UIChooseClass.InitScreen both call Navigator.SetSelected(List) on the SCREEN's navigator,
    // whose navigables are the header and ListContainer, not the list; UINavigator.SetSelected
    // answers an unknown control by setting SelectedIndex to -1, so from then on the screen's
    // Enter reaches GetSelected() == none and falls through to nothing (live: the psi picker's
    // navigator read -1 with the list selected and focused). Vanilla keyboard players cannot
    // press Enter there either — the controller survives only through the FXS_BUTTON_A hatch in
    // UISimpleCommodityScreen.OnUnrealCommand, which is `ISCONTROLLERACTIVE-gated. So we resume
    // the game's own routing one hop below the break: the list, which answers Enter by handing
    // it to the selected row, whose confirm click fires OnItemDoubleClicked → OnPurchaseClicked.
    // Gated on the game's answer rather than on class, so a screen that handles its Enter
    // (LootRecovered's close, every purchase that went through) never gets a second one.
    return SendRowCommand(view.list, kCmdEnter);
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
    void* screen = PreferStack("UIChooseFacility", sFinder, sReported, ClassMatch::Derived);
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

// ---- choosing a facility upgrade (UIChooseUpgrade) ----
//
// See the header: the screen every upgrade path actually opens, and UIChooseFacility's twin in
// layout and in input. The list sits at the same offset on both classes; that is pinned for
// EACH class in sdk_layout_checks.cpp rather than assumed from the other, because two screens
// sharing a number today is a coincidence of their declaration order, not a contract.

void* FindChooseUpgradeScreen()
{
    // Stack first, like every screen-class finder — XComHQPresentationLayer.UIFacilityUpgrade
    // pushes this screen, so the stack has it — with the GObjects scan as the logged fallback.
    static ScreenFinder sFinder{"UIChooseUpgrade"};
    static bool sReported = false;
    void* screen = PreferStack("UIChooseUpgrade", sFinder, sReported, ClassMatch::Derived);
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

ChooseFacilityView ReadChooseUpgrade(void* screen)
{
    // The same read, deliberately: m_List at 0x418, UIListItemString rows populated through
    // SetText (names retained), bDisabled from UpgradeAlreadyPerformed || !HasEnoughPower ||
    // !CanBuildUpgrade, and the attention icon from bPriority — UIChooseUpgrade.PopulateList is
    // UIChooseFacility.PopulateList line for line. Selection and activation go through the
    // same two functions as well, since OnAccept reads the screen's own SelectedIndex here too.
    return ReadChooseFacility(screen);
}

// ---- the black market (UIBlackMarket entry menu, UIBlackMarket_Sell) ---------------------------
//
// See the header for what each screen retains. The Buy side is the commodity reader's.

namespace {

// UIBlackMarket
constexpr int32_t kBlackMarketTitleOffset = 0x410;         // m_strTitle
constexpr int32_t kBlackMarketInterestTitleOffset = 0x520; // m_strInterestTitle
constexpr int32_t kBlackMarketLibraryPanelOffset = 0x550;  // LibraryPanel — the quote's Flash panel
constexpr int32_t kBlackMarketBuyButtonOffset = 0x558;     // Button1
constexpr int32_t kBlackMarketSellButtonOffset = 0x560;    // Button2
// The welcome line's Flash call (UIBlackMarket.BuildScreen): one string operand.
constexpr const char* kBlackMarketQuoteCall = "SetMenuQuote";

// UIBlackMarket_Sell
constexpr int32_t kSellSaleAmountOffset = 0x410;      // SaleAmount
constexpr int32_t kSellConfirmButtonOffset = 0x418;   // ConfirmButton
constexpr int32_t kSellInventoryLabelOffset = 0x430;  // m_strInventoryLabel
constexpr int32_t kSellSellLabelOffset = 0x440;       // m_strSellLabel
constexpr int32_t kSellTotalLabelOffset = 0x450;      // m_strTotalLabel
constexpr int32_t kSellCostLabelOffset = 0x460;       // m_strCostLabel
constexpr int32_t kSellInterestedLabelOffset = 0x470; // m_strInterestedLabel
constexpr int32_t kSellQuantityLabelOffset = 0x480;   // m_strQuantityLabel
constexpr int32_t kSellListOffset = 0x4C0;            // List

// UIBlackMarket_SellItem
constexpr int32_t kSellItemNumSellingOffset = 0x400; // NumSelling
constexpr int32_t kSellItemRefOffset = 0x404;        // ItemRef.ObjectID
constexpr int32_t kSellItemPriceOffset = 0x410;      // Price

// BuildScreen sizes the list for sixteen rows; the market's BuyPrices table is the real bound,
// and a modded market could be longer. Loud when clipped, like every list walk in this file.
constexpr int kMaxSellRows = 64;

BlackMarketButton ReadMarketButton(void* button)
{
    BlackMarketButton b;
    b.handle = button;
    if (!button)
        return b;
    b.label = CleanText(ReadFStringField(button, kButtonTextOffset));
    b.disabled = (ReadU32Raw(button, kButtonFlagsOffset) & kButtonDisabled) != 0;
    // RefreshButtons hands SetDisabled the reason as its tooltip text, and SetDisabled(false)
    // removes the tooltip again — so the tooltip IS the reason, present exactly while greyed.
    if (b.disabled)
        b.reason = CleanText(ReadPanelTooltip(button));
    return b;
}

std::string ReadLabel(void* obj, int32_t offset)
{
    return CleanText(ReadFStringField(obj, offset));
}

} // namespace

void* FindBlackMarketScreen()
{
    // The Buy and Sell screens are pushed OVER this one and are SIBLINGS of it (all three extend
    // UIScreen), so tolerating a subclass here cannot confuse the three.
    return FindStackScreenRaw("UIBlackMarket", ClassMatch::Derived);
}

BlackMarketView ReadBlackMarket(void* screen)
{
    BlackMarketView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.title = ReadLabel(screen, kBlackMarketTitleOffset);
    v.interestTitle = ReadLabel(screen, kBlackMarketInterestTitleOffset);
    v.buy = ReadMarketButton(ReadPtrRaw(screen, kBlackMarketBuyButtonOffset));
    v.sell = ReadMarketButton(ReadPtrRaw(screen, kBlackMarketSellButtonOffset));

    // The welcome line: m_strWelcome[Rand(NUM_WELCOMES)], chosen in BuildScreen and pushed through
    // LibraryPanel.MC.FunctionString("SetMenuQuote"). The index is never stored, so the capture
    // is the only reader that can know which of the seven was drawn. Best effort by design: an
    // evicted capture leaves the quote empty and the recipe simply has no line for it.
    void* library = ReadPtrRaw(screen, kBlackMarketLibraryPanelOffset);
    const int32_t index = library ? FlashShadow::PanelCacheIndex(library) : -1;
    FlashShadow::Call call;
    if (index >= 0 && FlashShadow::GetCall(index, kBlackMarketQuoteCall, call))
        v.quote = CleanText(FlashArg(call.StringArgs(), 0));
    return v;
}

void* FindBlackMarketSellScreen()
{
    return FindStackScreenRaw("UIBlackMarket_Sell", ClassMatch::Derived);
}

SellView ReadBlackMarketSell(void* screen)
{
    SellView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.saleAmount = ReadI32Raw(screen, kSellSaleAmountOffset);
    v.confirm = ReadPtrRaw(screen, kSellConfirmButtonOffset);
    if (v.confirm) {
        v.confirmLabel = ReadLabel(v.confirm, kButtonTextOffset);
        v.confirmDisabled = (ReadU32Raw(v.confirm, kButtonFlagsOffset) & kButtonDisabled) != 0;
    }
    v.inventoryLabel = ReadLabel(screen, kSellInventoryLabelOffset);
    v.quantityLabel = ReadLabel(screen, kSellQuantityLabelOffset);
    v.costLabel = ReadLabel(screen, kSellCostLabelOffset);
    v.sellLabel = ReadLabel(screen, kSellSellLabelOffset);
    v.totalLabel = ReadLabel(screen, kSellTotalLabelOffset);
    v.interestedLabel = ReadLabel(screen, kSellInterestedLabelOffset);

    v.list = ReadPtrRaw(screen, kSellListOffset);
    if (!v.list)
        return v;
    v.selected = ReadI32Raw(v.list, kListSelectedIndexOffset);

    void* rows[kMaxSellRows] = {};
    int rowCount = TakeListItemsRaw("Black market: sell rows", v.list, rows, kMaxSellRows);
    for (int i = 0; i < rowCount; i++) {
        // PopulateData spawns nothing but sell items into this list, but the index stays the
        // game's own either way — SetSelectedIndex and the companion's records are expressed in it.
        if (!rows[i] || !IsKindOfRaw(rows[i], "UIBlackMarket_SellItem"))
            continue;
        SellRow r;
        r.handle = rows[i];
        r.index = i;
        r.selling = ReadI32Raw(rows[i], kSellItemNumSellingOffset);
        r.price = ReadI32Raw(rows[i], kSellItemPriceOffset);
        r.itemId = ReadI32Raw(rows[i], kSellItemRefOffset);
        v.rows.push_back(r);
    }
    return v;
}

bool SetBlackMarketSellSelection(const SellView& view, int index)
{
    // List.OnSelectionChanged = SelectedItemChanged repopulates the item card, as on the
    // commodity list — this is what keeps the card stop describing the row under focus.
    return SetListIndex(view.list, index);
}

bool AdjustBlackMarketSellRow(const SellView& view, int index, int delta)
{
    if (!view.screen || delta == 0)
        return false;
    for (const SellRow& row : view.rows) {
        if (row.index != index)
            continue;
        // The row's OWN arrow handler: the click sound, ChangeQuantity (which refuses past what
        // the player owns and below zero), the screen's UpdateTotalValue and the card refresh —
        // one unit per press, the way the key itself works.
        return SendRowCommand(row.handle, delta > 0 ? kCmdArrowRight : kCmdArrowLeft);
    }
    return false;
}

bool ConfirmBlackMarketSale(const SellView& view)
{
    if (!view.screen)
        return false;
    // The top of the game's own routing: the screen's OnUnrealCommand answers Enter itself with
    // OnConfirmButtonClicked, ahead of the navigator and the list.
    return SendScreenCommand(view.screen, kCmdEnter);
}

// ---- recruiting (UIRecruitSoldiers) --------------------------------------------------------------
//
// See the header for what the screen retains (nothing on a row; the recruit array beside the
// list). Four offsets, all pinned in sdk_layout_checks.cpp.

namespace {

constexpr int32_t kRecruitListOffset = 0x410;       // List
constexpr int32_t kRecruitArrayOffset = 0x418;      // m_arrRecruits (TArray<XComGameState_Unit*>)
constexpr int32_t kRecruitTitleOffset = 0x428;      // m_strListTitle
constexpr int32_t kRecruitNoRecruitsOffset = 0x438; // m_strNoRecruits

// ResistanceHQ.Recruits is refilled to a handful each month; a modded pool could be longer. Loud
// when clipped, like every list walk in this file.
constexpr int kMaxRecruitRows = 64;

} // namespace

void* FindRecruitScreen()
{
    // Focused only: the not-enough-supplies dialog it raises pushes no screen, so the recruit
    // screen stays focused under it and the dialog recipe wins by probe order.
    //
    // The cleanest confirmed override there is: LWOTC's UIRecruitSoldiers_LW extends this class
    // and replaces UpdateData alone, leaving the inherited List and its UIRecruitmentListItem
    // rows — which the row filter below already matches tolerantly — exactly where they were.
    void* screen = FindStackScreenRaw("UIRecruitSoldiers", ClassMatch::Derived);
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

RecruitView ReadRecruits(void* screen)
{
    RecruitView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.title = ReadLabel(screen, kRecruitTitleOffset);
    v.emptyText = ReadLabel(screen, kRecruitNoRecruitsOffset);
    // The COUNT only. The elements are game-state objects the companion resolves for itself off
    // the same array, and a pointer read here would be a UObject held for nothing.
    const int32_t recruits = ReadArrayCountRaw(screen, kRecruitArrayOffset);
    v.recruits = recruits < 0 ? 0 : recruits;

    v.list = ReadPtrRaw(screen, kRecruitListOffset);
    if (!v.list)
        return v;
    v.selected = ReadI32Raw(v.list, kListSelectedIndexOffset);

    void* rows[kMaxRecruitRows] = {};
    const int rowCount = TakeListItemsRaw("Recruits: rows", v.list, rows, kMaxRecruitRows);
    for (int i = 0; i < rowCount; i++) {
        // UpdateData spawns nothing but recruit rows, but the index stays the game's own either
        // way — SetSelectedIndex, OnRecruitSelected and m_arrRecruits are all expressed in it.
        if (!rows[i] || !IsKindOfRaw(rows[i], "UIRecruitmentListItem"))
            continue;
        RecruitRow r;
        r.handle = rows[i];
        r.index = i;
        // UIListItemString::bDisabled — InitRecruitItem's SetDisabled(true) when the supplies
        // were short of the cost at build time. The game still lets the row be selected and
        // activated; it is the purchase it refuses, with its own dialog.
        r.disabled = (ReadU32Raw(rows[i], kListItemStringFlagsOffset) & kListItemDisabled) != 0;
        v.rows.push_back(r);
    }

    // The two halves are rebuilt together, so a mismatch means a read caught the screen
    // mid-UpdateData — worth a line, never a crash.
    if (v.recruits != static_cast<int>(v.rows.size())) {
        static int sLastReported = -1;
        if (sLastReported != v.recruits) {
            sLastReported = v.recruits;
            LOG_INFO("Recruits: %d state object(s) against %zu row(s)", v.recruits, v.rows.size());
        }
    }
    return v;
}

bool SetRecruitSelection(const RecruitView& view, int index)
{
    // List.OnSelectionChanged = OnRecruitChanged repaints the right-hand panel, as the commodity
    // list's does its card — this is what keeps the details stop describing the row under focus.
    return SetListIndex(view.list, index);
}

bool RecruitSelected(const RecruitView& view, int index)
{
    if (!view.screen || !view.list)
        return false;
    // Select first, then the SCREEN's Enter: UIRecruitSoldiers.OnUnrealCommand answers
    // FXS_KEY_ENTER with OnRecruitSelected(List, List.SelectedIndex) ahead of its navigator, and
    // the row's own RECRUIT button is out of the navigator entirely (SetConfirmButtonStyle calls
    // DisableNavigation on it). From here down, the index is the argument.
    SetListIndex(view.list, index);
    return SendScreenCommand(view.screen, kCmdEnter);
}

// ---- the post-mission rewards recap (UIRewardsRecap) ---------------------------------------------
//
// A finder and nothing else: the screen has no widgets and every word is the companion's.

void* FindRewardsRecapScreen()
{
    void* screen = FindStackScreenRaw("UIRewardsRecap", ClassMatch::Derived);
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

} // namespace GameUI

#ifdef _MSC_VER
#pragma pack(pop)
#endif
