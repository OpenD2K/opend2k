#include "platform/language.hpp"

#include "formats/io.hpp"
#include "formats/ps1_disc.hpp"
#include "platform/data_dir.hpp"
#include "platform/settings.hpp"
#include "platform/user_data.hpp"
#include "render/art_pack.hpp"

#include <SDL3/SDL.h>

#include <filesystem>
#include <map>
#include <mutex>

namespace d2k::platform {

namespace fs = std::filesystem;
namespace lang = formats::lang;

namespace {

lang::Sources gSources;
lang::Resolved gResolved;

lang::Id idOr(const std::string& code, lang::Id def) {
    if (code.empty()) return def;
    const auto id = lang::fromCode(code);
    return id ? *id : def;
}

}  // namespace

lang::Selection languageSelection(const Settings& s) {
    lang::Selection sel;
    const lang::Id base = idOr(s.language, lang::Id::English);
    sel.text = idOr(s.languageText, base);
    sel.voices = idOr(s.languageVoices, base);
    sel.movies = idOr(s.languageMovies, base);
    sel.ruTextFixes = s.russianTextFixes;
    return sel;
}

std::string languageKey(const Settings& s) {
    return s.language + "|" + s.languageText + "|" + s.languageVoices + "|" + s.languageMovies + "|" + s.translationDir + "|" + s.aiVoicesDir + "|" +
           s.ps1Image + "|" + (s.russianTextFixes ? "fixes" : "fargus");
}

std::string defaultTranslationDir(const std::string& dataDir) {
    if (dataDir.empty()) return {};
    // data -> "Dune 2000" -> its parent.
    const fs::path game = fs::path(dataDir).lexically_normal().parent_path();
    const fs::path parent = game.parent_path();
    std::error_code ec;
    for (const char* name : {"ru-pc", "ru_pc", "Russian"}) {
        const std::string p = formats::findFileNoCase(parent.string(), name);
        if (!p.empty() && fs::is_directory(p, ec)) return p;
    }
    return {};
}

std::string translationDir(const Settings& s, const std::string& dataDir) {
    return s.translationDir.empty() ? defaultTranslationDir(dataDir) : s.translationDir;
}

std::string defaultAiVoicesDir() {
    if (const std::string p = portablePath("Voices/ru-ai"); !p.empty()) return p;  // a portable build
    const std::string pref = prefPath();
    return pref.empty() ? std::string() : (fs::path(pref) / "Voices" / "ru-ai").string();
}

std::string aiVoicesDir(const Settings& s) { return s.aiVoicesDir.empty() ? defaultAiVoicesDir() : s.aiVoicesDir; }

bool ps1DiscIsRussian(const std::string& image) {
    if (image.empty()) return false;
    static std::mutex m;
    static std::map<std::string, bool> cache;
    std::lock_guard lock(m);
    if (auto it = cache.find(image); it != cache.end()) return it->second;
    bool ru = false;
    formats::ps1::Disc d;
    formats::ps1::Disc::DirEntry mix;
    if (d.open(image) && d.find("DATA.MIX", mix)) {
        bool banks = false;
        const formats::ps1::FatEntry* text = nullptr;
        for (const auto& e : d.fat()) {
            if (e.xa) continue;
            std::string n = e.name;
            for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (n == "satr.dat") banks = true;
            if (n == "d2ktext.dat") text = &e;
        }
        if (banks && text && text->size < 1u << 20) {
            // d2ktext.dat: the Russian disc writes Russian with Latin look-alike codes for its
            // hacked font ("XAPKOHHEH" = ХАРКОННЕН); the English one says "HARKONNEN".
            const auto bytes = d.readForm1(mix.lba + text->sector, text->size);
            const std::string s(bytes.begin(), bytes.end());
            ru = s.find("XAPKOHHEH") != std::string::npos;
        }
    }
    cache[image] = ru;
    return ru;
}

void applyLanguage(const Settings& s, const std::string& dataDir, const std::string& ps1Override,
                   const std::string& appMoviesDir) {
    lang::Sources src;
    src.dataDir = dataDir;
    src.translation = lang::scanTranslation(translationDir(s, dataDir));
    // The Russian text fixes (docs/russian-translation-review.md): artpacks/lang/ru-fixes (the art
    // pack folders: pref path, next to the executable, the working directory) or <translation>/ru-fixes.
    {
        std::vector<std::string> roots = render::ArtPack::searchDirs();
        roots.push_back(src.translation.root);
        src.ruFixesDir = lang::findRuFixes(roots);
    }
    src.ps1Image = ps1Override.empty() ? s.ps1Image : ps1Override;
    src.aiVoicesRoot = aiVoicesDir(s);
    src.aiVoicesDir = lang::findAiVoices(src.aiVoicesRoot);
    src.ps1Russian = ps1DiscIsRussian(src.ps1Image);
    if (!dataDir.empty()) src.movieDirs.push_back((fs::path(dataDir).parent_path() / "movies").string());
    if (!appMoviesDir.empty()) src.movieDirs.push_back(appMoviesDir);
    if (const std::string p = portableMoviesDir(); !p.empty()) src.movieDirs.push_back(p);
    gSources = src;
    gResolved = lang::resolve(languageSelection(s), src);
    lang::setActiveCodepage(gResolved.codepage);
    SDL_Log("Language: text %s, voices %s, cutscenes %s%s%s%s%s", lang::code(gResolved.sel.text), lang::code(gResolved.sel.voices),
            lang::code(gResolved.sel.movies), src.translation.root.empty() ? "" : "; translation folder ",
            src.translation.root.c_str(), gResolved.textOverlayUib.empty() ? "" : "; text fixes ", gResolved.textOverlayUib.c_str());
    if (gResolved.sel.voices == lang::Id::RussianAi) SDL_Log("Language: AI voices %s", src.aiVoicesDir.c_str());
}

const lang::Resolved& activeLanguage() { return gResolved; }
const lang::Sources& languageSources() { return gSources; }

formats::StringTable loadGameText(const std::string& dataDir, const std::string& basePath) {
    using formats::findFileNoCase;
    const std::string ui = findFileNoCase(dataDir, "UI_DATA");
    const std::string textPath = ui.empty() ? std::string() : findFileNoCase(ui, "Text.UIB");
    formats::StringTable base;
    const std::string bp = basePath.empty() ? textPath : basePath;
    if (!bp.empty()) base = formats::loadUib(bp);
    const lang::Resolved& r = gResolved;
    // Text.UIB is Latin (CP1252); a mission's own table may be anything (formats/language.hpp
    // detectCodepage). Its values are kept in the language's code page, so the untranslated
    // ones draw right with the language's font ("Grâce" not "Grвce" with the Cyrillic one).
    const int baseCp = basePath.empty() || basePath == textPath ? 1252 : lang::detectCodepage(base);
    lang::recodeValues(base, baseCp, r.codepage);
    if (r.textUib.empty()) return base;
    try {
        formats::StringTable translated = formats::loadUib(r.textUib);
        if (!r.textOverlayUib.empty()) {
            try {
                translated = lang::applyOverlay(translated, formats::loadUib(r.textOverlayUib));
            } catch (const std::exception& e) {
                SDL_Log("Language text fixes %s: %s", r.textOverlayUib.c_str(), e.what());
            }
        }
        // The base table's English: Text.UIB itself, or (for a mission's own table) Text.UIB.
        if (basePath.empty() || basePath == textPath) return lang::mergeTranslation(base, base, translated);
        formats::StringTable english = textPath.empty() ? formats::StringTable{} : formats::loadUib(textPath);
        lang::recodeValues(english, 1252, r.codepage);  // compared with the recoded base values
        return lang::mergeTranslation(base, english, translated);
    } catch (const std::exception& e) {
        SDL_Log("Language text %s: %s", r.textUib.c_str(), e.what());
        return base;
    }
}

formats::Font loadGameFont(const std::string& dataDir, bool col) {
    using formats::findFileNoCase;
    const lang::Resolved& r = gResolved;
    std::string fnt = col ? r.fontColFnt : r.fontFnt;
    if (fnt.empty()) fnt = findFileNoCase(dataDir, col ? "FONTCOL.FNT" : "FONT.FNT");
    std::string bin = r.fontBin;
    if (bin.empty()) bin = findFileNoCase(findFileNoCase(dataDir, "bin"), "FONT.BIN");
    if (fnt.empty()) throw std::runtime_error(col ? "FONTCOL.FNT not found" : "FONT.FNT not found");
    formats::Font f = formats::loadFont(fnt, bin);
    // The Russian map draws 0x92 (CP1251 right quote, used as an apostrophe in the text) as a
    // space; draw it as the apostrophe.
    if (r.codepage == 1251 && f.charMap[0x92] == 0x20) f.charMap[0x92] = f.charMap[static_cast<unsigned char>('\'')];
    return f;
}

std::string uiR16Path(const std::string& dataDir) {
    if (!gResolved.uiR16.empty()) return gResolved.uiR16;
    return formats::findFileNoCase(dataDir, "UI_ENG.R16");
}

const std::string& uiGfxOverlayDir() { return gResolved.uiGfxDir; }

}  // namespace d2k::platform
