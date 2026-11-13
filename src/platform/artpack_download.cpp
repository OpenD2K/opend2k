#include "platform/artpack_download.hpp"

#include "formats/sha256.hpp"
#include "formats/zip_reader.hpp"
#include "platform/http_client.hpp"
#include "render/art_pack.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifndef D2K_VERSION
#define D2K_VERSION "0.0.0"
#endif

namespace d2k::platform {

namespace fs = std::filesystem;

namespace {

double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

// key=value of a small ini file ("" when missing).
std::string iniValue(const fs::path& file, const std::string& key) {
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos || line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (trim(line.substr(0, eq)) == key) return trim(line.substr(eq + 1));
    }
    return {};
}

}  // namespace

std::string formatBytes(std::uint64_t b) {
    char buf[32];
    if (b >= 1000ull * 1000 * 1000) std::snprintf(buf, sizeof buf, "%.1f GB", double(b) / 1e9);
    else if (b >= 1000ull * 1000) std::snprintf(buf, sizeof buf, "%.0f MB", double(b) / 1e6);
    else std::snprintf(buf, sizeof buf, "%.0f KB", double(b) / 1e3);
    return buf;
}

ArtPackDownloader& ArtPackDownloader::instance() {
    static ArtPackDownloader d;
    return d;
}

ArtPackDownloader::~ArtPackDownloader() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
}

std::string ArtPackDownloader::manifestUrl() const {
    if (!override_.empty()) return override_;
    if (const char* e = std::getenv("D2K_ARTPACK_MANIFEST_URL"); e && *e) return e;
    if (!settingsUrl_.empty()) return settingsUrl_;
    return kArtPackManifestUrl;
}

void ArtPackDownloader::autoCheck(bool enabled) {
    if (!enabled || !autoCheck_ || autoChecked_ || busy()) return;
    autoChecked_ = true;
    {
        std::lock_guard l(m_);
        if (st_.haveManifest) return;
    }
    check();
}

void ArtPackDownloader::check() { start(false); }
void ArtPackDownloader::download() { start(true); }

void ArtPackDownloader::cancel() { cancel_ = true; }

bool ArtPackDownloader::busy() const { return running_.load(); }

ArtPackDownloader::Status ArtPackDownloader::status() const {
    std::lock_guard l(m_);
    return st_;
}

bool ArtPackDownloader::takeInstalled(std::string& name) {
    std::lock_guard l(m_);
    if (installedName_.empty()) return false;
    name.swap(installedName_);
    installedName_.clear();
    return true;
}

void ArtPackDownloader::previewState(State s, const formats::ArtPackManifest& m, std::uint64_t done, const std::string& error) {
    std::lock_guard l(m_);
    st_.state = s;
    st_.haveManifest = !m.name.empty();
    st_.manifest = m;
    st_.done = done;
    st_.total = m.size;
    st_.bytesPerSec = s == State::Downloading ? 6.4e6 : 0;
    st_.error = error;
    autoChecked_ = true;
}

void ArtPackDownloader::start(bool download) {
    if (running_.exchange(true)) {
        // Busy (an update check that the Options page started): download right after it.
        if (download) pendingDownload_ = true;
        return;
    }
    if (worker_.joinable()) worker_.join();
    cancel_ = false;
    pendingDownload_ = false;
    worker_ = std::thread([this, download] {
        work(download);
        while (pendingDownload_.exchange(false) && !cancel_.load()) {
            bool checked = false;
            {
                std::lock_guard l(m_);
                checked = st_.haveManifest;
            }
            if (!checked) break;
            work(true);
        }
        running_ = false;
    });
}

void ArtPackDownloader::setState(State s) {
    std::lock_guard l(m_);
    st_.state = s;
    st_.error.clear();
}

void ArtPackDownloader::fail(State s, const std::string& error) {
    std::lock_guard l(m_);
    st_.state = cancel_.load() ? State::Cancelled : s;
    st_.error = cancel_.load() ? std::string() : error;
    SDL_Log("Art pack: %s", error.c_str());
}

