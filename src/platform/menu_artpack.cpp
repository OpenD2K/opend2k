// Options -> Art pack (not original): the downloadable OpenD2K art pack - installed version,
// update check, Download with progress / Cancel, errors with Retry and the manual download link,
// and the small "download the 3D art pack?" prompt on the Graphics page's 3D tab. The work is
// done by platform/artpack_download.* on its worker thread; nothing is downloaded before the
// player presses a Download button.

#include "platform/artpack_download.hpp"
#include "platform/community_menu.hpp"
#include "platform/data_dir.hpp"
#include "platform/game_options.hpp"
#include "platform/menu.hpp"
#include "platform/movies.hpp"
#include "platform/platform_info.hpp"
#include "platform/preset_previews.hpp"
#include "platform/settings.hpp"
#include "platform/ui.hpp"
#include "render/art_pack.hpp"
#include "render/ui_text.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace d2k::platform {

using render::tr;
using State = ArtPackDownloader::State;

namespace {

// Screenshot runs (--menu-page artpack-*): the installed state to show (-1 = the real one).
int gPreviewInstalled = -1;

// installed() touches the file system: cached per pack name, refreshed after an install and
// every two seconds (a pack copied in by hand shows up too).
InstalledArtPack installedCached(const std::string& name) {
    static InstalledArtPack cached;
    static int gen = -1;
    static Uint64 at = 0;
    const Uint64 now = SDL_GetTicks();
    if (gPreviewInstalled >= 0) {
        InstalledArtPack p;
        p.name = name;
        p.installed = gPreviewInstalled > 0;
        p.version = p.installed ? "0.0.1" : "";
        p.hasModels = p.installed;
        return p;
    }
    if (cached.name != name || gen != render::ArtPack::generation() || now - at > 2000) {
        cached = ArtPackDownloader::installed(name);
        gen = render::ArtPack::generation();
        at = now;
    }
    return cached;
}

bool updateAvailable(const ArtPackDownloader::Status& s, const InstalledArtPack& inst) {
    return s.haveManifest && ArtPackDownloader::isUpdate(s.manifest, inst);
}

std::string licenceLabel(const std::string& l) {
    if (l == "cc-by-sa-4.0") return "CC BY-SA 4.0";
    if (l == "gpl-3.0-or-later") return "GPL-3.0-or-later";
    return l;
}

std::string releasePage(const ArtPackDownloader::Status& s) {
    return s.haveManifest && !s.manifest.page.empty() ? s.manifest.page : std::string(kArtPackReleasePage);
}

}  // namespace

std::string Menu::artPackRowLabel() const {
    const auto s = ArtPackDownloader::instance().status();
    const InstalledArtPack inst = installedCached(s.haveManifest ? s.manifest.name : kArtPackName);
    std::string v;
    if (s.state == State::Downloading && s.total)
        v = render::trf("downloading %d%%", int(s.done * 100 / s.total));
    else if (s.state == State::Verifying || s.state == State::Installing)
        v = tr("installing...");
    else if (updateAvailable(s, inst))
        v = render::trf("update %s available", ("v" + s.manifest.version).c_str());
    else if (inst.installed)
        v = inst.version.empty() ? std::string(tr("installed")) : "v" + inst.version;
    else
        v = tr("not installed");
    return std::string(tr("Art pack")) + ": " + v;
}

void Menu::applyInstalledArtPack() {
    std::string name;
    if (!ArtPackDownloader::instance().takeInstalled(name)) return;
    // The new pack takes effect without a restart: the session reopens the art pack / model pack
    // when these settings change (or at the next battle).
    settings_.artPack = name;
    const InstalledArtPack inst = ArtPackDownloader::installed(name);
    if (inst.hasModels) {
        settings_.modelPack = name;
        settings_.models3d = Settings::Models3D::New;
    }
    SDL_Log("Art pack %s selected (art%s)", name.c_str(), inst.hasModels ? " + 3D models" : "");
    // 3D picked on the welcome page: now it can be used.
    if (welcomeWants3d_ && inst.hasModels && Settings::modelPackInstalled()) {
        settings_.applyPreset(Settings::GraphicsPreset::ThreeD);
        welcomeWants3d_ = false;
    }
    wantSave = true;
}

bool Menu::artPackInstalled() const {
    if (gPreviewInstalled >= 0) return gPreviewInstalled > 0;
    const auto s = ArtPackDownloader::instance().status();
    return installedCached(s.haveManifest ? s.manifest.name : kArtPackName).installed || redesignArtPackInstalled();
}

void Menu::openArtPackFrom(const char* back) {
    artPackBack_ = back;
    open(MenuPage::ArtPack);
}

bool Menu::artPackPromptWanted() const {
    if (artPackPromptLater_) return false;
    const auto s = ArtPackDownloader::instance().status();
    if (s.state == State::Done) return false;
    return !installedCached(s.haveManifest ? s.manifest.name : kArtPackName).installed;
}

float Menu::artPackPrompt(float x, float y, float w) {
    if (!artPackPromptWanted()) return 0;
    auto& dl = ArtPackDownloader::instance();
    const auto s = dl.status();
    const float h = kArtPackPromptH - 6;
    const SDL_FRect box = st_.r(x, y, w, h);
    ui_.fill(box, Colour{60, 40, 20, 200});
    ui_.outline(box, theme::kAccent);
    const float ts = ui_.textScale * 0.9f;
    std::string line = tr("OpenD2K 3D art pack");
    if (s.haveManifest) line += ", " + formatBytes(s.manifest.size);
    {
        Ui::BoldScope bold(ui_);
        const SDL_FRect t = st_.r(x + 6, y + 4, 0, 0);
        ui_.text(line, t.x, t.y, theme::kTitle, ts);
    }
    const bool busy = dl.busy() && s.state != State::Checking;
    const SDL_FRect t2 = st_.r(x + 6, y + 21, 0, 0);
    ui_.text(ui_.ellipsize(busy ? std::string(tr("Downloading...")) : std::string(tr("Redesigned 3D models and sprites")),
                           st_.r(0, 0, w - 190, 0).w, ts * 0.9f),
             t2.x, t2.y, theme::kDim, ts * 0.9f);
    const float bw = 84, bh = 22, by = y + (h - bh) / 2;
    if (ui_.button(st_.r(x + w - 2 * bw - 12, by, bw, bh), tr(busy ? "Show" : "Download"), ButtonStyle::Primary)) {
        if (!busy) dl.download();  // the player asked for it
        artPackBack_ = "graphics";
        open(MenuPage::ArtPack);
    }
    if (ui_.button(st_.r(x + w - bw - 6, by, bw, bh), tr("Later"))) artPackPromptLater_ = true;
    return kArtPackPromptH;
}

