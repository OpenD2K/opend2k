// Format loader tests against the original game data (skipped if game/ is absent).

#include "formats/io.hpp"
#include "formats/language.hpp"
#include "formats/map.hpp"
#include "formats/mis.hpp"
#include "formats/r16.hpp"
#include "formats/templates.hpp"
#include "formats/bintables.hpp"
#include "formats/community.hpp"
#include "formats/mission_ini.hpp"
#include "formats/aud.hpp"
#include "formats/font.hpp"
#include "formats/soundrs.hpp"
#include "formats/uib.hpp"
#include "formats/uil.hpp"
#include "formats/nsis.hpp"
#include "formats/ps1_str.hpp"
#include "formats/ps1_sound.hpp"
#include "formats/ps1_models.hpp"
#include "formats/vqa.hpp"
#include "formats/artpack_manifest.hpp"
#include "formats/release_info.hpp"
#include "formats/sha256.hpp"
#include "formats/zip_reader.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <random>
#include <vector>
#include <cctype>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {
int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

// Per-run temp folder names (tests may run concurrently).
std::string uniqueName(const char* base) {
    return std::string(base) + "_" + std::to_string(std::random_device{}()) + "_" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

// A tiny synthetic NSIS 3 (ANSI, non-solid, stored) installer: SetOutPath $INSTDIR\sub, then
// File a.txt, File ..\evil.txt and File b\..\..\x.txt (the last two must be refused).
std::vector<std::uint8_t> syntheticInstaller() {
    std::vector<std::uint8_t> h;
    auto put32 = [](std::vector<std::uint8_t>& v, std::uint32_t x) {
        for (int i = 0; i < 4; ++i) v.push_back(std::uint8_t(x >> (8 * i)));
    };
    const std::string strings = std::string("\0", 1) + std::string("\x03\x95\x80\\sub\0", 9) + std::string("a.txt\0", 6) +
                                std::string("..\\evil.txt\0", 12) + std::string("b\\..\\..\\x.txt\0", 14);
    const std::uint32_t entOff = 68, nEnt = 4, strOff = entOff + 28 * nEnt, langOff = strOff + std::uint32_t(strings.size());
    put32(h, 0);  // flags
    const std::uint32_t blocks[8][2] = {{0, 0}, {entOff, 0}, {entOff, nEnt}, {strOff, 0}, {langOff, 0}, {0, 0}, {0, 0}, {0, 0}};
    for (auto& b : blocks) { put32(h, b[0]); put32(h, b[1]); }
    const std::int32_t ents[4][7] = {{11, 1, 1, 0, 0, 0, 0},     // SetOutPath "$INSTDIR\sub"
                                     {20, 0, 10, 0, 0, 0, 0},    // File a.txt     @0
                                     {20, 0, 16, 9, 0, 0, 0},    // File ..\evil.txt @9
                                     {20, 0, 28, 9, 0, 0, 0}};   // File b\..\..\x.txt
    for (auto& e : ents)
        for (std::int32_t v : e) put32(h, std::uint32_t(v));
    h.insert(h.end(), strings.begin(), strings.end());
    std::vector<std::uint8_t> data;
    put32(data, std::uint32_t(h.size()));  // stored header block
    data.insert(data.end(), h.begin(), h.end());
    put32(data, 5);
    for (char c : std::string("hello")) data.push_back(std::uint8_t(c));
    std::vector<std::uint8_t> exe(512, 0);  // "stub"
    exe[0] = 'M'; exe[1] = 'Z';
    put32(exe, 0);           // first header flags
    put32(exe, 0xDEADBEEF);
    for (char c : std::string("NullsoftInst")) exe.push_back(std::uint8_t(c));
    put32(exe, std::uint32_t(h.size()));
    put32(exe, std::uint32_t(28 + data.size() + 4));
    exe.insert(exe.end(), data.begin(), data.end());
    put32(exe, 0);  // CRC (not checked)
    return exe;
}

// ---- art pack download pieces (no network): SHA-256, manifest, zip + traversal guard ----------

void testSha256() {
    using d2k::formats::Sha256;
    CHECK(Sha256::hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256::hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";  // 56 bytes: padding spills a block
    CHECK(Sha256::hex(two.data(), two.size()) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // A million 'a', fed in odd-sized pieces (block boundary handling).
    Sha256 s;
    const std::string chunk(997, 'a');
    std::size_t left = 1000000;
    while (left) {
        const std::size_t n = std::min(left, chunk.size());
        s.update(chunk.data(), n);
        left -= n;
    }
    CHECK(s.finishHex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// The app update check (platform/app_update.*): semver order, the GitHub release JSON, and which
// release asset each platform downloads (names from tools/release/package_*.sh).
void testReleaseInfo() {
    using namespace d2k::formats;
    // Semver.
    CHECK(compareSemver("0.0.2", "0.0.1") > 0 && compareSemver("v0.0.1", "0.0.1") == 0 && compareSemver("0.0.10", "0.0.9") > 0);
    CHECK(compareSemver("0.1", "0.1.0") == 0 && compareSemver("1.0.0", "0.99.99") > 0 && compareSemver("0.0.1", "0.0.2") < 0);
    CHECK(compareSemver("0.1.0-beta", "0.1.0") < 0 && compareSemver("0.1.0", "0.1.0-rc.1") > 0);
    CHECK(compareSemver("0.1.0-alpha", "0.1.0-alpha.1") < 0 && compareSemver("0.1.0-alpha.2", "0.1.0-alpha.10") < 0);
    CHECK(compareSemver("0.1.0-1", "0.1.0-alpha") < 0 && compareSemver("0.1.0-beta", "0.1.0-alpha") > 0);
    CHECK(compareSemver("0.1.0+build.5", "0.1.0") == 0 && compareSemver("00012.0", "12") == 0);
    CHECK(compareSemver("99999999999999999999.0", "99999999999999999998.9") > 0);  // no overflow

    // A trimmed GitHub API response (extra keys and nested objects are ignored).
    const std::string api = "https://api.github.com/repos/OpenD2K/opend2k/releases/latest";
    const std::string dl = "https://github.com/OpenD2K/opend2k/releases/download/v0.0.2/";
    std::string json = R"({ "url": "x", "id": 1234567, "tag_name": "v0.0.2", "name": "OpenD2K 0.0.2",
        "html_url": "https://github.com/OpenD2K/opend2k/releases/tag/v0.0.2", "draft": false, "prerelease": false,
        "published_at": "2026-12-01T10:00:00Z", "author": {"login": "someone", "id": 1},
        "body": "## 0.0.2\n- fixes \"quoted\"", "assets": [)";
    const char* names[] = {"OpenD2K-0.0.2-macos-universal.zip", "OpenD2K-0.0.2-macos-universal.zip.sha256",
                           "OpenD2K-0.0.2-windows-x64.zip",     "OpenD2K-0.0.2-linux-x86_64.tar.gz",
                           "OpenD2K-0.0.2-android.apk",         "OpenD2K-redesign-pack-0.0.2.zip",
                           "artpack-manifest.json"};
    for (std::size_t i = 0; i < std::size(names); ++i)
        json += std::string(i ? "," : "") + R"({"name": ")" + names[i] + R"(", "size": )" + std::to_string(1000 + i) +
                R"(, "uploader": {"login": "u"}, "browser_download_url": ")" + dl + names[i] + R"("})";
    json += "]}";
    ReleaseInfo r;
    std::string err;
    CHECK(parseReleaseInfo(json, api, r, err));
    CHECK(err.empty() && r.tag == "v0.0.2" && r.version == "0.0.2" && r.name == "OpenD2K 0.0.2");
    CHECK(r.page == "https://github.com/OpenD2K/opend2k/releases/tag/v0.0.2" && r.publishedAt == "2026-12-01T10:00:00Z");
    CHECK(!r.draft && !r.prerelease && r.assets.size() == std::size(names));
    CHECK(r.assets[2].size == 1002 && r.assets[2].url == dl + "OpenD2K-0.0.2-windows-x64.zip");
    auto pick = [&](const char* os, const char* arch) {
        const int i = selectReleaseAsset(r, PlatformId{os, arch});
        return i < 0 ? std::string() : r.assets[std::size_t(i)].name;
    };
    CHECK(pick("macos", "arm64") == "OpenD2K-0.0.2-macos-universal.zip");
    CHECK(pick("macos", "x86_64") == "OpenD2K-0.0.2-macos-universal.zip");
    CHECK(pick("windows", "x86_64") == "OpenD2K-0.0.2-windows-x64.zip");
    CHECK(pick("windows", "arm64").empty());  // only an x64 build: the release page instead
    CHECK(pick("linux", "x86_64") == "OpenD2K-0.0.2-linux-x86_64.tar.gz");
    CHECK(pick("linux", "arm64").empty());
    CHECK(pick("android", "arm64") == "OpenD2K-0.0.2-android.apk");
    CHECK(pick("ios", "arm64").empty());
    // A per-arch macOS build beats the universal one; a .zip beats a .dmg.
    ReleaseInfo r2;
    for (const char* n : {"OpenD2K-0.0.3-macos-universal.dmg", "OpenD2K-0.0.3-macos-arm64.zip", "OpenD2K-0.0.3-macos-x86_64.zip",
                          "OpenD2K-0.0.3-macos-universal.zip", "Other-0.0.3-macos-arm64.zip"})
        r2.assets.push_back({n, dl + n, 1});
    auto pick2 = [&](const char* arch) {
        const int i = selectReleaseAsset(r2, PlatformId{"macos", arch});
        return i < 0 ? std::string() : r2.assets[std::size_t(i)].name;
    };
    CHECK(pick2("arm64") == "OpenD2K-0.0.3-macos-arm64.zip" && pick2("x86_64") == "OpenD2K-0.0.3-macos-x86_64.zip");
    CHECK(pick2("") == "OpenD2K-0.0.3-macos-universal.zip");
    // A minimal hand-written file; relative asset URLs resolve against it (the local test server).
    CHECK(parseReleaseInfo(R"({"tag_name":"0.1.0","assets":[{"name":"OpenD2K-0.1.0-linux-x86_64.tar.gz","browser_download_url":"f.tar.gz"},
                              {"name":"no-url"}]})",
                           "http://127.0.0.1:8766/rel/release.json", r, err));
    CHECK(r.version == "0.1.0" && r.page.empty() && r.assets.size() == 1 && r.assets[0].url == "http://127.0.0.1:8766/rel/f.tar.gz");
    // Refused: GitHub's error document, a tag that is no version, junk.
    CHECK(!parseReleaseInfo(R"({"message":"Not Found","documentation_url":"https://docs.github.com"})", api, r, err) &&
          err == "GitHub: Not Found");
    CHECK(!parseReleaseInfo(R"({"tag_name":"latest"})", api, r, err) && !err.empty());
    CHECK(!parseReleaseInfo("[1]", api, r, err) && !parseReleaseInfo("<html>", api, r, err));
    CHECK(!currentPlatform().os.empty());
}

void testArtPackManifest() {
    using namespace d2k::formats;
    const std::string sha(64, 'a');
    ArtPackManifest m;
    std::string err;
    const std::string base = "https://github.com/OpenD2K/opend2k/releases/latest/download/artpack-manifest.json";
    const std::string json = R"({ "format": 1, "name": "opend2k-redesign", "title": "OpenD2K redesign \u00e9 pack",
        "version": "0.0.2", "file": "OpenD2K-redesign-pack-0.0.2.zip", "size": 612345678,
        "sha256": ")" + std::string(64, 'A') + R"(", "urls": ["https://example.org/a.zip", "b.zip", "/c.zip", "ftp://x/y"],
        "min_engine": "0.0.1", "licence": "cc-by-sa-4.0", "contains": ["art", "models"], "extra": {"ignored": [1, 2, null]} })";
    CHECK(parseArtPackManifest(json, base, m, err));
    CHECK(err.empty());
    CHECK(m.name == "opend2k-redesign" && m.version == "0.0.2" && m.size == 612345678ull && m.sha256 == sha);
    CHECK(m.title == "OpenD2K redesign \xC3\xA9 pack");
    CHECK(m.urls.size() == 3 && m.urls[0] == "https://example.org/a.zip" &&
          m.urls[1] == "https://github.com/OpenD2K/opend2k/releases/latest/download/b.zip" && m.urls[2] == "https://github.com/c.zip");
    CHECK(m.contains.size() == 2 && m.licence == "cc-by-sa-4.0" && m.minEngine == "0.0.1");
    // No urls: "file" next to the manifest (the local test server layout).
    CHECK(parseArtPackManifest(R"({"name":"p","version":"1","size":5,"sha256":")" + sha + R"(","file":"p.zip"})",
                               "http://127.0.0.1:8765/dir/artpack-manifest.json?x=1", m, err));
    CHECK(m.urls.size() == 1 && m.urls[0] == "http://127.0.0.1:8765/dir/p.zip");
    // Refused: bad names (path traversal via the folder name), bad hash, no size, newer format, junk.
    for (const char* bad : {R"({"name":"../evil","version":"1","size":5,"sha256":"%s","file":"p.zip"})",
                            R"({"name":"a/b","version":"1","size":5,"sha256":"%s","file":"p.zip"})",
                            R"({"name":"p","version":"1","size":5,"sha256":"1234","file":"p.zip"})",
                            R"({"name":"p","version":"1","sha256":"%s","file":"p.zip"})",
                            R"({"format":2,"name":"p","version":"1","size":5,"sha256":"%s","file":"p.zip"})",
                            R"({"name":"p","version":"1","size":5,"sha256":"%s"})",
                            R"({"name":"p","version":"1","size":5,"sha256":"%s","file":"p.zip")", R"([1,2])", "nonsense"}) {
        std::string j = bad;
        if (const auto at = j.find("%s"); at != std::string::npos) j.replace(at, 2, sha);
        CHECK(!parseArtPackManifest(j, base, m, err) && !err.empty());
    }
    CHECK(compareVersions("0.0.10", "0.0.9") > 0 && compareVersions("0.1", "0.1.0") == 0 && compareVersions("1.2.3", "1.10") < 0);
    CHECK(compareVersions("0.0.1-beta", "0.0.1") == 0);
    CHECK(isSafePackName("opend2k-redesign") && !isSafePackName("..") && !isSafePackName("a b") && !isSafePackName(""));
    CHECK(resolveUrl("https://h.org/a/b/m.json", "https://x.org/z") == "https://x.org/z");
}

// A zip writer for the tests: stored entries, plus raw (already deflated) ones.
struct TestZip {
    std::vector<std::uint8_t> data, cd;
    int count = 0;
    static void p16(std::vector<std::uint8_t>& v, unsigned x) {
        v.push_back(std::uint8_t(x));
        v.push_back(std::uint8_t(x >> 8));
    }
    static void p32(std::vector<std::uint8_t>& v, std::uint32_t x) {
        for (int i = 0; i < 4; ++i) v.push_back(std::uint8_t(x >> (8 * i)));
    }
    void add(const std::string& name, const std::vector<std::uint8_t>& payload, std::uint32_t size, std::uint32_t crc, unsigned method,
             std::uint32_t unixMode = 0100644) {
        const std::uint32_t off = std::uint32_t(data.size());
        p32(data, 0x04034b50); p16(data, 20); p16(data, 0); p16(data, method); p16(data, 0); p16(data, 0);
        p32(data, crc); p32(data, std::uint32_t(payload.size())); p32(data, size); p16(data, unsigned(name.size())); p16(data, 0);
        data.insert(data.end(), name.begin(), name.end());
        data.insert(data.end(), payload.begin(), payload.end());
        p32(cd, 0x02014b50); p16(cd, 0x031E); p16(cd, 20); p16(cd, 0); p16(cd, method); p16(cd, 0); p16(cd, 0);
        p32(cd, crc); p32(cd, std::uint32_t(payload.size())); p32(cd, size); p16(cd, unsigned(name.size())); p16(cd, 0); p16(cd, 0);
        p16(cd, 0); p16(cd, 0); p32(cd, unixMode << 16); p32(cd, off);
        cd.insert(cd.end(), name.begin(), name.end());
        ++count;
    }
    void stored(const std::string& name, const std::string& text, std::uint32_t unixMode = 0100644) {
        const std::vector<std::uint8_t> v(text.begin(), text.end());
        add(name, v, std::uint32_t(v.size()), d2k::formats::zip::crc32(v.data(), v.size()), 0, unixMode);
    }
    std::vector<std::uint8_t> finish() const {
        std::vector<std::uint8_t> out = data;
        const std::uint32_t cdOff = std::uint32_t(out.size());
        out.insert(out.end(), cd.begin(), cd.end());
        p32(out, 0x06054b50); p16(out, 0); p16(out, 0); p16(out, unsigned(count)); p16(out, unsigned(count));
        p32(out, std::uint32_t(cd.size())); p32(out, cdOff); p16(out, 0);
        return out;
    }
};

void testZipReader() {
    namespace fs = std::filesystem;
    namespace zip = d2k::formats::zip;
    CHECK(zip::crc32("123456789", 9) == 0xCBF43926u);
    CHECK(zip::safeRelativePath("a/./b//c.png") == "a/b/c.png");
    for (const char* bad : {"../x", "a/../../x", "/etc/passwd", "C:/x", "a\\..\\x", "a/..", ".."}) CHECK(zip::safeRelativePath(bad).empty());

    const fs::path tmp = fs::temp_directory_path() / uniqueName("d2k_zip_test");
    fs::create_directories(tmp);
    auto writeZip = [&](const std::string& name, const TestZip& z) {
        const auto bytes = z.finish();
        std::ofstream(tmp / name, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        return (tmp / name).string();
    };
    // A good pack: folder entry, stored files, a deflated file; a stray top-level file outside
    // the prefix is skipped.
    TestZip good;
    good.stored("opend2k-redesign/", "", 040755);
    good.stored("opend2k-redesign/pack.ini", "name=Test\nscale=2\n");
    good.stored("opend2k-redesign/DATA/12.png", std::string("\x89PNG fake", 9));
    const std::string text = "hello hello hello hello hello deflate in OpenD2K!\n";
    good.add("opend2k-redesign/models/readme.txt",
             {0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0xc0, 0x41, 0xa6, 0xa4, 0xa6, 0xe5, 0x24, 0x96, 0xa4,
              0x2a, 0x64, 0xe6, 0x29, 0xf8, 0x17, 0xa4, 0xe6, 0xb9, 0x18, 0x79, 0x2b, 0x72, 0x01, 0x00},
             std::uint32_t(text.size()), 0x155182c9u, 8);
    good.stored("README-outside.txt", "not part of the pack");
    const std::string goodZip = writeZip("good.zip", good);
    d2k::formats::zip::Archive a;
    std::string err;
    CHECK(a.open(goodZip, err) && a.entries().size() == 5);
    int progressCalls = 0;
    auto r = zip::extractAll(goodZip, (tmp / "out").string(), "opend2k-redesign/", [&](std::uint64_t, std::uint64_t) {
        ++progressCalls;
        return true;
    });
    CHECK(r.ok && r.files == 3 && progressCalls > 0);
    CHECK(d2k::formats::readFile((tmp / "out/pack.ini").string()).size() == 18);
    const auto inflated = d2k::formats::readFile((tmp / "out/models/readme.txt").string());
    CHECK(std::string(inflated.begin(), inflated.end()) == text);
    CHECK(!fs::exists(tmp / "out/README-outside.txt") && !fs::exists(tmp / "out/opend2k-redesign"));

    // Corrupt CRC -> error.
    TestZip crc;
    crc.add("p/pack.ini", {'a', 'b'}, 2, 0x12345678u, 0);
    r = zip::extractAll(writeZip("crc.zip", crc), (tmp / "crc").string(), "p");
    CHECK(!r.ok && r.error.find("CRC") != std::string::npos);

    // Path traversal: one bad entry refuses the whole archive and nothing is written.
    for (const char* evil : {"opend2k-redesign/../../evil.txt", "/abs.txt", "opend2k-redesign/..\\evil.txt", "C:evil.txt"}) {
        TestZip t;
        t.stored("opend2k-redesign/pack.ini", "x");
        t.stored(evil, "pwned");
        r = zip::extractAll(writeZip("evil.zip", t), (tmp / "evil/out").string(), "");
        CHECK(!r.ok && r.error.find("unsafe") != std::string::npos);
        CHECK(!fs::exists(tmp / "evil.txt") && !fs::exists(tmp / "evil/evil.txt") && !fs::exists(tmp / "evil/out/opend2k-redesign/pack.ini"));
    }
    // Symlinks are refused.
    TestZip link;
    link.stored("p/pack.ini", "x");
    link.stored("p/escape", "/etc", 0120777);
    r = zip::extractAll(writeZip("link.zip", link), (tmp / "link").string(), "p");
    CHECK(!r.ok && r.error.find("link") != std::string::npos);
    // Cancel from the progress callback.
    r = zip::extractAll(goodZip, (tmp / "cancel").string(), "opend2k-redesign", [](std::uint64_t, std::uint64_t) { return false; });
    CHECK(!r.ok && r.cancelled);
    // Not a zip.
    std::ofstream(tmp / "junk.zip") << "this is not a zip file at all, just text";
    CHECK(!a.open((tmp / "junk.zip").string(), err) && !err.empty());
    std::error_code ec;
    fs::remove_all(tmp, ec);
}

void testNsisSynthetic() {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / uniqueName("d2k_nsis_synthetic");
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    const auto exe = syntheticInstaller();
    std::ofstream((tmp / "setup.exe").string(), std::ios::binary)
        .write(reinterpret_cast<const char*>(exe.data()), std::streamsize(exe.size()));
    const auto a = d2k::formats::nsis::open((tmp / "setup.exe").string());
    CHECK(!a.unicode && a.layout == d2k::formats::nsis::Layout::NonSolid && a.entrySize == 28);
    CHECK(a.files.size() == 1 && a.files[0].path == "sub/a.txt" && a.skipped == 2);
    const auto r = d2k::formats::nsis::extract(a, (tmp / "out").string());
    CHECK(r.ok && r.files == 1);
    CHECK(d2k::formats::readFile((tmp / "out/sub/a.txt").string()) == std::vector<std::uint8_t>({'h', 'e', 'l', 'l', 'o'}));
    CHECK(!fs::exists(tmp / "evil.txt") && !fs::exists(tmp / "x.txt"));
    fs::remove_all(tmp);
}

// The Gruntmods 1.6.7 installer next to the installed game (skipped if absent): a few files
// unpacked from it must equal the installed copy byte for byte.
void testNsisInstaller(const std::string& data) {
    namespace fs = std::filesystem;
    const std::string exe = data + "/../../Dune 2000 Gruntmods Edition 1.6.7.exe";
    if (!fs::exists(exe)) {
        std::puts("Gruntmods installer not found, skipping NSIS installer test");
        return;
    }
    using namespace d2k::formats;
    const auto a = nsis::open(exe);
    std::printf("installer: %s\n", a.describe().c_str());
    CHECK(a.unicode && a.layout == nsis::Layout::Chunked64 && a.entrySize == 36 && a.files.size() > 14000);
    const char* want[] = {"Dune 2000/DUNE2000.DAT", "Dune 2000/data/bin/Templates.bin", "Dune 2000/data/UI_DATA/menus.uib",
                          "Dune 2000/data/maps/_H5V1.MIS", "Dune 2000/version.txt"};
    auto wanted = [&](const nsis::File& f) {
        for (const char* w : want)
            if (std::equal(f.path.begin(), f.path.end(), w, w + std::strlen(w),
                           [](char x, char y) { return std::tolower((unsigned char)x) == std::tolower((unsigned char)y); }))
                return true;
        return false;
    };
    const fs::path tmp = fs::temp_directory_path() / uniqueName("d2k_nsis_gruntmods");
    fs::remove_all(tmp);
    nsis::ExtractOptions opt;
    opt.filter = wanted;
    const auto r = nsis::extract(a, tmp.string(), opt);
    if (!r.ok) std::fprintf(stderr, "installer extract failed: %s\n", r.error.c_str());
    CHECK(r.ok && r.files == std::size(want));
    for (const nsis::File& f : a.files) {
        if (!wanted(f)) continue;
        const std::string rel = f.path.substr(std::strlen("Dune 2000/"));
        const std::string installed = findFileNoCase((fs::path(data).parent_path() / fs::path(rel).parent_path()).string(),
                                                     fs::path(rel).filename().string());
        CHECK(!installed.empty() && readFile((tmp / f.path).string()) == readFile(installed));
    }
    fs::remove_all(tmp);
    // Cancelling from the progress callback stops at once.
    int calls = 0;
    const auto c = nsis::extract(a, tmp.string(), {}, [&](const nsis::Progress&) { return ++calls < 3; });
    CHECK(!c.ok && c.cancelled && c.files < 3);
    fs::remove_all(tmp);
}

// Format80 / LCW (formats/vqa.hpp): every command, absolute and relative forms.
void testFormat80() {
    using d2k::formats::decompressFormat80;
    std::vector<std::uint8_t> out;
    // literals "ABC", short back copy (3 from 3 back), fill 4 x 'Z', long copy 0xFF (5 from 0),
    // 0xC0 copy (3 from offset 1), end.
    const std::vector<std::uint8_t> abs = {0x83, 'A', 'B', 'C', 0x00, 0x03, 0xFE, 4, 0, 'Z', 0xFF, 5, 0, 0, 0,
                                           0xC0, 1, 0, 0x80};
    CHECK(decompressFormat80(abs, out, 100, false));
    CHECK(std::string(out.begin(), out.end()) == "ABCABCZZZZABCABBCA");
    // Relative: the 0xFF / 0xC0 offsets count back from the write position.
    const std::vector<std::uint8_t> rel = {0x83, 'A', 'B', 'C', 0xFF, 4, 0, 3, 0, 0xC0, 2, 0, 0x80};
    CHECK(decompressFormat80(rel, out, 100, true));
    CHECK(std::string(out.begin(), out.end()) == "ABCABCACAC");
    // A back reference before the start is refused; output stops at maxOut.
    CHECK(!decompressFormat80(std::vector<std::uint8_t>{0x00, 0x05, 0x80}, out, 100, false));
    CHECK(decompressFormat80(abs, out, 5, false) && out.size() == 5);
}

// A PC cutscene (skipped without the movie files): T_TITL_E.VQA from the game's movies folder,
// D2K_MOVIES_DIR, or the app-managed folder.
void testVqaMovie(const std::string& data) {
    namespace fs = std::filesystem;
    std::vector<std::string> dirs = {data + "/../movies"};
    if (const char* env = std::getenv("D2K_MOVIES_DIR")) dirs.insert(dirs.begin(), env);
    if (const char* home = std::getenv("HOME")) dirs.push_back(std::string(home) + "/Library/Application Support/d2k-mac/Dune 2000/Movies");
    std::string path;
    for (const auto& d : dirs) {
        const std::string p = fs::exists(d) ? d2k::formats::findFileNoCase(d, "T_TITL_E.VQA") : std::string();
        std::error_code ec;
        if (!p.empty() && fs::file_size(p, ec) > 0) { path = p; break; }
    }
    if (path.empty()) {
        std::puts("T_TITL_E.VQA not found, skipping VQA movie test");
        return;
    }
    d2k::formats::VqaDecoder d;
    CHECK(d.open(path));
    const auto& h = d.header();
    CHECK(h.version == 2 && h.width == 640 && h.height == 400 && h.blockW == 4 && h.blockH == 4 && h.fps == 15);
    CHECK(h.hicolor() && h.colors == 0 && h.frames == 96);
    CHECK(h.hasAudio() && h.sampleRate == 22050 && h.channels == 1 && h.bits == 16);
    auto fnv = [](const std::vector<std::uint32_t>& v) {
        std::uint64_t x = 1469598103934665603ull;
        for (std::uint32_t p : v)
            for (int i = 0; i < 4; ++i) { x ^= (p >> (8 * i)) & 255; x *= 1099511628211ull; }
        return x;
    };
    int frames = 0;
    std::size_t samples = 0;
    std::uint64_t h40 = 0, last = 0;
    while (d.nextFrame()) {
        samples += d.takeAudio().size();
        if (d.frameIndex() == 40) h40 = fnv(d.pixels());
        last = fnv(d.pixels());
        ++frames;
    }
    std::printf("VQA %s: %d frames, %zu samples\n", path.c_str(), frames, samples);
    CHECK(d.error().empty());
    CHECK(frames == 96 && samples == 141120);
    // Frame 40 (first codebook) and the last frame (after the VQFL codebook switch at 76).
    CHECK(h40 == 0x66ae07b6bfef417aull);
    CHECK(last == 0x22bfd3bb54a08a35ull);
}

// A PS1 cutscene (skipped without the disc image game/ps1/*.cue): A_LOSE.STR frame 9.
void testPs1Movie(const std::string& data) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(data) / "../../ps1";
    std::string cue;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.path().extension() == ".cue") cue = e.path().string();
    if (cue.empty()) {
        std::puts("PS1 disc image not found, skipping STR test");
        return;
    }
    using namespace d2k::formats::ps1;
    Disc disc;
    CHECK(disc.open(cue));
    CHECK(disc.fat().size() == 762);
    const FatEntry* e = disc.xaEntry("A_LOSE.STR");
    CHECK(e != nullptr);
    if (!e) return;
    StrDecoder st;
    CHECK(st.open(disc, *e));
    CHECK(st.width() == 320 && st.height() == 208 && st.sampleRate() == 18900 && st.channels() == 1);
    for (int i = 0; i < 10; ++i) CHECK(st.nextFrame());
    double r = 0, g = 0, b = 0;
    for (std::uint32_t p : st.pixels()) { r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255; }
    const double n = double(st.pixels().size());
    // Means of the decoded picture (float IDCT: compared with a tolerance).
    std::printf("STR A_LOSE frame 9: mean RGB %.2f %.2f %.2f\n", r / n, g / n, b / n);
    CHECK(std::abs(r / n - 88.15) < 0.5 && std::abs(g / n - 79.12) < 0.5 && std::abs(b / n - 72.06) < 0.5);
    CHECK(st.takeAudio().size() == 12096);  // three 4032-sample XA sectors so far
}

