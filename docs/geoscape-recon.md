# The geoscape — recon

> Static pass, 2026-08-21. Sources: the SDK's `Development/SrcOrig/XComGame/Classes`
> and `XComGame/Config/DefaultInput.ini` / `DefaultUI.ini`, plus the Highlander
> checkout at `deps/X2WOTCCommunityHighlander` (pinned `v1.31.0-release`).
> **First live pass 2026-08-21 — §12 is what it measured**, and it confirmed the
> headline finding, falsified two claims made here, and produced one rule (§4.3)
> that reshapes the design. §10 is the check list, with three items still open.
> Anything still marked *[inferred]* has NOT been measured.

The hollow globe on the CIC deck is `UIStrategyMap` — the Avenger flying over
Earth, the screen where the strategy campaign actually happens. This is the
layer where a blind player currently has the least: the Avenger ant farm
(`docs/avenger-view-recon.md`) gets them as far as walking into the CIC, and
the globe is where that trail stops.

---

## 1. What it is, and how the player gets there

`UIStrategyMap extends UIX2SimpleScreen`, 1935 lines, spawned exactly once by
`XComHQPresentationLayer.UIEnterStrategyMap` (`:665`) and pushed onto the stack.
It is reached from:

- **The CIC room in the ant farm** — `X2StrategyElement_DefaultFacilities:175`,
  the CIC facility template's click handler:
  `HQPres.ClearToFacilityMainMenu(); HQPres.m_kFacilityGrid.Hide(); HQPres.UIEnterStrategyMap(true);`
  So the ant-farm recipe's Enter on the CIC room is already the door — no
  intermediate `UIFacility` screen. *[inferred — the ant farm has never been
  driven into the CIC live]*
- **`UIAvengerShortcuts` key `1`** (`FXS_KEY_1` → `NavHelp.HotlinkToGeoscape()`),
  from anywhere in the Avenger.
- The game itself, after a mission, a covert-action ambush, or a
  `XGGeoscape.OnEnterMissionControl` transition.

Three UI states (`EStrategyMapState`): `eSMS_Default`, `eSMS_Resistance`
(the resistance-network overlay, toggled by the nav-help button), and
`eSMS_Flight` (the Avenger in transit — **all input is refused**:
`OnUnrealCommand` opens with `if(m_eUIstate == eSMS_Flight) return true;`).

**Stack while it is up** — ✅ MEASURED 2026-08-21, top first:
`UIStrategyMap` → **`UIFacilityGrid`** → `UIAvengerHUD` → `UIEventNotices`.
⚠ Note the order: the ant farm sits *between* the globe and the Avenger HUD,
not below it — so a key `UIStrategyMap` does not handle meets `UIFacilityGrid`
before it ever reaches `Shortcuts`/`NavHelp` (§7). Our gate is "is
`UIStrategyMap` the first stack entry", the same test `ReadAvengerGate` already
makes for the grid; and the grid's own gate correctly stood the ant-farm reader
down (`navigator: detached`) because the grid is no longer first.

---

## 2. ⛔ THE HEADLINE FINDING — the geoscape's selection system is controller-only

`XComHeadquartersInput.PostProcessCheckGameLogic` (`:311`):

```unrealscript
simulated function bool PostProcessCheckGameLogic( float DeltaTime )
{
	ProcessGeoscapeRotation(DeltaTime);
	if( `ISCONTROLLERACTIVE )
	{
		ProcessGeoscapeSelection(DeltaTime);   // -> UIStrategyMap.UpdateSelection
		ProcessGeoscapeZoom(DeltaTime);
		ProcessPawnRotation();
	}
	...
}
```

`UIStrategyMap.UpdateSelection` (`:1437`) is the whole cursor: it moves the
3D selection puck, and — while `bSelectNearest` — calls
`SelectMapItemNearestLocation(ViewLocation)` (`:319`), which selects whatever
selectable map item is nearest the **centre of the view**, inside
`SelectionRadius * zoom`. On a controller the globe *is* a cursor: you pan, and
the pin under the crosshair becomes `SelectedMapItem`.

On mouse+keyboard that function never runs. Consequences, all of them
mechanical:

- `SelectedMapItem` is never set by proximity. The `if (SelectedMapItem != none)`
  branch at the top of `OnUnrealCommand` (`:1631`) — which is what routes a key
  to the *pin* rather than to the screen — is dead for a keyboard player.
- The selection puck (`CursorMesh`) never moves, and `Crosshair` is spawned
  only under `` `ISCONTROLLERACTIVE `` in the first place.
- `UpdateZoom` never runs either, so the analogue-trigger zoom path is dead;
  keyboard zoom exists only as `FXS_MOUSE_SCROLL_UP/DOWN` in `OnUnrealCommand`.

This is the same shape as `UIFacilityGrid.InitializeNavigationGrid` — a
complete navigation system the game builds and then declines to run for
mouse+keyboard players. ⛔ But **it is NOT the same fix.** The ant farm needed
one call, once, per grid instance. This one is a per-frame tick: ungating it
would mean an outbound `ProcessEvent` at 60 Hz forever, and it would also start
fighting whoever else moves the view. The ant-farm lesson does not transfer —
see §8 for what replaces it.

✅ There is a second half to this, and it is a gift. See §4.4: the tray
navigator *is* keyboard-driven on mouse+keyboard, and it calls
`SetSelectedMapItem(MapItem, true)` — so `SelectedMapItem` becomes non-none via
the arrows and then *stays* set, precisely because nothing recomputes it.
✅ Measured 2026-08-21 (§12): one `Left` moved the index 0 → 1 and the selection
POI → mission, and it was unchanged 12 s later.

---

## 3. The vanilla keyboard map, as read from source

Everything a mouse+keyboard player can do with the keyboard on the geoscape:

| Key | Where it is handled | Effect |
|---|---|---|
| `←` `→` `↑` `↓` (and `W A S D`) | `XComHeadquartersInput.PreProcessCheckGameLogic:88-146` | set `m_fKeyPanX/Y`; `ProcessGeoscapeRotation` pans the globe. Returns `true`, which **continues** processing (`XComInputBase:492` aborts only on `false`) — so the arrow ALSO reaches the screen stack |
| `←` `→` | `UIStrategyMap` → `super.OnUnrealCommand` → `UINavigator` (`HorizontalNavigation = true`) | `Navigator.Prev()` / `Next()` over the mission-tray icons — ✅ measured, §12 |
| `↑` `↓` | same | nothing (`HorizontalNavigation` blocks the vertical branch) |
| `Enter` / `Space` | `UIStrategyMap.OnUnrealCommand:1663` | `ToggleScan()` — start/stop time, **iff `IsScanningAllowedAtCurrentLocation()`**. Note this does not consult `SelectedMapItem` at all; it acts on where the Avenger IS |
| `Esc` / RMB | `:1652` | `TryExiting()` — stops the scan if scanning, else leaves for the Avenger |
| `Tab` | `:1651`, inside `` `if(`notdefined(FINAL_RELEASE)) `` | also `TryExiting()`. ⚠ **Retail is not a FINAL_RELEASE build** (established by the live `XComTacticalCheatManager`), so this is live in the shipped game |
| mouse wheel | `:1677` | zoom, `ZoomSpeed = 0.085` per notch |
| `1`…`7` | `UIAvengerHUD.Shortcuts`, guarded twice on `bIsVisible` | ✅ measured 2026-08-21: **nothing on the globe.** The shortcut bar is hidden while the map is up, so `2`-`7` are swallowed by the gate whose comment says it exists to stop them "pulling us from the geoscape". `1` reaches the globe *from the Avenger* only. The digits are free here |
| `P` | `UIAvengerHUD:587`, also `notdefined(FINAL_RELEASE)` | opens the personnel list |
| `F5` / `F9` | `XComHeadquartersInput.Key_F5` / `Key_F10` | quicksave / quickload |
| LMB drag | `PreProcessCheckGameLogic:78` | pan the globe |
| LMB on a pin | `UIStrategyMapItem.OnMouseEvent` `FXS_L_MOUSE_UP` | `GeoscapeEntity.AttemptSelectionCheckInterruption()` — **the only activation verb the game gives a keyboard player no route to** |

Everything else in `OnUnrealCommand` is a controller code: `FXS_BUTTON_LBUMPER`
/ `RBUMPER` (tray prev/next), `FXS_BUTTON_SELECT` (resistance-network toggle),
`FXS_BUTTON_R3` (look at Avenger), `FXS_BUTTON_L3` (Chosen info),
`FXS_BUTTON_Y` (dark events), `FXS_BUTTON_A` (activate the selected pin). The
four `FXS_DPAD_*` cases are present and **empty** — `SelectMapItemInDirection`
is commented out; directional pin selection was cut.

---

## 4. The four surfaces

### 4.1 The mission tray — `UIStrategyMap_MissionIcon` × 20 ✅ FULLY RETAINED

