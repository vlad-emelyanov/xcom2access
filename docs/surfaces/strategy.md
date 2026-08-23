# Strategy and transient surfaces

Implementation rules for the globe, the screens between missions, and everything that appears then vanishes. Each ⛔ was earned by a live bug; each ⚠ is a
known limit or an open decision. **Read the section for a surface before editing
it** — `CLAUDE.md`'s Surfaces table points here for exactly that reason.

Cross-cutting rules (buffer sizing, the screen-accept trap, the empty-field trap,
speech joining, parity) live in `CLAUDE.md`; only what is specific to these
screens is here.

## Geoscape tray

The globe's pins are not the ant farm's one-call ungate — selection is per-frame and
`ISCONTROLLERACTIVE`-gated — but the TRAY along the bottom already answers the arrows on
mouse+keyboard, and what the mouse-active guard withholds is only the visible half:
`MissionIcon.OnReceiveFocus` early-returns, so there are no brackets, no camera fly-to and no
tooltip refresh. The recipe supplies those three and the voice; every word but one is the game's
own hover tooltip.

- ⛔ **Hover is moved as a symmetric IN/OUT pair**, because IN saves the camera location and OUT
  restores it — an unmatched IN overwrites the saved location with a site the player never asked
  to return to.
- Claiming the arrows costs nothing back (Ctrl+arrow still pans, by the leniency rule) and buys
  correctness: panning scrolls a pin off screen, `UpdateVisuals` gates on `bOnScreen`, and the
  reader would otherwise describe class defaults.
- Beside it a ring on its own keys — PageUp/Dn, Ctrl+them for the category, `O`, `Home`, `C`, `/`
  — an AID rather than parity, because it reads the fourteen regions the tray never holds.

## Skyranger briefing

Not a movie and not a progress dialog but a real `UIScreen` on the transition map, where the
presentation layer is destroyed and rebuilt — which is why "did we find it" earns its own
`/status` line rather than being left to the stack dump. The one edge that matters, the map
finishing loading, is published by the screen's OWN 1 s timer, so the 250 ms poll resolves it four
times finer than the game does.

⛔ `gAnnounced` is a POINTER, not a bool: mission-start and mission-end briefings are different
objects, so a changed pointer is exactly "a new briefing to read".

## Modals, alerts, and the transient family

One recipe for every modal notification in the game — `UIAlert`'s 118 variants, the bespoke
faction/bond/Chosen/UFO popups, the blocking progress dialog — which look nothing alike and share
exactly what a reader needs: topmost on the stack, Enter and Escape answered at SCREEN level. Two
stops, and the message is one node per LINE, because a Chosen reveal is several paragraphs and
speech is serial.

- ⛔ **Activation is the screen's own `OnUnrealCommand` and never a button `Click`**: the buttons
  are spawned with empty labels and get their text through Flash, and several alert names branch
  before dispatching (`eAlert_Contact` refuses when contact is unaffordable,
  `eAlert_TrainingComplete` routes Enter to cancel), so sending the key keeps every branch a poke
  would lose.
- The transient half is the unglamorous file with the highest value in it: `UINarrativeCommLink`
  is how essentially ALL story dialogue in XCOM 2 is delivered, and without it a blind player gets
  the voice acting and nothing else — nothing at all in a language whose VO they do not have.
  Toasts and the strategy help blade ride along. **No graph recipe**: a tab stop over a line about
  to vanish is worse than useless, and attaching would take the arrows from the screen the player
  is working on.
- ⛔ **Change detection is deliberately NOT in native** — all three surfaces re-render continuously
  (the commlink retypes its subtitle as the line scrolls), so "what is on screen" reads true for
  hundreds of consecutive polls and only the model can turn that level into an edge.
- **Tick order is load-bearing in one place**: `Dropship` runs after `TransientReader`, so the
  arrival block queues behind Central rather than over him.

## Squad select and armory loadout

- ⛔ **A field looked up in the wrong place answers `""`, and an empty answer is indistinguishable
  from "the game is not showing this"** — the ambiguity the drop-empties wire format is built on,
  and four of squad select's five first-run defects were that one bug in different clothes. The
  four screen-wide labels (edit, dismiss, promote, select-unit) are class defaults, identical for
  every card, so the companion sends them once on the MISSION record; asking the CARD for them
  yields `""`, the build skips an unlabelled node, and whole controls silently cease to exist.
