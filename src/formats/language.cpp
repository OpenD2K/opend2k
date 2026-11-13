#include "formats/language.hpp"

#include "formats/io.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>

namespace d2k::formats::lang {

namespace fs = std::filesystem;

namespace {

struct Info {
    Id id;
    const char* code;
    const char* native;
    const char* english;
    const char* suffix;  // Gruntmods file suffix ("Fre": TextFre.UIB, GAMESFXFre, ui_FRE.r16)
    char movie;          // cutscene file letter
};
constexpr Info kInfo[kCount] = {
    {Id::English, "en", "English", "English", "Eng", 'e'},
    {Id::French, "fr", "Fran\xC3\xA7" "ais", "French", "Fre", 'f'},
    {Id::German, "de", "Deutsch", "German", "Ger", 'g'},
    {Id::Spanish, "es", "Espa\xC3\xB1ol", "Spanish", "Spa", 's'},
    {Id::RussianPs1, "ru-ps1", "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9 (PS1)", "Russian (PS1)", "Rus", 'e'},
    {Id::RussianPc, "ru-pc",
     "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9 (\xD0\xBF\xD0\xB5\xD1\x80\xD0\xB5\xD0\xB2\xD0\xBE\xD0\xB4)",
     "Russian (translation)", "Rus", 'e'},
    {Id::RussianAi, "ru-ai", "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9 (AI, \xD0\xBE\xD1\x80\xD0\xB8\xD0\xB3\xD0\xB8\xD0\xBD\xD0\xB0\xD0\xBB\xD1\x8C\xD0\xBD\xD1\x8B\xD0\xB5 \xD0\xB3\xD0\xBE\xD0\xBB\xD0\xBE\xD1\x81\xD0\xB0)", "Russian (AI voices)", "Rus", 'e'},
};
const Info& info(Id id) { return kInfo[static_cast<int>(id)]; }

// Bytes 0x80..0xFF -> Unicode (0 = undefined), generated from Python's cp1251 / cp1252 codecs.
constexpr std::uint16_t kCp1251[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0,      0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
};
constexpr std::uint16_t kCp1252[32] = {
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178,
};

std::uint32_t decodeByte(unsigned char b, int cp) {
    if (b < 0x80) return b;
    if (cp == 1251) return kCp1251[b - 0x80];
    if (b < 0xA0) return kCp1252[b - 0x80];
    return b;  // CP1252 0xA0..0xFF = Latin-1
}

int encodeChar(std::uint32_t u, int cp) {
    if (u < 0x80) return int(u);
    for (int b = 0x80; b < 0x100; ++b)
        if (decodeByte(static_cast<unsigned char>(b), cp) == u) return b;
    return -1;
}

// Transliteration for characters a code page lacks.
std::string translit(std::uint32_t u) {
    // Latin-1 letters -> base letter (for CP1251 fonts).
    static const char* const kLatin = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    if (u >= 0xC0 && u <= 0xFF) {
        if (u == 0xC6) return "AE";
        if (u == 0xE6) return "ae";
        if (u == 0xDF) return "ss";
        return std::string(1, kLatin[u - 0xC0]);
    }
    if (u == 0x0152) return "OE";
    if (u == 0x0153) return "oe";
    // Russian -> Latin (for CP1252 fonts).
    static const char* const kCyr[32] = {"A", "B", "V", "G", "D", "E", "Zh", "Z", "I", "J", "K", "L", "M", "N", "O", "P",
                                         "R", "S", "T", "U", "F", "Kh", "Ts", "Ch", "Sh", "Shch", "\"", "Y", "'", "E", "Yu", "Ya"};
    if (u >= 0x0410 && u <= 0x042F) return kCyr[u - 0x0410];
    if (u >= 0x0430 && u <= 0x044F) {
        std::string s = kCyr[u - 0x0430];
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    if (u == 0x0401) return "Yo";
    if (u == 0x0451) return "yo";
    if (u == 0x2018 || u == 0x2019) return "'";
    if (u == 0x201C || u == 0x201D || u == 0x00AB || u == 0x00BB) return "\"";
    if (u == 0x2013 || u == 0x2014) return "-";
    if (u == 0x2026) return "...";
    return "?";
}

// UTF-8 decoding (invalid bytes become U+FFFD).
template <class F>
void forEachCodePoint(const std::string& s, F&& f) {
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::uint32_t u = 0xFFFD;
        int n = 1;
        if (c < 0x80) u = c;
        else if ((c & 0xE0) == 0xC0) { u = c & 0x1F; n = 2; }
        else if ((c & 0xF0) == 0xE0) { u = c & 0x0F; n = 3; }
        else if ((c & 0xF8) == 0xF0) { u = c & 0x07; n = 4; }
        if (n > 1) {
            if (i + std::size_t(n) > s.size()) { u = 0xFFFD; n = 1; }
            else
                for (int k = 1; k < n; ++k) {
                    const unsigned char d = static_cast<unsigned char>(s[i + std::size_t(k)]);
                    if ((d & 0xC0) != 0x80) { u = 0xFFFD; n = 1; break; }
                    u = (u << 6) | (d & 0x3F);
                }
        }
        f(u);
        i += std::size_t(n);
    }
}

void appendUtf8(std::string& out, std::uint32_t u) {
    if (u < 0x80) out += static_cast<char>(u);
    else if (u < 0x800) {
        out += static_cast<char>(0xC0 | (u >> 6));
        out += static_cast<char>(0x80 | (u & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (u >> 12));
        out += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (u & 0x3F));
    }
}

int gActiveCodepage = 1252;

std::string lowerStr(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isDir(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::is_directory(p, ec);
}

// A sub-folder of `dir`, any case ("" when missing).
std::string subDir(const std::string& dir, const std::string& name) {
    const std::string p = findFileNoCase(dir, name);
    return isDir(p) ? p : std::string();
}

bool hasFileWithExt(const std::string& dir, const std::string& ext) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && e.file_size(ec) > 0 && lowerStr(e.path().extension().string()) == ext) return true;
    return false;
}

// Any non-empty "<stem>_<letter>.vqa" in the folders.
bool hasMovies(const std::vector<std::string>& dirs, char letter) {
    const std::string tail = std::string("_") + letter + ".vqa";
    for (const std::string& d : dirs) {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(d, ec)) {
            if (!e.is_regular_file(ec) || e.file_size(ec) == 0) continue;
            const std::string n = lowerStr(e.path().filename().string());
            if (n.size() > tail.size() && n.compare(n.size() - tail.size(), tail.size(), tail) == 0) return true;
        }
    }
    return false;
}

