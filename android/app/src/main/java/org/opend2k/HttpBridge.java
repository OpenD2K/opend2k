package org.opend2k;

import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;

/**
 * The art pack download's HTTPS GET on Android (src/platform/http_client_android.cpp calls get() through
 * JNI from a worker thread). Same rules as the other platforms' backends (src/platform/http_client.hpp):
 * HTTPS only (plain http only to localhost, for tests), redirects followed by hand and never to a
 * non-HTTPS URL, "Range: bytes=N-" to resume, a generic User-Agent, no cookies or other identifying
 * headers. The body goes to native code in chunks; the native side is polled for a cancel.
 */
public final class HttpBridge {
    private HttpBridge() {}

    // Native callbacks (libmain.so); ctx is the C++ request.
    private static native boolean nativeOnStart(long ctx, int status, boolean partial, long contentLength);
    private static native boolean nativeOnData(long ctx, byte[] data, int n);
    private static native boolean nativeCancelled(long ctx);

    private static boolean allowed(URL u) {
        final String p = u.getProtocol();
        if ("https".equalsIgnoreCase(p)) return true;
        if (!"http".equalsIgnoreCase(p)) return false;
        final String h = u.getHost();
        return "localhost".equalsIgnoreCase(h) || "127.0.0.1".equals(h) || "[::1]".equals(h) || "::1".equals(h);
    }

    /**
     * Returns "" when the whole body was delivered, else the error ("cancelled", "aborted",
     * "write failed", "HTTP 404", ...). The final status / partial / length reach nativeOnStart (2xx only);
     * statusOut[0] gets the final HTTP status (0 = no response).
     */
    public static String get(long ctx, String url, long resumeFrom, int connectMs, int stallMs, String userAgent, int[] statusOut) {
        HttpURLConnection c = null;
        try {
            URL u = new URL(url);
            final boolean startedSecure = "https".equalsIgnoreCase(u.getProtocol());
            for (int hop = 0; ; ++hop) {
                if (!allowed(u) || (startedSecure && !"https".equalsIgnoreCase(u.getProtocol())))
                    return "refused a redirect to a non-https URL";
                c = (HttpURLConnection) u.openConnection();
                c.setInstanceFollowRedirects(false);
                c.setUseCaches(false);
                c.setConnectTimeout(connectMs);
                c.setReadTimeout(stallMs);
                c.setRequestProperty("User-Agent", userAgent);
                c.setRequestProperty("Accept-Encoding", "identity");  // byte ranges of the file itself
                if (resumeFrom > 0) c.setRequestProperty("Range", "bytes=" + resumeFrom + "-");
                final int status = c.getResponseCode();
                statusOut[0] = status;
                if (nativeCancelled(ctx)) return "cancelled";
                if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                    final String loc = c.getHeaderField("Location");
                    c.disconnect();
                    c = null;
                    if (loc == null || hop >= 10) return "too many redirects";
                    u = new URL(u, loc);
                    continue;
                }
                if (status < 200 || status >= 300) return "HTTP " + status;
                if (!nativeOnStart(ctx, status, status == 206, c.getContentLengthLong())) return "aborted";
                final byte[] buf = new byte[256 * 1024];
                try (InputStream in = c.getInputStream()) {
                    for (;;) {
                        if (nativeCancelled(ctx)) return "cancelled";
                        final int n = in.read(buf);
                        if (n < 0) break;  // end of the body
                        if (n > 0 && !nativeOnData(ctx, buf, n)) return "write failed";
                    }
                }
                return "";
            }
        } catch (java.net.SocketTimeoutException e) {
            return "timed out";
        } catch (Exception e) {
            final String m = e.getMessage();
            return e.getClass().getSimpleName() + (m != null ? ": " + m : "");
        } finally {
            if (c != null) c.disconnect();
        }
    }
}
