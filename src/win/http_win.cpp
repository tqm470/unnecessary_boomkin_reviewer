#include "http_win.h"

#include "winutil.h"

#include <winhttp.h>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_OPTION_DECOMPRESSION
#define WINHTTP_OPTION_DECOMPRESSION 118
#endif
#ifndef WINHTTP_DECOMPRESSION_FLAG_ALL
#define WINHTTP_DECOMPRESSION_FLAG_ALL 3
#endif

namespace ubr {

static std::string winhttpError(DWORD code) {
    switch (code) {
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
            return "Could not resolve the server name. Are you online?";
        case ERROR_WINHTTP_CANNOT_CONNECT:
        case ERROR_WINHTTP_CONNECTION_ERROR:
            return "Could not connect to Warcraft Logs. A firewall or proxy may be blocking it.";
        case ERROR_WINHTTP_TIMEOUT:
            return "The request timed out.";
        case ERROR_WINHTTP_SECURE_FAILURE:
            return "Secure connection failed (TLS). A proxy may be intercepting HTTPS.";
        default:
            return "Network error " + std::to_string(code) + ".";
    }
}

WinHttpClient::WinHttpClient() {
    const wchar_t* agent = L"UnnecessaryBoomkinReviewer/1.0";
    HINTERNET s = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) // Windows before 8.1
        s = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                        WINHTTP_NO_PROXY_BYPASS, 0);
    if (s) {
        WinHttpSetTimeouts(s, 15000, 15000, 60000, 120000);
        DWORD flags = WINHTTP_DECOMPRESSION_FLAG_ALL;
        WinHttpSetOption(s, WINHTTP_OPTION_DECOMPRESSION, &flags, sizeof flags); // best effort
    }
    session_ = s;
}

WinHttpClient::~WinHttpClient() {
    if (session_) WinHttpCloseHandle((HINTERNET)session_);
}

HttpResponse WinHttpClient::post(const std::string& url, const Headers& headers,
                                 const std::string& body) {
    HttpResponse out;
    if (!session_) {
        out.error = "Could not start WinHTTP.";
        return out;
    }
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        out.error = "Bad URL: " + url;
        return out;
    }

    HINTERNET conn = WinHttpConnect((HINTERNET)session_, host, uc.nPort, 0);
    if (!conn) {
        out.error = winhttpError(GetLastError());
        return out;
    }
    DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"POST", path, nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) {
        out.error = winhttpError(GetLastError());
        WinHttpCloseHandle(conn);
        return out;
    }
    std::wstring hdr;
    for (const auto& h : headers) hdr += widen(h.first) + L": " + widen(h.second) + L"\r\n";

    BOOL ok = WinHttpSendRequest(req, hdr.c_str(), (DWORD)-1L, (LPVOID)body.data(),
                                 (DWORD)body.size(), (DWORD)body.size(), 0) &&
              WinHttpReceiveResponse(req, nullptr);
    if (!ok) {
        out.error = winhttpError(GetLastError());
    } else {
        DWORD status = 0, len = sizeof status;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
        out.status = (int)status;
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail)) {
                out.error = winhttpError(GetLastError());
                break;
            }
            if (avail == 0) break;
            size_t old = out.body.size();
            out.body.resize(old + avail);
            DWORD read = 0;
            if (!WinHttpReadData(req, &out.body[old], avail, &read)) {
                out.error = winhttpError(GetLastError());
                break;
            }
            out.body.resize(old + read);
        }
        if (!out.error.empty()) out.status = 0; // a truncated body is not a response
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    return out;
}

} // namespace ubr