bool Menu::openArtPackPreview(const std::string& name) {
    formats::ArtPackManifest m;
    m.name = kArtPackName;
    m.title = "OpenD2K redesign pack";
    m.version = "0.0.1";
    m.size = 612'400'000;
    m.licence = "cc-by-sa-4.0";
    m.sha256 = std::string(64, '0');
    auto& dl = ArtPackDownloader::instance();
    if (name == "artpack-live" || name == "artpack-live-download") {
        // The real state (end-to-end tests with --artpack-manifest and --frames N); -download
        // starts the download as if the button had been pressed.
        artPackBack_.clear();
        open(MenuPage::ArtPack);
        if (name == "artpack-live-download") dl.download();
        return true;
    }
    gPreviewInstalled = 0;
    if (name == "artpack") dl.previewState(State::Checked, m, 0, "");
    else if (name == "artpack-checking") dl.previewState(State::Checking, {}, 0, "");
    else if (name == "artpack-update") {
        gPreviewInstalled = 1;
        m.version = "0.0.2";
        dl.previewState(State::Checked, m, 0, "");
    } else if (name == "artpack-downloading") dl.previewState(State::Downloading, m, 276'000'000, "");
    else if (name == "artpack-error") dl.previewState(State::Failed, m, 0, "checksum mismatch - the download was damaged, please retry");
    else if (name == "artpack-offline") dl.previewState(State::CheckFailed, {}, 0, "The Internet connection appears to be offline.");
    else if (name == "artpack-done") {
        gPreviewInstalled = 1;
        dl.previewState(State::Done, m, m.size, "");
    } else if (name == "artpack-prompt") {
        dl.previewState(State::Checked, m, 0, "");
        open(MenuPage::Graphics);
        graphicsTab_ = 2;
        return true;
    } else {
        return false;
    }
    artPackBack_.clear();
    open(MenuPage::ArtPack);
    return true;
}

MenuResult Menu::pageArtPack() {
    MenuResult res;
    backdrop(back_, 0.25f);
    title(tr("Art pack"), 110, 30);
    ui_.panel(st_.r(100, 56, 440, 318));
    auto& dl = ArtPackDownloader::instance();
    const auto s = dl.status();
    const InstalledArtPack inst = installedCached(s.haveManifest ? s.manifest.name : kArtPackName);
    const bool update = updateAvailable(s, inst);
    const float x = 116, w = 408;
    const float ts = ui_.textScale, small = ui_.textScale * 0.85f;
    const float lineH = ui_.textHeight(ts) / st_.s + 4;
    float y = 66;
    auto textAt = [&](const std::string& t, float yy, Colour c, float scale, float xx = 116) {
        const SDL_FRect r = st_.r(xx, yy, 0, 0);
        ui_.text(t, r.x, r.y, c, scale);
    };
    {
        Ui::BoldScope bold(ui_);
        textAt(s.haveManifest ? s.manifest.title : std::string(tr("OpenD2K redesign pack")), y, theme::kTitle, ts + 0.5f);
        y += ui_.textHeight(ts + 0.5f) / st_.s + 6;
    }
    y += ui_.wrapped(tr("Redesigned sprites for the Enhanced graphics and 3D models for the 3D mode, made for OpenD2K. "
                        "Optional: the game works without it, with the original art."),
                     st_.r(x, y, w, 40), theme::kText, small) /
             st_.s +
         8;
    // Installed / latest.
    const float vx = x + 110;
    textAt(tr("Installed"), y, theme::kDim, ts);
    textAt(inst.installed ? (inst.version.empty() ? std::string(tr("yes (version unknown)")) : "v" + inst.version)
                          : std::string(tr("not installed")),
           y, inst.installed ? theme::kText : theme::kDim, ts, vx);
    y += lineH;
    textAt(tr("Latest"), y, theme::kDim, ts);
    std::string latest;
    if (s.haveManifest) {
        latest = "v" + s.manifest.version + ", " + formatBytes(s.manifest.size);
        if (!s.manifest.licence.empty()) latest += ", " + licenceLabel(s.manifest.licence);
    } else if (s.state == State::Checking) {
        latest = tr("checking...");
    } else if (s.state == State::CheckFailed) {
        latest = tr("could not check");
    } else {
        latest = tr("not checked");
    }
    textAt(latest, y, update ? Colour{150, 220, 120, 255} : theme::kText, ts, vx);
    y += lineH + 8;

    // State: progress, messages, buttons.
    const float bh = 24;
    const bool busy = dl.busy();
    auto openPage = [&] { SDL_OpenURL(releasePage(s).c_str()); };
    if (s.manualOnly) {
        y += ui_.wrapped(render::trf("Downloading in the game is not available here (%s). Download the zip from the release page "
                                     "and unpack it into the folder below.",
                                     s.error.c_str()),
                         st_.r(x, y, w, 50), theme::kBad, small) /
                 st_.s +
             6;
        if (ui_.button(st_.r(x, y, 200, bh), tr("Open download page"), ButtonStyle::Primary)) openPage();
        y += bh + 8;
    } else if (s.state == State::Downloading || s.state == State::Verifying || s.state == State::Installing) {
        const float frac = s.total ? std::clamp(float(double(s.done) / double(s.total)), 0.f, 1.f) : 0.f;
        const SDL_FRect bar = st_.r(x, y, w, 14);
        ui_.fill(bar, Colour{12, 8, 5, 255});
        ui_.fill(SDL_FRect{bar.x, bar.y, bar.w * frac, bar.h}, theme::kAccent);
        ui_.outline(bar, theme::kEdge);
        y += 20;
        std::string msg;
        if (s.state == State::Downloading) {
            msg = render::trf("Downloading: %s of %s", formatBytes(s.done).c_str(), formatBytes(s.total).c_str());
            if (s.bytesPerSec > 1) {
                char sp[32];
                std::snprintf(sp, sizeof sp, "%.1f MB/s", s.bytesPerSec / 1e6);
                msg += "  (" + std::string(sp) + ")";
            }
        } else {
            msg = tr(s.state == State::Verifying ? "Checking the download..." : "Installing...");
        }
        textAt(msg, y, theme::kText, small);
        y += lineH + 4;
        if (ui_.button(st_.r(x, y, 130, bh), tr("Cancel"))) dl.cancel();
        y += bh + 8;
    } else if (s.state == State::Checking) {
        textAt(tr("Checking for the latest version..."), y, theme::kDim, small);
        y += lineH + bh + 12;
    } else {
        if (s.state == State::Done) {
            const bool models = ArtPackDownloader::installed(settings_.artPack).hasModels;
            y += ui_.wrapped(tr(models ? "Installed. The redesigned art and the 3D models are selected (Options > Graphics)."
                                       : "Installed. The redesigned art is selected (Options > Graphics)."),
                             st_.r(x, y, w, 36), Colour{150, 220, 120, 255}, small) /
                     st_.s +
                 6;
        } else if (s.state == State::Failed || s.state == State::CheckFailed) {
            const std::string what = tr(s.state == State::Failed ? "Download failed" : "Could not check for the latest version");
            y += ui_.wrapped(what + ": " + uiUtf8(s.error), st_.r(x, y, w, 36), theme::kBad, small) / st_.s + 6;
        } else if (s.state == State::Cancelled) {
            textAt(tr("Download cancelled - Download continues where it stopped."), y, theme::kDim, small);
            y += lineH + 2;
        } else if (inst.installed && !inst.version.empty() && s.haveManifest && !update) {
            textAt(tr("Up to date."), y, Colour{150, 220, 120, 255}, small);
            y += lineH + 2;
        }
        float bx = x;
        const bool failed = s.state == State::Failed;
        // A copy without a version (put there by hand / a development pack) can be replaced by
        // the release: the download goes to the app data folder, which is searched first.
        if (!inst.installed || inst.version.empty() || update || failed || s.state == State::Cancelled) {
            std::string label = tr(failed ? "Retry" : update ? "Update" : "Download");
            if (s.haveManifest) label += " (" + formatBytes(s.manifest.size) + ")";
            if (ui_.button(st_.r(bx, y, 170, bh), label, ButtonStyle::Primary, !busy)) dl.download();
            bx += 176;
        }
        if (ui_.button(st_.r(bx, y, 110, bh), tr(s.state == State::CheckFailed ? "Retry" : "Check now"), ButtonStyle::Normal, !busy))
            dl.check();
        bx += 116;
        if (failed || s.state == State::CheckFailed)
            if (ui_.button(st_.r(bx, y, std::min(200.f, x + w - bx), bh), tr("Open download page"))) openPage();
        y += bh + 8;
    }
    // Settings + where it goes.
    ui_.toggle(st_.r(x, y, w, 20), tr("Check for updates when Options opens"), settings_.artPackUpdateCheck);
    y += 26;
    const std::string dir = render::ArtPack::userDir();
    y += ui_.wrapped(std::string(tr("Downloaded from")) + " github.com/OpenD2K/opend2k. " + tr("Installed in") + ": " + uiUtf8(dir),
                     st_.r(x, y, w, 30), theme::kDim, small * 0.95f) /
         st_.s;
    if (ui_.button(st_.r(100, 377, 110, 22), tr("Back")) || ui_.takeBack())
        open(artPackBack_ == "graphics"  ? MenuPage::Graphics
             : artPackBack_ == "updates" ? MenuPage::Updates
             : artPackBack_ == "welcome" ? MenuPage::Welcome
                                         : MenuPage::Options);
    return res;
}


