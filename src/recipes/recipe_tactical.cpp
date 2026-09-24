// The TACTICAL family's recipes: the ambient HUD graph, and the two mission bookends that open
// the instant a mission ends (docs/mission-summary-recon.md — the second half of this file).
//
// ---- the tactical HUD recipe — the mod's first AMBIENT recipe (plan phase 3, the HUD graph).
//
// Everything about how this recipe holds input is INVERTED from the menu recipes, and the
// Recipe::ambient flag is what expresses it: it attaches SILENTLY the moment a mission's HUD
// settles and then sits UNFOCUSED for most of the mission, while the tile cursor keeps the
// arrows and the game switches soldiers on E / Q (the companion's XComInput.ini moves the vanilla
// Tab / LeftShift commands there, which is what frees Tab). Tab focuses it (HUD mode — the
// navigator's claims come alive and arrows walk the stops); Escape hands the keys back; raising the
// shot HUD takes them back without asking (YieldFocus — targeting owns the arrows, and the
// targeting poll's narration speaks next, so the yield is silent).
//
// The stops mirror what is ON SCREEN, in the parity-audited order the plan fixed: SOLDIER (the
// bottom-left panel plus the weapon/ammo readout — companion-backed, because those panels are
// write-only pipes into Flash and the facts live only in XCOMHISTORY), ABILITIES (pure widget
// read), COMMANDS (the mouse-controls strip, the one other tactical panel that RETAINS its
// words — label, bound key and state per button), TARGETS (the enemy strip — companion-backed
// off the strip's own arrays, every mark a head carries plus the flag's cover and health beside
// it), OBJECTIVES (retained widget data through the Ctrl+X composers).
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