// PS1 speech (formats/ps1_sound.hpp): SPU ADPCM decoding of hand-built blocks and the
// PC name -> bank slot table. Synthetic, always run.
void testPs1SpeechSynthetic() {
    using namespace d2k::formats::ps1;
    // Block 1: filter 0, shift 12 -> samples are the raw nibbles (signed): 1, -1, 7, -8, then 0.
    // Block 2: filter 1 (60/64), shift 12, all nibbles 0 -> decays from the previous sample;
    //          end flag set. Block 3 must not be decoded.
    std::uint8_t blocks[48] = {};
    blocks[0] = 0x0C;
    blocks[2] = 0xF1;  // low nibble first: 1, then 0xF = -1
    blocks[3] = 0x87;  // 7, then 0x8 = -8
    blocks[16] = 0x1C;
    blocks[17] = 0x01;  // end
    blocks[32] = 0x0C;
    blocks[34] = 0x11;
    const std::vector<std::int16_t> pcm = decodeSpuAdpcm(blocks);
    CHECK(pcm.size() == 56);
    if (pcm.size() == 56) {
        CHECK(pcm[0] == 1 && pcm[1] == -1 && pcm[2] == 7 && pcm[3] == -8 && pcm[4] == 0 && pcm[27] == 0);
        CHECK(pcm[28] == 0);  // (0*60 + 32) >> 6
    }
    // Shift 0 = full scale; the filter keeps the running value and clamps at 16 bits.
    std::uint8_t loud[16] = {0x00, 0x00, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77};
    loud[0] = 0x40;  // filter 4 (122/64, -60/64), shift 0
    const std::vector<std::int16_t> l = decodeSpuAdpcm(loud);
    CHECK(l.size() == 28 && l[0] == 28672 && l[1] == 32767 && l[27] <= 32767);
    // A partial trailing block is ignored.
    CHECK(decodeSpuAdpcm(std::span<const std::uint8_t>(blocks, 15)).empty());

    std::string bank;
    int slot = -1;
    CHECK(speechSlotFor("AI_ATACK", bank, slot) && bank == "satr" && slot == 138);
    CHECK(speechSlotFor("hi_power", bank, slot) && bank == "shar" && slot == 120);
    CHECK(speechSlotFor("O_SSEL1.AUD", bank, slot) && bank == "sord" && slot == 67);
    CHECK(speechSlotFor("A_VSEL1", bank, slot) && bank == "satr" && slot == 61);
    CHECK(speechSlotFor("G_SCONF2", bank, slot) && bank == "satr" && slot == 90);
    CHECK(speechSlotFor("AI_ENEMY", bank, slot) && bank == "matr" && slot == 1);
    CHECK(speechSlotFor("OI_ENEMY", bank, slot) && bank == "sord" && slot == 151);
    CHECK(!speechSlotFor("AI_MAP1A", bank, slot));
    CHECK(!speechSlotFor("AI_GLOAD", bank, slot));
    CHECK(!speechSlotFor("S01V1-00", bank, slot));
    CHECK(!speechSlotFor("mgun2", bank, slot));
    CHECK(!speechSlotFor("", bank, slot));
}