// ---- first-run welcome page ------------------------------------------------------------------
// Shown once per install (Settings::firstRunArtPackAsked, false only for a new player) right after
// the game data is set up, before the main menu (both menu sets): what OpenD2K is, the look
// (Original / Enhanced / 3D, each with its preview rendered from the player's data) and, while
// the OpenD2K art pack is missing, what it adds and a Download button. The manifest (for the
// size) is fetched when the page opens, as for the Options page; nothing is downloaded before
// the player presses Download. A download keeps running after Continue (Options > Art pack shows
// it); once installed the art and the 3D models switch to the pack (applyInstalledArtPack) and a
// 3D choice becomes the 3D preset. Beside the art pack, the cutscenes: the game files (the Gruntmods
// Edition installer) have none, so the box says so, opens the Gruntmods cutscene page and imports a
// cutscene installer with the data-folder page's file picker / import (welcomeCutscenes).

bool Menu::welcomeDue() const {
    if (settings_.firstRunArtPackAsked || !hasData() || !persistent_) return false;
    return page_ == MenuPage::Main || (page_ == MenuPage::Original && original_->current() == "MAINMENU");
}

void Menu::finishWelcome() {
    settings_.firstRunArtPackAsked = true;
    wantSave = true;
    open(MenuPage::Main);  // Graphics: Original -> the original MAINMENU
}

bool Menu::openWelcomePreview(const std::string& nameIn) {
    // "-step2": the phone layout's second step (Downloads) of the same state.
    std::string name = nameIn;
    welcomeStep_ = 0;
    if (name.size() > 6 && name.compare(name.size() - 6, 6, "-step2") == 0) {
        name.resize(name.size() - 6);
        welcomeStep_ = 1;
    }
    formats::ArtPackManifest m;
    m.name = kArtPackName;
    m.title = "OpenD2K redesign pack";
    m.version = "0.0.1";
    m.size = 612'400'000;
    m.licence = "cc-by-sa-4.0";
    m.sha256 = std::string(64, '0');
    auto& dl = ArtPackDownloader::instance();
    gPreviewInstalled = 0;
    if (name == "welcome") dl.previewState(State::Checked, m, 0, "");
    else if (name == "welcome-nomanifest") dl.previewState(State::Idle, {}, 0, "");
    else if (name == "welcome-downloading") {
        dl.previewState(State::Downloading, m, 276'000'000, "");
        welcomeWants3d_ = true;
    } else if (name == "welcome-failed") dl.previewState(State::Failed, m, 0, "checksum mismatch - the download was damaged, please retry");
    else if (name == "welcome-installed") {
        gPreviewInstalled = 1;
        dl.previewState(State::Done, m, m.size, "");
        // A pack on this machine: selected as after the download (the previews show it).
        if (const std::string p = render::ArtPack::threeDPack(); !p.empty()) settings_.artPack = settings_.modelPack = p;
    } else if (name == "welcome-graphics" || name == "welcome-graphics-custom") {
        // The Graphics page as a new player without the pack sees it (the art row's offer).
        overrideRedesignArtPackInstalled(0);
        dl.previewState(State::Checked, m, 0, "");
        settings_.applyPreset(Settings::GraphicsPreset::Enhanced);
        open(MenuPage::Graphics);
        if (name == "welcome-graphics-custom") graphicsTab_ = static_cast<int>(Settings::GraphicsPreset::Custom);
        return true;
    } else if (name.rfind("welcome-cutscenes-", 0) == 0) {
        // The cutscenes box: none / some / all found, an installer being unpacked, just added (made-up counts).
        dl.previewState(State::Checked, m, 0, "");
        const std::string v = name.substr(18);
        const int total = int(Movies::gallery().size());
        if (v == "none") previewCutscenes_ = 0;
        else if (v == "some") previewCutscenes_ = total / 3;
        else if (v == "all") previewCutscenes_ = total;
        else if (v == "importing") {
            previewCutscenes_ = 0;
            previewImport_ = true;
        } else if (v == "added") {
            previewCutscenes_ = total;
            importInfo_ = render::trf("Added %d cutscene files to %%s", 43);
            if (const auto at = importInfo_.find("%s"); at != std::string::npos) importInfo_.replace(at, 2, "Movies");
        } else {
            return false;
        }
    } else if (name == "welcome-live") {
        gPreviewInstalled = -1;  // the real state (with --artpack-manifest: a real check)
    } else {
        return false;
    }
    // As a new player sees it: the Enhanced preset (one-frame runs never save settings).
    if (name != "welcome-live") settings_.applyPreset(Settings::GraphicsPreset::Enhanced);
    open(MenuPage::Welcome);
    return true;
}

namespace {

// The welcome page's art pack offer: what the pack adds, or why it cannot be downloaded.
std::string artOfferText(const ArtPackDownloader::Status& s) {
    std::string text = tr("Redesigned units and buildings for Enhanced, and the 3D models the 3D mode needs. Free (CC BY-SA). "
                          "Without it, Enhanced shows the original 1998 sprites.");
    if (kTouchPlatform) text += std::string(" ") + tr("Large download: Wi-Fi recommended.");
    if (s.state == State::Failed) text = std::string(tr("Download failed")) + ": " + uiUtf8(s.error);
    if (s.manualOnly) text = tr("Downloading in the game is not available here. Options > Art pack has the download page.");
    return text;
}

}  // namespace

