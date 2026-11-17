#define WIN32_LEAN_AND_MEAN
#include "tmdb_metadata.h"
#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <wincrypt.h>
#include <strsafe.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shell32.lib")

namespace {
static const wchar_t *kHost = L"api.themoviedb.org";
static const wchar_t *kTokenFile = L"tmdb-token.bin";
static const DWORD kConnectTimeout = 5000;
static const DWORD kSendTimeout = 8000;
static const DWORD kReceiveTimeout = 12000;

bool DataPath(wchar_t *out, size_t count, const wchar_t *leaf) {
    wchar_t appData[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE,
                                nullptr, SHGFP_TYPE_CURRENT, appData))) return false;
    if (FAILED(StringCchPrintfW(out, count, L"%s\\ABMAVPlayer\\%s", appData, leaf))) return false;
    wchar_t dir[MAX_PATH] = {};
    if (FAILED(StringCchPrintfW(dir, _countof(dir), L"%s\\ABMAVPlayer", appData))) return false;
    if (!CreateDirectoryW(dir, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    return true;
}
std::wstring UrlEncode(const wchar_t *value) {
    if (!value) return {};
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return {};
    std::string utf8((size_t)bytes, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, &utf8[0], bytes, nullptr, nullptr);
    utf8.pop_back();
    static const char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (unsigned char c : utf8) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') encoded.push_back((char)c);
        else { encoded.push_back('%'); encoded.push_back(hex[c >> 4]); encoded.push_back(hex[c & 15]); }
    }
    return std::wstring(encoded.begin(), encoded.end());
}
bool ReadToken(std::wstring &token) {
    wchar_t path[MAX_PATH] = {};
    if (!DataPath(path, _countof(path), kTokenFile)) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 65536;
    std::vector<BYTE> encrypted(ok ? (size_t)size.QuadPart : 0);
    DWORD read = 0;
    if (ok) ok = ReadFile(file, encrypted.data(), (DWORD)encrypted.size(), &read, nullptr) && read == encrypted.size();
    CloseHandle(file);
    if (!ok) return false;
    DATA_BLOB input = { (DWORD)encrypted.size(), encrypted.data() }, output = {};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return false;
    int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPCCH)output.pbData, (int)output.cbData, nullptr, 0);
    if (chars > 0) {
        token.resize((size_t)chars);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPCCH)output.pbData, (int)output.cbData, &token[0], chars);
        while (!token.empty() && token.back() == L'\0') token.pop_back();
    }
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return !token.empty();
}
bool HttpRequest(const std::wstring &path, std::vector<BYTE> &body, DWORD timeoutMs) {
    std::wstring token;
    if (!ReadToken(token)) return false;
    HINTERNET session = WinHttpOpen(L"ABM-AV-Player/1.1 (TMDB metadata client)",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, (int)kConnectTimeout, (int)kConnectTimeout,
                       (int)kSendTimeout, (int)(timeoutMs ? timeoutMs : kReceiveTimeout));
    HINTERNET connect = WinHttpConnect(session, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    std::wstring auth = L"Authorization: Bearer " + token + L"\r\nAccept: application/json\r\n";
    BOOL sent = request && WinHttpSendRequest(request, auth.c_str(), (DWORD)-1L,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);
    DWORD status = 0, statusSize = sizeof(status);
    if (sent) sent = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                         WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (sent && status >= 200 && status < 300) {
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available)) { sent = FALSE; break; }
            if (!available) break;
            if (body.size() + available > 8 * 1024 * 1024) { sent = FALSE; break; }
            size_t old = body.size(); body.resize(old + available);
            DWORD got = 0;
            if (!WinHttpReadData(request, body.data() + old, available, &got)) { sent = FALSE; break; }
            body.resize(old + got);
            if (!got) break;
        }
    } else sent = FALSE;
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    SecureZeroMemory(token.data(), token.size() * sizeof(wchar_t));
    return sent && !body.empty();
}
bool JsonToWide(const std::vector<BYTE> &bytes, wchar_t *out, size_t capacity) {
    if (!out || capacity == 0 || bytes.empty()) return false;
    int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                    (LPCCH)bytes.data(), (int)bytes.size(), nullptr, 0);
    if (chars <= 0 || (size_t)chars >= capacity) return false;
    int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                      (LPCCH)bytes.data(), (int)bytes.size(), out, (int)capacity - 1);
    if (written <= 0) return false;
    out[written] = L'\0';
    return true;
}
}