bool ArtPackDownloader::fetchManifest() {
    std::string why;
    if (!http::available(&why)) {
        {
            std::lock_guard l(m_);
            st_.manualOnly = true;
        }
        fail(State::CheckFailed, why);
        return false;
    }
    setState(State::Checking);
    const std::string url = manifestUrl();
    std::string body;
    const http::Response r = http::getText(url, body, 256 * 1024, cancel_);
    if (!r.ok) {
        fail(State::CheckFailed, r.status == 404 ? std::string("no art pack published yet (HTTP 404)") : r.error);
        return false;
    }
    formats::ArtPackManifest m;
    std::string err;
    if (!formats::parseArtPackManifest(body, url, m, err)) {
        fail(State::CheckFailed, "bad manifest: " + err);
        return false;
    }
    std::lock_guard l(m_);
    st_.manifest = m;
    st_.haveManifest = true;
    st_.state = State::Checked;
    st_.error.clear();
    rawManifest_ = body;
    SDL_Log("Art pack manifest: %s %s, %s (%s)", m.name.c_str(), m.version.c_str(), formatBytes(m.size).c_str(), url.c_str());
    return true;
}

void ArtPackDownloader::work(bool download) {
    bool have = false;
    {
        std::lock_guard l(m_);
        have = st_.haveManifest;
    }
    if (!download || !have) {
        if (!fetchManifest()) return;
    }
    if (!download) return;
    formats::ArtPackManifest m;
    {
        std::lock_guard l(m_);
        m = st_.manifest;
    }
    if (!m.minEngine.empty() && formats::compareVersions(D2K_VERSION, m.minEngine) < 0) {
        fail(State::Failed, "this art pack needs OpenD2K " + m.minEngine + " or newer");
        return;
    }
    const std::string user = render::ArtPack::userDir();
    if (user.empty()) {
        fail(State::Failed, "no writable app data folder");
        return;
    }
    std::error_code ec;
    const fs::path dl = fs::path(user) / ".download";
    fs::create_directories(dl, ec);
    if (ec) {
        fail(State::Failed, "cannot create " + dl.string() + ": " + ec.message());
        return;
    }
    // Room for the zip and its unpacked copy (PNGs are stored, so about the same again).
    if (const fs::space_info sp = fs::space(dl, ec); !ec && sp.available < m.size * 2 + (64ull << 20)) {
        fail(State::Failed, "not enough free disk space (needs about " + formatBytes(m.size * 2) + ")");
        return;
    }
    // Same name for the same file: an interrupted download of this very zip resumes.
    const std::string part = (dl / (m.name + "-" + m.sha256.substr(0, 16) + ".zip.part")).string();
    if (!downloadZip(part)) return;
    if (!verify(part)) return;
    if (!install(part)) return;
    fs::remove(part, ec);
    fs::remove(dl, ec);  // only when empty
    render::ArtPack::invalidate();
    std::lock_guard l(m_);
    st_.state = State::Done;
    st_.error.clear();
    installedName_ = m.name;
    SDL_Log("Art pack %s %s installed in %s", m.name.c_str(), m.version.c_str(), user.c_str());
}

