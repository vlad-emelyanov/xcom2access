// Game-UI access, the My2K screen (UIFiraxisLiveLogin) — see the header. Explicit offsets from the
// generated SDK's dump comments, each pinned in sdk_layout_checks.cpp.

#include "game_ui_my2k.h"
#include "game_ui_internal.h"
#include "log.h"

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma pack(push, 4)
#endif

namespace GameUI {

namespace {

// ---- UIFiraxisLiveLogin ----
constexpr int32_t kMy2KGeneric1Offset = 0x440;  // m_Generic1Button — Cancel / Disagree all / Close
constexpr int32_t kMy2KGeneric2Offset = 0x448;  // m_Generic2Button — OK / Agree all
constexpr int32_t kMy2KGeneric4Offset = 0x450;  // m_Generic4Button — New user / Create
constexpr int32_t kMy2KUsernameOffset = 0x458;  // m_UsernameButton — opens the e-mail input
constexpr int32_t kMy2KPasswordOffset = 0x460;  // m_PasswordButton — opens the password input
constexpr int32_t kMy2KEulaTitleOffset = 0x470; // m_eulaTitle (UITextContainer*)
constexpr int32_t kMy2KEulaTextOffset = 0x478;  // m_eulaText (UITextContainer*)
constexpr int32_t kMy2KEulaRowsOffset = 0x488;  // m_eulaButtons (TArray<UIMechaListItem*>)
constexpr int32_t kMy2KInitOKOffset = 0x548;    // m_InitOKLabel (localized FString)
constexpr int32_t kMy2KTitleOffset = 0x6CC;     // m_2KTitle (FString)
constexpr int32_t kMy2KMessageOffset = 0x6DC;   // m_2kMessage (FString)

// ---- UITextContainer / UIText ----
constexpr int32_t kTextContainerTextOffset = 0x3E0; // UITextContainer::Text (UIText*)
constexpr int32_t kUITextTextOffset = 0x3D8;        // UIText::Text
constexpr int32_t kUITextHtmlOffset = 0x3E8;        // UIText::HtmlText

// The buttons in the order InitScreen adds them to the game's own Navigator.
constexpr int32_t kMy2KButtonOrder[] = {kMy2KUsernameOffset, kMy2KPasswordOffset,
    kMy2KGeneric1Offset, kMy2KGeneric4Offset, kMy2KGeneric2Offset};

// A whole FString, sized from its own count. ⛔ Not ReadFStringFieldRaw: that reads into a fixed
// 1024-character buffer, and a legal document is tens of thousands of characters that 2K's server
// decides — a clipped read here would end the Terms of Service mid-sentence and sound exactly
// like the end of the document (the size-the-buffer-from-the-data hard rule).
std::string ReadWholeFStringRaw(const void* obj, int32_t offset)
{
    if (!obj)
        return std::string();
    const uint8_t* fstr = static_cast<const uint8_t*>(obj) + offset;
    const int32_t count = ReadI32Raw(fstr, 8); // includes the terminator
    if (count <= 1)
        return std::string();
    constexpr int32_t kMaxWchars = 1 << 22; // 4M characters: far past any document, short of garbage
    if (count > kMaxWchars) {
        LOG_WARN("My2K: refusing an FString of %d characters at +0x%X", count, offset);
        return std::string();
    }
    std::wstring buf(static_cast<size_t>(count), L'\0');
    const int len = CopyFStringRaw(fstr, &buf[0], count);
    return len > 0 ? WcharToUtf8(buf.c_str(), len) : std::string();
}

// A UITextContainer's words: its UIText's Text, else the HtmlText (SetHTMLText writes only that).
std::string ReadTextContainerRaw(void* container)
{
    void* text = container ? ReadPtrRaw(container, kTextContainerTextOffset) : nullptr;
    if (!text)
        return std::string();
    std::string raw = ReadWholeFStringRaw(text, kUITextTextOffset);
    if (raw.empty())
        raw = ReadWholeFStringRaw(text, kUITextHtmlOffset);
    return raw;
}

void AppendUtf8(std::string& out, unsigned long cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x110000) {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// HTML character references, decoded AFTER CleanText has removed the tags — decoding first would
// turn an escaped "&lt;" into a '<' that CleanText then reads as the start of a tag. The game's
// own strings never carry these, which is why CleanText does not decode them; a document 2K
// serves as HTML can. A non-breaking space becomes a space, and runs are collapsed again.
std::string DecodeEntities(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] != '&') {
            out += text[i];
            continue;
        }
        const size_t semi = text.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 10) {
            out += '&';
            continue;
        }
        const std::string name = text.substr(i + 1, semi - i - 1);
        unsigned long cp = 0;
        if (name == "amp")
            cp = '&';
        else if (name == "lt")
            cp = '<';
        else if (name == "gt")
            cp = '>';
        else if (name == "quot")
            cp = '"';
        else if (name == "apos")
            cp = '\'';
        else if (name == "nbsp")
            cp = ' ';
        else if (name.size() > 1 && name[0] == '#')
            cp = (name[1] == 'x' || name[1] == 'X') ? strtoul(name.c_str() + 2, nullptr, 16)
                                                    : strtoul(name.c_str() + 1, nullptr, 10);
        if (cp == 0) {
            out += '&'; // not a reference we know: keep the text as written
            continue;
        }
        AppendUtf8(out, cp == 0xA0 ? ' ' : cp);
        i = semi;
    }
    // Collapse the spaces a decoded &nbsp; can leave doubled, and trim.
    std::string collapsed;
    collapsed.reserve(out.size());
    for (char c : out) {
        if (c == ' ' && (collapsed.empty() || collapsed.back() == ' '))
            continue;
        collapsed += c;
    }
    while (!collapsed.empty() && collapsed.back() == ' ')
        collapsed.pop_back();
    return collapsed;
}

