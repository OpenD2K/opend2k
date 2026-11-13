#include "platform/movies.hpp"

#include "formats/io.hpp"
#include "platform/audio.hpp"
#include "platform/data_dir.hpp"
#include "platform/language.hpp"
#include "platform/settings.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

namespace d2k::platform {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Text.UIB's movie entries (used when the table is missing or lacks a key).
struct NameStem {
    const char* name;
    const char* stem;
};
constexpr NameStem kDefaultStems[] = {
    {"mve_losta", "a_lose_e"},
    {"mve_losth", "h_lose_e"},
    {"mve_losto", "o_lose_e"},
    {"mve_sneakpeak", "ts_sizzl"},
    {"mve_unfold", "g_maps_e"},
    {"mve_intro1", "g_int1_e"},
    {"mve_intro2", "g_int2_e"},
    {"mve_fast", "t_titl_e"},
    {"mve_house_atreides", "a_mntg_e"},
    {"mve_house_harkonnen", "h_mntg_e"},
    {"mve_house_ordos", "o_mntg_e"},
    {"mve_atreides_wins", "a_finl_e"},
    {"mve_harkonnen_wins", "h_finl_e"},
    {"mve_ordos_wins", "o_finl_e"},
};

}  // namespace

Movies::Movies(SDL_Renderer* r, Settings& settings) : settings_(settings), player_(r) {}

Movies::~Movies() = default;

void Movies::setData(const std::string& dataDir) {
    stop();
    dataDir_ = dataDir;
    text_ = {};
    if (dataDir.empty()) return;
    if (auto p = formats::findFileNoCase(dataDir + "/UI_DATA", "Text.UIB"); !p.empty()) {
        try {
            text_ = formats::loadUib(p);
        } catch (const std::exception& e) {
            SDL_Log("Movies: %s: %s", p.c_str(), e.what());
        }
    }
}

std::string Movies::stemFor(const std::string& name, int house) const {
    std::string st = baseStemFor(name, house);
    // The cutscene language's letter (a_br03_e -> a_br03_f; platform/language.hpp).
    const char letter = activeLanguage().movieLetter;
    if (st.size() > 2 && st[st.size() - 2] == '_' && std::strchr("efgs", st.back()) &&
        std::count(st.begin(), st.end(), '_') >= 2)
        st.back() = letter;
    return st;
}

std::string Movies::baseStemFor(const std::string& name, int house) const {
    std::string key = lower(name);
    if (key == "mve_lost") key += "aho"[std::clamp(house, 0, 2)];
    if (const std::string* v = text_.findNoCase(key); v && !v->empty()) return lower(*v);
    for (const NameStem& d : kDefaultStems)
        if (key == d.name) return d.stem;
    // mve_briefing_<house><n> -> <h>_br0<n>_e
    static const char* houses[] = {"atreides", "harkonnen", "ordos"};
    for (int h = 0; h < 3; ++h) {
        const std::string pre = std::string("mve_briefing_") + houses[h];
        if (key.size() == pre.size() + 1 && key.compare(0, pre.size(), pre) == 0 && key.back() >= '1' &&
            key.back() <= '9')
            return std::string(1, "aho"[h]) + "_br0" + key.back() + "_e";
    }
    return {};
}

std::vector<std::string> Movies::searchFolders() const {
    // The language's own folders first (the Russian translation's dubbed movies).
    std::vector<std::string> out = activeLanguage().movieDirs;
    if (!dataDir_.empty()) out.push_back((fs::path(dataDir_).parent_path() / "movies").string());
    const std::string app = appDirOverride_.empty() ? appMoviesDir() : appDirOverride_;
    if (!app.empty()) out.push_back(app);
    if (const std::string p = portableMoviesDir(); !p.empty()) out.push_back(p);
    // iPad: VQA files (or a movies folder) copied into the app's Documents with the Files app.
    for (const std::string& d : documentsMovieDirs()) out.push_back(d);
    return out;
}

std::string Movies::locate(const std::string& stem) const {
    if (stem.empty()) return {};
    const std::string want = lower(stem) + ".vqa";
    // a_br03_e -> also a_br03_<any letter> (other cutscene languages)
    const bool lang = want.size() > 6 && want[want.size() - 6] == '_';
    std::error_code ec;
    auto fromDisc = [&]() -> std::string {
        if (const formats::ps1::Disc* d = disc()) {
            const std::string entry = ps1EntryFor(stem);
            if (const formats::ps1::FatEntry* e = d->xaEntry(entry)) return "ps1:" + e->name;
        }
        return {};
    };
    // Russian (PS1): the disc's dubbed movies before any PC file.
    if (!activeLanguage().ps1Movies.empty() && activeLanguage().ps1Movies == ps1Image())
        if (std::string p = fromDisc(); !p.empty()) return p;
    for (const std::string& dir : searchFolders()) {
        std::string alt, altE;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (!e.is_regular_file(ec) || e.file_size(ec) == 0) continue;  // 0-byte placeholders
            const std::string n = lower(e.path().filename().string());
            if (n == want) return e.path().string();
            if (lang && n.size() == want.size() && n.compare(0, want.size() - 5, want, 0, want.size() - 5) == 0 &&
                n.compare(n.size() - 4, 4, ".vqa") == 0) {
                if (alt.empty()) alt = e.path().string();
                if (n[n.size() - 5] == 'e') altE = e.path().string();  // English first
            }
        }
        if (!altE.empty()) return altE;
        if (!alt.empty()) return alt;
    }
    return fromDisc();
}

