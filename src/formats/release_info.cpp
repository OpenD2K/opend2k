#include "formats/release_info.hpp"

#include "formats/artpack_manifest.hpp"  // resolveUrl
#include "formats/json.hpp"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>

namespace d2k::formats {

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isAbsoluteHttp(const std::string& u) { return u.rfind("https://", 0) == 0 || u.rfind("http://", 0) == 0; }

std::string stripV(const std::string& s) {
    return !s.empty() && (s[0] == 'v' || s[0] == 'V') ? s.substr(1) : s;
}

bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// Numeric strings compared as numbers without overflow ("0012" = "12").
int compareNumeric(std::string a, std::string b) {
    auto strip = [](std::string& s) {
        const std::size_t i = s.find_first_not_of('0');
        s = i == std::string::npos ? std::string() : s.substr(i);
    };
    strip(a);
    strip(b);
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    return a == b ? 0 : (a < b ? -1 : 1);
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t p = s.find(sep, start);
        out.push_back(s.substr(start, p == std::string::npos ? std::string::npos : p - start));
        if (p == std::string::npos) break;
        start = p + 1;
    }
    return out;
}

// The name's tokens between '-', '.' and ' ' (lower case; '_' stays: "x86_64").
std::vector<std::string> tokens(const std::string& name) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : lower(name)) {
        if (c == '-' || c == '.' || c == ' ') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string normArch(const std::string& t) {
    if (t == "arm64" || t == "aarch64") return "arm64";
    if (t == "x86_64" || t == "x64" || t == "amd64") return "x86_64";
    if (t == "x86" || t == "i386" || t == "i686" || t == "win32") return "x86";
    if (t == "armv7" || t == "armeabi" || t == "arm") return "arm";
    return {};
}

std::string osOf(const std::vector<std::string>& toks) {
    for (const auto& t : toks) {
        if (t == "macos" || t == "mac" || t == "osx") return "macos";
        if (t == "windows" || t == "win" || t == "win64" || t == "win32") return "windows";
        if (t == "linux") return "linux";
        if (t == "android") return "android";
        if (t == "ios" || t == "ipados") return "ios";
    }
    return {};
}

}  // namespace

int compareSemver(const std::string& a, const std::string& b) {
    auto parts = [](const std::string& v, std::vector<std::string>& nums, std::string& pre) {
        std::string s = stripV(v);
        if (const auto plus = s.find('+'); plus != std::string::npos) s.resize(plus);
        if (const auto dash = s.find('-'); dash != std::string::npos) {
            pre = s.substr(dash + 1);
            s.resize(dash);
        }
        for (const auto& p : split(s, '.')) {
            std::string digits;
            for (char c : p) {
                if (c < '0' || c > '9') break;
                digits += c;
            }
            nums.push_back(digits);
        }
    };
    std::vector<std::string> na, nb;
    std::string pa, pb;
    parts(a, na, pa);
    parts(b, nb, pb);
    for (std::size_t i = 0; i < std::max(na.size(), nb.size()); ++i) {
        const int c = compareNumeric(i < na.size() ? na[i] : "", i < nb.size() ? nb[i] : "");
        if (c) return c;
    }
    if (pa.empty() || pb.empty()) return pa.empty() == pb.empty() ? 0 : (pa.empty() ? 1 : -1);
    const auto ia = split(pa, '.'), ib = split(pb, '.');
    for (std::size_t i = 0; i < std::min(ia.size(), ib.size()); ++i) {
        const bool da = allDigits(ia[i]), db = allDigits(ib[i]);
        int c = 0;
        if (da && db) c = compareNumeric(ia[i], ib[i]);
        else if (da != db) c = da ? -1 : 1;  // numeric identifiers sort first
        else c = ia[i] == ib[i] ? 0 : (ia[i] < ib[i] ? -1 : 1);
        if (c) return c;
    }
    return ia.size() == ib.size() ? 0 : (ia.size() < ib.size() ? -1 : 1);
}

