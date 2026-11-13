#include "platform/installer_import.hpp"

#include "platform/data_dir.hpp"
#include "platform/user_data.hpp"

#include <SDL3/SDL.h>

#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <vector>
#include <sstream>

namespace d2k::platform {

namespace fs = std::filesystem;
namespace nsis = formats::nsis;

namespace {

constexpr const char* kMarker = ".d2k-extracted";

bool startsWithNoCase(const std::string& s, const char* prefix) {
    for (std::size_t i = 0; prefix[i]; ++i)
        if (i >= s.size() || std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    return true;
}

// Folder name for a copy: the installer's name without odd characters.
std::string copyName(const std::string& installer) {
    std::string out;
    for (char c : fs::path(installer).stem().string()) {
        const unsigned char u = static_cast<unsigned char>(c);
        out += (std::isalnum(u) || c == ' ' || c == '.' || c == '-' || c == '_' || c == '(' || c == ')' || u >= 0x80) ? c : '_';
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    while (!out.empty() && (out.front() == ' ' || out.front() == '.')) out.erase(out.begin());
    return out.empty() ? "Gruntmods" : out;
}

std::string markerSignature(const std::string& installer) {
    std::error_code ec;
    std::ostringstream s;
    s << "installer=" << fs::path(installer).filename().string() << "\nsize=" << fs::file_size(installer, ec) << "\n";
    return s.str();
}

bool readsAs(const fs::path& file, const std::string& prefix) {
    std::ifstream in(file, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return content.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

ImportResult importInstaller(const std::string& installer, const std::string& currentDataDir,
                             const std::string& rootOverride, const nsis::ProgressFn& progress) {
    ImportResult r;
    const auto t0 = std::chrono::steady_clock::now();
    auto finish = [&]() -> ImportResult {
        r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return r;
    };
    nsis::Archive a;
    try {
        a = nsis::open(installer);
    } catch (const std::exception& e) {
        r.error = std::string("Not a usable installer: ") + e.what();
        return finish();
    }
    SDL_Log("Installer %s: %s", installer.c_str(), a.describe().c_str());
    bool hasData = false, hasMovies = false;
    for (const nsis::File& f : a.files) {
        hasData = hasData || startsWithNoCase(f.path, "Dune 2000/data/");
        hasMovies = hasMovies || startsWithNoCase(f.path, "Dune 2000/movies/");
    }
    const std::string rootStr = gameDataRoot(rootOverride);
    if (rootStr.empty()) {
        r.error = "No folder for application data (SDL_GetPrefPath failed)";
        return finish();
    }
    const fs::path root(rootStr);
    std::error_code ec;

    if (hasData) {
        const std::string name = copyName(installer);
        const fs::path dest = root / name, partial = root / (name + ".partial");
        const std::string sig = markerSignature(installer);
        if (fs::exists(dest / kMarker, ec) && readsAs(dest / kMarker, sig)) {
            r.dataDir = normalizeDataDir((dest / "Dune 2000").string());
            if (!r.dataDir.empty()) {
                r.ok = r.reused = true;
                return finish();
            }
        }
        fs::remove_all(partial, ec);
        nsis::ExtractOptions opt;
        opt.prefix = "Dune 2000/";  // the game; the D2K+ Windows editors are left out
        const nsis::ExtractResult x = nsis::extract(a, partial.string(), opt, progress);
        r.files = x.files;
        r.bytes = x.bytes;
        if (!x.ok) {
            fs::remove_all(partial, ec);
            r.cancelled = x.cancelled;
            r.error = x.cancelled ? "Cancelled" : x.error;
            return finish();
        }
        fs::remove_all(dest, ec);
        fs::rename(partial, dest, ec);
        if (ec) {
            r.error = "Cannot move the unpacked files into place: " + ec.message();
            return finish();
        }
        std::ofstream(dest / kMarker, std::ios::binary) << sig << "files=" << x.files << "\n" << a.describe() << "\n";
        r.dataDir = normalizeDataDir((dest / "Dune 2000").string());
        if (r.dataDir.empty()) {
            r.error = "The installer did not contain a complete Dune 2000 data folder";
            return finish();
        }
        r.ok = true;
        return finish();
    }

    if (hasMovies) {
        // A cutscene installer: its movies/ go into the app-managed movies folder
        // (appMoviesDir()), whichever way the game data was provided.
        r.moviesOnly = true;
        const std::string moviesDir = appMoviesDir(rootOverride);
        if (moviesDir.empty()) {
            r.error = "No folder for application data (SDL_GetPrefPath failed)";
            return finish();
        }
        nsis::ExtractOptions opt;
        opt.prefix = "Dune 2000/movies/";
        opt.stripPrefix = true;
        const nsis::ExtractResult x = nsis::extract(a, moviesDir, opt, progress);
        r.files = x.files;
        r.bytes = x.bytes;
        if (!x.ok) {
            r.cancelled = x.cancelled;
            r.error = x.cancelled ? "Cancelled (some movies may be incomplete)" : x.error;
            return finish();
        }
        r.ok = true;
        r.dataDir = currentDataDir;
        r.moviesDir = moviesDir;
        return finish();
    }
    r.error = "This installer contains no Dune 2000 data";
    return finish();
}

namespace {

// Android: SDL's file picker returns a content:// URI (the Storage Access Framework). Its file name:
// the URI's last path segment, percent-decoded ("primary:Download/Dune 2000 ... .exe" -> the part after
// the last '/' or ':'), "installer.exe" when it has no .exe name (some providers use numeric ids).
std::string contentUriFileName(const std::string& uri) {
    std::string dec;
    for (std::size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size() && std::isxdigit((unsigned char)uri[i + 1]) && std::isxdigit((unsigned char)uri[i + 2])) {
            dec += char(std::stoi(uri.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            dec += uri[i];
        }
    }
    const std::size_t cut = dec.find_last_of("/:");
    std::string name = cut == std::string::npos ? dec : dec.substr(cut + 1);
    for (char& c : name)
        if (c == '\\' || c == '?' || c == '*' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    if (name.size() < 5 || !startsWithNoCase(name.substr(name.size() - 4), ".exe")) name = "installer.exe";
    return name;
}

// Copies `uri` (anything SDL_IOFromFile opens: a path or an Android content:// URI) to `dest`.
bool copyFromUri(const std::string& uri, const std::string& dest, std::string& error, const std::function<bool(double)>& progress) {
    SDL_IOStream* in = SDL_IOFromFile(uri.c_str(), "rb");
    if (!in) {
        error = std::string("Cannot open the chosen file: ") + SDL_GetError();
        return false;
    }
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out) {
        SDL_CloseIO(in);
        error = "Cannot write " + dest;
        return false;
    }
    const Sint64 total = SDL_GetIOSize(in);
    std::vector<char> buf(1 << 20);
    Sint64 done = 0;
    bool ok = true;
    for (;;) {
        const size_t n = SDL_ReadIO(in, buf.data(), buf.size());
        if (n == 0) {
            if (SDL_GetIOStatus(in) != SDL_IO_STATUS_EOF) {
                error = std::string("Read error: ") + SDL_GetError();
                ok = false;
            }
            break;
        }
        out.write(buf.data(), std::streamsize(n));
        if (!out) {
            error = "Write error (storage full?)";
            ok = false;
            break;
        }
        done += Sint64(n);
        if (progress && !progress(total > 0 ? double(done) / double(total) : 0.0)) {
            error = "Cancelled";
            ok = false;
            break;
        }
    }
    SDL_CloseIO(in);
    out.close();
    if (!ok) {
        std::error_code ec;
        fs::remove(dest, ec);
    }
    return ok;
}

}  // namespace

InstallerImportTask::InstallerImportTask(std::string installer, std::string currentDataDir, std::string rootOverride)
    : installer_(std::move(installer)) {
    // A content:// URI (Android's file picker) is copied into the app's folder first, then unpacked
    // from there like a file put there by hand; the copy is deleted afterwards.
    std::string source;
    if (startsWithNoCase(installer_, "content://")) {
        source = installer_;
        const std::string dir = documentsDir();
        installer_ = (fs::path(dir.empty() ? prefPath() : dir) / contentUriFileName(source)).string();
    }
    thread_ = std::thread([this, source, cur = std::move(currentDataDir), root = std::move(rootOverride)] {
        if (!source.empty()) {
            {
                std::lock_guard lock(mutex_);
                current_ = "Copying " + fs::path(installer_).filename().string();
            }
            std::string err;
            if (!copyFromUri(source, installer_, err, [this](double f) {
                    fraction_ = float(f);
                    return !cancel_.load();
                })) {
                result_.error = err;
                finished_ = true;
                return;
            }
            fraction_ = 0.f;
        }
        result_ = importInstaller(installer_, cur, root, [this](const nsis::Progress& p) {
            fraction_ = p.inputTotal ? float(double(p.inputDone) / double(p.inputTotal)) : 0.f;
            filesDone_ = p.filesDone;
            filesTotal_ = p.filesTotal;
            {
                std::lock_guard lock(mutex_);
                current_ = p.current;
            }
            return !cancel_.load();
        });
        if (!source.empty()) {
            std::error_code ec;
            fs::remove(installer_, ec);  // our copy of the picked file
        }
        finished_ = true;
    });
}

InstallerImportTask::~InstallerImportTask() {
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
}

std::string InstallerImportTask::currentFile() const {
    std::lock_guard lock(mutex_);
    return current_;
}

}  // namespace d2k::platform