#include <cstdint>
#include <cstdlib>
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

        // The full soldier sheet (UITacticalCharInfoScreen — game_ui_tactical.h). The game opens
        // it from the gamepad's L3 and from nothing else; its HUD hint is spawned in the
        // controller branch alone. Re-provided as a button named with that hint's own label, and
        // offered only while the shot HUD is down — the same gate CanCharInfoShow applies. The
        // companion reproduces Stick_L3's body.
        if (!gate.menuRaised) {
            NodeVtable vt;
            vt.Type = &ButtonType();
            std::string label = GameUI::ReadCharInfoHint(gScreen);
            if (label.empty())
                label = Vocab::SoldierInfoFallback();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(label));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;
            vt.OnActivate = []() {
                // A verb, not a read: the answer is the screen appearing, which the next poll
                // attaches to. The helper logs a missing companion itself.
                FetchCompanionRecords("X2AccessCharInfoReader", "OpenCharInfo");
            };
            b.AddItem(ControlId::Structural("thud/sol/" + oid + "/info"), std::move(vt));
        }
        b.PopContext();
    }

    // ---- Abilities: one node per live bar icon, a pure widget read (T2) ----
    if (!bar.entries.empty()) {
        anyNode = true;
        b.BeginStop("abilities");
        b.PushContext(Vocab::StopAbilities());
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
                        TTS::Speak(Vocab::Disabled);
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
                            TTS::Speak(Vocab::Disabled);
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

    // ---- Targets: the enemy strip (UITacticalHUD_Enemies), one node per head ----
    //
    // Companion-backed, read off the strip's OWN arrays — m_arrTargets in the strip's order
    // (sorted by hit chance, best shot leftmost), m_arrCurrentlyAffectable, m_arrSSEnemies,
    // CurrentTargetIndex — so a head here is exactly a head on screen, greyed where the game
    // greys it. Per head: the glyph's name, the colour's word (civilian / ally), the hover hit
    // chance, the flanked marker and the flag's cover and health, the squadsight badge, the two
    // buff/debuff badges as the effect names their icons carry, the targeted highlight, and the
    // one AID the plan grants this stop, distance and bearing in the scanner's words. The
    // composers are X2AccessTacticalReader's (BuildEnemyRecords), the same ones the reticle and
    // the scanner's ring read through, so no two of the three can disagree about a target.
    //
    // HORIZONTAL like the strip. The targeted head carries the Selected part, which is also what
    // lands the stop on it; a strip with no current target lands on the best shot, the head the
    // game's own first click would pick. Built whether or not the shot HUD is raised — raised,
    // the layer is yielding focus anyway, and the strip is still the strip.
    //
    // Enter is the strip's own click (SelectEnemyByIndex → DirectTargetObject), keyed on the
    // OBJECT because the strip re-sorts on every refresh. Offered on a greyed head too, as the
    // mouse is: the game answers with silence there, so the companion answers for it.
    std::vector<WireRecord> heads
        = FetchCompanionRecords("X2AccessTacticalReader", "BuildEnemyRecords");
    if (!heads.empty()) {
        anyNode = true;
        b.BeginStop("targets");
        b.PushContext(Vocab::StopTargets);
        b.StartRow("targets");
        for (const WireRecord& rec : heads) {
            const std::string oid = RecField(rec, "oid");
            std::string name = RecField(rec, "name");
            if (name.empty())
                name = Vocab::TargetFallback;

            NodeVtable vt;
            vt.Type = &ButtonType();
            vt.Announcements.emplace_back(NodeAnnouncement::Static(name));
            vt.Announcements.back().Kind = AnnouncementKinds::Label;

            // The strip's marks and the flag's facts, in the order a sighted eye takes them:
            // what it is, the number on it, how exposed it is, how hurt, then the badges, then
            // the aid last, labelled by its own words ("7 tiles, 3 north, 2 east").
            const std::string value = JoinParts(rec,
                {"team", "hit", "cover", "health", "sight", "bonus", "penalty", "dist", "bearing"});
            if (!value.empty()) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(value));
                vt.Announcements.back().Kind = AnnouncementKinds::Value;
            }

            // The greyed icon, in the companion's own state word rather than the generic one:
            // "not targetable" says why the head is dim — the selected ability cannot reach it.
            const std::string blocked = RecField(rec, "blocked");
            if (!blocked.empty()) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(blocked));
                vt.Announcements.back().Kind = AnnouncementKinds::Enabled;
            }

            // ⚠ Selected is also the LANDING part (GraphTypes.hpp) — deliberately so here: the
            // stop genuinely has a current item, the head the reticle is on.
            const std::string targeted = RecField(rec, "targeted");
            if (!targeted.empty()) {
                vt.Announcements.emplace_back(NodeAnnouncement::Static(targeted));
                vt.Announcements.back().Kind = AnnouncementKinds::Selected;
            }

            const int objectId = atoi(oid.c_str());
            vt.OnActivate = [objectId]() {
                // The companion speaks only refusals ("No longer visible", "Unavailable");
                // success is silent here because targeting raising is what speaks next, through
                // the targeting poll, and the navigator yields focus on its next tick.
                struct {
                    int32_t ObjectID;
                } params{objectId};
                std::string answer;
                if (!GameUI::CallCompanion("X2AccessTacticalReader", "SelectEnemyHead", &params,
                        &answer)) {
                    TTS::Speak(Vocab::Disabled);
                    return;
                }
                if (!answer.empty())
                    TTS::Speak(GameUI::CleanText(answer));
            };

            // The ObjectID is the identity: a new enemy is a new node wherever the sort puts it,
            // and a changed hit chance on the same one is a value change under a held identity.
            b.AddItem(ControlId::Structural("thud/tgt/" + oid), std::move(vt));
        }
        b.EndRow();
        b.PopContext();
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
                                                        : std::string(Vocab::StopObjectives()));
        for (const auto& [suffix, text] : lines)
            b.AddLabel(ControlId::Structural("thud/obj/" + suffix),
                [text = text]() { return text; });
        b.PopContext();
    }

    // The Legacy Pack's score (and a challenge's countdown), trailing, only on those missions.
    if (AddTLEHudStop(b))
        anyNode = true;

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

