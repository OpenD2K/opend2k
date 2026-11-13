#pragma once
// Our own labels in the Enhanced UI (in-game panel, tooltips, the modern front-end menus, pause
// menu, options, cheats, community pages) in the current text language: English, French, German,
// Spanish or Russian (docs/languages.md). The English text is the key; tr() returns UTF-8 (the
// TrueType UI font draws it, Cyrillic included) or the key itself when there is no translation.
// Game strings (Text.UIB) are not ours and stay as they are. The table lives in ui_text.cpp plus
// one ui_text_*.inc per area (menus, options, cheats, ...).

#include <string>

namespace d2k::render {

// Settings language code: "en", "fr", "de", "es", "ru-ps1", "ru-pc", "ru-ai" (anything else = English).
void setUiLanguage(const std::string& code);
const char* tr(const char* english);
inline const char* tr(const std::string& english) { return tr(english.c_str()); }
// printf-style: the translated pattern (same conversions, same order) formatted with the args.
std::string trf(const char* englishFormat, ...);
// The current language index: 0 en, 1 fr, 2 de, 3 es, 4 ru.
int uiLanguage();

}  // namespace d2k::render
