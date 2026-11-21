#pragma once
// The per-user app data folder: settings.ini, saves/, highscores, community progress, GameData/
// (copies built from the installer), Movies/ (imported cutscenes), Voices/, previews/, artpacks/
// (downloaded art packs). It lives outside the app, so replacing the app (a new .app / zip / apk)
// keeps all of it; the game never writes into the app bundle or next to the executable.
//
// SDL_GetPrefPath(kPrefOrg, kPrefApp) - fixed strings, NOT the app's display name (D2K_APP_NAME is
// "OpenD2K" in release builds and "Dune 2000" in development builds: both share this folder):
//   macOS    ~/Library/Application Support/OpenD2K/Dune 2000/
//   Windows  %APPDATA%\OpenD2K\Dune 2000\ (roaming profile)
//   Linux    $XDG_DATA_HOME/OpenD2K/Dune 2000/ (~/.local/share/OpenD2K/Dune 2000/)
//   iOS      <app sandbox>/Library/Application Support/OpenD2K/Dune 2000/ (kept across updates)
//   Android  the app's internal storage (files/; kept across updates)
// Up to 0.0.2 the folder was ".../d2k-mac/Dune 2000/" (the repo's old name): the first start of a
// newer build moves it to the new place (migrateOldPrefFolder) so settings, saves and downloads are
// kept. Never change these strings again without the same kind of move.
// (render/art_pack.cpp ArtPack::userDir goes through prefPath().)
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

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace d2k::platform {

inline constexpr const char* kPrefOrg = "OpenD2K";
inline constexpr const char* kOldPrefOrg = "d2k-mac";  // up to 0.0.2
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
// Copies the folder `from` into `to` (which must not exist yet) and checks the copy: every regular file of
// `from` is in `to` with the same size, and `to` holds no other files. False (and `why`) on any error or
// difference; the caller removes the partial copy. (Fallback of migrateOldPrefFolder.)
inline bool copyTreeVerified(const std::filesystem::path& from, const std::filesystem::path& to, std::string& why,
                             std::uintmax_t& files, std::uintmax_t& bytes) {
    namespace fs = std::filesystem;
    std::error_code ec;
    files = bytes = 0;
    fs::copy(from, to, fs::copy_options::recursive, ec);
    if (ec) {
        why = "copy: " + ec.message();
        return false;
    }
    std::uintmax_t copied = 0;
    for (auto it = fs::recursive_directory_iterator(from, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const fs::path dst = to / fs::relative(it->path(), from, ec);
        const std::uintmax_t size = it->file_size(ec);
        if (ec || !fs::is_regular_file(dst) || fs::file_size(dst, ec) != size || ec) {
            why = "verify: " + dst.string() + (ec ? " (" + ec.message() + ")" : " missing or a different size");
            return false;
        }
        ++files;
        bytes += size;
    }
    if (ec) {
        why = "verify: " + ec.message();
        return false;
    }
    for (auto it = fs::recursive_directory_iterator(to, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec)) ++copied;
    if (ec || copied != files) {
        why = ec ? "verify: " + ec.message() : "verify: " + std::to_string(copied) + " files in the copy, " + std::to_string(files) + " expected";
        return false;
    }
    return true;
}
// Moves the pre-0.0.3 folder <base>/d2k-mac/Dune 2000/ to <base>/OpenD2K/Dune 2000/ (`now`, just
// created empty by SDL_GetPrefPath) once. Returns the folder to use: `now`, or the old one if the move
// failed (nothing is lost; the next start tries again). Android's pref path has no org/app parts.
// When the rename fails - Windows cannot rename a folder onto another drive (e.g. %APPDATA%\d2k-mac as a
// junction to D:, "Access is denied") - the folder is copied and the copy verified (file count + sizes);
// only then is the old folder deleted (if that fails, it is left and the new one used). A failed copy is
// removed again and the old folder stays in use, as before.
// False in headless / one-frame / recording runs (--simtest, screenshots, --render-previews...; App sets it before
// the first prefPath()): they never move the player's folder - with a pre-0.0.3 folder present they just read it
// in place. (A --simtest on the Legion once really migrated the owner's folder.)
inline bool& migrationAllowed() {
    static bool allowed = true;
    return allowed;
}
inline std::string migrateOldPrefFolder(const std::string& now) {
#if defined(SDL_PLATFORM_ANDROID)
    return now;
#else
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path cur = fs::path(now);
    if (cur.filename().empty()) cur = cur.parent_path();          // ".../OpenD2K/Dune 2000"
    const fs::path base = cur.parent_path().parent_path();
    const fs::path old = base / kOldPrefOrg / cur.filename();      // ".../d2k-mac/Dune 2000"
    if (base.empty() || !fs::is_directory(old, ec)) return now;
    if (!migrationAllowed()) {
        SDL_Log("User data: %s not moved (test / one-frame run); reading it in place", old.string().c_str());
        return withSlash(old.string());
    }
    if (!fs::is_empty(cur, ec) || ec) {
        SDL_Log("User data: both %s and %s exist; using the new one", old.string().c_str(), cur.string().c_str());
        return now;
    }
    fs::remove(cur, ec);
    fs::rename(old, cur, ec);
    if (ec) {
        const std::string moveErr = ec.message();
        std::string why;
        std::uintmax_t files = 0, bytes = 0;
        if (copyTreeVerified(old, cur, why, files, bytes)) {
            fs::remove_all(old, ec);
            if (ec)
                SDL_Log("User data: copied %s to %s (%ju files, %ju bytes; the move failed: %s); could not delete the "
                        "old folder (%s) - left as it is", old.string().c_str(), cur.string().c_str(), files, bytes,
                        moveErr.c_str(), ec.message().c_str());
            else
                SDL_Log("User data: copied %s to %s (%ju files, %ju bytes; the move failed: %s) and deleted the old folder",
                        old.string().c_str(), cur.string().c_str(), files, bytes, moveErr.c_str());
            fs::remove(base / kOldPrefOrg, ec);  // only if now empty (a junction: just the link)
            return now;
        }
        fs::remove_all(cur, ec);  // the partial copy; the old folder is untouched
        SDL_Log("User data: could not move %s to %s (%s), nor copy it (%s); using the old folder", old.string().c_str(),
                cur.string().c_str(), moveErr.c_str(), why.c_str());
        fs::create_directories(cur, ec);
        return withSlash(old.string());
    }
    SDL_Log("User data: moved %s to %s", old.string().c_str(), cur.string().c_str());
    fs::remove(base / kOldPrefOrg, ec);  // only if now empty
    return now;
#endif
}
}  // namespace user_data_detail
using user_data_detail::migrationAllowed;

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
    static const std::string s = [] {
        std::string d;
        if (char* p = SDL_GetPrefPath(kPrefOrg, kPrefApp)) {
            d = p;
            SDL_free(p);
        }
        return d.empty() ? d : user_data_detail::migrateOldPrefFolder(d);
    }();
    return s;
}

