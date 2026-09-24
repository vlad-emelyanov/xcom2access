# X2Access agent rules

Read `CLAUDE.md` for the project rulebook and `docs/localization.md` for the localization contract.

- Every new or changed player-facing mod string, in UnrealScript or C++, must use the native XCOM 2 localization system through `X2AccessVocab` and have both English (`.int`) and Russian (`.rus`) entries. Check for an existing game-localized word first; reuse it when its meaning matches. Keep developer-only diagnostics out of speech.
