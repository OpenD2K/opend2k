#include "render/art_pack.hpp"

#include "platform/user_data.hpp"
#include "render/mod_art.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace d2k::render {

namespace fs = std::filesystem;

bool loadPng(const std::string& path, ArtImage& out) {
    SDL_Surface* s = SDL_LoadPNG(path.c_str());
    if (!s) return false;
    SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(s);
    if (!c) return false;
    out.w = c->w;
    out.h = c->h;
    out.px.resize(static_cast<std::size_t>(c->w) * c->h);
    for (int y = 0; y < c->h; ++y)
        std::memcpy(out.px.data() + static_cast<std::size_t>(y) * c->w,
                    static_cast<const std::uint8_t*>(c->pixels) + static_cast<std::size_t>(y) * c->pitch,
                    static_cast<std::size_t>(c->w) * 4);
    SDL_DestroySurface(c);
    return !out.empty();
}

namespace {

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Folder `name` inside `dir`, matched case-insensitively (packs made on case-sensitive systems).
std::string findChild(const std::string& dir, const std::string& name) {
    std::error_code ec;
    if (fs::is_directory(fs::path(dir) / name, ec)) return (fs::path(dir) / name).string();
    const std::string want = lower(name);
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_directory(ec) && lower(e.path().filename().string()) == want) return e.path().string();
    return {};
}

