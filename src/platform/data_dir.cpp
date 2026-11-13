#include "platform/data_dir.hpp"

#include "formats/io.hpp"
#include "platform/platform_info.hpp"
#include "platform/user_data.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace d2k::platform {

namespace fs = std::filesystem;

bool isDataDir(const std::string& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    if (formats::findFileNoCase(dir, "DATA.R16").empty()) return false;
    const std::string bin = formats::findFileNoCase(dir, "bin");
    return !bin.empty() && !formats::findFileNoCase(bin, "Templates.bin").empty();
}

std::string normalizeDataDir(const std::string& dir) {
    if (dir.empty()) return {};
    if (isDataDir(dir)) return dir;
    for (const char* sub : {"data", "Dune 2000/data", "game/Dune 2000/data"}) {
        const std::string p = (fs::path(dir) / sub).string();
        if (isDataDir(p)) return p;
    }
    return {};
}

std::string gameDataRoot(const std::string& overrideRoot) {
    if (!overrideRoot.empty()) return overrideRoot;
    const std::string pref = prefPath();
    return pref.empty() ? std::string() : (fs::path(pref) / "GameData").string();
}

std::string appMoviesDir(const std::string& overrideRoot) {
    if (!overrideRoot.empty()) return (fs::path(overrideRoot).parent_path() / "Movies").string();
    const std::string pref = prefPath();
    return pref.empty() ? std::string() : (fs::path(pref) / "Movies").string();
}

std::string portableMoviesDir() { return portablePath("Movies"); }

std::string documentsDir() {
    if constexpr (!kTouchPlatform) return {};
#if defined(__ANDROID__)
    // Android: the app's external files folder (Android/data/com.opend2k.app/files): reachable over USB / a file
    // manager without a storage permission; the player copies the "Dune 2000" folder (or the installer) there.
    const char* d = SDL_GetAndroidExternalStoragePath();
#else
    const char* d = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
#endif
    return d ? std::string(d) : std::string();
}

DocumentsScan scanDocuments() {
    DocumentsScan out;
    const std::string docs = documentsDir();
    if (docs.empty()) return out;
    auto lowerExt = [](const fs::path& p) {
        std::string e = p.extension().string();
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return e;
    };
    std::error_code ec;
    std::vector<std::string> bins;
    auto visit = [&](const fs::path& dir, int depth, auto& self) -> void {
        std::size_t vqas = 0;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            const fs::path p = e.path();
            if (p.filename().string().rfind('.', 0) == 0) continue;  // hidden / .Trash
            if (e.is_directory(ec)) {
                if (const std::string d = normalizeDataDir(p.string()); !d.empty()) out.dataDirs.push_back(d);
                else if (depth < 2) self(p, depth + 1, self);
                continue;
            }
            const std::string ext = lowerExt(p);
            if (ext == ".vqa") {
                if (e.file_size(ec) > 0) ++vqas;  // (0-byte placeholders do not count)
                continue;
            }
            if (depth > 1) continue;  // deeper: only movies
            if (ext == ".exe") out.installers.push_back(p.string());
            else if (ext == ".cue") out.ps1Images.push_back(p.string());
            else if (ext == ".bin") bins.push_back(p.string());
        }
        if (vqas) {
            out.movieDirs.push_back(dir.string());
            out.movieFiles += vqas;
        }
    };
    visit(fs::path(docs), 0, visit);
    if (out.ps1Images.empty()) out.ps1Images = bins;  // a lone .bin works without its .cue
    for (auto* v : {&out.dataDirs, &out.installers, &out.ps1Images, &out.movieDirs}) std::sort(v->begin(), v->end());
    return out;
}

bool isCutsceneInstallerName(const std::string& path) {
    std::string n = fs::path(path).filename().string();
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return n.find("cutscene") != std::string::npos || n.find("movie") != std::string::npos;
}

std::vector<std::string> documentsMovieDirs() {
    if constexpr (!kTouchPlatform) return {};
    static std::vector<std::string> dirs;
    static Uint64 at = 0;
    const Uint64 now = SDL_GetTicks();
    if (at == 0 || now - at > 3000) {
        dirs = scanDocuments().movieDirs;
        at = now;
    }
    return dirs;
}

