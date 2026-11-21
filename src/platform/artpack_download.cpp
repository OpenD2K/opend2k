#include "platform/artpack_download.hpp"
#include "formats/utf8_bom.hpp"

#include "formats/sha256.hpp"
#include "formats/zip_reader.hpp"
#include "platform/app_update.hpp"
#include "platform/http_client.hpp"
#include "platform/language.hpp"
#include "formats/language.hpp"
#include "render/art_pack.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
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
    formats::skipUtf8Bom(in);
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

namespace {
// The AI voice folder holds a set that this download did not write (the player generated it): never replaced.
bool foreignVoiceSet(const formats::ArtPackManifest& m, const std::string& dest) {
    std::error_code ec;
    return m.install == formats::kInstallAiVoices && !dest.empty() && fs::exists(dest, ec) &&
           !fs::exists(fs::path(dest) / "installed.ini", ec);
}
const char* const kForeignVoiceSet = "a voice set that was not downloaded is already in ";
}  // namespace

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
void ArtPackDownloader::download() { start(true, {std::string()}); }
void ArtPackDownloader::download(const std::vector<std::string>& names) {
    if (!names.empty()) start(true, names);
}

void ArtPackDownloader::cancel() {
    cancel_ = true;
    std::lock_guard l(m_);
    queue_.clear();
}

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
    st_.startedMs = 0;
    st_.checkedAt = s == State::Checking || s == State::Idle ? 0 : previewCheckedAt();
    autoChecked_ = true;
}

