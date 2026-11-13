#pragma once
// Front-end page layouts of the original UI (data/UI_DATA):
// - `*.uil` page scripts: u32 count, then count x {u16 len, NUL-terminated line}. Lines are a
//   small text language read by the CUIManager page builder (CInterface 0x4359a0 / FUN_004390e0):
//   "BACKGROUND img" .. "END_BACKGROUND", "GROUPBOX name", "TEXTBOX[_L|_C][_UL] name",
//   "BUTTON name", "ANIMATION name", "MOVIE name", "LISTBOX name", "CHECKBOX name",
//   "VALUEBAR[_NOVAL] name", "EDITBOX[FOCUS] name", "MULTISTATEBOX name", "LISTBOXEMPTYSLOT name",
//   "TOGGLEBUTTON name" each followed by positional
//   fields (position "x, y", size "w, h", colours, text key, images, ...) and commands
//   ("JumpToMenu X", "BackMenu", "EnterText KEY", ...) up to the matching "END_<TYPE>".
//   "//" starts a comment. Lines outside a block are ignored (Globe.uil has an orphan
//   GROUPBOX body whose header line is missing).
// - `menus.uib`: u32 count, {u16 len, menu name, u16 len, "<file>.uid", u32, u32} — the menu
//   name -> page file table (the game loads `<file>.uil`).
// - `colours.uib`: u32 count, {u16 len, name, u32 0x00RRGGBB} — the named UI colours.
// Layouts documented in re/notes/campaign-flow.md.

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace d2k::formats {

struct UilWidget {
    std::string kind;     // BACKGROUND, GROUPBOX, TEXTBOX, BUTTON, ANIMATION, MOVIE, LISTBOX, ...
    std::string suffix;   // "_L", "_C", "_R", "_UL", "_L_UL" (TEXTBOX), "_NOVAL" (VALUEBAR), ...
    std::string name;     // second word of the header (BACKGROUND: the image)
    int x = 0, y = 0, w = 0, h = 0;
    // Remaining lines in order, comments stripped and trimmed. The fixed fields come first
    // (see kFixedFields), then the commands.
    std::vector<std::string> fields;

    const std::string& field(std::size_t i) const;      // "" when missing
    std::vector<std::string> commands() const;          // fields after the fixed ones
    static std::size_t fixedFields(const std::string& kind);
};

struct UilPage {
    std::vector<UilWidget> widgets;  // file order = draw order
    const UilWidget* find(const std::string& name) const;
};

UilPage parseUil(std::span<const std::uint8_t> data);
UilPage loadUil(const std::string& path);

// menus.uib: menu name (upper case as stored) -> page file stem ("loadmis" for "loadmis.uid").
std::unordered_map<std::string, std::string> parseMenus(std::span<const std::uint8_t> data);

// colours.uib: name -> 0x00RRGGBB.
std::unordered_map<std::string, std::uint32_t> parseUiColours(std::span<const std::uint8_t> data);

}  // namespace d2k::formats
