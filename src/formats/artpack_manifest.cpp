#include "formats/artpack_manifest.hpp"

#include "formats/json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <memory>

namespace d2k::formats {

namespace {

using Json = json::Value;
using json::field;
using json::str;

bool isAbsoluteHttp(const std::string& u) { return u.rfind("https://", 0) == 0 || u.rfind("http://", 0) == 0; }

}  // namespace

bool isSafePackName(const std::string& name) {
    if (name.empty() || name.size() > 64 || name == "." || name == "..") return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
    });
}

std::string resolveUrl(const std::string& base, const std::string& ref) {
    if (ref.empty() || isAbsoluteHttp(ref)) return ref;
    if (const auto colon = ref.find(':'); colon != std::string::npos && colon < ref.find('/')) return ref;  // other scheme
    const std::size_t scheme = base.find("://");
    if (scheme == std::string::npos) return ref;
    const std::string b = base.substr(0, base.find_first_of("?#", scheme + 3));
    const std::size_t pathStart = b.find('/', scheme + 3);
    const std::string origin = pathStart == std::string::npos ? b : b.substr(0, pathStart);
    if (ref[0] == '/') return origin + ref;
    return (pathStart == std::string::npos ? origin + "/" : b.substr(0, b.rfind('/') + 1)) + ref;
}

int compareVersions(const std::string& a, const std::string& b) {
    auto parts = [](const std::string& s) {
        std::vector<long> v;
        long cur = 0;
        bool any = false;
        for (char c : s) {
            if (c >= '0' && c <= '9') {
                cur = cur * 10 + (c - '0');
                any = true;
            } else if (c == '.') {
                v.push_back(cur);
                cur = 0;
                any = false;
            } else {
                break;  // "-beta" etc.
            }
        }
        if (any || !v.empty()) v.push_back(cur);
        return v;
    };
    const auto pa = parts(a), pb = parts(b);
    for (std::size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        const long x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

bool parseArtPackManifest(const std::string& json, const std::string& manifestUrl, ArtPackManifest& out,
                          std::string& error) {
    out = ArtPackManifest{};
    Json root;
    if (!json::parse(json, root, error)) return false;
    if (root.kind != Json::Object) {
        error = "the manifest is not a JSON object";
        return false;
    }
    if (const Json* f = field(root, "format"); f && f->kind == Json::Number) out.format = int(f->num);
    if (out.format != 1) {
        error = "unsupported manifest format " + std::to_string(out.format) + " (a newer OpenD2K is needed)";
        return false;
    }
    out.name = str(root, "name");
    out.title = str(root, "title");
    out.version = str(root, "version");
    out.file = str(root, "file");
    out.sha256 = str(root, "sha256");
    out.minEngine = str(root, "min_engine");
    out.licence = str(root, "licence");
    if (out.licence.empty()) out.licence = str(root, "license");
    out.page = str(root, "page");
    std::transform(out.sha256.begin(), out.sha256.end(), out.sha256.begin(),
                   [](char c) { return char(std::tolower(static_cast<unsigned char>(c))); });
    if (const Json* s = field(root, "size"); s && s->kind == Json::Number && s->num > 0 && s->num < 1e15 &&
                                             std::floor(s->num) == s->num)
        out.size = std::uint64_t(s->num);
    if (const Json* u = field(root, "urls"); u && u->kind == Json::Array)
        for (const Json& e : u->arr)
            if (e.kind == Json::String && !e.str.empty()) out.urls.push_back(resolveUrl(manifestUrl, e.str));
    if (const std::string one = str(root, "url"); !one.empty()) out.urls.push_back(resolveUrl(manifestUrl, one));
    if (out.urls.empty() && !out.file.empty() && out.file.find('/') == std::string::npos)
        out.urls.push_back(resolveUrl(manifestUrl, out.file));
    out.urls.erase(std::remove_if(out.urls.begin(), out.urls.end(), [](const std::string& u) { return !isAbsoluteHttp(u); }),
                   out.urls.end());
    if (const Json* c = field(root, "contains"); c && c->kind == Json::Array)
        for (const Json& e : c->arr)
            if (e.kind == Json::String) out.contains.push_back(e.str);
    if (out.title.empty()) out.title = out.name;
    if (!isSafePackName(out.name)) error = "bad or missing pack name";
    else if (out.version.empty()) error = "missing version";
    else if (out.size == 0) error = "missing size";
    else if (out.sha256.size() != 64 ||
             !std::all_of(out.sha256.begin(), out.sha256.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
        error = "bad sha256";
    else if (out.urls.empty()) error = "no download URL";
    return error.empty();
}

}  // namespace d2k::formats