MenuResult Menu::pageWelcome() {
    using GP = Settings::GraphicsPreset;
    MenuResult res;
    backdrop(back_, 0.35f);
    auto& dl = ArtPackDownloader::instance();
    const auto s = dl.status();
    const bool installed = artPackInstalled();
    // 3D needs the pack's models (screenshots: as the made-up installed state says).
    const bool can3d = gPreviewInstalled >= 0 ? gPreviewInstalled > 0 : Settings::modelPackInstalled();
    if (phoneUi()) return pageWelcomePhone(installed, can3d);
    const bool busy = s.state == State::Downloading || s.state == State::Verifying || s.state == State::Installing;
    const float small = ui_.textScale * 0.85f;
    const float x0 = 28, w0 = 584;
    ui_.panel(st_.r(16, 10, 608, 384));
    const float titleTs = std::round(ui_.textScale * 1.5f);
    {
        Ui::BoldScope bold(ui_);
        const SDL_FRect t = st_.r(x0, 18, 0, 0);
        ui_.text(tr("Welcome to OpenD2K"), t.x, t.y, theme::kTitle, titleTs);
    }
    float y = 18 + ui_.textHeight(titleTs) / st_.s + 4;
    y += ui_.wrapped(tr("OpenD2K is a free, native rebuild of Dune 2000 (1998). It plays the original game - campaigns, "
                        "missions, rules and balance - from your own game files, with optional modern graphics."),
                     st_.r(x0, y, w0, 40), theme::kText, small) /
             st_.s +
         6;
    {
        const SDL_FRect h = st_.r(x0, y, w0, 0);
        ui_.text(tr("Choose your look"), h.x, h.y, theme::kAccent, ui_.textScale);
        ui_.text(tr("You can change it any time in Options > Graphics."), h.x + h.w, h.y + ui_.textHeight(ui_.textScale) -
                 ui_.textHeight(small), theme::kDim, small, Align::Right);
        y += ui_.textHeight(ui_.textScale) / st_.s + 4;
    }
    // Three cards: picture, name, what it is.
    const GP active = settings_.activePreset();
    const bool phone = phoneUi();  // the phone's larger text: taller buttons
    const float gap = 10, cw = (w0 - 2 * gap) / 3;
    static const char* const kNames[] = {"Original", "Enhanced", "3D"};
    static const char* const kBadge[] = {"1998", "2D", "3D"};
    const char* lines[3] = {
        "The 1998 game, exactly: the original pixels, sidebar, shroud and effects.",
        installed ? "Modern 2D: redesigned units and buildings, designed terrain, lighting, modern effects and the modern panel."
                  : "Modern 2D: designed terrain, lighting, modern effects and the modern panel, with the original 1998 unit "
                    "and building sprites.",
        can3d ? "A 2.5D battlefield: 3D models, 3D terrain with sun shadows, modern effects."
              : "A 2.5D battlefield with 3D models. Needs the art pack."};
    // As tall as the longest description needs (languages and the phone font differ), so the boxes
    // below get the rest.
    float textH = 0;
    for (const char* l : lines)
        textH = std::max(textH, ui_.wrapped(tr(l), st_.r(0, 0, cw - 12, 100), theme::kText, small * 0.95f, 0, false) / st_.s);
    const float cardTextH = ui_.textHeight(ui_.textScale) / st_.s + 2 + std::min(textH, 56.f) + 2;
    // The boxes below (art pack | cutscenes) need their text + a button row: the pictures give way
    // when they do not fit (phones: the font is larger against the page).
    const float gapB = 8, artW = 304, cutW = w0 - artW - gapB;
    auto measure = [&](const std::string& t, float wIn, float sc) {
        return ui_.wrapped(t, st_.r(0, 0, wIn, 100), theme::kText, sc, 0, false) / st_.s;
    };
    const std::string offer = artOfferText(s);
    const float headH = ui_.textHeight(ui_.textScale * 0.95f) / st_.s + 3, rowH = 5 + 20 + 2 + 5;
    const float artNeed = 5 + headH + (installed && !busy ? 24.f : busy ? 40.f : measure(offer, artW - 16, small)) + rowH;
    const float cutNeed = 5 + headH +
                          measure(tr("Not included in the game files: a separate free download from Gruntmods, one installer per language."),
                                  cutW - 16, small) +
                          2 + measure(tr("A PS1 disc image also works: Options > Game data folder."), cutW - 16, small * 0.9f) + rowH;
    const float ph = std::clamp(360 - y - 8 - std::max(artNeed, cutNeed) - cardTextH, cw * 5 / 16, cw * 9 / 16);
    const float cardH = ph + cardTextH;
    for (int i = 0; i < 3; ++i) {
        const float cx = x0 + i * (cw + gap);
        const SDL_FRect card = st_.r(cx, y, cw, cardH);
        const bool disabled = i == 2 && !can3d;
        const bool selected = disabled ? welcomeWants3d_ : static_cast<int>(active) == i && !welcomeWants3d_;
        bool hot = false;
        ui_.fill(card, selected ? Colour{70, 48, 24, 230} : Colour{26, 18, 11, 200});
        if (ui_.hotspot(card, &hot)) {
            if (disabled) {
                welcomeWants3d_ = !welcomeWants3d_;  // switched on once the pack is installed
            } else {
                welcomeWants3d_ = false;
                settings_.applyPreset(static_cast<GP>(i));
            }
            wantSave = true;
        }
        GraphicsPreviews::Picture pic;
        if (previews_ && !disabled) pic = previews_->preset(i, settings_);
        else pic.unavailable = true;
        drawPreviewCard(ui_, st_.r(cx + 4, y + 4, cw - 8, ph - 8), pic, kBadge[i], disabled ? tr("Needs the art pack") : nullptr, small);
        float ty = y + ph;
        {
            Ui::BoldScope bold(ui_);
            const SDL_FRect t = st_.r(cx + 6, ty, 0, 0);
            ui_.text(tr(kNames[i]), t.x, t.y, disabled ? theme::kDim : theme::kTitle, ui_.textScale);
        }
        if (selected) {
            const SDL_FRect m = st_.r(cx + cw - 6, ty, 0, 0);
            ui_.text(tr(disabled ? "after the download" : "Chosen"), m.x, m.y + ui_.textHeight(ui_.textScale) - ui_.textHeight(small),
                     Colour{150, 220, 120, 255}, small, Align::Right);
        }
        ty += ui_.textHeight(ui_.textScale) / st_.s + 2;
        ui_.wrapped(tr(lines[i]), st_.r(cx + 6, ty, cw - 12, cardH - (ty - y) - 2), disabled ? theme::kDim : theme::kText, small * 0.95f);
        // (hot also means keyboard focus, which starts on the first card: only the mouse lights it here)
        const bool under = hot && ui_.mouseX() >= card.x && ui_.mouseX() < card.x + card.w && ui_.mouseY() >= card.y &&
                           ui_.mouseY() < card.y + card.h;
        ui_.outline(card, selected ? theme::kAccent : under ? theme::kTitle : theme::kEdge, selected ? 2.f : 1.f);
    }
    y += cardH + 8;

    // Two boxes side by side: the art pack (what it is and Download, the progress, or installed)
    // and the cutscenes (welcomeCutscenes).
    const float boxH = 360 - y;
    const SDL_FRect box = st_.r(x0, y, artW, boxH);
    ui_.fill(box, Colour{60, 40, 20, 200});
    ui_.outline(box, theme::kAccent);
    const float bx = x0 + 8, bw = artW - 16;
    float by = y + 5;
    const std::string size = s.haveManifest ? formatBytes(s.manifest.size) : std::string(tr("about 600 MB"));
    auto heading = [&](const std::string& t, Colour c) {
        Ui::BoldScope bold(ui_);
        const SDL_FRect r = st_.r(bx, by, 0, 0);
        const float hs = ui_.textWidth(t, ui_.textScale * 0.95f) > bw * st_.s ? small : ui_.textScale * 0.95f;  // long: smaller
        ui_.text(ui_.ellipsize(t, bw * st_.s, hs), r.x, r.y + ui_.textHeight(ui_.textScale * 0.95f) - ui_.textHeight(hs), c, hs);
        by += ui_.textHeight(ui_.textScale * 0.95f) / st_.s + 3;
    };
    const float btnH = phone ? 28.f : 20.f, btnY = y + boxH - 5 - btnH;
    auto rest = [&](float wIn) { return st_.r(bx, by, wIn, btnY - by - 2); };
    if (installed && !busy) {
        heading(tr("OpenD2K art pack installed"), Colour{150, 220, 120, 255});
        ui_.wrapped(tr("The redesigned units and buildings (Enhanced) and the 3D models (3D mode) are selected."), rest(bw),
                    theme::kText, small);
    } else if (busy) {
        heading(render::trf("Downloading the OpenD2K art pack (%s)", size.c_str()), theme::kTitle);
        const float frac = s.total ? std::clamp(float(double(s.done) / double(s.total)), 0.f, 1.f) : 0.f;
        const float cancelW = 90;
        const SDL_FRect bar = st_.r(bx, by + 4, bw - cancelW - 8, 12);
        ui_.fill(bar, Colour{12, 8, 5, 255});
        ui_.fill(SDL_FRect{bar.x, bar.y, bar.w * frac, bar.h}, theme::kAccent);
        ui_.outline(bar, theme::kEdge);
        if (ui_.button(st_.r(bx + bw - cancelW, by, cancelW, btnH), tr("Cancel"), ButtonStyle::Normal, true, 0.85f)) dl.cancel();
        by += btnH + 4;
        std::string msg = s.state == State::Downloading
                              ? render::trf("%s of %s", formatBytes(s.done).c_str(), formatBytes(s.total).c_str())
                              : std::string(tr(s.state == State::Verifying ? "Checking the download..." : "Installing..."));
        msg += std::string(" - ") + tr("you can continue: it goes on in the background (Options > Art pack).");
        ui_.wrapped(msg, st_.r(bx, by, bw, y + boxH - by - 2), theme::kText, small);
    } else {
        heading(render::trf("Download the OpenD2K art pack (%s, from GitHub)?", size.c_str()), theme::kTitle);
        ui_.wrapped(offer, rest(bw), s.state == State::Failed ? theme::kBad : theme::kText, phone ? small * 0.85f : small);
        const float btnW = 150, btnX = bx + bw - btnW;
        if (s.manualOnly) {
            if (ui_.button(st_.r(btnX, btnY, btnW, btnH), tr("Art pack"), ButtonStyle::Normal, true, 0.85f)) openArtPackFrom("welcome");
        } else if (ui_.button(st_.r(btnX, btnY, btnW, btnH),
                              std::string(tr(s.state == State::Failed ? "Retry" : "Download")) + (phone ? std::string() : " (" + size + ")"), ButtonStyle::Primary,
                              true, 0.85f)) {
            dl.download();  // the player asked for it
        }
    }
    welcomeCutscenes(res, x0 + artW + gapB, y, w0 - artW - gapB, boxH);
    // Bottom row.
    const bool decline = !installed && !busy;
    if (ui_.button(phone ? st_.r(612 - 170, 362, 170, 30) : st_.r(612 - 150, 366, 150, 22), tr(decline ? "Not now" : "Continue"),
                   decline ? ButtonStyle::Normal : ButtonStyle::Primary) ||
        ui_.takeBack())
        finishWelcome();
    if (welcomeWants3d_ && !can3d) {
        const SDL_FRect n = st_.r(x0, 370, 0, 0);
        ui_.text(tr("3D is switched on when the art pack is installed."), n.x, n.y, theme::kDim, small);
    }
    return res;
}

