# Languages

One setting picks the game language: **English / Français / Deutsch / Español / Русский (PS1) /
Русский (перевод) / Русский (AI, оригинальные голоса)**. It switches text, fonts, the sidebar art, speech and cutscenes together. An
optional *Advanced* mix takes each of Text / Voices / Cutscenes from any language. Everything comes from
the player's own files at runtime. Nothing of it is in the repository.

Code: `src/formats/language.*` (model, detection, file resolution, encodings; no SDL, tested by
`formats_test`), `src/platform/language.*` (the active language and the loaders), speech from the PS1
disc in `src/formats/ps1_sound.*` (docs/ps1-assets.md "Speech slots → PC sample names").

## Where each language's files come from

| Language | Text + fonts | Sidebar art | Voices | Cutscenes |
|---|---|---|---|---|
| English | `UI_DATA/Text.UIB`, `FONT.FNT`, `FONTCOL.FNT`, `bin/FONT.BIN` | `UI_ENG.R16` | `GAMESFXEng/` (else `GAMESFX/`) | `*_E.VQA` |
| Français / Deutsch / Español | `Text{Fre,Ger,Spa}.UIB`, `FONT{..}.FNT`, `FONTCOL{..}.FNT`, `bin/FONT{..}.BIN` | `ui_{FRE,GER,SPA}.r16` | `GAMESFX{Fre,Ger,Spa}/` (AUDs and its `SOUND.RS`) | `*_{F,G,S}.VQA` (else English) |
| Русский (перевод) | translation folder: `Ui_data/Text.uib` (CP1251), `font.fnt`, `fontcol.fnt`, `Bin/Font.bin` | `UI_RUS.R16` (in the Gruntmods data) | translation `GAMESFX/*.AUD` ("Stream" dub) | translation `MOVIES/*_E.vqa` (Russian dub) |
| Русский (PS1) | as Русский (перевод) (English without the folder) | `UI_RUS.R16` | the Russian PS1 disc's speech banks | the Russian PS1 disc's STR movies first |
| Русский (AI) | as Русский (перевод) (+ the text fixes) | `UI_RUS.R16` | our generated AI set `<pref>/Voices/ru-ai/GAMESFX/*.AUD`, else English | as Русский (перевод) |

