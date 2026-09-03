# Monthly council report + its Chosen page — recon

Measured from `UIResistanceReport.uc`, `UIResistanceReport_ChosenEvents.uc`,
`UIResistanceReport_FactionEvents.uc`, `XComHQPresentationLayer.uc` and the generated SDK's layout
dump, 2026-09-03; built the same day from a live screenshot of the Chosen page ("ДЕЙСТВИЯ
ИЗБРАННЫХ") that the mod was silent on. **Nothing here has run live yet** — the checklist is §6 and
the debt entry is in `next-steps.md`.

## 1. The chain

At the end of every month the strategy layer shows, in order:

| Screen | Pushed by | What it shows | Closes into |
|---|---|---|---|
| `UIResistanceReport` | `XComHQPresentationLayer.UIResistanceReport()` | the council's report card: date, supply income or loss, bonus / dark-event modifiers, the resistance and alien activity lists, Avatar progress | its own `CloseScreen` calls `UIResistanceReport_ChosenEvents()` |
| `UIResistanceReport_ChosenEvents` | the report's close (also `bSimpleView` from elsewhere) | one column per Chosen — met or `UNKNOWN CHOSEN` — with the knowledge meter, a new knowledge level, last month's activities, this month's planned action; a one-time knowledge warning popup | its `CloseScreen` calls `UIAdventOperations(true)` (the popup recipe's, headings only) |
| `UIResistanceReport_FactionEvents` | nothing — `UIResistanceReport_FactionEvents()` on the presentation layer has **no caller** in the shipped sources | an empty `BuildScreen()` | — (dead in WOTC) |
| `UIRewardsRecap` | `UIRewardsRecap()` from the loot / research-available screens | ⬜ still owed (`ui-inventory.md`) | — |

Both live screens answer **Enter only** (`FXS_BUTTON_A` / `FXS_KEY_ENTER` → `OnContinue`;
`FXS_BUTTON_B` returns false) and route Escape nowhere. Both extend `UIX2SimpleScreen`.

## 2. What is on screen, and where each word comes from

### 2.1 `UIResistanceReport`

`BuildScreen`, in push order — every value off a screen function or the two HQ states the base
class hands it (`RESHQ()`, `ALIENHQ()`):

| On screen | Source |
|---|---|
| `RESISTANCE MONTHLY REPORT` / the date / `SECURE TRANSMISSION` | `m_strReportTitle`, `GetDateString()`, `m_strResistanceReportGreeble` |
| `Monthly Supply Income:` or `Monthly Supply Loss:` + `+§N` | sign of `RESHQ().GetSuppliesReward(true)`; `GetSupplyRewardString()` |
| `Popular Support Bonus` + `+N%` | only when `SupplyDropPercentIncrease > 0` |
| `DARK EVENT PENALTY:` + `-N%` | only when `SavedSupplyDropPercentDecrease > 0` (the saved copy: dark events deactivate before the report shows) |
| `RESISTANCE ACTIVITY:` + "Title N, Title N and Title N" | `GetMonthlyActivity()`, each entry `GetColoredText(Title @ Count, Rating)`, comma-joined, then `FormatCommaSeparatedNouns` |
| `ALIEN ACTIVITY:` + same | `GetMonthlyActivity(true)` |
| `AVATAR PROGRESS` + pips | only once `ALIENHQ().bHasSeenDoomMeter`; pips = `GetCurrentDoom()` |
| `NEW STAFF AVAILABLE:` block | pushed with **five empty strings** — the filling code is commented out in the shipped source. Nothing to read |

### 2.2 `UIResistanceReport_ChosenEvents`

`RealizeColumn(ChosenState, ActionState, idx)` per entry of `ChosenRefs` (assassin, warlock, hunter
order via `ChosenOrder`, then any others), in push order:

| Operand | Spoken? | Source |
|---|---|---|
| `UNKNOWN CHOSEN` or `""` | yes — for an unmet Chosen the column shows this alone | `bMetXCom ? "" : UnknownChosenLabel` |
| icon, image | no — art | |
| `Level + 1` | **no** — a bare digit in a badge, no label anywhere on the page | |
| class, name, `'nickname'` | yes | `GetChosenClassName()`, `GetChosenName()`, `GetChosenNickname()` (the quotes are the screen's own decoration and are not reproduced) |
| knowledge before → after | yes, both ends when they differ | `GetKnowledgePercent()`, `GetKnowledgePercent(true)`; heading `KnowledgeLabel` |
| last month's activities | yes, one line each | `GetMonthActivitiesList()` — the `PrevMonthActivitiesHeader` (ends in a newline) followed by `-item` lines joined with `\n`; split back on the newline, leading `-` dropped |
| `DEFEATED` | yes, when `bDefeated` | `DefeatedChosenLabel` |
| action name, summary, quote | yes | `ActionState.GetDisplayName()` / `GetSummaryText()` / `GetQuote()` under `ActionHeader` |
| `NEW KNOWLEDGE LEVEL: <title>` + up to three bullets | yes, when `bJustIncreasedKnowledgeLevel` | `KnowledgeGainPrefix @ KnowledgeGainTitle[level]`, `KnowledgeGain{First,Second,Third}Bullet[level]` — the second and third ship empty for every level |

The **warning popup** (`SetChosenWarningPopup`: `KnowledgePopupTitle` / `Text` / `ButtonTitle`) is
drawn once per campaign — the first month a Chosen gains a knowledge level, while
`!ALIENHQ().bSeenChosenKnowledgeGain`. While `bShowChosenWarningPopup` is set the screen's Enter
goes to `OnPopupButton` (records the flag, closes the popup) and NOT to `OnContinue`; afterwards the
same key closes the screen.

`UIChosenLevelUp` popups: when any Chosen `bJustLeveledUp` this month, `OnInit` pushes one
`UIChosenLevelUp` per Chosen over this screen BEFORE realizing the columns; the popup recipe reads
those, and `OnReceiveFocus` realizes the columns once they are dismissed.

## 3. Native reads

| Class | Field | Offset | Used for |
|---|---|---|---|
| `UIResistanceReport_ChosenEvents` | `ChosenRefs` | `0x630` | column count |
| | flag dword (`bSimpleView` 0x1, `bShowChosenWarningPopup` 0x2) | `0x650` | the warning state — the wire-cache discriminator, since dismissing the popup changes the graph without respawning a widget |

`UIResistanceReport` needs no offsets: finder only. Companion: `X2AccessReportReader`
(`DescribeResistanceReport`) and `X2AccessChosenReader.DescribeChosenEvents`.

## 4. The recipes

**`monthly report`**: one document stop (positions suppressed) with one node per card line —
label then value — and Continue (the screen's own Enter, which pushes the Chosen page). Screen
name: the title with the date as its subtitle.

**`chosen activity`**: while the warning is up, a `warning` stop first — title, text, the popup's
own ACCEPT button on Enter — and **no Continue** (the screen's Enter would dismiss the popup, and a
node must do what it says); then one stop per column — the unmet phrase alone, or identity (+
`DEFEATED`), `KNOWLEDGE: N percent` / `M percent to N percent`, the new-level title and bullets,
the activities heading and lines, then the planned action under the game's `PLANNED ACTIVITY`
heading as a context (name with summary; Space re-speaks the summary; the quote as its own line) —
then Continue. Screen name: `CHOSEN ACTIVITY`.

## 5. Parity rulings

- The Chosen `Level` badge is a bare digit with no label on this page (as on the dossier) — not
  spoken.
- The unmet column is one node saying exactly what the column draws; the screen pushes the met
  Chosen's data anyway and the movie hides it, so none of it is spoken.
- The `'nickname'` quotes and the activity `-` bullets are typography, not words.
- The staff block is pushed empty by the game; reading nothing there is the game's answer.
- The Avatar meter is pips with a heading; "N out of M" is ours, keyed on `GetCurrentDoom()` /
  `GetMaxDoom()`.

## 6. First live pass — checklist

1. Month end: the report must attach with its title and date as the name; the supply line must
   carry the same signed figure the card shows; the activity lists must read as sentences with the
   game's own serial comma (no `<font` markup in the ring — CleanText strips the colouring).
2. Enter on Continue must land on the Chosen page with `CHOSEN ACTIVITY` spoken and one stop per
   column; the unmet column must say only `UNKNOWN CHOSEN`.
3. On a month where a Chosen gained a knowledge level: the new-level line and its first bullet
   (the second and third ship empty), and the knowledge line as "M percent to N percent". On the
   FIRST such month of a campaign, the warning stop must lead, its ACCEPT must dismiss the popup
   (the graph re-reads with the Continue now present), and nothing may say "Continue" before that.
4. On a month where a Chosen levelled up: the level-up popups (popup recipe) come first and the
   columns must read once they are dismissed.
5. `grep -n 'img:///\|UILibrary' ` over the ring must find nothing (the icon and action image
   are never sent).