// The welcome page's cutscenes box. The Gruntmods Edition installer has no cutscenes (they are
// separate free installers per language, kCutsceneDownloadPage), so a new player is told where to
// get them. "Add cutscene installer..." is the data-folder page's import: the SDL file dialog on
// desktops and Android (the system picker), and on iOS the cutscene installers found in the app's
// Documents (Files app), as documentsSection lists them. A PS1 disc image also works (data folder page).
void Menu::welcomeCutscenes(MenuResult& res, float x, float y, float w, float h) {
    const auto [found, total] = cutsceneCount();
    const bool all = found >= total;
    const float small = ui_.textScale * 0.85f, headTs = ui_.textScale * 0.95f;
    const Colour green{150, 220, 120, 255};
    const SDL_FRect box = st_.r(x, y, w, h);
    ui_.fill(box, Colour{60, 40, 20, 200});
    ui_.outline(box, theme::kAccent);
    const float bx = x + 8, bw = w - 16;
    float by = y + 5;
    {
        Ui::BoldScope bold(ui_);
        const SDL_FRect r = st_.r(bx, by, 0, 0);
        ui_.text(tr("Cutscenes (optional)"), r.x, r.y, all ? green : theme::kTitle, headTs);
    }
    {
        const std::string status = all ? render::trf("all %d found", total)
                                       : found ? render::trf("%d of %d found", found, total) : std::string(tr("none found"));
        const SDL_FRect r = st_.r(bx + bw, by, 0, 0);
        ui_.text(status, r.x, r.y + ui_.textHeight(headTs) - ui_.textHeight(small), all ? green : found ? theme::kAccent : theme::kDim,
                 small, Align::Right);
    }
    by += ui_.textHeight(headTs) / st_.s + 3;
    const float btnH = 20, btnY = y + h - 5 - btnH;

    if (import_ || previewImport_) {  // an installer chosen here is being unpacked
        const bool preview = !import_;
        const std::string name = preview ? "Dune_2000_Gruntmods_Edition_Cutscenes.exe"
                                         : std::filesystem::path(import_->installer()).filename().string();
        const float frac = preview ? 0.42f : import_->fraction();
        const int done = preview ? 18 : int(import_->filesDone()), count = preview ? 43 : int(import_->filesTotal());
        const SDL_FRect t = st_.r(bx, by, 0, 0);
        ui_.text(ui_.ellipsize(render::trf("Unpacking %s", uiUtf8(name).c_str()), bw * st_.s, small), t.x, t.y, theme::kText, small);
        by += ui_.textHeight(small) / st_.s + 6;
        const float cancelW = 80;
        const SDL_FRect bar = st_.r(bx, by + 4, bw - cancelW - 8, 12);
        ui_.fill(bar, Colour{12, 8, 5, 255});
        ui_.fill(SDL_FRect{bar.x, bar.y, bar.w * std::clamp(frac, 0.f, 1.f), bar.h}, theme::kAccent);
        ui_.outline(bar, theme::kEdge);
        const bool cancelling = import_ && importCancelled_;
        if (ui_.button(st_.r(bx + bw - cancelW, by, cancelW, btnH), cancelling ? tr("Cancelling...") : tr("Cancel"), ButtonStyle::Normal,
                       !cancelling, 0.85f) &&
            import_) {
            import_->cancel();
            importCancelled_ = true;
        }
        by += btnH + 4;
        const SDL_FRect p = st_.r(bx, by, 0, 0);
        ui_.text(render::trf("%d%%   %d / %d files", int(frac * 100 + 0.5f), done, count), p.x, p.y, theme::kDim, small);
        pollImport(res);
        return;
    }
    {
        std::lock_guard lock(folderMutex_);
        startPickedImport();  // "Add cutscene installer..." (the dialog's result arrives on another thread)
    }
    const CutsceneBoxText cb = cutsceneBoxText(all);
    const std::string& iosInstaller = cb.iosInstaller;
    by += ui_.wrapped(tr(cb.text), st_.r(bx, by, bw, btnY - by), theme::kText, small) / st_.s + 2;
    const std::string& note = cb.note;
    const Colour noteC = cb.noteColour;
    if (!note.empty()) ui_.wrapped(note, st_.r(bx, by, bw, btnY - by), noteC, small * 0.9f);
    if (all) return;
    const float b2 = (bw - 6) / 2;
    const SDL_FRect get = st_.r(bx, btnY, b2, btnH), add = st_.r(bx + b2 + 6, btnY, b2, btnH);
    // The Gruntmods page: one "Download <Language> Cutscenes" button per language (movies.hpp).
    if (ui_.button(get, ui_.ellipsize(tr("Get cutscenes"), get.w - 8, small), ButtonStyle::Primary, true, 0.85f))
        SDL_OpenURL(kCutsceneDownloadPage);
    if constexpr (kIOS) {
        if (!iosInstaller.empty() &&
            ui_.button(add,
                       ui_.ellipsize(std::string(tr("Add cutscenes from")) + " " + std::filesystem::path(iosInstaller).filename().string(),
                                     add.w - 8, small),
                       ButtonStyle::Normal, true, 0.85f)) {
            std::lock_guard lock(folderMutex_);
            pickedInstaller_ = iosInstaller;
            installerPicked_ = true;
        }
    } else if (ui_.button(add, ui_.ellipsize(tr("Add cutscene installer..."), add.w - 8, small), ButtonStyle::Normal, true, 0.85f)) {
        requestInstaller();
    }
}

