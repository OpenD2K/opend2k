#pragma once
// Game languages: which of the player's files give the text, fonts, speech and cutscenes of each
// language, and the text encodings (docs/languages.md).
//
// Sources (all on the player's disk, loaded at runtime, never shipped):
//   English / French / German / Spanish — the Gruntmods data folder: UI_DATA/Text<Lng>.UIB,
//     FONT<Lng>.FNT / FONTCOL<Lng>.FNT / bin/FONT<Lng>.BIN, ui_<LNG>.r16 (sidebar art),
//     GAMESFX<Lng>/ (speech AUDs + SOUND.RS), movies *_<e|f|g|s>.VQA. This mirrors the community
//     patch's InitLanguage (dune2000.ini [Options] Language=Eng|Fre|Ger|Spa: "text%s.uib",
//     "font%s.fnt", "FontCol%s.fnt", "bin\font%s.bin", "GAMESFX%s", "Movies%s" when present) and
//     Setup__LoadUI_LanguageFile 0x460ec0 ("ui_%s.r16" with Text.UIB's "Language" value).
//   Russian (translation) — a Russian PC translation folder (e.g. game/ru-pc): the "Fargus" text
//     (Ui_data/Text.uib in CP1251, font.fnt / fontcol.fnt + Bin/Font.bin with Cyrillic glyphs, UI_gfx
//     title/button art), the "Stream" dub (GAMESFX/*.AUD) and its Russian-dubbed MOVIES/*_E.VQA.
//     Only text, fonts, art, speech and movies are used — never its old bin/ rules or maps.
//   Russian (PS1) — speech and cutscenes from the Russian PS1 disc image (formats/ps1_sound.hpp,
//     formats/ps1_str.hpp); text from the translation folder (the PS1 disc has only 192 strings).
//   Russian (AI) — the Russian text as Russian (translation), the speech from OpenD2K's generated AI
//     voice set (tools/voice/gen_ru_ai.py writes <pref path>/Voices/ru-ai/GAMESFX/*.AUD under the PC
//     names; docs/ai-voices.md), lines it lacks in English; the translation's Russian-dubbed movies.
//
// Text is kept as the bytes of the files: Latin text in CP1252 (Latin-1), Russian in CP1251. The
// bitmap fonts draw bytes through their FONT.BIN character map (formats/font.hpp), so a CP1251
// string needs the Cyrillic font + its map. Strings we write ourselves (UTF-8 literals, typed text)
// are converted with toCodepage().
//
// No SDL here: unit-tested by formats_test.

#include "formats/uib.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace d2k::formats::lang {

enum class Id { English, French, German, Spanish, RussianPs1, RussianPc, RussianAi };
constexpr int kCount = 7;
constexpr std::array<Id, kCount> kAll = {Id::English, Id::French, Id::German, Id::Spanish, Id::RussianPs1, Id::RussianPc, Id::RussianAi};

const char* code(Id id);                       // settings value: "en", "fr", "de", "es", "ru-ps1", "ru-pc", "ru-ai"
std::optional<Id> fromCode(const std::string& code);
const char* nativeName(Id id);                 // UTF-8: "Français", "Русский (PS1)", ...
const char* englishName(Id id);                // ASCII: "French", "Russian (PS1)", ...
int codepageOf(Id textLanguage);               // 1252 or 1251
// The Russian languages: their text is the translation's (Fargus, + the fixes overlay).
bool isRussian(Id id);
// The name as bytes for a font of `codepage`: the native name when it can be written in it,
// else the English one ("Русский (PS1)" in CP1251; "Russian (PS1)" for a Latin font).
// `brief`: a shorter native name for narrow rows ("Русский (AI)" for "Русский (AI, оригинальные голоса)").
std::string displayName(Id id, int codepage, bool brief = false);

// ---- encodings ---------------------------------------------------------------------------
// UTF-8 -> CP1251 / CP1252 bytes. Characters the code page lacks are transliterated (é -> e in
// CP1251, Ж -> Zh in CP1252) or become '?'.
std::string toCodepage(const std::string& utf8, int codepage);
std::string toUtf8(const std::string& bytes, int codepage);
bool encodable(const std::string& utf8, int codepage);  // without transliteration
// Upper case of code page text (ASCII, Latin-1 accents / Cyrillic).
std::string upper(std::string s, int codepage);
// The code page of the text currently shown (set with the active language; default 1252).
int activeCodepage();
void setActiveCodepage(int cp);

// ---- per-source code pages ---------------------------------------------------------------
// Text from files that are not the active language's string table (community mission .ini
// names / briefings / [Text], their own TextUib, intel .txt, MultiplayerMaps.ini, Players/*.ini)
// has its own code page: Latin (CP1252) unless the file is UTF-8 or written in Russian. Such
// text is converted where it is loaded, never drawn as bytes of the active code page (that drew
// "Grâce" as "Grвce" with the Russian font).
constexpr int kUtf8 = 65001;
// Valid UTF-8 with at least one multi-byte character (pure ASCII is not "UTF-8" here).
bool isUtf8(const std::string& s);
// kUtf8 for UTF-8 text; 1251 when the letters are mostly CP1251 Cyrillic (0xC0..0xFF and ¸/¨
// outnumber the ASCII letters); else 1252.
int detectCodepage(const std::string& bytes);
// `bytes` in code page `cp` (kUtf8 = already UTF-8) as UTF-8.
std::string sourceToUtf8(const std::string& bytes, int cp);
// `bytes` from code page `from` to code page `to` (either may be kUtf8); characters `to` lacks
// are transliterated (â -> a for CP1251).
std::string recode(const std::string& bytes, int from, int to);
// A string for a code-page bitmap font (FONT.FNT through its FONT.BIN map): UTF-8 text (our
// labels, the catalog's community text) is converted to `cp` (accented Latin letters fall back
// to the bare letter for the Cyrillic font); code page bytes stay as they are.
std::string forBitmapFont(const std::string& s, int cp);
// A string table's code page (detectCodepage over its values) / its values recoded in place.
int detectCodepage(const StringTable& t);
void recodeValues(StringTable& t, int from, int to);

