package com.lionsarmor.x16wifi;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;

/**
 * Called from native code (x16-emulator/src/esp32wifi.c) via JNI to perform
 * the WiFi card's AT&G command - a plain HTTP(S) GET. Desktop builds shell
 * out to the "curl" command-line tool for this; there's no such binary
 * inside an Android app's sandbox, so this does the same job with
 * Android's own HttpURLConnection instead. That also means no vendored TLS
 * library or CA bundle is needed here - the system's own, already-current
 * trust store handles HTTPS the normal Android way.
 *
 * This class's package must stay com.lionsarmor.x16wifi regardless of
 * which specific app a given build bundles: the Gradle "namespace" (Java
 * package) is fixed even though "applicationId" (the installed package
 * name) varies per bundled app - see android/bundle-app-android.sh. The
 * native side's JNI FindClass() call depends on this path being stable.
 */
public class HttpBridge {
    /**
     * Returns the response body, or null if the request never completed
     * (bad URL, DNS failure, connection refused, timeout). Matches curl's
     * own behavior of returning whatever body came back even for a non-2xx
     * HTTP status (e.g. a 404 page) rather than treating that as failure -
     * the emulator side already relays that HTTP status transparently to
     * the guest program either way.
     */
    public static byte[] httpGet(String urlString, int timeoutMs) {
        HttpURLConnection conn = null;
        try {
            URL url = new URL(urlString);
            conn = (HttpURLConnection) url.openConnection();
            conn.setConnectTimeout(timeoutMs);
            conn.setReadTimeout(timeoutMs);
            conn.setRequestMethod("GET");
            conn.setInstanceFollowRedirects(true);

            int code = conn.getResponseCode();
            InputStream in;
            try {
                in = conn.getInputStream();
            } catch (IOException e) {
                // 4xx/5xx: HttpURLConnection routes the body through
                // getErrorStream() instead of throwing it away.
                in = conn.getErrorStream();
            }
            if (in == null) {
                return null;
            }

            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[16 * 1024];
            int n;
            while ((n = in.read(buffer)) != -1) {
                out.write(buffer, 0, n);
            }
            in.close();
            return out.toByteArray();
        } catch (IOException | RuntimeException e) {
            return null;
        } finally {
            if (conn != null) {
                conn.disconnect();
            }
        }
    }
}
