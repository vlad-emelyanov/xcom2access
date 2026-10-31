#include "help_pages.h"
#include "avenger.h"
#include "free_aim.h"
#include "geo_scanner.h"
#include "hacking_screen.h"
#include "log.h"
#include "menu_navigator.h"
#include "narrator.h"
#include "tactical_hud.h"
#include "text_input.h"
#include "tile_reader.h"
#include "tutorial_reader.h"
#include "vocab.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace HelpPages {

namespace {

constexpr uint64_t kRepeatWindowMs = 8000;

// Most specific first; ComputeContext() walks the same order. A context's pages are its row in
// kContexts. ⛔ A word named here MUST exist in X2AccessVocab, both loc files and vocab.h:
// tools/check_localization.py reads this file for `Vocab::Help...` names and fails offline on
// one that is not declared, or declared and not listed here.
enum class Context : uint8_t {
    MultilineText,
    Hacking,
    FreeAim,
    Targeting,
    TacticalHud,
    TacticalWorld,
    TutorialWorld,
    Geoscape,
    AvengerGrid,
    Menu,
    Unread,
};

struct Pages {
    const Vocab::Word* words;
    size_t count;
};

#define PAGES(arr) Pages{arr, sizeof(arr) / sizeof(arr[0])}

const Vocab::Word kMultilineText[] = {Vocab::HelpText1};
const Vocab::Word kHacking[] = {Vocab::HelpHack1};
const Vocab::Word kFreeAim[] = {Vocab::HelpFree1, Vocab::HelpFree2};
const Vocab::Word kTargeting[] = {Vocab::HelpTarget1};
const Vocab::Word kTacticalHud[] = {Vocab::HelpHud1, Vocab::HelpHud2};
const Vocab::Word kTacticalWorld[] = {Vocab::HelpWorld1, Vocab::HelpWorld2, Vocab::HelpWorld3,
    Vocab::HelpWorld4, Vocab::HelpWorld5};
// The tutorial differs in one page only: I re-reads the tutorial message there, and the squad
// brief and the soldier switch are not live.
const Vocab::Word kTutorialWorld[] = {Vocab::HelpWorld1, Vocab::HelpWorld2, Vocab::HelpWorld3,
    Vocab::HelpTutorial4, Vocab::HelpWorld5};
const Vocab::Word kGeoscape[] = {Vocab::HelpGeo1, Vocab::HelpGeo2};
const Vocab::Word kAvengerGrid[] = {Vocab::HelpAvenger1};
const Vocab::Word kMenu[] = {Vocab::HelpMenu1, Vocab::HelpMenu2};
// Unread has no page of its own: it speaks Vocab::NavHelpUnavailable directly.

constexpr Pages kContexts[] = {
    PAGES(kMultilineText), PAGES(kHacking), PAGES(kFreeAim), PAGES(kTargeting),
    PAGES(kTacticalHud), PAGES(kTacticalWorld), PAGES(kTutorialWorld), PAGES(kGeoscape),
    PAGES(kAvengerGrid), PAGES(kMenu),
};
constexpr size_t kPagedContextCount = sizeof(kContexts) / sizeof(kContexts[0]);
static_assert(kPagedContextCount == static_cast<size_t>(Context::Unread),
    "every context before Unread needs a page row, in enum order");

// The same gates the claims use, flags only. Order is the answer to "which surface owns the
// keys right now": a modal text field and the hacking overlay are screens of their own; a
// non-ambient recipe (menu, Geoscape tray) took the surface outright; below that the tactical
// layer's three input modes, then the ant farm.
Context ComputeContext()
{
    if (TextInput::IsMultiLineActive())
        return Context::MultilineText;
    if (HackingScreen::IsActive())
        return Context::Hacking;
    if (MenuNavigator::MenuAttached())
        return GeoScanner::IsActive() ? Context::Geoscape : Context::Menu;
    if (FreeAim::IsActive())
        return Context::FreeAim;
    if (TacticalHUD::IsTargeting())
        return Context::Targeting;
    if (MenuNavigator::OwnsInput()) // the ambient recipe holds input: a HUD layer is focused
        return TacticalHUD::IsMissionActive() ? Context::TacticalHud : Context::Menu;
    if (TileReader::IsActive())
        return TutorialReader::InTutorialMission() ? Context::TutorialWorld
                                                   : Context::TacticalWorld;
    if (GeoScanner::IsActive())
        return Context::Geoscape;
    if (Avenger::IsGridActive())
        return Context::AvengerGrid;
    return Context::Unread;
}

} // namespace

void Speak()
{
    static Context sContext = Context::Unread;
    static size_t sPage = 0;
    static uint64_t sLastMs = 0;
    static bool sHave = false;

    const Context context = ComputeContext();
    const uint64_t now = GetTickCount64();

    if (context == Context::Unread) {
        sHave = false;
        Narrator::Say(Narrator::Channel::Aid, std::string(Vocab::NavHelpUnavailable));
        return;
    }

    const Pages& pages = kContexts[static_cast<size_t>(context)];
    if (sHave && context == sContext && now - sLastMs <= kRepeatWindowMs)
        sPage = (sPage + 1) % pages.count;
    else
        sPage = 0;
    sContext = context;
    sLastMs = now;
    sHave = true;

    std::string text = pages.words[sPage];
    if (text.empty()) {
        // Vocab::Lookup already logged the missing key; nothing compiled in to fall back on.
        LOG_WARN("HelpPages: context %d page %zu has no text", static_cast<int>(context), sPage);
        return;
    }
    // The first page of a multi-page context says there is more. Both parts end in a full stop,
    // so a plain space joins them.
    if (sPage == 0 && pages.count > 1) {
        const std::string more = Vocab::HelpMore;
        if (!more.empty())
            text += " " + more;
    }
    Narrator::Say(Narrator::Channel::Aid, text);
}

} // namespace HelpPages