bool ArtPackDownloader::downloadZip(const std::string& part) {
    formats::ArtPackManifest m;
    {
        std::lock_guard l(m_);
        m = st_.manifest;
        st_.state = State::Downloading;
        st_.error.clear();
        st_.total = m.size;
        st_.done = 0;
        st_.bytesPerSec = 0;
    }
    std::error_code ec;
    std::uint64_t have = fs::exists(part, ec) ? fs::file_size(part, ec) : 0;
    if (ec || have > m.size) {
        fs::remove(part, ec);
        have = 0;
    }
    if (have == m.size) return true;  // complete from an earlier run: verify() decides
    std::string lastError;
    for (std::size_t attempt = 0; attempt < m.urls.size() * 2; ++attempt) {
        const std::string& url = m.urls[attempt % m.urls.size()];
        if (attempt >= m.urls.size()) {  // second round: start over (a server without ranges / a bad part)
            fs::remove(part, ec);
            have = 0;
        }
        std::ofstream out;
        std::uint64_t written = have;
        speedBase_ = have;
        speedT0_ = nowSec();
        {
            std::lock_guard l(m_);
            st_.done = have;
        }
        http::Callbacks cb;
        cb.onStart = [&](int, bool partial, std::int64_t len) {
            if (have > 0 && !partial) written = 0;  // the server sent the whole file: start over
            if (len >= 0 && written + std::uint64_t(len) > m.size) return false;  // not our file
            out.open(fs::path(part), std::ios::binary | (written > 0 ? std::ios::app : std::ios::trunc));
            return bool(out);
        };
        cb.onData = [&](const void* p, std::size_t n) {
            if (written + n > m.size) return false;
            if (!out.write(static_cast<const char*>(p), std::streamsize(n))) return false;
            written += n;
            const double t = nowSec();
            std::lock_guard l(m_);
            st_.done = written;
            if (t - speedT0_ > 0.5) st_.bytesPerSec = double(written - speedBase_) / (t - speedT0_);
            return true;
        };
        http::Request req;
        req.url = url;
        req.resumeFrom = std::int64_t(have);
        SDL_Log("Art pack: downloading %s%s", url.c_str(), have ? (" from byte " + std::to_string(have)).c_str() : "");
        const http::Response r = http::get(req, cb, cancel_);
        out.close();
        if (r.ok && written == m.size) return true;
        if (r.cancelled || cancel_.load()) {
            fail(State::Cancelled, "cancelled");
            return false;
        }
        lastError = r.ok ? "download incomplete (" + formatBytes(written) + " of " + formatBytes(m.size) + ")" : r.error;
        have = fs::exists(part, ec) ? fs::file_size(part, ec) : 0;
        if (ec || have > m.size) have = 0;
        SDL_Log("Art pack: %s failed: %s", url.c_str(), lastError.c_str());
    }
    fail(State::Failed, lastError.empty() ? std::string("download failed") : lastError);
    return false;
}

bool ArtPackDownloader::verify(const std::string& part) {
    formats::ArtPackManifest m;
    {
        std::lock_guard l(m_);
        m = st_.manifest;
        st_.state = State::Verifying;
        st_.done = 0;
        st_.total = m.size;
    }
    std::error_code ec;
    const std::uint64_t size = fs::file_size(part, ec);
    if (ec || size != m.size) {
        fs::remove(part, ec);
        fail(State::Failed, "the download has the wrong size - please retry");
        return false;
    }
    std::ifstream in(fs::path(part), std::ios::binary);
    formats::Sha256 sha;
    std::vector<char> buf(1 << 20);
    std::uint64_t done = 0;
    while (in) {
        if (cancel_.load()) {
            fail(State::Cancelled, "cancelled");
            return false;
        }
        in.read(buf.data(), std::streamsize(buf.size()));
        const std::streamsize n = in.gcount();
        if (n <= 0) break;
        sha.update(buf.data(), std::size_t(n));
        done += std::uint64_t(n);
        std::lock_guard l(m_);
        st_.done = done;
    }
    const std::string got = sha.finishHex();
    in.close();  // Windows cannot delete an open file (the bad part would stay and fail every retry)
    if (done != m.size || got != m.sha256) {
        fs::remove(part, ec);
        fail(State::Failed, "checksum mismatch - the download was damaged, please retry");
        SDL_Log("Art pack: sha256 %s, expected %s", got.c_str(), m.sha256.c_str());
        return false;
    }
    return true;
}

