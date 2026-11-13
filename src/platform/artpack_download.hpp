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

#include "formats/artpack_manifest.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace d2k::platform {

// The stable manifest URL (GitHub serves the newest release's asset under "latest").
inline constexpr const char* kArtPackManifestUrl =
    "https://github.com/OpenD2K/opend2k/releases/latest/download/artpack-manifest.json";
// For a manual download (shown when the download cannot be done in the app).
inline constexpr const char* kArtPackReleasePage = "https://github.com/OpenD2K/opend2k/releases/latest";
// The pack the UI offers before a manifest is known.
inline constexpr const char* kArtPackName = "opend2k-redesign";

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
    void download();
    void cancel();
    bool busy() const;
    Status status() const;
    // The pack just installed (once per install): the UI selects it in the settings.
    bool takeInstalled(std::string& name);

    // The installed copy of `name` (any search dir; version from its installed.ini / pack.ini).
    static InstalledArtPack installed(const std::string& name = kArtPackName);
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
    void start(bool download);
    void work(bool download);
    bool fetchManifest();
    bool downloadZip(const std::string& part);
    bool verify(const std::string& part);
    bool install(const std::string& part);
    void setState(State s);
    void fail(State s, const std::string& error);

    mutable std::mutex m_;
    Status st_;
    std::string rawManifest_;
    std::thread worker_;
    std::atomic<bool> cancel_{false}, running_{false}, pendingDownload_{false};
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
