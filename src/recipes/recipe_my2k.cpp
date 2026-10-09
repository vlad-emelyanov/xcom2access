// The My2K screen (UIFiraxisLiveLogin) — above all the legal documents a first launch raises over
// the main menu (docs/surfaces/shell.md, "My2K legal documents and account login"; the header
// comment in game_ui_my2k.h).
//
// ⛔ THE GAME OFFERS NO KEYBOARD HERE AT ALL, so nothing this recipe does has a vanilla key to
// defer to: ReadEULA puts the shell input into BlockingInput while the mouse is active, and the
// screen turned the mouse on itself. Every activation goes through the mouse's own path instead —
// a button's Click, a row's Click — which the block does not touch.
//
// TWO MODES, as the screen draws them:
//   · the list: 2K's message, one "Read: <document>" row per document, then the panel's buttons
//     (in ReadEULA, decline all and accept all). Any other state of the screen — a connection
//     notice, the account-link form the main menu's My2K button opens — reads through the same
//     shape: its message and whichever buttons it is showing.
//   · an open document: its title and its text, one node per line, then the OK that goes back.
//     The panel is hidden while it is up, so its buttons are not offered there either.
//
// ⛔ NO FOCUS WRITE-BACK. The screen is in mouse mode — it never fills its Navigator when the mouse
// is active — so there is no game selection to keep in step, and pushing one would move the
// highlight under a sighted helper's cursor for nothing. The one place the game READS focus is
// ReadEulas, and the document activation sets it itself, immediately before the click.

#include "recipes/recipe_context.h"
#include "recipes/recipes.h"

#include "game_ui_my2k.h"
#include "vocab.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace MenuNavigator {
namespace Recipes {
namespace {

// The document as a page: no StartRow, one node per line, positions suppressed — the research
// report's shape and its reasons (BuildResearchReport). The title rides in every id, so opening a
// second document is a new graph the differ speaks, not an unchanged one.
std::unique_ptr<GraphRender> BuildMy2KDocument(const GameUI::My2KView& view)
{
    const std::string key = "my2k/doc/" + view.documentTitle + "/";

    GraphBuilder b;
    b.BeginStop("document");
    b.PushContext("", "", /*positions*/ false);

    ControlId first;
    auto say = [&](const std::string& id, const std::string& text) {
        if (text.empty())
            return;
        ControlId node = ControlId::Structural(key + id);
        if (!first.IsValid())
            first = node;
        b.AddLabel(std::move(node), [text]() { return text; });
    };
    say("title", view.documentTitle);
    for (size_t i = 0; i < view.documentLines.size(); i++)
        say("line/" + std::to_string(i), view.documentLines[i]);
    if (view.documentLines.empty()) {
        // Opened, and nothing in it. Said rather than left silent: the player still has to find
        // the OK, and a stop holding only a title sounds like a document that was read.
        say("empty", Vocab::LegalDocumentNoText);
    }
    if (first.IsValid())
        b.SetStart(first);
    b.PopContext();

    b.BeginStop("close");
    NodeVtable vt;
    vt.Type = &ButtonType();
    vt.Announcements.emplace_back(NodeAnnouncement::Static(
        view.closeLabel.empty() ? Vocab::LegalDocumentClose() : view.closeLabel));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;
    // gScreen at activation, not captured: the handler runs from the input drain, a frame or more
    // after this render.
    vt.OnActivate = []() { GameUI::CloseMy2KDocument(gScreen); };
    b.AddItem(ControlId::Structural(key + "close"), std::move(vt));

    return b.Build();
}

std::unique_ptr<GraphRender> BuildMy2K()
{
    const GameUI::My2KView view = GameUI::ReadMy2K(gScreen);
    if (view.documentOpen)
        return BuildMy2KDocument(view);

    // Between states the screen can show nothing at all — Startup's HideAll runs before ReadEULA
    // shows its panel — and an empty graph would attach to silence. Not ready yet.
    if (view.messageLines.empty() && view.documents.empty() && view.buttons.empty())
        return nullptr;

    // The title in every id: the screen changes state in place, and two states with the same
    // buttons (a notice, then the next notice) must still read as a new screen.
    const std::string key = "my2k/" + view.title + "/";

    GraphBuilder b;
    b.BeginStop("content");

    ControlId start;
    if (!view.messageLines.empty()) {
        // The message is prose, not menu rows: no "1 of 3" on it, the report's empty context.
        b.PushContext("", "", /*positions*/ false);
        for (size_t i = 0; i < view.messageLines.size(); i++) {
            ControlId node = ControlId::Structural(key + "msg/" + std::to_string(i));
            if (!start.IsValid())
                start = node;
            const std::string text = view.messageLines[i];
            b.AddLabel(std::move(node), [text]() { return text; });
        }
        b.PopContext();
    }

    ControlId focusedRow;
    for (const GameUI::My2KDocumentRow& doc : view.documents) {
        ControlId id = ControlId::Referenced(doc.handle, key + "doc/" + std::to_string(doc.index));
        if (doc.focused && !focusedRow.IsValid())
            focusedRow = id;
        if (!start.IsValid())
            start = id;

        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(doc.label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        vt.OnActivate = [index = doc.index]() { GameUI::OpenMy2KDocument(gScreen, index); };
        b.AddItem(std::move(id), std::move(vt));
    }

    if (!view.buttons.empty()) {
        b.BeginStop("buttons");
        b.StartRow();
        for (const GameUI::MenuItem& btn : view.buttons) {
            ControlId id = ControlId::Referenced(btn.handle, key + "btn/" + btn.label);
            if (!start.IsValid())
                start = id;

            NodeVtable vt;
            vt.Type = &ButtonType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(btn.label));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            if (btn.disabled) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
                vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
            }
            // The button's own Click: its delegate is resolved on the screen at call time, so it
            // reaches the CURRENT state's handler — in ReadEULA, accept all or decline all (decline
            // has no confirmation in vanilla either: the state's own DeclinePopup is never called).
            vt.OnActivate = [handle = btn.handle]() { GameUI::ClickButton(handle); };
            vt.OnTooltip = PanelTooltipAction(btn.handle);
            b.AddItem(std::move(id), std::move(vt));
        }
        b.EndRow();
    }

    // Back from a document, land on the row that opened it (the activation left it focused);
    // otherwise on the message, which is what the screen is asking.
    if (focusedRow.IsValid())
        b.SetStart(std::move(focusedRow));
    else if (start.IsValid())
        b.SetStart(std::move(start));

    return b.Build();
}

std::string My2KName()
{
    const GameUI::My2KView view = GameUI::ReadMy2K(gScreen);
    return view.title.empty() ? std::string(Vocab::My2KAccount) : view.title;
}

} // namespace

// ---- the recipe this file defines ----
//
// ⛔ THE PROBE ORDER IS NOT HERE — it is the kRecipes literal in menu_navigator.cpp. The screen is
// pushed over the main menu — on boot, from the My2K button, and again when Challenge Mode finds
// the documents declined (XComChallengeModeManager.UIActionCallback_DisplayDeclinedEULA) — and
// raises only dialogs (and the text-entry dialog, which is no recipe) above itself. So it sits
// right under the dialog and popup recipes, above every shell screen.
const Recipe kMy2KRecipe{"my2k", ProbeFocused<GameUI::FindMy2KScreen>, BuildMy2K, My2KName};

} // namespace Recipes
} // namespace MenuNavigator
