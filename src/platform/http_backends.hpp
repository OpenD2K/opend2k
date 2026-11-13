#pragma once
// Internal: the HTTP backends behind platform/http_client.hpp. CMake defines
// D2K_HTTP_HAVE_NATIVE (Apple NSURLSession / Windows WinHTTP / Android HttpURLConnection via JNI) and D2K_HTTP_HAVE_CURL (dlopen'd
// libcurl: Linux, also built on macOS for testing with D2K_HTTP_BACKEND=curl).

#include "platform/http_client.hpp"

namespace d2k::platform::http {

namespace native {
const char* name();
bool available(std::string* why);
Response get(const Request& req, const Callbacks& cb, const std::atomic<bool>& cancel);
}  // namespace native

namespace curl {
bool available(std::string* why);
Response get(const Request& req, const Callbacks& cb, const std::atomic<bool>& cancel);
}  // namespace curl

}  // namespace d2k::platform::http