This is the strip of icons along the bottom, and it is the single most
important object on the screen: **it is the game's own list of everything
actionable.** `UIStrategyMap.MAX_NUM_STRATEGYICONS = 20` icons are spawned in
`InitMissionIcons` (`:1364`) and re-bound by `UpdateMissions` (`:1057`) —
which is called on `OnReceiveFocus`, on flight-mode toggle, and whenever the
map changes.

What `UpdateMissions` puts in it, in this order:

1. **Faction HQs** — `XComGameState_Haven`, into fixed slots by champion
   class: Reaper → 0, Skirmisher → 1, Templar → 2.
2. **Other scan sites** — `XComHQ.GetAvailableScanningSites()`, sorted by
   `SortScanSitesType`. That call returns every `XComGameState_ScanningSite`
   that `CanBeScanned()`, plus the Black Market when visible, plus faction
   havens. Subclasses: `XComGameState_WorldRegion`, `_BlackMarket`,
   `_PointOfInterest`, `_ResourceCache`, `_Haven`.
3. **Missions** — every `XComGameState_MissionSite` with `Available`, sorted
   by `SortMissionsUnlocked` then `SortMissionsType`; Guerrilla Ops collapse to
   one entry; the Final mission only once `!bNotAtThreshold`.

⛔ **A region appears in the tray only while it is actively scannable.**
`XComGameState_WorldRegion.CanBeScanned()` is
`(!HaveMadeContact() && bCanScanForContact) || (ResistanceLevel == eResLevel_Contact && bCanScanForOutpost)`.
So the tray never holds "the seventeen regions" — it holds the two or three
you could scan right now. Everything a player wants to know about the *rest* of
the world (income, resistance level, which Chosen owns it, whether contact is
even possible yet) is not here. That gap is §8.2.

Each icon retains, as real script properties:

| Field | What it is |
|---|---|
| `ScanSite` | `XComGameState_ScanningSite` ref, or none |
| `MissionSite` | `XComGameState_MissionSite` ref, or none |
| `MapItem` | the `UIStrategyMapItem` pin on the globe |
| `Tooltip` | `UIStrategyMap_MissionIconTooltip`, which retains `Title` and `Body` |
| `idNum`, `bFocused`, `bMoveCamera` | |
| `OnClickedDelegate` | bound to `<site>.AttemptSelectionCheckInterruption` |

`Title`/`Body` come from `GetUIButtonTooltipTitle()` / `GetUIButtonTooltipBody()`
on the state object — already localized, already exactly what the sighted
player's hover tooltip says. For a scan site the default pair is
*"<site name>: <region name>"* / *"N days remaining"*
(`XComGameState_ScanningSite:303-318`); missions override with the mission-source
pin label and, for Guerrilla Ops, a list of the parallel ops.

