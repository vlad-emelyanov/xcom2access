# Mod Config Menu recon

How the Mod Config Menu screen (Workshop 667104300, `MCM_OptionsScreen`) was measured before it
was built (2026-09-17, from the mod's own `Src/` clone at `refmods/ModConfigMenu/`, `SrcOrig` for
the engine widgets it is made of, and the loc file; the game was closed, so nothing here has run).
It is the first screen of a new surface family — **mod-provided screens** (`src/game_ui_mods.cpp`,
`src/recipes/recipe_mods.cpp`) — whose defining fact is in §4. LWOTC's replacement screens
(`lwotc-recon.md`) will join it. Rules earned by the first live pass go in `surfaces/shell.md`.

## 1. Reachability — one injected button, one pushed screen

- `MCM_OptionsMenuListenerHook` (a `UIScreenListener` with `ScreenClass = none`) hands every
  `UIOptionsPCScreen` OnInit to `MCM_OptionsMenuListener.InjectModOptionsButton`, which spawns a
  **plain `UIButton`** child of the settings screen at (500, 850), labelled `m_strModMenuButton`
  ("MOD SETTINGS" in `.int`). It lives in **no class field** — the listener's own comment explains
  the GC workaround that makes it so. `GameUI::ReadOptions` read four buttons from four fixed
  offsets, so this one was never a node.
- Click → `ShowModOptionsDialog` → `Movie.Stack.Push(Spawn(class'MCM_OptionsScreen', ParentScreen))`
  — pushed **over** `UIOptionsPCScreen`, which loses `bIsFocused`. Always pushed: there is no other
  way to open it, which is why the finder is stack-only (§4).
- Escape / `FXS_BUTTON_B` → `OnCancel`: `TriggerCancelEvent` on every page, then Pop — **every
  change on every page is discarded**. Save and Exit → `TriggerSaveEvent` on every page, then Pop.
- `.ini` on this machine: `USE_FLAT_DISPLAY_STYLE=False` (the screen is pushed onto the parent's
  movie, not a flat one), `SHOW_SOLDIER=False` (the main-menu soldier is hidden while it is open —
  irrelevant to reading).

## 2. The widget tree (`MCM_OptionsScreen.CreateSkeleton`)

```
MCM_OptionsScreen (UIScreen)
└─ Container            UIPanel, InitPanel('')            ← the ONE DIRECT UIPanel child holding a UIList
   ├─ BG                UIImage
   ├─ VSeparator        UIImage
   ├─ SaveAndExitButton UIButton, m_strSaveAndExit        ← spawn order: Save first, Cancel second
   ├─ CancelButton      UIButton, m_strCancel
   ├─ TitleHeader       UIX2PanelHeader (m_strTitle / m_strSubtitle)
   ├─ TabsList          UIList 'ModTabSelectList'
   │  └─ ItemContainer.ChildPanels[i]  MCM_SettingsTab extends UIMechaListItem (a checkbox row)
   └─ MCM_SettingsPanel ×N (LAZY — one per page, spawned on that page's first Show)
      ├─ SettingsList   UIList 'OptionsList'
      │  └─ ItemContainer.ChildPanels, top to bottom:
      │     MCM_UISettingSeparator (TitleLine: UIText 'Title' + UIScrollingText 'Description')
      │     per group: MCM_GroupLabelSeparator (UIText Title = the heading), then its settings
      │     UIImage 'MCMBottomPadding' (transparent, no label)
      └─ ResetButton    UIButton, m_strResetButton — Hide()n at spawn, shown by EnableResetButton
```

⛔ **A screen's `ChildPanels` is not the tree above — it is a flat REGISTRY of every panel on the
screen.** `UIPanel.uc:140-144`: every panel whose parent is not the screen also calls
`Screen.AddChild(self)` ("Every panel is a child of screen"), and the screen inserts **itself**
(`UIPanel.uc:866`); `dev-harness.md` measured `UITacticalHUD` listing all 159 of its panels. So the
screen's array holds the Container AND every `ItemContainer` (each an exact `UIPanel`, `UIList.uc:79`),
every row, every row widget. Panels **below** the screen list only their own children. The walk
from the screen therefore filters on `UIPanel.ParentPanel == screen` (`kPanelParentPanelOffset`,
pinned) and skips the screen; walks from the Container, a page, a list or a separator need no
filter. The same rule fixed the injected-button scan on `UIOptionsPCScreen`, where the Key Bindings
row's embedded `UIButton` sat in the screen's array beside MCM's button.

**Tabs.** `AddTabsListButton` → `MCM_SettingsTab.InitSettingsTab` → `UpdateDataCheckbox(Label, "",
false, CheckboxChangedCallback, CheckboxClickedCallback)`. The box is a radio in disguise:
`ChoosePanelByPageID` re-checks exactly the tab whose `SettingsPageID == SelectedPageID` and
unchecks the rest, so **checked == this page is showing**. Enter on the row: `UIMechaListItem.
OnUnrealCommand` → `Checkbox.OnUnrealCommand` → the changed callback → `OnClickHandler` =
`TabClickedHandler` → `ChoosePanelByPageID`. A custom-page tab (`NewCustomSettingsPage`) unchecks
itself and calls the mod's own `CustomPageCallback` — a screen of that mod's, not this recipe's.

