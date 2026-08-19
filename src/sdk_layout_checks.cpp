// Compile-time cross-check of every hand-transcribed engine offset against the regenerated
// SDK's COMPILED layouts (pack 0x4 since the 2026-08-19 generator fixes — before that the
// compiled layouts drifted from the engine and only the dump comments could be trusted).
//
// This TU emits no code and changes no behavior: the runtime keeps reading through
// game_ui.cpp's SEH-guarded raw primitives with explicit offsets (the chokepoint the hard
// rules require). What this buys is that a future regeneration, game patch, or transcription
// typo turns into a BUILD ERROR here instead of a silent wrong read in-game.
//
// Bitfield flag dwords (panel/button/checkbox/dropdown state bits) cannot be offsetof'd;
// for those the nearest plain neighbor is asserted instead, which pins the same layout.

#include "SdkHeaders.hpp"

#include <cstddef>
#include <type_traits>

namespace {

// ---- core types ----
static_assert(std::is_same_v<FString::ElementType, const wchar_t>,
    "FString must be wide (UTF16_FSTRING) — XCOM 2's runtime FString is TArray<wchar_t>");
static_assert(sizeof(FScriptDelegate) == 0x20, "engine delegate stride (see generator notes)");

// ---- UIPanel (flag dword at 0x284 is bitfields; MCName pins the layout up to it) ----
static_assert(offsetof(AUIPanel, MCName) == 0x27C, "UIPanel::MCName");
static_assert(offsetof(AUIPanel, CachedTooltipId) == 0x2AC, "UIPanel::CachedTooltipId");
static_assert(offsetof(AUIPanel, Movie) == 0x2B8, "UIPanel::Movie");

// ---- UIMovie / presentation / tooltip chain ----
static_assert(offsetof(UUIMovie, PRES) == 0x204, "UIMovie::PRES");
static_assert(offsetof(AXComPresentationLayerBase, m_kTooltipMgr) == 0x55C,
    "XComPresentationLayerBase::m_kTooltipMgr");
static_assert(offsetof(AUITooltipMgr, TextTooltip) == 0x414, "UITooltipMgr::TextTooltip");
static_assert(offsetof(AUITooltipMgr, Tooltips) == 0x41C, "UITooltipMgr::Tooltips");
static_assert(offsetof(AUITooltip, Id) == 0x3D8, "UITooltip::Id");
static_assert(offsetof(AUITextTooltip, sTitle) == 0x48C, "UITextTooltip::sTitle");
static_assert(offsetof(AUITextTooltip, sBody) == 0x49C, "UITextTooltip::sBody");
static_assert(sizeof(FTTextTooltipData) == 0xAC, "TTextTooltipData stride");
static_assert(offsetof(FTTextTooltipData, Id) == 0x00, "TTextTooltipData::Id");
static_assert(offsetof(FTTextTooltipData, sTitle) == 0x44, "TTextTooltipData::sTitle");
static_assert(offsetof(FTTextTooltipData, sBody) == 0x54, "TTextTooltipData::sBody");

// ---- buttons / text widgets ----
static_assert(offsetof(AUIButton, Text) == 0x3D8, "UIButton::Text");
static_assert(offsetof(AUIScrollingText, Text) == 0x3D8, "UIScrollingText::Text");
static_assert(offsetof(AUIScrollingText, HtmlText) == 0x3E8, "UIScrollingText::HtmlText");

// ---- main menu shell ----
static_assert(offsetof(AUIShell, MAINMENU) == 0x4CC, "UIShell::MAINMENU");
static_assert(offsetof(AUIShell, TickerText) == 0x4F4, "UIShell::TickerText");
static_assert(offsetof(AUIFinalShell, m_my2KButton) == 0x5C8, "UIFinalShell::m_my2KButton");
static_assert(offsetof(AUIFinalShell, FiraxisLiveClient) == 0x608,
    "UIFinalShell::FiraxisLiveClient");

// ---- settings screen ----
static_assert(offsetof(AUIOptionsPCScreen, m_strTitle) == 0x42C, "options title");
static_assert(offsetof(AUIOptionsPCScreen, m_strTabAudio) == 0x4DC, "audio tab name");
static_assert(offsetof(AUIOptionsPCScreen, m_strTabVideo) == 0x4BC, "video tab name");
static_assert(offsetof(AUIOptionsPCScreen, m_strTabGraphics) == 0x4CC, "graphics tab name");
static_assert(offsetof(AUIOptionsPCScreen, m_strTabGameplay) == 0x4EC, "gameplay tab name");
static_assert(offsetof(AUIOptionsPCScreen, m_strTabInterface) == 0x4FC, "interface tab name");
static_assert(offsetof(AUIOptionsPCScreen, List) == 0x140C, "options List");
static_assert(offsetof(AUIOptionsPCScreen, m_arrMechaItems) == 0x1414, "options rows");
static_assert(offsetof(AUIOptionsPCScreen, m_iCurrentTab) == 0x1424, "options current tab");
static_assert(offsetof(AUIOptionsPCScreen, GPUAutoDetectButton) == 0x1434, "GPU button");
static_assert(offsetof(AUIOptionsPCScreen, SaveAndExitButton) == 0x1444, "save button");
static_assert(offsetof(AUIOptionsPCScreen, ExitWitoutChangesButton) == 0x144C, "discard button");
static_assert(offsetof(AUIOptionsPCScreen, CreditsButton) == 0x1454, "credits button");
static_assert(offsetof(AUIList, SelectedIndex) == 0x404, "UIList::SelectedIndex");

// ---- settings rows (flag dword at 0x414 is bitfields; neighbors pin it) ----
static_assert(offsetof(AUIMechaListItem, Desc) == 0x404, "row Desc");
static_assert(offsetof(AUIMechaListItem, BG) == 0x40C, "row BG");
static_assert(offsetof(AUIMechaListItem, Value) == 0x418, "row Value");
static_assert(offsetof(AUIMechaListItem, Spinner) == 0x428, "row Spinner");
static_assert(offsetof(AUIMechaListItem, dropdown) == 0x430, "row Dropdown");
static_assert(offsetof(AUIMechaListItem, Button) == 0x438, "row Button");
static_assert(offsetof(AUIMechaListItem, Checkbox) == 0x440, "row Checkbox");
static_assert(offsetof(AUIMechaListItem, Slider) == 0x448, "row Slider");
static_assert(offsetof(AUIMechaListItem, Type) == 0x450, "row Type");

// ---- row widgets ----
static_assert(offsetof(AUICheckbox, Text) == 0x3D8, "UICheckbox::Text (bChecked bits follow)");
static_assert(offsetof(AUISlider, Percent) == 0x3E8, "UISlider::Percent");
static_assert(offsetof(AUIListItemSpinner, Value) == 0x3DC, "UIListItemSpinner::Value");
static_assert(offsetof(AUIDropdown, Items) == 0x3F8, "UIDropdown::Items");
static_assert(offsetof(AUIDropdown, SelectedItem) == 0x418, "UIDropdown::SelectedItem");

} // namespace