bool ArtPackDownloader::install(const std::string& part) {
    formats::ArtPackManifest m;
    std::string raw;
    {
        std::lock_guard l(m_);
        m = st_.manifest;
        raw = rawManifest_;
        st_.state = State::Installing;
        st_.done = 0;
    }
    // The pack folder inside the zip: <name>/pack.ini, else artpacks/<name>/pack.ini, else the one
    // folder that has a pack.ini.
    formats::zip::Archive a;
    std::string err;
    if (!a.open(part, err)) {
        fail(State::Failed, "cannot read the pack: " + err);
        return false;
    }
    std::string prefix;
    std::vector<std::string> candidates;
    for (const auto& e : a.entries()) {
        const std::string safe = formats::zip::safeRelativePath(e.name);
        if (safe.size() < 9 || safe.compare(safe.size() - 9, 9, "/pack.ini") != 0) continue;
        const std::string dir = safe.substr(0, safe.size() - 9);
        if (dir == m.name || dir == "artpacks/" + m.name) {
            prefix = dir;
            break;
        }
        if (std::count(dir.begin(), dir.end(), '/') <= 1) candidates.push_back(dir);
    }
    if (prefix.empty() && candidates.size() == 1) prefix = candidates[0];
    if (prefix.empty()) {
        fail(State::Failed, "the zip has no " + m.name + "/pack.ini");
        return false;
    }
    const fs::path user(render::ArtPack::userDir());
    const fs::path tmp = user / (".install-" + m.name), dest = user / m.name, old = user / (".old-" + m.name);
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::remove_all(old, ec);
    const formats::zip::ExtractResult x =
        formats::zip::extractAll(part, tmp.string(), prefix, [this](std::uint64_t done, std::uint64_t total) {
            std::lock_guard l(m_);
            st_.done = done;
            st_.total = total;
            return !cancel_.load();
        });
    if (!x.ok) {
        fs::remove_all(tmp, ec);
        fail(x.cancelled ? State::Cancelled : State::Failed, "unpacking failed: " + x.error);
        return false;
    }
    if (!fs::exists(tmp / "pack.ini", ec)) {
        fs::remove_all(tmp, ec);
        fail(State::Failed, "the unpacked pack has no pack.ini");
        return false;
    }
    {
        std::ofstream o(tmp / "installed.ini");
        o << "; written by the OpenD2K art pack download (platform/artpack_download.cpp)\n"
          << "name=" << m.name << "\nversion=" << m.version << "\nsha256=" << m.sha256 << "\nlicence=" << m.licence << "\n";
        if (!raw.empty()) std::ofstream(tmp / "artpack-manifest.json", std::ios::binary) << raw;
    }
    // Swap: the old copy moves aside first and is deleted only after the new one is in place.
    const bool hadOld = fs::exists(dest, ec);
    if (hadOld) {
        fs::rename(dest, old, ec);
        if (ec) {
            fs::remove_all(tmp, ec);
            fail(State::Failed, "cannot replace the installed pack (in use?): " + ec.message());
            return false;
        }
    }
    fs::rename(tmp, dest, ec);
    if (ec) {
        const std::string msg = ec.message();
        if (hadOld) fs::rename(old, dest, ec);
        fs::remove_all(tmp, ec);
        fail(State::Failed, "cannot install the pack: " + msg);
        return false;
    }
    fs::remove_all(old, ec);
    SDL_Log("Art pack: unpacked %zu files (%s) into %s", x.files, formatBytes(x.bytes).c_str(), dest.string().c_str());
    return true;
}

InstalledArtPack ArtPackDownloader::installed(const std::string& name) {
    InstalledArtPack r;
    r.name = name;
    for (const auto& d : render::ArtPack::searchDirs()) {
        const fs::path root = fs::path(d) / name;
        std::error_code ec;
        if (!fs::exists(root / "pack.ini", ec)) continue;
        r.installed = true;
        r.root = root.string();
        r.version = iniValue(root / "installed.ini", "version");
        r.sha256 = iniValue(root / "installed.ini", "sha256");
        if (r.version.empty()) r.version = iniValue(root / "pack.ini", "version");
        r.hasModels = fs::is_directory(root / "models", ec);
        break;
    }
    return r;
}

