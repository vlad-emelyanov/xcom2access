#pragma once

// Graph A11y Kernel — the TABLE / DOCUMENT emitter. STL-only; see ControlId.hpp for the boundary
// rule. Ported from WrathAccess's GraphSheet, with the host vocabulary lifted into a config
// struct so this can live inside the kernel instead of beside it.
//
// WHAT A TABLE NEEDS THAT A LIST DOES NOT. A list row is one thing with one name, and flattening
// it into a sentence loses nothing. A TABLE row is several facts under headings, and speech has
// three problems with that a sighted reader does not have:
//
//   * the headings are drawn once, at the top, and never again — so a cell read on its own is a
//     value with no idea what it means ("3" under a column you cannot see);
//   * reading the whole row every time is the only alternative, and on a twenty-soldier roster
//     with six columns that is a paragraph per arrow press;
//   * moving down a column and having focus snap back to the first cell makes a column
//     impossible to READ DOWN, which is the single most useful thing a table offers.
//
// This emitter answers all three with graph primitives and no special announcer support:
//
//   column header on column change  -> the Left/Right EDGE LABEL (the destination column's
//                                      header), so stepping sideways says "class, Ranger";
//   row name when off the primary   -> the Up/Down edge label into a non-primary cell, so moving
//                                      down a column says which row you landed in;
//   the whole-row readout           -> the PRIMARY (column 0) cell's announcement list carries
//                                      the row's other facts as extra parts, so vertical
//                                      navigation along column 0 reads the whole row exactly as a
//                                      flat list would — and each part stays independently
//                                      filterable, like any control's;
//   keeping your column             -> NodeVtable::Column plus KeyGraph::SlideToColumn, which
//                                      restores it after a NON-directional jump (a row vanishing
//                                      under you).
//
// So the flat reading and the tabular reading are the same graph: column 0 IS the list, and the
// cells to its right are the same facts made addressable.
//
// Emit rows into one region, then start the next; Finish() closes the last. Raw mode underneath
// (explicit edges), so no automatic positions — a table stamps its own coordinates.

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "GraphBuilder.hpp"
#include "GraphTypes.hpp"

namespace X2Access::Graph
{

/// The words and types the HOST owns. The kernel speaks no language of its own, so a sheet is
/// handed the three things it cannot invent.
struct SheetConfig
{
    /// Read in place of an empty cell. A blank cell must SAY something: silence is
    /// indistinguishable from a cell that failed to resolve, or from no cell at all.
    std::string Blank;

    /// The role word for a region that has columns ("table"), announced once on entry.
    std::string TableRole;

    /// The control type stamped on auto-built metadata cells (a read-only text line).
    const ControlType* CellType = nullptr;
};

class GraphSheet
{
public:
    /// `keyPrefix` namespaces every structural key this sheet emits; it must be stable across
    /// rebuilds. `builder` must outlive the sheet.
    GraphSheet(GraphBuilder& builder, std::string keyPrefix, SheetConfig config);

    GraphSheet(const GraphSheet&) = delete;
    GraphSheet& operator=(const GraphSheet&) = delete;

    /// Whether row primaries carry their domain object as a tier-1 focus REFERENCE (the default).
    /// Turn off for sheets whose objects can migrate to another sheet: focus would otherwise chase
    /// the object across tables instead of staying put on the nearest neighbour.
    bool FollowRowRefs = true;

    /// Start a region: a Ctrl+arrow jump target and a context level. `columns` are the headers for
    /// the metadata cells — column 0, the primary, has none because its own readout identifies it.
    /// An empty `columns` makes a plain one-column list region.
    GraphSheet& Region(const std::string& label, std::vector<std::string> columns = {},
        const std::string& role = "");

    /// One row: the interactive primary's vtable, then one text-producer per metadata column. A
    /// null producer leaves that logical column EMPTY and unlandable (a sparse row) while keeping
    /// the column numbers of the cells after it — which is what lets vertical navigation match
    /// columns across ragged rows.
    ///
    /// `rowRef` is the row's DOMAIN OBJECT and should be passed whenever rows can appear, vanish
    /// or reorder: keys derive from it, so a removed row's focus slides to a genuinely different
    /// identity and the differ announces the landing. Index keys would silently rebadge the next
    /// row as "the same control".
    GraphSheet& Row(NodeVtable primary, const void* rowRef,
        std::vector<std::function<std::string()>> cells);

    /// A row whose cells are pre-built vtables at explicit LOGICAL columns (a sparse grid).
    /// Column numbers are 1-based; 0 is the primary.
    GraphSheet& RowAt(NodeVtable primary, const void* rowRef,
        std::vector<std::pair<int, NodeVtable>> cells);

    /// A single full-width line — a lead row, a section note. Never a landing-group member, so
    /// the row-vanish rule cannot drop a player onto it.
    GraphSheet& Line(NodeVtable vtable);

    /// Close the final region. Call once, after the last row.
    void Finish();

    /// The structural key this sheet gives one cell of a row that HAS a domain object. Exposed
    /// because a host that keeps a side table keyed on structural keys — a focus write-back map —
    /// has to name the same cells the sheet named, and two implementations of one key format
    /// would drift the first time either changed.
    static std::string RowKey(const std::string& keyPrefix, const void* rowRef, int col);

private:
    struct CellRef
    {
        int Col = 0;
        ControlId Id;
    };

    void BeginRow(const NodeVtable& primary, const void* rowRef);
    void EmitCell(NodeVtable vtable, int col);
    void WireVertical();
    void CloseRegion();

    std::string Header(int col) const;
    std::string CellText(const std::function<std::string()>& producer) const;

    static ControlId FindAt(const std::vector<CellRef>& row, int col);
    static bool HasCol(const std::vector<CellRef>& row, int col);

    GraphBuilder& _b;
    std::string _key;
    SheetConfig _config;

    int _regionIndex = -1;
    bool _contextOpen = false;

    std::vector<std::string> _columns; // headers for cells 1..N; empty = a plain list region
    int _row = -1;
    std::vector<CellRef> _rowIds;
    std::vector<CellRef> _prevRowIds;
    std::function<std::string()> _rowName;     // the current row's primary label
    std::function<std::string()> _prevRowName;
    const void* _rowRef = nullptr;
    std::function<void()> _rowActivate;  // the primary's Enter, inherited by the row's cells
    std::function<void()> _rowSecondary; // and its Backspace
};

} // namespace X2Access::Graph