⚠ The Highlander adds `MissionIconSetScanSite` / `MissionIconSetMissionSite`
events (issue #537) fired at exactly the moment an icon is bound to a state
object, and `OverrideScanSiteTooltip` / `OverrideMissionSiteTooltip` tuples. So
a companion listener could model the tray event-driven rather than by polling.
Neither is needed if the widget read works — note it and move on.

### 4.2 The pins on the globe — `UIStrategyMapItem` and its 21 subclasses

One per `XComGameState_GeoscapeEntity`, spawned lazily by
`UIStrategyMap.GetMapItem` and registered as the entity's *visualizer*
(`History.SetVisualizer`), which is also how you go back the other way:
`History.GetVisualizer(ObjectID)`.

The base class retains its label under a dirty check
(`UIStrategyMapItem.SetLabel:105` — `m_strLabel`, `m_strLabel2`, `m_strSource`,
`m_strSource2`, `m_strReward`, `m_strReward2`, `m_strFlavorText`), so for every
subclass whose `UpdateFlyoverText` goes through `SetLabel`, the retained value
**is** what Flash was last told. That covers Continent, City, Bastion, Haven,
Avenger, Skyranger, UFO, LandingSite, TradingPost.

⛔ Two subclasses do NOT retain and must be re-derived:

- **`UIStrategyMapItem_Region`** — `SetRegionInfo()` (`:394`) pushes
  region label / supply income / state / resistance level / hover info straight
  into Flash with no dirty check and no field. The source is
  `XComGameState_WorldRegion` + its template, which is script-natural.
- **`UIStrategyMapItem_Mission`** — retains only `Label` (the mission-source
  pin label) via `MC.FunctionString("setHTMLText", …)`. And the pin is
  `Hide()`n by default; `Show()` is overridden to no-op unless `bIsFocused`.

⛔ **`UIScanButton` is NOT a uniform readable surface — this claim was made
here and FALSIFIED live 2026-08-21.** Region, POI, Black Market, Resource Cache
and Faction HQ pins each spawn one, and it does retain `Type`, `ButtonState`,
`Title`, `Subtitle`, `DaysValue`, `DaysLabel`, `IconPath`, `PercentFilled`,
`bShouldAnimate` under dirty checks. But measured on a live campaign, of the
twenty-odd scan buttons on the map **exactly one** held real text — the POI the
Avenger was landed at (*"НЕПОВРЕЖДЕННЫЕ СТРОЕНИЯ" / "Новобранцы" / "6" / "дн."*).
Every other one read either the class default `"UNINITIALIZED"` with
`Type = eUIScanButtonType_MAX` and `PercentFilled = -1`, or four empty strings.
The reason is §4.3, and it applies to the whole pin layer, not just this widget.
`GetButtonType()` is still the pin's own statement of what a scan would do
(`Default` / `Contact` / `Tower` / `Faction`) — when it has been set at all.

`IsSelectable()` — the base returns false; true on Mission,
AlienNetworkComponent, POI, ResistanceHQ, BlackMarket, ResourceCache, and on
Region conditionally (`:688`: not the Res-HQ region, scan button in a real
state, and either a contact/outpost button showing or a scan available). That
predicate is the game's own answer to "is this pin worth stopping on", and a
ring should honour it rather than invent one — except that it deliberately
excludes contacted regions, which is exactly what §8.2 needs to include.

### 4.3 ⛔ A pin's widget state is worthless until it has been on screen

Measured 2026-08-21 and the single most important result of the live pass.
`UIStrategyMapItem.UpdateVisuals` computes
`bUpdateScreenPosition = ShouldGeoscapeEntityBeDrawn && bOnScreen` and calls
`UpdateFromGeoscapeEntity` — which is what runs `UpdateFlyoverText`,
`SetLabel`, `SetRegionInfo`, `ScanButton.SetText` and the `Show()`/`Hide()` on
the contact and outpost buttons — **only inside that branch.** So a pin the
camera has never framed has never been told anything.

Two region pins, same screen, same instant:

| | `bLastOnScreen` | `HistoryIndexOnLastUpdate` |
|---|---|---|
| region ObjectID 18 (in view) | true | 2687 |
| region ObjectID 34 (off view) | false | **−1** |

`HistoryIndexOnLastUpdate == -1` is the exact, self-declaring "never updated"
marker (`DefaultProperties` sets it to −1 and `UpdateFromGeoscapeEntity` stamps
`History.GetCurrentHistoryIndex()`). Past that first update the value is only
as fresh as the last frame the pin was visible.

⚠ **Widget *visibility* is as untrustworthy as widget *text* here.** Of sixteen
region pins, the only two whose `ContactButton` correctly read hidden were the
two that had updated; every never-updated region still showed its
`UILargeButton` (*"УСТАНОВИТЬ КОНТАКТ"*) and `UIScanButton` as `visible`,
straight from `DefaultProperties`. So the ant farm's rule — *take a row's kind
from a widget that is VISIBLE* — inverts on this screen and must not be carried
over.

⛔ **Consequence for the design.** The ant-farm pattern ("every word comes off
the overlay, none from the game state") does **not** transfer to the globe. It
transfers to exactly two things: the mission tray, which `UpdateMissions`
rebuilds regardless of camera position (§4.1), and pins that happen to be in
view. Everything else — and that is the entire region layer, which is most of
what a player needs — has to be read from `XComGameState_*` in the companion.
A ring that read pins would be reading `"UNINITIALIZED"` for most of the world.

⚠ Two corollaries worth keeping:
- `m_strLabel` is **not** a label. Measured: the POI pin holds
  `"СЛУХИ СОПРОТИВЛЕНИЯ FLYOVER TEXT NOT YET IMPLEMENTED FOR THIS PIN TYPE"`
  and every `UIStrategyMapItem_RegionLink` holds the bare placeholder — the base
  `UpdateFlyoverText` fires for subclasses that never override it. Never speak
  `m_strLabel` without knowing the subclass overrides it.
- `MapPin_Header` and `MapPin_Tooltip` **are** reliable: they are `localized`
  defaults on the class, not per-instance state, so they are populated whether
  or not the pin has ever drawn (*"ШТАБ СОПРОТИВЛЕНИЯ"*, *"МЕСТО ОПЕРАЦИИ"*,
  *"СЛУХИ СОПРОТИВЛЕНИЯ"*). They give the pin's *kind*, never its contents.

### 4.4 ✅ The tray navigator — keyboard-driven already, CONFIRMED LIVE

`UIStrategyMap.InitScreen` sets `Navigator.HorizontalNavigation = true`,
`LoopSelection = true`, `OnSelectedIndexChanged = OnNavigationChanged`.
`UpdateMissions` ends with, on the mouse+keyboard branch,
`Navigator.SelectFirstAvailableIfNoCurrentSelection()`. `Navigator.AddControl`
is called for every bound icon except a faction HQ whose faction has not been
met — which is a parity filter the game already wrote for us.

`UIScreen.OnUnrealCommand` pipes anything the screen did not handle into
`Navigator.OnUnrealCommand`, and `UINavigator` maps `←`/`→` to `Prev`/`Next`
when `HorizontalNavigation`. `UIStrategyMap.OnUnrealCommand` sends arrows to
`default:` → `super`. So the chain closes: **`←`/`→` should already step the
tray on mouse+keyboard.**

What happens on each step, `OnNavigationChanged` (`:454`):

```unrealscript
MissionItemUI.MissionIcons[i].bMoveCamera = true;
MissionItemUI.MissionIcons[i].OnReceiveFocus();
SetSelectedMapItem(MapItem, true);
```

⛔ And `UIStrategyMap_MissionIcon.OnReceiveFocus` (`:258`) opens with
`if (Movie.IsMouseActive()) return;` — so on mouse+keyboard the icon does
**not** show its selection brackets and does **not** fly the camera to the
site. `SetSelectedMapItem(MapItem, true)` is outside that guard and does run.

So the predicted mouse+keyboard behaviour is: arrows silently move a selection
that has no visual, no camera move and no tooltip — and, because
`UpdateSelection` never runs to overwrite it, the selection *sticks*. If that
holds live, we get the game's own list cursor for free and only have to supply
the voice and the camera. **This is finding #1 for the live pass.**

The camera move itself is not gated: `OnMouseEvent(FXS_L_MOUSE_IN)` (`:139`)
runs `if (bMoveCamera || `ISCONTROLLERACTIVE == false)` → `CAMSaveCurrentLocation()`
then `CAMLookAtEarth(site.Get2DLocation(), ZoomLevel)` — and `ZoomLevel` is
`0.5` from `DefaultUI.ini:93`. So calling the icon's own `OnMouseEvent` gives
us camera + tooltip refresh with no new code; `CAMLookAtEarth` is
`XComHQPresentationLayer:5218` and is just `NewEarthView` + `EARTH.SetViewLocation`
+ `EARTH.SetCurrentZoomLevel`.

### 4.5 The HUD — `UIStrategyMap_HUD` + `UIAvengerHUD` ⛔ mostly write-only

`UIStrategyMap_HUD` (spawned by `InitScreen`, `LibID = "StrategyMapHUD"`)
draws the doom meter, the doom countdown, the continent popular-support meter,
the alert meter, the tray's three section labels, and two info buttons. It
retains almost nothing: `CachedDoom`, `bDoomCounterVisible`, `m_strLog`. Doom
pips, support pips and the countdown all go to Flash as `MC.QueueNumber`
sequences. The sources are `XComGameState_HeadquartersAlien`
(`GetCurrentDoom` / `GetMaxDoom` / `GetTimerDisplayValues`) and
`XComGameState_Continent` — companion-side reads.

The clock and date are on `UIAvengerHUD.EventQueue`:
`UIEventQueue.RefreshDateTime` (`:159`) pushes
`X2StrategyGameRulesetDataStructures.GetDateString(dateTimeData)` plus
separated hours / minutes / suffix into Flash and retains none of it. Source is
`` `GAME.GetGeoscape().m_kDateTime ``, **localized to the Avenger's longitude**
via `GetLocalizedTime(XComHQ.Get2DLocation(), …)` unless `XComHQ.Flying`. Any
"what time is it" readout has to reproduce that clause or it will disagree with
the screen by hours.

Per `docs/avenger-view-recon.md` §2.5 the event-queue *list* itself is retained
(`UIEventQueue_ListItem.Title` / `DaysValue`) — that is the right-hand list of
upcoming events, and on the geoscape it is the closest thing to "what is about
to happen".

---

## 5. Activation — one verb, four shapes

Everything a pin does goes through
`XComGameState_GeoscapeEntity.AttemptSelectionCheckInterruption()` (`:242`):

```
AttemptSelectionCheckInterruption -> CanInteract() && !DisplayInterruptionPopup()
   -> AttemptSelection -> CanInteract() && !DisplaySelectionPrompt()
      -> ConfirmSelection -> XComHQ.SetPendingPointOfTravel(entity)   // fly there
```

- **Scan sites, POIs, regions, Black Market, faction HQs** take the default
  path: the Avenger flies there (`SetPendingPointOfTravel`), the map enters
  `eSMS_Flight`, and on arrival scanning becomes possible.
- **Missions** override `DisplaySelectionPrompt` (`XComGameState_MissionSite:1721`)
  → `HQPRES.OnMissionSelected(self)` → the `UIMission_*` briefing screen. So a
  mission pin opens a screen rather than flying anything.
- **`DisplayInterruptionPopup`** fires a confirmation `UIDialogueBox` when the
  move would abandon an in-progress scan — the dialog recipe already covers
  that modal.
- **Region pins carry three extra verbs**, and `UIStrategyMapItem_Region.OnUnrealCommand`
  (`:662`) states the game's own priority order:
  `ShowContactButton()` → `HQPRES.UIMakeContact(region)`; else
  `ShowOutpostButton()` → `HQPRES.UIBuildOutpost(region)`; else
  `IsAvengerLandedHere()` → `ScanButton.ClickButtonScan()`; else fly there.
  On mouse+keyboard those are three separate on-pin buttons, reachable only by
  clicking them.

Starting and stopping time is separate and is *not* a pin verb:
`UIStrategyMap.ToggleScan()` (`:1751`), on `Enter`/`Space`, acting on
`XComHQ.GetCurrentScanningSite()` — where the Avenger already is. It warns
first (`UITimeSensitiveMission`) if any `MissionState.Expiring` mission has not
had its skip popup shown, and sets `Geoscape.m_fTimeScale` to
`TWELVE_HOURS` / `ONE_MINUTE`.

---

## 6. Where the keyboard dead-ends

Ranked by what it costs a player:

1. ⛔ **No pin can be activated from the keyboard.** `FXS_BUTTON_A` is the only
   route to `AttemptSelectionCheckInterruption` in `OnUnrealCommand`, and it
   requires `SelectedMapItem != none`, which §2 says never happens by proximity.
   Even though the tray navigator does set it (§4.4), `Enter`/`Space` are
   intercepted by the screen for `ToggleScan` *before* reaching the pin —
   `UIStrategyMap.OnUnrealCommand` handles `FXS_KEY_ENTER`/`SPACEBAR` in its own
   switch, and the `SelectedMapItem.OnUnrealCommand(cmd, arg)` delegation above
   it only forwards codes the pin recognises, which is `FXS_BUTTON_A` and not
   the keyboard's Enter. *[inferred — this is the second thing to measure]*
2. ⛔ **Make Contact / Build Outpost are mouse-only.** `ContactButton` and
   `OutpostButton` live on the region pin; the pin is not in any navigator on
   mouse+keyboard.
3. ⛔ **Dark Events is mouse-only.** `AddDarkEventsButton` ends with
   `DarkEventsContainer.DisableNavigation(); DarkEventsButton.DisableNavigation();`
   and the only key route is `FXS_BUTTON_Y`.
4. ⛔ **Chosen Info and Resistance Orders are mouse-only.**
   `m_kChosenInfoButton` / `m_kResistanceInfoButton` are children of
   `StrategyMapHUD`, and `UpdateMissions` calls `Navigator.Clear()` and then
   adds only mission icons — so the HUD panel is not in the screen's navigator
   at all. Controller has `FXS_BUTTON_L3` for Chosen; keyboard has nothing for
   either.
5. ⛔ **The resistance-network toggle is a nav-help button.** On mouse+keyboard
   `UpdateButtonHelp` (`:651`) adds it as a clickable `XComButtonIconPC`
   hotlink; the key route is `FXS_BUTTON_SELECT`, controller only.
6. ⚠ **`UpdateCenteredNavHelp` (`:699`) is dead code** — declared, never called
   from anywhere in the SDK sources. So the *"SCAN SITE"* / *"MOVE TO SITE"*
   prompts it builds never appear for anyone, and `m_strScanSite`,
   `m_strMoveToSite`, `m_strStartScan`, `m_strSelectSite` are unused strings.
   Do not treat them as parity text; do consider them as a source of *localized
   verb words* we would otherwise have to invent.

---

## 7. The key budget — the strategy layer is nearly empty, unlike tactical

Measured over every section of `DefaultInput.ini`.
`[XComGame.XComHeadquartersInput]` binds: mouse axes and buttons, `Enter`,
`SpaceBar`, `Escape`, arrows, `W A S D`, `0`-`9`, numpad `0`-`9`, `F1`-`F5`,
`F9`, `F12`, `Alt+F4`, `Tab`, `PageUp`, `PageDown`, the modifier keys, and the
whole Xbox pad. **No other letters.**
`[Engine.PlayerInput]` adds `W A S D` (arrow forwarders), `P N M U J B`
(`<X>_Key_Press` forwarders into `XComInputBase.InputEvent`) and `Shift+R`
(ErrorReport) — and *removes* `F E C T Y` with `-Bindings`.

So the letters bound anywhere in the strategy layer are
**`W A S D` + `P N M U J B`**, leaving free:

> **C E F G H I K L O Q R T V X Y Z** — sixteen bare letters.

Against **two** in tactical (`H` and `I`). Punctuation is free here as it is
there (`;` `'` `,` `.` `/` carry no `Bindings=` entry in any section).

Two caveats before spending any of it:

- ⚠ `P` opens the personnel list and `Tab` exits the geoscape, both under
  `` `if(`notdefined(FINAL_RELEASE)) `` — **which retail satisfies**. Treat
  them as taken.
