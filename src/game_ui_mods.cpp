// The MOD-PROVIDED SCREENS family's readers — Mod Config Menu first. See the header for what
// this family is; docs/mod-config-menu-recon.md for how the screen was measured.
//
// ⛔ NO static_assert PINS ANYTHING IN THIS FILE, AND NONE CAN. Every class read here that begins
// `MCM_` is a Workshop mod's, absent from the generated SDK, so there is no dump comment to
// transcribe from and no `offsetof` to pin against. That is why nothing below reads a mod class at
// a numeric offset:
//   · widgets are found by WALKING ChildPanels from the screen and matching class names
//     (ObjectClassNamedRaw / IsKindOfRaw) — the tree is the mod's own, and the ENGINE fields the
//     walk reads (UIPanel::ChildPanels, the flag dword, UIList::ItemContainer, UIButton::Text,
//     UIText::Text, UIPanel::MCName) are game_ui_internal.h's, pinned in sdk_layout_checks.cpp;
//   · the screen's localized words are read BY NAME through UE3Reflect::ReadStringProperty, which
//     walks the class's UProperty chain at runtime and takes the offset the ENGINE holds.
// If a mod-class field is ever needed that neither route reaches (SelectedPageID, SettingsPanels),
// the answer is a by-name integer read through the same reflection walk in ue3_reflection.cpp —
// never a hard-coded offset, which no rebuild of ours could ever catch drifting.

#include "game_ui_mods.h"
#include "game_ui_internal.h"
#include "log.h"
#include "ue3_reflection.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

