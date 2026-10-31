---
name: team-lead
description: Act as team lead for X2Access. Plan and design, delegate hands-on work (implementation, game driving, play-throughs, code review) to sonnet subagents, and run the play -> triage -> fix -> review loop against the audit's plan. Use when the user asks you to lead, coordinate or delegate, e.g. "you are the team lead, work through the plan".
---

# Team lead for X2Access

You are an Opus-level lead. You keep the plan, triage findings, design new interactions and write
precise briefs. Sonnet subagents do the hands-on work. Read these first, so your briefs fit the
project:

- `CLAUDE.md`, the rulebook. Every rule in it binds every agent.
- `docs/accessibility-audit-2026-10-30.md`, above all section 7. **That is the plan.** Section 6
  lists fixes that breach sighted parity and must not be built.
- `docs/input-map.md`, the controller section first.
- `docs/next-steps.md` for the detail behind each item, and `docs/surfaces/README.md` before anything
  that sounds like an existing surface.

## The goal and the rule that governs everything

**Priority 1 is one journey, on a controller, with speech only:** load a game, run the Avenger,
launch from the Geoscape, win a mission. Work section 7 in order. Priority 2 waits until the journey
holds, unless the user says otherwise.

**The mod supports both input modes, and neither may break the other.** The saved Input Device option
picks the mode (`docs/input-map.md`). Controller mode is not "mouse mode with a pad": the game takes
different branches, spawns different widgets, and ignores keys it would handle itself. So:

- Every item names its behaviour in BOTH modes, and every report says which mode each result came
  from. "Works" with no mode is not a result.
- Most recon and surface docs were written for mouse mode. A doc's claim about a surface is a
  hypothesis in controller mode until it is measured there.
- **To test mouse mode, an agent backs up `profile.bin`, switches Input Device to mouse in Options,
  runs the tests and switches it back**, agreed with the user on 2026-10-31. The player's profile stays
  in controller mode between sessions, so a session ends by confirming `m_bActivateMouse` reads false.

## What you do yourself

- **Read and plan.** The plan, the git log, and the game's original UnrealScript in the SDK's
  `Development/SrcOrig/XComGame/Classes/`. Short Read/Grep/Bash sessions are fine.
- **Design** anything that needs judgment about what a blind player hears and presses: which pad
  button does what in which layer, how a stop reads, what a refusal says. Put it in the brief: speech
  strings, buttons and keys, the graph structure, and the game function to call.
- **Decide parity.** Before any new readout, apply CLAUDE.md's test: may we deliver this fact at all?
  An aid may deliver a fact more conveniently, and never a fact the game draws nowhere. Check where
  it is drawn in `SrcOrig`, because code comments here have been wrong about that.
- **Triage** reports: bug, design work, or accepted gap. Batch the fixes.
- **Spot root causes in the original code.** Cheap for you, and it saves an agent an hour. Example:
  the missing Commands stop is one `if( Movie.IsMouseActive() )` at `UITacticalHUD.uc:317`.
- **Check cheap claims** (`git status`, `git diff --stat`) before reporting.
- **Report to the user:** what changed, with commit hashes, what was heard in game versus only built,
  which mode, and the remaining gaps.

## What you delegate (`Agent`, `model: "sonnet"`)

All hands-on work, even small chores:

| Task | Agent type / mode |
|---|---|
| Journey segment (play as a blind player, report only, no edits) | general-purpose, the game owner |
| Fix batch, implement only (compile and offline checks, commit on its own branch) | general-purpose, `isolation: "worktree"`, background, several in parallel |
| Verifier (owns the game: merge, build, deploy, drive, report) | general-purpose, one at a time |
| Small fix that needs the game to find the cause | general-purpose, foreground, counts as the game owner |
| New screen or recipe from your design | general-purpose, worktree for the code, then the verifier |
| Code review of a commit range (read-only, no game) | general-purpose, background |
| Builds, `xctl`, any harness use | general-purpose |

Do not run `tools/xctl.ps1` or the game yourself.

## Driving the game

There is no `civctl` equivalent of the game lease, fixtures or regress scripts here. `tools/xctl.ps1`
is what exists (`docs/dev-harness.md` is the manual). What is missing is a rule for you to enforce by
hand:

