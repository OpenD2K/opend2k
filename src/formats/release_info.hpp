#pragma once
// The latest OpenD2K release, as GitHub's REST API describes it
// (GET https://api.github.com/repos/OpenD2K/opend2k/releases/latest), for the app update check
// (platform/app_update.*; docs/release.md "App update check"). Only these fields are read:
//
//   { "tag_name": "v0.0.2", "name": "OpenD2K 0.0.2", "html_url": "https://github.com/.../tag/v0.0.2",
//     "prerelease": false, "draft": false, "published_at": "2026-12-01T10:00:00Z",
//     "assets": [ { "name": "OpenD2K-0.0.2-macos-universal.zip", "size": 123,
//                   "browser_download_url": "https://github.com/.../OpenD2K-0.0.2-macos-universal.zip" }, ... ] }
//
// A hand-written file with the same keys works too (tests, a mirror). Asset names follow the
// release scripts (tools/release/package_*.sh): OpenD2K-<version>-<os>[-<arch>].<ext>.
// Pure (no OS calls).

#include <cstdint>
#include <string>
#include <vector>

namespace d2k::formats {

struct ReleaseAsset {
    std::string name, url;
    std::uint64_t size = 0;
};

struct ReleaseInfo {
    std::string tag;          // "v0.0.2"
    std::string version;      // "0.0.2" (tag without the leading v)
    std::string name;         // "OpenD2K 0.0.2" ("" = none)
    std::string page;         // html_url: the release page (notes + every download)
    std::string publishedAt;  // ISO 8601 or ""
    bool prerelease = false, draft = false;
    std::vector<ReleaseAsset> assets;  // only those with an absolute http(s) URL
};

// Parses and validates (a tag whose version starts with a digit). Relative asset URLs are resolved
// against `sourceUrl`. False + `error` otherwise; a GitHub error document ({"message": ...}) gives
// its message.
bool parseReleaseInfo(const std::string& json, const std::string& sourceUrl, ReleaseInfo& out, std::string& error);

// Semantic version compare: <0, 0, >0. A leading "v" is ignored; any number of numeric parts
// (missing = 0); a pre-release ("0.1.0-beta.2") sorts before its release; "+build" is ignored.
int compareSemver(const std::string& a, const std::string& b);

// Which build this is, as used in the release asset names: os = "macos" | "windows" | "linux" |
// "android" | "ios"; arch = "arm64" | "x86_64" | "x86" | "arm" | "" (unknown).
struct PlatformId {
    std::string os, arch;
};
PlatformId currentPlatform();  // compile-time

// The asset to download for `p` (index into r.assets), -1 when none fits (iOS always: there the
// release page / TestFlight is the way). Rules: the name starts with "OpenD2K-", is not the art pack
// (…-pack-…, *.json, *.sha256 …), names the OS (macos/mac/osx, windows/win, linux, android) and an
// archive this OS opens (macOS .zip > .dmg; Windows .zip > .exe/.msi; Linux .tar.gz > .tar.xz >
// .AppImage; Android .apk); a name with this arch beats "universal", which beats no arch; a name
// with another arch is skipped (arm64 = aarch64, x86_64 = x64 = amd64).
int selectReleaseAsset(const ReleaseInfo& r, const PlatformId& p);

}  // namespace d2k::formats