// "<n>.png", "<n>.house.png", "<n>.shadow.png", "<n>.glow.png" -> n and the kind (0 base, 1 house, 2 shadow,
// 3 glow).
bool parseFrameName(const std::string& file, int& n, int& kind) {
    const std::string f = lower(file);
    if (f.size() < 5 || f.compare(f.size() - 4, 4, ".png") != 0) return false;
    std::string stem = f.substr(0, f.size() - 4);
    kind = 0;
    if (stem.size() > 6 && stem.compare(stem.size() - 6, 6, ".house") == 0) { kind = 1; stem.resize(stem.size() - 6); }
    else if (stem.size() > 7 && stem.compare(stem.size() - 7, 7, ".shadow") == 0) { kind = 2; stem.resize(stem.size() - 7); }
    else if (stem.size() > 5 && stem.compare(stem.size() - 5, 5, ".glow") == 0) { kind = 3; stem.resize(stem.size() - 5); }
    if (stem.empty() || stem.size() > 6 || !std::all_of(stem.begin(), stem.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return false;
    n = std::stoi(stem);
    return true;
}

std::string& userDirOverride() {
    static std::string d;
    return d;
}
std::string& exclusiveDir() {
    static std::string d;
    return d;
}
std::unordered_map<std::string, bool>& redesignCache() {
    static std::unordered_map<std::string, bool> cache;
    return cache;
}
int gGeneration = 0;

}  // namespace

std::string ArtPack::userDir() {
    if (!userDirOverride().empty()) return userDirOverride();
    // A portable build (platform/user_data.hpp): downloads go to its writable folder.
    if (platform::isPortable()) {
        const std::string w = platform::portableWritableDir();
        return w.empty() ? std::string() : (fs::path(w) / "artpacks").string();
    }
    std::string dir;
    if (char* pref = SDL_GetPrefPath("d2k-mac", "Dune 2000")) {
        dir = (fs::path(pref) / "artpacks").string();
        SDL_free(pref);
    }
    return dir;
}

void ArtPack::setUserDirOverride(const std::string& dir) { userDirOverride() = dir; }
void ArtPack::setExclusiveDir(const std::string& dir) {
    exclusiveDir() = dir;
    invalidate();
}

void ArtPack::invalidate() {
    redesignCache().clear();
    ++gGeneration;
}

int ArtPack::generation() { return gGeneration; }

std::vector<std::string> ArtPack::searchDirs() {
    std::vector<std::string> dirs;
    auto add = [&](const fs::path& p) {
        std::error_code ec;
        if (!fs::is_directory(p, ec)) return;
        const std::string s = fs::weakly_canonical(p, ec).string();
        if (std::find(dirs.begin(), dirs.end(), s) == dirs.end()) dirs.push_back(s);
    };
    if (!exclusiveDir().empty()) {
        add(fs::path(exclusiveDir()));
        return dirs;
    }
    if (const std::string user = userDir(); !user.empty()) add(fs::path(user));
    if (const std::string p = platform::portablePath("artpacks"); !p.empty()) add(fs::path(p));
    if (const char* base = SDL_GetBasePath()) {
        // SDL returns ".../Dune 2000.app/Contents/Resources/" (trailing slash): without the slash every parent_path()
        // goes up one real level (the app bundle is 4 levels below the project folder).
        fs::path p = fs::path(base).lexically_normal();
        if (p.filename().empty()) p = p.parent_path();
        for (int up = 0; up < 6 && !p.empty(); ++up) {
            add(p / "artpacks");
            if (p == p.parent_path()) break;
            p = p.parent_path();
        }
    }
    std::error_code ec;
    add(fs::current_path(ec) / "artpacks");
    return dirs;
}

std::vector<std::string> ArtPack::available() {
    std::vector<std::string> names;
    for (const auto& d : searchDirs()) {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(d, ec))
            if (e.is_directory(ec) && fs::exists(e.path() / "pack.ini", ec)) names.push_back(e.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

std::string ArtPack::threeDPack(const std::string& preferred) {
    static std::vector<std::string> packs;  // folders with models/ and DATA/, sorted
    static Uint64 scanned = 0;
    const Uint64 now = SDL_GetTicks();
    if (scanned == 0 || now - scanned > 2000) {
        scanned = now + 1;
        packs.clear();
        for (const auto& d : searchDirs()) {
            std::error_code ec;
            for (const auto& e : fs::directory_iterator(d, ec))
                if (e.is_directory(ec) && fs::exists(e.path() / "pack.ini", ec) && fs::is_directory(e.path() / "models", ec) &&
                    fs::is_directory(e.path() / "DATA", ec))
                    packs.push_back(e.path().filename().string());
        }
        std::sort(packs.begin(), packs.end());
        packs.erase(std::unique(packs.begin(), packs.end()), packs.end());
    }
    if (std::find(packs.begin(), packs.end(), preferred) != packs.end()) return preferred;
    return packs.empty() ? std::string() : packs.front();
}

bool ArtPack::isRedesign(const std::string& name) {
    if (name.empty() || name == "original" || name == "upscaled") return false;
    auto& cache = redesignCache();
    if (const auto it = cache.find(name); it != cache.end()) return it->second;
    bool redesign = false;
    for (const auto& d : searchDirs()) {
        const fs::path ini = fs::path(d) / name / "pack.ini";
        std::error_code ec;
        if (!fs::exists(ini, ec)) continue;
        std::ifstream in(ini);
        std::string line;
        while (std::getline(in, line)) {
            const std::string l = lower(trim(line));
            if (l.find("render_pack.py") != std::string::npos) redesign = true;  // packs written before kind=
            const auto eq = l.find('=');
            if (l.empty() || l[0] == ';' || l[0] == '#' || eq == std::string::npos) continue;
            const std::string v = trim(l.substr(eq + 1));
            if (trim(l.substr(0, eq)) == "kind") redesign = v == "rendered" || v == "redesign";
        }
        break;
    }
    cache.emplace(name, redesign);
    return redesign;
}

std::unique_ptr<ArtPack> ArtPack::open(const std::string& name) {
    if (name.empty()) return nullptr;
    for (const auto& d : searchDirs()) {
        const fs::path root = fs::path(d) / name;
        std::error_code ec;
        if (!fs::exists(root / "pack.ini", ec)) continue;
        auto p = std::make_unique<ArtPack>();
        p->folder_ = name;
        p->title_ = name;
        p->root_ = root.string();
        std::ifstream in(root / "pack.ini");
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = lower(trim(line.substr(0, eq))), v = trim(line.substr(eq + 1));
            try {
                if (k == "name") p->title_ = v;
                else if (k == "scale") p->scale_ = std::clamp(std::stoi(v), 1, 16);
                else if (k == "derive_shadows") p->deriveShadows_ = v != "0";
                else if (k == "derive_house") p->deriveHouse_ = v != "0";
            } catch (const std::exception&) {
                SDL_Log("pack.ini: bad value for %s", k.c_str());
            }
        }
        const std::string data = findChild(p->root_, "DATA");
        if (!data.empty()) {
            for (const auto& e : fs::directory_iterator(data, ec)) {
                int n = 0, kind = 0;
                if (!e.is_regular_file(ec) || !parseFrameName(e.path().filename().string(), n, kind)) continue;
                (kind == 0 ? p->frames_ : kind == 1 ? p->houseMasks_ : kind == 2 ? p->shadowMasks_ : p->glowMasks_).insert(n);
            }
            // hotspots.txt: "<frame> <x> <y>" per line (texels), '#' / ';' comments.
            std::ifstream hs(fs::path(data) / "hotspots.txt");
            while (std::getline(hs, line)) {
                line = trim(line);
                if (line.empty() || line[0] == '#' || line[0] == ';') continue;
                int n = 0, x = 0, y = 0;
                if (std::sscanf(line.c_str(), "%d %d %d", &n, &x, &y) == 3 && n >= 0) p->hotspots_[n] = {x, y};
            }
        }
        SDL_Log("Art pack '%s' (%s): scale %d, %d frames", p->title_.c_str(), p->root_.c_str(), p->scale_,
                p->frameCount());
        return p;
    }
    SDL_Log("Art pack '%s' not found (searched %zu folders)", name.c_str(), searchDirs().size());
    return nullptr;
}

int ArtPack::packFrame(int frame) const { return mod_ ? mod_->packFrame(frame) : frame; }

bool ArtPack::hasFrame(int frame) const {
    const int f = packFrame(frame);
    return f >= 0 && frames_.count(f) != 0;
}

bool ArtPack::hotspot(int frame, int& x, int& y) const {
    frame = packFrame(frame);
    const auto it = hotspots_.find(frame);
    if (it == hotspots_.end()) return false;
    x = it->second.first;
    y = it->second.second;
    return true;
}

bool ArtPack::loadFrame(int frame, ArtImage& base, ArtImage& house, ArtImage& shadow) const {
    if (!hasFrame(frame)) return false;
    frame = packFrame(frame);
    const std::string data = findChild(root_, "DATA");
    const std::string stem = (fs::path(data) / std::to_string(frame)).string();
    if (!loadPng(stem + ".png", base)) {
        SDL_Log("Art pack: cannot read %s.png: %s", stem.c_str(), SDL_GetError());
        return false;
    }
    if (houseMasks_.count(frame)) loadPng(stem + ".house.png", house);
    if (shadowMasks_.count(frame)) loadPng(stem + ".shadow.png", shadow);
    return true;
}

bool ArtPack::loadGlow(int frame, ArtImage& glow) const {
    frame = packFrame(frame);
    if (!glowMasks_.count(frame)) return false;
    const std::string data = findChild(root_, "DATA");
    return loadPng((fs::path(data) / (std::to_string(frame) + ".glow.png")).string(), glow);
}

const ArtPack::TileDir& ArtPack::tileDir(const std::string& tileset) const {
    const std::string key = lower(tileset);
    if (auto it = tileDirs_.find(key); it != tileDirs_.end()) return it->second;
    TileDir d;
    d.path = findChild(root_, tileset);
    std::error_code ec;
    if (!d.path.empty())
        for (const auto& e : fs::directory_iterator(d.path, ec)) {
            int n = 0, kind = 0;
            if (e.is_regular_file(ec) && parseFrameName(e.path().filename().string(), n, kind) && kind == 0) d.tiles.insert(n);
        }
    return tileDirs_.emplace(key, std::move(d)).first->second;
}

int ArtPack::tileCount(const std::string& tileset) const { return static_cast<int>(tileDir(tileset).tiles.size()); }

bool ArtPack::loadTile(const std::string& tileset, int tile, ArtImage& out) const {
    if (mod_ && (tile = mod_->packTile(tileset, tile)) < 0) return false;
    const TileDir& d = tileDir(tileset);
    if (!d.tiles.count(tile)) return false;
    return loadPng((fs::path(d.path) / (std::to_string(tile) + ".png")).string(), out);
}

bool ArtPack::hasSection(const std::string& section) const { return !findChild(root_, section).empty(); }

bool ArtPack::loadImage(const std::string& section, int index, ArtImage& out) const {
    const std::string dir = findChild(root_, section);
    if (dir.empty()) return false;
    const fs::path p = fs::path(dir) / (std::to_string(index) + ".png");
    std::error_code ec;
    return fs::exists(p, ec) && loadPng(p.string(), out);
}

std::vector<std::uint32_t> downsample2(const std::vector<std::uint32_t>& src, int w, int h, int& ow, int& oh) {
    ow = (w + 1) / 2;
    oh = (h + 1) / 2;
    std::vector<std::uint32_t> dst(static_cast<std::size_t>(ow) * oh);
    for (int y = 0; y < oh; ++y)
        for (int x = 0; x < ow; ++x) {
            std::uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const int sx = std::min(w - 1, x * 2 + dx), sy = std::min(h - 1, y * 2 + dy);
                    const std::uint32_t c = src[static_cast<std::size_t>(sy) * w + sx];
                    const std::uint32_t ca = c >> 24;
                    r += (c & 0xFF) * ca;
                    g += ((c >> 8) & 0xFF) * ca;
                    b += ((c >> 16) & 0xFF) * ca;
                    a += ca;
                    ++n;
                }
            std::uint32_t out = 0;
            if (a > 0) {
                out = (std::min<std::uint32_t>(255, r / a)) | (std::min<std::uint32_t>(255, g / a) << 8) |
                      (std::min<std::uint32_t>(255, b / a) << 16) | ((a / n) << 24);
            }
            dst[static_cast<std::size_t>(y) * ow + x] = out;
        }
    return dst;
}

std::vector<std::uint32_t> resample(const std::vector<std::uint32_t>& src, int w, int h, int ow, int oh) {
    std::vector<std::uint32_t> dst(static_cast<std::size_t>(ow) * oh);
    for (int y = 0; y < oh; ++y)
        for (int x = 0; x < ow; ++x) {
            // Area-ish: average the source texels covered by this output texel (box), which is
            // also correct for upscaling (covers one texel -> nearest).
            const float fx0 = float(x) * w / ow, fx1 = float(x + 1) * w / ow;
            const float fy0 = float(y) * h / oh, fy1 = float(y + 1) * h / oh;
            const int x0 = std::min(w - 1, int(fx0)), x1 = std::max(x0 + 1, std::min(w, int(std::ceil(fx1))));
            const int y0 = std::min(h - 1, int(fy0)), y1 = std::max(y0 + 1, std::min(h, int(std::ceil(fy1))));
            std::uint64_t r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx) {
                    const std::uint32_t c = src[static_cast<std::size_t>(sy) * w + sx], ca = c >> 24;
                    r += (c & 0xFF) * ca;
                    g += ((c >> 8) & 0xFF) * ca;
                    b += ((c >> 16) & 0xFF) * ca;
                    a += ca;
                    ++n;
                }
            dst[static_cast<std::size_t>(y) * ow + x] =
                a ? std::uint32_t(r / a) | std::uint32_t(g / a) << 8 | std::uint32_t(b / a) << 16 | std::uint32_t(a / n) << 24 : 0;
        }
    return dst;
}

}  // namespace d2k::render
