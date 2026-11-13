#pragma once
// The active game language (formats/language.hpp; docs/languages.md): built from the settings
// (Settings::language + the Advanced text / voices / cutscenes overrides, the translation folder,
// the PS1 disc image) and the data folder. App applies it at startup and whenever the language
// settings change in the front end (then the menu font, texts, sounds and movies are reloaded);
// every loader of text, fonts, sidebar art, speech and cutscenes asks it for its files.

#include "formats/font.hpp"
#include "formats/language.hpp"
#include "formats/uib.hpp"

#include <string>

namespace d2k::platform {

struct Settings;

// The language parts chosen in the settings (Advanced overrides "" = follow the language).
formats::lang::Selection languageSelection(const Settings& s);
// Changes whenever anything that applyLanguage() depends on changes.
std::string languageKey(const Settings& s);

// <Dune 2000>/../ru-pc next to the game folder (discovery when no folder is set), or "".
std::string defaultTranslationDir(const std::string& dataDir);
// The translation folder in use: Settings::translationDir, else defaultTranslationDir().
std::string translationDir(const Settings& s, const std::string& dataDir);
// The Russian AI voice set's folder: <pref path>/Voices/ru-ai, or Settings::aiVoicesDir when set.
std::string defaultAiVoicesDir();
std::string aiVoicesDir(const Settings& s);
// True when the image is the Russian PS1 disc with its speech banks.
bool ps1DiscIsRussian(const std::string& image);

// Builds the sources and resolves the selection. `ps1Override` = --ps1, `appMoviesDir` = the app's
// Movies folder (overridable with --game-data-root).
void applyLanguage(const Settings& s, const std::string& dataDir, const std::string& ps1Override = {},
                   const std::string& appMoviesDir = {});
const formats::lang::Resolved& activeLanguage();
const formats::lang::Sources& languageSources();

// Loaders in the active language:
// Text.UIB (or a mission's TextUib at `basePath`) with the language's values merged by key (for
// Russian: the Fargus text with the fixes overlay applied first, Settings::russianTextFixes).
formats::StringTable loadGameText(const std::string& dataDir, const std::string& basePath = {});
// FONT.FNT (col = false) / FONTCOL.FNT (col = true) with the language's character map.
formats::Font loadGameFont(const std::string& dataDir, bool col);
// UI_ENG.R16 or the language's ui_<lng>.r16.
std::string uiR16Path(const std::string& dataDir);
// The language's UI_GFX folder searched before data/UI_GFX ("" = none).
const std::string& uiGfxOverlayDir();

}  // namespace d2k::platform
