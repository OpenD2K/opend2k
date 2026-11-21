#pragma once
// artpack-manifest.json: describes the downloadable OpenD2K art pack (a GitHub release asset next
// to the zip; written by tools/release/package_artpack.sh, read by platform/artpack_download.*).
// docs/release.md "Art pack". Example:
//
//   { "format": 1, "name": "opend2k-redesign", "title": "OpenD2K redesign pack",
//     "version": "0.0.1", "file": "OpenD2K-redesign-pack-0.0.1.zip", "size": 612345678,
//     "sha256": "<64 hex>", "urls": ["https://github.com/OpenD2K/opend2k/releases/download/v0.0.1/..."],
//     "min_engine": "0.0.1", "licence": "cc-by-sa-4.0", "contains": ["art", "models"] }
//
// "urls" may be empty or relative: then "file" (or the relative url) is resolved against the
// manifest's own URL. The zip holds one folder <name>/ with pack.ini (artpacks/<name>/ also
// accepted). Pure (no OS calls).
//
// Optional "extras" (0.0.3+): more packs offered beside the main one, each an object with the same
// keys (name, title, version, file, size, sha256, urls, licence, contains) plus "description"
// (one line for the UI), "for3d" (suggested for the 3D Reimagined look) and "requires" (the pack
// it only works with, e.g. the Reimagined models overlay the main pack's models). An extra's zip
// holds one folder <name>/ (no pack.ini needed). Older games ignore the key (format stays 1); an
// invalid extra is dropped, the main pack stays usable. 0.0.3 extras: models-reimagined,
// photo-terrain, ground-clutter, ru-ai-voices (tools/release/package_artpack.sh).
// "install" (an extra, 0.0.3+): where it goes. Absent = <art pack folder>/<name>; "ai-voices" = the
// Russian AI voice folder (<pref path>/Voices/ru-ai, platform/language.hpp defaultAiVoicesDir; the
// zip's <name>/ folder becomes that folder). An unknown value drops the extra (a newer game's kind).

#include <cstdint>
#include <string>
#include <vector>

namespace d2k::formats {

struct ArtPackManifest {
    int format = 1;
    std::string name;     // pack folder name (artpacks/<name>)
    std::string title;    // shown in the UI ("" = name)
    std::string version;  // pack version ("0.0.1")
    std::string file;     // zip file name
    std::uint64_t size = 0;
    std::string sha256;   // lower-case hex
    std::vector<std::string> urls;  // absolute download URLs, first = preferred (resolved)
    std::string minEngine;          // oldest OpenD2K that can use the pack ("" = any)
    std::string licence;
    std::vector<std::string> contains;  // "art", "models"
    std::string page;  // human download page (the manual-download link), "" = the release page
    // Extras (see above). In an extra: description / for3d / requires; extras of an extra are ignored.
    std::string description;
    bool for3d = false;
    std::string requires_;
    bool isExtra = false;  // an entry of "extras": its zip needs no pack.ini
    std::string install;   // "" = the art pack folder, kInstallAiVoices = the AI voice folder
    std::vector<ArtPackManifest> extras;
    // This manifest or one of its extras by pack name (nullptr when none).
    const ArtPackManifest* item(const std::string& name) const;
};

// ArtPackManifest::install values.
inline constexpr const char* kInstallAiVoices = "ai-voices";

// Parses and validates (name is a plain folder name, size > 0, sha256 = 64 hex digits, at least
// one download URL after resolving against `manifestUrl`). False + `error` otherwise.
bool parseArtPackManifest(const std::string& json, const std::string& manifestUrl, ArtPackManifest& out,
                          std::string& error);

// Dotted numeric version compare ("0.0.10" > "0.0.9"; missing parts are 0; a non-numeric suffix
// is ignored): <0, 0, >0.
int compareVersions(const std::string& a, const std::string& b);

// `ref` resolved against `base` (absolute http(s) URL stays; "/x" = base's origin + /x; otherwise
// relative to base's directory).
std::string resolveUrl(const std::string& base, const std::string& ref);

// A plain pack folder name: [A-Za-z0-9._-], not "." / "..", 1..64 chars.
bool isSafePackName(const std::string& name);

}  // namespace d2k::formats