- ✅ **The number row IS free on the globe** — measured 2026-08-21. `1` reaches
  the geoscape *from the Avenger*, but `2`-`7` do nothing *on* the geoscape.
  `UIAvengerShortcuts.OnUnrealCommand` guards twice on `bIsVisible`, and the
  shortcut bar is hidden while the map is up; Firaxis' comment on the second
  guard says why: *"if we aren't visible don't change categories because it
  will pull us from the geoscape"*. `1` still fires because its effect runs
  inside the switch, before that gate — but only where the bar is visible.
  So digits are inert here and cost nothing to claim.
- The leniency rule still holds: anything we claim bare stays reachable at
  `Ctrl+key`, because none of these are `KeyBind`s with `bIgnoreCtrl`.

---

## 8. Design sketch — two surfaces, and why

This is the section to argue with, not a decision.

The project's rule is that **graph recipes are the default and game-native
narrate-only is the exception that must be justified**
(`feedback_graph_recipes_are_the_rule`). The test earned by the ant farm:
*narrate-only is justified only when taking the arrows would destroy
information we cannot reproduce.* Here the arrows destroy nothing we cannot
reproduce — the camera move is one `CAMLookAtEarth` call, the tooltip is
retained on the widget, and there is no focus-gated label like the ant farm's
`if( bIsFocused )` room name. **So: recipe, not narrate-only.** But the map
also has a scanner-shaped half, and the split mirrors tactical exactly.

### 8.1 Surface A — a graph recipe over the mission tray

Nodes come from `UIStrategyMap.Navigator`'s own targets (which already exclude
un-met faction HQs), one per icon, in the game's own tray order. Each node
speaks its retained tooltip `Title` + `Body`, plus the pin's `UIScanButton`
state when it has one — *"Тень: Западная Африка. Разведка контакта, 12 дней,
34%"* — plus lock and golden-path flags (`AS_SetLock` / `AS_SetGoldenPath`
are pushed, so those two need re-deriving from `bNotAtThreshold` /
`GetMissionSource().bGoldenPath`; everything else is retained).

Activation: the icon's `OnClickedDelegate`, i.e.
`AttemptSelectionCheckInterruption` — the game's own click path, which brings
the interruption popup and the mission screen with it.

Moving the camera as focus moves: `MissionIcon.OnMouseEvent(FXS_L_MOUSE_IN)`,
which is *not* mouse-gated and does `CAMSaveCurrentLocation` +
`CAMLookAtEarth`. That keeps the sighted view in sync with the blind cursor —
worth doing even though nobody is looking, because it keeps `SelectedMapItem`,
the pin's hover state and our node in agreement.

### 8.2 Surface B — a geoscape ring, the object scanner's sibling

The tray answers *what can I act on*. It does not answer *what is the world
like* — and on the geoscape that second question is most of the game. A ring
with the object scanner's exact shape (per-category cursor, `PageUp`/`PageDown`
within a category, `Ctrl+PageUp`/`Ctrl+PageDown` between non-empty categories,
one key to re-announce, one key to fly the camera there) over the geoscape
entities:

- `regions` — name, resistance level, supply income, contact state, controlling
  Chosen, whether contact/outpost is available. Seventeen of them, and today a
  blind player can reach exactly none.
- `continents` — name, bonus card name, whether the bonus is active, radio
  towers `n of m`. Retained on the pin (`SetLabel` path).
- `missions` — including the ones the tray collapses (all parallel Guerrilla Ops).
- `scan sites` — POIs, resource caches, Black Market, faction HQs.
- `facilities` — `XComGameState_AlienNetworkComponent`, with `Doom`.
- `airships` — the Avenger (and where it is), the Skyranger in flight, UFOs.
- `chosen` — `XComGameState_AdventChosen` territory.
- `covert actions` — `XComGameState_CovertAction` sites.

Enumeration is `History.IterateByClassType(class'XComGameState_GeoscapeEntity')`
filtered on `ShouldBeVisible()` — the game's own visibility predicate, which is
what `SelectMapItemNearestLocation` uses and therefore the fog-parity rule for
this layer. Words come from the pin (`History.GetVisualizer(ObjectID)`) where
retained, from the state object where not.

Position language is the open question. Tactical says *"6 north, 1 west"*; on a
globe that is wrong. Candidates: the region name (*"Western Africa"*), the
continent, and `XComEarth.ConvertEarthCoordsToMiles` for a real distance from
the Avenger. **Region-and-continent is probably the whole answer** — it is what
a sighted player reads off the labels, and a bearing on a sphere is not
information anyone uses.

### 8.3 What each surface must NOT do

- ⛔ **Do not ungate `ProcessGeoscapeSelection`.** Per-frame outbound calls, and
  it would fight the recipe for `SelectedMapItem` every frame — the geoscape's
  own version of `Mouse_CheckForPathing`. If we ever *do* need it silenced,
  the mouse-gate swallow applies to `XComHeadquartersInput.PostProcessCheckGameLogic`
  the same way it applied to the tactical one; but the reason to swallow would
  be to stop the game overwriting *us*, and on mouse+keyboard it never runs, so
  there is nothing to stop.
- ⛔ **Do not flip `ISCONTROLLERACTIVE`.** Same ruling as the ant farm, and
  worse here: it changes which HUD is built, hides the To-Do widget
  (`UpdateToDoWidget:303`), hides the Chosen and Resistance buttons
  (`UIStrategyMap_HUD.UpdateData`), and routes the number keys through a
  highlight-then-confirm path.
- ⚠ **Do not claim `Enter`/`Space`.** They are the scan toggle, they work today,
  and they are the one thing a blind player can already do on this screen.
- ⚠ **Do not claim the arrows without deciding about globe panning.** They pan
  AND (probably) drive the tray. If we claim them for our own cursor, panning
  survives at `Ctrl+arrow` for free — but panning is also what a sighted player
  uses to *find* things, and our ring replaces that job entirely.

---

## 9. Data inventory