// Is this tag one that ends a line on screen? <br>, and the block elements a document is built of.
bool IsLineBreakTag(const std::string& tag)
{
    size_t i = 1; // past '<'
    while (i < tag.size() && (tag[i] == '/' || tag[i] == ' '))
        i++;
    std::string name;
    while (i < tag.size() && std::isalnum(static_cast<unsigned char>(tag[i])))
        name += static_cast<char>(std::tolower(static_cast<unsigned char>(tag[i++])));
    static const char* const kBreaks[] = {"br", "p", "li", "div", "tr", "ul", "ol", "table",
        "h1", "h2", "h3", "h4", "h5", "h6"};
    for (const char* b : kBreaks)
        if (name == b)
            return true;
    return false;
}

// Raw markup into the lines the screen draws, each cleaned and decoded, empties dropped.
// ⛔ Split FIRST, then clean (FlashArgLines' rule): CleanText collapses whitespace and turns a
// break into a space, so cleaning the whole document would fuse it into one node.
std::vector<std::string> SplitMarkupLines(const std::string& raw)
{
    std::vector<std::string> lines;
    std::string chunk;
    auto flush = [&]() {
        std::string line = DecodeEntities(CleanText(chunk));
        if (!line.empty())
            lines.push_back(std::move(line));
        chunk.clear();
    };
    for (size_t i = 0; i < raw.size(); i++) {
        const char c = raw[i];
        if (c == '\n' || c == '\r') {
            flush();
        } else if (c == '<') {
            const size_t close = raw.find('>', i);
            if (close == std::string::npos) {
                chunk += raw.substr(i);
                break;
            }
            const std::string tag = raw.substr(i, close - i + 1);
            if (IsLineBreakTag(tag))
                flush();
            else
                chunk += tag; // presentation markup: CleanText removes it with the line
            i = close;
        } else {
            chunk += c;
        }
    }
    flush();
    return lines;
}

bool PanelVisible(void* panel)
{
    return panel && (ReadU32Raw(panel, kPanelFlagsOffset) & kPanelVisible) != 0;
}

