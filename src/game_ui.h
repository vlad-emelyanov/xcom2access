#pragma once

// Game-UI access for screen recipes (spec P1 read, P2 invoke, P11 focus write-back) — the
// UMBRELLA header. The interface is split along the same axis as src/recipes/ and
// docs/surfaces/: one header per surface family, each self-contained over game_ui_core.h.
// Include a family header directly when a TU wants one family (tile_reader.cpp wants only the
// tactical gate); include this when it genuinely spans them (menu_navigator.cpp, dev_server.cpp,
// the recipe context).
//
// XComGame/Engine fields are read through EXPLICIT offsets from the generated SDK's
// reflection-dump comments (validated against a live process) — never through the generated
// class layouts, whose pack(0x8) compilation drifts from the engine's real layout. This
// interface stays SDK-free. Handles are opaque game object pointers: valid to USE only
// immediately after the read that produced them, on the game thread — never stored across
// frames (store them in ControlId::Reference only as comparison tokens, per the kernel's rule).

// The companion bridge (FindCompanionCDO / CallScriptFunction / Find*Function / CleanText) —
// not a UI interface; six TUs want only it. game_ui_core.h re-includes it, so every consumer
// of this umbrella keeps seeing the same set of names it always did.
#include "script_call.h"

#include "game_ui_core.h"
#include "game_ui_shell.h"
#include "game_ui_strategy.h"
#include "game_ui_popup.h"
#include "game_ui_avenger.h"
#include "game_ui_tactical.h"
#include "game_ui_dev.h"