// A file:// URL for a local folder (for SDL_OpenURL): "/Users/<you>/Library/Application Support/x/" ->
// "file:///Users/<you>/Library/Application%20Support/x/"; Windows "C:\Users\<you>\AppData\..." ->
// "file:///C:/Users/<you>/AppData/...". Reserved/unsafe ASCII is percent-encoded; bytes >= 0x80 (UTF-8)
// are percent-encoded too, except with `windows` where ShellExecuteW takes them as-is (an IRI) and
// would decode %XX in the ANSI code page.
inline std::string fileUrl(const std::string& path, bool windows) {
    static const char* hex = "0123456789ABCDEF";
    std::string p = path;
    if (windows)
        for (char& c : p)
            if (c == '\\') c = '/';
    std::string out = "file://";
    if (!p.empty() && p[0] != '/') out += '/';  // "C:/..." -> "file:///C:/..."
    for (const unsigned char c : p) {
        const bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                          c == '/' || c == '-' || c == '.' || c == '_' || c == '~' || c == ':' || (windows && c >= 0x80);
        if (keep) out += char(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
inline std::string fileUrl(const std::string& path) {
#if defined(SDL_PLATFORM_WINDOWS)
    return fileUrl(path, true);
#else
    return fileUrl(path, false);
#endif
}

// Shows the per-user data folder (prefPath: settings.ini, saves, GameData...; portable builds: the
// "OpenD2K Data" folder) in Finder / Explorer / the file manager. Desktop only (the menus hide the
// button on iOS / Android). Returns false when there is no folder or the system refused.
inline bool openUserDataFolder() {
    const std::string d = prefPath();
    if (d.empty()) return false;
    const std::string url = fileUrl(d);
    SDL_Log("Opening the data folder: %s", url.c_str());
    if (SDL_OpenURL(url.c_str())) return true;
    SDL_Log("Could not open %s: %s", url.c_str(), SDL_GetError());
    return false;
}

// prefPath() for display: without the trailing separator.
inline std::string userDataFolderForDisplay() {
    std::string d = prefPath();
    while (d.size() > 1 && (d.back() == '/' || d.back() == '\\')) d.pop_back();
    return d;
}

}  // namespace d2k::platform