std::string gruntmodsText(const std::string& dataDir, Id id) {
    const std::string ui = subDir(dataDir, "UI_DATA");
    return ui.empty() ? std::string() : findFileNoCase(ui, std::string("Text") + info(id).suffix + ".UIB");
}

std::string gruntmodsVoices(const std::string& dataDir, Id id) { return subDir(dataDir, std::string("GAMESFX") + info(id).suffix); }

}  // namespace

const char* code(Id id) { return info(id).code; }
const char* nativeName(Id id) { return info(id).native; }
const char* englishName(Id id) { return info(id).english; }

std::optional<Id> fromCode(const std::string& c) {
    for (const Info& i : kInfo)
        if (lowerStr(c) == i.code) return i.id;
    return std::nullopt;
}

bool isRussian(Id id) { return id == Id::RussianPs1 || id == Id::RussianPc || id == Id::RussianAi; }

int codepageOf(Id id) { return isRussian(id) ? 1251 : 1252; }

std::string displayName(Id id, int cp, bool brief) {
    const char* n = brief && id == Id::RussianAi ? "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9 (AI)" : nativeName(id);
    return encodable(n, cp) ? toCodepage(n, cp) : std::string(englishName(id));
}

std::string toCodepage(const std::string& utf8, int cp) {
    std::string out;
    forEachCodePoint(utf8, [&](std::uint32_t u) {
        const int b = encodeChar(u, cp);
        if (b >= 0) out += static_cast<char>(b);
        else out += translit(u);
    });
    return out;
}

bool encodable(const std::string& utf8, int cp) {
    bool ok = true;
    forEachCodePoint(utf8, [&](std::uint32_t u) { ok = ok && encodeChar(u, cp) >= 0; });
    return ok;
}

