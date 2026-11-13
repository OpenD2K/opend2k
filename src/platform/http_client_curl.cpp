// libcurl backend of platform/http_client.hpp, loaded at run time (dlopen) so the program does not
// need libcurl to start: without it the art pack page shows the manual download link. The few
// option / info numbers are libcurl's stable ABI values (curl/curl.h: CURLOPT(name, type, n) =
// type base + n), so no curl headers are needed to build.

#include "platform/http_backends.hpp"

#include <dlfcn.h>
#include <strings.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace d2k::platform::http::curl {

namespace {

using CURL = void;
using CurlCode = int;
using EasyInit = CURL* (*)();
using EasySetopt = CurlCode (*)(CURL*, int, ...);
using EasyPerform = CurlCode (*)(CURL*);
using EasyCleanup = void (*)(CURL*);
using EasyGetinfo = CurlCode (*)(CURL*, int, ...);
using EasyStrerror = const char* (*)(CurlCode);
using GlobalInit = CurlCode (*)(long);

constexpr int kLong = 0, kObject = 10000, kFunction = 20000, kOffT = 30000;
constexpr int OPT_WRITEDATA = kObject + 1, OPT_URL = kObject + 2, OPT_ERRORBUFFER = kObject + 10,
              OPT_WRITEFUNCTION = kFunction + 11, OPT_USERAGENT = kObject + 18, OPT_LOW_SPEED_LIMIT = kLong + 19,
              OPT_LOW_SPEED_TIME = kLong + 20, OPT_NOPROGRESS = kLong + 43, OPT_FOLLOWLOCATION = kLong + 52,
              OPT_XFERINFODATA = kObject + 57, OPT_MAXREDIRS = kLong + 68, OPT_CONNECTTIMEOUT = kLong + 78,
              OPT_HEADERFUNCTION = kFunction + 79, OPT_HEADERDATA = kObject + 29, OPT_NOSIGNAL = kLong + 99,
              OPT_RESUME_FROM_LARGE = kOffT + 116, OPT_PROTOCOLS = kLong + 181, OPT_REDIR_PROTOCOLS = kLong + 182,
              OPT_XFERINFOFUNCTION = kFunction + 219, OPT_PROTOCOLS_STR = kObject + 318,
              OPT_REDIR_PROTOCOLS_STR = kObject + 319;
constexpr int INFO_RESPONSE_CODE = 0x200000 + 2;
constexpr long PROTO_HTTP = 1 << 0, PROTO_HTTPS = 1 << 1;
constexpr int E_OK = 0, E_WRITE_ERROR = 23, E_ABORTED_BY_CALLBACK = 42;

struct Lib {
    void* h = nullptr;
    EasyInit init = nullptr;
    EasySetopt setopt = nullptr;
    EasyPerform perform = nullptr;
    EasyCleanup cleanup = nullptr;
    EasyGetinfo getinfo = nullptr;
    EasyStrerror strerror = nullptr;
    std::string error;
};

const Lib& lib() {
    static Lib l;
    static std::once_flag once;
    std::call_once(once, [] {
        const char* names[] = {
#if defined(__APPLE__)
            "/usr/lib/libcurl.4.dylib", "libcurl.4.dylib", "libcurl.dylib",
#else
            "libcurl.so.4", "libcurl-gnutls.so.4", "libcurl-nss.so.4", "libcurl.so",
#endif
        };
        for (const char* n : names)
            if ((l.h = dlopen(n, RTLD_NOW | RTLD_LOCAL))) break;
        if (!l.h) {
            l.error = "libcurl not found (install libcurl4 / libcurl)";
            return;
        }
        l.init = reinterpret_cast<EasyInit>(dlsym(l.h, "curl_easy_init"));
        l.setopt = reinterpret_cast<EasySetopt>(dlsym(l.h, "curl_easy_setopt"));
        l.perform = reinterpret_cast<EasyPerform>(dlsym(l.h, "curl_easy_perform"));
        l.cleanup = reinterpret_cast<EasyCleanup>(dlsym(l.h, "curl_easy_cleanup"));
        l.getinfo = reinterpret_cast<EasyGetinfo>(dlsym(l.h, "curl_easy_getinfo"));
        l.strerror = reinterpret_cast<EasyStrerror>(dlsym(l.h, "curl_easy_strerror"));
        auto ginit = reinterpret_cast<GlobalInit>(dlsym(l.h, "curl_global_init"));
        if (!l.init || !l.setopt || !l.perform || !l.cleanup || !l.getinfo || !ginit) {
            l.error = "libcurl is missing functions";
            l.init = nullptr;
            return;
        }
        ginit(3);  // CURL_GLOBAL_ALL (once, before any other thread uses curl)
    });
    return l;
}

struct Ctx {
    const Callbacks* cb;
    const std::atomic<bool>* cancel;
    long status = 0;
    bool started = false, aborted = false, writeFailed = false, partial = false;
    std::int64_t contentLength = -1;
    std::int64_t resumeFrom = 0;
};

// Header lines of every response in the redirect chain: track the last status line and its
// Content-Length; onStart fires with the first body byte (only the final response has one).
std::size_t onHeader(char* p, std::size_t size, std::size_t n, void* user) {
    auto* c = static_cast<Ctx*>(user);
    const std::string line(p, size * n);
    if (line.rfind("HTTP/", 0) == 0) {
        const auto sp = line.find(' ');
        c->status = sp == std::string::npos ? 0 : std::strtol(line.c_str() + sp + 1, nullptr, 10);
        c->contentLength = -1;
    } else if (line.size() > 15 && strncasecmp(line.c_str(), "content-length:", 15) == 0) {
        c->contentLength = std::strtoll(line.c_str() + 15, nullptr, 10);
    }
    return size * n;
}

bool start(Ctx* c) {
    if (c->started) return true;
    c->started = true;
    c->partial = c->status == 206;
    if (c->cb->onStart && !c->cb->onStart(int(c->status), c->partial, c->contentLength)) {
        c->aborted = true;
        return false;
    }
    return true;
}

std::size_t onWrite(char* p, std::size_t size, std::size_t n, void* user) {
    auto* c = static_cast<Ctx*>(user);
    if (c->cancel->load()) return 0;
    if (c->status < 200 || c->status >= 300) return size * n;  // error body: drop (FAILONERROR off)
    if (!start(c)) return 0;
    if (c->cb->onData && !c->cb->onData(p, size * n)) {
        c->writeFailed = true;
        return 0;
    }
    return size * n;
}

int onProgress(void* user, std::int64_t, std::int64_t, std::int64_t, std::int64_t) {
    return static_cast<Ctx*>(user)->cancel->load() ? 1 : 0;
}

}  // namespace