// ---- the mission bookends (UIMissionSummary / UIChosenMissionSummary) ----
//
// docs/mission-summary-recon.md. The two windows that open the instant a mission ends, ahead of
// the after-action walk-up — until now the only part of the post-mission chain with no reader at
// all, and the first thing a blind player meets after a fight they just won. Words from the
// Flash capture; the reading is entirely GameUI::ReadMissionSummary / ReadChosenSummary.
//
// ⛔ ONLY THE BUTTON NODES ACTIVATE, and this is the deliberate difference from the research
// deck, whose every node advances. The game's own OnUnrealCommand does answer Enter anywhere
// with Continue on both screens — but unlike the deck these windows carry TWO verbs (Continue
// and the photobooth hotlink), so "Enter means the first one, wherever you are" would be a
// guess. Continue also COMMITS the map unload and cannot be taken back, which is not something
// to hand to a stray keypress while the player is reading the rating.
//
// Escape is consumed by both screens by design ("so there is no UI functionality in Mission
// Summary" — UIMissionSummary.uc:650), so the layer offers no close verb either: Continue is the
// only way out, exactly as for a sighted player.

// One label/value row as a node. The label carries the game's own trailing ':' and the two are
// joined by the announcer's phrase rule, never by us — see AppendPhrase. A row whose label is
// missing degrades to the value alone rather than to a Value announcement with nothing to label.
// Returns an INVALID id when nothing was added, so callers never have to duplicate the test.
ControlId AddStatNode(GraphBuilder& b, const std::string& id, const GameUI::SummaryStat& stat)
{
    if (stat.label.empty() && stat.value.empty())
        return ControlId();
    NodeVtable vt;
    vt.Announcements.emplace_back(
        NodeAnnouncement::Static(stat.label.empty() ? stat.value : stat.label));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;
    if (!stat.label.empty() && !stat.value.empty()) {
        vt.Announcements.emplace_back(NodeAnnouncement::Static(stat.value));
        vt.Announcements.back().Kind = AnnouncementKinds::Value;
    }
    ControlId node = ControlId::Structural(id);
    b.AddItem(node, std::move(vt));
    return node;
}

// A read-only line. Returns the id so a caller can hand it to SetStart; invalid when the text
// was empty, because an unlabelled node is a ghost the player can land on and hear nothing from.
ControlId AddLine(GraphBuilder& b, const std::string& id, const std::string& text)
{
    if (text.empty())
        return ControlId();
    ControlId node = ControlId::Structural(id);
    b.AddLabel(node, [text]() { return text; });
    return node;
}

// The Continue node both screens end on. `label` is the game's own where it has one.
void AddContinueStop(GraphBuilder& b, const std::string& key, const std::string& label)
{
    b.BeginStop("actions");
    NodeVtable vt;
    vt.Type = &ButtonType();
    vt.Announcements.emplace_back(
        NodeAnnouncement::Static(label.empty() ? Vocab::SummaryContinueFallback() : label));
    vt.Announcements.back().Kind = AnnouncementKinds::Label;
    // The screen's own INPUT path, not its handler: OnUnrealCommand is where the auto-photo,
    // the camera fade and bWaitingForMissionSummary all sequence from (the hard rule about
    // preferring the game's path, and about it meaning the input path).
    vt.OnActivate = []() { GameUI::SendScreenCommand(gScreen, GameUI::kCmdEnter); };
    b.AddItem(ControlId::Structural(key + "continue"), std::move(vt));
}