std::string toUtf8(const std::string& bytes, int cp) {
    std::string out;
    for (unsigned char b : bytes) {
        const std::uint32_t u = decodeByte(b, cp);
        appendUtf8(out, u ? u : 0xFFFD);
    }
    return out;
}

std::string upper(std::string s, int cp) {
    for (auto& ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c >= 'a' && c <= 'z') ch = static_cast<char>(c - 32);
        else if (c >= 0xE0 && (cp == 1251 || (c != 0xF7 && c != 0xFF))) ch = static_cast<char>(c - 32);  // а-я / à-þ
        else if (cp == 1251 && c == 0xB8) ch = static_cast<char>(0xA8);                 // ё
    }
    return s;
}

int activeCodepage() { return gActiveCodepage; }
void setActiveCodepage(int cp) { gActiveCodepage = cp == 1251 ? 1251 : 1252; }

bool isUtf8(const std::string& s) {
    bool multi = false;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { ++i; continue; }
        const int n = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (n == 0 || c == 0xC0 || c == 0xC1 || i + std::size_t(n) > s.size()) return false;
        for (int k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + std::size_t(k)]) & 0xC0) != 0x80) return false;
        multi = true;
        i += std::size_t(n);
    }
    return multi;
}

int detectCodepage(const std::string& bytes) {
    if (isUtf8(bytes)) return kUtf8;
    std::size_t ascii = 0, cyr = 0;
    for (unsigned char c : bytes) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) ++ascii;
        else if (c >= 0xC0 || c == 0xA8 || c == 0xB8) ++cyr;
    }
    // French / German / Spanish text has a few accented letters among many ASCII ones; Russian
    // text is mostly 0xC0..0xFF.
    return cyr > 0 && cyr >= ascii ? 1251 : 1252;
}

std::string sourceToUtf8(const std::string& bytes, int cp) {
    if (cp == kUtf8) return bytes;
    return toUtf8(bytes, cp);
}

std::string recode(const std::string& bytes, int from, int to) {
    if (from == to) return bytes;
    bool ascii = true;
    for (unsigned char c : bytes) ascii = ascii && c < 0x80;
    if (ascii) return bytes;
    const std::string u = sourceToUtf8(bytes, from);
    return to == kUtf8 ? u : toCodepage(u, to);
}

std::string forBitmapFont(const std::string& s, int cp) { return isUtf8(s) ? toCodepage(s, cp) : s; }

int detectCodepage(const StringTable& t) {
    std::string all;
    for (const auto& [k, v] : t.entries) {
        all += v;
        all += ' ';
    }
    return detectCodepage(all);
}

void recodeValues(StringTable& t, int from, int to) {
    if (from == to) return;
    for (auto& [k, v] : t.entries) v = recode(v, from, to);
}

Translation scanTranslation(const std::string& root) {
    Translation t;
    if (!isDir(root)) return t;
    t.root = root;
    std::function<void(const fs::path&, int)> walk = [&](const fs::path& dir, int depth) {
        const std::string d = dir.string();
        const std::string name = lowerStr(dir.filename().string());
        if (t.textDir.empty()) {
            const std::string ui = subDir(d, "Ui_data");
            if (!ui.empty() && !findFileNoCase(ui, "Text.uib").empty() && !findFileNoCase(d, "font.fnt").empty()) t.textDir = d;
        }
        if (t.voicesDir.empty() && name == "gamesfx" && hasFileWithExt(d, ".aud")) t.voicesDir = d;
        if (t.moviesDir.empty() && name == "movies" && hasFileWithExt(d, ".vqa")) t.moviesDir = d;
        if (depth >= 4) return;
        std::error_code ec;
        std::vector<fs::path> subs;
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.is_directory(ec)) subs.push_back(e.path());
        std::sort(subs.begin(), subs.end());
        for (const auto& s : subs) walk(s, depth + 1);
    };
    walk(fs::path(root), 0);
    return t;
}