| Fact | Where it lives | Retained? |
|---|---|---|
| Tray contents | `UIStrategyMap.MissionItemUI.{MissionIcons,ScanSites,Missions}` | ✅ |
| Tray icon → state object | `UIStrategyMap_MissionIcon.{ScanSite,MissionSite}` | ✅ |
| Tray icon words | `UIStrategyMap_MissionIcon.Tooltip.{Title,Body}` | ✅ |
| Navigable subset + cursor | `UIStrategyMap.Navigator.{NavigationTargets,SelectedIndex}` | ✅ |
| Selected pin | `UIStrategyMap.{SelectedMapItem,LastSelectedMapItem}` | ✅ (but see §2) |
| UI state / flight mode | `UIStrategyMap.m_eUIState` | ✅ |
| Scan button on any pin | `UIScanButton.{Type,ButtonState,Title,Subtitle,DaysValue,DaysLabel,PercentFilled}` | ✅ |
| Pin labels (most types) | `UIStrategyMapItem.{m_strLabel,m_strLabel2,m_strSource,…}` | ✅ |
| Region label / income / level | `UIStrategyMapItem_Region.SetRegionInfo` | ⛔ Flash-only → `XComGameState_WorldRegion` |
| Mission pin label | `UIStrategyMapItem_Mission.Label` | ✅ (single string only) |
| Pin world position | `UIStrategyMapItem.{Cached2DWorldLocation,CachedWorldLocation}` | ✅ |
| Pin ↔ state object | `GeoscapeEntityRef.ObjectID`; `History.GetVisualizer(id)` | ✅ |
| Doom / support / alert meters | `UIStrategyMap_HUD` | ⛔ Flash-only → `XComGameState_HeadquartersAlien`, `_Continent` |
| Date and clock | `UIEventQueue.RefreshDateTime` | ⛔ Flash-only → `` `GAME.GetGeoscape().m_kDateTime `` + `GetLocalizedTime` |
| Event queue rows | `UIEventQueue_ListItem.{Title,DaysValue}` | ✅ (per avenger recon §2.5) |
| Is time running | `` `GAME.GetGeoscape().IsScanning() ``, `m_fTimeScale` | — |
| Where the Avenger is | `XComHQ.GetCurrentScanningSite()`, `XComHQ.Get2DLocation()`, `XComHQ.Flying` | — |
| View / zoom | `` `EARTH.{GetViewLocation,GetCurrentZoomLevel} `` | — |

**Highlander delta**: `UIStrategyMap`, `UIStrategyMap_HUD`,
`UIStrategyMap_MissionIcon` and `XComGameState_GeoscapeEntity` are all patched,
but **not one of them adds a `var`** — verified by diffing `SrcOrig` against the
pinned checkout. Layouts are unchanged; the changes are the tray-slot fix for
modded factions (issue #76), the events in §4.1, `ShouldShowResInfoButton`
(#365), the nav-help event (#932), and a `SetPendingPointOfTravel` RTB fix
(#927). `UIStrategyMapItem*` and `XComHeadquartersInput` are not patched at all.

---

## 10. What the live pass must answer

In order. All of these are `just arm` + the harness; none needs a build.

1. **`/status` with the globe up** — confirm `UIStrategyMap` is the first stack
   entry and record what is under it. Decides the gate.
2. **Does `←`/`→` step the tray?** `just dev-props "screen=UIStrategyMap"`,
   note `SelectedMapItem` and the navigator's index; `just dev-input Left`;
   read again. This is the single most consequential unknown (§4.4). If it
   steps, we inherit a working list cursor; if it does not, surface A has to
   drive `Navigator.SetSelected` itself.
3. **Does `SelectedMapItem` then STICK?** Re-read after a few seconds without
   input. §2 predicts yes on mouse+keyboard; a controller-active session would
   predict no.
4. **`/gui` on `UIStrategyMap`** — how many panels, and whether the twenty
   `StrategyIcon<N>` icons and the `UIStrategyMapItem` pins are visible in the
   tree with their `ParentPanel` links intact. Expect it to be large.
5. **`/props "ptr=<a tray icon>"`** — confirm `ScanSite` / `MissionSite` /
   `MapItem` / `Tooltip` read as live object pointers, and that the tooltip's
   `Title` / `Body` hold localized text.
6. **`/props "ptr=<a region pin's UIScanButton>"`** — confirm `Title`,
   `DaysValue`, `DaysLabel`, `PercentFilled`, `Type`. This is the claim that
   one reader covers every scannable pin.
7. **`/call fn=LookAtAvenger arg=0.75` on `UIStrategyMap`** — cheapest proof
   that outbound calls into this screen work, and it moves the camera visibly.
8. **`/call` an icon's `OnMouseEvent`** with `FXS_L_MOUSE_IN` — does the camera
   fly to the site on mouse+keyboard, as §4.4 predicts?
9. **Press `Enter` with the Avenger somewhere scannable** — confirm the scan
   starts and note what `/speech` says today (nothing, presumably).
10. **Press `1` on the globe** — does it jump into an Avenger room? Decides
    whether the number row is a hazard.
11. **`/props "cdo=UIStrategyMap"`** and `cdo=UIStrategyMapItem_Region` — check
    which localized strings are actually populated, per the
    `UITacticalHUD_InfoPanel` lesson: a `localized` var declared on two classes
    can be empty on one of them, and the tell is a double space in speech.
12. **What is the Avenger's current site, and does the tray hold a region?**
    Sanity-check §4.1's claim that regions only appear while scannable.

---

## 11. Open design questions (for the UI discussion, not decided here)

- **Two surfaces or one?** The tray recipe and the geoscape ring overlap on
  missions and scan sites. Tactical keeps the tile cursor and the object
  scanner separate because one is a place and the other is a list; here both
  are lists. A single ring with the tray as one of its categories is the other
  shape.
- **What does the arrow do?** Step the tray (the game's own axis), or step our
  ring? They cannot both own it.
- **Position words on a sphere.** Region + continent, or miles from the
  Avenger, or both — and does anyone ever need a bearing?
- **The four mouse-only HUD verbs** (Dark Events, Chosen Info, Resistance
  Orders, ResNet toggle) — do they become recipe nodes on the tray surface, or
  do they get letters out of the sixteen we have?
- **Time.** Starting a scan is one key today. But *time passing* is the thing a
  blind player cannot perceive at all: days tick by, events fire, and nothing
  says so. That is a narration problem, not a navigation one, and it may be the
  most valuable single feature in this whole layer.
- **The screens behind the pins** — `UIMission_*`, `UIMakeContact`,
  `UIBuildOutpost`, `UIAdventOperations`, `UIChosenInformation`,
  `UIStrategyPolicy`, `UIAlert` — are a second, larger body of work. `UIAlert`
  in particular fires constantly while time runs and is the geoscape's real
  interrupt surface.

---

## 12. ✅ FIRST LIVE PASS (2026-08-21) — what was measured

Ordinary campaign, Avenger landed at a POI in Africa, harness on :8774.
No build; every result is a harness read plus one synthetic arrow and two
`/call`s. Ten of the twelve §10 checks ran; the two that would change campaign
state (starting a scan, pressing a number key) were deliberately left.

### Confirmed

- **Stack order** — `UIStrategyMap` / `UIFacilityGrid` / `UIAvengerHUD` /
  `UIEventNotices`. §1 corrected: the grid is second, not last.
- **The arrows drive the tray, and the selection sticks.** Baseline
  `SelectedIndex = 0`, `SelectedMapItem = UIStrategyMapItem_POI`. One synthetic
  `Left` → `SelectedIndex = 1`, `SelectedMapItem = UIStrategyMapItem_Mission`
  (exactly icon 4's `MapItem`). Re-read 12 s later: unchanged. §2 and §4.4 both
  hold — the navigator is live on mouse+keyboard and nothing recomputes what it
  sets, because `UpdateSelection` is controller-gated.
- **The tray is fully readable and localized.** `NavigableControls` = 2 icons,
  `HorizontalNavigation = true`, `LoopSelection = true`,
  `OnSelectedIndexChanged → UIStrategyMap.OnNavigationChanged`. Icon `idNum 3`
  → `ScanSite = XComGameState_PointOfInterest`, tooltip
  *"НЕПОВРЕЖДЕННЫЕ СТРОЕНИЯ: АФРИКА"* / *"Новобранцы: 6 дн. осталось"*. Icon
  `idNum 4` → `MissionSite`, tooltip *"СЕКРЕТНЫЙ ОБЪЕКТ «АДВЕНТА»"* /
  *"Западная Африка"*. ⚠ The controls live in `NavigableControls`;
  `NavigationTargets` is empty and is a different list — the ant farm's
  re-arm test used the other one.
- **The mouse-active guard is real.** `/call OnReceiveFocus` on the selected
  icon returned normally and left `bFocused = false`. So no brackets, no
  tooltip refresh, no camera move — our layer must supply all three.
- **Outbound calls work on this screen.** `/call LookAtAvenger arg=0.75`
  returned normally; `XComEarth` then read `v2ViewLoc = {0.500, 0.518}`,
  `fCurrentZoom = fTargetZoom = 1.0`. Earth coordinates are normalized 0..1,
  so `CAMLookAtEarth` gives us the write side and `v2ViewLoc` the read side.
- **The widget tree is 303 panels**: 85 City pins, 22 RegionLinks, 16 Regions,
  15 Havens, 9 Missions, 6 Continents, 3 ResistanceHQ, 3 AdventChosen, plus the
  20 tray icons and the HUD. The City and RegionLink pins carry nothing a
  player wants and would have to be excluded from any ring by class.

### ⛔ Falsified

- **`UIScanButton` as a uniform reader** (§4.2). One of ~20 held real text.
- **The retained-widget strategy for pins** (§4.3, new). Off-screen pins have
  `HistoryIndexOnLastUpdate = -1` and hold class defaults — including the
  literal string `"UNINITIALIZED"` and default-visible buttons. The region
  layer must be read from `XComGameState_WorldRegion`, not from widgets.

### Also noted

- Five tray icons are `visible`, only two are navigable: slots 0-2 hold
  `Faction_unknown` placeholders for the three factions not yet met, which
  `UpdateMissions` deliberately keeps out of the navigator. A sighted player
  therefore *sees* "three factions I haven't met" and our node list does not
  say so. Open parity question, not a bug.
- `UIStrategyMapItem_Mission` really does retain only `Label`
  (*"Секретный объект «Адвента»"*) with every `m_str*` empty, and is the one pin
  that reads `FOCUSED` — its `Show()` override keys off `bIsFocused`.
- The screen's own localized strings are populated:
  `m_strStartTime = "НАЧАТЬ СКАНИРОВАНИЕ:"`,
  `m_strStopTime = "ОСТАНОВИТЬ СКАНИРОВАНИЕ"`,
  `m_strScanDisabled = "ДАННЫЕ ОТСУТСТВУЮТ…"`, `m_strDarkEventsLabel = "СОБЫТИЯ"`.
  So §6's dead `UpdateCenteredNavHelp` strings are available to us as localized
  verb words even though the game never shows them.

### Not yet run

- Check 9 — press `Enter` to start the scan. Mutating: it would run a 6-day
  scan and advance the campaign.
- Check 10 — press `1` on the globe, to see whether it jumps to an Avenger room.
- Check 11 — `/props?cdo=` sweep for empty `localized` strings.
- The event queue and clock (`UIAvengerHUD.EventQueue`) were not read.

---

## 13. The design, as decided (2026-08-21)

Decided with the user after the live pass. This supersedes the sketch in §8.

### 13.0 The organising idea — the sort origin IS the geoscape's cursor

Every other surface in this project has a cursor: tactical has the tile cursor,
the ant farm has the highlight box. The geoscape has **none** for a
mouse+keyboard player, because the only cursor the game owns is the
proximity one and that is controller-gated (§2). A flat list of sites therefore
strips the globe of place — you learn *what* exists and never *where*.

The fix (the user's, 2026-08-21): give the ring a **movable sort origin**.
The ring sorts by distance from a point; that point defaults to the Avenger and
can be moved to any object you have just heard. Then "next nearest" means
something, and the globe has somewhere to stand.

- **Default origin: the Avenger's current position.**
- **`Home`** — make the thing you just heard the new origin. Works from the ring
  and from the tray, on the LAST ANNOUNCED entry, exactly as the tactical object
  scanner's `/` does: it acts on what you just heard, not on an abstract
  "current category".
- **`C`** — origin back to the Avenger. Same key and same meaning as the tile
  cursor's recenter, and it is literally `UIStrategyMap.LookAtAvenger(0.75)`
  plus the origin reset (verified callable, §12).

Rules the idea implies:

- **One origin, shared by every category.** It is a place, not a per-list
  setting. The per-category *cursor* stays per-category, as in the object
  scanner.
- **Moving the origin keeps identity and recomputes ordinals.** Each category's
  cursor is stored as an `ObjectID`, never an index, so after a re-sort you are
  still on the same object — its ordinal simply changed. The object scanner
  already keys this way and for the same reason.
- **Moving the origin moves the camera there**, which is not cosmetic: §4.3
  means an off-screen pin never updates, so parking the camera on the area you
  are studying is what makes those pins readable at all.
- **Announce the origin on every change** — *"Sorting from Western Africa"* /
  *"Sorting from the Avenger"*.
- **A vanished origin falls back to the Avenger and says so.** Missions expire
  and POIs are consumed; silence there would leave the player sorting from a
  ghost.
- **The origin does NOT re-sort the tray.** The tray's order is the game's own
  (faction HQs, then scan sites, then missions) and that order is parity.
- ⛔ **Distance must use `XGGeoscape.GetShortestDist` / `ShortestWrappedDistance`.**
  Earth X wraps at the antimeridian, so a naive subtraction reports a
  neighbouring site as maximally far. `XComEarth.ConvertEarthCoordsToMiles`
  converts to the game's own unit.
- Bearings become meaningful with a movable origin — *"1,400 miles north-east"*
  is a real answer where an absolute bearing on a sphere was not. This reverses
  the doubt recorded in §8.2.

### 13.1 Surface A — the tray, on the arrows

✅ **We claim the arrows** (decided). Rationale beyond ergonomics: bare arrows
pan the globe, and panning drifts pins off screen where they stop updating
(§4.3), so sharing the axis actively degrades our own data. `Ctrl+arrow` still
pans, free, by the leniency rule.

Nodes are the visible tray icons. Movement calls the game's own
`Navigator.SetSelected` / `Prev` / `Next` rather than a private cursor, so
`OnNavigationChanged` keeps firing and `SelectedMapItem` stays consistent —
the "prefer the game's own handlers" rule.

⚠ **Placeholder icons get stops** (decided, against the recommendation). The
three `Faction_unknown` slots are visible on screen but deliberately excluded
from the game's navigator, so our node list is five where the game's is two.
Consequence that must be handled explicitly rather than discovered: **when the
cursor sits on a placeholder we do NOT move the game's navigator**, leaving it
on the last real icon. Otherwise `SelectedMapItem` silently disagrees with what
was last spoken. Placeholders are read-only stops — no activation, no camera
move, and no tooltip exists for them (`SetMissionIconTooltip` early-returns for
an unmet faction), so their wording is ours to invent and will be English until
a localized source is found.

### 13.2 Surface B — the world ring, on PageUp/PageDown

✅ **Separate keys** (decided) — the tactical split between the tile cursor and
the object scanner, transferred whole, so the muscle memory carries over.

- `PageDown` / `PageUp` — next / previous within the current category
- `Ctrl+PageDown` / `Ctrl+PageUp` — next / previous non-empty category, announcing its name
- `O` — re-announce
- `/` — take the camera there
- `Home` / `C` — move / reset the sort origin (§13.0)

Categories are **companion-sourced** — §4.3 forbids reading pins — and every one
of them shares a single filter: the game's own `ShouldBeVisible()`, which is the
predicate `SelectMapItemNearestLocation` uses and therefore the fog-parity rule
for this layer.

Note where the category lines actually fall: two state classes fork on a
condition, and the fork is the meaningful split, not the class.
`XComGameState_MissionSite.GetUIClass` returns `AlienNetworkComponent` when
`MakesDoom()` and `Mission` otherwise; `XComGameState_Haven.GetUIClass` returns
`ResistanceHQ` when `IsFactionHQ()` and `Haven` otherwise.

| Category | Source | Typical | Speaks |
|---|---|---|---|
| `regions` | `XComGameState_WorldRegion` | 17 | name, resistance level, supply income, controlling Chosen, contact/outpost availability, Avenger present |
| `missions` | `MissionSite` `Available && !MakesDoom()` | 1-4 | type, region, expiry, rewards, difficulty — including the parallel Guerrilla Ops the tray collapses to one |
| `facilities` | `MissionSite` `MakesDoom()` | 0-3 | the Avatar-project sites, with `Doom` |
| `scan sites` | `PointOfInterest` + `ResourceCache` + `BlackMarket` | 1-4 | title, reward, days remaining, percent complete |
| `faction HQs` | `Haven` where `IsFactionHQ()` | 0-3 | faction, met or not, influence |
| `continents` | `XComGameState_Continent` | 6 | bonus card, active or not, radio towers *n* of *m* |
| `chosen` | `XComGameState_AdventChosen` | 0-3 | which, territory, met/defeated, knowledge |
| `airships` | `HeadquartersXCom` + `Skyranger` + `UFO` | 1-2 | where the Avenger is and what it is doing |
| `cities` | `XComGameState_City` | 85 | flavour and spatial anchoring — see below, this one is an AID |

Typically 5-7 are non-empty, which is a sane amount for `Ctrl+PageUp`/`PageDown`
to cycle.

**Excluded.** `RegionLink` (22) — but the fact it represents is valuable and
`XComGameState_WorldRegion.LinkedRegions` gives adjacency directly, so it goes
**into the region readout** rather than becoming 22 stops; adjacency is what
governs where contact can expand next. Plain `Haven` (~15, one per region) —
its `ShouldBeVisible()` is true only at `eResLevel_Outpost`, so it means "this
region has a radio tower", which the region's own level already says.
`Bastion` and `TradingPost` — rare, or a duplicate of Black Market.
`CovertAction` — `ShouldBeVisible()` is just `bStarted`, so at most the running
one; folded into `;` instead of given a category.

#### Cities are an AID, not parity — and must be labelled as one

⛔ `XComGameState_City.ShouldBeVisible()` returns **`false` unconditionally**, and
`CanInteract()` likewise. No city pin is ever drawn. The one place city names
were wired to something player-facing —
`X2StrategyElement_DefaultMissionSiteDescriptions:279-291` — is **commented
out**. Firaxis built the layer and switched it off, so a sighted player never
sees a city name anywhere on the geoscape.

Announcing them therefore hands a blind player something no sighted player has.
That is permitted by the parity rule's own second clause — *aids that genuinely
compensate for lost vision stay, labelled as aids, not sold as parity* — and
unlike the cheat-gated WOTC initiative order there is no competitive leak: a
city name is inert geography. But it must be labelled, and it must not be
described as reading the screen.

Two consequences for how it is built:

- ⚠ Because the pin never draws, `UpdateFlyoverText` never runs and `m_strLabel`
  is never set. The name comes from `XComGameState_City.GetDisplayName()`, which
  forks: a real city returns the localized `X2CityTemplate.DisplayName`
  (*"Lagos"* — a true Earth anchor), while `bAdventMadeCity` returns the
  procedural `GeneratedName` (`AdventCityPrefix @ AdventCityNames[rand] @ NN`,
  e.g. *"ADVENT … 07"* — world-building, not an anchor). Half this category
  anchors you geographically and half of it does not.
- **Weave before you categorise.** The primary use is appending *"near \<city\>"*
  to readouts that already name a place — mission stops, scan sites, and above
  all the sort-origin announcement (*"Sorting from Western Africa, near
  Lagos"*). That puts the flavour exactly where it anchors, at no extra stops.
  The category exists for browsing on top of that; 85 entries are tractable only
  because the sort origin (§13.0) means you walk the nearest few.
- `X2CityTemplate` carries `Location` (Vector) and `Region` (name), so each city
  knows where it is and which region owns it.

⚠ **Unmeasured**: what a live city name actually reads as. One
`/props?instance=XComGameState_City` answers it; the game closed before it ran.

### 13.3 Surface C — status, on `;`

Avenger location, scanning or not, days remaining, date and time, doom,
continent support. `;` because in tactical `;` already means *tell me about the
thing I am controlling*.

⚠ The clock must reproduce `UIEventQueue.RefreshDateTime`'s localisation clause
(`GetLocalizedTime(XComHQ.Get2DLocation(), …)` unless `XComHQ.Flying`) or it
will disagree with the screen by hours.

The four verbs vanilla gives the keyboard no route to — Dark Events, Chosen
Info, Resistance Orders, ResNet toggle — hang off this surface. Key assignment
is open; there are sixteen free letters and, per §7, the digits too.

### 13.4 Time narration — events only

✅ **Events only** (decided): scan complete, mission appeared or expired,
council request, Chosen activity. No daily tick, no periodic scan progress.
Scan progress is available on demand through `;`.

### 13.5 Untouched

`Enter` / `Space` stay the scan toggle — they work today and are the one thing
a blind player can already do here. `Esc` stays exit. `Tab` stays exit.

⛔ **This is not a preference, and honouring it cost a code change.** A graph
recipe does not opt into its claims: attaching one claims Up/Down/Left/Right,
`Home`, `End`, `Tab`, `Shift+Tab`, `Enter` and `Space` for the whole layer, and
every other recipe in `menu_navigator.cpp` sits on a screen where that is right.
`UIStrategyMap` is the first one where it is not — read its `OnUnrealCommand`:

```unrealscript
`if(`notdefined(FINAL_RELEASE))
        case FXS_KEY_TAB:
