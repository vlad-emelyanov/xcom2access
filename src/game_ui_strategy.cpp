// The STRATEGY family's readers: squad select, the armory loadout, after action, promotion,
// the dropship briefing, covert actions, resistance orders and the geoscape. Split out of
// game_ui.cpp along the recipe-family axis; the raw primitives stay in game_ui.cpp behind
// game_ui_internal.h.

#include "game_ui_strategy.h"
#include "game_ui_internal.h"
#include "log.h"

#include <cstring>
#include <string>

// Hand-rolled ProcessEvent parameter blocks below must match the engine's pack(4) layout.
#ifdef _MSC_VER
#pragma pack(push, 0x4)
#endif

namespace GameUI {

namespace {

// ---- squad select: UISquadSelect + its cards (docs/squad-select-recon.md §3) ----
constexpr int32_t kSquadSelectedSlotOffset = 0x410; // m_iSelectedSlot — LAGS, never the authority
constexpr int32_t kSquadSlotListOffset = 0x420;     // m_kSlotList (UIList_SquadEditor*)
constexpr int32_t kSquadLaunchButtonOffset = 0x428; // LaunchButton (UILargeButton*)
constexpr int32_t kSquadSoldierSlotsOffset = 0x460; // SoldierSlotCount (int)
constexpr int32_t kSquadFlagsOffset = 0x478;        // UISquadSelect flag dword
constexpr uint32_t kSquadLaunched = 0x0001;         // bLaunched
constexpr uint32_t kSquadWalkupDone = 0x0002;       // bReceivedWalkupEvent — input gate
constexpr uint32_t kSquadNoCancel = 0x0020;         // bNoCancel
constexpr uint32_t kSquadDisableLoadout = 0x0100;   // bDisableLoadout
constexpr int32_t kSquadLaunchLabelOffset = 0x480;  // m_strLaunch (FString)
constexpr int32_t kSquadMissionLabelOffset = 0x490; // m_strMission (FString)
constexpr int32_t kSquadStripGearOffset = 0x4C0;    // m_strStripGear (FString)
constexpr int32_t kSquadStripWeaponsOffset = 0x4F0; // m_strStripWeapons (FString)
constexpr int32_t kSquadStripItemsOffset = 0x520;   // m_strStripItems (FString)
constexpr int32_t kSquadBuildItemsOffset = 0x550;   // m_strBuildItems (FString)
constexpr int32_t kSquadBoostSoldierOffset = 0x570; // m_strBoostSoldier (FString)

// UISquadSelect_ListItem — one soldier card.
constexpr int32_t kCardSlotIndexOffset = 0x3D8;   // SlotIndex — into XComHQ.Squad, NOT visual
constexpr int32_t kCardFlagsOffset = 0x3DC;       // UISquadSelect_ListItem flag dword
constexpr uint32_t kCardDisabled = 0x0001;        // bDisabled — the greyed locked card
constexpr uint32_t kCardDisabledLoadout = 0x0002; // bDisabledLoadout
constexpr uint32_t kCardDisabledEdit = 0x0004;    // bDisabledEdit
constexpr uint32_t kCardDisabledDismiss = 0x0008; // bDisabledDismiss
constexpr int32_t kCardUtilitySlotsOffset = 0x408; // UtilitySlots (UIList*)
constexpr int32_t kCardBondIconOffset = 0x410;     // BondIcon (UIBondIcon*)
constexpr int32_t kCardActiveButtonOffset = 0x4E0; // m_eActiveButton (byte, ESquadSelectButton)

// UIArmory_Loadout / UIArmory_LoadoutItem. The flag dwords are pinned by the plain members
// on either side of them, the way every other bitfield in this file is.
constexpr int32_t kLoadoutActiveListOffset = 0x04FC;   // ActiveList (UIList*)
constexpr int32_t kLoadoutEquippedListOffset = 0x050C; // EquippedList
constexpr int32_t kLoadoutLockerListOffset = 0x051C;   // LockerList
constexpr int32_t kLoadoutInventoryTitleOffset = 0x053C;
constexpr int32_t kLoadoutLockerTitleOffset = 0x054C;
constexpr int32_t kLoadoutTipStripItemsOffset = 0x09DC;
constexpr int32_t kLoadoutTipStripItemsOffOffset = 0x09EC;
constexpr int32_t kLoadoutTipStripGearOffset = 0x09FC;
constexpr int32_t kLoadoutTipStripGearOffOffset = 0x0A0C;
constexpr int32_t kLoadoutTipStripWeaponsOffset = 0x0A1C;
constexpr int32_t kLoadoutTipStripWeaponsOffOffset = 0x0A2C;
constexpr int32_t kLoadoutMakeAvailableOffset = 0x0A4C;
constexpr int32_t kLoadoutFlagsOffset = 0x0A74; // between LocTag (0xA6C) and StrippedUnits (0xA78)
constexpr uint32_t kLoadoutGearStripped = 0x0001;
constexpr uint32_t kLoadoutItemsStripped = 0x0002;
constexpr uint32_t kLoadoutWeaponsStripped = 0x0004;

constexpr int32_t kLoadItemTitleOffset = 0x03D8;
constexpr int32_t kLoadItemSubtitleOffset = 0x03E8;
constexpr int32_t kLoadItemSlotTypeOffset = 0x0428;
constexpr int32_t kLoadItemCountOffset = 0x0438;
constexpr int32_t kLoadItemFlagsOffset = 0x043C; // between Count and EquipmentSlot
constexpr uint32_t kLoadItemLocked = 0x0001;
constexpr uint32_t kLoadItemInfinite = 0x0002;
constexpr uint32_t kLoadItemDisabled = 0x0004;
constexpr uint32_t kLoadItemCanBeCleared = 0x0008;
constexpr uint32_t kLoadItemLoadoutSlot = 0x0010;
constexpr int32_t kLoadItemEquipSlotOffset = 0x0440; // EInventorySlot (byte)
constexpr int32_t kLoadItemDropButtonOffset = 0x0458;

// UISquadSelect_UtilityItem — one small inventory slot on a card.
constexpr int32_t kUtilButtonOffset = 0x3E0;    // Button (UIButton*) — carries the disabled bit
constexpr int32_t kUtilSlotIndexOffset = 0x3F0; // SlotIndex (int)
constexpr int32_t kUtilSlotTypeOffset = 0x3F4;  // SlotType (byte, EInventorySlot)

// ---- after action: UIAfterAction + its soldier panels (docs/after-action-recon.md §3) ----
constexpr int32_t kAfterActionFlagsOffset = 0x410;  // UIAfterAction flag dword
constexpr uint32_t kAfterActionHudShown = 0x0004;   // bRecievedShowHUDRemoteEvent — input gate
constexpr int32_t kAfterActionSlotListOffset = 0x4E4; // m_kSlotList (UIList*)

// UIAfterAction_ListItem — one soldier panel. The words all went into Flash; what it keeps is
// the unit ref, two flags and the two buttons.
constexpr int32_t kAAItemUnitRefOffset = 0x3D8;     // UnitReference (StateObjectReference, int)
constexpr int32_t kAAItemPromoteButtonOffset = 0x454; // PromoteButton — ALSO the memorial button
constexpr int32_t kAAItemFlagsOffset = 0x464;       // UIAfterAction_ListItem flag dword
constexpr uint32_t kAAItemCanPromote = 0x0002;      // m_bCanPromote
constexpr uint32_t kAAItemKIA = 0x0004;             // m_bKIA — PromoteButton means Memorialize
constexpr int32_t kAAItemBondIconOffset = 0x468;    // BondIcon (UIBondIcon*)

// Caps for the squad-select / armory list reads. Each is a bound WE chose rather than one the
// game states, so each read goes through TakeListItemsRaw and says so if the game hands us more.
constexpr int kMaxSquadCards = 16;   // the squad's slot cards
constexpr int kMaxUtilitySlots = 16; // one card's utility row
constexpr int kMaxLoadoutRows = 64;  // the armory's inventory-slot rows

// A card's utility row, expanded into one control per slot. Each is separately focusable (the
// game's own Left/Right walks them) and separately locked, and the Highlander rebuilds this list
// through CHUIItemSlotEnumerator — so it is READ, never derived from the unit's pockets, which is
// what keeps a slot-adding mod from shifting every index under us (recon §3).
// One list into LoadoutRow structs, in the list's own order.
void ReadLoadoutRows(void* list, std::vector<LoadoutRow>& out)
{
    void* items[kMaxLoadoutRows] = {};
    const int count = TakeListItemsRaw("Loadout: slot rows", list, items, kMaxLoadoutRows);
    for (int i = 0; i < count; i++) {
        LoadoutRow row;
        row.handle = items[i];
        row.dropButton = ReadPtrRaw(items[i], kLoadItemDropButtonOffset);
        row.slotLabel = CleanText(ReadFStringField(items[i], kLoadItemSlotTypeOffset));
        row.title = CleanText(ReadFStringField(items[i], kLoadItemTitleOffset));
        row.subtitle = CleanText(ReadFStringField(items[i], kLoadItemSubtitleOffset));
        row.count = ReadI32Raw(items[i], kLoadItemCountOffset);
        row.equipmentSlot = static_cast<int>(ReadU8Raw(items[i], kLoadItemEquipSlotOffset));
        const uint32_t flags = ReadU32Raw(items[i], kLoadItemFlagsOffset);
        row.infinite = (flags & kLoadItemInfinite) != 0;
        row.disabled = (flags & kLoadItemDisabled) != 0;
        row.canBeCleared = (flags & kLoadItemCanBeCleared) != 0;
        row.loadoutSlot = (flags & kLoadItemLoadoutSlot) != 0;
        out.push_back(std::move(row));
    }
}

void AppendUtilityControls(void* card, std::vector<SquadControl>& out)
{
    void* list = ReadPtrRaw(card, kCardUtilitySlotsOffset);
    void* items[kMaxUtilitySlots] = {};
    const int count = TakeListItemsRaw("SquadSelect: utility slots", list, items,
        kMaxUtilitySlots);
    for (int i = 0; i < count; i++) {
        SquadControl c;
        c.handle = items[i];
        c.button = SquadButton::Utility;
        // The LIST position, which is what SetSelectedIndex and the game's own
        // ShiftHorizontalFocus count in; the item's own SlotIndex is its index WITHIN an
        // inventory slot type and repeats across types, so it is not a cursor.
        c.utilityIndex = i;
        c.utilitySlotType = static_cast<int>(ReadU8Raw(items[i], kUtilSlotTypeOffset));
        // The lock lives on the item's own Button (SetLocked disables it), not on the panel.
        void* button = ReadPtrRaw(items[i], kUtilButtonOffset);
        c.disabled = !button || (ReadU32Raw(button, kButtonFlagsOffset) & kButtonDisabled) != 0;
        // ⛔ SetLocked writes the reason and NOTHING ELSE — not SlotType, not SlotIndex — so a
        // locked slot is the one case the companion cannot name from the widget, and this
        // tooltip is the whole of what the game says about it.
        c.tooltip = ReadPanelTooltip(button);
        out.push_back(c);
    }
}

} // namespace

void* FindArmoryLoadout()
{
    return FindScreen(kScrArmoryLoadout);
}

void* FindSquadSelect()
{
    return FindScreen(kScrSquadSelect);
}

LoadoutView ReadArmoryLoadout(void* screen)
{
    LoadoutView v;
    if (!screen)
        return v;

    void* equippedList = ReadPtrRaw(screen, kLoadoutEquippedListOffset);
    void* lockerList = ReadPtrRaw(screen, kLoadoutLockerListOffset);
    void* activeList = ReadPtrRaw(screen, kLoadoutActiveListOffset);
    if (!equippedList || !activeList)
        return v; // still building — InitArmory spawns both before PopulateData

    v.active = (activeList == lockerList && lockerList) ? LoadoutList::Locker
                                                        : LoadoutList::Equipped;
    ReadLoadoutRows(activeList, v.rows);
    v.selectedRow = ReadI32Raw(activeList, kListSelectedIndexOffset);
    // The equipped list stays on screen while the locker is open ("openList" only slides it),
    // so it is read as a mirror — and ONLY then, since when it is the active list it is v.rows.
    if (v.active == LoadoutList::Locker)
        ReadLoadoutRows(equippedList, v.equipped);

    v.inventoryTitle = CleanText(ReadFStringField(screen, kLoadoutInventoryTitleOffset));
    v.lockerTitle = CleanText(ReadFStringField(screen, kLoadoutLockerTitleOffset));
    v.makeAvailable = CleanText(ReadFStringField(screen, kLoadoutMakeAvailableOffset));

    const uint32_t flags = ReadU32Raw(screen, kLoadoutFlagsOffset);
    v.gearStripped = (flags & kLoadoutGearStripped) != 0;
    v.itemsStripped = (flags & kLoadoutItemsStripped) != 0;
    v.weaponsStripped = (flags & kLoadoutWeaponsStripped) != 0;
    v.tipStripItems = CleanText(ReadFStringField(screen, kLoadoutTipStripItemsOffset));
    v.tipStripItemsOff = CleanText(ReadFStringField(screen, kLoadoutTipStripItemsOffOffset));
    v.tipStripGear = CleanText(ReadFStringField(screen, kLoadoutTipStripGearOffset));
    v.tipStripGearOff = CleanText(ReadFStringField(screen, kLoadoutTipStripGearOffOffset));
    v.tipStripWeapons = CleanText(ReadFStringField(screen, kLoadoutTipStripWeaponsOffset));
    v.tipStripWeaponsOff = CleanText(ReadFStringField(screen, kLoadoutTipStripWeaponsOffOffset));

    v.valid = true;
    return v;
}

bool SetLoadoutSelection(void* screen, int index)
{
    void* active = screen ? ReadPtrRaw(screen, kLoadoutActiveListOffset) : nullptr;
    if (!active || index < 0)
        return false;
    SetListIndex(active, index);
    return true;
}

bool ActivateLoadoutSelection(void* screen)
{
    if (!screen)
        return false;
    // ⛔ OnAccept() reads ActiveList.SelectedIndex and ignores everything else, so the caller
    // MUST have written the selection first — the same ordering the save/load rows earned, where
    // a click sent while the two disagreed deleted the wrong save.
    uint8_t noParams[16] = {};
    return CallNamedFunction(screen, "OnAccept", noParams);
}

bool ClearLoadoutRow(const LoadoutRow& row)
{
    if (!row.handle)
        return false;
    // The handler's UIButton parameter is dead weight in its body — it works from the row's own
    // ItemRef — but the real button is right there, so the frame is honest either way.
    void* parms[2] = {row.dropButton, nullptr};
    return CallNamedFunction(row.handle, "OnDropItemClicked", parms);
}

SquadSelectView ReadSquadSelect(void* screen)
{
    SquadSelectView v;
    if (!screen)
        return v;

    const uint32_t flags = ReadU32Raw(screen, kSquadFlagsOffset);
    v.walkupDone = (flags & kSquadWalkupDone) != 0;
    v.loadoutDisabled = (flags & kSquadDisableLoadout) != 0;
    v.canCancel = (flags & kSquadNoCancel) == 0;
    v.soldierSlots = ReadI32Raw(screen, kSquadSoldierSlotsOffset);

    v.launch = ReadPtrRaw(screen, kSquadLaunchButtonOffset);
    if (v.launch)
        v.launchDisabled = (ReadU32Raw(v.launch, kButtonFlagsOffset) & kButtonDisabled) != 0;
    // The button prints its two words on two lines; spoken they are one label.
    v.launchTitle = CleanText(ReadFStringField(screen, kSquadLaunchLabelOffset));
    const std::string mission = CleanText(ReadFStringField(screen, kSquadMissionLabelOffset));
    if (!mission.empty())
        v.launchTitle += (v.launchTitle.empty() ? "" : " ") + mission;

    void* list = ReadPtrRaw(screen, kSquadSlotListOffset);
    void* cards[kMaxSquadCards] = {};
    const int count = TakeListItemsRaw("SquadSelect: slot cards", list, cards, kMaxSquadCards);
    for (int i = 0; i < count; i++) {
        SquadSlot slot;
        slot.handle = cards[i];
        slot.slotIndex = ReadI32Raw(cards[i], kCardSlotIndexOffset);

        const uint32_t cardFlags = ReadU32Raw(cards[i], kCardFlagsOffset);
        slot.locked = (cardFlags & kCardDisabled) != 0;
        // bIsFocused, not m_iSelectedSlot: the screen's copy is written from OnReceiveFocus,
        // which RefreshFocus defers behind a 0.25 s timer, and it was caught live disagreeing.
        // This is the same bit the game's own GetActiveListItem() looks at (recon §5).
        slot.focused = (ReadU32Raw(cards[i], kPanelFlagsOffset) & kPanelFocused) != 0;
        slot.activeButton = static_cast<int>(ReadU8Raw(cards[i], kCardActiveButtonOffset));
        slot.bondIcon = ReadPtrRaw(cards[i], kCardBondIconOffset);
        // bHasTooltip on the CARD is set by CreateFlyoverTooltip and only for a tired soldier,
        // so this is empty exactly when the game shows no warning.
        slot.tooltip = ReadPanelTooltip(cards[i]);

        if (!slot.locked) {
            // Declaration order IS the card's top-to-bottom order. Edit and Dismiss are emitted
            // even when disabled — the game greys them and a sighted player still sees them, so
            // skipping (which is what vanilla's keyboard does) would lose parity. Promote, the
            // heavy weapon and the bond icon are NOT emitted here: those widgets are HIDDEN when
            // the soldier has none, and only the unit state knows which — the companion prunes
            // them, since it is already reading the unit for the words.
            SquadControl edit;
            edit.handle = cards[i];
            edit.button = SquadButton::Edit;
            edit.disabled = (cardFlags & kCardDisabledEdit) != 0;
            slot.controls.push_back(edit);

            SquadControl dismiss;
            dismiss.handle = cards[i];
            dismiss.button = SquadButton::Dismiss;
            dismiss.disabled = (cardFlags & kCardDisabledDismiss) != 0;
            slot.controls.push_back(dismiss);

            SquadControl primary;
            primary.handle = cards[i];
            primary.button = SquadButton::WeaponPrime;
            primary.disabled = (cardFlags & kCardDisabledLoadout) != 0;
            slot.controls.push_back(primary);

            AppendUtilityControls(cards[i], slot.controls);
        }
        v.slots.push_back(std::move(slot));
    }

    // Mouse-only on PC, every one of them (recon §4). Labels come from the screen's own localized
    // properties rather than from the nav help, which retains nothing but a delegate array.
    struct VerbDef {
        int32_t offset;
        const char* handler;
    };
    static constexpr VerbDef kVerbs[] = {
        {kSquadStripItemsOffset, "OnStripItems"},
        {kSquadStripWeaponsOffset, "OnStripWeapons"},
        {kSquadStripGearOffset, "OnStripGear"},
        {kSquadBuildItemsOffset, "OnBuildItems"},
        {kSquadBoostSoldierOffset, "OnBoostSoldier"},
    };
    for (const VerbDef& def : kVerbs) {
        SquadVerb verb;
        verb.label = CleanText(ReadFStringField(screen, def.offset));
        if (verb.label.empty())
            continue;
        verb.handler = def.handler;
        v.verbs.push_back(std::move(verb));
    }
    return v;
}

bool SetSquadSelectFocus(void* screen, int visualIndex, const SquadControl& control)
{
    void* list = ReadPtrRaw(screen, kSquadSlotListOffset);
    void* cards[kMaxSquadCards] = {};
    const int count = TakeListItemsRaw("SquadSelect: slot cards", list, cards, kMaxSquadCards);
    if (visualIndex < 0 || visualIndex >= count)
        return false;

    // The order the game's own ShiftFocus uses: drop the old card's focus, take the new one's,
    // then place the button. Doing it the other way round leaves two cards lit in Flash.
    for (int i = 0; i < count; i++) {
        if (i == visualIndex)
            continue;
        if (ReadU32Raw(cards[i], kPanelFlagsOffset) & kPanelFocused) {
            uint8_t none[16] = {};
            CallNamedFunction(cards[i], "OnLoseFocus", none);
        }
    }

    SetListIndex(list, visualIndex);
    uint8_t noParams[16] = {};
    CallNamedFunction(cards[visualIndex], "OnReceiveFocus", noParams);

    if (control.button == SquadButton::Utility) {
        // The utility row is a list of its own; the card's HandleUtilityItemFocus reads its
        // SelectedIndex back, so the row selection has to land before the button does.
        SetListIndex(ReadPtrRaw(cards[visualIndex], kCardUtilitySlotsOffset), control.utilityIndex);
    }

    // SelectUnit is ours, not ESquadSelectButton's (see the enum), so it must not be handed to a
    // function that switches on the game's own values. The card focus above is the whole of what
    // an empty card needs; enforcing that here rather than only documenting it.
    if (control.button == SquadButton::SelectUnit)
        return true;

    struct {
        int32_t ButtonEnumAsInt;
        uint32_t bReverseFocusIfUtility;
    } params{static_cast<int32_t>(control.button), 0};
    return CallNamedFunction(cards[visualIndex], "SetSquadSelectButtonFocus", &params);
}

bool ActivateSquadControl(void* screen, const SquadSlot& slot, const SquadControl& control)
{
    (void)screen;
    if (!slot.handle)
        return false;

    // Deliberately NOT HandleButtonSelect(): it switches on the card's own m_eActiveButton, i.e.
    // on the GAME's idea of which control is focused rather than on ours, so a single frame of
    // disagreement fires the wrong verb — dismiss where the player asked for edit. Every leaf has
    // a direct entry point that takes no arguments (recon §7).
    if (control.button == SquadButton::Utility) {
        return ClickButton(ReadPtrRaw(control.handle, kUtilButtonOffset));
    }

    uint8_t noParams[16] = {};
    switch (control.button) {
    case SquadButton::Promote:
        return CallNamedFunction(slot.handle, "OnClickedPromote", noParams);
    case SquadButton::Edit:
        return CallNamedFunction(slot.handle, "OnClickedEditUnitButton", noParams);
    case SquadButton::Dismiss:
        return CallNamedFunction(slot.handle, "OnClickedDismissButton", noParams);
    case SquadButton::SelectUnit:
        // The empty card's only verb, and the one the game reaches from `selectUnitButtonMC` and
        // from HandleButtonSelect's ListItemIsEmpty() branch. It sets m_iSelectedSlot, snaps the
        // camera and opens UIPersonnel_SquadSelect with the screen's own ChangeSlot/UpdateState
        // delegates — everything the mouse path does.
        return CallNamedFunction(slot.handle, "OnClickedSelectUnitButton", noParams);
    case SquadButton::WeaponPrime:
        return CallNamedFunction(slot.handle, "OnClickedPrimaryWeapon", noParams);
    case SquadButton::WeaponHeavy:
        return CallNamedFunction(slot.handle, "OnClickedHeavyWeapon", noParams);
    case SquadButton::Bond:
        // OnClickBondIcon(UIBondIcon Icon) is the card's own handler and the icon is the one
        // parameter it never reads — its body looks the unit up through GetUnitRef() and branches
        // on bCanHaveBonds. Passing the real icon anyway costs nothing and keeps the frame
        // honest, so the usual "never hand script a pointer it might deref" rule is not even
        // tested here.
        {
            void* parms[2] = {slot.bondIcon, nullptr};
            return CallNamedFunction(slot.handle, "OnClickBondIcon", parms);
        }
    default:
        return false;
    }
}

// ---- after action (UIAfterAction) ----

void* FindAfterAction()
{
    return FindScreen(kScrAfterAction);
}

AfterActionView ReadAfterAction(void* screen)
{
    AfterActionView v;
    if (!screen)
        return v;

    v.hudShown = (ReadU32Raw(screen, kAfterActionFlagsOffset) & kAfterActionHudShown) != 0;

    void* list = ReadPtrRaw(screen, kAfterActionSlotListOffset);
    void* cards[kMaxSquadCards] = {};
    const int count = TakeListItemsRaw("AfterAction: soldier panels", list, cards,
        kMaxSquadCards);
    for (int i = 0; i < count; i++) {
        AfterActionSlot slot;
        slot.handle = cards[i];
        slot.focused = (ReadU32Raw(cards[i], kPanelFlagsOffset) & kPanelFocused) != 0;
        const uint32_t flags = ReadU32Raw(cards[i], kAAItemFlagsOffset);
        slot.canPromote = (flags & kAAItemCanPromote) != 0;
        slot.kia = (flags & kAAItemKIA) != 0;
        slot.promoteButton = ReadPtrRaw(cards[i], kAAItemPromoteButtonOffset);
        slot.bondIcon = ReadPtrRaw(cards[i], kAAItemBondIconOffset);
        v.slots.push_back(slot);
    }
    return v;
}

bool SetAfterActionFocus(void* screen, int visualIndex, AfterActionControl control)
{
    void* list = ReadPtrRaw(screen, kAfterActionSlotListOffset);
    void* cards[kMaxSquadCards] = {};
    const int count = TakeListItemsRaw("AfterAction: soldier panels", list, cards,
        kMaxSquadCards);
    if (visualIndex < 0 || visualIndex >= count)
        return false;

    // Same order the game's own focus path uses: drop the old card, take the new one, then
    // place the inner focus. OnReceiveFocus lands it on the promote button (or the bond icon
    // when there is no promote), so only the disagreeing case needs correcting after.
    for (int i = 0; i < count; i++) {
        if (i == visualIndex)
            continue;
        if (ReadU32Raw(cards[i], kPanelFlagsOffset) & kPanelFocused) {
            uint8_t none[16] = {};
            CallNamedFunction(cards[i], "OnLoseFocus", none);
        }
    }

    SetListIndex(list, visualIndex);
    uint8_t noParams[16] = {};
    CallNamedFunction(cards[visualIndex], "OnReceiveFocus", noParams);

    // Deterministic inner focus — NOT OnUpDownInputHandler, which toggles from whatever state
    // the game is in. Both calls are plain UIPanel focus functions; a control the card lacks
    // (null widget) leaves the card focus as the whole write.
    void* promote = ReadPtrRaw(cards[visualIndex], kAAItemPromoteButtonOffset);
    void* bond = ReadPtrRaw(cards[visualIndex], kAAItemBondIconOffset);
    if (control == AfterActionControl::Bond && bond) {
        if (promote)
            CallNamedFunction(promote, "OnLoseFocus", noParams);
        CallNamedFunction(bond, "OnReceiveFocus", noParams);
    } else if (control != AfterActionControl::Bond && promote) {
        if (bond)
            CallNamedFunction(bond, "OnLoseFocus", noParams);
        CallNamedFunction(promote, "OnReceiveFocus", noParams);
    }
    return true;
}

bool ActivateAfterActionControl(const AfterActionSlot& slot, AfterActionControl control)
{
    if (!slot.handle)
        return false;

    uint8_t noParams[16] = {};
    switch (control) {
    case AfterActionControl::Promote:
        // The zero-param wrapper the card's own Enter path lands on: calls
        // UIAfterAction.OnPromote(UnitReference), which slides the pawns away and pushes
        // UIArmory_Promotion with every side effect the game expects.
        return CallNamedFunction(slot.handle, "AfterActionPromote", noParams);
    case AfterActionControl::Memorial: {
        // The KIA card's verb — the same widget as promote with the delegate swapped. Its
        // UIButton parameter is never read; the real button rides along to keep the frame
        // honest. Opens UIArmory_Photobooth.
        void* parms[2] = {slot.promoteButton, nullptr};
        return CallNamedFunction(slot.handle, "OnClickedMemorialize", parms);
    }
    case AfterActionControl::Bond: {
        // OnClickBondIcon guards on bCanHaveBonds itself and never reads its parameter.
        void* parms[2] = {slot.bondIcon, nullptr};
        return CallNamedFunction(slot.handle, "OnClickBondIcon", parms);
    }
    }
    return false;
}

// ---- the Skyranger load screen (UIDropShipBriefing_MissionStart / _MissionEnd) ----
//
// The two variants share a base but not a layout: UIDropShipBriefingBase adds a TArray<int> at
// 0x410, and each subclass then declares its own strings from 0x420, in its own order. So the
// offsets below cannot be folded into one set, and the little table is what keeps the two apart.
//
// ⛔ Read the MOUSE+KEYBOARD prompt, never the console one. Both classes carry a second,
// controller-worded string (m_strConsoleLaunch / m_strConsolePressKeyToContinue) that the game
// substitutes a button GLYPH into via a %A token — speaking it would say "%A LAUNCH MISSION".
// The game picks between them on `ISCONTROLLERACTIVE and so, implicitly, do we by never
// transcribing the console one's offset at all.

namespace {

constexpr int32_t kDropStartLaunchOffset = 0x460;  // m_strLaunch
constexpr int32_t kDropStartLoadingOffset = 0x470; // m_strLoadingText
constexpr int32_t kDropStartButtonOffset = 0x4B4;  // LaunchButton
constexpr int32_t kDropStartTipOffset = 0x4C4;     // m_strCurrentTip

constexpr int32_t kDropEndButtonOffset = 0x424;  // LaunchButton
constexpr int32_t kDropEndPromptOffset = 0x43C;  // m_strPressKeyToContinue
constexpr int32_t kDropEndLoadingOffset = 0x4DC; // m_strLoadingText

} // namespace

DropshipView ReadDropship()
{
    DropshipView v;

    // Stack lookup, not a ScreenFinder: this screen lives on the TRANSITION map, where the
    // presentation layer is destroyed and rebuilt around it (XComPlayerController:1298). A
    // cached instance is exactly the corpse the hard rules warn about, and the stack — which
    // StackIsLiveRaw re-finds per call — is the one handle that survives the rebuild.
    //
    // ONE walk for both classes rather than FindStackScreenRaw twice: this poll runs for the
    // whole session and finds nothing in almost all of it, and taking whichever variant is
    // TOPMOST is also the more honest answer than preferring one by name.
    void* screens[kMaxStackScreens] = {};
    const int count = WalkStackRaw(screens, kMaxStackScreens);
    for (int i = 0; i < count && !v.screen; i++) {
        // Skip past anything that is not one of ours — unlike the GetCurrentScreen finders, a
        // screen over the briefing does not make the briefing the wrong answer.
        if (!screens[i] || !StackEntryLiveRaw(screens[i]))
            continue;
        char name[64];
        if (!ObjClassNameAnsiRaw(screens[i], name, sizeof(name)))
            continue;
        if (strcmp(name, "UIDropShipBriefing_MissionStart") == 0) {
            v.screen = screens[i];
            v.missionStart = true;
        } else if (strcmp(name, "UIDropShipBriefing_MissionEnd") == 0) {
            v.screen = screens[i];
        }
    }
    if (!v.screen)
        return v;

    // ⛔ The VISIBLE bit alone, not IsShownRaw. The game's readiness signal is one statement —
    // `LaunchButton.bIsVisible = true;` in OnCheckLoading — and nothing else on this screen ever
    // writes it, so that bit is the whole answer. Also requiring bIsInited would tie our
    // announcement to the button's Flash init callback, and if that callback never arrived we
    // would simply never say "ready": a silent failure indistinguishable from a slow load, which
    // is precisely the failure mode this feature exists to remove.
    const int32_t buttonOffset = v.missionStart ? kDropStartButtonOffset : kDropEndButtonOffset;
    if (void* button = ReadPtrRaw(v.screen, buttonOffset))
        v.ready = (ReadU32Raw(button, kPanelFlagsOffset) & kPanelVisible) != 0;

    if (v.missionStart) {
        v.loadingText = ReadFStringField(v.screen, kDropStartLoadingOffset);
        v.prompt = ReadFStringField(v.screen, kDropStartLaunchOffset);
        v.tip = ReadFStringField(v.screen, kDropStartTipOffset);
    } else {
        v.loadingText = ReadFStringField(v.screen, kDropEndLoadingOffset);
        v.prompt = ReadFStringField(v.screen, kDropEndPromptOffset);
    }
    return v;
}

// ---- covert actions (UICovertActions) ----
//
// See the header for the three-way split and the two ⛔s (destructive selection, mouse-only
// confirm). This half reads the ROWS, which really are retained, plus the four structural facts
// the companion cannot see: which action the panels are describing, whether one is already
// running, whether the game is in its own slot mode, and whether the launch button is on screen.

namespace {

constexpr int32_t kCovertActionRefOffset = 0x580;   // UICovertActions::ActionRef (ObjectID first)
constexpr int32_t kCovertActionsFlagsOffset = 0x5A4; // bActionInProgress .. bTutorialPopupDisplayed
constexpr uint32_t kCovertActionInProgress = 0x01;   // bActionInProgress
constexpr uint32_t kCovertActionSelectingSlots = 0x20; // bIsSelectingSlots
constexpr int32_t kCovertActionsListOffset = 0x5A8;   // UICovertActions::List
constexpr int32_t kCovertActionsLaunchOffset = 0x5B0; // UICovertActions::LaunchButton

// UIListItemString::metadataInt — the action's ObjectID on an action row, and the ONLY field that
// tells one row's action from another's. Zero on the faction headings, which never set it.
constexpr int32_t kListItemStringMetaIntOffset = 0x3D8;

// Four factions' worth of actions plus a heading each, bounded far above what the game builds.
constexpr int kMaxCovertActionRows = 64;

} // namespace

void* FindCovertActionsScreen()
{
    static ScreenFinder sFinder{"UICovertActions"};
    static bool sReported = false;
    void* screen = PreferStack("UICovertActions", sFinder, sReported);
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

CovertActionsView ReadCovertActions(void* screen)
{
    CovertActionsView v;
    if (!screen)
        return v;
    v.screen = screen;

    const uint32_t flags = ReadU32Raw(screen, kCovertActionsFlagsOffset);
    v.actionInProgress = (flags & kCovertActionInProgress) != 0;
    v.selectingSlots = (flags & kCovertActionSelectingSlots) != 0;
    v.actionId = ReadI32Raw(screen, kCovertActionRefOffset);

    // ⛔ Only while the game is SHOWING it. UpdateData calls LaunchButton.Hide() outright when an
    // action is in progress or influence is unmet, and UIButton::Click() refuses a hidden button
    // anyway — so a node built over a hidden one would advertise a verb that cannot fire. This is
    // the documented exception to never-gate-on-bIsVisible: a BUTTON's visibility is toggled
    // deliberately and does mean hidden.
    void* launch = ReadPtrRaw(screen, kCovertActionsLaunchOffset);
    if (launch && (ReadU32Raw(launch, kPanelFlagsOffset) & kPanelVisible) != 0) {
        v.launch = launch;
        v.launchDisabled = (ReadU32Raw(launch, kButtonFlagsOffset) & kButtonDisabled) != 0;
    }

    v.list = ReadPtrRaw(screen, kCovertActionsListOffset);
    if (!v.list)
        return v;

    v.selected = ReadI32Raw(v.list, kListSelectedIndexOffset);

    void* rows[kMaxCovertActionRows] = {};
    const int rowCount
        = TakeListItemsRaw("CovertActions: rows", v.list, rows, kMaxCovertActionRows);
    for (int i = 0; i < rowCount; i++) {
        if (!rows[i])
            continue;
        // ⛔ The list holds TWO kinds of row. BuildScreen spawns a UICovertOpsFactionListItem
        // heading whenever the faction changes, and that class pushes its words through
        // MC.BeginFunctionOp("populateData") — so it derives from UIListItemString, sits in the
        // same itemContainer, and has an EMPTY Text. It is also DisableNavigation()ed, i.e. the
        // game's own arrows skip it, so ours must too. Matched by CLASS rather than by the empty
        // Text: an empty answer is indistinguishable from a field looked up in the wrong place,
        // and this one has a name to ask for instead.
        char cls[64];
        if (ObjClassNameAnsiRaw(rows[i], cls, sizeof(cls))
            && strcmp(cls, "UICovertOpsFactionListItem") == 0)
            continue;

        CovertActionRow r;
        r.handle = rows[i];
        // The LIST's index, headings included — SetSelectedIndex counts them, so a row's position
        // among the actions is not the number to hand back to the game.
        r.index = i;
        r.actionId = ReadI32Raw(rows[i], kListItemStringMetaIntOffset);
        r.label = CleanText(ReadFStringField(rows[i], kListItemStringTextOffset));
        r.disabled = (ReadU32Raw(rows[i], kListItemStringFlagsOffset) & kListItemDisabled) != 0;
        v.rows.push_back(std::move(r));
    }
    return v;
}

bool SetCovertActionSelection(const CovertActionsView& view, int index)
{
    return SetListIndex(view.list, index);
}

bool ActivateCovertActionLaunch(const CovertActionsView& view)
{
    // No select-then-Enter dance here, and deliberately: OnLaunchClicked reads the screen's own
    // ActionRef, which only SelectedItemChanged writes — and that is already in step, because the
    // recipe's focus write-back is what moved it. Click() re-tests IsDisabled itself.
    if (!view.launch || view.launchDisabled)
        return false;
    return ClickButton(view.launch);
}

// ---- resistance orders (UIStrategyPolicy) ----
//
// See the header for why this screen needs a recipe rather than a reader. Native reads the
// STRUCTURE — which cards are in which slot of which column, which one is picked up, and whether
// the game will accept a change at all; every word comes from X2AccessPolicyReader, because a
// UIStrategyPolicy_Card retains none.

namespace {

constexpr int32_t kPolicyColumnsOffset = 0x4D0; // TArray<UIList*>, always four
constexpr int32_t kPolicyHandOffset = 0x4E0;    // UIStrategyPolicy_DeckList*
constexpr int32_t kPolicyFlagsOffset = 0x4F8;   // bDragging .. m_bLoopHandSelection
constexpr uint32_t kPolicyEnableDragging = 0x04; // bEnableDragging
constexpr uint32_t kPolicyResistanceReport = 0x10; // bResistanceReport
constexpr int32_t kPolicyInspectingOffset = 0x538;  // UIStrategyPolicy_Card*

// UIStrategyPolicy_Card. AssociatedFactionName at 0x3D8 is an FName and is deliberately NOT read:
// the companion has the faction from the card state, and an FName would be an untranslated
// 'Faction_Reapers' at the player.
// ⛔ ColumnIndex (0x3E0) and ColumnSlot (0x3E4) are real fields and are deliberately NOT read:
// only RealizeColumn's slot loop assigns them. CreateLockedCard — the locked placeholder appended
// AFTER that loop — leaves both at the default 0, so a column with a real slot 0 holds two cards
// answering (column, slot) identically, which live-crashed the build with a duplicate control id.
// The address both halves agree on is the LIST POSITION — the same index the companion's
// ResolveCard hands to GetItem().
constexpr int32_t kPolicyCardRefOffset = 0x3E8;   // CardRef (ObjectID first)
constexpr int32_t kPolicyCardFlagsOffset = 0x3EC; // bLocked | bVital
constexpr uint32_t kPolicyCardLocked = 0x01;
constexpr uint32_t kPolicyCardVital = 0x02;

// A column holds at most three slots plus one locked placeholder; the hand is the player's whole
// unplayed deck, which the game does not page on mouse+keyboard (FilterCardsToDisplay returns
// everything unless a controller is active). Both bounded far above what the game builds.
constexpr int kMaxPolicySlots = 16;
constexpr int kMaxPolicyHandCards = 64;
constexpr int kPolicyColumnCount = 4;

// column/slot are the caller's list position (column -1 = the hand), never the card's own
// ColumnIndex/ColumnSlot fields — see the offset block above for why those lie on a locked
// placeholder. List order is slot order: RealizeColumn creates the real slots in index order and
// appends the placeholder last, so the position is the truth the fields only approximate.
PolicyCard ReadPolicyCardRaw(void* handle, void* inspecting, int column, int slot)
{
    PolicyCard c;
    c.handle = handle;
    c.cardId = ReadI32Raw(handle, kPolicyCardRefOffset);
    const uint32_t flags = ReadU32Raw(handle, kPolicyCardFlagsOffset);
    c.locked = (flags & kPolicyCardLocked) != 0;
    c.vital = (flags & kPolicyCardVital) != 0;
    c.inspecting = handle == inspecting;
    c.column = column;
    c.slot = slot;
    return c;
}

} // namespace

void* FindStrategyPolicyScreen()
{
    static ScreenFinder sFinder{"UIStrategyPolicy"};
    static bool sReported = false;
    void* screen = PreferStack("UIStrategyPolicy", sFinder, sReported);
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

StrategyPolicyView ReadStrategyPolicy(void* screen)
{
    StrategyPolicyView v;
    if (!screen)
        return v;
    v.screen = screen;

    const uint32_t flags = ReadU32Raw(screen, kPolicyFlagsOffset);
    v.canPlay = (flags & kPolicyEnableDragging) != 0;
    v.resistanceReport = (flags & kPolicyResistanceReport) != 0;

    void* inspecting = ReadPtrRaw(screen, kPolicyInspectingOffset);
    if (inspecting)
        v.inspectingId = ReadI32Raw(inspecting, kPolicyCardRefOffset);

    void* lists[kPolicyColumnCount] = {};
    const int listCount
        = TakeArrayRaw("StrategyPolicy: columns", screen, kPolicyColumnsOffset, lists,
            kPolicyColumnCount);
    for (int i = 0; i < listCount; i++) {
        if (!lists[i])
            continue;
        PolicyColumn col;
        col.list = lists[i];
        col.index = i;
        void* cards[kMaxPolicySlots] = {};
        const int n = TakeListItemsRaw("StrategyPolicy: slots", lists[i], cards, kMaxPolicySlots);
        for (int j = 0; j < n; j++) {
            if (cards[j])
                col.cards.push_back(ReadPolicyCardRaw(cards[j], inspecting, i, j));
        }
        v.columns.push_back(std::move(col));
    }

    v.hand = ReadPtrRaw(screen, kPolicyHandOffset);
    if (v.hand) {
        void* cards[kMaxPolicyHandCards] = {};
        const int n
            = TakeListItemsRaw("StrategyPolicy: hand", v.hand, cards, kMaxPolicyHandCards);
        for (int j = 0; j < n; j++) {
            if (cards[j])
                v.handCards.push_back(ReadPolicyCardRaw(cards[j], inspecting, /*column*/ -1, j));
        }
    }
    return v;
}

// ---- promotion / level-up (UIArmory_Promotion + subclasses) ----

namespace {

constexpr int32_t kArmoryUnitRefOffset = 0x450;      // UIArmory::UnitReference (ObjectID first)

// UIArmory_Promotion (the classic list screen; UIArmory_PromotionPsiOp reuses it unchanged).
constexpr int32_t kPromoClassRowOffset = 0x53C;       // ClassRowItem (UIArmory_PromotionItem*)
constexpr int32_t kPromoSelectedBranchOffset = 0x614; // SelectedAbilityIndex (int)
constexpr int32_t kPromoListOffset = 0x618;           // List (UIList*)

// UIArmory_PromotionItem — one rank row of the classic screen.
constexpr int32_t kPromoItemRankOffset = 0x3D8;     // Rank (int) — an ABILITY TREE index
constexpr int32_t kPromoItemIcon1Offset = 0x404;    // AbilityIcon1 (UIIcon*)
constexpr int32_t kPromoItemIcon2Offset = 0x40C;    // AbilityIcon2 (UIIcon*)
constexpr int32_t kPromoItemSelectedOffset = 0x41C; // SelectedAbility (int)
constexpr int32_t kPromoItemFlagsOffset = 0x420;    // bIsDisabled | bEligibleForPromotion
constexpr uint32_t kPromoItemDisabled = 0x1;
constexpr uint32_t kPromoItemEligible = 0x2;

// UIArmory_PromotionHero and its columns.
constexpr int32_t kPromoHeroColumnsOffset = 0x624; // Columns (TArray<UIArmory_PromotionHeroColumn*>)
constexpr int32_t kPromoHeroSelectedColOffset = 0x6E4; // m_iCurrentlySelectedColumn (int)
constexpr int32_t kPromoColRankOffset = 0x3D8;         // Rank (int)
constexpr int32_t kPromoColIconsOffset = 0x3FC;        // AbilityIcons (TArray<UIIcon*>)
constexpr int32_t kPromoColFlagsOffset = 0x40C;        // bIsDisabled | bEligibleForPurchase
constexpr uint32_t kPromoColDisabled = 0x1;
constexpr uint32_t kPromoColEligible = 0x2;
constexpr int32_t kPromoColPanelIndexOffset = 0x420; // m_iPanelIndex (int)

// UIArmory_PromotionHero spawns exactly seven columns and NUM_ABILITIES_PER_COLUMN is 4; the
// classic screen's list is one row per rank. Both bounded well above what the game draws.
constexpr int kMaxPromotionRows = 32;
constexpr int kMaxPromotionBranches = 4;

// Probe order is the class hierarchy's, most-derived first — FindStackScreenRaw and
// IsInstanceRaw both match the EXACT class name, so a hero screen would never answer to
// "UIArmory_Promotion" and the three can be told apart cleanly.
struct PromotionClass {
    const char* name;
    PromotionKind kind;
};
constexpr PromotionClass kPromotionClasses[] = {
    {"UIArmory_PromotionHero", PromotionKind::Hero},
    {"UIArmory_PromotionPsiOp", PromotionKind::PsiOp},
    {"UIArmory_Promotion", PromotionKind::Classic},
};

ScreenFinder gPromoHeroFinder{"UIArmory_PromotionHero"};
ScreenFinder gPromoPsiFinder{"UIArmory_PromotionPsiOp"};
ScreenFinder gPromoClassicFinder{"UIArmory_Promotion"};
ScreenFinder* const kPromotionFinders[]
    = {&gPromoHeroFinder, &gPromoPsiFinder, &gPromoClassicFinder};
bool gPromoFallbackReported[3] = {};

// Which of the three classes `screen` is, or false when it is none of them.
bool PromotionKindOf(void* screen, PromotionKind* out)
{
    for (const PromotionClass& c : kPromotionClasses) {
        if (ObjectClassNamedRaw(screen, c.name)) {
            *out = c.kind;
            return true;
        }
    }
    return false;
}

// The ability slots of one row, in branch order. `icons` are the row's own UIIcon pointers; a
// HIDDEN one is the game saying the tree has no ability at that branch, which is what makes a
// ragged tree readable — the cell keeps its branch NUMBER while being unlandable.
void ReadPromotionCells(void* const* icons, int count, std::vector<PromotionCell>& out)
{
    for (int i = 0; i < count; i++) {
        PromotionCell cell;
        cell.branch = i;
        cell.icon = icons[i];
        cell.visible = cell.icon && (ReadU32Raw(cell.icon, kPanelFlagsOffset) & kPanelVisible) != 0;
        out.push_back(cell);
    }
}

} // namespace

void* FindPromotionScreen()
{
    for (int i = 0; i < 3; i++) {
        if (void* onStack = FindStackScreenRaw(kPromotionClasses[i].name))
            return onStack;
    }
    // The stack is the authority; this only covers the window in which it is being rebuilt.
    for (int i = 0; i < 3; i++) {
        void* found = kPromotionFinders[i]->Find();
        if (found) {
            if (!gPromoFallbackReported[i]) {
                gPromoFallbackReported[i] = true;
                LOG_INFO("GameUI: %s is not on the screen stack — falling back to an object scan",
                    kPromotionClasses[i].name);
            }
            return found;
        }
    }
    return nullptr;
}

PromotionView ReadPromotion(void* screen)
{
    PromotionView v;
    if (!screen || !PromotionKindOf(screen, &v.kind))
        return v;
    v.screen = screen;
    v.unitId = ReadI32Raw(screen, kArmoryUnitRefOffset);

    if (v.kind == PromotionKind::Hero) {
        void* columns[kMaxPromotionRows] = {};
        int n = ReadPtrArrayRaw(screen, kPromoHeroColumnsOffset, columns, kMaxPromotionRows);
        for (int i = 0; i < n; i++) {
            if (!columns[i])
                continue;
            PromotionRow row;
            row.handle = columns[i];
            row.rank = ReadI32Raw(columns[i], kPromoColRankOffset);
            uint32_t flags = ReadU32Raw(columns[i], kPromoColFlagsOffset);
            row.disabled = (flags & kPromoColDisabled) != 0;
            row.eligible = (flags & kPromoColEligible) != 0;
            row.selectedBranch = ReadI32Raw(columns[i], kPromoColPanelIndexOffset);

            void* icons[kMaxPromotionBranches] = {};
            int iconCount
                = ReadPtrArrayRaw(columns[i], kPromoColIconsOffset, icons, kMaxPromotionBranches);
            ReadPromotionCells(icons, iconCount, row.cells);
            v.rows.push_back(std::move(row));
        }
        // The hero screen navigates itself through its own pair of ints, so they ARE the cursor.
        int col = ReadI32Raw(screen, kPromoHeroSelectedColOffset);
        if (col >= 0 && col < static_cast<int>(v.rows.size())) {
            v.selectedRow = col;
            v.selectedBranch = v.rows[static_cast<size_t>(col)].selectedBranch;
        }
        v.valid = !v.rows.empty();
        return v;
    }

    // Classic / PsiOp. The class row is a SEPARATE widget from the list and it is rank 0 — the
    // list starts at rank 1. Reading it first is what puts the rows in tree order.
    if (void* classRow = ReadPtrRaw(screen, kPromoClassRowOffset)) {
        PromotionRow row;
        row.handle = classRow;
        row.rank = ReadI32Raw(classRow, kPromoItemRankOffset);
        uint32_t flags = ReadU32Raw(classRow, kPromoItemFlagsOffset);
        row.disabled = (flags & kPromoItemDisabled) != 0;
        row.eligible = (flags & kPromoItemEligible) != 0;
        row.selectedBranch = ReadI32Raw(classRow, kPromoItemSelectedOffset);
        void* icons[2] = {ReadPtrRaw(classRow, kPromoItemIcon1Offset),
            ReadPtrRaw(classRow, kPromoItemIcon2Offset)};
        ReadPromotionCells(icons, 2, row.cells);
        v.rows.push_back(std::move(row));
    }

    void* list = ReadPtrRaw(screen, kPromoListOffset);
    void* items[kMaxPromotionRows] = {};
    int n = ReadListItemsRaw(list, items, kMaxPromotionRows);
    for (int i = 0; i < n; i++) {
        if (!items[i])
            continue;
        PromotionRow row;
        row.handle = items[i];
        row.rank = ReadI32Raw(items[i], kPromoItemRankOffset);
        uint32_t flags = ReadU32Raw(items[i], kPromoItemFlagsOffset);
        row.disabled = (flags & kPromoItemDisabled) != 0;
        row.eligible = (flags & kPromoItemEligible) != 0;
        row.selectedBranch = ReadI32Raw(items[i], kPromoItemSelectedOffset);
        void* icons[2] = {ReadPtrRaw(items[i], kPromoItemIcon1Offset),
            ReadPtrRaw(items[i], kPromoItemIcon2Offset)};
        ReadPromotionCells(icons, 2, row.cells);
        v.rows.push_back(std::move(row));
    }

    // The list index is one behind ours, whose slot 0 is the class row. A NEGATIVE index is not
    // "nothing selected" here: UpdateClassRowSelection reads exactly that to mean the class row
    // has focus, which is where InitPromotion parks a rank-1 soldier.
    const bool haveClassRow = !v.rows.empty() && v.rows.front().rank == 0;
    int listIndex = list ? ReadI32Raw(list, kListSelectedIndexOffset) : -1;
    if (listIndex >= 0)
        v.selectedRow = haveClassRow ? listIndex + 1 : listIndex;
    else if (haveClassRow)
        v.selectedRow = 0;
    if (v.selectedRow >= static_cast<int>(v.rows.size()))
        v.selectedRow = -1;
    v.selectedBranch = ReadI32Raw(screen, kPromoSelectedBranchOffset);

    v.valid = !v.rows.empty();
    return v;
}

bool SetPromotionSelection(const PromotionView& view, int row, int branch)
{
    if (!view.valid || row < 0 || row >= static_cast<int>(view.rows.size()))
        return false;
    const PromotionRow& r = view.rows[static_cast<size_t>(row)];

    if (view.kind == PromotionKind::Hero) {
        // The hero screen navigates itself through two plain ints, so writing them IS its model
        // rather than a fight with it, and OnReceiveFocus does the highlight the same way its
        // controller branch does.
        WriteI32Raw(view.screen, kPromoHeroSelectedColOffset, row);
        WriteI32Raw(r.handle, kPromoColPanelIndexOffset, branch);
        CallNamedFunction(r.handle, "OnReceiveFocus", nullptr);
        // PreviewAbility repaints the description panel — the one thing that makes the SCREEN
        // show what the player is hearing. The game reaches it only from a mouse hover or the
        // controller path, which is exactly the already-written-but-unreachable shape this layer
        // exists to call.
        struct {
            int32_t Rank;
            int32_t Branch;
        } params{r.rank, branch};
        CallNamedFunction(view.screen, "PreviewAbility", &params);
        return true;
    }

    // Classic: drive the LIST, which fires OnSelectionChanged = PreviewRow and repaints the
    // description panel for free. The class row is not IN the list, so landing on it means
    // leaving the list where UpdateClassRowSelection expects it.
    void* list = ReadPtrRaw(view.screen, kPromoListOffset);
    const bool haveClassRow = view.rows.front().rank == 0;
    if (!(haveClassRow && row == 0))
        SetListIndex(list, haveClassRow ? row - 1 : row);
    // The row's own within-row cursor, and the screen's copy of it that PreviewRow reads back.
    WriteI32Raw(view.screen, kPromoSelectedBranchOffset, branch);
    struct {
        int32_t AbilityIndex;
    } params{branch};
    CallNamedFunction(r.handle, "SetSelectedAbility", &params);
    return true;
}

bool ActivatePromotionCell(const PromotionView& view, int row, int branch)
{
    if (!view.valid || row < 0 || row >= static_cast<int>(view.rows.size()))
        return false;
    const PromotionRow& r = view.rows[static_cast<size_t>(row)];
    if (branch < 0 || branch >= static_cast<int>(r.cells.size()))
        return false;

    // Selection first. Both row classes read their OWN Rank inside SelectAbility and the classic
    // one hands the screen (Rank, idx) — so a call made while the widgets still pointed at
    // another cell would buy a different ability from the one just spoken.
    SetPromotionSelection(view, row, branch);

    // SelectAbility(int) is declared on BOTH row classes with the same name and signature, and
    // is already the three-way verb the game wrote: owned opens the info popup, purchasable
    // raises the confirm dialog (which the dialog recipe reads), locked plays the refusal sound.
    struct {
        int32_t Idx;
    } params{branch};
    return CallNamedFunction(r.handle, "SelectAbility", &params);
}

// ---- the geoscape (UIStrategyMap) — the mission tray ------------------------------------------
//
// The globe has no cursor for a mouse+keyboard player: ProcessGeoscapeSelection, and therefore
// UpdateSelection -> SelectMapItemNearestLocation, runs only inside `if( `ISCONTROLLERACTIVE)`.
// What IS live on mouse+keyboard is the mission TRAY along the bottom — the game's own list of
// everything actionable — because InitScreen sets Navigator.HorizontalNavigation and
// UIScreen.OnUnrealCommand pipes unhandled arrows into Navigator.Prev/Next. And because nothing
// recomputes it, whatever those arrows select STICKS: measured live 2026-08-21, one synthetic
// Left moved SelectedIndex 0 -> 1 and SelectedMapItem POI -> Mission, unchanged 12 s later.
//
// So the game hands us a working list cursor that is invisible and silent, and this reader
// supplies the three things the mouse-active guard withholds — brackets, camera, tooltip.

namespace {

// Offsets from the generated SDK's reflection-dump comments, cross-checked live 2026-08-21
// against the running class through /props on an ordinary campaign.
constexpr int32_t kStratMapUiStateOffset = 0x620;      // UIStrategyMap::m_eUIState (byte)
constexpr int32_t kStratMapMissionIconsOffset = 0x624; // ::MissionItemUI.MissionIcons
constexpr int32_t kStratMapSelectedItemOffset = 0x78C; // ::SelectedMapItem
constexpr int32_t kPanelNavigatorOffset = 0x2D8;       // UIPanel::Navigator
constexpr int32_t kNavSelectedIndexOffset = 0x6C;      // UINavigator::SelectedIndex
constexpr int32_t kNavControlsOffset = 0x74;           // ::NavigableControls
constexpr int32_t kIconScanSiteOffset = 0x49C;         // UIStrategyMap_MissionIcon::ScanSite
constexpr int32_t kIconMissionSiteOffset = 0x4A4;      // ::MissionSite
constexpr int32_t kIconMapItemOffset = 0x4AC;          // ::MapItem
constexpr int32_t kIconTooltipOffset = 0x4B4;          // ::ToolTip
constexpr int32_t kIconIdNumOffset = 0x4C0;            // ::idNum
constexpr int32_t kIconImagePathOffset = 0x3DC;        // UIIcon::ImagePath
constexpr int32_t kMapItemPinHeaderOffset = 0x4A0;     // UIStrategyMapItem::MapPin_Header

// The art the game draws in a faction slot whose faction has not been met. This is the game's
// OWN answer to "is this a placeholder", and it is the only reliable one — see ReadStrategyMap.
constexpr char kFactionUnknownArt[] = "Faction_unknown";
constexpr int32_t kTooltipTitleOffset = 0x484;         // UIStrategyMap_MissionIconTooltip::Title
constexpr int32_t kTooltipBodyOffset = 0x494;          // ::Body
constexpr int32_t kMapItemEntityRefOffset = 0x494;     // UIStrategyMapItem::GeoscapeEntityRef

// The HUD's four keyboard-unreachable verbs, and the localized words the screen already owns
// for them. m_strDarkEventsLabel is the one measured live so far ("СОБЫТИЯ", 2026-08-21).
constexpr int32_t kStratMapHudOffset = 0x610;             // UIStrategyMap::StrategyMapHUD
constexpr int32_t kStratMapDarkEventsButtonOffset = 0x654;    // ::DarkEventsButton
constexpr int32_t kStratMapDarkEventsContainerOffset = 0x65C; // ::DarkEventsContainer
constexpr int32_t kStratMapDarkEventsLabelOffset = 0x6AC;     // ::m_strDarkEventsLabel
constexpr int32_t kStratMapToggleResNetOffset = 0x6BC;        // ::m_strToggleResNet
constexpr int32_t kStratMapResOrdersOffset = 0x6CC;           // ::m_srResistanceOrders
constexpr int32_t kStratMapChosenInfoOffset = 0x6DC;          // ::m_srChosenInfo
constexpr int32_t kStratMapFlagsOffset = 0x7C8;               // bCursorAlwaysVisible..
constexpr uint32_t kStratMapResNetForcedOn = 0x08;            // ::m_bResNetForcedOn
constexpr int32_t kHudChosenButtonOffset = 0x474;  // UIStrategyMap_HUD::m_kChosenInfoButton
constexpr int32_t kHudResOrdersButtonOffset = 0x47C; // ::m_kResistanceInfoButton

constexpr int kMaxTrayIcons = 20; // UIStrategyMap.MAX_NUM_STRATEGYICONS
constexpr int kMaxNavControls = 32;

// UIUtilities_Input's own constants. The hover PAIR is deliberately the mouse one rather than a
// D-pad command: that handler is where the game itself keeps the camera fly-to and the tooltip
// refresh. FXS_L_MOUSE_UP is what the Dark Events handler tests for, and what the game's own
// FXS_BUTTON_Y branch passes it.
constexpr int32_t kFxsLMouseUp = 391;
constexpr int32_t kFxsLMouseIn = 392;
constexpr int32_t kFxsLMouseOut = 393;
// ⛔ 400, NOT 391, is a CLICK. UIIcon.OnMouseEvent fires OnClickedDelegate on
// FXS_L_MOUSE_UP_DELAYED and ignores FXS_L_MOUSE_UP entirely (UIIcon.uc:280) — which is a live
// Firaxis bug in UIStrategyMap_MissionIcon.OnUnrealCommand, whose FXS_BUTTON_A branch sends 391
// and therefore does nothing but play a click sound. Getting this constant wrong is silent.
constexpr int32_t kFxsLMouseUpDelayed = 400;

// A child widget's visible bit IS meaningful (unlike a screen's) — the game toggles it
// deliberately, which is exactly how it answers "is this verb on screen right now".
bool WidgetShowing(void* panel)
{
    if (!panel)
        return false;
    uint32_t flags = ReadU32Raw(panel, kPanelFlagsOffset);
    return (flags & kPanelVisible) && !(flags & kPanelRemoved);
}

// UIPanel.OnMouseEvent(int cmd, array<string> args).
//
// The args array is passed by pointer into ProcessEvent's frame (parameters are shallow-memcpy'd)
// and the destroy pass skips everything below ParmsSize, so a zeroed empty TArray we own is both
// safe and never freed by the engine. No handler this is used for reads it.
bool SendIconMouseEvent(void* icon, int32_t cmd)
{
    if (!icon)
        return false;
    struct {
        int32_t Cmd;
        void* ArgsData;
        int32_t ArgsCount;
        int32_t ArgsMax;
    } params{cmd, nullptr, 0, 0};
    return CallNamedFunction(icon, "OnMouseEvent", &params);
}

// UIStrategyMap.SetSelectedMapItem(UIStrategyMapItem Selection, optional bool NavigationSelect).
//
// The one line UIStrategyMap.OnNavigationChanged exists to reach, called directly — the header's
// note on SetTraySelection says why the delegate itself is the wrong door. NavigationSelect=true
// is load-bearing: the false branch ends in Navigator.OnLoseFocus() + a no-argument
// Navigator.SetSelected(), i.e. a DESELECT of the control we just selected.
bool SetSelectedMapItemRaw(void* screen, void* mapItem)
{
    if (!screen || !mapItem)
        return false;
    struct {
        void* Selection;
        uint32_t bNavigationSelect;
    } params{mapItem, 1};
    return CallNamedFunction(screen, "SetSelectedMapItem", &params);
}

} // namespace

GeoscapeGate ReadGeoscapeGate()
{
    GeoscapeGate gate;
    void* screens[kMaxStackScreens] = {};
    int count = WalkStackRaw(screens, kMaxStackScreens);

    // GetCurrentScreen is the FIRST non-none entry and applies no state-bit filter of its own,
    // so neither do we — the same rule ReadAvengerGate follows, and for the same reason: a gate
    // that disagreed with the game about which screen the arrows reach is the one disagreement
    // that matters.
    bool sawTop = false;
    for (int i = 0; i < count; i++) {
        if (!screens[i])
            continue;
        char name[64];
        if (!ObjClassNameAnsiRaw(screens[i], name, sizeof(name)))
            continue;
        if (strcmp(name, "UIStrategyMap") == 0) {
            gate.mapOnStack = true;
            gate.current = !sawTop;
            if (!sawTop) {
                // Still refuse a map that is on the stack but torn down.
                if (!StackEntryLiveRaw(screens[i]))
                    gate.current = false;
                // Carried out by VALUE, in this same call: the finder below needs exactly the
                // screen this walk already identified, and walking again to re-answer a question
                // just answered is how the two would come to disagree. Nothing caches it across
                // frames, so the never-hold-a-UObject rule is untouched.
                if (gate.current)
                    gate.map = screens[i];
            }
            break;
        }
        if (!sawTop) {
            sawTop = true;
            strncpy_s(gate.blocker, name, _TRUNCATE);
        }
    }
    return gate;
}

void* FindStrategyMapScreen()
{
    // One walk, not two: ReadGeoscapeGate has already found the map and validated it.
    return ReadGeoscapeGate().map;
}

StrategyMapView ReadStrategyMap(void* screen)
{
    StrategyMapView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.uiState = static_cast<int>(ReadU32Raw(screen, kStratMapUiStateOffset) & 0xFF);

    v.navigator = ReadPtrRaw(screen, kPanelNavigatorOffset);

    // The game's own navigable set, which is the ONLY honest source for "is this icon
    // reachable": UpdateMissions calls AddControl exactly for the icons it wants reachable, so
    // membership here is the game's own parity filter rather than a rule of ours.
    // ⚠ It is NavigableControls, not NavigationTargets — the latter is empty on this screen and
    // is what the ant farm's re-arm test looks at, a different list entirely.
    void* navControls[kMaxNavControls] = {};
    int navCount = 0;
    if (v.navigator) {
        v.selected = ReadI32Raw(v.navigator, kNavSelectedIndexOffset);
        navCount = TakeArrayRaw("StrategyMap: NavigableControls", v.navigator, kNavControlsOffset,
            navControls, kMaxNavControls);
    }

    void* icons[kMaxTrayIcons] = {};
    int iconCount = TakeArrayRaw("StrategyMap: MissionIcons", screen, kStratMapMissionIconsOffset,
        icons, kMaxTrayIcons);

    for (int i = 0; i < iconCount; i++) {
        void* icon = icons[i];
        if (!icon)
            continue;
        // A tray slot the game is not showing is not a stop. Unlike a SCREEN's bIsVisible —
        // which the game leaves false on screens it is plainly showing, hence the hard rule
        // against gating readiness on it — a child widget's visible bit is toggled deliberately
        // and does mean hidden. UpdateMissions calls Hide() on every slot it did not bind.
        uint32_t flags = ReadU32Raw(icon, kPanelFlagsOffset);
        if (!(flags & kPanelVisible) || (flags & kPanelRemoved))
            continue;

        TrayIcon t;
        t.handle = icon;
        t.slot = i;
        t.idNum = ReadI32Raw(icon, kIconIdNumOffset);
        t.scanSite = ReadPtrRaw(icon, kIconScanSiteOffset) != nullptr;
        t.mission = ReadPtrRaw(icon, kIconMissionSiteOffset) != nullptr;
        t.mapItem = ReadPtrRaw(icon, kIconMapItemOffset);
        if (t.mapItem) {
            t.entityId = ReadI32Raw(t.mapItem, kMapItemEntityRefOffset);
            // A localized, GENERIC noun for the kind of place the pin marks ("ШТАБ
            // СОПРОТИВЛЕНИЯ"). It is a CPF_Localized class default, so unlike everything else on
            // a pin it is NOT subject to the §4.3 staleness rule — an off-screen pin holds it
            // just as correctly as a framed one, because every instance of the class holds the
            // same value. That is exactly why it is safe to use for a slot with no tooltip.
            t.pinHeader = ReadFStringField(t.mapItem, kMapItemPinHeaderOffset);
        }

        void* tooltip = ReadPtrRaw(icon, kIconTooltipOffset);
        if (tooltip) {
            t.title = ReadFStringField(tooltip, kTooltipTitleOffset);
            t.body = ReadFStringField(tooltip, kTooltipBodyOffset);
        }

        for (int n = 0; n < navCount; n++) {
            if (navControls[n] == icon) {
                t.navIndex = n;
                break;
            }
        }

        // ⛔ THE PLACEHOLDER TEST IS THE ART, and nothing else works.
        //
        // The obvious tests both fail, measured live 2026-08-21 on an unmet faction's slot:
        // ScanSite is SET (to the faction's XComGameState_Haven, so "no site" is false), and the
        // icon IS in NavigableControls (so "not navigable" is false). What is actually true of
        // it is that ToolTip is NULL — SetMissionIconTooltip early-returns on
        // `HavenState != None && !IsResistanceFactionMet()` before it ever spawns one — and that
        // the game draws the Faction_unknown art. The art is the better test of the two: it is
        // the same fact a sighted player is reading off the screen, and it cannot be confused
        // with an icon whose tooltip merely has not been built yet.
        t.placeholder = ReadFStringField(icon, kIconImagePathOffset).find(kFactionUnknownArt)
            != std::string::npos;

        v.icons.push_back(std::move(t));
    }

    // The HUD's four verbs, each present only when the game is drawing it. Order is the screen's
    // own left-to-right reading of the top bar, with the map-mode toggle last because it changes
    // what the globe SHOWS rather than opening anything.
    void* darkContainer = ReadPtrRaw(screen, kStratMapDarkEventsContainerOffset);
    void* darkButton = ReadPtrRaw(screen, kStratMapDarkEventsButtonOffset);
    if (WidgetShowing(darkContainer) && darkButton) {
        HudVerb verb;
        verb.kind = HudVerb::Kind::DarkEvents;
        verb.button = darkButton;
        // On mouse+keyboard AddDarkEventsButton does a plain SetText(m_strDarkEventsLabel), so
        // the button RETAINS the localized word; the screen's own string is the fallback for the
        // controller branch, which wraps it in an icon injection.
        verb.label = ReadFStringField(darkButton, kButtonTextOffset);
        if (verb.label.empty())
            verb.label = ReadFStringField(screen, kStratMapDarkEventsLabelOffset);
        v.verbs.push_back(std::move(verb));
    }

    void* hud = ReadPtrRaw(screen, kStratMapHudOffset);
    if (hud) {
        void* chosenBtn = ReadPtrRaw(hud, kHudChosenButtonOffset);
        if (WidgetShowing(chosenBtn)) {
            HudVerb verb;
            verb.kind = HudVerb::Kind::ChosenInfo;
            verb.button = chosenBtn;
            // InitButton was called with no caption, so the button's own Text is empty — these
            // two read from the SCREEN's localized strings instead.
            verb.label = ReadFStringField(screen, kStratMapChosenInfoOffset);
            v.verbs.push_back(std::move(verb));
        }
        void* ordersBtn = ReadPtrRaw(hud, kHudResOrdersButtonOffset);
        if (WidgetShowing(ordersBtn)) {
            HudVerb verb;
            verb.kind = HudVerb::Kind::ResistanceOrders;
            verb.button = ordersBtn;
            verb.label = ReadFStringField(screen, kStratMapResOrdersOffset);
            v.verbs.push_back(std::move(verb));
        }
    }

    // The resistance-network toggle has no button of its own on mouse+keyboard — UpdateButtonHelp
    // adds it as a nav-help hotlink — so there is no widget to gate on and it is always offered.
    // That matches the screen: the hotlink is drawn whenever the map is.
    {
        HudVerb verb;
        verb.kind = HudVerb::Kind::ResNetToggle;
        verb.label = ReadFStringField(screen, kStratMapToggleResNetOffset);
        verb.on = (ReadU32Raw(screen, kStratMapFlagsOffset) & kStratMapResNetForcedOn) != 0;
        v.verbs.push_back(std::move(verb));
    }
    return v;
}

bool ActivateTraySelection(const StrategyMapView& view, const TrayIcon& icon)
{
    if (!view.screen || !icon.handle || icon.placeholder)
        return false;

    // Best effort, and cosmetic ONLY — nothing below depends on it. The 3D selection cursor, the
    // pin highlight and the nav-help prompts all read SelectedMapItem, so a screen whose
    // activation we are about to trigger should be looking at the thing we are activating.
    if (icon.mapItem && ReadPtrRaw(view.screen, kStratMapSelectedItemOffset) != icon.mapItem)
        SetSelectedMapItemRaw(view.screen, icon.mapItem);

    // The click itself, sent to the ICON. This is the mouse's own path and the whole reason the
    // hazard above is only cosmetic: OnClickedDelegate was bound by SetScanSite/SetMissionSite to
    // THIS icon's own site (`OnClickedDelegate = ScanSite.AttemptSelectionCheckInterruption`), so
    // the target is decided by which icon we hand the event to and by nothing else. Every gate
    // stays the game's — CanInteract, the interruption popup, the mission briefing prompt — and
    // AS_SetLock is a Flash-only call that never touches IsDisabled, so a "locked" pin is refused
    // by CanInteract inside the delegate rather than by the widget, exactly as it is for a mouse.
    return SendIconMouseEvent(icon.handle, kFxsLMouseUpDelayed);
}

bool ActivateHudVerb(const StrategyMapView& view, const HudVerb& verb)
{
    if (!view.screen)
        return false;

    switch (verb.kind) {
    case HudVerb::Kind::ChosenInfo:
    case HudVerb::Kind::ResistanceOrders:
        // UIButton::Click() fires OnClickedDelegate and refuses a disabled or hidden button
        // itself, so the guard the mouse path gets comes along for free. Both handlers also
        // check the screen stack before pushing, which is what stops a double press stacking
        // two copies of the same screen.
        return ClickButton(verb.button);

    case HudVerb::Kind::DarkEvents: {
        // ⛔ NOT Click(): ProcessMouseEvents wired this button's handler as an
        // OnMouseEventDelegate, so it has no OnClickedDelegate and Click() would return false
        // having done nothing at all. This is the game's own keyboard route reproduced exactly,
        // `none` panel included — OnDarkEventsClicked ignores the panel and tests only the
        // command, which must be FXS_L_MOUSE_UP or the body does not run.
        struct {
            void* Panel;
            int32_t Cmd;
        } params{nullptr, kFxsLMouseUp};
        return CallNamedFunction(view.screen, "OnDarkEventsClicked", &params);
    }

    case HudVerb::Kind::ResNetToggle: {
        // Zero parameters, but ProcessEvent still needs a block to point at. The screen's own
        // function owns both halves of the toggle — the forced-on flag and the SetUIState call
        // whose ordering its comment calls load-bearing — so this must never be open-coded.
        char noParams[8] = {};
        return CallNamedFunction(view.screen, "OnResNetClicked", noParams);
    }
    }
    return false;
}

bool SetTraySelection(const StrategyMapView& view, const TrayIcon& icon)
{
    // ⛔ A placeholder must leave the game's navigator alone. Moving it would put
    // SelectedMapItem on an icon other than the one just spoken, and nothing on screen would
    // ever show the disagreement.
    if (!view.navigator || icon.navIndex < 0)
        return false;

    void* controls[kMaxNavControls] = {};
    int count = TakeArrayRaw("StrategyMap: NavigableControls", view.navigator, kNavControlsOffset,
        controls, kMaxNavControls);
    if (icon.navIndex >= count || !controls[icon.navIndex])
        return false;

    // UINavigator.SetSelected(UIPanel optionalSelectedControl). Passing the CONTROL rather than
    // writing SelectedIndex is what gets the game's own bookkeeping — but that is ALL it gets:
    // this mover is the one that does not fire OnSelectedIndexChanged, so the screen's
    // OnNavigationChanged does not run and SelectedMapItem does not follow. See the header.
    struct {
        void* Control;
    } params{controls[icon.navIndex]};
    if (!CallNamedFunction(view.navigator, "SetSelected", &params))
        return false;

    // The half SetSelected leaves undone, and the one that matters: the 3D selection cursor, the
    // pin highlight, the nav-help prompts and — the reason the geoscape has an activation verb at
    // all — what Enter will act on.
    return SetSelectedMapItemRaw(view.screen, icon.mapItem);
}

bool MoveTrayHover(const TrayIcon& icon, bool entering)
{
    if (!icon.handle || icon.placeholder)
        return false;
    return SendIconMouseEvent(icon.handle, entering ? kFxsLMouseIn : kFxsLMouseOut);
}

// ---- the Chosen dossier and the bond screens (docs/chosen-bond-recon.md §2) ----

namespace {

// UIChosenInfo
constexpr int32_t kChosenInfoSelectedOffset = 0x520; // CurrentChosenIndex
constexpr int32_t kChosenInfoAllOffset = 0x524;      // AllActiveChosen (TArray)

// UISoldierBondScreen
constexpr int32_t kBondScreenFlagsOffset = 0x410;    // m_bFlipSort, m_bDirtySortHeaders, bSquadOnly
constexpr uint32_t kBondFlipSort = 0x1;
constexpr uint32_t kBondSquadOnly = 0x4;
constexpr int32_t kBondScreenSortTypeOffset = 0x414; // m_eSortType (a byte enum)
constexpr int32_t kBondScreenUnitRefOffset = 0x4E4;  // UnitRef (StateObjectReference: ObjectID first)
constexpr int32_t kBondScreenListOffset = 0x4EC;     // List

// UISoldierBondListItem
constexpr int32_t kBondRowUnitRefOffset = 0x4C4;    // UnitRef
constexpr int32_t kBondRowBondButtonOffset = 0x4CC; // ActivateBondButton

// The whole crew but one can be on this list — the personnel roster's bound, for the same reason.
constexpr int kMaxBondRows = 256;

// The stack-first finder every one of these screens shares, answering only while the screen is
// the FOCUSED one — a bond screen under its own confirm screen has already lost the keys.
void* FindFocusedStackScreen(const char* className, ScreenFinder& finder, bool& reported)
{
    void* screen = PreferStack(className, finder, reported);
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

} // namespace

void* FindChosenInfoScreen()
{
    static ScreenFinder sFinder{"UIChosenInfo"};
    static bool sReported = false;
    return FindFocusedStackScreen("UIChosenInfo", sFinder, sReported);
}

ChosenInfoView ReadChosenInfo(void* screen)
{
    ChosenInfoView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.selected = ReadI32Raw(screen, kChosenInfoSelectedOffset);
    // The COUNT only. The elements are game-state objects the companion resolves for itself off
    // the same array, and a pointer read here would be a UObject held for nothing.
    const int32_t count = ReadArrayCountRaw(screen, kChosenInfoAllOffset);
    v.count = count < 0 ? 0 : count;
    return v;
}

void* FindBondScreen()
{
    static ScreenFinder sFinder{"UISoldierBondScreen"};
    static bool sReported = false;
    return FindFocusedStackScreen("UISoldierBondScreen", sFinder, sReported);
}

BondScreenView ReadBondScreen(void* screen)
{
    BondScreenView v;
    if (!screen)
        return v;
    v.screen = screen;

    const uint32_t flags = ReadU32Raw(screen, kBondScreenFlagsOffset);
    v.flipSort = (flags & kBondFlipSort) != 0;
    v.squadOnly = (flags & kBondSquadOnly) != 0;
    v.sortType = ReadU8Raw(screen, kBondScreenSortTypeOffset);
    v.unitId = ReadI32Raw(screen, kBondScreenUnitRefOffset);

    v.list = ReadPtrRaw(screen, kBondScreenListOffset);
    if (!v.list)
        return v;
    v.selected = ReadI32Raw(v.list, kListSelectedIndexOffset);

    void* rows[kMaxBondRows] = {};
    const int rowCount = TakeListItemsRaw("SoldierBonds: rows", v.list, rows, kMaxBondRows);
    for (int i = 0; i < rowCount; i++) {
        if (!rows[i])
            continue;
        BondRow r;
        r.handle = rows[i];
        r.index = i;
        r.unitId = ReadI32Raw(rows[i], kBondRowUnitRefOffset);
        // UIButton::IsDisabled — UpdateData's SetDisabled(true) for a soldier already bonded to
        // someone else. The game still lets the row be selected; it is the bond it refuses.
        r.disabled = (ReadU32Raw(rows[i], kButtonFlagsOffset) & kButtonDisabled) != 0;
        void* bondButton = ReadPtrRaw(rows[i], kBondRowBondButtonOffset);
        r.bondOffered
            = bondButton && (ReadU32Raw(bondButton, kPanelFlagsOffset) & kPanelVisible) != 0;
        v.rows.push_back(r);
    }
    return v;
}

bool SetBondSelection(const BondScreenView& view, int index)
{
    return SetListIndex(view.list, index);
}

void* FindBondConfirmScreen()
{
    static ScreenFinder sFinder{"UISoldierBondConfirmScreen"};
    static bool sReported = false;
    return FindFocusedStackScreen("UISoldierBondConfirmScreen", sFinder, sReported);
}

// ---- the monthly report and its Chosen page (docs/resistance-report-recon.md) ----

namespace {

// UIResistanceReport_ChosenEvents
constexpr int32_t kChosenEventsRefsOffset = 0x630;  // ChosenRefs (TArray<StateObjectReference>)
constexpr int32_t kChosenEventsFlagsOffset = 0x650; // bSimpleView, bShowChosenWarningPopup
constexpr uint32_t kChosenEventsSimpleView = 0x1;
constexpr uint32_t kChosenEventsWarning = 0x2;

} // namespace

void* FindChosenEventsScreen()
{
    static ScreenFinder sFinder{"UIResistanceReport_ChosenEvents"};
    static bool sReported = false;
    return FindFocusedStackScreen("UIResistanceReport_ChosenEvents", sFinder, sReported);
}

ChosenEventsView ReadChosenEvents(void* screen)
{
    ChosenEventsView v;
    if (!screen)
        return v;
    v.screen = screen;
    const uint32_t flags = ReadU32Raw(screen, kChosenEventsFlagsOffset);
    v.warningShown = (flags & kChosenEventsWarning) != 0;
    v.simpleView = (flags & kChosenEventsSimpleView) != 0;
    const int32_t count = ReadArrayCountRaw(screen, kChosenEventsRefsOffset);
    v.count = count < 0 ? 0 : count;
    return v;
}

void* FindResistanceReportScreen()
{
    static ScreenFinder sFinder{"UIResistanceReport"};
    static bool sReported = false;
    return FindFocusedStackScreen("UIResistanceReport", sFinder, sReported);
}

// ---- the covert action report (UICovertActionReport) — docs/covert-actions-recon.md §8b ----

namespace {

// UICovertActionReport. ⚠ Packed, not 8-aligned: the button pointer sits right after the 4-byte
// ref, exactly as the picker's ActionRef/arrActions pair does. Pinned in sdk_layout_checks.cpp.
constexpr int32_t kCovertReportActionRefOffset = 0x440;      // ActionRef (ObjectID first)
constexpr int32_t kCovertReportContinueOffset = 0x444;       // ContinueButton (UILargeButton*)

} // namespace

void* FindCovertReportScreen()
{
    static ScreenFinder sFinder{"UICovertActionReport"};
    static bool sReported = false;
    return FindFocusedStackScreen("UICovertActionReport", sFinder, sReported);
}

CovertReportView ReadCovertReport(void* screen)
{
    CovertReportView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.actionId = ReadI32Raw(screen, kCovertReportActionRefOffset);
    // RefreshConfirmPanel spawns the button LAST in UpdateData, so a read that lands between
    // BuildScreen and the end of OnInit finds it null — an ordinary answer, and the recipe's
    // fallback label covers it.
    v.continueButton = ReadPtrRaw(screen, kCovertReportContinueOffset);
    if (v.continueButton) {
        // UILargeButton.InitLargeButton → UIButton.InitButton → SetText, so Text really is
        // retained here; on a controller the label carries an injected icon, which CleanText
        // strips.
        v.continueLabel = CleanText(ReadFStringField(v.continueButton, kButtonTextOffset));
    }
    return v;
}

// ---- the armory hub (UIArmory_MainMenu) — docs/armory-recon.md §1 ----

namespace {

constexpr int32_t kArmoryMenuListOffset = 0x6F0;          // UIArmory_MainMenu::List
constexpr int32_t kListItemStringAttentionOffset = 0x440; // UIListItemString::AttentionIcon
constexpr int32_t kPanelMCNameOffset = 0x27C;             // UIPanel::MCName

// Eight in the shipped game; the OnArmoryMainMenuUpdate event lets a mod add more. Loud when
// clipped.
constexpr int kMaxArmoryMenuRows = 32;

} // namespace

void* FindArmoryMenuScreen()
{
    static ScreenFinder sFinder{"UIArmory_MainMenu"};
    static bool sReported = false;
    return FindFocusedStackScreen("UIArmory_MainMenu", sFinder, sReported);
}

ArmoryMenuView ReadArmoryMenu(void* screen)
{
    ArmoryMenuView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.unitId = ReadI32Raw(screen, kArmoryUnitRefOffset);
    v.list = ReadPtrRaw(screen, kArmoryMenuListOffset);
    if (!v.list)
        return v;
    v.selected = ReadI32Raw(v.list, kListSelectedIndexOffset);

    void* rows[kMaxArmoryMenuRows] = {};
    const int rowCount = TakeListItemsRaw("ArmoryMenu: rows", v.list, rows, kMaxArmoryMenuRows);
    for (int i = 0; i < rowCount; i++) {
        if (!rows[i] || !IsKindOfRaw(rows[i], "UIListItemString"))
            continue;
        ArmoryMenuRow r;
        r.handle = rows[i];
        r.index = i;
        char name[64];
        if (FNameAtAnsiRaw(static_cast<const uint8_t*>(rows[i]) + kPanelMCNameOffset, name,
                sizeof(name)))
            r.key = name;
        // Retained: InitListItem → SetText. The promote row's Text carries an InjectImage <img>
        // prefix when the promote icon shows; the cleaning read strips it.
        r.label = ReadFStringField(rows[i], kListItemStringTextOffset);
        r.disabled = (ReadU32Raw(rows[i], kListItemStringFlagsOffset) & kListItemDisabled) != 0;
        // NeedsAttention(true) spawns the icon; the game keeps no bool, so the icon's own
        // presence and visibility are the state — a child widget, where bIsVisible is meaningful.
        void* icon = ReadPtrRaw(rows[i], kListItemStringAttentionOffset);
        r.attention = icon && (ReadU32Raw(icon, kPanelFlagsOffset) & kPanelVisible) != 0;
        v.rows.push_back(std::move(r));
    }
    return v;
}

bool SetArmoryMenuSelection(const ArmoryMenuView& view, int index)
{
    return SetListIndex(view.list, index);
}

bool ActivateArmoryMenuRow(const ArmoryMenuView& view, int index)
{
    if (!view.screen || !view.list)
        return false;
    // Select first, then the SCREEN's Enter: UIArmory.OnUnrealCommand answers it with OnAccept,
    // which reads the list's selection — the Highlander's by re-sending Enter to the list (so a
    // mod-wrapped delegate runs), vanilla's by switching on selectedIndex. Both ignore whatever
    // row the caller had in mind, which is why selecting first is the argument.
    SetListIndex(view.list, index);
    return SendScreenCommand(view.screen, kCmdEnter);
}

// ---- weapon upgrades (UIArmory_WeaponUpgrade / UIArmory_WeaponTrait) — docs/armory-recon.md §2 ----

namespace {

// UIArmory_WeaponUpgrade. ⛔ The Highlander's WeaponViewOffsets (Issue #832) sits at the head of
// the class, so every offset here is 0x10 past vanilla's — the dump this build was cut from is the
// Highlander's, and the pins below would refuse a vanilla one.
constexpr int32_t kWeaponUpgradeActiveListOffset = 0x510;    // ActiveList
constexpr int32_t kWeaponUpgradeSlotsListOffset = 0x520;     // SlotsList
constexpr int32_t kWeaponUpgradeUpgradesListOffset = 0x530;  // UpgradesList
constexpr int32_t kWeaponUpgradeWeaponRefOffset = 0x538;     // WeaponRef (ObjectID first)
constexpr int32_t kWeaponUpgradeCustomizeListOffset = 0x574; // CustomizeList
// UIArmory_WeaponTrait
constexpr int32_t kWeaponTraitStartingIndexOffset = 0x704; // StartingIndex

// UIArmory_WeaponUpgradeItem — the loadout row's layout through Count, then its own tail.
constexpr int32_t kUpgradeItemTitleOffset = 0x3D8;     // title
constexpr int32_t kUpgradeItemSubtitleOffset = 0x3E8;  // subTitle
constexpr int32_t kUpgradeItemCountOffset = 0x438;     // Count
constexpr int32_t kUpgradeItemFlagsOffset = 0x43C;     // bIsLocked | bIsInfinite | bIsDisabled
constexpr uint32_t kUpgradeItemLocked = 0x1;
constexpr uint32_t kUpgradeItemInfinite = 0x2;
constexpr uint32_t kUpgradeItemDisabled = 0x4;
constexpr int32_t kUpgradeItemTemplateOffset = 0x440;  // UpgradeTemplate
constexpr int32_t kUpgradeItemSlotIndexOffset = 0x450; // SlotIndex

// A weapon has at most a handful of slots; the inventory's upgrade list and a pattern picker's
// options are the long ones. Loud when clipped.
constexpr int kMaxWeaponUpgradeRows = 128;

void ReadUpgradeRows(const char* what, void* list, std::vector<WeaponUpgradeRow>& out)
{
    void* rows[kMaxWeaponUpgradeRows] = {};
    const int rowCount = TakeListItemsRaw(what, list, rows, kMaxWeaponUpgradeRows);
    for (int i = 0; i < rowCount; i++) {
        if (!rows[i] || !IsKindOfRaw(rows[i], "UIArmory_WeaponUpgradeItem"))
            continue;
        WeaponUpgradeRow r;
        r.handle = rows[i];
        r.index = i;
        r.title = ReadFStringField(rows[i], kUpgradeItemTitleOffset);
        r.subtitle = ReadFStringField(rows[i], kUpgradeItemSubtitleOffset);
        r.count = ReadI32Raw(rows[i], kUpgradeItemCountOffset);
        const uint32_t flags = ReadU32Raw(rows[i], kUpgradeItemFlagsOffset);
        r.locked = (flags & kUpgradeItemLocked) != 0;
        r.infinite = (flags & kUpgradeItemInfinite) != 0;
        r.disabled = (flags & kUpgradeItemDisabled) != 0;
        r.hasUpgrade = ReadPtrRaw(rows[i], kUpgradeItemTemplateOffset) != nullptr;
        r.slotIndex = ReadI32Raw(rows[i], kUpgradeItemSlotIndexOffset);
        out.push_back(std::move(r));
    }
}

} // namespace

void* FindWeaponUpgradeScreen()
{
    // Topmost non-null entry only: the picker is a subclass pushed OVER the upgrade screen, and
    // the confirm dialog pushes no screen, so "the top of the stack is one of the family" is
    // exactly the screen the keys reach.
    void* screen = TopStackScreenRaw([](void* candidate, const char*) {
        return IsKindOfRaw(candidate, "UIArmory_WeaponUpgrade");
    });
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

WeaponUpgradeView ReadWeaponUpgrade(void* screen)
{
    WeaponUpgradeView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.trait = ObjectClassNamedRaw(screen, "UIArmory_WeaponTrait");
    v.unitId = ReadI32Raw(screen, kArmoryUnitRefOffset);
    v.weaponId = ReadI32Raw(screen, kWeaponUpgradeWeaponRefOffset);
    v.slotsList = ReadPtrRaw(screen, kWeaponUpgradeSlotsListOffset);
    v.upgradesList = ReadPtrRaw(screen, kWeaponUpgradeUpgradesListOffset);
    v.customizeList = ReadPtrRaw(screen, kWeaponUpgradeCustomizeListOffset);
    void* active = ReadPtrRaw(screen, kWeaponUpgradeActiveListOffset);
    v.upgradesActive = active && active == v.upgradesList;

    if (v.trait) {
        // The picker reuses SlotsList as a plain string list, with the options retained on the
        // rows (UIListItemString.SetText) and the landing index on the screen.
        v.startingIndex = ReadI32Raw(screen, kWeaponTraitStartingIndexOffset);
        if (v.slotsList) {
            v.optionsSelected = ReadI32Raw(v.slotsList, kListSelectedIndexOffset);
            void* rows[kMaxWeaponUpgradeRows] = {};
            const int rowCount
                = TakeListItemsRaw("WeaponTrait: options", v.slotsList, rows, kMaxWeaponUpgradeRows);
            for (int i = 0; i < rowCount; i++) {
                if (!rows[i] || !IsKindOfRaw(rows[i], "UIListItemString"))
                    continue;
                MenuItem item;
                item.handle = rows[i];
                item.index = i;
                item.label = ReadFStringField(rows[i], kListItemStringTextOffset);
                v.options.push_back(std::move(item));
            }
        }
        return v;
    }

    if (v.slotsList) {
        v.slotsSelected = ReadI32Raw(v.slotsList, kListSelectedIndexOffset);
        ReadUpgradeRows("WeaponUpgrade: slots", v.slotsList, v.slots);
    }
    if (v.upgradesList) {
        v.upgradesSelected = ReadI32Raw(v.upgradesList, kListSelectedIndexOffset);
        ReadUpgradeRows("WeaponUpgrade: upgrades", v.upgradesList, v.upgrades);
    }
    if (v.customizeList) {
        v.customizeSelected = ReadI32Raw(v.customizeList, kListSelectedIndexOffset);
        v.customize = ReadListRows(v.customizeList);
    }
    return v;
}

bool SelectWeaponUpgradeRow(const WeaponUpgradeView& view, WeaponUpgradeList which, int index)
{
    if (which == WeaponUpgradeList::Slots) {
        if (view.upgradesActive)
            return false; // the game switched the slots list off — see the header
        return SetListIndex(view.slotsList, index);
    }
    if (!view.upgradesActive)
        return false;
    return SetListIndex(view.upgradesList, index);
}

bool ActivateWeaponUpgradeRow(const WeaponUpgradeView& view, WeaponUpgradeList which, int index)
{
    if (!view.screen)
        return false;
    if (!SelectWeaponUpgradeRow(view, which, index))
        return false;
    // The SCREEN's Enter: UIArmory.OnUnrealCommand → OnAccept → OnItemClicked(ActiveList,
    // ActiveList.SelectedIndex). The disabled refusal, the picker's open animation, the confirm
    // dialog and the reuse-upgrades shortcut are all inside that call.
    return SendScreenCommand(view.screen, kCmdEnter);
}

bool SelectWeaponCustomizeRow(const WeaponUpgradeView& view, int index)
{
    return SetListIndex(view.customizeList, index);
}

bool ActivateWeaponCustomizeRow(const WeaponUpgradeView& view, int index)
{
    for (const OptionRow& row : view.customize) {
        if (row.index != index)
            continue;
        // The row's own OnUnrealCommand(Enter) → its OnClickDelegate: OpenWeaponNameInputBox,
        // WeaponColorSelector or CustomizeWeaponPattern. The game itself only reaches these rows
        // through OnAccept's no-selection forward, so the row handler is the honest path.
        return SendRowCommand(row.handle, kCmdEnter);
    }
    return false;
}

bool SelectWeaponPattern(const WeaponUpgradeView& view, int index)
{
    return view.trait && SetListIndex(view.slotsList, index);
}

bool ActivateWeaponPattern(const WeaponUpgradeView& view, int index)
{
    if (!view.screen || !view.trait)
        return false;
    // UIArmory_WeaponTrait.OnAccept fires OnItemClicked(SlotsList, SlotsList.SelectedIndex) and
    // closes — select first, then the screen's own Enter.
    SetListIndex(view.slotsList, index);
    return SendScreenCommand(view.screen, kCmdEnter);
}

} // namespace GameUI

#ifdef _MSC_VER
#pragma pack(pop)
#endif
