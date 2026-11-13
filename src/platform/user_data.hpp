#pragma once
// The per-user app data folder: settings.ini, saves/, highscores, community progress, GameData/
// (copies built from the installer), Movies/ (imported cutscenes), Voices/, previews/, artpacks/
// (downloaded art packs). It lives outside the app, so replacing the app (a new .app / zip / apk)
// keeps all of it; the game never writes into the app bundle or next to the executable.
//
// SDL_GetPrefPath(kPrefOrg, kPrefApp) - fixed strings, NOT the app's display name (D2K_APP_NAME is
// "OpenD2K" in release builds and "Dune 2000" in development builds: both share this folder):
//   macOS    ~/Library/Application Support/d2k-mac/Dune 2000/
//   Windows  %APPDATA%\d2k-mac\Dune 2000\
//   Linux    $XDG_DATA_HOME/d2k-mac/Dune 2000/ (~/.local/share/d2k-mac/Dune 2000/)
//   iOS      <app sandbox>/Library/Application Support/d2k-mac/Dune 2000/ (kept across updates)
//   Android  the app's internal storage (files/; kept across updates)
// Never change these strings: existing players would lose their settings, saves and downloads.
// (render/art_pack.cpp ArtPack::userDir uses the same pair.)
//
// Portable mode (a self-contained private build, tools/release/package_macos_portable.sh): when a
// "portable" folder ships with the app - macOS <App>.app/Contents/Resources/portable/, Windows/Linux
// portable/ next to the executable (or D2K_PORTABLE_DIR) - it holds everything read-only:
//   portable/Dune 2000/data      the game data (wins over the saved data folder; --data still overrides)
//   portable/ru-pc/              the Russian PC translation (found next to "Dune 2000" as usual)
//   portable/artpacks/           art packs (searched after the writable artpacks/)
//   portable/Voices/ru-ai/       the AI Russian voices
//   portable/Movies/             cutscenes (searched after the writable Movies/)
//   portable/ps1/                PS1 disc images (settings may name them as "@portable/ps1/...")
//   portable/settings-default.ini  the settings of a first start (no settings.ini yet)
// and prefPath() (settings.ini, saves, highscores, previews, downloads) becomes "OpenD2K Data/" next
// to the app (beside the .app / the executable), or - when that is not writable (run from a DMG) -
// <temp>/OpenD2K Data/. Nothing goes to the normal per-user folder. Without the folder nothing changes.

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace d2k::platform {

inline constexpr const char* kPrefOrg = "d2k-mac";
inline constexpr const char* kPrefApp = "Dune 2000";

namespace user_data_detail {
inline std::string withSlash(std::string d) {
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
    return d;
}
inline bool writableDir(const std::filesystem::path& d) {
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    if (!std::filesystem::is_directory(d, ec)) return false;
    const std::filesystem::path probe = d / ".write-test";
    { std::ofstream f(probe); if (!f) return false; f << "ok"; if (!f) return false; }
    std::filesystem::remove(probe, ec);
    return true;
}
// The folder that holds the app: the .app's parent on macOS, the executable's folder elsewhere.
inline std::filesystem::path appContainerDir() {
    const char* base = SDL_GetBasePath();
    if (!base) return {};
    std::filesystem::path p = std::filesystem::path(base).lexically_normal();
    if (p.filename().empty()) p = p.parent_path();
    for (std::filesystem::path q = p; !q.empty() && q != q.parent_path(); q = q.parent_path())
        if (q.extension() == ".app") return q.parent_path();
    return p;
}
}  // namespace user_data_detail

// The portable folder with a trailing separator, "" when this is a normal install (computed once).
inline const std::string& portableDir() {
    static const std::string dir = [] {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path p;
        if (const char* env = SDL_getenv("D2K_PORTABLE_DIR"); env && *env) p = env;
        else if (const char* base = SDL_GetBasePath()) p = fs::path(base) / "portable";  // macOS: .../Contents/Resources/
        else return std::string();
        if (!fs::is_directory(p, ec)) return std::string();
        const std::string d = user_data_detail::withSlash(p.lexically_normal().string());
        SDL_Log("Portable mode: read-only content in %s", d.c_str());
        return d;
    }();
    return dir;
}
inline bool isPortable() { return !portableDir().empty(); }

// portableDir() + sub when that exists, else "".
inline std::string portablePath(const std::string& sub) {
    if (!isPortable()) return {};
    std::error_code ec;
    const std::filesystem::path p = std::filesystem::path(portableDir()) / sub;
    return std::filesystem::exists(p, ec) ? p.string() : std::string();
}

// Settings path values may be written relative to the portable folder ("@portable/ps1/x.cue").
inline constexpr const char* kPortablePrefix = "@portable/";
inline std::string expandPortablePath(const std::string& v) {
    const std::string pre = kPortablePrefix;
    if (v.compare(0, pre.size(), pre) != 0) return v;
    return isPortable() ? portableDir() + v.substr(pre.size()) : std::string();
}
inline std::string contractPortablePath(const std::string& v) {
    if (!isPortable() || v.empty()) return v;
    const std::string& d = portableDir();
    return v.compare(0, d.size(), d) == 0 ? kPortablePrefix + v.substr(d.size()) : v;
}

// The writable folder of a portable build (with a trailing separator; computed once).
inline const std::string& portableWritableDir() {
    static const std::string dir = [] {
        namespace fs = std::filesystem;
        const fs::path beside = user_data_detail::appContainerDir() / "OpenD2K Data";
        if (!user_data_detail::appContainerDir().empty() && user_data_detail::writableDir(beside)) {
            SDL_Log("Portable mode: settings and saves in %s", beside.string().c_str());
            return user_data_detail::withSlash(beside.string());
        }
        std::error_code ec;
        const fs::path tmp = fs::temp_directory_path(ec) / "OpenD2K Data";
        if (!ec && user_data_detail::writableDir(tmp)) {
            SDL_Log("Portable mode: %s is not writable; settings and saves in %s", beside.string().c_str(), tmp.string().c_str());
            return user_data_detail::withSlash(tmp.string());
        }
        SDL_Log("Portable mode: no writable folder (tried %s); settings are not kept", beside.string().c_str());
        return std::string();
    }();
    return dir;
}

// The folder with a trailing separator, created if needed; "" when SDL has none.
// Tests: D2K_PREF_DIR=<folder> uses that folder instead (settings.ini, saves, previews...; a fresh
// folder = a new player; it also wins over portable mode). The art pack folder (render::ArtPack::userDir) has its own --artpack-dir.
inline std::string prefPath() {
    if (const char* env = SDL_getenv("D2K_PREF_DIR"); env && *env) {
        std::string d = env;
        SDL_CreateDirectory(d.c_str());
        if (d.back() != '/' && d.back() != '\\') d += '/';
        return d;
    }
    if (isPortable()) return portableWritableDir();
    std::string s;
    if (char* p = SDL_GetPrefPath(kPrefOrg, kPrefApp)) {
        s = p;
        SDL_free(p);
    }
    return s;
}

}  // namespace d2k::platform
