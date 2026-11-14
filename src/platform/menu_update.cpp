// Options -> Updates (not original): is a newer OpenD2K out (platform/app_update.*), plus the art
// pack's state (platform/artpack_download.*, its own page does the download). The app is never
// replaced by the game itself: "Download" opens this platform's release file in the browser. The
// check runs only when Options opens (Settings::artPackUpdateCheck, shared with the art pack) or
// when "Check now" is pressed.

#include "platform/app_update.hpp"
#include "platform/artpack_download.hpp"
#include "platform/community_menu.hpp"
#include "platform/menu.hpp"
#include "platform/settings.hpp"
#include "platform/ui.hpp"
#include "platform/user_data.hpp"
#include "render/ui_text.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <ctime>

namespace d2k::platform {

using render::tr;
using UState = AppUpdateChecker::State;

namespace {

const Colour kGood{150, 220, 120, 255};

const char* platformLabel(const std::string& os) {
    if (os == "macos") return "macOS";
    if (os == "windows") return "Windows";
    if (os == "linux") return "Linux";
    if (os == "android") return "Android";
    if (os == "ios") return "iOS / iPadOS";
    return "?";
}

// The state the page shows: Checking while `held` (Menu::checkingShown: at least kCheckShowMs after a press).
UState shownState(const AppUpdateChecker::Status& s, bool held) {
    if ((s.state == UState::Checked || s.state == UState::Failed) && held) return UState::Checking;
    return s.state;
}

}  // namespace

bool Menu::checkingShown(bool checking, std::uint64_t startedMs) {
    return checking || (startedMs != 0 && SDL_GetTicks() - startedMs < kCheckShowMs);
}

std::string Menu::checkedAtText(std::int64_t checkedAt) {
    if (checkedAt <= 0) return {};
    const std::time_t t = std::time_t(checkedAt);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    char hm[8];
    std::snprintf(hm, sizeof hm, "%02d:%02d", lt.tm_hour, lt.tm_min);
    return render::trf("Checked at %s", hm);
}

void Menu::checkingSpinner(float x, float y, float size) {
    // Eight dots round a circle, the bright one running clockwise (one turn in 0.8 s).
    const SDL_FRect box = st_.r(x, y, size, size);
    const float cx = box.x + box.w * 0.5f, cy = box.y + box.h * 0.5f, rad = box.w * 0.38f, dot = std::max(2.0f, box.w * 0.2f);
    const int head = int((SDL_GetTicks() / 100) % 8);
    for (int i = 0; i < 8; ++i) {
        const float a = float(i) * 0.785398f;
        const int age = (head - i + 8) % 8;
        Colour c = theme::kTitle;
        c.a = std::uint8_t(255 - age * 28);
        ui_.fill(SDL_FRect{cx + std::sin(a) * rad - dot * 0.5f, cy - std::cos(a) * rad - dot * 0.5f, dot, dot}, c);
    }
}

std::string Menu::updatesRowLabel() const {
    auto s = AppUpdateChecker::instance().status();
    s.state = shownState(s, checkingShown(false, s.startedMs));
    std::string v;
    if (s.state == UState::Checking) v = tr("checking...");
    else if (s.haveRelease && s.newer) v = render::trf("OpenD2K %s available", ("v" + s.release.version).c_str());
    else if (ArtPackDownloader::instance().updateAvailable()) v = tr("art pack update available");
    else if (s.state == UState::Failed) v = tr("could not check");
    else if (s.haveRelease) v = tr("up to date");
    else v = std::string("v") + appVersion();
    return std::string(tr("Updates")) + ": " + v;
}

bool Menu::openUpdatesPreview(const std::string& name) {
    auto& c = AppUpdateChecker::instance();
    if (name == "updates-live") {  // the real check (--update-url URL --frames N)
        open(MenuPage::Updates);
        return true;
    }
    formats::ReleaseInfo r;
    r.tag = "v0.0.2";
    r.version = "0.0.2";
    r.name = "OpenD2K 0.0.2";
    r.page = "https://github.com/OpenD2K/opend2k/releases/tag/v0.0.2";
    for (const char* n : {"OpenD2K-0.0.2-macos-universal.zip", "OpenD2K-0.0.2-windows-x64.zip",
                          "OpenD2K-0.0.2-linux-x86_64.tar.gz", "OpenD2K-0.0.2-android.apk"})
        r.assets.push_back({n, std::string("https://github.com/OpenD2K/opend2k/releases/download/v0.0.2/") + n, 48'300'000});
    formats::ReleaseInfo same = r;
    same.tag = std::string("v") + appVersion();
    same.version = appVersion();
    if (name == "updates" || name == "updates-uptodate") c.previewState(UState::Checked, same, "");
    else if (name == "updates-available") c.previewState(UState::Checked, r, "");
    else if (name == "updates-checking") c.previewState(UState::Checking, {}, "");
    else if (name == "updates-failed") c.previewState(UState::Failed, {}, "GitHub's request limit reached - try again later (HTTP 403)");
    else if (name == "updates-disabled") {
        settings_.artPackUpdateCheck = false;
        c.previewState(UState::Idle, {}, "");
    } else {
        return false;
    }
    // The art pack line under it: checking with the app, else checked (its "Checked at" line).
    ArtPackDownloader::instance().previewState(
        name == "updates-checking" ? ArtPackDownloader::State::Checking
        : name == "updates-disabled" ? ArtPackDownloader::State::Idle
                                     : ArtPackDownloader::State::Checked,
        {}, 0, "");
    open(MenuPage::Updates);
    return true;
}

MenuResult Menu::pageUpdates() {
    MenuResult res;
    backdrop(back_, 0.25f);
    title(tr("Updates"), 110, 30);
    ui_.panel(st_.r(100, 56, 440, 318));
    auto& checker = AppUpdateChecker::instance();
    const auto real = checker.status();
    const UState shown = shownState(real, checkingShown(false, real.startedMs));
    // While "Checking..." is held after a quick answer, the page shows nothing of that answer yet.
    AppUpdateChecker::Status s = shown == real.state ? real : AppUpdateChecker::Status{};
    s.state = shown;
    const float x = 116, w = 408;
    const float ts = ui_.textScale, small = ui_.textScale * 0.85f;
    const float lineH = ui_.textHeight(ts) / st_.s + 4;
    const float bh = 24;
    float y = 66;
    auto textAt = [&](const std::string& t, float yy, Colour c, float scale, float xx = 116) {
        const SDL_FRect r = st_.r(xx, yy, 0, 0);
        ui_.text(t, r.x, r.y, c, scale);
    };
    {
        Ui::BoldScope bold(ui_);
        textAt("OpenD2K", y, theme::kTitle, ts + 0.5f);
        y += ui_.textHeight(ts + 0.5f) / st_.s + 6;
    }
    const formats::PlatformId plat = formats::currentPlatform();
    const float vx = x + 110;
    textAt(tr("This version"), y, theme::kDim, ts);
    textAt(std::string("v") + appVersion() + "  (" + platformLabel(plat.os) + (plat.arch.empty() ? "" : " " + plat.arch) + ")", y,
           theme::kText, ts, vx);
    y += lineH;
    textAt(tr("Latest"), y, theme::kDim, ts);
    std::string latest;
    if (s.haveRelease) latest = "v" + s.release.version + (s.release.publishedAt.size() >= 10 ? ", " + s.release.publishedAt.substr(0, 10) : "");
    else if (s.state == UState::Checking) latest = tr("checking...");
    else if (s.state == UState::Failed) latest = tr("could not check");
    else latest = tr("not checked");
    textAt(latest, y, s.haveRelease && s.newer ? kGood : theme::kText, ts, vx);
    y += lineH + 6;

    const bool busy = checker.busy() || s.state == UState::Checking;
    const std::string checkedAt = checkedAtText(real.checkedAt);
    auto checkedLine = [&] {  // under the result
        if (checkedAt.empty()) return;
        textAt(checkedAt, y, theme::kDim, small * 0.95f);
        y += ui_.textHeight(small * 0.95f) / st_.s + 4;
    };
    if (s.state == UState::Checking) {
        checkingSpinner(x, y, 14);
        textAt(tr("Checking for the latest version..."), y, theme::kDim, small, x + 20);
        y += lineH + 4;
        ui_.button(st_.r(x, y, 110, bh), tr("Check now"), ButtonStyle::Normal, false);  // greyed: the layout stays
        y += bh + 10;
    } else {
        float bx = x;
        if (s.state == UState::Failed) {
            y += ui_.wrapped(std::string(tr("Could not check for the latest version")) + ": " + uiUtf8(s.error), st_.r(x, y, w, 36),
                             theme::kBad, small) /
                     st_.s +
                 2;
            checkedLine();
        } else if (s.haveRelease && s.newer) {
            {
                Ui::BoldScope bold(ui_);
                textAt(render::trf("OpenD2K %s is available.", ("v" + s.release.version).c_str()), y, kGood, ts);
            }
            y += lineH;
            y += ui_.wrapped(tr(plat.os == "ios"
                                    ? "Install it the same way as this app (see the release page)."
                                    : "Download it and replace this app. Game data, settings, saves and art packs stay in the app "
                                      "data folder: nothing has to be downloaded or imported again."),
                             st_.r(x, y, w, 40), theme::kText, small) /
                     st_.s +
                 2;
            checkedLine();
            std::string label = tr("Download");
            if (s.asset >= 0) label += " (" + formatBytes(s.release.assets[std::size_t(s.asset)].size) + ")";
            if (ui_.button(st_.r(bx, y, 150, bh), label, ButtonStyle::Primary)) SDL_OpenURL(AppUpdateChecker::downloadUrl(s).c_str());
            bx += 156;
            if (ui_.button(st_.r(bx, y, 130, bh), tr("Release notes"))) SDL_OpenURL(AppUpdateChecker::releasePage(s).c_str());
            bx += 136;
        } else if (s.haveRelease) {
            textAt(tr("Up to date."), y, kGood, small);
            y += lineH;
            checkedLine();
        } else if (!settings_.artPackUpdateCheck) {
            y += ui_.wrapped(tr("Automatic checks are off. Check now looks once (github.com)."), st_.r(x, y, w, 30), theme::kDim, small) /
                     st_.s +
                 6;
        }
        if (ui_.button(st_.r(bx, y, 110, bh), tr(s.state == UState::Failed ? "Retry" : "Check now"), ButtonStyle::Normal, !busy)) {
            checker.check();
            ArtPackDownloader::instance().check();  // the art pack too, in the same place
        }
        bx += 116;
        if (s.state == UState::Failed)
            if (ui_.button(st_.r(bx, y, std::min(180.f, x + w - bx), bh), tr("Open download page")))
                SDL_OpenURL(AppUpdateChecker::releasePage(s).c_str());
        y += bh + 10;
    }

    // The art pack: its state in one line; its own page downloads it.
    {
        const SDL_FRect sep = st_.r(x, y, w, 1);
        ui_.fill(sep, theme::kEdge);
        y += 8;
        const bool apUpdate = ArtPackDownloader::instance().updateAvailable();
        const auto ap = ArtPackDownloader::instance().status();
        const bool apChecking = artPackCheckingShown();
        float lx = x;
        if (apChecking) {
            checkingSpinner(x, y + 4, 14);
            lx += 20;
        }
        textAt(apChecking ? std::string(tr("Art pack")) + ": " + tr("checking...") : artPackRowLabel(), y + 4,
               apUpdate && !apChecking ? kGood : theme::kText, ts, lx);
        if (ui_.button(st_.r(x + w - 120, y, 120, 22), std::string(tr(apUpdate ? "Update" : "Show")) + "  >")) {
            artPackBack_ = "updates";
            open(MenuPage::ArtPack);
        }
        y += 26;
        if (const std::string at = checkedAtText(ap.checkedAt); !apChecking && !at.empty()) {
            textAt(at, y, theme::kDim, small * 0.95f);
            y += ui_.textHeight(small * 0.95f) / st_.s + 2;
        }
        y += 4;
    }
    ui_.toggle(st_.r(x, y, w, 20), tr("Check for updates when Options opens"), settings_.artPackUpdateCheck);
    y += 26;
    ui_.wrapped(std::string(tr("App data folder")) + ": " + uiUtf8(prefPath()), st_.r(x, y, w, 30), theme::kDim, small * 0.95f);
    if (ui_.button(st_.r(100, 377, 110, 22), tr("Back")) || ui_.takeBack()) open(MenuPage::Options);
    return res;
}

}  // namespace d2k::platform