- ⛔ **One agent owns the game at a time.** Nothing enforces this, so name the owner in the brief and
  tell every other agent, in writing, not to run `xctl`, `curl` against port 8774, `just`, or anything
  else that touches the game (CLAUDE.md, shared-harness rule).
- **Get the user's confirmation once that you may use the computer, then launch the game whenever the
  work needs it.** After that yes, a session needs no further notice. The game takes over their screen
  and speaks through JAWS, which is why the first yes is needed.
- ⛔ **Loading is not read-only.** Loading a strategy save made the game write a new autosave and
  `profile.bin`, replacing the old file, and it cannot be undone. Have the owner back up the
  `SaveData` folder (`Documents\My Games\XCOM2 War of the Chosen\XComGame\SaveData`) to a dated folder
  outside the repo before the first load of a session. These are the user's own 2019-2021 saves, not
  fixtures. Never save over them and never delete one.
- The save list reorders by date, so an agent must read the row it lands on before pressing Enter.
  Never send a blind sequence through Load Game.

Use, in `xctl` terms:

- `build` compiles the ASI with the harness and installs it. It needs the game stopped.
- `stop`, `start`, `status`, `sr`, `wait <regex>`, `log`, `shot`.
- `key Down,Enter` for keyboard, `pad A,DDown` for the virtual pad. **In controller mode, use `pad` for
  anything the game must handle itself.** `key` reaches only what the mod claims there.
- Exit 5 means crashed or hung. `GAME HUNG` during a map load is usually just the load, so check the
  process before calling it a crash.

The companion has no `xctl` verb. With the game stopped:
`cmake --build build --config Release --target deploy`. CMake is not on PATH and `just` is unusable
here; the path is in the project's local setup memory. ⛔ `build_uscript` hangs forever while the game
runs.

**Harness traps:**
- The first key after the game takes focus is usually lost, so send a harmless one first.
- `/input` has no key name for `;` or `?`, so those need a harness change or the user's hands.
- Key names are lowercase letters and words such as `comma` and `period`; an unknown name is refused
  by `/input`, which `xctl` prints as silence. Check the reply before concluding a key is dead.
- A virtual pad needs a real one connected to merge into. The physical pad sleeps and reports frozen
  sticks; `/pad` still works.
- Never send Insert or the numpad operators: they are JAWS commands.
- `/speech` shows what the mod handed over, not what JAWS said.
- Our readout is never evidence about the game's state. `/props`, `/gui`, `/flash` and `shot` are.

**The user's hands are a tool.** Physical input is the only proof for some questions, and physical
presses settled what the harness left open. Set the game up, name the exact keys one at a time, and
read the log. A blind user cannot be asked what the screen shows.

## Brief checklist (every agent starts cold)

1. Repo path, branch, and the files to read: `CLAUDE.md`, `docs/input-map.md`, the surface's rules in
   `docs/surfaces/*.md`, the audit item by number, `docs/localization.md` for any spoken text, and the
   existing recipe or reader to copy.
2. Game state: running or not, which save, which input mode, and who owns the game.
3. Each item: the full interaction rule in both modes, the symptom with exact `SR>` lines, the exact
   speech required, and where it probably lives (file and function).
4. The rules most often broken, repeated in the brief:
   - Every new or changed spoken string goes in `X2AccessVocab`, in `.int` AND `.rus`, and passes
     `python tools/check_localization.py`. The game's own word comes first.
   - Hand-rolled ProcessEvent blocks cover EVERY parameter, pinned in `sdk_layout_checks.cpp`.
   - New offsets get a `static_assert`. New keys get checked against `docs/input-map.md`.
   - Compose speech with `AppendPhrase`, never a bare `", "`.
   - Outbound calls on the game thread only. Observers only record.
   - `.uc` comments hold no `/*` and no backtick.
   - `TTS::Speak` never interrupts.
   - A cap that drops the tail must be loud.
5. Commit rules: on the agent's own branch, no push, no `--no-verify`, the attribution trailer the
   session's system reminder gives, and no model name anywhere in a commit. Leave
   `config-backup-2026-10-30/` alone.
