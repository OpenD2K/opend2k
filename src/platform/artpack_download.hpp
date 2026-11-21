#pragma once
// In-app download of the OpenD2K art pack (redesigned sprites + 3D models; docs/release.md "Art
// pack"). The pack is a GitHub release asset of OpenD2K/opend2k with artpack-manifest.json next to
// it (formats/artpack_manifest.hpp). Flow, all on one worker thread:
//   check()     GET the manifest (only when the player opens Options / the art pack page, or
//               presses a button - never at startup, and not at all with update checks off)
//   download()  manifest (if not fetched yet) -> GET the zip into <userDir>/.download/*.part
//               (resumed with a Range request when a part of the same file is there) -> size +
//               SHA-256 check -> unpack into a temp folder (path-traversal guarded,
//               formats/zip_reader.hpp) -> rename it to <userDir>/<name> (the old copy is
//               replaced only after the new one is complete) -> ArtPack::invalidate().
// Nothing is downloaded unless the player pressed Download. Requests carry only a generic
// User-Agent (platform/http_client.hpp).
// Several packs (0.0.3): the manifest's "extras" (the 3D Reimagined models, photo terrain, ground
// clutter) are downloaded the same way, one after the other from a queue (download(names)); each is
// optional and shows its size. Extras install into <userDir>/<name> too (no pack.ini needed) and take
// effect at the next battle (the 3D renderer looks the packs up when it starts).
// An extra with "install": "ai-voices" (the Russian AI voices, ru-ai-voices) installs into the AI voice
// folder instead (platform/language.hpp defaultAiVoicesDir: <pref path>/Voices/ru-ai); a folder there
// that the download did not write (a set the player generated: no installed.ini) is never replaced.
// After such an install the language sources are rebuilt (aiVoicesGeneration() changes languageKey).

#include "formats/artpack_manifest.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace d2k::platform {

// The stable manifest URL (GitHub serves the newest release's asset under "latest").
inline constexpr const char* kArtPackManifestUrl =
    "https://github.com/OpenD2K/opend2k/releases/latest/download/artpack-manifest.json";
// For a manual download (shown when the download cannot be done in the app).
inline constexpr const char* kArtPackReleasePage = "https://github.com/OpenD2K/opend2k/releases/latest";
// The pack the UI offers before a manifest is known.
inline constexpr const char* kArtPackName = "opend2k-redesign";
// A download(names) entry that stands for missingFor3d() of the manifest (resolved once it is fetched).
inline constexpr const char* kArtPack3dSet = "@3d";

struct InstalledArtPack {
    bool installed = false;
    std::string name, version, root;  // version "" = unknown (a pack copied by hand)
    std::string sha256;               // of the zip it came from (installed.ini), "" = unknown
    bool hasModels = false;
};

class ArtPackDownloader {
public:
    enum class State { Idle, Checking, Checked, CheckFailed, Downloading, Verifying, Installing, Done, Failed, Cancelled };
    struct Status {
        State state = State::Idle;
        bool haveManifest = false;
        formats::ArtPackManifest manifest;
        std::uint64_t done = 0, total = 0;  // bytes (Downloading / Verifying / Installing)
        double bytesPerSec = 0;
        std::string error;
        bool manualOnly = false;  // no HTTPS backend here: only the manual link works
        std::uint64_t startedMs = 0;  // SDL_GetTicks() when the last manifest check started (0 = none this run)
        std::int64_t checkedAt = 0;   // time(nullptr) when the last manifest check ended (answer or error; 0 = never)
        // The queued download in progress: the pack being fetched (name + title; "" = none) and its
        // place in the queue (1-based) of queueLen packs.
        std::string current, currentTitle;
        int queuePos = 0, queueLen = 0;
        std::vector<std::string> installedNames;  // what the last finished download installed (Done)
    };

    // One per process (the Options page, the art pack page and the 3D prompt share it).
    static ArtPackDownloader& instance();
    ~ArtPackDownloader();

