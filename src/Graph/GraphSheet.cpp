#include "GraphSheet.hpp"

#include <cstdint>

namespace X2Access::Graph
{

namespace
{

// Structural keys are strings, and a row's identity is a pointer, so the pointer is spelled out
// rather than hashed: two rows can never collide, and a key read out of a log names the object.
std::string PtrKey(const void* p)
{
    static const char* kHex = "0123456789abcdef";
    auto v = reinterpret_cast<std::uintptr_t>(p);
    std::string out;
    out.reserve(sizeof(v) * 2);
    for (int shift = static_cast<int>(sizeof(v)) * 8 - 4; shift >= 0; shift -= 4)
        out.push_back(kHex[(v >> shift) & 0xF]);
    return out;
}

bool IsBlank(const std::string& s)
{
    for (char c : s)
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            return false;
    return true;
}

} // namespace

GraphSheet::GraphSheet(GraphBuilder& builder, std::string keyPrefix, SheetConfig config)
    : _b(builder)
    , _key(std::move(keyPrefix))
    , _config(std::move(config))
{
}

GraphSheet& GraphSheet::Region(
    const std::string& label, std::vector<std::string> columns, const std::string& role)
{
    CloseRegion();
    _regionIndex++;
    _b.SetRegion(_key + "reg:" + std::to_string(_regionIndex));
    if (!label.empty())
    {
        const std::string chosen
            = !role.empty() ? role : (columns.empty() ? std::string() : _config.TableRole);
        // positions=false: a table stamps its own coordinates through the cells' own readouts, so
        // the context must not also count its rows as if they were menu items.
        _b.PushContext(label, chosen, /*positions*/ false);
        _contextOpen = true;
    }
    _columns = std::move(columns);
    return *this;
}

GraphSheet& GraphSheet::Row(
    NodeVtable primary, const void* rowRef, std::vector<std::function<std::string()>> cells)
{
    BeginRow(primary, rowRef);
    EmitCell(std::move(primary), 0);

    for (std::size_t i = 0; i < cells.size(); i++)
    {
        if (!cells[i])
            continue; // sparse: an empty logical column is not landable, but keeps its number

        NodeVtable vt;
        vt.Type = _config.CellType;
        std::function<std::string()> producer = cells[i];
        const std::string blank = _config.Blank;
        vt.Announcements.emplace_back(
            [producer, blank]() {
                std::string v = producer ? producer() : std::string();
                return IsBlank(v) ? blank : v;
            },
            false, AnnouncementKinds::Label);
        // Type-ahead matches the ROW's name from any cell in it: a player searching for a soldier
        // should find them whether or not focus happens to be sitting in column 0.
        vt.SearchText = _rowName;
        EmitCell(std::move(vt), static_cast<int>(i) + 1);
    }

    WireVertical();
    return *this;
}

GraphSheet& GraphSheet::RowAt(
    NodeVtable primary, const void* rowRef, std::vector<std::pair<int, NodeVtable>> cells)
{
    BeginRow(primary, rowRef);
    EmitCell(std::move(primary), 0);
    for (auto& kv : cells)
        EmitCell(std::move(kv.second), kv.first);
    WireVertical();
    return *this;
}

GraphSheet& GraphSheet::Line(NodeVtable vtable)
{
    // rowRef null: a lead line is not a data row, so it takes a positional key and stays OUT of
    // the landing group — the row-vanish rule must never drop a player onto a heading.
    BeginRow(vtable, nullptr);
    EmitCell(std::move(vtable), 0);
    WireVertical();
    return *this;
}

void GraphSheet::Finish()
{
    CloseRegion();
}

std::string GraphSheet::RowKey(const std::string& keyPrefix, const void* rowRef, int col)
{
    return keyPrefix + "row" + PtrKey(rowRef) + "c" + std::to_string(col);
}

void GraphSheet::CloseRegion()
{
    if (_contextOpen)
    {
        _b.PopContext();
        _contextOpen = false;
    }
    // Rows DO chain across a region boundary: the previous row's ids survive this call, so the
    // last row of one region wires vertically to the first of the next. A table split into
    // sections is still one column to read down.
    _columns.clear();
}

void GraphSheet::BeginRow(const NodeVtable& primary, const void* rowRef)
{
    _rowRef = rowRef;
    _row++;
    _prevRowIds = _rowIds;
    _prevRowName = _rowName;
    _rowIds.clear();

    // The row's name for vertical edge labels is the primary's LABEL — its first announcement
    // part. Copied before the vtable is moved into the graph.
    _rowName = primary.Announcements.empty() ? nullptr : primary.Announcements.front().Text;

    // The row IS its associated element: remember the primary's activations so every cell answers
    // Enter and Backspace like the element itself, and acting never requires walking back to
    // column 0 first.
    _rowActivate = primary.OnActivate;
    _rowSecondary = primary.OnSecondary;
}

void GraphSheet::EmitCell(NodeVtable vtable, int col)
{
    if (col > 0)
    {
        if (!vtable.OnActivate)
            vtable.OnActivate = _rowActivate;
        if (!vtable.OnSecondary)
            vtable.OnSecondary = _rowSecondary;
    }
    vtable.Column = col;

    // DATA rows join this sheet's landing group; static lines (a lead row, an empty message) stay
    // out, so a vanishing row lands on a neighbouring ROW rather than on a heading.
    if (_rowRef)
        vtable.LandGroup = _key;

    const std::string skey = _rowRef ? RowKey(_key, _rowRef, col)
                                     : _key + "r" + std::to_string(_row) + "c" + std::to_string(col);

    ControlId id = (_rowRef && col == 0 && FollowRowRefs) ? ControlId::Referenced(_rowRef, skey)
                                                          : ControlId::Structural(skey);
    _b.AddNode(id, std::move(vtable));

    // Left/right to the nearest EMITTED cell (sparse rows skip empty columns), labelled with the
    // DESTINATION column's header — that is the whole column-header mechanism: no label onto the
    // primary, whose full readout identifies the row by itself.
    if (!_rowIds.empty())
    {
        const CellRef& left = _rowIds.back();
        _b.Connect(id, GraphDir::Left, left.Id, left.Col == 0 ? std::string() : Header(left.Col));
        _b.Connect(left.Id, GraphDir::Right, id, Header(col));
    }
    _rowIds.push_back(CellRef{col, id});
}

void GraphSheet::WireVertical()
{
    if (_prevRowIds.empty() || _rowIds.empty())
        return;

    // The same LOGICAL column where both rows have it, else the other row's primary — so a ragged
    // row never dead-ends a column walk. Labels name the DESTINATION ROW when landing off the
    // primary, so you know which row you are in without the full readout; landings on column 0
    // stay unlabelled, because the primary's own parts read the row.
    for (const CellRef& cell : _rowIds)
    {
        const bool matched = HasCol(_prevRowIds, cell.Col);
        _b.Connect(cell.Id, GraphDir::Up, FindAt(_prevRowIds, cell.Col),
            (matched && cell.Col > 0) ? CellText(_prevRowName) : std::string());
    }
    for (const CellRef& cell : _prevRowIds)
    {
        const bool matched = HasCol(_rowIds, cell.Col);
        _b.Connect(cell.Id, GraphDir::Down, FindAt(_rowIds, cell.Col),
            (matched && cell.Col > 0) ? CellText(_rowName) : std::string());
    }
}

ControlId GraphSheet::FindAt(const std::vector<CellRef>& row, int col)
{
    for (const CellRef& c : row)
        if (c.Col == col)
            return c.Id;
    return row.front().Id; // fall to the row's primary
}

bool GraphSheet::HasCol(const std::vector<CellRef>& row, int col)
{
    for (const CellRef& c : row)
        if (c.Col == col)
            return true;
    return false;
}

std::string GraphSheet::Header(int col) const
{
    const int index = col - 1;
    if (index < 0 || index >= static_cast<int>(_columns.size()))
        return {};
    return _columns[static_cast<std::size_t>(index)];
}

std::string GraphSheet::CellText(const std::function<std::string()>& producer) const
{
    return producer ? producer() : std::string();
}

} // namespace X2Access::Graph