// PS1 speech from the Russian disc (skipped without game/ps1/*.cue).
void testPs1Speech(const std::string& data) {
    namespace fs = std::filesystem;
    std::string cue;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(fs::path(data) / "../../ps1", ec))
        if (e.path().extension() == ".cue") cue = e.path().string();
    if (cue.empty()) {
        std::puts("PS1 disc image not found, skipping PS1 speech test");
        return;
    }
    using namespace d2k::formats::ps1;
    SpeechBanks sb;
    CHECK(!sb.clip("AI_ATACK"));  // not open yet
    CHECK(sb.open(cue));
    CHECK(sb.slotCount("satr") == 150 && sb.slotCount("matr") == 4);
    for (const char* name : {"AI_ATACK", "HI_REINF", "OI_1MIN", "A_VSEL1", "H_ECONF2", "G_SSEL1", "AI_HATTK"}) {
        const auto c = sb.clip(name);
        CHECK(c.has_value());
        if (!c) continue;
        const double sec = double(c->samples.size()) / c->sampleRate;
        CHECK(c->sampleRate == kSpeechSampleRate && c->channels == 1 && sec > 0.3 && sec < 4.0);
        CHECK(c->samples.back() != 0);  // zero padding trimmed
        int peak = 0;
        for (std::int16_t v : c->samples) peak = std::max(peak, std::abs(int(v)));
        CHECK(peak > 2000);
    }
    const auto overflow = sb.slotClip("shar", 150);  // count 150 on this disc -> matr 0
    const auto matr0 = sb.slotClip("matr", 0);
    CHECK(overflow && matr0 && overflow->samples == matr0->samples);
    CHECK(!sb.clip("AI_MAP1A"));
    CHECK(!sb.clip("O_SSEL1"));  // the Russian banks have no saboteur lines
}