    // URL priority: setManifestUrlOverride (--artpack-manifest) > D2K_ARTPACK_MANIFEST_URL >
    // Settings::artPackManifestUrl (setSettingsUrl) > kArtPackManifestUrl.
    void setManifestUrlOverride(const std::string& url) { override_ = url; }
    void setSettingsUrl(const std::string& url) { settingsUrl_ = url; }
    std::string manifestUrl() const;

    // Automatic checks (Options page opened) allowed in this run (false in screenshot runs).
    void setAutoCheckAllowed(bool b) { autoCheck_ = b; }
    // The Options page opened: check() once per run when `enabled` (Settings::artPackUpdateCheck).
    void autoCheck(bool enabled);

    void check();
    void download();  // the main pack (the manifest's own entry)
    // Packs by name (the main pack and / or extras), in the manifest's order; added to the queue
    // when a download is already running.
    void download(const std::vector<std::string>& names);
    void cancel();
    bool busy() const;
    Status status() const;
    // The pack just installed (once per install): the UI selects it in the settings.
    bool takeInstalled(std::string& name);

    // The installed copy of `name` (any search dir; version from its installed.ini / pack.ini). The
    // main pack needs pack.ini; another pack (an extra) counts when its folder has installed.ini or
    // any content (a copy by hand / a development checkout: version unknown).
    static InstalledArtPack installed(const std::string& name = kArtPackName);
    // The installed copy of a manifest item (its install target: the AI voice folder for "ai-voices").
    static InstalledArtPack installed(const formats::ArtPackManifest& item);
    // Where an item is installed (<art pack folder>/<name>, or the AI voice folder); "" = no writable folder.
    static std::string installDir(const formats::ArtPackManifest& item);
    // An item of the manifest should be offered: not installed, or an update (isUpdate).
    static bool needed(const formats::ArtPackManifest& item);
    // What the 3D Reimagined look still lacks: the main pack (models) when not installed, and every
    // "for3d" extra that is missing or has an update. Empty when everything is there.
    static std::vector<std::string> missingFor3d(const formats::ArtPackManifest& m);
    // Total download size of these items of `m`.
    static std::uint64_t sizeOf(const formats::ArtPackManifest& m, const std::vector<std::string>& names);
    // The manifest offers an update for the installed copy: a newer version, or the same version
    // republished (installed.ini's sha256 differs from the manifest's). False when nothing is
    // installed, the installed version is unknown (hand-copied) or the manifest is older.
    bool updateAvailable() const;
    static bool isUpdate(const formats::ArtPackManifest& m, const InstalledArtPack& i);

    // Blocking variants for the command line (--artpack-check / --artpack-install): log lines
    // through `log`; returns 0 on success.
    int runBlocking(bool install, const std::function<void(const std::string&)>& log);

    // Screenshots (--menu-page artpack-*): show a made-up state without network.
    void previewState(State s, const formats::ArtPackManifest& m, std::uint64_t done, const std::string& error);

private:
    ArtPackDownloader() = default;
    void start(bool download, const std::vector<std::string>& names = {});
    void work(const std::vector<std::string>& names);  // empty = only check
    bool installItem(const formats::ArtPackManifest& m);
    bool fetchManifest();
    bool downloadZip(const formats::ArtPackManifest& m, const std::string& part);
    bool verify(const formats::ArtPackManifest& m, const std::string& part);
    bool install(const formats::ArtPackManifest& m, const std::string& part);
    void setState(State s);
    void fail(State s, const std::string& error);

    mutable std::mutex m_;
    Status st_;
    std::string rawManifest_;
    std::thread worker_;
    std::atomic<bool> cancel_{false}, running_{false};
    std::vector<std::string> queue_;  // packs to download next (guarded by m_; "" = the main pack)
    std::string override_, settingsUrl_;
    bool autoCheck_ = true, autoChecked_ = false;
    std::string installedName_;
    std::uint64_t speedBase_ = 0;
    double speedT0_ = 0;
};

// --http-get URL FILE: one GET through the platform backend into FILE (prints status, size,
// sha256); 0 on success.
int httpGetToFile(const std::string& url, const std::string& file, const std::function<void(const std::string&)>& log);

// "612 MB" / "1.2 GB" / "850 KB".
std::string formatBytes(std::uint64_t bytes);

}  // namespace d2k::platform