This mirrors the community patch (`ref/dune2000-sp-patches/src/multi-language.c`, `InitLanguage`:
dune2000.ini `[Options] Language=Eng|Fre|Ger|Spa|Rus` picks `text%s.uib`, `font%s.fnt`, `FontCol%s.fnt`,
`bin\font%s.bin`, `GAMESFX%s\` and `Movies%s\` when they exist) and the original
`Setup__LoadUI_LanguageFile` 0x460ec0 (`ui_%s.r16` with Text.UIB's `Language` value: ENG/FRE/GER/SPA).
The French, German and Spanish fonts in Gruntmods 1.6.7 are byte-identical to the English ones (Latin-1
accents at glyphs 0x80+). Each `Text<Lng>.UIB` names its own cutscene letter (`a_br03_f`).

### The Russian translation folder (`game/ru-pc` for the owner)
A Russian PC translation for v1.06: "Текст от Фаргус" (Fargus text and fonts) and "Озвучка от Stream"
(Stream voices, 251 AUDs with the PC names, 43 Russian-dubbed VQAs with `_E` names). Its instructions copy it
over a Gruntmods install and set `Language=Rus`. We use **only** text, fonts, voices and movies. Its old
`bin/*.bin`, `templates.bin`, maps and `Data.r16` stay unused, because they would break the Gruntmods rules and art.
Its `UI_gfx` has no Russian art (English Easy/Normal/Hard, the plain title without the Gruntmods credit),
and its UIL pages differ only on 4 Gruntmods-modified pages, so both come from the Gruntmods data.
The folder is found:
- in `Settings::translationDir` (data-folder page → *Translation folder…*, `--translation DIR`), else
- next to the game folder: `<Dune 2000>/../ru-pc` (also `ru_pc`, `Russian`).

`scanTranslation` searches up to 4 levels deep. It looks for a folder with `Ui_data/Text.uib` and `font.fnt`
(text), a `GAMESFX` folder with AUDs (voices) and a `MOVIES` folder with VQAs. So the original download
layout and a merged copy both work.

**Text keys.** Fargus' Text.uib has 898 keys; Gruntmods' Text.UIB has 994. The 96 missing Gruntmods
keys (Survival, Select Mission, the extra campaigns, ...) stay English, unless the fixes overlay (below)
translates them. The merge
(`lang::mergeTranslation`) keeps the **base table's order**, because mission messages index into
Text.UIB by number. It replaces values by key and appends keys that only the translation has. It never
takes `VersionNumber`: the Fargus file says "V 1.02 RUS", while the data is 1.06. Empty translated values
also keep the English text. A community mission's own TextUib keeps its own values, and only keys equal
to the English Text.UIB get translated. Repeated keys (348 × `CreditEntry`, `Barracks` twice, ...) merge by
position, and only when both files repeat them equally often.

**Text fixes.** `artpacks/lang/ru-fixes/Text.uib` (gitignored; built by `tools/ru_text_fixes.py`) corrects the
Fargus text and fills in the missing keys. It is applied by key over the Fargus table before the merge.
Setting: *Russian text: Fargus / Fargus + fixes* (`russianTextFixes`, default on; `--ru-text fargus|fixes`), for every
Russian entry (перевод, PS1, AI).
Review and glossary: docs/russian-translation-review.md.

### Русский (PS1)
The Russian PS1 disc (`Settings::ps1Image`, data-folder page → *PS1 disc image…*, `--ps1`). It is
recognised by its `d2ktext.dat`, which writes Russian in Latin look-alikes ("XAPKOHHEH" = ХАРКОННЕН), and
by its speech banks (`satr.dat`). The speech replaces each PC speech file that has a PS1 slot (201 PC
names, at 6998 Hz). Lines the disc lacks (the `*_MAP*` campaign-map lines, game loaded/saved, the Ordos
saboteur) come from the Russian PC dub when the translation folder is present, else from the English
files; the Gruntmods mission lines S01..S18 stay English. SFX stay PC. The disc's movies play before any PC VQA. The PS1
UI text (192 strings) is not used; the Fargus text is. The English PS1 disc is not offered as a
language (its movies still fill in for missing PC cutscenes, as before).

### Русский (AI, оригинальные голоса)
Code `ru-ai`. Text, fonts, sidebar art and the text-fixes option are those of Русский (перевод); the
cutscenes are the translation folder's Russian-dubbed VQAs (else English; the Advanced overrides still
apply). The speech is OpenD2K's AI voice set: Russian lines converted into each original speaker's voice
(docs/ai-voices.md). It is never shipped. The player generates it once with their own Yandex SpeechKit key:

```
python3 tools/voice/gen_ru_ai.py --data "<Dune 2000>/data" \
    --out "~/Library/Application Support/d2k-mac/Dune 2000/Voices/ru-ai" ...   (docs/ai-voices.md "Full set")
```

- **Folder:** `SDL_GetPrefPath()/Voices/ru-ai` (`lang::findAiVoices`: its `GAMESFX/` with non-empty AUDs, or
  the folder itself), or settings.ini `aiVoicesDir` / `--ai-voices DIR`. The generator's `manifest.tsv`,
  `preview/` and `work/` are ignored.
- **Availability:** offered only when that folder has AUDs. Without it the entry is dimmed on the Original
  Language page and left out of the Enhanced row; its hint says "no AI voices: generate with
  tools/voice/gen_ru_ai.py". With the voices but no translation folder it plays the voices with English text.
- **Missing lines** (the Gruntmods `S01..S18` and new-unit voices, anything not generated) play the original
  English files, never the Stream dub or the PS1 disc. Silent files are skipped as for the Stream dub.
  SFX (`SOUND.RS`) stay the game's own.
- `--sound-test --language ru-ai` lists each speech sample's file: with the full set, all 141 speech samples of
  samples.uib come from the ru-ai folder, 54 SFX from SOUND.RS (7 are missing in the game data itself).

## Encodings
Text stays as the bytes of the files. Latin text is CP1252 (Latin-1); Russian text is **CP1251**. The
game's bitmap fonts draw bytes through their `FONT.BIN` character map (`formats/font.hpp`). The Fargus
`Font.bin` maps CP1251 А..я (0xC0..0xFF) to the Cyrillic glyphs 0x80..0xBF of its `font.fnt` /
`fontcol.fnt`, and keeps ASCII as is. So the path UIB bytes → charMap → glyph works unchanged in the
Original pages (`uil_view`), the Enhanced UI (`render/text`, `ui.*`), tooltips, mission messages
(FONTCOL), briefings, and the campaign and score screens. The translation writes 0x92 (’) as its
apostrophe, but its map draws that byte as a space, so we map it to `'`.

**Per-source code pages.** Text that is not the language's string table carries its own code page
(`lang::detectCodepage`: UTF-8 when the bytes are valid UTF-8, CP1251 when Cyrillic letters outnumber the
ASCII ones, else CP1252). Community mission .ini names / authors / briefings (`MissionIni::codepage`), a
mission's own TextUib, intel .txt files and MultiplayerMaps.ini names are converted to UTF-8 in the catalog;
the mission's `[Text]` / `[Sides]` and Players/*.ini names are recoded to the active code page for the battle,
and the untranslated values of Text.UIB / a mission TextUib are recoded to the language's code page when the
table is merged. The Play font draws UTF-8 as is; the bitmap fonts convert UTF-8 to the active code page on
the way in (`lang::forBitmapFont` in `render/text.cpp`), so "Coup de Grâce" (FEYS14V2.ini) shows "â" with a
Latin font and "Grace" with the Fargus Cyrillic font, never "Grвce".

The strings we write ourselves go through `lang::toCodepage(utf8, cp)`:
- **Typed text:** SDL's UTF-8 is converted (player name, save names, high scores); Cyrillic can be typed
  in Russian.
- **Language names:** each name shows natively when the current font can write it ("Русский (PS1)" with
  the Cyrillic font). Otherwise the English name shows ("French" under the Russian font, which has no ç).
- **Upper case:** `lang::upper` handles CP1251/CP1252 upper case (for any display upper-casing).
- **Our own UI words**: the Enhanced UI (menus, in-game panel, tooltips, pause menu, options, cheats incl. the
  placement hint on the battlefield, community pages) is translated by `render/ui_text.*` (UTF-8, Play font);
  our rows on the original pages remain English, except the "Language" button ("Язык" in Russian).

## Settings and UI
`settings.ini`: `language=en|fr|de|es|ru-ps1|ru-pc|ru-ai`, the Advanced overrides
`languageText` / `languageVoices` / `languageMovies` (empty = follow `language`), `translationDir` and
`aiVoicesDir` (empty = `<pref path>/Voices/ru-ai`).
- **Enhanced Options:** a *Language* row offers every language with at least one part available. Below
  it, the Advanced rows *Text | Voices* and *Cutscenes*, plus a hint when the pick lacks something
  ("needs the Russian PS1 disc image"). The row shows "Русский (AI)" (the full name does not fit).
- **Original SETTINGS page:** *Game Settings* and *Language* share a row as half-width buttons. Language
  opens our page `D2K_LANGUAGE`, built from loadmis.uil and Load.uil widgets: the languages on the left
  (dimmed without sources), and on the right what the selected one gives and from where. *Select* applies
  it (screenshot name `language`).
- **Applying a change:** it takes effect in the front end. App sees the language key change, then reloads
  the menu font, texts, sound bank and movie names and returns to the options page. A running battle
  keeps its language until the next start.
- **Command line:** `--language CODE`, `--text-language` / `--voices-language` / `--movies-language CODE`,
  `--translation DIR`, `--ai-voices DIR`, `--ps1 IMAGE`. `--sound-test` logs where the speech of the active language
  comes from and exits. `D2K_SOUND_LOG=1` logs each sample's file while playing.

## Gaps
- Our own rows on the Original (bitmap-font) pages are English in every language.
- The 96 Gruntmods-only keys stay English in Russian without the text fixes overlay.
- The Stream dub's `*_MAP*` and `*_PRMRY` files are silent placeholders, so Russian (PS1) plays silence for
  the campaign-map lines (docs/russian-translation-review.md, open points).
- French, German and Spanish cutscenes need the Gruntmods cutscene installer for that language (data
  page → *Choose installer*). Until then the English movies play.
- Russian (PS1) has one voice set for all houses (the disc's Harkonnen/Ordos banks copy Atreides). The
  Ordos saboteur lines and the `*_MAP*` briefing lines come from the Stream dub (or English).