bool ArtPackDownloader::updateAvailable() const {
    const Status s = status();
    if (!s.haveManifest) return false;
    return isUpdate(s.manifest, installed(s.manifest.name));
}

bool ArtPackDownloader::isUpdate(const formats::ArtPackManifest& m, const InstalledArtPack& i) {
    if (!i.installed || i.version.empty() || m.name != i.name) return false;
    const int c = formats::compareVersions(m.version, i.version);
    if (c != 0) return c > 0;
    return !i.sha256.empty() && !m.sha256.empty() && i.sha256 != m.sha256;  // republished as the same version
}

int ArtPackDownloader::runBlocking(bool doInstall, const std::function<void(const std::string&)>& log) {
    log("Manifest: " + manifestUrl() + " (HTTP backend " + http::backendName() + ")");
    start(false);
    if (worker_.joinable()) worker_.join();
    Status s = status();
    if (s.state != State::Checked) {
        log("Check failed: " + s.error);
        return 1;
    }
    const InstalledArtPack inst = installed(s.manifest.name);
    log("Pack " + s.manifest.name + " " + s.manifest.version + ", " + formatBytes(s.manifest.size) + ", sha256 " +
        s.manifest.sha256 + "; installed: " + (inst.installed ? (inst.version.empty() ? "yes (unknown version)" : inst.version) + " at " + inst.root : "no"));
    if (!doInstall) return 0;
    start(true);
    State last = State::Idle;
    int lastPct = -1;
    while (busy()) {
        SDL_Delay(200);
        s = status();
        const int pct = s.total ? int(s.done * 100 / s.total) : 0;
        if (s.state != last || pct / 10 != lastPct / 10) {
            static const char* const kNames[] = {"idle", "checking", "checked", "check failed", "downloading", "verifying",
                                                 "installing", "done", "failed", "cancelled"};
            log(std::string("  ") + kNames[int(s.state)] + " " + std::to_string(pct) + "% (" + formatBytes(s.done) + ")");
            last = s.state;
            lastPct = pct;
        }
    }
    if (worker_.joinable()) worker_.join();
    s = status();
    if (s.state != State::Done) {
        log("Install failed: " + s.error);
        return 1;
    }
    log("Installed " + s.manifest.name + " " + s.manifest.version + " in " + render::ArtPack::userDir());
    return 0;
}

int httpGetToFile(const std::string& url, const std::string& file, const std::function<void(const std::string&)>& log) {
    std::string why;
    log(std::string("HTTP backend: ") + http::backendName() + ", User-Agent " + http::userAgent());
    if (!http::available(&why)) {
        log("not available: " + why);
        return 1;
    }
    std::ofstream out(fs::path(file), std::ios::binary | std::ios::trunc);
    if (!out) {
        log("cannot write " + file);
        return 1;
    }
    formats::Sha256 sha;
    std::uint64_t n = 0;
    std::atomic<bool> cancel{false};
    http::Callbacks cb;
    cb.onStart = [&](int status, bool partial, std::int64_t len) {
        log("status " + std::to_string(status) + (partial ? " (partial)" : "") + ", length " + std::to_string(len));
        return true;
    };
    cb.onData = [&](const void* p, std::size_t len) {
        sha.update(p, len);
        n += len;
        return bool(out.write(static_cast<const char*>(p), std::streamsize(len)));
    };
    http::Request req;
    req.url = url;
    const double t0 = nowSec();
    const http::Response r = http::get(req, cb, cancel);
    log("result: " + std::string(r.ok ? "ok" : "FAILED " + r.error) + ", HTTP " + std::to_string(r.status) + ", " +
        std::to_string(n) + " bytes in " + std::to_string(nowSec() - t0) + " s, sha256 " + sha.finishHex());
    return r.ok ? 0 : 1;
}

}  // namespace d2k::platform