// PS1 3D models (formats/ps1_models.hpp) from both discs when present (game/ps1, game/ps1-en).
void testPs1Models(const std::string& data) {
    namespace fs = std::filesystem;
    int discs = 0;
    for (const char* sub : {"../../ps1", "../../ps1-en"}) {
        std::string cue;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(fs::path(data) / sub, ec))
            if (e.path().extension() == ".cue") cue = e.path().string();
        if (cue.empty()) continue;
        ++discs;
        using namespace d2k::formats::ps1;
        Disc disc;
        CHECK(disc.open(cue));
        std::vector<d2k::formats::Model3D> models;
        std::vector<ModelInfo> info;
        std::string err;
        CHECK(loadModels(disc, models, err, &info));
        if (!err.empty()) std::printf("PS1 models: %s\n", err.c_str());
        CHECK(models.size() == 35);
        if (models.size() != 35) continue;
        CHECK(models[0].name == "trike" && models[10].name == "cmbtnk" && models[14].name == "wtrap" &&
              models[34].name == "wall");
        CHECK(models[0].texW == 32 && models[10].texW == 64 && models[10].texH == 64);
        for (const auto& m : models) CHECK(!m.body.empty() && m.texture.size() == std::size_t(m.texW * m.texH));
        // Combat tank: body, a house marker (2 triangles), shadow, turret on its own pivot.
        const auto& t = models[10];
        CHECK(t.house.tris.size() == 6 && !t.shadow.empty() && !t.turret.empty());
        CHECK(t.turretPivot[1] > 8 && t.turretPivot[1] < 12);  // 80 PS1 units up = 10 px
        // Wind trap: anchored at the art box's bottom-left (z <= 0 = north of it), its 2 tiles of
        // ground on the lower two rows of the 2x3 box (z -64..0 px); the curtains closing its
        // back stay inside that box.
        float zmin = 1e9f, zmax = -1e9f;
        for (const auto& v : models[14].body.tris) zmin = std::min(zmin, v.pos[2]), zmax = std::max(zmax, v.pos[2]);
        CHECK(zmin > -66 && zmax < 2);
        CHECK(models[14].anchor == d2k::formats::Model3D::Anchor::ArtBottomLeft);
        // Not a mirror: the Atreides barracks' hawk faces the (south) camera: with faces wound
        // counter-clockwise seen from outside, nearly all its area faces south (+z), not north.
        CHECK(models[20].name == "a_barrak");
        float south = 0, north = 0;
        const auto& bt = models[20].body.tris;
        for (std::size_t i = 0; i + 2 < bt.size(); i += 3) {
            if (bt[i].oneSided > 0.5f) continue;
            const float ux = bt[i + 1].pos[0] - bt[i].pos[0], uy = bt[i + 1].pos[1] - bt[i].pos[1];
            const float vx = bt[i + 2].pos[0] - bt[i].pos[0], vy = bt[i + 2].pos[1] - bt[i].pos[1];
            const float nz = ux * vy - uy * vx;  // z of the right-hand normal (x area)
            (nz > 0 ? south : north) += std::fabs(nz);
        }
        CHECK(south > 5 * north);
        // The open back of a building gets one-sided curtains.
        bool curtains = false;
        for (const auto& v : models[14].body.tris) curtains |= v.oneSided > 0.5f;
        CHECK(curtains);
        CHECK(modelForUnit("COMBAT_TANK") == "cmbtnk" && modelForBuilding("ORDOS BARRACKS", "BARRACKS") == "o_barrak");
    }
    if (!discs) std::puts("PS1 disc images not found, skipping PS1 model test");
}