extern "C" BOOL ABM_TMDB_SetReadToken(const wchar_t *token) {
    if (!token) return FALSE;
    while (*token && iswspace(*token)) ++token;
    if (wcslen(token) > 4096) return FALSE;
    wchar_t path[MAX_PATH] = {};
    if (!DataPath(path, _countof(path), kTokenFile)) return FALSE;
    if (!*token) return DeleteFileW(path) || GetLastError() == ERROR_FILE_NOT_FOUND;
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, token, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return FALSE;
    std::vector<BYTE> plain((size_t)bytes);
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, token, -1, (LPSTR)plain.data(), bytes, nullptr, nullptr)) return FALSE;
    DATA_BLOB input = { (DWORD)plain.size(), plain.data() }, encrypted = {};
    BOOL ok = CryptProtectData(&input, L"ABM AV Player TMDB credential", nullptr, nullptr, nullptr,
                               CRYPTPROTECT_UI_FORBIDDEN, &encrypted);
    SecureZeroMemory(plain.data(), plain.size());
    if (!ok) return FALSE;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, nullptr);
    DWORD written = 0;
    ok = file != INVALID_HANDLE_VALUE && WriteFile(file, encrypted.pbData, encrypted.cbData, &written, nullptr)
         && written == encrypted.cbData && FlushFileBuffers(file);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    SecureZeroMemory(encrypted.pbData, encrypted.cbData);
    LocalFree(encrypted.pbData);
    return ok;
}
extern "C" BOOL ABM_TMDB_GetReadToken(wchar_t *token, size_t tokenCount) {
    if (!token || tokenCount == 0) return FALSE;
    token[0] = L'\0';
    std::wstring value;
    if (!ReadToken(value) || value.size() >= tokenCount) return FALSE;
    StringCchCopyW(token, tokenCount, value.c_str());
    SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
    return TRUE;
}
extern "C" BOOL ABM_TMDB_IsConfigured(void) {
    std::wstring token; bool ok = ReadToken(token);
    if (!token.empty()) SecureZeroMemory(token.data(), token.size() * sizeof(wchar_t));
    return ok;
}
extern "C" BOOL ABM_TMDB_Search(const wchar_t *query, ABM_TMDB_MEDIA_TYPE type,
                                 wchar_t *jsonUtf8, size_t jsonCapacity) {
    if (!query || !*query || !jsonUtf8 || jsonCapacity == 0) return FALSE;
    const wchar_t *kind = type == ABM_TMDB_MOVIE ? L"movie" : type == ABM_TMDB_TV ? L"tv" : L"multi";
    std::wstring path = L"/3/search/" + std::wstring(kind) + L"?query=" + UrlEncode(query) + L"&include_adult=false";
    std::vector<BYTE> body;
    return HttpRequest(path, body, kReceiveTimeout) && JsonToWide(body, jsonUtf8, jsonCapacity);
}
extern "C" BOOL ABM_TMDB_GetDetails(ABM_TMDB_MEDIA_TYPE type, int tmdbId,
                                     wchar_t *jsonUtf8, size_t jsonCapacity) {
    if (tmdbId <= 0 || !jsonUtf8 || jsonCapacity == 0 ||
        (type != ABM_TMDB_MOVIE && type != ABM_TMDB_TV)) return FALSE;
    wchar_t path[128] = {};
    StringCchPrintfW(path, _countof(path), L"/3/%s/%d?append_to_response=credits,images,videos,recommendations",
                     type == ABM_TMDB_MOVIE ? L"movie" : L"tv", tmdbId);
    std::vector<BYTE> body;
    return HttpRequest(path, body, kReceiveTimeout) && JsonToWide(body, jsonUtf8, jsonCapacity);
}
extern "C" BOOL ABM_TMDB_DownloadImage(const wchar_t *imagePath, const wchar_t *destination, DWORD timeoutMs) {
    if (!imagePath || !destination || imagePath[0] != L'/' || wcsstr(imagePath, L"..")) return FALSE;
    std::wstring path = L"https://image.tmdb.org/t/p/w500" + std::wstring(imagePath);
    URL_COMPONENTS parts = {}; parts.dwStructSize = sizeof(parts);
    wchar_t host[128] = {}, urlPath[2048] = {};
    parts.lpszHostName = host; parts.dwHostNameLength = _countof(host);
    parts.lpszUrlPath = urlPath; parts.dwUrlPathLength = _countof(urlPath);
    if (!WinHttpCrackUrl(path.c_str(), 0, 0, &parts)) return FALSE;
    HINTERNET session = WinHttpOpen(L"ABM-AV-Player/1.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return FALSE;
    WinHttpSetTimeouts(session, (int)kConnectTimeout, (int)kConnectTimeout, (int)kSendTimeout,
                       (int)(timeoutMs ? timeoutMs : kReceiveTimeout));
    HINTERNET connect = WinHttpConnect(session, host, parts.nPort, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", urlPath, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    BOOL ok = request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);
    DWORD status = 0, statusSize = sizeof(status);
    if (ok) ok = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) && status == 200;
    HANDLE file = INVALID_HANDLE_VALUE;
    if (ok) file = CreateFileW(destination, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, nullptr);
    if (ok && file == INVALID_HANDLE_VALUE) ok = FALSE;
    while (ok) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) { ok = FALSE; break; }
        if (!available) break;
        BYTE buffer[16384]; DWORD got = 0;
        DWORD take = available < sizeof(buffer) ? available : (DWORD)sizeof(buffer);
        if (!WinHttpReadData(request, buffer, take, &got)) { ok = FALSE; break; }
        if (!got) break;
        DWORD written = 0;
        if (!WriteFile(file, buffer, got, &written, nullptr) || written != got) { ok = FALSE; break; }
    }
    if (file != INVALID_HANDLE_VALUE) { FlushFileBuffers(file); CloseHandle(file); }
    if (!ok) DeleteFileW(destination);
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}
extern "C" const wchar_t *ABM_TMDB_AttributionNotice(void) {
    return L"This product uses the TMDB API but is not endorsed or certified by TMDB.";
}
