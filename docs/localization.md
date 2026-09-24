# Localization — how the mod's own words reach the player in their language

The game is localized; the mod speaks on top of it. Two kinds of word reach a player, and the
whole design is about keeping them apart:

- **The game's words** — a cover type, a hit result, an ability name, a unit's name, an objective
  sentence, a status effect. These are read off the template, the screen or the state object that
  owns them (`X2CharacterTemplate.strCharacterName`, `X2AbilityTemplate.LocFriendlyName`,
  `class'UITacticalHUD_Countdown'.default.m_strReinforcementsTitle` …). They are never copied into
  a file of ours. Before adding a word, look for the game's first — the tutorial marker's "Move"
  turned out to be `StandardMove`'s own `LocFriendlyName`, and the lone Lost is simply the unit's
  name.
- **Our words** — what the game has nowhere: the tile cursor's compass, the scanner's category
  names, "Press again to move". These live on **one class, `X2AccessVocab`**, as `localized` vars,
  and in **`mods/X2AccessCompanion/Localization/X2AccessCompanion.<lang>`** under `[X2AccessVocab]`.

## The contract

- ⛔ **One vocabulary class.** Every `localized` var the companion owns is declared on
  `X2AccessVocab` and nowhere else. A `localized` var declared on two classes and populated on one
  fails silently (`engine-notes.md`, the double-space tell); one class means one section in the
  file and one place to check. Readers use `class'X2AccessVocab'.default.<Key>`; the generator's
  `ChosenADGeneratorName` moved here from the scanner for exactly this reason.
- **The `.int` file is the reference.** Every key is in it, with a
  comment where the wording has a reason. A key missing from a language file reads as `""`, and
  every reader treats `""` as *say nothing* — so a missing key is a silent gap, not an English
  fallback. (The engine has no per-key fallback to INT for a mod package; the whole-file fallback
  is what happens when the language file is absent.) Translate against the `.int`, and after a
  live run in another language shape-count the speech ring: an English word inside a foreign line
  is the tell (CLAUDE.md, hard rules).
- **Tracked files are UTF-8; the engine reads UTF-16.** X2ModBuildCommon's `_ConvertLocalization`
  rewrites everything under `Localization/` to UTF-16 LE with a BOM in staging (verified: the
  deployed files start `FF FE`). Never commit a UTF-16 file and never hand-convert.
- ⛔ **A mod loc file cannot override a key that vanilla already ships.** A file named after a map
  package (`sm_ChosenBulwark_01.rus`) did nothing (tried 2026-09-17, ten files, restart, heard
  unchanged), and none of the 40 workshop mods shipping an `XComGame.int` redefines a vanilla key —
  only ADDING keys is evidenced. So a wrong game word (the "Chosen Sarcophagus" turrets) is fixed by
  choosing a different game word or, last, a word of ours, never by overriding.
- **A word that is a developer's, not a player's, stays an English literal**: `"Bad category"`,
  `"Bad direction"`, the `` `log `` lines. They are unreachable by a player or read only by us.

## Word order: vanilla's tags

A line with a number or a name in it is a **template** carrying the game's own expansion tags —
`<XGParam:IntValue0/>`, `<XGParam:IntValue1/>`, `<XGParam:IntValue2/>`, `<XGParam:StrValue0/>` — and
`X2AccessVocab.Fill1/Fill2/Fill3/FillS` expand it through `XGParamTag` and `XEXPAND.ExpandString`,
exactly as `UIArmory` and `SeqAct_ExpandLocalizedString` do. The translator moves the tag; the
code never concatenates a number onto a word. `Expand` writes every slot (unused ones cleared), so
a stale value from the game's own last expansion can never leak into a line of ours. A template
with no tags comes back unchanged.

Where a direction has to be DECLINED rather than inserted (cover: "с севера"), the key is per
direction — `FullCoverNorth` … `HalfCoverWest` — rather than a template over a compass word.
The Russian phrases use the game's "Высокое укрытие" / "Низкое укрытие" terminology from
`XLocalizedData.TargetHighCover` / `TargetLowCover`; the target readout reads those fields directly.
Bearings (`OffsetNorth` = "<N> north") are templates because the number is the variable part.

## Plurals: the rule vanilla does not have

Vanilla has 393 `Singular`/`Plural` key pairs chosen by `== 1` (an English split), abbreviations
("очк.", "дн."), hedges ("очко(а)") and accepted errors ("в течение 2 дней"). Ours:

- A counted noun is **three keys**, `<Name>_One`, `<Name>_Few`, `<Name>_Many`, each a template over
  `IntValue0`; `X2AccessVocab.Count(N, One, Few, Many)` picks and fills.
- **`PluralRule`** — one localized key per language file, not per word — selects the rule. `""` or
  `"english"`: One for exactly 1, Many otherwise. `"slavic"`: the last-two-digits rule (1, 21, 31
  → One; 2-4, 22-24 → Few; 5-20, 25-30 and every 11-14 → Many). English cannot use the Slavic
  rule — "21 tile" — which is why the rule is a key and not a heuristic on whether Few is filled.
- **Fallbacks**: an empty form falls to Many; an empty Many falls to One. A half-filled file still
  speaks, wrongly inflected rather than silently.
- Native `Vocab::CountForm` uses the same `PluralRule` loaded from the CDO. Number phrases use
  `{0}` and `{1}` in the native word table; translators may move the placeholders.

## Native

`Vocab::LoadFromGame` reads the same values off the `X2AccessVocab` CDO by property name on the
game tick, caches the strings and never retains the CDO pointer. `Vocab::Word` resolves the cache
without reading engine memory, including from UI callbacks. `src/vocab_words.inc` is the list of
native property names. A missing key falls back to its English literal; this is diagnostic debt,
not a translated line. The Graph kernel remains STL-only. Startup success is spoken after the
vocabulary loads; a hook-initialization failure may still use English because the game localization
system is unavailable at that point.

Run `python tools/check_localization.py` after changing any word. It checks that the class, INT,
RUS, and native word index agree, that keys are unique, and that Russian templates retain every
placeholder. The installed game's `XComGame.int/.rus` were checked for exact matching terms;
screen and template fields are preferred at runtime, while matching translations inform fallbacks.

## Status

| Surface | Vocabulary | Heard live |
|---|---|---|
| Tile cursor, object scanner and tactical narration | game fields + `X2AccessVocab`, INT + RUS | pending |
| Strategy, Avenger and legacy listener words | game fields + `X2AccessVocab`, INT + RUS | pending |
| Native recipes, objectives, status and input words | `Vocab::Word` from the same CDO, INT + RUS | pending |

The live test list is `next-steps.md`, "Localization hearing pass".