std::unique_ptr<GraphRender> BuildMissionSummary()
{
    const GameUI::MissionSummaryView view = GameUI::ReadMissionSummary(gScreen);

    GraphBuilder b;
    b.BeginStop("summary");
    // Empty-label context with positions suppressed, the research report's arrangement and for
    // its reason: this is a document, and stamping "3 of 9" onto "TURNS TAKEN: 12" would read
    // as part of the statistic.
    b.PushContext("", "", /*positions*/ false);

    // The result is also the screen name, so it is NOT the landing node — but it IS in the
    // graph, which is the deliberate departure from the report recipe. There the header was a
    // generic "RESEARCH REPORT" and losing it cost nothing; here it is whether the player won,
    // and it has to stay re-readable after they have arrowed down to the rating.
    AddLine(b, "missionsummary/result", view.result);
    AddLine(b, "missionsummary/op", view.opName);

    ControlId firstStat;
    for (const GameUI::SummaryStat& stat : view.stats) {
        // ⛔ NAMED ids, never the loop index: a pair the screen drew nothing for is dropped by
        // the reader, and a numbered id would silently renumber every row after it — the
        // empty-answer trap in its positional form.
        ControlId node = AddStatNode(b, std::string("missionsummary/stat/") + stat.id, stat);
        if (node.IsValid() && !firstStat.IsValid())
            firstStat = node;
    }

    if (view.result.empty() && view.opName.empty() && view.stats.empty()) {
        // Recognised the screen and captured nothing. Said out loud for PopupNoText's reason —
        // the player still has to press Continue to leave, and silence here is indistinguishable
        // from the mission having ended into nothing at all.
        AddLine(b, "missionsummary/empty", Vocab::SummaryNoText);
    } else if (firstStat.IsValid()) {
        // Land past the two headline lines: both are already in the screen name, and repeating
        // one of them as the landing announcement would say it twice in a row.
        b.SetStart(firstStat);
    }
    b.PopContext();

    AddContinueStop(b, "missionsummary/", view.continueLabel);

    // The photobooth hotlink. Vanilla reaches it by mouse click or the gamepad's Y and by no
    // keyboard route at all — but it is ON SCREEN, which is what decides (sighted parity), and
    // ReadButtonField has already answered "absent, hidden or unlabelled" in one: Hide() is
    // exactly how the game suppresses it in multiplayer and after a total squad wipe.
    //
    // ⚠ It opens UIPhotographerScreen, which has no reader — the same deliberate hole the
    // after-action screen's Memorialize button has, and the same answer: a sighted player can
    // walk into it too, so the node exists and the screen behind it is a separate entry.
    if (view.photobooth.handle) {
        NodeVtable vt;
        vt.Type = &ButtonType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(view.photobooth.label));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        // The screen's own zero-parameter verb — the one the button's OnClickedDelegate wraps,
        // so the camera-fade release and the particle cleanup it does first both still run.
        vt.OnActivate = []() {
            GameUI::InvokeScreenVerb(gScreen, "CloseThenOpenPhotographerScreen");
        };
        b.AddItem(ControlId::Structural("missionsummary/photobooth"), std::move(vt));
    }

    return b.Build();
}

std::string MissionSummaryName()
{
    // The headline a sighted player takes in at a glance: the result, then the operation's own
    // name — the same shape AfterActionName uses one screen later, so the two bookends of the
    // post-mission chain announce themselves alike.
    const GameUI::MissionSummaryView view = GameUI::ReadMissionSummary(gScreen);
    const std::string name = JoinPhrases(view.result, view.opName);
    return name.empty() ? std::string(Vocab::ScreenMissionSummaryFallback) : name;
}

