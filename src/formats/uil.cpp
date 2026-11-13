#include "formats/uil.hpp"

#include "formats/io.hpp"

#include <cstdlib>
#include <stdexcept>

namespace d2k::formats {

namespace {

std::string trim(std::string s) {
    if (auto c = s.find("//"); c != std::string::npos) s.resize(c);
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

// "x, y" / "x y" / "x,y"
void parsePair(const std::string& s, int& a, int& b) {
    const char* p = s.c_str();
    char* end = nullptr;
    a = static_cast<int>(std::strtol(p, &end, 10));
    while (*end == ' ' || *end == ',' || *end == '\t') ++end;
    b = static_cast<int>(std::strtol(end, nullptr, 10));
}

std::string readStr(ByteReader& r) {
    const std::uint16_t len = r.u16();
    auto b = r.bytes(len);
    std::string s(reinterpret_cast<const char*>(b.data()), b.size());
    if (auto z = s.find('\0'); z != std::string::npos) s.resize(z);
    return s;
}

// Order matters where one keyword is a prefix of another (CInterface__Create 0x4359a0 tests
// TOGGLEBUTTON before BUTTON, LISTBOXEMPTYSLOT before LISTBOX, EDITBOXFOCUS before EDITBOX).
const char* const kKinds[] = {"BACKGROUND", "GROUPBOX",      "TEXTBOX",  "TOGGLEBUTTON", "BUTTON",
                              "ANIMATION",  "MOVIE",         "LISTBOXEMPTYSLOT", "LISTBOX", "CHECKBOX",
                              "MULTISTATEBOX", "EDITBOXFOCUS", "EDITBOX", "VALUEBAR"};

}  // namespace

std::size_t UilWidget::fixedFields(const std::string& kind) {
    if (kind == "GROUPBOX") return 3;    // background colour, border colour, image
    if (kind == "TEXTBOX") return 5;     // background, highlight, lowlight, font colour, text key
    if (kind == "BUTTON") return 9;      // text, highlight, lowlight, up, down, font up, font down, up gfx, down gfx
    if (kind == "ANIMATION") return 1;   // "<image> <fps>"
    if (kind == "LISTBOX" || kind == "LISTBOXEMPTYSLOT") return 14;  // colours, slider/arrow images
    if (kind == "TOGGLEBUTTON") return 10;  // text on, text off, then the BUTTON fields from highlight on
    if (kind == "CHECKBOX") return 5;    // border colour, on colour, off colour, on gfx, off gfx
    if (kind == "MULTISTATEBOX") return 4;  // border colour, state count, colours, gfx per state
    if (kind == "EDITBOX" || kind == "EDITBOXFOCUS") return 3;  // bar colour, cursor colour, font colour
    // background, slider, highlight, lowlight, number font, number back colours, region gfx,
    // bar gfx, "min max step initial"
    if (kind == "VALUEBAR") return 9;
    return 0;
}

const std::string& UilWidget::field(std::size_t i) const {
    static const std::string empty;
    return i < fields.size() ? fields[i] : empty;
}

std::vector<std::string> UilWidget::commands() const {
    const std::size_t n = fixedFields(kind);
    return n < fields.size() ? std::vector<std::string>(fields.begin() + static_cast<std::ptrdiff_t>(n), fields.end())
                             : std::vector<std::string>{};
}

const UilWidget* UilPage::find(const std::string& name) const {
    for (const auto& w : widgets)
        if (w.name == name) return &w;
    return nullptr;
}

UilPage parseUil(std::span<const std::uint8_t> data) {
    ByteReader r(data);
    const std::uint32_t count = r.u32();
    std::vector<std::string> lines;
    for (std::uint32_t i = 0; i < count && !r.atEnd(); ++i) lines.push_back(trim(readStr(r)));
    UilPage page;
    UilWidget* cur = nullptr;
    int step = 0;  // lines read in the current block
    for (const std::string& line : lines) {
        if (!cur) {
            const std::string head = line.substr(0, line.find(' '));
            for (const char* k : kKinds) {
                const std::string ks = k;
                if (head.rfind(ks, 0) != 0) continue;
                const std::string suffix = head.substr(ks.size());
                if (!suffix.empty() && suffix[0] != '_') continue;
                UilWidget w;
                w.kind = ks;
                w.suffix = suffix;
                const auto sp = line.find(' ');
                // The name is the second word ("BUTTON startCampaignGame 0": trailing values are
                // not part of it).
                w.name = sp == std::string::npos ? std::string() : trim(line.substr(sp + 1));
                if (auto e = w.name.find_first_of(" \t"); e != std::string::npos) w.name.resize(e);
                page.widgets.push_back(std::move(w));
                cur = &page.widgets.back();
                step = 0;
                break;
            }
            continue;
        }
        if (line.rfind("END_", 0) == 0) {
            cur = nullptr;
            continue;
        }
        if (cur->kind == "BACKGROUND") continue;
        if (step == 0) parsePair(line, cur->x, cur->y);
        else if (step == 1) parsePair(line, cur->w, cur->h);
        else cur->fields.push_back(line);
        ++step;
    }
    return page;
}

UilPage loadUil(const std::string& path) {
    const auto bytes = readFile(path);
    return parseUil(bytes);
}

std::unordered_map<std::string, std::string> parseMenus(std::span<const std::uint8_t> data) {
    ByteReader r(data);
    const std::uint32_t count = r.u32();
    std::unordered_map<std::string, std::string> out;
    for (std::uint32_t i = 0; i < count && !r.atEnd(); ++i) {
        const std::string name = readStr(r);
        std::string file = readStr(r);
        r.u32();
        r.u32();
        if (auto dot = file.find('.'); dot != std::string::npos) file.resize(dot);
        out[name] = file;
    }
    return out;
}

std::unordered_map<std::string, std::uint32_t> parseUiColours(std::span<const std::uint8_t> data) {
    ByteReader r(data);
    const std::uint32_t count = r.u32();
    std::unordered_map<std::string, std::uint32_t> out;
    for (std::uint32_t i = 0; i < count && !r.atEnd(); ++i) {
        const std::string name = readStr(r);
        out[name] = r.u32();
    }
    return out;
}

}  // namespace d2k::formats