// ---- sources -----------------------------------------------------------------------------
// The parts found in a Russian translation folder (formats/language.cpp scanTranslation):
struct Translation {
    std::string root;
    std::string textDir;    // folder holding Ui_data/Text.uib + font.fnt ("" = none)
    std::string voicesDir;  // GAMESFX folder with the dubbed *.AUD ("" = none)
    std::string moviesDir;  // folder with *.VQA cutscenes ("" = none)
    bool any() const { return !textDir.empty() || !voicesDir.empty() || !moviesDir.empty(); }
};
// Looks (up to 4 levels deep) for "Ui_data/Text.uib" + "font.fnt", a "GAMESFX" folder with AUDs
// and a "MOVIES" folder with VQAs. Empty result when `root` is not a folder.
Translation scanTranslation(const std::string& root);

// The AI voice set's speech folder under `root` (e.g. <pref path>/Voices/ru-ai): "<root>/GAMESFX" when it
// holds AUDs, else `root` itself when it does ("" = none).
std::string findAiVoices(const std::string& root);

struct Sources {
    std::string dataDir;           // Gruntmods "Dune 2000/data"
    Translation translation;       // Russian PC translation (may be empty)
    std::string ruFixesDir;        // OpenD2K's Russian text fixes (a folder with Text.uib, CP1251) applied
                                   // over the translation's text ("" = none; findRuFixes)
    std::string ps1Image;          // PS1 disc image (.cue/.bin; "" = none)
    bool ps1Russian = false;       // the image is the Russian PS1 disc with speech banks
    std::string aiVoicesRoot;      // where the Russian AI voices are looked for (shown in hints; may not exist)
    std::string aiVoicesDir;       // their GAMESFX folder with AUDs ("" = none; findAiVoices)
    std::vector<std::string> movieDirs;  // folders searched for PC cutscenes (data/../movies, app Movies)
};

// What a language can offer with the given sources. Missing parts fall back to English.
struct Availability {
    bool text = false, voices = false, movies = false;
    std::string needs;  // short hint when something is missing ("" = complete)
    bool any() const { return text || voices || movies; }
};
Availability availability(Id id, const Sources& s);

// The files of one selection (text / voices / cutscenes may come from different languages).
struct Selection {
    Id text = Id::English, voices = Id::English, movies = Id::English;
    bool ruTextFixes = true;  // Russian text: Fargus + the fixes overlay (Sources::ruFixesDir) / Fargus only
};

struct Resolved {
    Selection sel;
    int codepage = 1252;
    // Text: the language's string table (merged over the base Text.UIB by key) and fonts.
    std::string textUib;     // "" = the base Text.UIB only
    std::string textOverlayUib;  // applied over textUib by key before the merge ("" = none): the
                                 // Russian text fixes (docs/russian-translation-review.md)
    std::string fontFnt, fontColFnt, fontBin;  // "" = the data folder's FONT.FNT / FONTCOL.FNT / bin/FONT.BIN
    std::string uiR16;       // ui_<lng>.r16 ("" = UI_ENG.R16)
    std::string uiGfxDir;    // folder searched before data/UI_GFX (Russian title / button art)
    // Speech: a GAMESFX-like folder searched first (AUDs, SOUND.RS) and/or the PS1 disc.
    std::string voicesDir;
    std::string ps1Speech;   // PS1 image whose speech banks replace the PC speech ("" = none; voicesDir then
                             // holds the lines the disc lacks: the Russian PC dub)
    // Cutscenes: the language letter of the PC file names, folders searched first, PS1 first.
    char movieLetter = 'e';
    std::vector<std::string> movieDirs;
    std::string ps1Movies;   // PS1 image preferred for movies ("" = PC files first)
};
// Unavailable parts fall back to English (text/voices) or to the default movie search.
Resolved resolve(const Selection& sel, const Sources& s);

// The base table with the values of `translated` for keys whose base value is `english`'s (all
// keys when base == english). Keeps the base table's order (mission messages index into it) and
// appends keys only the translation has. Empty `translated` keeps `base`.
// A key that occurs several times in the base table (CreditEntry, Barracks, "") takes the n-th
// translated value for its n-th occurrence when both tables have it equally often, else stays.
StringTable mergeTranslation(const StringTable& base, const StringTable& english, const StringTable& translated);

// `translated` with the values of `overlay` by key (every occurrence of the key; empty overlay
// values are ignored); keys only the overlay has are appended.
StringTable applyOverlay(const StringTable& translated, const StringTable& overlay);

// The Russian text fixes folder: the first "<root>/lang/ru-fixes" (or "<root>/ru-fixes") with a
// Text.uib, for each root in order ("" = none). Roots: the art pack folders, the translation folder.
std::string findRuFixes(const std::vector<std::string>& roots);

}  // namespace d2k::formats::lang