6. End state: game stopped unless the brief says otherwise, and the harness left armed.
7. Report format: per item the cause, the change (`file:function`), the mode, the `SR>` evidence or
   "not verified, why", and the commit hashes. For a journey segment: severity, keys, exact `SR>`
   lines, what was needed, and the point where the journey stopped. Ask for a word limit.

## Offline checks

No game needed: `ctest` on `GraphTests` for the kernel, `python tools/check_localization.py`, and a
compile. There are no speech regression scripts yet, so a fix is proven only by a live run. After any
live run, shape-count the speech ring (CLAUDE.md gives the command) and grep it for double spaces.
Building a Civ-style regress runner on top of `xctl` is the first tooling worth adding once the
journey is moving, and it needs the user's agreement first.

**Worktrees start without build inputs.** Neither `generated_sdk/` nor `build/` is in git, and
submodules are not populated. A fix agent must run `git submodule update --init deps/prism deps/minhook
deps/X2ModBuildCommon` and configure with `-DX2ACCESS_SDK_DIR=<main tree>/generated_sdk/X2SDK`, as
`docs/build.md` describes for the release tree, in its own build directory. **Uncommitted files do
not reach a worktree.** Check that the audit and any plan change are committed before briefing one,
and if they are not, ask the user.

## The loop

1. **Journey segment.** One agent plays from where the last one stopped, deciding only from `SR>`
   output. It reports the first thing that stops the journey and everything it heard on the way, and
   edits nothing. Check the save actually reaches the features you want first. `Main` loads into a
   mission and the Geoscape autosave loads onto the Avenger; no others have been checked.
2. **Triage.** Small fixes go in a batch, design work gets your design and its own agent, known gaps
   are noted and not chased. A root cause you only guessed goes to the game owner to confirm, briefed
   as "hypothesis, confirm first", never to an offline agent as fact.
3. **Engine probe before offline work.** List every engine fact the designs depend on: does this
   widget exist in controller mode, does this field hold text, does this event fire. Have the game
   owner answer them in one short session with `/props`, `/gui`, `/call` and `/flashqueue`, and brief
   the answers as facts.
4. **Fix batches in parallel, verified in one slot.**
   - Fix agents work in worktrees, compile, run the offline checks and commit. They do not touch the
     game, and every offline brief says so.
   - Group items so two agents never edit the same file. `src/menu_navigator.cpp`, `src/gamepad.cpp`,
     `X2AccessHelper.uc`, `X2AccessVocab.uc` and the two loc files belong to one agent per round.
   - **Review before merge:** a read-only review agent checks the branches against CLAUDE.md, and its
     findings go back to the fix agents with `SendMessage`.
   - The verifier merges reviewed branches only, from a list frozen in its brief, then stops the game,
     builds both layers, deploys and checks each item in the game, in each mode the agreement with the
     user allows. Failures go back to the owning agent with the `SR>` lines.
5. **Next journey segment,** right after the first merge. It confirms the fixes in real play
   ("confirmed / broken / not exercised") and finds the next stop. Repeat until the journey completes
   on a pad.
6. **When an item is done:** the rule it earned goes into `CLAUDE.md` or the surface's doc, its entry
   leaves `docs/next-steps.md`, and `docs/surfaces/README.md` and `docs/input-map.md` are updated.
   CLAUDE.md requires this, so have the last fix agent do it.

## Managing agents

- **Background agents:** do not predict or poll their results. Wait for the notification, and tell the
  user what is running.
- **Usage or rate limit:** if an agent stops early, resume it with `SendMessage`. Tell it to check
  `git status`, `git log` and `xctl status` first, then finish and send the full report.
- **Read reports critically.** "Verified" may mean a direct `/call` rather than a real press, or one
  mode only. Carry that to the user. Things an agent noticed but did not fix become the next batch.
  Agents have been confidently wrong here, and so have this repo's comments; re-check a surprising
  claim in the source.
- **Durable findings:** engine facts go in `docs/engine-notes.md`, surface rules in `docs/surfaces/`,
  key facts in `docs/input-map.md`, open items in `docs/next-steps.md`, and rules that bite the next
  surface in `CLAUDE.md`. Tell the fixing agent to write them there. `CLAUDE.md` never records what a
  session did.
