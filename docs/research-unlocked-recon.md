# New research available — `UIResearchUnlocked` recon

Survey date 2026-08-31, and unlike most recons this one was measured on a LIVE instance first:
the game was sitting on the screen (two techs unlocked after a mission) while `/props`,
`/flashqueue` and the SDK's `SrcOrig` were read side by side. `docs/ui-inventory.md` lists it
P2; it is the post-mission screen between the loot recap and the rewards recap, so a blind
player meets it after every mission that unlocked anything.

## 1. What the screen is

`class UIResearchUnlocked extends UIScreen` (`UIResearchUnlocked.uc:10`), package
`/ package/gfxResearchUnlocked/ResearchUnlocked`, always on the **3D movie**. Tygan stands on
the armory dais with a DECK of research cards — one per newly available tech — showing one card
at a time, the edges of the rest visible behind it. Continue flips to the next card; after the
last one the screen closes itself.

**It spawns no widgets.** Live `ChildPanels` holds exactly one entry: the screen itself.
`InitScreen` spawns a `UIPawnMgr` and fires two remote events; everything visible is Flash.

### Entry point — exactly one

`UIInventory_LootRecovered.CloseScreen()` (`:319-322`): after the loot recap pops, if
`UnlockedTechs.Length > 0`, `XComHQPresentationLayer.UIResearchUnlocked(UnlockedTechs)`
(`:1846`) pushes this screen (guarded by `IsNotInStack`) and calls `PopulateData` immediately
after `Push` — so the Flash pushes land on the very next `ProcessQueuedCommands` frame, well
before the navigator's settle window ends.

