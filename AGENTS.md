# X2Access agent rules

Read `CLAUDE.md` for the project rulebook and `docs/localization.md` for the localization contract.

- Every new or changed mod-owned player-facing string, in UnrealScript or C++, must use `X2AccessVocab` with English (`.int`) and Russian (`.rus`) entries. Check for an existing game-localized word first; reuse it when its meaning matches. Keep developer-only diagnostics out of speech.
- Give each spoken concept one source: read a game-owned label from its screen or game localization, or translate a mod-owned label in `X2AccessVocab`. Do not copy a game-owned label into mod localization as a fallback; an empty live screen read may use the game's localized class default.
- Do not put English speech defaults in C++ `Vocab::Word` or player-facing UnrealScript error paths. A missing localization value is logged and left empty; add and translate its key instead of supplying a code fallback.
