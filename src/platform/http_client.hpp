#pragma once
// Minimal blocking HTTPS GET for the art pack download (platform/artpack_download.*), on each
// platform's own stack - no new libraries in the build:
//   Apple (macOS, iOS)  NSURLSession            http_client_apple.mm
//   Windows             WinHTTP                 http_client_win.cpp
//   Linux / other       libcurl, dlopen'd       http_client_curl.cpp (missing -> available() false,
//                                                the UI shows the manual download link)
// Rules: HTTPS only (plain http only to localhost / 127.0.0.1 / [::1], for tests with a local
// server), redirects followed (GitHub release -> objects.githubusercontent.com) but never to a
// non-HTTPS URL, a generic User-Agent "OpenD2K/<version>" and no other identifying headers or
// cookies. Call from a worker thread; `cancel` is polled.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace d2k::platform::http {

struct Request {
    std::string url;
    std::int64_t resumeFrom = 0;  // > 0: "Range: bytes=N-" (the response says whether it was honoured)
    int connectTimeoutSec = 20;
    int stallTimeoutSec = 60;     // abort when no byte arrives for this long
};

struct Response {
    bool ok = false;         // 2xx and the whole body delivered
    bool cancelled = false;
    int status = 0;          // final HTTP status (after redirects), 0 = no response
    bool partial = false;    // 206: the body continues at resumeFrom
    std::int64_t contentLength = -1;  // of this response's body, -1 unknown
    std::string error;       // human-readable, "" when ok
};

// onStart(status, partial, contentLength) once the final response's headers arrive (false =
// abort); onData(bytes, n) per chunk (false = abort, error "write failed").
struct Callbacks {
    std::function<bool(int, bool, std::int64_t)> onStart;
    std::function<bool(const void*, std::size_t)> onData;
};

Response get(const Request& req, const Callbacks& cb, const std::atomic<bool>& cancel);

// Whether the backend works here (Linux: libcurl could be loaded); `why` explains when not.
bool available(std::string* why = nullptr);
const char* backendName();
// https://..., or http:// to localhost / 127.0.0.1 / [::1].
bool urlAllowed(const std::string& url);
// "OpenD2K/<version>".
std::string userAgent();

// Small GET into memory (the manifest): fails above `maxBytes`.
Response getText(const std::string& url, std::string& body, std::size_t maxBytes, const std::atomic<bool>& cancel);

}  // namespace d2k::platform::http