- ⛔ **A repeating field must never be dropped when empty.** `util` is indexed by list position, so
  a locked slot in front of an occupied one makes the occupied one answer for the pocket in front
  of it and pushes the last slot off the end into the fallback.
- ⛔ **`UISquadSelect_ListItem.SlotIndex` IS NOT UNIQUE, so it cannot be the key that joins the two
  halves' records.** `UpdateData` returns at `if(bDisabled) return;` exactly one line before
  `SlotIndex = Index != -1 ? Index : SlotIndex`, so a locked "increase squad size" card is never
  given one and keeps the default **0**. Live 2026-08-23 a six-card strip read `0,1,0,2,3,0`: both
  locked cards collided with the genuine squad-slot-0 soldier, who matched the FIRST record and
  announced herself as *"увеличьте размер отряда в партизанской школе"* for the whole screen — and
  because the locked record carries no `weapon`/`util` fields, every one of her controls whose
  words come from the card record lost its label and was skipped, leaving her card three nodes
  deep instead of six. Both halves now key on the card's POSITION in `m_kSlotList`, the one index
  they walk in the same order. The generalisation outranks the "never by push order" rule it
  replaces: **a field both halves read off the same object is a key only if the game guarantees it
  is distinct — and a field the game sometimes never writes cannot be.** The list position is
  still the right choice over a running counter, because the companion's `Card == none` skip must
  leave a visible HOLE rather than shift every later card onto the wrong soldier.
- ⛔ **"Is this card empty" is a question only the unit ref can answer, never the widget tree.** An
  unmanned card keeps every widget it was built with — the `ВЫБРАТЬ БОЙЦА` card still owned a
  two-item `UtilitySlots` list under a hidden parent — so a `controls.empty()` test never fired,
  the card was built as an ordinary one, and the node the player landed on was **Edit**.
  `OnClickedEditUnitButton` guards its whole body with `if( XComHQ.Squad[...].ObjectID > 0 )`, so
  on an empty slot it fell through and did nothing: no picker, no negative sound, **no way to put
  a soldier on the mission**. The verb that opens `UIPersonnel_SquadSelect` is
  `OnClickedSelectUnitButton`, reachable in vanilla only from `selectUnitButtonMC` and from
  `HandleButtonSelect`'s own `if(ListItemIsEmpty())` branch — a special case we did not inherit,
  because activating through `HandleButtonSelect` is deliberately refused (it dispatches on the
  game's `m_eActiveButton` rather than ours). `SquadButton::SelectUnit` names it; it is **not** an
  `ESquadSelectButton` member and is blocked from reaching `SetSquadSelectButtonFocus`.
- **Join after cleaning, never before**: rank, name, nickname and class pre-joined in the companion
  left a separator behind when `CleanText` reduced a nickname to nothing. They are four fields,
  joined after each is cleaned. Header labels come from `UISquadSelectMissionInfo`'s own strings —
  English constants against localized values read as a mixed-language sentence.
- **An icon in its unlit state gets no words**: every card ended in the bond icon's title for the
  one state where `UpdateData` leaves it at `SetBondLevel(0)` with `AnimateCohesion(false)` —
  unlit, and passed straight over by a sighted eye.
- **Arrows follow the screen's geometry, not the builder's automatic single-item chain**, which ran
  Down out of the bottom of one card into the top of the next. Cards sit side by side and each
  stacks its controls vertically, so Up/Down walks one soldier's card and Left/Right steps to the
  soldier beside them at the same height. Every edge is wired explicitly, and a direction with
  nowhere to go points at ITSELF — `KeyGraph::Move` treats `dest == node` as a wall, which is how a
  card's top and bottom stop without wrapping. Cards are ragged (a Reaper has one utility slot
  where a Grenadier has three), so Left/Right clamps to the neighbour's last control.
- The loadout screen exists for verbs a keyboard cannot reach at all: strip items, weapons and gear
  are dispatched from mouse handlers and controller trigger codes, and unequip is the row's X
  button.