std::vector<void*> ReadDocumentRows(void* screen)
{
    return ReadPtrArrayAllRaw("My2K: document rows", screen, kMy2KEulaRowsOffset);
}

} // namespace

void* FindMy2KScreen()
{
    static ScreenFinder sFinder{"UIFiraxisLiveLogin"};
    static bool sReported = false;
    void* screen = PreferStack("UIFiraxisLiveLogin", sFinder, sReported, ClassMatch::Derived);
    return (screen && IsFocusedRaw(screen)) ? screen : nullptr;
}

My2KView ReadMy2K(void* screen)
{
    My2KView v;
    if (!screen)
        return v;
    v.screen = screen;
    v.title = DecodeEntities(CleanText(ReadWholeFStringRaw(screen, kMy2KTitleOffset)));

    void* docText = ReadPtrRaw(screen, kMy2KEulaTextOffset);
    v.documentOpen = PanelVisible(docText);
    if (v.documentOpen) {
        // ReadEulas hid everything else: the panel (HideMy2kPanel, Flash-side — the buttons keep
        // their own visible bits, which is why they are NOT read here) and every document row.
        v.documentTitle = DecodeEntities(CleanText(
            ReadTextContainerRaw(ReadPtrRaw(screen, kMy2KEulaTitleOffset))));
        v.documentLines = SplitMarkupLines(ReadTextContainerRaw(docText));
        v.closeLabel = CleanText(ReadWholeFStringRaw(screen, kMy2KInitOKOffset));
        return v;
    }

    v.messageLines = SplitMarkupLines(ReadWholeFStringRaw(screen, kMy2KMessageOffset));

    const std::vector<void*> rows = ReadDocumentRows(screen);
    for (size_t i = 0; i < rows.size(); i++) {
        OptionRow row;
        // MakeRow skips a hidden row, which is every row while a document is open — and they
        // stay in the array (AgreeToEULA re-shows them), so the slot index is kept, not packed.
        if (!rows[i] || !MakeRow(rows[i], static_cast<int>(i), &row))
            continue;
        My2KDocumentRow doc;
        doc.handle = rows[i];
        doc.index = static_cast<int>(i);
        doc.label = row.label;
        doc.focused = row.focused;
        v.documents.push_back(std::move(doc));
    }

    for (int32_t offset : kMy2KButtonOrder) {
        MenuItem button = ReadButtonField(screen, offset);
        if (button.handle)
            v.buttons.push_back(std::move(button));
    }
    return v;
}

bool OpenMy2KDocument(void* screen, int index)
{
    if (!screen)
        return false;
    const std::vector<void*> rows = ReadDocumentRows(screen);
    if (index < 0 || index >= static_cast<int>(rows.size()) || !rows[index])
        return false;
    // ⛔ Exactly one focused row before the click: ReadEulas walks every row and loads the text of
    // EACH one whose bIsFocused is set, so a stale focus on another row (a mouse hover) would leave
    // the last focused document on screen instead of the one asked for.
    uint8_t noParams[16] = {};
    for (size_t i = 0; i < rows.size(); i++) {
        if (rows[i] && static_cast<int>(i) != index)
            CallNamedFunction(rows[i], "OnLoseFocus", noParams);
    }
    CallNamedFunction(rows[index], "OnReceiveFocus", noParams);
    // UIMechaListItem.Click: the mouse's own path to the row's OnClickDelegate (ReadEulas).
    return CallNamedFunction(rows[index], "Click", noParams);
}

bool CloseMy2KDocument(void* screen)
{
    // ⛔ Only while the document is showing. The SAME Escape on the list is the screen's
    // OnGeneric1ButtonPress — in ReadEULA, decline every document with no confirmation.
    if (!screen || !PanelVisible(ReadPtrRaw(screen, kMy2KEulaTextOffset)))
        return false;
    return SendScreenCommand(screen, kCmdEscape);
}

} // namespace GameUI

#ifdef _MSC_VER
#pragma pack(pop)
#endif
