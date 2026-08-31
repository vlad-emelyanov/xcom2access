// The tactical HUD recipe — the mod's first AMBIENT recipe (plan phase 3, the HUD graph).
//
// Everything about how this recipe holds input is INVERTED from the menu recipes, and the
// Recipe::ambient flag is what expresses it: it attaches SILENTLY the moment a mission's HUD
// settles and then sits UNFOCUSED for most of the mission, while the tile cursor keeps the
// arrows and the game keeps Tab. Ctrl+Tab focuses it (HUD mode — the navigator's claims come
// alive and arrows walk the stops); Escape or Ctrl+Tab again hands the keys back; raising the
// shot HUD takes them back without asking (YieldFocus — targeting owns the arrows, and the
// targeting poll's narration speaks next, so the yield is silent).
//
// The stops mirror what is ON SCREEN, in the parity-audited order the plan fixed: SOLDIER (the
// bottom-left panel plus the weapon/ammo readout — companion-backed, because those panels are
// write-only pipes into Flash and the facts live only in XCOMHISTORY), ABILITIES (pure widget
// read), COMMANDS (the mouse-controls strip, the one other tactical panel that RETAINS its
// words — label, bound key and state per button), OBJECTIVES (retained widget data through the
// Ctrl+X composers). The enemy stop is still queued work.
//
// ⛔ Probe subtlety that is easy to lose: while ATTACHED, the navigator re-probes only THIS
// recipe — a higher recipe gets its chance only after a detach. Every menu that can appear over
// tactical detaches us through the gate (the pause menu and the hacking screen are real stack
// entries, so hudOnStack falls or blocked rises) EXCEPT the modal dialog, which never pushes a
// screen: the probe has to refuse by its own hand while one is showing, or the player would
// answer it blind with our claims eating the keys.

#include "recipes/recipe_context.h"
#include "recipes/recipes.h"

#include "objective_reader.h"
#include "tts.h"
#include "vocab.h"
#include "log.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace MenuNavigator {
namespace Recipes {
namespace {

void* ProbeTacticalHud()
{
    // The dialog recipe leads kRecipes but only wins while the navigator is DETACHED — and a
    // UIDialogueBox takes no stack entry, so the tactical gate cannot see it either. Refusing
    // here is what turns "a dialog raised over tactical" into a detach, which is what lets the
    // dialog recipe attach on the very next poll.
    if (GameUI::FindDialogScreen())
        return nullptr;
    GameUI::TacticalGate gate = GameUI::ReadTacticalGate();
    if (!gate.hudOnStack || gate.blocked)
        return nullptr;
    return GameUI::FindTacticalHUD();
}

std::unique_ptr<GraphRender> BuildTacticalHud()
{
    GameUI::AbilityBarView bar = GameUI::ReadAbilityBar(gScreen);
    if (!bar.valid)
        return nullptr;
    GameUI::TacticalGate gate = GameUI::ReadTacticalGate();

    GraphBuilder b;
    bool anyNode = false;

    // ---- Soldier: the selected unit's stat block, one node per fact ----
    //
    // Companion-backed: the soldier panel, the unit flag and the weapon/ammo readout are all
    // write-only pipes into Flash, so the words come from X2AccessUnitReader — the SAME class
    // that composes the `;` readout, so the stop and the key cannot disagree about a fact
    // (the shared-oracle rule). One outbound call per rebuild, the plan's stated budget.
    std::vector<WireRecord> soldier
        = FetchCompanionRecords("X2AccessUnitReader", "BuildSoldierRecords");
    if (!soldier.empty()) {
        anyNode = true;
        b.BeginStop("soldier");
        b.PushContext(Vocab::StopSoldier);
        // The unit's ObjectID rides every identity: a soldier SWITCH must read to the differ as
        // new nodes, where damage or a spent action on the same soldier is a text change under a
        // held identity (the live-part watch's business, not a focus jump).
        std::string oid = RecField(soldier.front(), "oid");
        int n = 0;
        for (const WireRecord& rec : soldier) {
            std::string text = RecField(rec, "text");
            if (text.empty())
                continue; // a fact that does not apply is a record the companion never emits;
                          // nothing positional rides on the index (each id carries its kind)
            b.AddLabel(ControlId::Structural("thud/sol/" + oid + "/" + std::to_string(n++) + "/"
                           + RecField(rec, "kind")),
                [text]() { return text; });
        }
        b.PopContext();
    }

    // ---- Abilities: one node per live bar icon, a pure widget read (T2) ----
    if (!bar.entries.empty()) {
        anyNode = true;
        b.BeginStop("abilities");
        b.PushContext(Vocab::StopAbilities);
        // The bar is drawn HORIZONTAL — a strip of icons — so it is walked with Left/Right,
        // and the game wraps it to a second row past MAX_NUM_ABILITIES_PER_ROW_BAR (15).
        // Mirror both: rows of 15 sharing one row key, so Up/Down crosses rows
        // column-preserving exactly when the game itself draws two.
        constexpr size_t kBarRowLen = 15;
        for (size_t i = 0; i < bar.entries.size(); i++) {
            if (i % kBarRowLen == 0) {
                if (i)
                    b.EndRow();
                b.StartRow("abilities");
            }
            const GameUI::AbilityBarEntry& e = bar.entries[i];
            // The antenna text is mouse-active-only in the game's own code paths; the mouse
            // gate deliberately preserves IsMouseActive so it stays fed, but an empty name
            // must still land a node — dropping it would silently renumber every ability
            // after it (the empty-answer trap).
            std::string name = e.name.empty() ? Vocab::AbilityFallback(static_cast<int>(i))
                                              : e.name;

            NodeVtable vt;
            vt.Type = &AbilityType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(name));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;

            // Cooldown and charge strings arrive with the game's own localized prefixes baked
            // in; the hotkey is phrased so a bare digit cannot read as a position.
            std::string value;
            AppendPhrase(value, e.cooldown);
            AppendPhrase(value, e.charges);
            if (!e.hotkey.empty())
                AppendPhrase(value, Vocab::AbilityHotkey(e.hotkey));
            if (!value.empty()) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(value));
                vt.Announcements.back().Kind = AnnouncementKinds::Value;
            }

