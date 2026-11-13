#include "platform/http_client.hpp"

#include "platform/http_backends.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#ifndef D2K_VERSION
#define D2K_VERSION "0.0.0"
#endif

namespace d2k::platform::http {

namespace {

std::string lowerStr(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The backend for this run: the platform's own; D2K_HTTP_BACKEND=curl picks libcurl on macOS /
// Linux too (to test that path on a Mac).
#if defined(D2K_HTTP_HAVE_CURL)
enum class Backend { Native, Curl };
Backend backend() {
#if defined(D2K_HTTP_HAVE_CURL) && defined(D2K_HTTP_HAVE_NATIVE)
    if (const char* e = std::getenv("D2K_HTTP_BACKEND"); e && std::string(e) == "curl") return Backend::Curl;
    return Backend::Native;
#elif defined(D2K_HTTP_HAVE_CURL)
    return Backend::Curl;
#else
    return Backend::Native;
#endif
}
#endif

}  // namespace

bool urlAllowed(const std::string& url) {
    const std::string u = lowerStr(url);
    if (u.rfind("https://", 0) == 0) return u.size() > 8;
    if (u.rfind("http://", 0) != 0) return false;
    std::string host = u.substr(7, u.find_first_of("/?#", 7) - 7);
    if (const auto at = host.rfind('@'); at != std::string::npos) return false;  // no credentials in URLs
    if (!host.empty() && host[0] == '[') host = host.substr(0, host.find(']') + 1);
    else host = host.substr(0, host.find(':'));
    return host == "localhost" || host == "127.0.0.1" || host == "[::1]";
}

std::string userAgent() { return std::string("OpenD2K/") + D2K_VERSION; }

const char* backendName() {
#if defined(D2K_HTTP_HAVE_CURL)
    if (backend() == Backend::Curl) return "libcurl";
#endif
#if defined(D2K_HTTP_HAVE_NATIVE)
    return native::name();
#else
    return "none";
#endif
}

bool available(std::string* why) {
#if defined(D2K_HTTP_HAVE_CURL)
    if (backend() == Backend::Curl) return curl::available(why);
#endif
#if defined(D2K_HTTP_HAVE_NATIVE)
    return native::available(why);
#else
    if (why) *why = "no HTTPS support in this build";
    return false;
#endif
}

Response get(const Request& req, const Callbacks& cb, const std::atomic<bool>& cancel) {
    if (!urlAllowed(req.url)) {
        Response r;
        r.error = "refused: not an https:// URL";
        return r;
    }
#if defined(D2K_HTTP_HAVE_CURL)
    if (backend() == Backend::Curl) return curl::get(req, cb, cancel);
#endif
#if defined(D2K_HTTP_HAVE_NATIVE)
    return native::get(req, cb, cancel);
#else
    (void)cb;
    (void)cancel;
    Response r;
    r.error = "no HTTPS support in this build";
    return r;
#endif
}

Response getText(const std::string& url, std::string& body, std::size_t maxBytes, const std::atomic<bool>& cancel) {
    body.clear();
    bool tooBig = false;
    Callbacks cb;
    cb.onStart = [&](int, bool, std::int64_t len) {
        if (len > 0 && std::uint64_t(len) > maxBytes) {
            tooBig = true;
            return false;
        }
        return true;
    };
    cb.onData = [&](const void* p, std::size_t n) {
        if (body.size() + n > maxBytes) {
            tooBig = true;
            return false;
        }
        body.append(static_cast<const char*>(p), n);
        return true;
    };
    Request req;
    req.url = url;
    req.stallTimeoutSec = 30;
    Response r = get(req, cb, cancel);
    if (tooBig) {
        r.ok = false;
        r.error = "response too large";
    }
    return r;
}

}  // namespace d2k::platform::http