// Community campaigns (formats/community.hpp, formats/mission_ini.hpp): synthetic data.
void testCommunitySynthetic() {
    using namespace d2k::formats;
    // Mission .ini keys (case-insensitive), [Text] by index, [Vars] in order.
    const MissionIni mi = parseMissionIni(parseIni(
        "[Basic]\r\nName=War of the Landsraad - Atreides Mission 3\r\nsideid=0\r\nBriefing=Line one_Line two\r\n"
        "TextUib=WOL_Text.uib\r\n[Data]\r\nCampaignFolder=WarOfTheLandsraad\r\nModsFolder=WOL_A\r\n"
        "ColoursFile=colours_A3.bin\r\n[Vars]\r\nInfiniteSpice=Yes\r\nSandWormShotSleep=40000\r\n[Text]\r\n"
        "2=Second\r\n0=Zero\r\n"));
    CHECK(mi.present && mi.sideId == 0 && mi.textUib == "WOL_Text.uib" && mi.modsFolder == "WOL_A");
    CHECK(mi.campaignFolder == "WarOfTheLandsraad" && mi.coloursFile == "colours_A3.bin");
    CHECK(mi.vars.size() == 2 && mi.vars[0].first == "InfiniteSpice" && mi.vars[1].second == "40000");
    CHECK(mi.text.size() == 2 && mi.text.at(0) == "Zero" && mi.text.at(2) == "Second");
    CHECK(missionBriefingText(mi.briefing) == "Line one\nLine two");

    // Mission name scheme.
    auto p = parseCommunityMissionName("War of the Landsraad - Sardaukar Mission 4");
    CHECK(p.valid && p.series == "War of the Landsraad" && p.house == "Sardaukar" && p.number == 4 && !p.bonus);
    p = parseCommunityMissionName("Coalition of Nobles - Mission 3 (Option 2)");
    CHECK(p.valid && p.series == "Coalition of Nobles" && p.number == 3 && p.option == 2 && p.house.empty());
    p = parseCommunityMissionName("AntiFremen - Mission 9 (part 2)");
    CHECK(p.valid && p.number == 9 && p.part == 2 && p.option == 0);
    p = parseCommunityMissionName("Rise Of The Mercenaries - by gruntmods.com - Mission 2");
    CHECK(p.valid && p.series == "Rise Of The Mercenaries" && p.subtitle == "by gruntmods.com" && p.number == 2);
    p = parseCommunityMissionName("Coalition of Nobles: New Enemies - Mission Bonus");
    CHECK(p.valid && p.bonus && p.series == "Coalition of Nobles: New Enemies");
    CHECK(parseCommunityMissionName("Coalition of Nobles - Bonus").bonus);
    CHECK(parseCommunityMissionName("Original Campaign - Atreides Mission 1a").valid == false);  // not a community series
    CHECK(parseCommunityMissionName("S01V1: Quality Control").valid == false);
    CHECK(parseCommunityMissionName("Test - Mission 4b").option == 2);

    // Grouping, order, alternatives, bonus, houses; progress / unlock rules.
    std::vector<CommunityMissionRef> refs = {
        {"T2", "Test - Mission 2", "Me", 1, ""},          {"T1", "Test - Mission 1", "Me", 1, ""},
        {"T3B", "Test - Mission 3 (Option 2)", "Me", 1, ""}, {"T3A", "Test - Mission 3 (Option 1)", "me", 1, ""},
        {"TB", "Test - Bonus", "Me", 1, ""},              {"T4", "Test - Mission 4", "Me", 1, ""},
        {"HA1", "Houses - Atreides Mission 1", "X", 0, ""}, {"HA2", "Houses - Atreides Mission 2", "X", 0, ""},
        {"HO1", "Houses - Ordos Mission 1", "X", 2, ""},  {"HO2", "Houses - Ordos Mission 2", "X", 2, ""},
        {"S1", "Single - Mission 1", "Y", 0, ""},
    };
    const auto camps = buildCommunityCampaigns(refs, "");
    CHECK(camps.size() == 3);  // Houses - Atreides, Houses - Ordos, Test (Single has one mission)
    if (camps.size() == 3) {
        CHECK(camps[0].name == "Houses - Atreides" && camps[1].name == "Houses - Ordos" && camps[1].side == 2);
        const CommunityCampaign& t = camps[2];
        CHECK(t.name == "Test" && t.id == "test" && t.author == "Me" && t.steps.size() == 5);
        if (t.steps.size() == 5) {
            CHECK(t.steps[0].stems == std::vector<std::string>{"T1"} && t.steps[1].stems == std::vector<std::string>{"T2"});
            CHECK((t.steps[2].stems == std::vector<std::string>{"T3A", "T3B"}));  // alternatives, option order
            CHECK(t.steps[3].stems == std::vector<std::string>{"T4"} && t.steps[4].bonus);
        }
        CommunityProgress pr;
        CHECK(communityStepUnlocked(t, 0, pr) && !communityStepUnlocked(t, 1, pr));
        CHECK(communityNextStep(t, pr) == 0 && communityRequiredSteps(t) == 4);
        pr.markWon("test", "T1");
        pr.markWon("test", "t2");
        CHECK(communityStepUnlocked(t, 2, pr) && !communityStepUnlocked(t, 3, pr));
        pr.markWon("TEST", "T3B");  // either alternative wins the step
        CHECK(communityStepWon(t, 2, pr) && communityStepUnlocked(t, 3, pr) && !communityStepUnlocked(t, 4, pr));
        CHECK(communityNextStep(t, pr) == 3 && communityWonSteps(t, pr) == 3);
        pr.markWon("test", "T4");
        CHECK(communityStepUnlocked(t, 4, pr) && communityWonSteps(t, pr) == 4);  // bonus open, not required
        // Round trip through the progress file.
        const std::string path = (std::filesystem::temp_directory_path() / "d2k_community_progress_test.ini").string();
        CHECK(pr.save(path));
        const CommunityProgress back = CommunityProgress::load(path);
        CHECK(back.won == pr.won && back.isWon("test", "t3b"));
        std::filesystem::remove(path);
    }
}

// Community campaigns from the installed Gruntmods data (skipped without it).
void testCommunityData(const std::string& data) {
    using namespace d2k::formats;
    const std::string missions = data + "/Missions";
    if (!std::filesystem::exists(missions + "/WOL_A1.ini")) {
        std::puts("Gruntmods community missions not found, skipping community tests");
        return;
    }
    std::vector<CommunityMissionRef> refs;
    for (const auto& e : std::filesystem::directory_iterator(missions)) {
        const std::string f = e.path().filename().string();
        if (f.size() < 5 || (f.substr(f.size() - 4) != ".ini" && f.substr(f.size() - 4) != ".INI")) continue;
        const MissionIni mi = loadMissionIni(e.path().string());
        refs.push_back({e.path().stem().string(), mi.name, mi.author, mi.sideId, mi.campaignFolder});
    }
    const auto camps = buildCommunityCampaigns(refs, data);
    auto find = [&](const std::string& name) -> const CommunityCampaign* {
        for (const auto& c : camps)
            if (c.name == name) return &c;
        return nullptr;
    };
    // Summers' Solstice from SplashArtData: 4 arcs, 21 steps (3 bonus), 30 maps; S01 has 2 versions.
    const CommunityCampaign* ss = find("Summers' Solstice");
    CHECK(ss && ss->chapters == 4 && ss->steps.size() == 21 && ss->missionCount() == 30 && ss->author == "Fey");
    if (ss && ss->steps.size() == 21) {
        CHECK((ss->steps[0].stems == std::vector<std::string>{"FEYS01V1", "FEYS01V2"}) && ss->steps[2].bonus);
        CHECK(ss->steps[2].label == "SBON1" && ss->steps[20].stems.front() == "FEYS18V1" && ss->steps[20].chapter == 4);
        CHECK(communityRequiredSteps(*ss) == 18);
    }
    const CommunityCampaign* wola = find("War of the Landsraad - Atreides");
    CHECK(wola && wola->steps.size() == 15 && wola->folder == "WarOfTheLandsraad" && wola->side == 0);
    if (wola && !wola->steps.empty()) CHECK(wola->steps[9].stems.front() == "WOL_A10");  // numeric order
    const CommunityCampaign* wols = find("War of the Landsraad - Sardaukar");
    CHECK(wols && wols->steps.size() == 5);
    const CommunityCampaign* con = find("Coalition of Nobles");
    CHECK(con && con->steps.size() == 17 && con->steps[2].missions.size() == 2 && con->steps[5].missions.size() == 3 &&
          con->steps.back().bonus);
    const CommunityCampaign* seq = find("Coalition of Nobles: New Enemies");
    CHECK(seq && seq->steps.size() == 17 && seq->steps[11].missions.size() == 3 && seq->steps.back().bonus);
    const CommunityCampaign* zz = find("Butlerian Jihad: Zone Zero");
    CHECK(zz && zz->steps.size() == 10 && zz->steps.back().stems.front() == "Butlerian Jihad Zone Zero Z");
    CHECK(find("Butlerian Jihad") && find("Butlerian Jihad War Machines") && find("War Of Assassins - Ordos"));
    CHECK(find("WoA Sandstorm - Harkonnen") && find("Rise Of The Mercenaries") && find("AntiFremen"));
    const CommunityCampaign* af = find("AntiFremen");
    CHECK(af && af->steps.size() == 10);  // mission 9 in two parts
    CHECK(!find("Original Campaign"));
    // A mods folder overlay and a colours file of a real mission.
    const MissionIni wol = loadMissionIni(missions + "/WOL_A10.ini");
    const std::string mods = missionModsDataDir(data, wol);
    CHECK(!mods.empty() && !findOverlayFile(mods, data, "bin", "Templates.bin").empty());
    CHECK(findOverlayFile(mods, data, "", "DATA.R16").find("CustomCampaignData") != std::string::npos);
    {  // not overridden (compare files, not strings: separators differ on Windows)
        std::error_code ec;
        CHECK(std::filesystem::equivalent(findOverlayFile(mods, data, "bin", "VARS.BIN"),
                                          findFileNoCase(data + "/bin", "VARS.BIN"), ec));
    }
    const std::string ccd = customCampaignDir(data, wol.campaignFolder);
    CHECK(!ccd.empty() && !findFileNoCase(findFileNoCase(ccd, "Colours"), wol.coloursFile).empty());
    // Extended .MIS counts.
    const auto feys = loadMis(findFileNoCase(missions, "_FEYS01V1.MIS"));
    CHECK(feys.extended && feys.extendedEvents > 64 && feys.events.size() <= 64);
    CHECK(!loadMis(findFileNoCase(missions, "_WOL_A1.MIS")).extended);
}

}  // namespace