namespace GameUI {

namespace {

// Engine offsets this TU needs beyond game_ui_internal.h's shared set. Both are pinned in
// sdk_layout_checks.cpp already (UIPanel::MCName at the top of the file; UIText::Text/HtmlText in
// the tactical block), which is why no new assert accompanies them.
constexpr int32_t kPanelMCNameOffset = 0x27C; // UIPanel::MCName (FName)
constexpr int32_t kTextTextOffset = 0x3D8;    // UIText::Text — same slot as UIScrollingText::Text
constexpr int32_t kTextHtmlOffset = 0x3E8;    // UIText::HtmlText — same slot as UIScrollingText's

// The Flash instance name MCM_Slider gives its value display (MCM_Slider.uc: InitScrollingText
// ('SliderValueTextControl', ...)). An IDENTIFIER matched, never spoken.
constexpr const char* kSliderValueMCName = "SliderValueTextControl";

bool PanelVisibleRaw(void* panel)
{
    return panel && (ReadU32Raw(panel, kPanelFlagsOffset) & kPanelVisible) != 0;
}

// The first DIRECT child of `panel` whose class is `className`, or null. For a panel BELOW the
// screen, ChildPanels holds exactly its own children, which is every caller here (separators, the
// container, a page). ⛔ Never call this on the SCREEN: a screen's ChildPanels is the flat
// registry of every panel on it, itself included (game_ui_internal.h, kPanelParentPanelOffset),
// and Locate() filters that walk on ParentPanel instead. `what` names the site for the sized
// read's refusal line and must be a literal.
void* FindChildOfClass(const char* what, void* panel, const char* className,
    ClassMatch match = ClassMatch::Exact)
{
    if (!panel)
        return nullptr;
    for (void* child : ReadPtrArrayAllRaw(what, panel, kPanelChildPanelsOffset)) {
        if (!child || child == panel)
            continue;
        if (ObjectClassNamedRaw(child, className, match))
            return child;
    }
    return nullptr;
}

// A text widget's retained words, cleaned. Text FIRST, HtmlText as the fallback — the rule the
// soldier sheet earned: SetSubTitle / SetText fill both, SetHTMLText fills only the html field.
std::string WidgetText(void* widget)
{
    if (!widget)
        return std::string();
    std::string text = ReadFStringField(widget, kTextTextOffset);
    if (text.empty())
        text = ReadFStringField(widget, kTextHtmlOffset);
    return CleanText(text);
}

// The words on one of MCM's two separator widgets (MCM_UISettingSeparator, the page title line;
// MCM_GroupLabelSeparator, a group heading). Both are a UIImage with a UIText child `Title` and a
// UIScrollingText child `Description`; UpdateTitle → Title.SetSubTitle fills the UIText. Exact
// class: UIScrollingText extends UIPanel, not UIText, so the two children cannot be confused.
std::string SeparatorTitle(void* separator)
{
    return WidgetText(FindChildOfClass("ModConfig: separator children", separator, "UIText"));
}

// A UIButton reached by POINTER as a MenuItem — ReadButtonField's body without the offset read,
// because these buttons are found by walking, not held in a field we could name. Handle stays
// null when the button is hidden or unlabelled, exactly as there.
MenuItem ButtonFromPtr(void* btn)
{
    MenuItem item;
    if (!btn)
        return item;
    const uint32_t flags = ReadU32Raw(btn, kPanelFlagsOffset);
    if (!(flags & kPanelVisible))
        return item;
    item.label = CleanText(ReadFStringField(btn, kButtonTextOffset));
    if (item.label.empty())
        return item;
    item.handle = btn;
    item.focused = (flags & kPanelFocused) != 0;
    item.disabled = (ReadU32Raw(btn, kButtonFlagsOffset) & kButtonDisabled) != 0;
    return item;
}

// Every visible, labelled UIButton among `panel`'s direct children, in spawn order.
void AppendButtons(const char* what, void* panel, std::vector<MenuItem>* out)
{
    if (!panel)
        return;
    for (void* child : ReadPtrArrayAllRaw(what, panel, kPanelChildPanelsOffset)) {
        if (!child || child == panel || !ObjectClassNamedRaw(child, "UIButton"))
            continue;
        MenuItem item = ButtonFromPtr(child);
        if (item.handle)
            out->push_back(std::move(item));
    }
}

// A slider row's own value display: the UIScrollingText child named SliderValueTextControl, read
// only while it is SHOWING. Empty otherwise, and the caller speaks NO value then: MCM_Slider.
// SetEditable(false) hides the display AND the bar together, so a sighted player sees the label
// alone, and the bar's percent — still in UISlider::Percent — is a fact the game draws nowhere.
std::string SliderValueText(void* row)
{
    for (void* child : ReadPtrArrayAllRaw("ModConfig: slider row children", row,
             kPanelChildPanelsOffset)) {
        if (!child || child == row || !ObjectClassNamedRaw(child, "UIScrollingText"))
            continue;
        char name[64];
        if (!FNameAtAnsiRaw(static_cast<const uint8_t*>(child) + kPanelMCNameOffset, name,
                sizeof(name))
            || strcmp(name, kSliderValueMCName) != 0)
            continue;
        return PanelVisibleRaw(child) ? WidgetText(child) : std::string();
    }
    return std::string();
}

// Where the screen's parts are this frame. Nothing here is cached: the whole point of walking is
// that the page can appear (first tab click), swap (another tab) or stay hidden under a page the
// mod opened elsewhere, and a pointer held across frames is the never-cache rule's bug.
struct Layout {
    void* container = nullptr;    // Container: the screen's one DIRECT UIPanel child holding a UIList
    void* tabsList = nullptr;     // TabsList
    void* page = nullptr;         // the MCM_SettingsPanel whose OWN visible bit is set
    void* settingsList = nullptr; // its SettingsList
};

Layout Locate(void* screen)
{
    Layout l;
    if (!screen)
        return l;
    // The Container is spawned as class'UIPanel' LITERALLY (MCM_OptionsScreen.CreateSkeleton),
    // so exact is right — and necessary: MCM_SettingsPanel extends UIPanel and also holds a UIList,
    // so a tolerant match here would take the first PAGE for the container.
    // ⛔ The SCREEN's ChildPanels is the flat registry of every panel on it (UIPanel.uc: every
    // panel calls Screen.AddChild(self)), not its children — so it also holds every UIList's
    // ItemContainer, each an exact UIPanel. Those have no UIList child and the container is
    // spawned first, which is why an unfiltered walk happened to resolve; the ParentPanel test is
    // what makes it resolve BY RIGHT rather than by spawn order.
    for (void* child : ReadPtrArrayAllRaw("ModConfig: screen children", screen,
             kPanelChildPanelsOffset)) {
        if (!child || child == screen || !ObjectClassNamedRaw(child, "UIPanel"))
            continue;
        if (ReadPtrRaw(child, kPanelParentPanelOffset) != screen)
            continue;
        void* list = FindChildOfClass("ModConfig: container children", child, "UIList");
        if (!list)
            continue;
        l.container = child;
        l.tabsList = list;
        break;
    }
    if (!l.container)
        return l;

    // The current page. ChoosePanelByPageID calls Hide() on every other page's panel, and
    // UIPanel.Hide clears bIsVisible on THAT panel only — its rows keep theirs — so the panel's own
    // bit is the one discriminator, and it is also why the rows are read from this panel and never
    // from a sibling: a hidden page's rows all still read visible. Before the first tab click no
    // MCM_SettingsPanel exists at all (the facade creates it on first Show), and this stays null.
    for (void* child : ReadPtrArrayAllRaw("ModConfig: pages", l.container,
             kPanelChildPanelsOffset)) {
        if (!child || child == l.container || !ObjectClassNamedRaw(child, "MCM_SettingsPanel"))
            continue;
        if (!PanelVisibleRaw(child))
            continue;
        l.page = child;
        break;
    }
    if (l.page)
        l.settingsList = FindChildOfClass("ModConfig: page children", l.page, "UIList");
    return l;
}

} // namespace

void* FindModConfigScreen()
{
    // Stack only — see the header. Exact: the mod spawns class'MCM_OptionsScreen' literally, and
    // nothing subclasses it.
    // Focused AND topmost: a custom page (NewCustomSettingsPage) pushes that mod's own screen
    // over this one, and the focused bit alone is not trusted on this stack — see
    // IsTopStackScreenRaw for the screen underneath us that taught that.
    void* screen = FindStackScreenRaw("MCM_OptionsScreen");
    return (screen && IsFocusedRaw(screen) && IsTopStackScreenRaw(screen)) ? screen : nullptr;
}

std::string ReadModConfigRowTooltip(void* row)
{
    if (!row)
        return {};
    std::string tip = ReadRowTooltip(row);
    if (tip.empty()) {
        // The row's own retained copy (MCM_SettingBase.DisplayTooltip), by name — the field the
        // mod's SetTooltipText call is fed from, present from InitSettingsItem on.
        tip = CleanText(
            UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(row), "DisplayTooltip"));
    }
    return tip;
}