// The cutscenes box's text, both layouts: on iOS the cutscene installer found in the app's Documents
// (Files app; there is no file dialog), the main line and the small note (an error, the import's
// result, or where else cutscenes come from).
Menu::CutsceneBoxText Menu::cutsceneBoxText(bool all) {
    CutsceneBoxText cb;
    if constexpr (kIOS) {
        const Uint64 now = SDL_GetTicks();
        if (docScanAt_ == 0 || now - docScanAt_ > 2000) {
            docScan_ = scanDocuments();
            docScanAt_ = now;
        }
        for (const std::string& f : docScan_.installers)
            if (isCutsceneInstallerName(f)) cb.iosInstaller = f;
    }
    cb.text = all ? "All the original cutscenes are here: they play in the campaign, between the missions."
                  : "Not included in the game files: a separate free download from Gruntmods, one installer per language.";
    cb.noteColour = theme::kDim;
    if (!dataDirError.empty()) {
        cb.note = dataDirError;
        cb.noteColour = theme::kBad;
    } else if (!importInfo_.empty()) {
        cb.note = importInfo_;
        cb.noteColour = Colour{150, 220, 120, 255};
    } else if (!all) {
        // (the Files app names the device: "On My iPhone" / "On My iPad")
        cb.note = kIOS && cb.iosInstaller.empty()
                      ? (isPhoneDevice()
                             ? tr("Copy the installer (.exe) into Files > On My iPhone > OpenD2K. A PS1 disc image (.cue + .bin) also works.")
                             : tr("Copy the installer (.exe) into Files > On My iPad > OpenD2K. A PS1 disc image (.cue + .bin) also works."))
                      : tr("A PS1 disc image also works: Options > Game data folder.");
    }
    return cb;
}

// ---- the welcome page on phones --------------------------------------------------------------
// phoneUi() (an iPhone / Android phone in landscape, ~390 x 844 dp): the stage layout scaled down
// leaves ~1 dp per stage unit there, so the page has its own layout in output pixels, in two steps.
// Step 1 "Choose your look": three large cards (picture, name, one short line), Next. Step 2
// "Downloads": the art pack and the cutscenes side by side with their full text and 48 dp buttons
// (scrolling if a language needs more room), Back and Not now / Continue. Text: labels ~16 sp,
// body 14 sp, small print 13 sp. The window's safe area keeps it out of the notch.

namespace {
constexpr Colour kWelcomeGreen{150, 220, 120, 255};
}

float Menu::welcomeCutscenesPhone(MenuResult& res, const SDL_FRect& r, bool draw) {
    const auto [found, total] = cutsceneCount();
    const bool all = found >= total;
    const float d = dp(), pad = std::round(10 * d), gap = std::round(8 * d), bh = std::round(48 * d);
    const float label = ui_.textScale, body = std::max(ts_, 15 * d / 12.5f), small = std::max(ts_ * 0.9f, 13.5f * d / 12.5f);
    const float x = r.x + pad, w = r.w - 2 * pad;
    if (draw) {
        ui_.fill(r, Colour{60, 40, 20, 210});
        ui_.outline(r, theme::kAccent);
    }
    float y = r.y + pad;
    {  // heading + status (beside it when both fit, else below)
        const std::string head = tr("Cutscenes (optional)");
        const std::string status = all ? render::trf("all %d found", total)
                                       : found ? render::trf("%d of %d found", found, total) : std::string(tr("none found"));
        Ui::BoldScope bold(ui_);
        const float hw = ui_.textWidth(head, label);
        const bool beside = hw + gap + ui_.textWidth(status, small) <= w;
        if (draw) ui_.text(head, x, y, all ? kWelcomeGreen : theme::kTitle, label);
        y += ui_.textHeight(label);
        const Colour sc = all ? kWelcomeGreen : found ? theme::kAccent : theme::kDim;
        Ui::BoldScope regular(ui_, false);
        if (beside) {
            if (draw) ui_.text(status, x + w, y - ui_.textHeight(small), sc, small, Align::Right);
        } else {
            if (draw) ui_.text(status, x, y + 2 * d, sc, small);
            y += ui_.textHeight(small) + 2 * d;
        }
        y += gap * 0.75f;
    }
    const float btnY = r.y + r.h - pad - bh;
    if (import_ || previewImport_) {  // an installer chosen here is being unpacked
        const bool preview = !import_;
        const std::string name = preview ? "Dune_2000_Gruntmods_Edition_Cutscenes.exe"
                                         : std::filesystem::path(import_->installer()).filename().string();
        const float frac = preview ? 0.42f : import_->fraction();
        const int done = preview ? 18 : int(import_->filesDone()), count = preview ? 43 : int(import_->filesTotal());
        if (draw) ui_.text(ui_.ellipsize(render::trf("Unpacking %s", uiUtf8(name).c_str()), w, body), x, y, theme::kText, body);
        y += ui_.textHeight(body) + gap;
        const SDL_FRect bar{x, y, w, std::round(12 * d)};
        if (draw) {
            ui_.fill(bar, Colour{12, 8, 5, 255});
            ui_.fill(SDL_FRect{bar.x, bar.y, bar.w * std::clamp(frac, 0.f, 1.f), bar.h}, theme::kAccent);
            ui_.outline(bar, theme::kEdge);
        }
        y += bar.h + gap;
        if (draw)
            ui_.text(render::trf("%d%%   %d / %d files", int(frac * 100 + 0.5f), done, count), x, y, theme::kDim, small);
        y += ui_.textHeight(small);
        if (draw) {
            const bool cancelling = import_ && importCancelled_;
            if (ui_.button({x, btnY, w, bh}, cancelling ? tr("Cancelling...") : tr("Cancel"), ButtonStyle::Normal, !cancelling) &&
                import_) {
                import_->cancel();
                importCancelled_ = true;
            }
            pollImport(res);
        }
        return y + gap + bh + pad - r.y;
    }
    if (draw) {
        std::lock_guard lock(folderMutex_);
        startPickedImport();  // "Add cutscene installer..." (the dialog's result arrives on another thread)
    }
    const CutsceneBoxText cb = cutsceneBoxText(all);
    y += ui_.wrapped(tr(cb.text), {x, y, w, 10000}, theme::kText, body, 0, draw) + gap * 0.5f;
    if (!cb.note.empty()) y += ui_.wrapped(cb.note, {x, y, w, 10000}, cb.noteColour, small, 0, draw) + gap * 0.5f;
    if (all) return y + pad - r.y;
    // Buttons: Get cutscenes (the Gruntmods page) and the import (desktop / Android: the file picker;
    // iOS: the installer found in Files), side by side when both labels fit, else one above the other.
    std::string addLabel;
    if constexpr (kIOS) {
        if (!cb.iosInstaller.empty())
            addLabel = std::string(tr("Add cutscenes from")) + " " + std::filesystem::path(cb.iosInstaller).filename().string();
    } else {
        addLabel = tr("Add cutscene installer...");
    }
    const std::string getLabel = tr("Get cutscenes");
    const float half = (w - gap) / 2, inner = 16 * d;
    const bool stacked = !addLabel.empty() && (ui_.textWidth(getLabel, label) > half - inner || ui_.textWidth(addLabel, label) > half - inner);
    const float buttonsH = stacked ? 2 * bh + gap : bh;
    if (draw) {
        const float by = r.y + r.h - pad - buttonsH;
        SDL_FRect get{x, by, w, bh}, add{x, by + bh + gap, w, bh};
        if (!addLabel.empty() && !stacked) {
            get.w = add.w = half;
            add = {x + half + gap, by, half, bh};
        }
        if (ui_.button(get, ui_.ellipsize(getLabel, get.w - inner, label), ButtonStyle::Primary)) SDL_OpenURL(kCutsceneDownloadPage);
        if (!addLabel.empty() && ui_.button(add, ui_.ellipsize(addLabel, add.w - inner, label))) {
            if constexpr (kIOS) {
                std::lock_guard lock(folderMutex_);
                pickedInstaller_ = cb.iosInstaller;
                installerPicked_ = true;
            } else {
                requestInstaller();
            }
        }
    }
    return y + gap * 0.5f + buttonsH + pad - r.y;
}