std::string findAiVoices(const std::string& root) {
    if (!isDir(root)) return {};
    if (const std::string g = subDir(root, "GAMESFX"); !g.empty() && hasFileWithExt(g, ".aud")) return g;
    return hasFileWithExt(root, ".aud") ? root : std::string();
}

Availability availability(Id id, const Sources& s) {
    Availability a;
    switch (id) {
    case Id::English:
        a.text = !s.dataDir.empty();
        a.voices = !s.dataDir.empty();
        a.movies = true;
        break;
    case Id::French:
    case Id::German:
    case Id::Spanish:
        a.text = !gruntmodsText(s.dataDir, id).empty();
        a.voices = !gruntmodsVoices(s.dataDir, id).empty();
        a.movies = hasMovies(s.movieDirs, info(id).movie);
        if (!a.text || !a.voices) a.needs = "needs the Gruntmods data (Text" + std::string(info(id).suffix) + ".UIB, GAMESFX" + info(id).suffix + ")";
        else if (!a.movies) a.needs = std::string("no ") + englishName(id) + " cutscenes (English ones play)";
        break;
    case Id::RussianPc:
        a.text = !s.translation.textDir.empty();
        a.voices = !s.translation.voicesDir.empty();
        a.movies = !s.translation.moviesDir.empty();
        if (!a.any()) a.needs = "needs the Russian translation folder (ru-pc)";
        else if (!a.text) a.needs = "no Russian text in the translation folder";
        else if (!a.voices) a.needs = "no Russian voices in the translation folder";
        break;
    case Id::RussianPs1:
        a.voices = s.ps1Russian;
        a.movies = s.ps1Russian;
        a.text = !s.translation.textDir.empty();
        if (!s.ps1Russian) a.needs = "needs the Russian PS1 disc image";
        else if (!a.text) a.needs = "text needs the Russian translation folder (ru-pc)";
        break;
    case Id::RussianAi:
        // Offered only with the generated voice set; text and movies as Russian (translation).
        a.voices = !s.aiVoicesDir.empty();
        a.text = a.voices && !s.translation.textDir.empty();
        a.movies = a.voices && !s.translation.moviesDir.empty();
        if (!a.voices) a.needs = "no AI voices: generate with tools/voice/gen_ru_ai.py";
        else if (!a.text) a.needs = "text needs the Russian translation folder (ru-pc)";
        else if (!a.movies) a.needs = "no Russian cutscenes in the translation folder (English ones play)";
        break;
    }
    return a;
}

Resolved resolve(const Selection& sel, const Sources& s) {
    Resolved r;
    r.sel = sel;
    // Text + fonts.
    const Availability at = availability(sel.text, s);
    const Id text = at.text ? sel.text : Id::English;
    r.sel.text = text;
    r.codepage = codepageOf(text);
    if (isRussian(text)) {
        const std::string& d = s.translation.textDir;
        r.textUib = findFileNoCase(subDir(d, "Ui_data"), "Text.uib");
        r.fontFnt = findFileNoCase(d, "font.fnt");
        r.fontColFnt = findFileNoCase(d, "fontcol.fnt");
        r.fontBin = findFileNoCase(subDir(d, "Bin"), "Font.bin");
        if (sel.ruTextFixes && !r.textUib.empty() && !s.ruFixesDir.empty())
            r.textOverlayUib = findFileNoCase(s.ruFixesDir, "Text.uib");
        // (Its UI_gfx has no Russian art — English Easy/Normal/Hard, the plain title without the
        // Gruntmods credit — so data/UI_GFX stays; uiGfxDir remains for translations that have some.)
        // The Gruntmods data has a Russian sidebar art file too (Setup__LoadUI_LanguageFile "ui_%s.r16").
        r.uiR16 = findFileNoCase(s.dataDir, "UI_RUS.R16");
    } else if (text != Id::English) {
        const std::string suf = info(text).suffix;
        r.textUib = gruntmodsText(s.dataDir, text);
        r.fontFnt = findFileNoCase(s.dataDir, "FONT" + suf + ".FNT");
        r.fontColFnt = findFileNoCase(s.dataDir, "FONTCOL" + suf + ".FNT");
        r.fontBin = findFileNoCase(subDir(s.dataDir, "bin"), "FONT" + suf + ".BIN");
        std::string lng = suf;
        for (char& c : lng) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        r.uiR16 = findFileNoCase(s.dataDir, "ui_" + lng + ".r16");
    }
    // Speech.
    const Availability av = availability(sel.voices, s);
    const Id voices = av.voices ? sel.voices : Id::English;
    r.sel.voices = voices;
    if (voices == Id::RussianPc) r.voicesDir = s.translation.voicesDir;
    // The AI set alone: lines it lacks play in English (SoundBank's next folders), never the Stream dub.
    else if (voices == Id::RussianAi) r.voicesDir = s.aiVoicesDir;
    else if (voices == Id::RussianPs1) {
        r.ps1Speech = s.ps1Image;
        // Lines the disc lacks (campaign-map briefings, game saved/loaded, Ordos saboteur): the
        // Russian PC dub when present, else English.
        r.voicesDir = s.translation.voicesDir;
    }
    else r.voicesDir = gruntmodsVoices(s.dataDir, voices);
    // Cutscenes.
    const Id movies = sel.movies;
    r.movieLetter = info(movies).movie;
    if ((movies == Id::RussianPc || movies == Id::RussianAi) && !s.translation.moviesDir.empty())
        r.movieDirs.push_back(s.translation.moviesDir);
    if (movies == Id::RussianPs1 && s.ps1Russian) r.ps1Movies = s.ps1Image;
    return r;
}