std::unique_ptr<GraphRender> BuildChosenSummary()
{
    const GameUI::ChosenSummaryView view = GameUI::ReadChosenSummary(gScreen);

    GraphBuilder b;
    b.BeginStop("chosen");
    b.PushContext("", "", /*positions*/ false);

    int added = 0;
    auto line = [&](const std::string& id, const std::string& text) {
        if (AddLine(b, "chosensummary/" + id, text).IsValid())
            added++;
    };
    auto stat = [&](const GameUI::SummaryStat& s) {
        if (AddStatNode(b, std::string("chosensummary/") + s.id, s).IsValid())
            added++;
    };

    // The title is the screen name and is not repeated here; the subtitle ships empty in every
    // localization we have, so it is read rather than assumed absent.
    line("subtitle", view.subtitle);
    line("identity", view.identity);
    // Only when the Chosen actually died on this mission — the badge is "" otherwise, and an
    // icon in its unlit state gets no words.
    line("defeated", view.defeated);
    for (size_t i = 0; i < view.flavorLines.size(); i++)
        line("flavor/" + std::to_string(i), view.flavorLines[i]);
    stat(view.strengths);
    stat(view.weaknesses);

    // Exactly one outcome block, or none. ⛔ SetChosenReward is pushed with three EMPTY strings
    // on the plain-encounter branch, so it is the emptiness that decides, never the call's
    // presence — which is why the reader hands these over as plain fields to test.
    stat(view.reward);
    if (!view.knowledge.label.empty() || !view.knowledge.value.empty()) {
        GameUI::SummaryStat knowledge = view.knowledge;
        if (view.knowledgeBefore >= 0 && view.knowledgeAfter >= 0) {
            // The bar's two ends are the block's whole content; the wording is ours because the
            // game draws the change rather than saying it.
            knowledge.value = JoinPhrases(
                Vocab::KnowledgeChange(view.knowledgeBefore, view.knowledgeAfter),
                knowledge.value);
        }
        stat(knowledge);
    }
    line("captured", view.capturedSoldier);

    if (added == 0)
        line("empty", Vocab::SummaryNoText);
    b.PopContext();

    // The Chosen screen has no m_strContinue of its own — its Continue is a UINavigationHelp
    // widget whose label lives in the movie — so this one really does fall back to our word.
    AddContinueStop(b, "chosensummary/", std::string());

    return b.Build();
}

std::string ChosenSummaryName()
{
    const GameUI::ChosenSummaryView view = GameUI::ReadChosenSummary(gScreen);
    return view.title.empty() ? std::string(Vocab::ScreenChosenSummaryFallback()) : view.title;
}

// ---- the loot receipt (UIInventoryTactical) ----
//
// game_ui_tactical.h for what it is — a receipt the visualizer is waiting on, not a picker — and
// what it retains. TWO STOPS: the items under the corpse or crate's own name, then OK. Space on an
// item reads the hover description a mouse player gets; nothing else on this screen can be
// pressed, and every key the game handles is OK anyway.

constexpr const char* kLootReaderClass = "X2AccessLootReader";

// Keyed on the rows and the screen: UpdateData runs at InitScreen and once more on the way out,
// so the rows are fixed for the screen's life.
WireCache gLootWords(kLootReaderClass, "DescribeLoot");

const std::vector<WireRecord>& FetchLootWords(const GameUI::LootView& view)
{
    return FetchWordsKeyedOnRows(gLootWords, view.screen, view.rows);
}

const WireRecord* FindLootRow(const std::vector<WireRecord>& records, int index)
{
    for (const WireRecord& rec : records) {
        if (RecField(rec, "kind") == "row" && atoi(RecField(rec, "index").c_str()) == index)
            return &rec;
    }
    return nullptr;
}

