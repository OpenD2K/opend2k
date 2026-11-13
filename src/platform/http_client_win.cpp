// WinHTTP backend of platform/http_client.hpp (Windows). Synchronous WinHTTP on the caller's worker
// thread; redirects are followed by WinHTTP with its default policy (never https -> http), cookies
// are off, the system proxy settings are used. `cancel` is polled between reads, so a stalled
// connection is dropped after the receive timeout at the latest.
// NOTE: written without a Windows machine at hand (CI off for 0.0.1) - review on the first
// Windows build.

#include "platform/http_backends.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <cwchar>
#include <string>
#include <vector>

namespace d2k::platform::http::native {

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(std::size_t(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string lastError(const char* what) {
    const DWORD e = GetLastError();
    if (e == ERROR_WINHTTP_TIMEOUT) return std::string(what) + ": timed out";
    if (e == ERROR_WINHTTP_NAME_NOT_RESOLVED) return std::string(what) + ": host not found (offline?)";
    if (e == ERROR_WINHTTP_CANNOT_CONNECT) return std::string(what) + ": cannot connect";
    if (e == ERROR_WINHTTP_SECURE_FAILURE) return std::string(what) + ": TLS / certificate error";
    if (e == ERROR_WINHTTP_REDIRECT_FAILED) return std::string(what) + ": redirect refused";
    return std::string(what) + " failed (error " + std::to_string(e) + ")";
}

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};

}  // namespace

const char* name() { return "WinHTTP"; }

bool available(std::string*) { return true; }

Response get(const Request& req, const Callbacks& cb, const std::atomic<bool>& cancel) {
    Response r;
    const std::wstring wurl = widen(req.url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = DWORD(-1);
    if (!WinHttpCrackUrl(wurl.c_str(), DWORD(wurl.size()), 0, &uc)) {
        r.error = "bad URL";
        return r;
    }
    const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (path.empty()) path = L"/";
    const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;

    const std::wstring ua = widen(userAgent());
    Handle session;
    // WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY needs Windows 8.1; fall back to the WinHTTP default.
    session.h = WinHttpOpen(ua.c_str(), 4 /* WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY */, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h)
        session.h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) {
        r.error = lastError("WinHttpOpen");
        return r;
    }
    const int connectMs = req.connectTimeoutSec * 1000, stallMs = req.stallTimeoutSec * 1000;
    WinHttpSetTimeouts(session.h, connectMs, connectMs, stallMs, stallMs);
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(session.h, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof policy);
    // TLS 1.2+ (older Windows 7/8 WinHTTP defaults lack it; GitHub requires it).
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    if (!WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
    }

    Handle connect;
    connect.h = WinHttpConnect(session.h, host.c_str(), uc.nPort, 0);
    if (!connect.h) {
        r.error = lastError("connect");
        return r;
    }
    Handle request;
    request.h = WinHttpOpenRequest(connect.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request.h) {
        r.error = lastError("request");
        return r;
    }
    DWORD features = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    WinHttpSetOption(request.h, WINHTTP_OPTION_DISABLE_FEATURE, &features, sizeof features);
    if (req.resumeFrom > 0) {
        const std::wstring range = L"Range: bytes=" + std::to_wstring(req.resumeFrom) + L"-";
        WinHttpAddRequestHeaders(request.h, range.c_str(), DWORD(-1), WINHTTP_ADDREQ_FLAG_ADD);
    }
    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        r.error = lastError("send");
        return r;
    }
    if (!WinHttpReceiveResponse(request.h, nullptr)) {
        r.error = lastError("receive");
        return r;
    }
    if (cancel.load()) {
        r.cancelled = true;
        r.error = "cancelled";
        return r;
    }
    DWORD status = 0, len = sizeof status;
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
                        &len, WINHTTP_NO_HEADER_INDEX);
    r.status = int(status);
    r.partial = status == 206;
    {
        wchar_t buf[32] = {};
        DWORD blen = sizeof buf;
        if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, buf, &blen,
                                WINHTTP_NO_HEADER_INDEX))
            r.contentLength = std::int64_t(_wcstoui64(buf, nullptr, 10));
    }
    // The final URL after redirects must still be https (the policy above already ensures it for
    // https starts; a plain-http start is only the localhost test server).
    {
        DWORD ulen = 0;
        WinHttpQueryOption(request.h, WINHTTP_OPTION_URL, nullptr, &ulen);
        if (ulen > 0) {
            std::vector<wchar_t> u(ulen / sizeof(wchar_t) + 1, L'\0');
            if (WinHttpQueryOption(request.h, WINHTTP_OPTION_URL, u.data(), &ulen) && secure &&
                _wcsnicmp(u.data(), L"https://", 8) != 0) {
                r.error = "refused a redirect to a non-https URL";
                return r;
            }
        }
    }
    if (status < 200 || status >= 300) {
        r.error = "HTTP " + std::to_string(status);
        return r;
    }
    if (cb.onStart && !cb.onStart(r.status, r.partial, r.contentLength)) {
        r.error = "aborted";
        return r;
    }
    std::vector<char> buf(256 * 1024);
    for (;;) {
        if (cancel.load()) {
            r.cancelled = true;
            r.error = "cancelled";
            return r;
        }
        DWORD got = 0;
        if (!WinHttpReadData(request.h, buf.data(), DWORD(buf.size()), &got)) {
            r.error = lastError("read");
            return r;
        }
        if (got == 0) break;  // end of the body
        if (cb.onData && !cb.onData(buf.data(), got)) {
            r.error = "write failed";
            return r;
        }
    }
    r.ok = true;
    return r;
}

}  // namespace d2k::platform::http::native
