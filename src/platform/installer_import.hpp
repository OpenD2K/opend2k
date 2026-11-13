#pragma once
// Building the game data folder from the Gruntmods Edition installer (.exe) instead of an
// installed game: the installer is unpacked with formats/nsis.* into
//   <pref path>/GameData/<installer name>/Dune 2000/...   (SDL_GetPrefPath("d2k-mac", "Dune 2000"))
// and the app then uses ".../Dune 2000/data". A finished copy carries a ".d2k-extracted" marker
// and is reused when the same installer is chosen again. The Gruntmods cutscene installers
// ("Dune 2000 <Language> Cutscene Installer.exe", movies only) are unpacked into the
// app-managed movies folder appMoviesDir() (<pref path>/Movies/*.VQA), whatever the data source.

#include "formats/nsis.hpp"
#include "platform/data_dir.hpp"  // gameDataRoot()

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace d2k::platform {

struct ImportResult {
    bool ok = false;
    bool reused = false;      // an existing complete copy was used
    bool cancelled = false;
    bool moviesOnly = false;  // a cutscene installer was unpacked into appMoviesDir()
    std::string dataDir;      // ".../Dune 2000/data" on success (movies only: currentDataDir)
    std::string moviesDir;    // movies only: where the VQA files went
    std::string error;
    std::size_t files = 0;
    std::uint64_t bytes = 0;
    double seconds = 0;
};

// Synchronous import. `currentDataDir` is only passed through for movies-only installers.
// Never touches other folders.
ImportResult importInstaller(const std::string& installer, const std::string& currentDataDir,
                             const std::string& rootOverride = {}, const formats::nsis::ProgressFn& progress = {});

// The same on a worker thread, for the data-folder page's progress screen.
class InstallerImportTask {
public:
    InstallerImportTask(std::string installer, std::string currentDataDir, std::string rootOverride = {});
    ~InstallerImportTask();  // cancels and joins
    InstallerImportTask(const InstallerImportTask&) = delete;
    InstallerImportTask& operator=(const InstallerImportTask&) = delete;

    void cancel() { cancel_ = true; }
    bool finished() const { return finished_; }
    float fraction() const { return fraction_; }
    std::string currentFile() const;
    std::size_t filesDone() const { return filesDone_; }
    std::size_t filesTotal() const { return filesTotal_; }
    const ImportResult& result() const { return result_; }  // valid once finished()
    const std::string& installer() const { return installer_; }

private:
    std::string installer_;
    std::thread thread_;
    std::atomic<bool> cancel_{false}, finished_{false};
    std::atomic<float> fraction_{0};
    std::atomic<std::size_t> filesDone_{0}, filesTotal_{0};
    mutable std::mutex mutex_;
    std::string current_;
    ImportResult result_;
};

}  // namespace d2k::platform