std::unique_ptr<GraphRender> BuildLoot()
{
    const GameUI::LootView view = GameUI::ReadLoot(gScreen);
    const std::vector<WireRecord>& words = FetchLootWords(view);
    const WireRecord* header = FindRecord(words, "header");

    GraphBuilder b;
    b.BeginStop("loot");
    // The panel's own header line: what the loot came off — a corpse, a crate, a chest.
    b.PushContext(header ? RecField(*header, "source") : std::string(), "", /*positions*/ true);
    int shown = 0;
    for (const GameUI::LootRow& row : view.rows) {
        const WireRecord* rec = FindLootRow(words, row.index);
        std::string name = rec ? RecField(*rec, "name") : std::string();
        if (name.empty())
            name = Vocab::SellItemFallback;
        NodeVtable vt;
        vt.Type = &TextType();
        vt.Announcements.emplace_back(NodeAnnouncement::Static(name));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        // The description is a mouse-following tooltip in vanilla; here it is the tooltip key.
        const std::string desc = rec ? RecField(*rec, "desc") : std::string();
        if (!desc.empty())
            vt.OnTooltip = [desc]() { TTS::Speak(desc); };
        b.AddItem(ControlId::Referenced(row.handle, "loot/row/" + std::to_string(row.index)),
            std::move(vt));
        shown++;
    }
    if (shown == 0) {
        // UpdateData draws a row only for items that reached the soldier's inventory; none is a
        // legitimate state, and the screen still has to be dismissed.
        b.AddLabel(
            ControlId::Structural("loot/empty"), []() { return std::string(Vocab::LootEmpty); });
    }
    b.PopContext();

    b.BeginStop("actions");
    NodeVtable ok;
    ok.Type = &ButtonType();
    ok.Announcements.emplace_back(NodeAnnouncement::Static(
        view.okLabel.empty() ? std::string(Vocab::LootOkFallback()) : view.okLabel));
    ok.Announcements.back().Kind = AnnouncementKinds::Label;
    ok.OnActivate = []() { GameUI::CloseLoot(GameUI::ReadLoot(gScreen)); };
    b.AddItem(ControlId::Structural("loot/ok"), std::move(ok));
    return b.Build();
}

std::string LootName()
{
    // The title bar as the screen composes it: LOOT RECOVERY, then whose. The title is read
    // natively as the fallback for the window before the companion answers.
    const GameUI::LootView view = GameUI::ReadLoot(gScreen);
    const WireRecord* header = FindRecord(FetchLootWords(view), "header");
    std::string name = header ? RecField(*header, "title") : std::string();
    if (name.empty())
        name = view.title;
    AppendPhrase(name, header ? RecField(*header, "looter") : std::string());
    return name.empty() ? std::string(Vocab::ScreenLootFallback) : name;
}

// ---- soldier info (UITacticalCharInfoScreen) ----
//
// game_ui_tactical.h: the one tactical screen that retains everything, so this is a pure widget
// read with no companion. TWO STOPS: the sheet's head — name, class, the seven stats as
// label/value nodes — then the effects, with the game's own PENALTIES / BONUSES / PASSIVES headings
// as contexts and one node per effect carrying its description. Nothing activates; Escape is the
// game's own close.

std::unique_ptr<GraphRender> BuildCharInfo()
{
    const GameUI::CharInfoView view = GameUI::ReadCharInfo(gScreen);
    if (!view.valid)
        return nullptr; // OnInit has not built the sheet yet — treated as "closed" until it has

    GraphBuilder b;
    ControlId start;

    b.BeginStop("soldier");
    b.PushContext("", "", /*positions*/ false);
    // The name is the screen name too, and stays in the graph re-readable, as the mission
    // summary's result does.
    start = AddLine(b, "charinfo/name", JoinPhrases(view.name, view.className));
    for (const GameUI::CharInfoStat& stat : view.stats) {
        // The screen draws "---" for a zero (PopulateStatValues: "does not display a 0"); the
        // dashes are its marker, not its words.
        GameUI::SummaryStat line;
        line.label = stat.label;
        line.value = stat.value == "---" ? std::string(Vocab::CharInfoNoValue) : stat.value;
        // Keyed on the label: the slots are the FLA's fixed seven, so the caption is the stable
        // identity and a slot the screen left blank drops out without renumbering the rest.
        ControlId node = AddStatNode(b, "charinfo/stat/" + stat.label, line);
        if (node.IsValid() && !start.IsValid())
            start = node;
    }
    b.PopContext();

    // The effects. AddEffectsToList emits a heading only for a category with something in it,
    // so the headings are the game's own grouping, and an empty category is simply absent.
    bool any = false;
    bool context = false;
    for (const GameUI::CharInfoEntry& e : view.entries) {
        if (!any) {
            b.BeginStop("effects");
            any = true;
        }
        if (e.header) {
            if (context)
                b.PopContext();
            b.PushContext(e.text, "", /*positions*/ true);
            context = true;
            continue;
        }
        NodeVtable vt;
        vt.Type = &TextType();
        const std::string name = e.text.empty() ? Vocab::AbilityFallback(e.index) : e.text;
        vt.Announcements.emplace_back(NodeAnnouncement::Static(name));
        vt.Announcements.back().Kind = AnnouncementKinds::Label;
        if (!e.description.empty()) {
            vt.Announcements.emplace_back(NodeAnnouncement::Static(e.description));
            vt.Announcements.back().Kind = AnnouncementKinds::Value;
        }
        // Name in the identity: the sheet is rebuilt per soldier, and a different soldier's
        // effect at the same index must read as a new node.
        b.AddItem(ControlId::Referenced(
                      e.handle, "charinfo/effect/" + std::to_string(e.index) + "/" + name),
            std::move(vt));
    }
    if (context)
        b.PopContext();

    if (start.IsValid())
        b.SetStart(std::move(start));
    return b.Build();
}