namespace {

void appendKey(StringTable& t, const std::string& k, const std::string& v) {
    t.index.emplace(k, t.entries.size());
    t.lowerIndex.emplace(lowerStr(k), t.entries.size());
    t.entries.emplace_back(k, v);
}

// key -> the values of all its occurrences, in file order.
std::unordered_map<std::string, std::vector<const std::string*>> occurrences(const StringTable& t) {
    std::unordered_map<std::string, std::vector<const std::string*>> m;
    for (const auto& [k, v] : t.entries) m[k].push_back(&v);
    return m;
}

}  // namespace

StringTable mergeTranslation(const StringTable& base, const StringTable& english, const StringTable& translated) {
    if (translated.entries.empty()) return base;
    StringTable out = base;
    const auto tOcc = occurrences(translated);
    const auto bOcc = occurrences(base);
    std::unordered_map<std::string, std::size_t> seen;
    for (auto& [k, v] : out.entries) {
        const std::size_t n = seen[k]++;
        if (k == "VersionNumber" || k == "Language") continue;  // the data's version ("V 1.06"), not the translation's
        const auto t = tOcc.find(k);
        if (t == tOcc.end()) continue;  // missing in the translation: keep (English)
        // Repeated keys (the credits are 348 x "CreditEntry"): positional, and only when both
        // tables repeat the key equally often; otherwise every line would get the first value.
        const std::size_t nb = bOcc.at(k).size();
        if (nb > 1 && t->second.size() != nb) continue;
        const std::string& tv = *t->second[nb > 1 ? n : 0];
        if (tv.empty() && !v.empty()) continue;  // empty in the translation: keep
        const std::string* e = english.find(k);
        if (&base == &english || !e || *e == v) v = tv;
    }
    for (const auto& [k, v] : translated.entries)
        if (!out.index.count(k)) appendKey(out, k, v);
    return out;
}

StringTable applyOverlay(const StringTable& translated, const StringTable& overlay) {
    if (overlay.entries.empty()) return translated;
    StringTable out = translated;
    for (auto& [k, v] : out.entries)
        if (const std::string* o = overlay.find(k); o && !o->empty()) v = *o;
    for (const auto& [k, v] : overlay.entries)
        if (!v.empty() && !out.index.count(k)) appendKey(out, k, v);
    return out;
}

std::string findRuFixes(const std::vector<std::string>& roots) {
    for (const auto& root : roots) {
        if (root.empty()) continue;
        for (const std::string& d : {subDir(subDir(root, "lang"), "ru-fixes"), subDir(root, "ru-fixes")})
            if (!d.empty() && !findFileNoCase(d, "Text.uib").empty()) return d;
    }
    return {};
}

}  // namespace d2k::formats::lang