`endif
        case FXS_KEY_ESCAPE:
        case FXS_R_MOUSE_DOWN:
        case FXS_BUTTON_B:
            TryExiting();
            break;

        case FXS_KEY_ENTER:
        case FXS_KEY_SPACEBAR:
            if (XCOMHQ().IsScanningAllowedAtCurrentLocation()) ToggleScan();
```

**`Enter` and `Space` are scoped off** (`attachedNotGeoscape`). Both map to
`ToggleScan()`, and on mouse+keyboard there is no third route to it — only those
two keys and a mouse click on the scan button — so a claim there is not a trade,
it is a removal. Our handlers had nothing to put in their place either: a tray
icon carries no `OnActivate`, because vanilla binds activation to the mouse-click
delegate alone.

✅ **`Tab` IS ours, and §13.7's HUD stop is what earns it.** Two things had to be
true, and the reasoning took a wrong turn on each before landing.

*Is the action behind the key survivable?* Yes. Tab's `case` label falls through
into Escape's body inside `` `if(`notdefined(FINAL_RELEASE)) `` — a debug
convenience Firaxis guarded out of shipping builds — and `Escape`, which this
layer never claims on any screen, already does the same `TryExiting()`. So *"the
game uses Tab, so leave it"* was false.

*Does the layer have parity-legitimate work for it?* ⛔ **Not from the ring or
the status block** — and this is the project's parity rule, not an ergonomic
preference:

> The stop list mirrors what is actually ON SCREEN; anything past that is a
> separately-labelled scan/aid surface and must never hand the player
> information a sighted player cannot get.

The ring enumerates regions, continents and faction HQs that no sighted player
can read off the globe. It is an **aid** — the geoscape's object scanner — so it
lives on `PageUp`/`PageDown` (§13.2) and status lives on `;` (§13.3), both
outside the Tab cycle. *"Tab is free, so take it for Surfaces B and C"* was
therefore also false.

What does earn it is **§6's four mouse-only verbs**, now built as stop 2
(§13.7): Dark Events, Chosen Info, Resistance Orders and the ResNet toggle. Each
is drawn on screen, clickable with a mouse, reachable by no key in the game, and
carries a localized label the screen already owns. That is parity content by any
reading — the same argument that gives the Avenger room's crew line a stop — and
with it Tab has real work.

⚠ **The general test, which both wrong turns missed:** a key is takeable when the
ACTION behind it survives losing it **and** the layer has parity-legitimate work
for it. `Enter`/`Space` → `ToggleScan` fails the first permanently. `Tab` passes
both.

### 13.7 ✅ Stop 2 — the HUD's four verbs (BUILT 2026-08-21, not yet run live)

| Verb | Label | Shown when | Activated by |
|---|---|---|---|
| Dark Events | `DarkEventsButton.Text` (a plain `SetText(m_strDarkEventsLabel)` on mouse+keyboard, so it is RETAINED), falling back to the screen's string | `DarkEventsContainer.bIsVisible` — which `UpdateDarkEvents` sets from `ShowDarkEventsButton()` | `OnDarkEventsClicked(none, FXS_L_MOUSE_UP)` on the SCREEN |
| Chosen Info | `m_srChosenInfo` | `m_kChosenInfoButton.bIsVisible` — `RefreshData` sets it from `AlienHQ.HaveMetAnyChosen() && !IsInFlightMode()` | `UIButton.Click()` |
| Resistance Orders | `m_srResistanceOrders` | `m_kResistanceInfoButton.bIsVisible` — from `ResHQ.NumMonths > 0 && HaveMetAnyFactions()` | `UIButton.Click()` |
| ResNet toggle | `m_strToggleResNet` + on/off | always (a nav-help hotlink, no widget to gate on) | `OnResNetClicked()` |

Every gate is the GAME's own `Show()`/`Hide()` call, read off the widget's visible
bit — the documented child-widget exception to *never gate readiness on
`bIsVisible`*, since here the game sets it deliberately and it does mean hidden.

⛔ **Dark Events must NOT go through `Click()`.** `AddDarkEventsButton` wires its
handler with `ProcessMouseEvents(OnDarkEventsClicked)`, i.e. as an
`OnMouseEventDelegate` — the button has **no** `OnClickedDelegate`, so `Click()`
returns false having done nothing at all. The route used instead is the game's own
keyboard path reproduced verbatim, `none` argument included:
`if (ShowDarkEventsButton() && DarkEventsButton != None) OnDarkEventsClicked(none,
FXS_L_MOUSE_UP);`. The handler ignores the panel and tests only the command, which
must be `FXS_L_MOUSE_UP` (391) or the body does not run. The other two are real
`OnClickedDelegate` buttons and `Click()` self-gates on `!IsDisabled && bIsVisible`,
so the mouse path's guard comes along free — and both handlers check the screen
stack before pushing, which is what stops a double press stacking two copies.

The ResNet toggle reports its state (`m_bResNetForcedOn`) because a sighted player
reads it off the globe redrawing itself; the label names the action but not which
way it is set.

## 14. ✅ SECOND LIVE PASS (2026-08-21) — Surfaces A and the HUD stop, running

Same campaign, harness on :8774, the built layer attached
(`navigator: attached (geoscape tray)`). Stack unchanged from §12.

### Confirmed

- **The tray reads, and it is the game's own words.** Five stops, positions
  stamped: *"НЕПОВРЕЖДЕННЫЕ СТРОЕНИЯ: АФРИКА, button, Новобранцы: 4 дн.
  осталось, 4 of 5"* and *"СЕКРЕТНЫЙ ОБЪЕКТ «АДВЕНТА», button, Западная Африка,
  5 of 5"*.
- ✅ **The announce-time re-read works, and it MATTERS.** The same POI stop read
  *"4 дн. осталось"* on one visit and *"2 дн. осталось"* later in the same
  session. That is `OnMouseEvent(FXS_L_MOUSE_IN)`'s
  `ScanSite = XCOMHISTORY.GetGameStateForObjectID(...)` refresh landing —
  Firaxis' *"Force an update of Scan Site game state"* — and it is the whole
  argument for reading at announce time rather than closing over the build.
  A captured string would still be saying "4 days".