// ---- languages (formats/language.hpp) ----------------------------------------------------

void writeFile(const std::filesystem::path& p, const std::string& bytes) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << bytes;
}

std::string uibBytes(const std::vector<std::pair<std::string, std::string>>& entries) {
    std::string out;
    auto u32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) out += char(v >> (8 * i)); };
    auto str = [&](const std::string& t) {
        const std::size_t n = t.size() + 1;
        out += char(n & 0xff);
        out += char(n >> 8);
        out += t;
        out += '\0';
    };
    u32(std::uint32_t(entries.size()));
    for (const auto& [k, v] : entries) { str(k); str(v); }
    return out;
}

void testLanguageSynthetic() {
    namespace lang = d2k::formats::lang;
    using d2k::formats::StringTable;
    // UTF-8 <-> code pages.
    const std::string ru = "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9";  // "Русский"
    CHECK(lang::toCodepage(ru, 1251) == "\xD0\xF3\xF1\xF1\xEA\xE8\xE9");
    CHECK(lang::toUtf8("\xD0\xF3\xF1\xF1\xEA\xE8\xE9", 1251) == ru);
    CHECK(lang::toCodepage("Fran\xC3\xA7" "ais", 1252) == "Fran\xE7" "ais");
    CHECK(lang::toCodepage("Fran\xC3\xA7" "ais", 1251) == "Francais");                  // no ç in CP1251
    CHECK(lang::toCodepage("\xD0\x96\xD1\x83\xD0\xBA", 1252) == "Zhuk");                 // "Жук" for a Latin font
    CHECK(lang::encodable(ru, 1251) && !lang::encodable(ru, 1252));
    CHECK(lang::displayName(lang::Id::French, 1251) == "French");
    CHECK(lang::displayName(lang::Id::French, 1252) == "Fran\xE7" "ais");
    CHECK(lang::displayName(lang::Id::RussianPs1, 1251) == "\xD0\xF3\xF1\xF1\xEA\xE8\xE9 (PS1)");
    CHECK(lang::displayName(lang::Id::RussianPc, 1252) == "Russian (translation)");
    CHECK(lang::upper("\xE0\xE1\xFF z", 1251) == "\xC0\xC1\xDF Z");                     // "абя z" -> "АБЯ Z"
    CHECK(lang::upper("\xB8", 1251) == "\xA8");                                          // ё -> Ё
    CHECK(lang::fromCode("ru-ps1") == lang::Id::RussianPs1 && !lang::fromCode("xx"));
    CHECK(lang::codepageOf(lang::Id::RussianPc) == 1251 && lang::codepageOf(lang::Id::German) == 1252);

    // Per-source code pages: a community .ini is Latin ("Coup de Grâce", FEYS14V2.ini), never
    // the active language's CP1251 ("Grвce" with the Russian font).
    {
        const std::string grace1252 = "S14V2: Coup de Gr\xE2" "ce";
        const std::string graceUtf8 = "S14V2: Coup de Gr\xC3\xA2" "ce";
        CHECK(lang::detectCodepage(grace1252) == 1252);
        CHECK(lang::detectCodepage(graceUtf8) == lang::kUtf8);
        CHECK(lang::detectCodepage(std::string("\xD0\xF3\xF1\xF1\xEA\xE8\xE9 \xF2\xE5\xEA\xF1\xF2")) == 1251);  // "Русский текст"
        CHECK(lang::detectCodepage(std::string("Plain ASCII")) == 1252);
        CHECK(!lang::isUtf8("ASCII") && lang::isUtf8(graceUtf8) && !lang::isUtf8(grace1252));
        CHECK(lang::sourceToUtf8(grace1252, 1252) == graceUtf8);
        // The Play font draws the UTF-8 as is; the bitmap fonts get the active code page:
        CHECK(lang::forBitmapFont(graceUtf8, 1252) == grace1252);                          // Latin font: â
        CHECK(lang::forBitmapFont(graceUtf8, 1251) == "S14V2: Coup de Grace");              // Fargus font: bare a
        CHECK(lang::forBitmapFont("\xD0\xF3\xF1", 1251) == "\xD0\xF3\xF1");                 // CP1251 bytes stay
        CHECK(lang::recode(grace1252, 1252, 1251) == "S14V2: Coup de Grace");
        CHECK(lang::recode("abc\xAC" "def", 1252, 1251) == "abc\xAC" "def");               // the ¬ line break survives
        const auto mi = d2k::formats::parseMissionIni(d2k::formats::parseIni(
            "[Basic]\nName=Summers' Solstice - S14V2: Coup de Gr\xE2" "ce\nBriefing=Coup de gr\xE2" "ce.\n[Vars]\nA=1\nB=2\n"));
        CHECK(mi.codepage == 1252);
        const auto miRu = d2k::formats::parseMissionIni(d2k::formats::parseIni(
            "[Basic]\nName=\xCC\xE8\xF1\xF1\xE8\xFF 1\n[Vars]\nHarvesterSpeed=1\nCarryallSpeed=2\n"));  // "Миссия 1"
        CHECK(miRu.codepage == 1251);
        StringTable t;
        t.entries = {{"A", "Gr\xE2" "ce"}, {"B", "plain"}};
        CHECK(lang::detectCodepage(t) == 1252);
        lang::recodeValues(t, 1252, 1251);
        CHECK(t.entries[0].second == "Grace" && t.entries[1].second == "plain");
    }

    // CP1251 bytes -> glyphs through the font's character map (the Russian FONT.BIN maps
    // 0xC0..0xFF, А..я, to glyph codes 0x80..0xBF).
    {
        std::vector<std::uint8_t> fnt(0x408, 0);
        fnt[1] = 4;     // space width
        fnt[2] = 0x21;  // first glyph code
        fnt[3] = 1;     // spacing
        fnt[4] = 1;     // height
        for (int code = 0x21; code < 0xC0; ++code) {  // glyph `code`: width = code & 0x3f | 1
            const int w = (code & 0x3f) | 1;
            for (int v : {w, 0, 0, 0, 1, 0, 0, 0}) fnt.push_back(std::uint8_t(v));
            for (int x = 0; x < w; ++x) fnt.push_back(1);
        }
        std::vector<std::uint8_t> map(256, 0x20);
        for (int c = 0x20; c < 0x80; ++c) map[std::size_t(c)] = std::uint8_t(c);
        for (int c = 0xC0; c < 0x100; ++c) map[std::size_t(c)] = std::uint8_t(c - 0x40);
        const auto f = d2k::formats::parseFont(fnt, map);
        const std::string word = lang::toCodepage("\xD0\x90\xD1\x8F", 1251);  // "Ая" = C0 FF
        CHECK(word == "\xC0\xFF");
        CHECK(f.glyphFor(0xC0) == &f.glyphs[0x80 - 0x21] && f.glyphFor(0xFF) == &f.glyphs[0xBF - 0x21]);
        CHECK(f.glyphFor(0xA0) == nullptr);  // mapped to a space
        CHECK(f.textWidth(word) == ((0x80 & 0x3f) | 1) + 1 + ((0xBF & 0x3f) | 1) + 1);
    }

    // Merging a translation: base order kept, missing / empty keys stay English, extra keys
    // appended, the version stays the data's; a mission's own value survives.
    {
        const StringTable base = d2k::formats::parseUib(std::vector<std::uint8_t>(
            [] { auto b = uibBytes({{"A", "a"}, {"B", "b"}, {"C", "c"}, {"VersionNumber", "V 1.06"}}); return std::vector<std::uint8_t>(b.begin(), b.end()); }()));
        const auto tb = uibBytes({{"B", "\xC1"}, {"A", ""}, {"D", "\xC4"}, {"VersionNumber", "V 1.02 RUS"}});
        const StringTable tr = d2k::formats::parseUib(std::vector<std::uint8_t>(tb.begin(), tb.end()));
        const StringTable m = lang::mergeTranslation(base, base, tr);
        CHECK(m.entries.size() == 5 && m.entries[0].second == "a" && m.entries[1].second == "\xC1" && m.entries[2].second == "c");
        CHECK(*m.find("VersionNumber") == "V 1.06" && m.find("D") && *m.find("D") == "\xC4" && m.findNoCase("d"));
        const auto mb = uibBytes({{"B", "mission text"}, {"C", "c"}});
        const StringTable mission = d2k::formats::parseUib(std::vector<std::uint8_t>(mb.begin(), mb.end()));
        const StringTable mm = lang::mergeTranslation(mission, base, tr);
        CHECK(*mm.find("B") == "mission text");
        // Repeated keys merge by position when both tables repeat them equally often, else stay.
        const auto rb = uibBytes({{"K", "e1"}, {"K", "e2"}, {"C", "x"}, {"C", "y"}, {"C", "z"}});
        const StringTable rbase = d2k::formats::parseUib(std::vector<std::uint8_t>(rb.begin(), rb.end()));
        const auto rt = uibBytes({{"K", "r1"}, {"K", "r2"}, {"C", "TITLE"}, {"C", "w"}});
        const StringTable rtr = d2k::formats::parseUib(std::vector<std::uint8_t>(rt.begin(), rt.end()));
        const StringTable rm = lang::mergeTranslation(rbase, rbase, rtr);
        CHECK(rm.entries[0].second == "r1" && rm.entries[1].second == "r2");
        CHECK(rm.entries[2].second == "x" && rm.entries[3].second == "y" && rm.entries[4].second == "z");
        // The fixes overlay replaces by key (all occurrences), appends new keys, ignores empty values.
        const auto ob = uibBytes({{"B", "\xC2"}, {"K", "fix"}, {"E", "new"}, {"D", ""}});
        const StringTable ov = d2k::formats::parseUib(std::vector<std::uint8_t>(ob.begin(), ob.end()));
        const StringTable o1 = lang::applyOverlay(rtr, ov);
        CHECK(o1.entries[0].second == "fix" && o1.entries[1].second == "fix" && o1.find("E") && *o1.find("E") == "new");
        const StringTable o2 = lang::applyOverlay(tr, ov);
        CHECK(*o2.find("B") == "\xC2" && *o2.find("D") == "\xC4");
        const StringTable om = lang::mergeTranslation(base, base, o2);
        CHECK(*om.find("B") == "\xC2" && *om.find("E") == "new");
    }

    // A translation folder laid out like game/ru-pc, and a Gruntmods-like data folder.
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / uniqueName("d2k_lang");
    writeFile(root / "ru" / "Text" / "Data" / "Ui_data" / "Text.uib", uibBytes({{"Ready", "\xC3\xCE\xD2\xCE\xC2\xCE"}}));
    writeFile(root / "ru" / "Text" / "Data" / "font.fnt", "x");
    writeFile(root / "ru" / "Text" / "Data" / "Bin" / "Font.bin", std::string(256, ' '));
    writeFile(root / "ru" / "Voices" / "data" / "GAMESFX" / "AI_ATACK.AUD", "x");
    writeFile(root / "ru" / "Voices" / "MOVIES" / "A_BR01_E.vqa", "x");
    writeFile(root / "data" / "UI_DATA" / "TextFre.UIB", uibBytes({{"Ready", "PRET"}}));
    writeFile(root / "data" / "GAMESFXFre" / "AI_ATACK.AUD", "x");
    writeFile(root / "data" / "FONTFre.FNT", "x");
    writeFile(root / "data" / "ui_FRE.r16", "x");
    writeFile(root / "data" / "UI_RUS.R16", "x");
    writeFile(root / "movies" / "A_BR01_G.VQA", "x");
    writeFile(root / "movies" / "A_BR02_F.VQA", "");  // 0-byte placeholder: not a French movie
    const lang::Translation t = lang::scanTranslation((root / "ru").string());
    CHECK(t.any() && fs::path(t.textDir).filename() == "Data" && fs::path(t.voicesDir).filename() == "GAMESFX" &&
          fs::path(t.moviesDir).filename() == "MOVIES");
    CHECK(!lang::scanTranslation((root / "nothing").string()).any());
    lang::Sources src;
    src.dataDir = (root / "data").string();
    src.translation = t;
    src.movieDirs = {(root / "movies").string()};
    const auto fr = lang::availability(lang::Id::French, src);
    CHECK(fr.text && fr.voices && !fr.movies && !fr.needs.empty());
    CHECK(lang::availability(lang::Id::German, src).movies && !lang::availability(lang::Id::German, src).text);
    const auto rpc = lang::availability(lang::Id::RussianPc, src);
    CHECK(rpc.text && rpc.voices && rpc.movies && rpc.needs.empty());
    const auto rps = lang::availability(lang::Id::RussianPs1, src);
    CHECK(rps.text && !rps.voices && !rps.movies && rps.needs == "needs the Russian PS1 disc image");
    {
        const lang::Resolved r = lang::resolve({lang::Id::RussianPc, lang::Id::RussianPc, lang::Id::RussianPc}, src);
        CHECK(r.codepage == 1251 && !r.textUib.empty() && !r.fontFnt.empty() && !r.fontBin.empty() && r.fontColFnt.empty());
        CHECK(r.voicesDir == t.voicesDir && r.movieDirs.size() == 1 && r.movieLetter == 'e' && !r.uiR16.empty());
        CHECK(r.textOverlayUib.empty());
        // The Russian text fixes: artpacks/lang/ru-fixes/Text.uib, applied unless switched off.
        writeFile(root / "artpacks" / "lang" / "ru-fixes" / "Text.uib", uibBytes({{"Ready", "\xC3"}}));
        CHECK(lang::findRuFixes({"", (root / "nothing").string()}).empty());
        lang::Sources fx = src;
        fx.ruFixesDir = lang::findRuFixes({"", (root / "artpacks").string()});
        CHECK(!fx.ruFixesDir.empty());
        lang::Selection sel{lang::Id::RussianPc, lang::Id::RussianPc, lang::Id::RussianPc};
        CHECK(!lang::resolve(sel, fx).textOverlayUib.empty());
        sel.ruTextFixes = false;
        CHECK(lang::resolve(sel, fx).textOverlayUib.empty());
        CHECK(lang::resolve({lang::Id::French, lang::Id::French, lang::Id::French}, fx).textOverlayUib.empty());
    }
    {
        // Mixed: French text, Russian (PS1) voices without a disc -> English voices, German movies.
        const lang::Resolved r = lang::resolve({lang::Id::French, lang::Id::RussianPs1, lang::Id::German}, src);
        CHECK(r.sel.text == lang::Id::French && r.codepage == 1252 && !r.fontFnt.empty() && !r.uiR16.empty());
        CHECK(r.sel.voices == lang::Id::English && r.ps1Speech.empty() && r.movieLetter == 'g');
        src.ps1Image = "disc.cue";
        src.ps1Russian = true;
        const lang::Resolved p = lang::resolve({lang::Id::English, lang::Id::RussianPs1, lang::Id::RussianPs1}, src);
        CHECK(p.ps1Speech == "disc.cue" && p.ps1Movies == "disc.cue" && p.voicesDir == t.voicesDir && p.textUib.empty());
    }
    {
        // Русский (AI): offered only with the generated voice set (<pref>/Voices/ru-ai/GAMESFX/*.AUD);
        // Russian text + fonts as Русский (перевод) (+ the fixes overlay), its own voices, the dubbed movies.
        lang::Sources ai = src;
        ai.ps1Image.clear();
        ai.ps1Russian = false;
        ai.aiVoicesRoot = (root / "Voices" / "ru-ai").string();
        ai.aiVoicesDir = lang::findAiVoices(ai.aiVoicesRoot);
        CHECK(ai.aiVoicesDir.empty());
        const auto none = lang::availability(lang::Id::RussianAi, ai);
        CHECK(!none.any() && none.needs.find("tools/voice/gen_ru_ai.py") != std::string::npos);
        {   // Without the voices, ru-ai falls back to English voices (not the Stream dub) and English text.
            const lang::Resolved r = lang::resolve({lang::Id::RussianAi, lang::Id::RussianAi, lang::Id::RussianAi}, ai);
            CHECK(r.sel.voices == lang::Id::English && r.voicesDir.empty() && r.sel.text == lang::Id::English);
        }
        writeFile(root / "Voices" / "ru-ai" / "GAMESFX" / "ZEROSIZE.AUD", "");  // empty files don't count
        CHECK(lang::findAiVoices(ai.aiVoicesRoot).empty());
        writeFile(root / "Voices" / "ru-ai" / "GAMESFX" / "AI_ATACK.AUD", "x");
        writeFile(root / "Voices" / "ru-ai" / "manifest.tsv", "x");
        ai.aiVoicesDir = lang::findAiVoices(ai.aiVoicesRoot);
        CHECK(fs::path(ai.aiVoicesDir).filename() == "GAMESFX");
        CHECK(lang::findAiVoices((root / "Voices" / "ru-ai" / "GAMESFX").string()) == (root / "Voices" / "ru-ai" / "GAMESFX").string());
        CHECK(lang::findAiVoices((root / "nothing").string()).empty());
        const auto a = lang::availability(lang::Id::RussianAi, ai);
        CHECK(a.text && a.voices && a.movies && a.needs.empty());
        CHECK(lang::fromCode("ru-ai") == lang::Id::RussianAi && lang::codepageOf(lang::Id::RussianAi) == 1251);
        CHECK(lang::isRussian(lang::Id::RussianAi) && !lang::isRussian(lang::Id::Spanish));
        CHECK(lang::displayName(lang::Id::RussianAi, 1251).rfind("\xD0\xF3\xF1\xF1\xEA\xE8\xE9 (AI, \xEE\xF0\xE8", 0) == 0);  // "Русский (AI, ори..."
        CHECK(lang::displayName(lang::Id::RussianAi, 1252) == "Russian (AI voices)");
        CHECK(lang::displayName(lang::Id::RussianAi, 1251, true) == "\xD0\xF3\xF1\xF1\xEA\xE8\xE9 (AI)");
        ai.ruFixesDir = lang::findRuFixes({(root / "artpacks").string()});
        lang::Selection sel{lang::Id::RussianAi, lang::Id::RussianAi, lang::Id::RussianAi};
        const lang::Resolved r = lang::resolve(sel, ai);
        const lang::Resolved pc = lang::resolve({lang::Id::RussianPc, lang::Id::RussianPc, lang::Id::RussianPc}, ai);
        CHECK(r.codepage == 1251 && r.textUib == pc.textUib && r.fontFnt == pc.fontFnt && r.fontBin == pc.fontBin && r.uiR16 == pc.uiR16);
        CHECK(!r.textOverlayUib.empty() && r.textOverlayUib == pc.textOverlayUib);
        CHECK(r.voicesDir == ai.aiVoicesDir && r.voicesDir != t.voicesDir && r.ps1Speech.empty());
        CHECK(r.movieDirs.size() == 1 && r.movieDirs[0] == t.moviesDir && r.movieLetter == 'e' && r.ps1Movies.empty());
        sel.ruTextFixes = false;
        CHECK(lang::resolve(sel, ai).textOverlayUib.empty());
        // Voices only (no translation folder): offered, text stays English with a hint.
        lang::Sources vo = ai;
        vo.translation = {};
        const auto av = lang::availability(lang::Id::RussianAi, vo);
        CHECK(av.voices && !av.text && !av.movies && !av.needs.empty());
        const lang::Resolved rv = lang::resolve({lang::Id::RussianAi, lang::Id::RussianAi, lang::Id::RussianAi}, vo);
        CHECK(rv.sel.text == lang::Id::English && rv.codepage == 1252 && rv.voicesDir == ai.aiVoicesDir && rv.movieDirs.empty());
    }
    std::error_code ec;
    fs::remove_all(root, ec);
}