            if (!e.available) {
                // The WHY is an FName code (AA_CannotAfford_ActionPoints …) with no localized
                // form the widgets retain, so the reason waits for the companion vocabulary —
                // speaking the code would hand a Russian player an internal identifier.
                vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
                vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
            } else {
                const int index = static_cast<int>(i);
                vt.OnActivate = [index]() {
                    // The game's own click path (input gate, tutorial gate, select sound,
                    // SelectAbility). On success targeting raises: the navigator yields focus
                    // on its next tick and the targeting poll announces ability and default
                    // target. A refusal comes back false and the game is silent about it, so
                    // the answer is owed here.
                    if (!GameUI::ActivateBarAbility(gScreen, index))
                        TTS::SpeakInterrupt(Vocab::Disabled);
                };
            }

            // Name in the identity: a soldier switch rebuilds the bar under the same widget
            // pointers, and the differ must see new nodes, not a silent relabel.
            b.AddItem(ControlId::Structural(
                          "thud/ab/" + std::to_string(i) + "/" + name),
                std::move(vt));
        }
        b.EndRow();
        b.PopContext();
    }

    // ---- Commands: the mouse-controls strip, the game's own answer to "what does this HUD
    // offer" (the sighted-parity hard rule names this exact panel) ----
    //
    // Only while the shot HUD is DOWN: raised, the strip shows Cancel Shot alone and slot 0's
    // retained label IS "cancel shot" — and the layer is yielding focus then anyway, so the
    // stop would never be walked. The read reconstructs the visible set from the same inputs
    // UpdateControls used; see game_ui.h.
    if (!gate.menuRaised) {
        GameUI::MouseControlsView ctl = GameUI::ReadMouseControls(gScreen);
        if (ctl.valid && !ctl.buttons.empty()) {
            anyNode = true;
            b.BeginStop("commands");
            b.PushContext(Vocab::StopCommands);
            b.StartRow("commands"); // drawn as one horizontal row of icon buttons
            for (const GameUI::CommandButtonEntry& e : ctl.buttons) {
                std::string name = e.label.empty() ? Vocab::CommandFallback(e.index) : e.label;

                NodeVtable vt;
                vt.Type = &ButtonType();
                vt.Announcements.emplace_back(NodeAnnouncement::Static(name));
                vt.Announcements.back().Kind = AnnouncementKinds::Label;

                // The player's actual binding, as the hover help gives it — this stop doubles
                // as the place the vanilla hotkeys get taught. The game's "no key bound" line
                // is a sentence of its own and must not be phrased as a key name.
                std::string value;
                if (!e.key.empty()) {
                    value = (!ctl.noKeyBound.empty() && e.key == ctl.noKeyBound)
                        ? e.key
                        : Vocab::AbilityHotkey(e.key);
                }
                if (!value.empty()) {
                    vt.Announcements.emplace_back(NodeAnnouncement::Static(value));
                    vt.Announcements.back().Kind = AnnouncementKinds::Value;
                }

                if (!e.enabled) {
                    // Disabled by OnAccept's own test (any non-Normal UIState), so the spoken
                    // state and the activation gate cannot disagree.
                    vt.Announcements.emplace_back(NodeAnnouncement::Static(Vocab::Disabled));
                    vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
                } else {
                    const int index = e.index;
                    vt.OnActivate = [index]() {
                        // The panel's own click path: selection first, then OnAccept — which
                        // owns the refusal sound. False means the call itself could not be
                        // made, and a pressed key must never be silent.
                        if (!GameUI::ActivateMouseControl(gScreen, index))
                            TTS::SpeakInterrupt(Vocab::Disabled);
                    };
                }

                // Label in the identity: slot 5 changes with the soldier's command abilities
                // and slot 6 appears with a Chosen — both must read as new nodes.
                b.AddItem(
                    ControlId::Structural("thud/cmd/" + std::to_string(e.index) + "/" + name),
                    std::move(vt));
            }
            b.EndRow();
            b.PopContext();
        }
    }

    // ---- Objectives: the panel that retains its words, through the Ctrl+X composers ----
    //
    // One composer per row (ObjectiveReader::DescribeLine) shared with the on-demand key, so
    // the two surfaces cannot disagree about a row — the audit rule's cheap corollary.
    GameUI::ObjectiveListView obj = GameUI::ReadObjectives();
    std::vector<GameUI::MissionCounter> counters = GameUI::ReadMissionCounters();
    std::vector<std::pair<std::string, std::string>> lines; // (id suffix, spoken text)
    if (obj.found) {
        int n = 0;
        for (const GameUI::ObjectiveLine& line : obj.lines) {
            if (line.title || !line.Speakable())
                continue;
            lines.emplace_back(std::to_string(n++) + "/" + line.label,
                ObjectiveReader::DescribeLine(line));
        }
    }
    for (size_t k = 0; k < counters.size(); k++)
        lines.emplace_back("ctr" + std::to_string(k) + "/" + counters[k].label,
            ObjectiveReader::DescribeMissionCounter(counters[k]));

    if (!lines.empty()) {
        anyNode = true;
        b.BeginStop("objectives");
        b.PushContext(obj.found && !obj.heading.empty() ? obj.heading
                                                        : std::string(Vocab::StopObjectives));
        for (const auto& [suffix, text] : lines)
            b.AddLabel(ControlId::Structural("thud/obj/" + suffix),
                [text = text]() { return text; });
        b.PopContext();
    }

    // A HUD with nothing readable yet (the first seconds of a mission, before the first
    // PopulateFlash) is not attachable — the navigator retries on its next poll.
    if (!anyNode)
        return nullptr;
    return b.Build();
}

std::string TacticalHudName()
{
    return Vocab::ScreenTacticalHud;
}

// The shot HUD raising is the game taking the arrows, Tab and F1-F8 back (targeting mode).
bool YieldTacticalFocus()
{
    return GameUI::ReadTacticalGate().menuRaised;
}

} // namespace

// Probed near the end of kRecipes: nothing it can share a stack with is below it there — every
// overlay that can appear during a mission (dialog, popup, pause menu, the promotion screen)
// resolves above, and the strategy screens can never probe ready while a tactical gate is.
const Recipe kTacticalHudRecipe{"tactical HUD", ProbeTacticalHud, BuildTacticalHud,
    TacticalHudName, /*SetListIndex*/ nullptr, /*ambient*/ true, YieldTacticalFocus};

} // namespace Recipes
} // namespace MenuNavigator
