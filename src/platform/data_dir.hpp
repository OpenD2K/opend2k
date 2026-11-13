#pragma once
// Finding the original "Dune 2000/data" folder so the app can be started by double-click.

#include <string>
#include <vector>

namespace d2k::platform {

// True if `dir` looks like the game's data folder (DATA.R16 and bin/Templates.bin present).
bool isDataDir(const std::string& dir);

// Accepts either the data folder itself or a folder containing it ("Dune 2000" or its parent);
// returns the data folder, or empty if none of them is valid.
std::string normalizeDataDir(const std::string& dir);

// Where copies built from the Gruntmods installer live (platform/installer_import.hpp):
// <SDL pref path>/GameData, or `overrideRoot` when given (--game-data-root, tests).
std::string gameDataRoot(const std::string& overrideRoot = {});

// The app-managed cutscene folder (platform/movies.hpp): <SDL pref path>/Movies
// (~/Library/Application Support/d2k-mac/Dune 2000/Movies), or <overrideRoot>/../Movies when a
// GameData root override is given (--game-data-root, tests). Empty if there is no pref path.
std::string appMoviesDir(const std::string& overrideRoot = {});

// A portable build's read-only cutscene folder (portable/Movies, platform/user_data.hpp); "" otherwise.
// Searched after appMoviesDir().
std::string portableMoviesDir();

// iPad (docs/ios.md): the app's Documents folder, which the Files app shows as
// "On My iPad > OpenD2K" (UIFileSharingEnabled). Empty on other platforms.
std::string documentsDir();

// What the player put into documentsDir(): game folders ("Dune 2000" or a data folder, also one
// level down), installers (*.exe: the Gruntmods Edition setup and its cutscene installers) and
// PS1 disc images (*.cue, else *.bin). Full paths, sorted.
// Loose cutscenes count too: movieDirs = Documents and its folders (two levels down) that hold
// *.VQA files (a "movies" folder, or the files dropped straight in), movieFiles = how many.
struct DocumentsScan {
    std::vector<std::string> dataDirs, installers, ps1Images, movieDirs;
    std::size_t movieFiles = 0;
};
DocumentsScan scanDocuments();
// True for a Gruntmods cutscene installer ("Dune 2000 English Cutscene Installer.exe"), by name.
bool isCutsceneInstallerName(const std::string& path);
// scanDocuments().movieDirs, rescanned at most every 3 seconds (platform/movies.hpp searches them).
std::vector<std::string> documentsMovieDirs();

// Usual places of an installed game: Windows "%ProgramFiles(x86)%\Dune 2000", "%ProgramFiles%\Dune 2000",
// "C:\Games\Dune 2000", "C:\Westwood\Dune2000", "C:\Dune 2000"; Linux ~/Games/Dune 2000 and the Wine
// prefix's Program Files / Games. Empty on macOS / iPadOS. Candidates only (they may not exist).
std::vector<std::string> usualInstallDirs();

// Search order: `cliDir` (--data), `savedDir` (settings), next to the executable / the .app
// bundle (and up to three parent folders), "game/Dune 2000/data" relative to the current
// directory (the repository layout), usual install folders (usualInstallDirs), the iPad's Documents
// folder, then the newest complete copy under gameDataRoot(). Returns empty if nothing was found. `tried` (optional)
// receives the candidates in order, for diagnostics.
std::string discoverDataDir(const std::string& cliDir, const std::string& savedDir,
                            std::vector<std::string>* tried = nullptr);

}  // namespace d2k::platform