bool parseReleaseInfo(const std::string& text, const std::string& sourceUrl, ReleaseInfo& out, std::string& error) {
    out = ReleaseInfo{};
    json::Value root;
    if (!json::parse(text, root, error)) return false;
    if (root.kind != json::Value::Object) {
        error = "the release info is not a JSON object";
        return false;
    }
    out.tag = json::str(root, "tag_name");
    if (out.tag.empty()) {
        const std::string msg = json::str(root, "message");  // GitHub: {"message": "Not Found", ...}
        error = msg.empty() ? std::string("no tag_name") : "GitHub: " + msg;
        return false;
    }
    out.version = stripV(out.tag);
    if (out.version.empty() || !std::isdigit(static_cast<unsigned char>(out.version[0]))) {
        error = "the tag \"" + out.tag + "\" is not a version";
        return false;
    }
    out.name = json::str(root, "name");
    out.page = json::str(root, "html_url");
    if (!isAbsoluteHttp(out.page)) out.page.clear();
    out.publishedAt = json::str(root, "published_at");
    if (const json::Value* v = json::field(root, "prerelease"); v && v->kind == json::Value::Bool) out.prerelease = v->b;
    if (const json::Value* v = json::field(root, "draft"); v && v->kind == json::Value::Bool) out.draft = v->b;
    if (const json::Value* a = json::field(root, "assets"); a && a->kind == json::Value::Array) {
        for (const json::Value& e : a->arr) {
            if (e.kind != json::Value::Object) continue;
            ReleaseAsset as;
            as.name = json::str(e, "name");
            as.url = resolveUrl(sourceUrl, json::str(e, "browser_download_url"));
            if (const json::Value* s = json::field(e, "size");
                s && s->kind == json::Value::Number && s->num > 0 && s->num < 1e15 && std::floor(s->num) == s->num)
                as.size = std::uint64_t(s->num);
            if (as.name.empty() || !isAbsoluteHttp(as.url)) continue;
            out.assets.push_back(std::move(as));
        }
    }
    return true;
}

PlatformId currentPlatform() {
    PlatformId p;
#if defined(__APPLE__) && defined(TARGET_OS_IOS) && TARGET_OS_IOS
    p.os = "ios";
#elif defined(__APPLE__)
    p.os = "macos";
#elif defined(__ANDROID__)
    p.os = "android";
#elif defined(_WIN32)
    p.os = "windows";
#elif defined(__linux__)
    p.os = "linux";
#else
    p.os = "other";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    p.arch = "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    p.arch = "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    p.arch = "x86";
#elif defined(__arm__) || defined(_M_ARM)
    p.arch = "arm";
#endif
    return p;
}

int selectReleaseAsset(const ReleaseInfo& r, const PlatformId& p) {
    // Archive preference per OS (higher = better).
    auto extScore = [&](const std::string& n) -> int {
        if (p.os == "macos") return endsWith(n, ".zip") ? 3 : endsWith(n, ".dmg") ? 2 : 0;
        if (p.os == "windows") return endsWith(n, ".zip") ? 3 : (endsWith(n, ".exe") || endsWith(n, ".msi")) ? 2 : 0;
        if (p.os == "linux")
            return endsWith(n, ".tar.gz") || endsWith(n, ".tgz") ? 3 : endsWith(n, ".tar.xz") ? 2 : endsWith(n, ".appimage") ? 1 : 0;
        if (p.os == "android") return endsWith(n, ".apk") ? 3 : 0;
        return 0;  // iOS: no sideloadable download
    };
    int best = -1, bestScore = 0;
    for (std::size_t i = 0; i < r.assets.size(); ++i) {
        const std::string n = lower(r.assets[i].name);
        if (n.rfind("opend2k-", 0) != 0) continue;
        const auto toks = tokens(n);
        if (std::find(toks.begin(), toks.end(), "pack") != toks.end() ||
            std::find(toks.begin(), toks.end(), "artpack") != toks.end())
            continue;
        if (osOf(toks) != p.os) continue;
        const int ext = extScore(n);
        if (ext == 0) continue;
        // This arch named: 3; "universal": 2; no arch: 1; only other arches named: skipped.
        int archScore = 1;
        bool otherArch = false;
        for (const auto& t : toks) {
            if (t == "universal") archScore = std::max(archScore, 2);
            const std::string a = normArch(t);
            if (a.empty()) continue;
            if (!p.arch.empty() && a == p.arch) archScore = 3;
            else otherArch = true;
        }
        if (otherArch && archScore != 3) continue;
        const int score = archScore * 10 + ext;
        if (score > bestScore) {
            bestScore = score;
            best = int(i);
        }
    }
    return best;
}

}  // namespace d2k::formats