- ✅ **Tab reaches the HUD stop and the ResNet toggle reads its state**:
  *"ВКЛ./ВЫКЛ. СЕТЬ СОПРОТИВЛЕНИЯ, button, off"*. One verb only, which is
  correct for this campaign — no Chosen met, no factions met, no dark events —
  so the per-verb visibility gates are doing their job rather than showing four
  buttons unconditionally.
- ✅ **§13.7's three unverified localized strings are all populated**:
  `m_srResistanceOrders = "ПРИКАЗЫ СОПРОТИВЛЕНИЮ"`,
  `m_srChosenInfo = "ИНФОРМАЦИЯ ОБ ИЗБРАННОМ"`, and `m_strToggleResNet` reached
  speech. No double space anywhere in the ring, so no `UITacticalHUD_InfoPanel`
  trap here.

### ⛔ Bug found and fixed: the three faction slots spoke as bare `"button, N of 5"`

The player's report — *five buttons, three with no labels* — was exact. §13.1's
placeholder test was wrong, and **both of its clauses were false** of the thing
it was meant to catch. Measured on the slot at `0x…5F2EC800`:

| Field | Expected by §13.1 | Actually |
|---|---|---|
| `ScanSite` | none | **`XComGameState_Haven`** — set |
| in `NavigableControls` | no | **yes** |
| `ToolTip` | — | **null** |
| `ImagePath` | — | `img:///UILibrary_XPACK_StrategyImages.Faction_unknown` |

So `navIndex < 0 && !scanSite && !mission` never fired, the icon fell through to
the tooltip branch, and with `ToolTip` null both announcements resolved empty —
leaving the control type's role word and the position as the entire readout.
§12's *"only two are navigable"* was a correct reading of a DIFFERENT campaign
state and does not generalize.

⛔ **The test is the ART.** `ImagePath` contains `Faction_unknown` — the same
fact a sighted player reads off the screen, and unlike *"has no tooltip"* it
cannot be confused with an icon whose tooltip has merely not been spawned yet
(`SetMissionIconTooltip` creates it lazily).

Three further corrections came out of the same fix:

- **Selection and hover are INDEPENDENT.** The first version skipped both for a
  placeholder. But a placeholder *is* navigable, so the game's navigator must
  still follow it or `SelectedMapItem` disagrees with what was spoken; only the
  camera hover is skipped, because `OnMouseEvent` early-returns on an unmet
  faction and the game would not move the camera either.
- **Never silence.** Any icon whose tooltip is empty now falls back to the pin's
  `MapPin_Header` — a `CPF_Localized` **class default**, so it is generic, always
  populated, and uniquely exempt from §4.3's staleness rule (every instance of
  the pin class holds the same value, framed or not).
- **Parity forbids naming the faction.** The placeholder reads the pin's generic
  noun (*"ШТАБ СОПРОТИВЛЕНИЯ"*) plus our *"not yet met"*. The icon deliberately
  identifies no faction and the game gives the slot no tooltip at all, so a
  sighted player hovering it learns nothing — the slot ORDER (Reaper,
  Skirmisher, Templar) would leak what the art withholds.

### ⛔ Second bug: pressing `Enter` on a POI did nothing — no way to fly anywhere

Reported by the player mid-session, and it is §13.5's "open gap" turning out to
be a blocker rather than a nicety: with `Enter` left to the game it reaches
`if (XCOMHQ().IsScanningAllowedAtCurrentLocation()) ToggleScan();`, which is
FALSE unless the Avenger is already parked on a scannable site. So on every tray
stop that is somewhere else — i.e. most of them, and all the ones you would want
to travel to — `Enter` was a dead key. **The tray was read-only and the layer
could not send the Avenger anywhere.**

✅ **Fixed by taking `Enter`, at no cost.** The chain the game already has, and
never wired to a key on mouse+keyboard:

```
UIStrategyMap.OnUnrealCommand(FXS_BUTTON_A, RELEASE)
  -> Haven special case: ToggleScan when this IS the site the Avenger sits on
  -> SelectedMapItem.OnUnrealCommand(FXS_BUTTON_A)
       -> GeoscapeEntity.AttemptSelectionCheckInterruption()
            -> CanInteract() && !DisplayInterruptionPopup()
                 -> AttemptSelection -> ConfirmSelection
                      -> XComHQ.SetPendingPointOfTravel(entity)   // the Avenger flies
```

`FXS_BUTTON_A` (300) is the controller accept button and the ONLY entry to any of
it — §6.1 called this correctly and the live session confirmed the consequence.

Three details that make the fix the game's rather than ours:

- **Send it to the SCREEN, not to the pin.** Going straight to the pin skips the
  Haven scan-toggle case; and `UIStrategyMapItem_Region`'s own override — which
  picks between Make Contact, Build Outpost, Scan and plain selection in the
  game's priority order — is reached through the screen anyway.
- **It acts on `SelectedMapItem`, never on an icon we pass in.** That is exactly
  why focus write-back must move the game's navigator first, and it is also what
  makes the placeholder safe: selection lands on an unmet faction's Haven, whose
  `CanInteract()` refuses, precisely as it would for a controller player.
- **RELEASE phase (32).** Both handlers open with
  `CheckInputIsReleaseOrDirectionRepeat` and drop anything else.

**`Space` keeps `ToggleScan`**, so scanning never loses a key — the pair mapped to
the same function is what made taking one of them free. `attachedNotGeoscape` now
guards Space alone.

### Still not run

Camera follow (speech cannot show it), `Ctrl+arrow` panning, Dark Events actually
opening the operations board, the detach-restores-camera path, and the new
`Enter` → travel path end to end.

⚠ **The consequence is an open gap, not a solved problem: the tray has no
activation verb.** `OnClickedDelegate` is bound to
`<site>.AttemptSelectionCheckInterruption`, and it is reachable by mouse click
and by controller `FXS_BUTTON_A` — never by a key on mouse+keyboard. So vanilla
cannot activate a tray icon from the keyboard either; our reader inherits that
and is read-only. Taking `Enter` for it would trade the geoscape's primary verb
for its secondary one, which is the wrong way round. It needs a key of its own,
and that is a decision for the next design pass — §7's budget has room.

### 13.6 ✅ SURFACE A IS BUILT (2026-08-21) — not yet run live

`ReadGeoscapeGate` / `FindStrategyMapScreen` / `ReadStrategyMap` /
`SetTraySelection` / `MoveTrayHover` in `src/game_ui.cpp`, the
`geoscape tray` recipe in `src/menu_navigator.cpp`, fifteen offsets pinned in
`src/sdk_layout_checks.cpp`. Builds clean; kernel tests pass.

Two things the build decided that recon had left open:

**The camera, the brackets and the tooltip come from ONE call, and it is the
game's own.** Rather than reproducing `CAMSaveCurrentLocation` +
`CAMLookAtEarth` ourselves, focus write-back hands the icon
`OnMouseEvent(FXS_L_MOUSE_IN)` — which does the camera pair, `MapItem.OnMouseIn()`
for the pin highlight, AND `SetMissionIconTooltip` from the state object. That
last is worth more than it looks: it re-reads the scan site out of the history
first (*"Force an update of Scan Site game state"*, Firaxis' own comment), so a
days-remaining count that has ticked since `UpdateMissions` last ran is current
by the time we speak it. The camera half is gated on
`bMoveCamera || `ISCONTROLLERACTIVE == false`, and on mouse+keyboard the second
term is already true, so none of it needs a flag set.

⛔ **Call the pair symmetrically.** `MOUSE_IN` saves the camera location and sets
`bOverrideZoomHide`; `MOUSE_OUT` restores and clears both. An unmatched `IN`
strands the pin in its hover state and overwrites the saved location with a site
the player never asked to return to — so every move sends `OUT` to the icon being
left first, and `Detach()` sends a final `OUT`. ⛔ And never on a placeholder:
with both `ScanSite` and `MissionSite` none, `OnMouseEvent`'s final `else`
dereferences `MissionSite.Get2DLocation()`. The unmet-faction slot is saved from
that only by the `Haven` early-return at the top, which is not a guarantee worth
leaning on.

**Placeholders are told apart by the game's own filter.** `UpdateMissions` calls
`Navigator.AddControl` for exactly the icons it wants reachable, so *"visible but
absent from `NavigableControls`"* IS the definition of a placeholder — no test of
ours, and nothing to keep in step with the game. Nodes are keyed by the pin's
`GeoscapeEntityRef.ObjectID` (stable over the campaign history) rather than by
slot, which is not, and placeholders fall back to slot since they have no entity.

What the first live pass must answer, in order: that the tray reads and speaks at
all; that `Left`/`Right` move it and the camera follows; that `Ctrl+arrow` still
pans (the arbiter's down-claim latch should already guarantee the release reaches
`m_fKeyPanX = 0`, so a stuck spin would mean that latch is wrong); that `Enter`
still scans and `Tab` still exits; and that leaving the screen does not park the
camera on the last site read.