void ArtPackDownloader::start(bool download, const std::vector<std::string>& names) {
    {
        std::lock_guard l(m_);
        if (download)
            for (const std::string& n : names)
                if (std::find(queue_.begin(), queue_.end(), n) == queue_.end()) queue_.push_back(n);
        // Busy (an update check that the Options page started, or a download): the running worker
        // takes the queue up when it is done.
        if (running_.exchange(true)) return;
    }
    if (worker_.joinable()) worker_.join();
    cancel_ = false;
    worker_ = std::thread([this, download] {
        if (!download) work({});
        for (;;) {
            std::vector<std::string> next;
            {
                std::lock_guard l(m_);
                if (cancel_.load()) queue_.clear();
                next.swap(queue_);
                if (next.empty()) {
                    running_ = false;  // (under the lock: start() sees either the queue taken or the worker gone)
                    return;
                }
            }
            work(next);
        }
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
    if (s == State::CheckFailed) st_.checkedAt = std::int64_t(std::time(nullptr));
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
    {
        std::lock_guard l(m_);
        st_.startedMs = SDL_GetTicks();
    }
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
    st_.checkedAt = std::int64_t(std::time(nullptr));
    rawManifest_ = body;
    SDL_Log("Art pack manifest: %s %s, %s (%s)", m.name.c_str(), m.version.c_str(), formatBytes(m.size).c_str(), url.c_str());
    return true;
}

void ArtPackDownloader::work(const std::vector<std::string>& names) {
    bool have = false;
    {
        std::lock_guard l(m_);
        have = st_.haveManifest;
    }
    if (names.empty() || !have) {
        if (!fetchManifest()) return;
    }
    if (names.empty()) return;
    formats::ArtPackManifest m;
    {
        std::lock_guard l(m_);
        m = st_.manifest;
    }
    // In the manifest's order (the main pack first: the Reimagined models overlay its models).
    std::vector<const formats::ArtPackManifest*> items;
    // kArtPack3dSet ("@3d"): what the 3D Reimagined look still lacks, known only now that the manifest is in.
    const bool set3d = std::find(names.begin(), names.end(), std::string(kArtPack3dSet)) != names.end();
    const std::vector<std::string> missing3d = set3d ? missingFor3d(m) : std::vector<std::string>{};
    auto wanted = [&](const std::string& n) {
        return std::find(names.begin(), names.end(), n) != names.end() ||
               std::find(missing3d.begin(), missing3d.end(), n) != missing3d.end();
    };
    if (set3d && missing3d.empty() && names.size() == 1) {  // everything is there already
        std::lock_guard l(m_);
        st_.state = State::Done;
        st_.error.clear();
        return;
    }
    if (wanted("") || wanted(m.name)) items.push_back(&m);
    for (const formats::ArtPackManifest& e : m.extras)
        if (wanted(e.name)) items.push_back(&e);
    if (items.empty()) {
        fail(State::Failed, "the art pack list has changed - press Check now");
        return;
    }
    std::string mainInstalled;
    std::vector<std::string> done;
    for (std::size_t i = 0; i < items.size(); ++i) {
        {
            std::lock_guard l(m_);
            st_.current = items[i]->name;
            st_.currentTitle = items[i]->title;
            st_.queuePos = int(i) + 1;
            st_.queueLen = int(items.size());
        }
        if (!installItem(*items[i])) return;  // (failed / cancelled: the state says why; the rest is dropped)
        if (!items[i]->isExtra) mainInstalled = items[i]->name;
        done.push_back(items[i]->name);
    }
    std::lock_guard l(m_);
    st_.state = State::Done;
    st_.error.clear();
    st_.current.clear();
    st_.currentTitle.clear();
    st_.installedNames = done;
    if (!mainInstalled.empty()) installedName_ = mainInstalled;
}

bool ArtPackDownloader::installItem(const formats::ArtPackManifest& m) {
    if (!m.minEngine.empty() && formats::compareVersions(D2K_VERSION, m.minEngine) < 0) {
        fail(State::Failed, "this art pack needs OpenD2K " + m.minEngine + " or newer");
        return false;
    }
    const std::string user = render::ArtPack::userDir();
    if (user.empty()) {
        fail(State::Failed, "no writable app data folder");
        return false;
    }
    if (const std::string dest = installDir(m); foreignVoiceSet(m, dest)) {  // (checked before downloading too)
        fail(State::Failed, kForeignVoiceSet + dest + " - move it away first");
        return false;
    }
    std::error_code ec;
    const fs::path dl = fs::path(user) / ".download";
    fs::create_directories(dl, ec);
    if (ec) {
        fail(State::Failed, "cannot create " + dl.string() + ": " + ec.message());
        return false;
    }
    // Room for the zip and its unpacked copy (PNGs are stored, so about the same again).
    if (const fs::space_info sp = fs::space(dl, ec); !ec && sp.available < m.size * 2 + (64ull << 20)) {
        fail(State::Failed, "not enough free disk space (needs about " + formatBytes(m.size * 2) + ")");
        return false;
    }
    // Same name for the same file: an interrupted download of this very zip resumes.
    const std::string part = (dl / (m.name + "-" + m.sha256.substr(0, 16) + ".zip.part")).string();
    if (!downloadZip(m, part)) return false;
    if (!verify(m, part)) return false;
    if (!install(m, part)) return false;
    fs::remove(part, ec);
    fs::remove(dl, ec);  // only when empty
    render::ArtPack::invalidate();
    if (m.install == formats::kInstallAiVoices) noteAiVoicesChanged();
    SDL_Log("Art pack %s %s installed in %s", m.name.c_str(), m.version.c_str(), installDir(m).c_str());
    return true;
}

bool ArtPackDownloader::downloadZip(const formats::ArtPackManifest& m, const std::string& part) {
    {
        std::lock_guard l(m_);
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

bool ArtPackDownloader::verify(const formats::ArtPackManifest& m, const std::string& part) {
    {
        std::lock_guard l(m_);
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

bool ArtPackDownloader::install(const formats::ArtPackManifest& m, const std::string& part) {
    std::string raw;
    {
        std::lock_guard l(m_);
        raw = rawManifest_;
        st_.state = State::Installing;
        st_.done = 0;
    }
    // The pack folder inside the zip: <name>/pack.ini, else artpacks/<name>/pack.ini, else the one
    // folder that has a pack.ini. An extra (no pack.ini): the folder <name>/ (or artpacks/<name>/).
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
    if (prefix.empty() && candidates.size() == 1 && !m.isExtra) prefix = candidates[0];
    if (prefix.empty() && m.isExtra)
        for (const auto& e : a.entries()) {
            const std::string safe = formats::zip::safeRelativePath(e.name);
            for (const std::string& dir : {m.name, "artpacks/" + m.name})
                if (safe.size() > dir.size() + 1 && safe.compare(0, dir.size() + 1, dir + "/") == 0) prefix = dir;
            if (!prefix.empty()) break;
        }
    if (prefix.empty()) {
        fail(State::Failed, "the zip has no " + m.name + (m.isExtra ? "/ folder" : "/pack.ini"));
        return false;
    }
    const fs::path dest(installDir(m));
    if (dest.empty()) {
        fail(State::Failed, "no writable folder for " + m.name);
        return false;
    }
    const fs::path user = dest.parent_path();
    const fs::path tmp = user / (".install-" + m.name), old = user / (".old-" + m.name);
    std::error_code ec;
    fs::create_directories(user, ec);
    // A voice set the player generated (no installed.ini: not from this download) stays untouched.
    if (foreignVoiceSet(m, dest.string())) {
        fail(State::Failed, kForeignVoiceSet + dest.string() + " - move it away first");
        return false;
    }
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
    if (!m.isExtra && !fs::exists(tmp / "pack.ini", ec)) {
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
        if (!fs::exists(root / "pack.ini", ec)) {
            // An extra (no pack.ini): installed.ini (the download) or any content (a copy by hand).
            if (name == kArtPackName || !fs::is_directory(root, ec)) continue;
            if (!fs::exists(root / "installed.ini", ec) && fs::directory_iterator(root, ec) == fs::directory_iterator()) continue;
        }
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

std::string ArtPackDownloader::installDir(const formats::ArtPackManifest& item) {
    if (item.install == formats::kInstallAiVoices) return defaultAiVoicesDir();
    const std::string user = render::ArtPack::userDir();
    return user.empty() ? std::string() : (fs::path(user) / item.name).string();
}

InstalledArtPack ArtPackDownloader::installed(const formats::ArtPackManifest& item) {
    if (item.install != formats::kInstallAiVoices) return installed(item.name);
    // The AI voice folder: the download's installed.ini, or a set the player generated (version unknown).
    InstalledArtPack r;
    r.name = item.name;
    const std::string dir = defaultAiVoicesDir();
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return r;
    const fs::path ini = fs::path(dir) / "installed.ini";
    if (!fs::exists(ini, ec) && formats::lang::findAiVoices(dir).empty()) return r;
    r.installed = true;
    r.root = dir;
    r.version = iniValue(ini, "version");
    r.sha256 = iniValue(ini, "sha256");
    return r;
}

bool ArtPackDownloader::needed(const formats::ArtPackManifest& item) {
    const InstalledArtPack i = installed(item);
    return !i.installed || isUpdate(item, i);
}

std::vector<std::string> ArtPackDownloader::missingFor3d(const formats::ArtPackManifest& m) {
    std::vector<std::string> r;
    if (!installed(m.name).installed) r.push_back(m.name);
    for (const formats::ArtPackManifest& e : m.extras)
        if (e.for3d && needed(e)) r.push_back(e.name);
    return r;
}

std::uint64_t ArtPackDownloader::sizeOf(const formats::ArtPackManifest& m, const std::vector<std::string>& names) {
    std::uint64_t t = 0;
    for (const std::string& n : names)
        if (const formats::ArtPackManifest* i = m.item(n.empty() ? m.name : n)) t += i->size;
    return t;
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
    // --artpack-install: the main pack and every extra (the full 3D Reimagined set).
    std::vector<std::string> names{s.manifest.name};
    for (const formats::ArtPackManifest& e : s.manifest.extras) {
        const InstalledArtPack ei = installed(e);
        log("  extra " + e.name + " " + e.version + ", " + formatBytes(e.size) + (e.for3d ? ", for 3D" : "") + ", " + e.licence +
            "; installed: " + (ei.installed ? (ei.version.empty() ? "yes (unknown version)" : ei.version) + " at " + ei.root : "no"));
        if (foreignVoiceSet(e, installDir(e))) {
            log(std::string("    (skipped: ") + kForeignVoiceSet + installDir(e) + ")");
            continue;
        }
        names.push_back(e.name);
    }
    if (!doInstall) return 0;
    download(names);
    State last = State::Idle;
    std::string lastItem;
    int lastPct = -1;
    while (busy()) {
        SDL_Delay(200);
        s = status();
        const int pct = s.total ? int(s.done * 100 / s.total) : 0;
        if (s.state != last || pct / 10 != lastPct / 10 || s.current != lastItem) {
            if (s.current != lastItem && !s.current.empty())
                log("  [" + std::to_string(s.queuePos) + "/" + std::to_string(s.queueLen) + "] " + s.current);
            lastItem = s.current;
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
    log("Installed " + std::to_string(names.size()) + " pack(s) (" + s.manifest.name + " " + s.manifest.version + (names.size() > 1 ? " + extras" : "") +
        ") in " + render::ArtPack::userDir());
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
