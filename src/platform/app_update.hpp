#pragma once
// App update check (docs/release.md "App update check"): is a newer OpenD2K release out? Reads the
// latest release from GitHub's REST API (formats/release_info.hpp) on a worker thread - only when
// the player opens Options / the Updates page (never at startup; Settings::artPackUpdateCheck turns
// the automatic check off) or presses "Check now". No self-update: "Download" opens this
// platform's release asset (or the release page) in the browser. Requests carry only the generic
// User-Agent (platform/http_client.hpp). Game data, settings, saves and art packs live in the
// per-user folder (platform/user_data.hpp), so a new app needs none of them again.

#include "formats/release_info.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace d2k::platform {

// The newest non-draft, non-prerelease release (GitHub API; 60 requests/hour per IP without a
// token - plenty for an on-demand check).
inline constexpr const char* kAppReleaseInfoUrl = "https://api.github.com/repos/OpenD2K/opend2k/releases/latest";
inline constexpr const char* kAppReleasesPage = "https://github.com/OpenD2K/opend2k/releases/latest";

// The compiled version (D2K_VERSION = CMake PROJECT_VERSION).
const char* appVersion();

class AppUpdateChecker {
public:
    enum class State { Idle, Checking, Checked, Failed };
    struct Status {
        State state = State::Idle;
        bool haveRelease = false;
        formats::ReleaseInfo release;
        int asset = -1;              // index into release.assets for this platform, -1 = none
        bool newer = false;          // release.version > appVersion()
        std::string error;
    };

    static AppUpdateChecker& instance();
    ~AppUpdateChecker();

    // URL priority: setUrlOverride (--update-url) > D2K_UPDATE_URL > Settings::appUpdateUrl
    // (setSettingsUrl) > kAppReleaseInfoUrl.
    void setUrlOverride(const std::string& url) { override_ = url; }
    void setSettingsUrl(const std::string& url) { settingsUrl_ = url; }
    std::string url() const;

    // Automatic checks allowed in this run (false in screenshot runs unless --update-url is given).
    void setAutoCheckAllowed(bool b) { autoCheck_ = b; }
    // The Options page opened: check() once per run when `enabled`.
    void autoCheck(bool enabled);
    void check();
    bool busy() const { return running_.load(); }
    Status status() const;

    // What "Download" opens: this platform's asset, else the release page, else kAppReleasesPage.
    static std::string downloadUrl(const Status& s);
    static std::string releasePage(const Status& s);

    // --update-check: one blocking check, log lines through `log`; 0 = checked (newer or not).
    int runBlocking(const std::function<void(const std::string&)>& log);

    // Screenshots (--menu-page updates-*): a made-up state without network.
    void previewState(State st, const formats::ReleaseInfo& r, const std::string& error);

private:
    AppUpdateChecker() = default;
    void work();
    Status evaluate(const formats::ReleaseInfo& r) const;

    mutable std::mutex m_;
    Status st_;
    std::thread worker_;
    std::atomic<bool> cancel_{false}, running_{false};
    std::string override_, settingsUrl_;
    bool autoCheck_ = true, autoChecked_ = false;
};

}  // namespace d2k::platform