// Each available language with the real data: known keys / fonts / voices resolve.
void testLanguageData(const std::string& data) {
    namespace lang = d2k::formats::lang;
    namespace fs = std::filesystem;
    lang::Sources src;
    src.dataDir = data;
    src.translation = lang::scanTranslation((fs::path(data).parent_path().parent_path() / "ru-pc").string());
    const auto base = d2k::formats::loadUib(data + "/UI_DATA/Text.UIB");
    struct Want { lang::Id id; const char* ready; };
    const Want wants[] = {{lang::Id::English, "READY"}, {lang::Id::French, "PRET"}, {lang::Id::German, "BEREIT"},
                          {lang::Id::Spanish, "LISTO"}, {lang::Id::RussianPc, "\xC3\xCE\xD2\xCE\xC2\xCE"}};  // ГОТОВО
    for (const Want& w : wants) {
        const lang::Availability a = lang::availability(w.id, src);
        if (!a.text) continue;  // e.g. no ru-pc folder next to the game
        const lang::Resolved r = lang::resolve({w.id, w.id, w.id}, src);
        d2k::formats::StringTable t = base;
        if (!r.textUib.empty()) t = lang::mergeTranslation(base, base, d2k::formats::loadUib(r.textUib));
        CHECK(t.find("Ready") && *t.find("Ready") == w.ready);
        CHECK(t.entries.size() >= base.entries.size() && t.entries[0].first == base.entries[0].first);
        CHECK(*t.find("VersionNumber") == *base.find("VersionNumber"));
        const std::string fnt = r.fontFnt.empty() ? data + "/FONT.FNT" : r.fontFnt;
        const std::string bin = r.fontBin.empty() ? data + "/bin/FONT.BIN" : r.fontBin;
        const auto font = d2k::formats::loadFont(fnt, bin);
        CHECK(font.textWidth(*t.find("Ready")) > 20);
        if (r.codepage == 1251) {
            // Cyrillic glyphs, distinct from the Latin look-alikes; a Gruntmods-only key stays English.
            CHECK(font.glyphFor(0xC3) && font.glyphFor(0xC3) != font.glyphFor('G') && font.glyphFor(0xFF));
            CHECK(t.find("Survival") && base.find("Survival") && *t.find("Survival") == *base.find("Survival"));
        }
        const std::string voices = r.voicesDir.empty() ? data + "/GAMESFXEng" : r.voicesDir;
        const std::string atack = d2k::formats::findFileNoCase(voices, "AI_ATACK.AUD");
        CHECK(!atack.empty() && d2k::formats::loadAud(atack).samples.size() > 1000);
        if (w.id != lang::Id::English) CHECK(r.sel.voices == w.id);
    }
}