std::vector<std::string> usualInstallDirs() {
    std::vector<std::string> out;
    auto env = [](const char* name) {
        const char* v = std::getenv(name);
        return v && *v ? fs::path(v) : fs::path();
    };
    auto add = [&](const fs::path& base, const char* sub) {
        if (!base.empty()) out.push_back((base / sub).string());
    };
#if defined(_WIN32)
    // std::getenv gives UTF-8 here: the executables' manifest sets the UTF-8 code page.
    add(env("ProgramFiles(x86)"), "Dune 2000");
    add(env("ProgramFiles"), "Dune 2000");
    const char* sd = std::getenv("SystemDrive");  // "C:" (a bare "C:" would be drive-relative)
    const fs::path drive = std::string(sd && *sd ? sd : "C:") + "/";
    add(drive / "Games", "Dune 2000");
    add(drive / "Westwood", "Dune2000");
    add(drive, "Dune 2000");
#elif defined(__linux__)
    const fs::path home = env("HOME");
    add(home / "Games", "Dune 2000");
    const fs::path wine = env("WINEPREFIX").empty() ? home / ".wine" : env("WINEPREFIX");
    if (!home.empty() || !env("WINEPREFIX").empty()) {
        add(wine / "drive_c" / "Program Files (x86)", "Dune 2000");
        add(wine / "drive_c" / "Program Files", "Dune 2000");
        add(wine / "drive_c" / "Games", "Dune 2000");
    }
#endif
    (void)env;  // (unused on macOS / iPadOS)
    (void)add;
    return out;
}

std::string discoverDataDir(const std::string& cliDir, const std::string& savedDir, std::vector<std::string>* tried) {
    auto attempt = [&](const std::string& d) -> std::string {
        if (d.empty()) return {};
        if (tried) tried->push_back(d);
        return normalizeDataDir(d);
    };
    if (!cliDir.empty()) return attempt(cliDir);  // an explicit --data is not second-guessed
    // A portable build's own data (platform/user_data.hpp) wins over a saved folder.
    if (const std::string p = portablePath("Dune 2000/data"); !p.empty())
        if (auto d = attempt(p); !d.empty()) return d;
    if (auto d = attempt(savedDir); !d.empty()) return d;
    // Next to the executable. Inside "Dune 2000.app/Contents/MacOS" (SDL_GetBasePath returns
    // Contents/Resources/ for bundles) walk up to the folder that holds the .app.
    if (const char* base = SDL_GetBasePath()) {
        fs::path p = fs::path(base).lexically_normal();
        if (p.filename().empty()) p = p.parent_path();
        for (int up = 0; up < 5 && !p.empty(); ++up) {
            if (p.extension() != ".app" && p.filename() != "Contents" && p.filename() != "Resources" &&
                p.filename() != "MacOS")
                if (auto d = attempt(p.string()); !d.empty()) return d;
            if (p == p.parent_path()) break;
            p = p.parent_path();
        }
    }
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (!ec) {
        if (auto d = attempt((cwd / "game" / "Dune 2000" / "data").string()); !d.empty()) return d;
    }
    // Windows / Linux: an existing installation in a usual place (Gruntmods Edition installs a
    // "Dune 2000" folder with data/ inside; Linux players often have it under Wine).
    for (const std::string& d : usualInstallDirs())
        if (auto found = attempt(d); !found.empty()) return found;
    // iPad: a game folder copied into the app's Documents (Files app / Finder).
    for (const std::string& d : scanDocuments().dataDirs)
        if (auto found = attempt(d); !found.empty()) return found;
    // Copies built from the installer (".d2k-extracted" marks a finished one); newest first.
    const fs::path root = gameDataRoot();
    fs::path best;
    fs::file_time_type bestTime{};
    for (const auto& e : fs::directory_iterator(root, ec)) {
        const fs::path marker = e.path() / ".d2k-extracted";
        if (!e.is_directory(ec) || !fs::exists(marker, ec)) continue;
        const auto t = fs::last_write_time(marker, ec);
        if (best.empty() || t > bestTime) { best = e.path(); bestTime = t; }
    }
    if (!best.empty()) return attempt((best / "Dune 2000").string());
    return {};
}

}  // namespace d2k::platform