std::string CharInfoName()
{
    const GameUI::CharInfoView view = GameUI::ReadCharInfo(gScreen);
    const std::string name = JoinPhrases(view.name, view.className);
    return name.empty() ? std::string(Vocab::ScreenCharInfoFallback()) : name;
}

} // namespace

// Probed near the end of kRecipes: nothing it can share a stack with is below it there — every
// overlay that can appear during a mission (dialog, popup, pause menu, the promotion screen)
// resolves above, and the strategy screens can never probe ready while a tactical gate is.
const Recipe kTacticalHudRecipe{"tactical HUD", ProbeTacticalHud, BuildTacticalHud,
    TacticalHudName, /*SetListIndex*/ nullptr, /*ambient*/ true, YieldTacticalFocus,
    /*ambientExitName*/ &Vocab::TacticalWorldMode};

// ⛔ ABOVE the ambient tactical recipe, both of them. They are pushed over UITacticalHUD while
// the map is still loaded, and the gate already treats them the way this needs — an unknown
// screen above the HUD raises `blocked`, which is exactly what kTacticalPassives' own comment
// says about the mission summary. Below every modal a mission can still raise (the pause menu
// answers Start on both screens), all of which resolve earlier in kRecipes. No SetListIndex on
// either: neither screen has a UIList, and the mission summary ends OnInit with Navigator.Clear()
// — there is no navigable widget for focus write-back to write to.
//
// Their order relative to EACH OTHER is free: the Chosen screen pops itself before pushing the
// mission summary, so the two are sequential and never stacked.
const Recipe kChosenSummaryRecipe{"chosen summary",
    ProbeFocused<GameUI::FindChosenSummary>, BuildChosenSummary, ChosenSummaryName};

const Recipe kMissionSummaryRecipe{"mission summary",
    ProbeFocused<GameUI::FindMissionSummary>, BuildMissionSummary, MissionSummaryName};

// ⛔ ABOVE the ambient tactical recipe, as the bookends are: both are pushed over UITacticalHUD,
// and the gate raises `blocked` for either — which is what detaches the HUD layer and lets these
// probe. Neither raises anything but the pause menu (listed earlier), and the two never share a
// stack with each other or with the bookends. No SetListIndex on either: the loot list is
// reachable by no key at all, and the sheet's list has a selection nothing reads.
const Recipe kLootRecipe{"loot", ProbeFocused<GameUI::FindLootScreen>, BuildLoot, LootName};

const Recipe kCharInfoRecipe{"soldier info", ProbeFocused<GameUI::FindCharInfoScreen>,
    BuildCharInfo, CharInfoName};

} // namespace Recipes
} // namespace MenuNavigator
