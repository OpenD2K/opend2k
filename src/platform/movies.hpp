#pragma once
// The cutscenes: which movie the original plays where, where the files are, and the player.
//
// Names: the front-end pages hold `MOVIE <name>` widgets (Intro1.uil "mve_intro1", AMis3.uil
// "mve_briefing_atreides3", lost.uil "mve_lost", ...). CMovie (FUN_0048d730) looks the name up
// in Text.UIB ("mve_briefing_atreides3" -> "a_br03_e"; "mve_lost" gets the house letter of
// MySideID first: "mve_lostA" -> "a_lose_e", the MOVIE widget builder FUN_0043bfc0) and opens
// "<MoviesResourcePath>\<file>.VQA" (format "%s%s.VQA" 0x4e904c). re/notes/campaign-flow.md §7.
//
// Sources, first match wins (0-byte placeholder files are skipped):
//   1. <data>/../movies/<file>.VQA         (an installed game with the cutscene pack)
//   2. appMoviesDir()/<file>.VQA           (~/Library/Application Support/d2k-mac/Dune 2000/Movies,
//                                           filled by importing a Gruntmods cutscene installer)
//   3. iPad: the app's Documents and its folders holding *.VQA (documentsMovieDirs(): files put
//      there with the Files app / Finder)
//   4. the PS1 disc image from the settings (formats/ps1_str.hpp), for the movies it has.
// The stem gets the cutscene language's letter (a_br03_f for French); the language's own folders
// (the Russian translation's MOVIES) are searched first, and Русский (PS1) plays the disc's movies
// before any PC file. A different language letter (English first) is accepted when the asked one
// is missing.

#include "formats/ps1_disc.hpp"
#include "formats/uib.hpp"
#include "platform/movie_player.hpp"

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <vector>

namespace d2k::platform {

class Audio;
struct Settings;

// The cutscenes are not in the Gruntmods Edition game installer: they are separate free installers
// ("Dune_2000_Gruntmods_Edition_Cutscenes.exe" = English, "Dune_Gruntmods_Edition_<French|German|
// Italian|Spanish>_Cutscenes.exe") listed on this page (checked 2026-11): one "Download Dune 2000:
// Gruntmods Edition <Language> Cutscenes" heading per language, each with a "Download <Language>
// Cutscenes (Downloaded Size ... MB)" button under it (English first, also as a torrent); the page
// opens on a note about the game launcher's button. It is linked as "Cutscenes and Extra Content"
// in the site's menu (gruntmods.com/dune_2000_gruntmods_edition/ itself has no cutscene button).
inline constexpr const char* kCutsceneDownloadPage = "https://gruntmods.com/cutscenes-and-extra-content";

class Movies {
public:
    Movies(SDL_Renderer* r, Settings& settings);
    ~Movies();
    void setAudio(Audio* a) { audio_ = a; }
    void setData(const std::string& dataDir);
    void setMoviesDirOverride(const std::string& dir) { appDirOverride_ = dir; }
    // --ps1: a disc image for this run instead of Settings::ps1Image.
    void setPs1Override(const std::string& image) { ps1Override_ = image; }
    // The PS1 disc image in use ("" = none) and whether it opened as a Dune 2000 disc.
    std::string ps1Image() const;
    bool ps1Ok() const;
    // Checks a disc image: "" when it is a usable Dune 2000 PS1 disc, else the reason.
    static std::string checkPs1Image(const std::string& image);
    // PC file stem -> PS1 STR entry ("a_br03_e" -> "A_BR03.STR", "a_mntg_e" -> "mntg_atr.str").
    static std::string ps1EntryFor(const std::string& stem);

    // UIL movie name -> file stem ("a_br03_e"); "" when unknown. `house` 0..2 for "mve_lost".
    // The stem carries the cutscene language's letter (platform/language.hpp).
    std::string stemFor(const std::string& name, int house) const;
    // Where a stem can be played from: a VQA path, "ps1:<entry>" or "".
    std::string locate(const std::string& stem) const;
    bool available(const std::string& name, int house = 0) const;

    // Starts the movie `name` when cutscenes are on (or `always`) and a source exists.
    // False: nothing plays (the caller goes on at once, like the original after a movie).
    bool play(const std::string& name, int house = 0, bool always = false);
    // Plays a file (VQA path) directly; false when it cannot be opened.
    bool playFile(const std::string& path);
    // Plays `name` after the current movie (and the ones queued before it).
    void queue(const std::string& name, int house = 0) { queue_.push_back({name, house, {}}); }
    bool active() const { return player_.active() || !queue_.empty(); }
    void stop() {
        queue_.clear();
        player_.stop();
    }
    void handleEvent(const SDL_Event& e) { player_.handleEvent(e); }
    void render();

    // The movies offered on the Movies page, in story order.
    struct Entry {
        std::string name;  // UIL movie name
        int house = 0;     // for "mve_lost"
        std::string title;
    };
    static const std::vector<Entry>& gallery();

    // Folders searched for VQA files (for the UI).
    std::vector<std::string> searchFolders() const;

private:
    std::string baseStemFor(const std::string& name, int house) const;  // as Text.UIB names it
    std::unique_ptr<MovieDecoder> open(const std::string& where, std::string* error) const;

    Settings& settings_;
    Audio* audio_ = nullptr;
    MoviePlayer player_;
    std::string dataDir_, appDirOverride_, ps1Override_;
    formats::StringTable text_;
    // The disc image's table of contents (opened again when the image path changes).
    mutable std::unique_ptr<formats::ps1::Disc> disc_;
    mutable std::string discPath_;
    const formats::ps1::Disc* disc() const;
    std::vector<Entry> queue_;
};

}  // namespace d2k::platform