**Pages are lazy.** `SelectedPageID = -1` at open; the right side is **empty** until a tab is
clicked. `MCM_SettingsPanelFacade.Show()` creates the `MCM_SettingsPanel` (`CreateUi` →
`ShowSettings`) on first show; `ChoosePanelByPageID` calls `Hide()` on every other facade.
`UIPanel.Hide()` clears `bIsVisible` on **that panel only** — its rows keep theirs. So the current
page is the `MCM_SettingsPanel` child of Container whose **own** flag dword carries
`kPanelVisible`, and rows are read from that panel only; a hidden page's rows all still read
visible.

**Row order.** `ShowSettings` inserts the bottom padding, then each group in reverse (its settings
in reverse, then its heading), then the title line — every one through `SettingsList.
MoveItemToTop`, which `SwapChildren`s the item down `ItemContainer.ChildPanels` to index 0. So the
array **is** the drawn order, and `SelectedIndex` indexes it.

**Settings rows.** `MCM_SettingBase extends UIMechaListItem`; each subclass sets the widget through
`SetWidgetType`, so `MakeRow` reads them exactly as it reads the game's own settings rows:

| Class | Widget | Value a sighted player reads |
|---|---|---|
| `MCM_Checkbox` | Checkbox | the box |
| `MCM_Slider` | Slider (`MCM_XCOM2_UISlider extends UISlider`) | **the `UIScrollingText` child `SliderValueTextControl`** — `UpdateSliderValueDisplay` writes the formatted number ("3", "0.50", or a mod's own filter) there; the bar's percent is NOT what is drawn |
| `MCM_Spinner` | Spinner, `Spinner.SetValue(text)` | the spinner's value text |
| `MCM_Dropdown` | Dropdown (`UIDropdown 'DropdownMC'`) | the dropdown's selection |
| `MCM_Button` | Button with its own ButtonLabel | the embedded button's label |
| `MCM_Label` | `UpdateDataDescription` | the text alone |

Tooltip: `AfterParentPageDisplayed` → `BG.SetTooltipText(DisplayTooltip)` — the row's BG child,
where `GameUI::ReadRowTooltip` already looks. `SetEditable(false)` → `SetDisabled`, and on the
slider, spinner and dropdown it also **hides** the widget (and the slider's value display).

## 3. Words

- Screen-level: `m_strTitle` ("Mod Settings"), `m_strSubtitle`, `m_strSaveAndExit`, `m_strCancel`
  are `localized` vars on the mod class. The header retains title and subtitle too
  (`UIX2PanelHeader.Title` / `Label`), but the class vars are read **by name** through
  `UE3Reflect::ReadStringProperty(screen, "m_strTitle")` — no offset needed (§4).
- Page title: `TitleLine.UpdateTitle(Title != "" ? Title : "Mod Settings")` → the `UIText` child's
  `SetSubTitle`, which fills `Text` and `HtmlText` both; read `Text` first, html as fallback.
- Group heading: `MCM_GroupLabel.GetFormattedLabel()` wraps the label in font markup before
  `UpdateTitle`, so `CleanText` is load-bearing on it.
- Buttons: `UIButton.Text`, retained.

## 4. What the build settled on

- **No `static_assert` can pin a mod class**, and none does. `MCM_*` classes are absent from the
  generated SDK. Nothing in `game_ui_mods.cpp` reads a mod class at a numeric offset: widgets are
  found by **walking `ChildPanels` and matching class names**, words are read **by name** through
  the reflection property walk. The engine offsets the walk uses (`UIPanel::ChildPanels`, the flag
  dword, `UIPanel::MCName`, `UIList::ItemContainer` / `SelectedIndex`, `UIButton::Text`,
  `UIText::Text` / `HtmlText`, the `UIMechaListItem` set behind `MakeRow`) are the shared ones,
  already pinned. If a mod-class field is ever needed by value (`SelectedPageID`,
  `SettingsPanels`), it is a by-name integer read through the same walk — never a hard-coded
  offset, which no rebuild of ours could catch drifting.
- **The container** is the screen's DIRECT child (`ParentPanel == screen`, §2) whose class is
  exactly `UIPanel` and which has a `UIList` child. Exact, not derived: `MCM_SettingsPanel extends
  UIPanel` and also holds a `UIList`, so a tolerant match would take the first page for the
  container. Direct, not merely listed: the screen's registry also holds every `ItemContainer`.
- **The current page** is the `MCM_SettingsPanel` child of the container whose own flags carry
  `kPanelVisible` (§2). None → `pageShown = false` and the recipe's page stop is a single label,
  `Vocab::ModConfigNoPage`.
- **The slider value** is the row's `UIScrollingText` child named `SliderValueTextControl` (matched
  on `UIPanel::MCName` — an identifier, never spoken), read only while that child is visible. A
  hidden display (a non-editable slider — `SetEditable(false)` hides the display AND the bar)
  speaks **no value**: the label and `unavailable` alone, as the sighted player sees it. The
  percent still held in the hidden `UISlider` is a fact the game draws nowhere (the aid rule).
- **Group boundaries** are kept even when a heading has no words (`AddGroup(name, "")` is legal;
  the formatted label is then bare font tags `CleanText` strips to nothing): the reader emits every
  `MCM_GroupLabelSeparator` as a heading entry, and the recipe closes the open group on each one,
  opening a new context only when there are words — so an unlabelled group's rows read under the
  page title, not under the previous group's heading.
- **The finder is stack-only** (`FindStackScreenRaw("MCM_OptionsScreen")`, exact). The screen is
  always pushed, and the object-scan fallback would spend a GObjects pass a second on every
  install without the mod, looking for a class that does not exist.
- **The injected button** becomes a node through a ChildPanels scan appended to `ReadOptions`:
  every visible, labelled `UIButton` DIRECT child of the settings screen (`ParentPanel == screen`,
  §2 — without it the Key Bindings row's embedded button doubled as a fifth bottom button) not
  among the four fixed ones.
  On mouse+keyboard the game's own extra buttons there (`ExitButton`, `ResetToDefaultsButton`) are
  controller- and console-only, so the scan catches exactly the injected ones — MCM's, and any
  other mod's.
- **Recipe** (`mod config`): three stops — `tabs` (vertical; a checked row speaks `selected`; Enter
  = `SendRowCommand(kCmdEnter)`), `page` (page title as the outer context, each group heading a
  context around its rows; `BuildOptions`' row mapping), `buttons` (Save and Exit, Cancel, Reset
  when shown). Start: the page's selected row, else its first row; with no page, the checked tab,
  else the first. Listed in `kRecipes` ahead of `settings`. Rows write focus back through the
  page's list, tabs through their own.

## 5. First live pass owed

Test set on this machine: AllSoldiersGainXP, EvacAll_WotC, WOTCIridarAutoReload — three pages.
From the main menu: Options → MOD SETTINGS.

**First run, 2026-09-17 21:16 (Alex):** item 1 passed — *"MOD SETTINGS, button, 4 of 4"* — and
then silence. `/status` showed the mod screen on top (`0x279 inited focused visible`, all three
tabs present) with the navigator still `attached (settings, focused)`: Settings two entries down
still read `focused`, because `UIOptionsPCScreen.OnLoseFocus` never calls `super` and so never
clears the bit, and the navigator re-probes only the attached recipe. Fixed the same evening:
`ProbeOptions` (and this screen's finder) require `IsTopmostScreen` as well as the focused bit
(`docs/surfaces/shell.md`, Settings). Items 2-7 below are still unrun.

**Second run, 21:31:** tabs, page switch, checkbox toggle, the slider's own value ("0.5"), the
Reset button on Iridar's page and the group heading all read. Space on Evac All's second row said
NOTHING though the mod ships *"Disables the tile overlay…"* for it: MCM calls `BG.SetTooltipText`
only from `AfterParentPageDisplayed`, which the panel's `Show()` runs, and a page's FIRST display
goes through `CreateUi` without a `Show()` — the manager is empty until the player leaves the tab
and comes back. `ReadModConfigRowTooltip` now falls back to the row's retained `DisplayTooltip`,
read by name. One line still owed a decision: a plain label row (`MCM_Label`, Iridar's credit
line) reads *"unavailable"* because the facade marks it non-editable — the greyed text is what a
sighted player sees, but on a row with nothing to edit the word is noise.

1. **The button exists**: on the settings screen the `buttons` stop has a fifth node, MOD
   SETTINGS, after the game's four (or three); Enter on it opens the mod screen and the attach
   names it "Mod Settings" plus the subtitle. If the stop still has four, the ChildPanels scan
   found the button hidden or unlabelled — `just dev-gui` on the settings screen.
2. **Tab landing**: with no page, the landing is the first tab and the `page` stop says "No page
   selected". Three tabs read, none `selected`.
3. **Page switch rebuild**: Enter on a tab — the tab re-reads with `selected`, the page stop fills
   with that mod's title as context, its groups as contexts, its rows; the landing moves to the
   page's first row. Enter on another tab swaps it; the previous page's rows must NOT linger.
4. **Slider value text**: a slider row speaks the number MCM draws ("3", not "33 percent"); Left /
   Right step it and the value re-speaks. A non-editable slider reads its label and `unavailable`
   and NO value — its bar and number are both hidden, so a percent here is a bug.
5. **Reset presence**: a page whose mod called `EnableResetButton` shows Reset in the `buttons`
   stop; one that did not shows only the two.
6. **Escape discards**: change a checkbox, Escape, reopen — the change is gone; Save and Exit keeps
   it. Say so on attach? Decide after hearing whether the loss surprises.
7. Audit: `grep -P '  '` over the ring (five localized labels read off instances); the distribution
   of `checked` / `unchecked` across a page that mixes both.