int main() {
    using namespace d2k::formats;

    // Synthetic: RGB555 conversion expands 5-bit channels to full range.
    CHECK(rgb555ToRgba(0x7FFF) == 0xFFFFFFFFu);
    CHECK(rgb555ToRgba(0x7C00) == 0xFF0000FFu);  // pure red -> R byte 0xFF
    testCommunitySynthetic();
    testLanguageSynthetic();
    testPs1SpeechSynthetic();

    testNsisSynthetic();
    testFormat80();
    testSha256();
    testArtPackManifest();
    testReleaseInfo();
    testZipReader();

    const std::string data = D2K_GAME_DATA_DIR;
    if (!std::filesystem::exists(data + "/BLOXBASE.R16")) {
        std::puts("game data not found, skipping data tests");
        return failures ? 1 : 0;
    }

    auto blox = loadR16(data + "/BLOXBASE.R16");
    CHECK(blox.frames.size() == 800);
    CHECK(blox.frames[0].width == 32 && blox.frames[0].height == 32 && blox.frames[0].bpp == 16);

    auto sprites = loadR16(data + "/DATA.R16");
    CHECK(sprites.frames.size() == 6555);  // incl. 1450 empty entries
    CHECK(!sprites.palettes.empty());

    auto map = loadMap(data + "/maps/2PLAY1.MAP");
    CHECK(map.width == 64 && map.height == 64);
    for (const auto& c : map.cells) CHECK(c.tile < 800);

    // Skirmish maps take their tileset from _SPAWN.MIS; campaign missions have their own.
    auto spawn = loadMis(findFileNoCase(data + "/maps", "_SPAWN.MIS"));
    CHECK(spawn.tileset == "Bloxbgbs" && spawn.tileAttributes == "tileatr3");
    auto h5 = loadMis(findFileNoCase(data + "/maps", "_H5V1.MIS"));
    CHECK(h5.tileset == "BLOXICE");

    // Templates.bin: art tables must account for exactly every DATA.R16 frame.
    auto t = loadTemplates(data + "/bin/Templates.bin");
    const auto& b = t.bin;
    CHECK(b.unitCount == 30 && b.buildingCount == 62);
    CHECK(std::string(b.unitNames[0]) == "LIGHT INF");
    CHECK(std::string(b.buildingNames[0]) == "ATREIDES CONSTRUCTION YARD");
    CHECK(t.buildingAnimationFirstFrame[b.buildingCount - 1] + b.buildingAnimationFrames[b.buildingCount - 1] ==
          static_cast<int>(sprites.frames.size()));

    auto td = loadTileData(data + "/bin/TILEDATA.BIN");
    auto colours = loadColours(data + "/bin/COLOURS.BIN");
    CHECK((colours[0][15] >> 24) == 0xFF);
    (void)td;

    auto armour = loadArmour(data + "/bin/ARMOUR.BIN");
    CHECK(armour.warheadCount == 14 && armour.armourNames[5] == "HEAVY");
    auto speed = loadSpeed(data + "/bin/SPEED.BIN");
    CHECK(speed.speedTypeNames[0] == "Infantry" && speed.values[2][2] > 0.49f && speed.values[2][2] < 0.51f);

    auto ui = loadR16(data + "/UI_ENG.R16", R16Layout::Ui);
    CHECK(ui.frames.size() == 55);

    auto font = loadFont(data + "/FONT.FNT", data + "/bin/FONT.BIN");
    CHECK(font.height == 14 && font.firstChar == 33 && font.glyphFor('A') != nullptr);
    CHECK(font.glyphFor(' ') == nullptr);

    auto rs = SoundArchive::load(data + "/GAMESFX/SOUND.RS");
    CHECK(rs.size() == 57);
    auto samples = loadUib(data + "/UI_DATA/samples.uib");
    CHECK(samples.entries.size() == 327 && samples.entries[1].first == "S_BAZOOKA");
    auto bazooka = rs.clip(samples.entries[1].second + ".wav");  // sample id 1 = S_BAZOOKA
    CHECK(bazooka.sampleRate > 0 && bazooka.samples.size() > 1000);

    auto clip = loadAud(data + "/GAMESFX/A_ESEL1.AUD");
    CHECK(clip.sampleRate == 22050 && clip.samples.size() > 20000);

    // Front-end page scripts (re/notes/campaign-flow.md).
    auto score1 = loadUil(data + "/UI_DATA/score_1.uil");
    const UilWidget* ourUnits = score1.find("ourUnitCasualty");
    CHECK(ourUnits && ourUnits->kind == "TEXTBOX" && ourUnits->suffix == "_L" && ourUnits->x == 375 && ourUnits->y == 40);
    CHECK(ourUnits && ourUnits->field(3) == "Red" && ourUnits->commands().size() == 1);
    const UilWidget* cont = score1.find("continue");
    CHECK(cont && cont->kind == "BUTTON" && cont->field(7) == "rBut4_up" && cont->commands().front() == "BackMenu");
    CHECK(score1.widgets.front().kind == "BACKGROUND" && score1.widgets.front().name == "scoreui");
    auto globe = loadUil(data + "/UI_DATA/Globe.uil");
    CHECK(globe.widgets.size() == 7);  // BACKGROUND, title, 5 buttons (the orphan GROUPBOX body is skipped)
    CHECK(globe.find("next") && globe.find("next")->commands().front() == "NextMission");
    auto menus = parseMenus(readFile(data + "/UI_DATA/menus.uib"));
    CHECK(menus["GLOBE"] == "Globe" && menus["SCORE_SINGLE"] == "score_1" && menus["MVE_ATREIDES1"] == "AMis1");
    auto uiColours = parseUiColours(readFile(data + "/UI_DATA/colours.uib"));
    CHECK(uiColours.size() == 51 && uiColours["JHighLightText"] == 0xcfa76fu && uiColours["White"] == 0xffffffu);

    testNsisInstaller(data);
    testVqaMovie(data);
    testPs1Movie(data);
    testPs1Speech(data);
    testPs1Models(data);

    testCommunityData(data);
    testLanguageData(data);

    std::printf("%s\n", failures ? "FAILED" : "all format tests passed");
    return failures ? 1 : 0;
}