MenuResult Menu::pageWelcomePhone(bool installed, bool can3d) {
    using GP = Settings::GraphicsPreset;
    MenuResult res;
    auto& dl = ArtPackDownloader::instance();
    const auto s = dl.status();
    const bool busy = s.state == State::Downloading || s.state == State::Verifying || s.state == State::Installing;
    const SDL_FRect sa = safeArea();
    const float d = dp(), m = std::round(12 * d), gap = std::round(10 * d), bh = std::round(48 * d);
    const float label = ui_.textScale, body = std::max(ts_, 15 * d / 12.5f), small = std::max(ts_ * 0.9f, 13.5f * d / 12.5f);
    const float titleTs = std::round(label * 1.3f);
    const float L = sa.x + m, R = sa.x + sa.w - m, T = sa.y + std::round(8 * d), B = sa.y + sa.h - m, W = R - L;
    const float barY = B - bh;
    {  // darker than the desktop page's backdrop: the stage picture's frame shows around the boxes here
        int ow = 0, oh = 0;
        SDL_GetCurrentRenderOutputSize(r_, &ow, &oh);
        ui_.fill({0, 0, float(ow), float(oh)}, Colour{0, 0, 0, 110});
    }
    // Header: the step's title, "1 / 2" on the right.
    {
        Ui::BoldScope bold(ui_);
        ui_.text(welcomeStep_ == 0 ? tr("Welcome to OpenD2K") : tr("Downloads"), L, T, theme::kTitle, titleTs);
    }
    ui_.text(welcomeStep_ == 0 ? "1 / 2" : "2 / 2", R, T + ui_.textHeight(titleTs) - ui_.textHeight(label), theme::kDim, label,
             Align::Right);
    float top = T + ui_.textHeight(titleTs) + std::round(4 * d);
    const bool note3d = welcomeWants3d_ && !can3d;

    if (welcomeStep_ == 0) {
        ui_.text(tr("Choose your look"), L, top, theme::kAccent, label);
        top += ui_.textHeight(label) + std::round(6 * d);
        // Three cards: picture, name (+ Chosen), one short line.
        const GP active = settings_.activePreset();
        static const char* const kNames[] = {"Original", "Enhanced", "3D"};
        static const char* const kBadge[] = {"1998", "2D", "3D"};
        const char* lines[3] = {"The 1998 game, exactly as it was.",
                                installed ? "Modern 2D with redesigned units and buildings."
                                          : "Modern 2D terrain and effects, the original unit sprites.",
                                "A 2.5D battlefield with 3D models and sun shadows."};
        const float cw = std::floor((W - 2 * gap) / 3), pad = std::round(8 * d);
        float descH = 0;
        for (const char* l : lines) descH = std::max(descH, ui_.wrapped(tr(l), {0, 0, cw - 2 * pad, 10000}, theme::kText, body, 0, false));
        const float textH = std::round(4 * d) + ui_.textHeight(label) + std::round(2 * d) + descH + pad;
        const float avail = barY - gap - top;
        const float picH = std::max(24 * d, std::min(avail - pad - textH, (cw - 2 * pad) * 0.625f));
        const float cardH = pad + picH + textH;
        const float cy = top + std::max(0.f, std::round((avail - cardH) / 2));
        for (int i = 0; i < 3; ++i) {
            const float cx = L + i * (cw + gap);
            const SDL_FRect card{cx, cy, cw, cardH};
            const bool disabled = i == 2 && !can3d;
            const bool selected = disabled ? welcomeWants3d_ : static_cast<int>(active) == i && !welcomeWants3d_;
            bool hot = false;
            ui_.fill(card, selected ? Colour{70, 48, 24, 235} : Colour{26, 18, 11, 215});
            if (ui_.hotspot(card, &hot)) {
                if (disabled) {
                    welcomeWants3d_ = !welcomeWants3d_;  // switched on once the pack is installed
                } else {
                    welcomeWants3d_ = false;
                    settings_.applyPreset(static_cast<GP>(i));
                }
                wantSave = true;
            }
            GraphicsPreviews::Picture pic;
            if (previews_ && !disabled) pic = previews_->preset(i, settings_);
            else pic.unavailable = true;
            drawPreviewCard(ui_, {cx + pad, cy + pad, cw - 2 * pad, picH}, pic, kBadge[i], disabled ? tr("Needs the art pack") : nullptr,
                            small / 1.0f);
            float ty = cy + pad + picH + std::round(4 * d);
            {
                Ui::BoldScope bold(ui_);
                ui_.text(tr(kNames[i]), cx + pad, ty, disabled ? theme::kDim : theme::kTitle, label);
            }
            if (selected)
                ui_.text(tr(disabled ? "after the download" : "Chosen"), cx + cw - pad, ty + ui_.textHeight(label) - ui_.textHeight(small),
                         kWelcomeGreen, small, Align::Right);
            ty += ui_.textHeight(label) + std::round(2 * d);
            ui_.wrapped(tr(lines[i]), {cx + pad, ty, cw - 2 * pad, cy + cardH - ty}, disabled ? theme::kDim : theme::kText, body);
            const bool under = hot && ui_.mouseX() >= card.x && ui_.mouseX() < card.x + card.w && ui_.mouseY() >= card.y &&
                               ui_.mouseY() < card.y + card.h;
            ui_.outline(card, selected ? theme::kAccent : under ? theme::kTitle : theme::kEdge, selected ? std::max(2.f, 2 * d) : std::max(1.f, d));
        }
        // Bottom: the hint (or the 3D note) and Next.
        const std::string next = std::string(tr("Next")) + "  >";
        const float nw = std::max(std::round(180 * d), ui_.textWidth(next, label) + 40 * d);
        const SDL_FRect nr{R - nw, barY, nw, bh};
        const std::string hint = note3d ? tr("3D is switched on when the art pack is installed.") : tr("You can change it any time in Options > Graphics.");
        const SDL_FRect hr{L, barY, nr.x - gap - L, bh};
        const float hh = ui_.wrapped(hint, hr, theme::kDim, small, 0, false);
        ui_.wrapped(hint, {hr.x, barY + std::max(0.f, (bh - hh) / 2), hr.w, bh}, theme::kDim, small);
        if (ui_.button(nr, next, ButtonStyle::Primary)) {
            welcomeStep_ = 1;
            welcomeScroll_ = 0;
            ui_.resetFocus();
        }
        if (ui_.takeBack()) finishWelcome();
        return res;
    }

    // Step 2: the art pack (what it adds and Download / the progress / installed) and the cutscenes,
    // in two equal columns; the content scrolls when a language needs more than the screen.
    const float colW = std::floor((W - gap) / 2), pad = std::round(10 * d), sg = std::round(8 * d);
    const std::string size = s.haveManifest ? formatBytes(s.manifest.size) : std::string(tr("about 600 MB"));
    auto artBox = [&](const SDL_FRect& r, bool draw) -> float {
        const float x = r.x + pad, w = r.w - 2 * pad;
        if (draw) {
            ui_.fill(r, Colour{60, 40, 20, 210});
            ui_.outline(r, theme::kAccent);
        }
        float y = r.y + pad;
        {  // heading + status
            const bool done = installed && !busy;
            const std::string head = done ? tr("OpenD2K art pack installed") : tr("OpenD2K art pack");
            const float frac = s.total ? std::clamp(float(double(s.done) / double(s.total)), 0.f, 1.f) : 0.f;
            const std::string status = done ? std::string() : busy ? std::to_string(int(frac * 100 + 0.5f)) + "%" : size + ", " + tr("from GitHub");
            Ui::BoldScope bold(ui_);
            const float hh = ui_.wrapped(head, {x, y, w, 10000}, done ? kWelcomeGreen : theme::kTitle, label, 0, false);
            const float hw = hh <= ui_.textHeight(label) * 1.5f ? ui_.textWidth(head, label) : w;
            if (draw) ui_.wrapped(head, {x, y, w, hh + 2}, done ? kWelcomeGreen : theme::kTitle, label);
            y += hh;
            Ui::BoldScope regular(ui_, false);
            if (!status.empty()) {
                const float sw = ui_.textWidth(status, small);
                if (hw + sg + sw <= w) {
                    if (draw) ui_.text(status, x + w, y - ui_.textHeight(small), theme::kDim, small, Align::Right);
                } else {
                    if (draw) ui_.text(status, x, y + 2 * d, theme::kDim, small);
                    y += ui_.textHeight(small) + 2 * d;
                }
            }
            y += sg * 0.75f;
        }
        const float btnY = r.y + r.h - pad - bh;
        if (installed && !busy) {
            y += ui_.wrapped(tr("The redesigned units and buildings (Enhanced) and the 3D models (3D mode) are selected."), {x, y, w, 10000},
                             theme::kText, body, 0, draw);
            return y + pad - r.y;
        }
        if (busy) {
            const float frac = s.total ? std::clamp(float(double(s.done) / double(s.total)), 0.f, 1.f) : 0.f;
            const SDL_FRect bar{x, y, w, std::round(12 * d)};
            if (draw) {
                ui_.fill(bar, Colour{12, 8, 5, 255});
                ui_.fill(SDL_FRect{bar.x, bar.y, bar.w * frac, bar.h}, theme::kAccent);
                ui_.outline(bar, theme::kEdge);
            }
            y += bar.h + sg;
            std::string msg = s.state == State::Downloading
                                  ? render::trf("%s of %s", formatBytes(s.done).c_str(), formatBytes(s.total).c_str())
                                  : std::string(tr(s.state == State::Verifying ? "Checking the download..." : "Installing..."));
            msg += std::string(" - ") + tr("you can continue: it goes on in the background (Options > Art pack).");
            y += ui_.wrapped(msg, {x, y, w, 10000}, theme::kText, body, 0, draw);
            if (draw && ui_.button({x, btnY, w, bh}, tr("Cancel"))) dl.cancel();
            return y + sg + bh + pad - r.y;
        }
        y += ui_.wrapped(artOfferText(s), {x, y, w, 10000}, s.state == State::Failed ? theme::kBad : theme::kText, body, 0, draw);
        if (draw) {
            if (s.manualOnly) {
                if (ui_.button({x, btnY, w, bh}, tr("Art pack"))) openArtPackFrom("welcome");
            } else {
                std::string b = std::string(tr(s.state == State::Failed ? "Retry" : "Download")) + " (" + size + ")";
                if (ui_.textWidth(b, label) > w - 16 * d) b = tr(s.state == State::Failed ? "Retry" : "Download");
                if (ui_.button({x, btnY, w, bh}, b, ButtonStyle::Primary)) dl.download();  // the player asked for it
            }
        }
        return y + sg + bh + pad - r.y;
    };
    const SDL_FRect view{L, top + std::round(4 * d), W, barY - gap - top - std::round(4 * d)};
    const float need = std::max(artBox({0, 0, colW, 0}, false), welcomeCutscenesPhone(res, {0, 0, W - colW - gap, 0}, false));
    const float boxH = std::max(need, view.h);
    const float y0 = ui_.beginScroll(view, welcomeScroll_, boxH, std::max(4.f, 4 * d));
    artBox({L, y0, colW, boxH}, true);
    welcomeCutscenesPhone(res, {L + colW + gap, y0, W - colW - gap, boxH}, true);
    ui_.endScroll();
    // Bottom: Back (to the looks), the 3D note, Not now / Continue.
    const std::string backLabel = tr("Back");
    const float bw = std::max(std::round(140 * d), ui_.textWidth(backLabel, label) + 40 * d);
    const bool decline = !installed && !busy;
    const std::string goLabel = tr(decline ? "Not now" : "Continue");
    const float gw = std::max(std::round(180 * d), ui_.textWidth(goLabel, label) + 40 * d);
    if (note3d) {
        const SDL_FRect nr{L + bw + gap, barY, W - bw - gw - 2 * gap, bh};
        const float nh = ui_.wrapped(tr("3D is switched on when the art pack is installed."), nr, theme::kDim, small, 0, false);
        ui_.wrapped(tr("3D is switched on when the art pack is installed."), {nr.x, barY + std::max(0.f, (bh - nh) / 2), nr.w, bh},
                    theme::kDim, small);
    }
    if (ui_.button({L, barY, bw, bh}, backLabel) || ui_.takeBack()) {
        welcomeStep_ = 0;
        ui_.resetFocus();
    }
    if (ui_.button({R - gw, barY, gw, bh}, goLabel, decline ? ButtonStyle::Normal : ButtonStyle::Primary)) finishWelcome();
    return res;
}

}  // namespace d2k::platform