bool available(std::string* why) {
    const Lib& l = lib();
    if (!l.init && why) *why = l.error;
    return l.init != nullptr;
}

Response get(const Request& req, const Callbacks& cb, const std::atomic<bool>& cancel) {
    Response r;
    const Lib& l = lib();
    if (!l.init) {
        r.error = l.error;
        return r;
    }
    CURL* h = l.init();
    if (!h) {
        r.error = "curl_easy_init failed";
        return r;
    }
    Ctx c{&cb, &cancel};
    c.resumeFrom = req.resumeFrom;
    char errbuf[256] = {};
    const std::string ua = userAgent();
    const bool local = req.url.rfind("http://", 0) == 0;  // urlAllowed: localhost only
    l.setopt(h, OPT_URL, req.url.c_str());
    l.setopt(h, OPT_ERRORBUFFER, errbuf);
    l.setopt(h, OPT_USERAGENT, ua.c_str());
    l.setopt(h, OPT_FOLLOWLOCATION, 1L);
    l.setopt(h, OPT_MAXREDIRS, 10L);
    // Protocols: https only (redirects too); the local test server is plain http.
    if (l.setopt(h, OPT_PROTOCOLS_STR, local ? "http,https" : "https") != E_OK)
        l.setopt(h, OPT_PROTOCOLS, local ? (PROTO_HTTP | PROTO_HTTPS) : PROTO_HTTPS);
    if (l.setopt(h, OPT_REDIR_PROTOCOLS_STR, "https") != E_OK) l.setopt(h, OPT_REDIR_PROTOCOLS, PROTO_HTTPS);
    l.setopt(h, OPT_CONNECTTIMEOUT, long(req.connectTimeoutSec));
    l.setopt(h, OPT_LOW_SPEED_LIMIT, 1L);
    l.setopt(h, OPT_LOW_SPEED_TIME, long(req.stallTimeoutSec));
    l.setopt(h, OPT_NOSIGNAL, 1L);
    l.setopt(h, OPT_HEADERFUNCTION, &onHeader);
    l.setopt(h, OPT_HEADERDATA, &c);
    l.setopt(h, OPT_WRITEFUNCTION, &onWrite);
    l.setopt(h, OPT_WRITEDATA, &c);
    l.setopt(h, OPT_NOPROGRESS, 0L);
    l.setopt(h, OPT_XFERINFOFUNCTION, &onProgress);
    l.setopt(h, OPT_XFERINFODATA, &c);
    if (req.resumeFrom > 0) l.setopt(h, OPT_RESUME_FROM_LARGE, static_cast<std::int64_t>(req.resumeFrom));
    const CurlCode rc = l.perform(h);
    long code = 0;
    l.getinfo(h, INFO_RESPONSE_CODE, &code);
    l.cleanup(h);
    r.status = int(code ? code : c.status);
    r.partial = c.partial;
    r.contentLength = c.contentLength;
    if (rc == E_OK && r.status >= 200 && r.status < 300) {
        if (!start(&c)) {  // empty body
            r.error = "aborted";
            return r;
        }
        r.ok = true;
        return r;
    }
    if (cancel.load()) {
        r.cancelled = true;
        r.error = "cancelled";
    } else if (c.writeFailed) {
        r.error = "write failed";
    } else if (c.aborted) {
        r.error = "aborted";
    } else if (rc == E_OK) {
        r.error = "HTTP " + std::to_string(r.status);
    } else {
        r.error = errbuf[0] ? errbuf : (l.strerror ? l.strerror(rc) : "network error");
        (void)E_WRITE_ERROR;
        (void)E_ABORTED_BY_CALLBACK;
    }
    return r;
}

}  // namespace d2k::platform::http::curl
