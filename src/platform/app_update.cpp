#include "platform/app_update.hpp"

#include "platform/http_client.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>

#ifndef D2K_VERSION
#define D2K_VERSION "0.0.0"
#endif

namespace d2k::platform {

const char* appVersion() { return D2K_VERSION; }

AppUpdateChecker& AppUpdateChecker::instance() {
    static AppUpdateChecker c;
    return c;
}

AppUpdateChecker::~AppUpdateChecker() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
}

std::string AppUpdateChecker::url() const {
    if (!override_.empty()) return override_;
    if (const char* e = std::getenv("D2K_UPDATE_URL"); e && *e) return e;
    if (!settingsUrl_.empty()) return settingsUrl_;
    return kAppReleaseInfoUrl;
}

void AppUpdateChecker::autoCheck(bool enabled) {
    if (!enabled || !autoCheck_ || autoChecked_ || busy()) return;
    autoChecked_ = true;
    {
        std::lock_guard l(m_);
        if (st_.haveRelease) return;
    }
    check();
}

void AppUpdateChecker::check() {
    if (running_.exchange(true)) return;
    if (worker_.joinable()) worker_.join();
    cancel_ = false;
    {
        std::lock_guard l(m_);
        st_.state = State::Checking;
        st_.error.clear();
    }
    worker_ = std::thread([this] {
        work();
        running_ = false;
    });
}

AppUpdateChecker::Status AppUpdateChecker::status() const {
    std::lock_guard l(m_);
    return st_;
}

AppUpdateChecker::Status AppUpdateChecker::evaluate(const formats::ReleaseInfo& r) const {
    Status s;
    s.state = State::Checked;
    s.haveRelease = true;
    s.release = r;
    s.asset = formats::selectReleaseAsset(r, formats::currentPlatform());
    s.newer = !r.draft && !r.prerelease && formats::compareSemver(r.version, appVersion()) > 0;
    return s;
}

void AppUpdateChecker::work() {
    auto fail = [&](const std::string& e) {
        std::lock_guard l(m_);
        st_.state = State::Failed;
        st_.error = e;
        SDL_Log("Update check: %s", e.c_str());
    };
    std::string why;
    if (!http::available(&why)) return fail(why);
    const std::string u = url();
    std::string body;
    const http::Response r = http::getText(u, body, 2u << 20, cancel_);
    if (cancel_.load()) return fail("cancelled");
    if (!r.ok) {
        if (r.status == 404) return fail("no release published yet (HTTP 404)");
        if (r.status == 403 || r.status == 429) return fail("GitHub's request limit reached - try again later (HTTP " + std::to_string(r.status) + ")");
        return fail(r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error);
    }
    formats::ReleaseInfo info;
    std::string err;
    if (!formats::parseReleaseInfo(body, u, info, err)) return fail("bad release info: " + err);
    const Status s = evaluate(info);
    {
        std::lock_guard l(m_);
        st_ = s;
    }
    SDL_Log("Update check: latest %s (this build %s)%s; download %s", info.version.c_str(), appVersion(),
            s.newer ? " - newer" : "", downloadUrl(s).c_str());
}

std::string AppUpdateChecker::releasePage(const Status& s) {
    return s.haveRelease && !s.release.page.empty() ? s.release.page : std::string(kAppReleasesPage);
}

std::string AppUpdateChecker::downloadUrl(const Status& s) {
    if (s.haveRelease && s.asset >= 0 && s.asset < int(s.release.assets.size())) return s.release.assets[std::size_t(s.asset)].url;
    return releasePage(s);
}

void AppUpdateChecker::previewState(State st, const formats::ReleaseInfo& r, const std::string& error) {
    std::lock_guard l(m_);
    st_ = r.tag.empty() ? Status{} : evaluate(r);
    st_.state = st;
    st_.error = error;
    autoChecked_ = true;
}

int AppUpdateChecker::runBlocking(const std::function<void(const std::string&)>& log) {
    log("Release info: " + url() + " (HTTP backend " + http::backendName() + "), this build " + appVersion() + " (" +
        formats::currentPlatform().os + " " + formats::currentPlatform().arch + ")");
    check();
    if (worker_.joinable()) worker_.join();
    const Status s = status();
    if (s.state != State::Checked) {
        log("Check failed: " + s.error);
        return 1;
    }
    log("Latest: " + s.release.tag + (s.release.name.empty() ? "" : " \"" + s.release.name + "\"") + ", " +
        std::to_string(s.release.assets.size()) + " assets; " + (s.newer ? "NEWER than this build" : "this build is up to date"));
    for (std::size_t i = 0; i < s.release.assets.size(); ++i)
        log(std::string(int(i) == s.asset ? "  * " : "    ") + s.release.assets[i].name);
    log("Download: " + downloadUrl(s));
    return 0;
}

}  // namespace d2k::platform