void ReadModConfigHeading(void* screen, std::string* title, std::string* subtitle)
{
    if (!screen)
        return;
    // By NAME: the two are `localized` vars on the mod class, and the reflection walk finds their
    // offsets on the class the engine actually loaded.
    if (title)
        *title = CleanText(
            UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(screen), "m_strTitle"));
    if (subtitle)
        *subtitle = CleanText(
            UE3Reflect::ReadStringProperty(reinterpret_cast<UObject*>(screen), "m_strSubtitle"));
}

ModConfigView ReadModConfig(void* screen)
{
    ModConfigView v;
    if (!screen)
        return v;
    v.screen = screen;
    ReadModConfigHeading(screen, &v.title, &v.subtitle);

    const Layout l = Locate(screen);
    if (!l.container)
        return v; // the skeleton is built in InitScreen; an empty view is the frame before it

    // The tabs: MCM_SettingsTab extends UIMechaListItem, one checkbox row per page, so MakeRow
    // reads them as it reads any settings row — label off Desc, checked off the box. Sized from
    // the list: a player has as many tabs as client mods.
    v.tabsList = l.tabsList;
    if (v.tabsList) {
        v.selectedTab = ReadI32Raw(v.tabsList, kListSelectedIndexOffset);
        v.tabs = ReadListRowsAll("ModConfig: tabs", v.tabsList);
    }

    // Save and Exit, then Cancel — spawn order in CreateSkeleton, which is on-screen order right
    // to left; the game's own settings screen lists Save first too.
    AppendButtons("ModConfig: container buttons", l.container, &v.buttons);

    if (!l.page)
        return v;
    v.pageShown = true;
    v.settingsList = l.settingsList;
    if (!v.settingsList)
        return v;
    v.selectedRow = ReadI32Raw(v.settingsList, kListSelectedIndexOffset);

    // The page's rows, in on-screen order. ShowSettings inserts every item with MoveItemToTop,
    // which swaps it down ItemContainer.ChildPanels to index 0, so the array IS the drawn order:
    // the title line, then per group its heading followed by its settings, then the transparent
    // bottom-padding UIImage. Sized from the list: a mod adds as many settings as it likes.
    void* itemContainer = ReadPtrRaw(v.settingsList, kListItemContainerOffset);
    if (!itemContainer)
        return v;
    const std::vector<void*> items
        = ReadPtrArrayAllRaw("ModConfig: settings rows", itemContainer, kPanelChildPanelsOffset);
    for (size_t i = 0; i < items.size(); i++) {
        void* item = items[i];
        if (!item)
            continue;
        if (ObjectClassNamedRaw(item, "MCM_UISettingSeparator")) {
            v.pageTitle = SeparatorTitle(item);
            continue;
        }
        if (ObjectClassNamedRaw(item, "MCM_GroupLabelSeparator")) {
            ModConfigItem heading;
            heading.heading = true;
            heading.text = SeparatorTitle(item);
            // Emitted even with NO words: the entry is the group BOUNDARY, and dropping it would
            // leave the previous group's context open over this group's rows. The recipe closes
            // the open group on every heading entry and opens a new one only when there are
            // words — an unlabelled group (AddGroup(name, "") is legal, and GetFormattedLabel
            // then yields bare font tags CleanText strips to nothing) reads under the page title.
            v.items.push_back(std::move(heading));
            continue;
        }
        // Every setting is an MCM_SettingBase, which extends UIMechaListItem; the only other
        // thing in the container is the bottom padding (a bare UIImage), and anything a future
        // MCM version adds that is not a mecha row is dropped rather than read with row offsets.
        if (!IsKindOfRaw(item, "UIMechaListItem"))
            continue;
        ModConfigItem entry;
        if (!MakeRow(item, static_cast<int>(i), &entry.row))
            continue;
        if (entry.row.widget == OptionWidget::Slider)
            entry.sliderValue = SliderValueText(item);
        v.items.push_back(std::move(entry));
    }

    // The page's Reset button: Hide()n at spawn and shown only when the mod called
    // EnableResetButton, so the visibility test inside is the whole gate.
    AppendButtons("ModConfig: page buttons", l.page, &v.buttons);
    return v;
}

bool SetModConfigTabIndex(void* screen, int index)
{
    return SetListIndex(Locate(screen).tabsList, index);
}

bool SetModConfigRowIndex(void* screen, int index)
{
    return SetListIndex(Locate(screen).settingsList, index);
}

} // namespace GameUI
