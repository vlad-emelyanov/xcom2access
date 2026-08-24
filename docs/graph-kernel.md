# The Graph a11y kernel — sheets, and why a table is not a list

`src/Graph/` is the engine-neutral navigation core (Factorio Access → … → RTAccess → CyberAccess
lineage), STL-only by contract. `CLAUDE.md` carries the contract and the two rules a host must obey;
this file is the design behind `GraphSheet`, kept out of the rulebook because it explains a
mechanism rather than forbidding a mistake.

## Sheets

The rule as `CLAUDE.md` states it, with the mechanism that makes it work:

  - ⛔ **A TABLE IS NOT A LIST, and the kernel has a `GraphSheet` for the
    difference** (host vocabulary lifted into a `SheetConfig` so it lives inside
    the kernel rather than beside it). A list row flattens into a sentence losing
    nothing. A table row is several facts under headings drawn ONCE at the top,
    and speech has three problems a sighted reader does not: a cell read alone is
    a value with no idea what it means; reading the whole row every time makes
    every arrow press a paragraph on a twenty-soldier roster; and focus snapping
    back to the first cell makes a COLUMN impossible to read down. The sheet
    answers all three with graph primitives and no announcer changes — the column
    header is the **Left/Right edge label**, the row name is the **Up/Down edge
    label** into a non-primary cell, and the whole-row readout is the **primary's
    own extra announcement parts**, so column 0 IS the flat list and the cells to
    its right are the same facts made addressable. Two fields carry it:
    `NodeVtable::Column` plus `KeyGraph::SlideToColumn`, which restores your
    column after a NON-directional jump (a row vanishing under you — ordinary
    arrow moves never consult it), and `NodeVtable::LandGroup`, the general
    row-vanish rule: land on the nearest survivor of the SAME group, never on a
    header or a lead line. Sparse rows are first-class — a null cell keeps its
    column NUMBER while being unlandable, which is what lets Up/Down match columns
    across ragged rows. **Reach for a sheet whenever a screen draws column
    headings.**