`CloseScreen` (`:176-197`) routes onward to `UIRewardsRecap` (tutorial-objective path) or
`ExitPostMissionSequence`. In the TUTORIAL campaign the same screen runs the same way — the
whole read is screen-side (capture + the screen's own two ints), so the read-the-screen rule is
satisfied by construction here.

### Input contract — every accept key ADVANCES

`OnUnrealCommand` (`:140-166`): `A` / `Enter` / `B` / `Escape` / `RMB` (and Tab in non-final
builds) all call `NextResearch()`, which tells Flash to flip and bumps `CurrentTechIndex` as a
safety net. **Escape does not cancel and nothing pauses on keyboard** (`FXS_BUTTON_START` is the
controller's pause; keyboard Escape is consumed as continue). The switch falls through to
`return true`, so the screen swallows everything else — arrows steer nothing.

When Flash runs out of cards it sends `OnCommand("NoMoreResearch")` → `CloseScreen`. The
`CurrentTechIndex > NumUnlockedTechs` check is a fallback only, which means the index can sit
at `NumUnlockedTechs` (one past the last card) for the frames between the last flip and the
close — a reader must clamp.

## 2. What is on it — one `addResearch` push per tech

`PopulateData` (`:61-101`) loops over the techs and calls `AddResearch` (`:199-208`) once per
card — `MC.BeginFunctionOp("addResearch")`, five operands, `EndOp` — then `realize` (no string
operands, so the capture legitimately does not keep it):

| # | Arg | Source | Live example |
|---|---|---|---|
| 0 | header | `m_strTitle` (localized, same every card) | "ДОСТУПНО ИССЛЕДОВАНИЕ" |
| 1 | title | `TechState.GetDisplayName()` | "Вскрытие мутона" |
| 2 | image | `TechState.GetImage()` — asset path, **skipped by POSITION, never spoken** | `img:///UILibrary_StrategyImages…` |
| 3 | description | `TechState.GetSummary()` | ~200-500 chars, one paragraph typically |
| 4 | shadow | `bShadowProject` — a BOOLEAN, so `StringArgs()` keeps an empty slot for it | `false` |

Missing-name/description fallbacks are expanded by the game itself (`m_strMissingTechName`)
before the push — nothing reaches us with markup.

✅ **Measured live**: cache index 21 (the screen's own `MC` — `PanelCacheIndex(screen)`
matches), holding the two `addResearch` calls in tech order plus one `addTweenBetween`
(an alpha animation, also pushed through the screen's MC). **Match by NAME, not by "calls on
this index".**

## 3. What the screen retains — two ints, and they are exactly what the capture lacks

The capture knows every card; the screen knows which one is SHOWING:

| Offset | Type | Name | Live |
|---|---|---|---|
| 0x0460 | int | `CurrentTechIndex` | 0 |
| 0x0464 | int | `NumUnlockedTechs` | 2 |

Both confirmed against the generated SDK (`XComGame_classes.hpp:79376`) and the running class —
identical. Pinned in `sdk_layout_checks.cpp`. The localized vars (`m_strTitle` 0x0410 and the
two missing-tech fallbacks) also read populated live, but the header already rides in every
card's payload, so no string property is read.

`UnlockedTechs` itself is a PARAMETER of `PopulateData` and is stored nowhere — without the
capture the words would need a history re-derivation, the same dead end the research report
recon walked through (`research-report-recon.md` §3).

## 4. Shape — one card stop keyed by ordinal, one continue stop

A graph recipe (nothing to destroy: no navigable widget, no focus model, arrows dead), built
like the research report with three deliberate differences:

- **Only the CURRENT card is built.** Sighted parity: one card is readable at a time; the deck
  behind shows only as edges. The card's title node carries the deck fact instead — a
  hand-stamped `PositionIndex/Count` (`current+1` of `NumUnlockedTechs`, only when the deck
  holds more than one), which reads through the kernel's own `PositionText` hook. The AUTOMATIC
  stamp cannot say this: it counts nodes within the stop (title + description lines), not cards
  in the deck, and it is suppressed for document lines anyway.
- **Every node id carries the card ordinal** (`unlocked/<index>/…`), the continue node
  included. A flip then invalidates every identity at once, `Reconcile` falls through to the
  start node, and the differ speaks the new card's title — the flip announcement costs nothing.
  ⚠ Keying the continue node per-SCREEN instead would keep focus parked on it across the flip
  and the new card would go entirely unspoken.
- **Every node activates**, not just the continue button. The game's own contract is "any
  accept key flips" (§1), and a player flipping through a deck should not walk back down to the
  button after every card. Enter anywhere sends `kCmdEnter` to the screen; there is nothing
  else Enter could mean here.

Header = SCREEN NAME (spoken once on attach), not a node — the research report's rule.
Description split on `\n` before `CleanText`, empties dropped after the positional read.
`ShadowProject` (arg 4) is NOT spoken — see §5.

## 5. What the first live pass must answer

1. **The flip announcement.** Press Continue on card 1 of ≥2: the differ should speak card 2's
   title (with "2 of N") within a poll tick. If focus was mid-description, same thing.
2. **The close.** Continue on the last card: Flash sends `NoMoreResearch`, the screen pops —
   confirm detach is clean and note what attaches next. `UIRewardsRecap` is ⬜ uncovered, so on
   the tutorial-objective path the player walks from a covered screen into silence; that is the
   next hole in this chain, not a defect here.
3. **A shadow project card** (`bShadowProject` true — WOTC Chosen research). Does Flash restyle
   or relabel the card in a way arg 0 does not carry? Screenshot beside `/flashqueue`. Until
   answered, the bool is captured but unspoken — speaking a word the screen does not show would
   need the game's own localized term anyway, never an English built-in.
4. **`CurrentTechIndex` vs the capture disagreeing** (count mismatch is logged): only plausible
   if a mod pushes extra cards or re-populates; confirm the log stays quiet in vanilla.

## 6. A find from the same session: the harness display was clipping the capture

`/flashqueue` printed the second card's description cut mid-word (a `�` at byte 512): every
operand line in `FlashQueueText` went through a fixed `char buf[512]`. The CAPTURE was whole —
`flash_shadow.cpp` sizes each string from its own length — but the display cap was silent, and
it very nearly read as "the capture truncates". That is CLAUDE.md's size-the-buffer rule biting
a diagnostic surface: fixed in the same change (string operands now print unclipped).