std::string Movies::ps1EntryFor(const std::string& stem) {
    std::string st = lower(stem);
    if (st.size() > 2 && st[st.size() - 2] == '_' && std::count(st.begin(), st.end(), '_') >= 2)
        st.resize(st.size() - 2);  // language letter
    if (st == "a_mntg") return "mntg_atr.str";
    if (st == "h_mntg") return "mntg_har.str";
    if (st == "o_mntg") return "mntg_ord.str";
    std::string up;
    for (char c : st) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return up + ".STR";
}

std::string Movies::ps1Image() const { return ps1Override_.empty() ? settings_.ps1Image : ps1Override_; }

const formats::ps1::Disc* Movies::disc() const {
    const std::string img = ps1Image();
    if (img != discPath_) {
        discPath_ = img;
        disc_.reset();
        if (!img.empty()) {
            auto d = std::make_unique<formats::ps1::Disc>();
            if (d->open(img) && !d->fat().empty()) disc_ = std::move(d);
            else SDL_Log("PS1 disc image %s: %s", img.c_str(), d->error().empty() ? "no DATA.FAT" : d->error().c_str());
        }
    }
    return disc_.get();
}

bool Movies::ps1Ok() const { return disc() != nullptr; }

std::string Movies::checkPs1Image(const std::string& image) {
    formats::ps1::Disc d;
    if (!d.open(image)) return d.error();
    if (!d.xaEntry("A_BR01.STR")) return "not a Dune 2000 PS1 disc (no DATA.XA movies)";
    return {};
}

bool Movies::available(const std::string& name, int house) const { return !locate(stemFor(name, house)).empty(); }

std::unique_ptr<MovieDecoder> Movies::open(const std::string& where, std::string* error) const {
    if (where.rfind("ps1:", 0) == 0) return openPs1Movie(ps1Image(), where.substr(4), error);
    return openVqaMovie(where, error);
}

bool Movies::play(const std::string& name, int house, bool always) {
    if (!always && !settings_.cutscenes) return false;
    const std::string stem = stemFor(name, house);
    const std::string where = locate(stem);
    if (where.empty()) {
        SDL_Log("Movie %s (%s): no file", name.c_str(), stem.empty() ? "?" : stem.c_str());
        return false;
    }
    std::string err;
    auto m = open(where, &err);
    if (!m) {
        SDL_Log("Movie %s: %s", where.c_str(), err.c_str());
        return false;
    }
    SDL_Log("Movie %s: %s (%dx%d, %d frames)", name.c_str(), where.c_str(), m->width(), m->height(), m->frameCount());
    return player_.start(std::move(m), audio_, std::clamp(std::max(settings_.soundVolume, settings_.musicVolume), 0.f, 1.f));
}

bool Movies::playFile(const std::string& path) {
    std::string err;
    auto m = open(path, &err);
    if (!m) {
        SDL_Log("Movie %s: %s", path.c_str(), err.c_str());
        return false;
    }
    return player_.start(std::move(m), audio_, std::clamp(std::max(settings_.soundVolume, settings_.musicVolume), 0.f, 1.f));
}

void Movies::render() {
    while (!player_.active() && !queue_.empty()) {
        const Entry e = queue_.front();
        queue_.erase(queue_.begin());
        play(e.name, e.house);
    }
    player_.render(settings_.enhanced());
}

const std::vector<Movies::Entry>& Movies::gallery() {
    static const std::vector<Entry> list = [] {
        std::vector<Entry> v{{"mve_fast", 0, "Title"},
                             {"mve_intro1", 0, "Introduction"},
                             {"mve_intro2", 0, "Introduction, part 2"},
                             {"mve_unfold", 0, "The planet Arrakis"}};
        static const char* houses[] = {"atreides", "harkonnen", "ordos"};
        static const char* names[] = {"Atreides", "Harkonnen", "Ordos"};
        for (int h = 0; h < 3; ++h) {
            v.push_back({std::string("mve_house_") + houses[h], h, std::string("House ") + names[h]});
            for (int m = 1; m <= 9; ++m)
                v.push_back({std::string("mve_briefing_") + houses[h] + char('0' + m), h,
                             std::string(names[h]) + " mission " + char('0' + m)});
            v.push_back({std::string("mve_") + houses[h] + "_wins", h, std::string(names[h]) + " victory"});
            v.push_back({"mve_lost", h, std::string(names[h]) + " defeat"});
        }
        v.push_back({"mve_sneakpeak", 0, "Tiberian Sun sneak peek"});
        return v;
    }();
    return list;
}

}  // namespace d2k::platform
