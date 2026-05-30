#include "update_checker.h"
#include "json.hpp"
#include <thread>
#include <atomic>
#include <chrono>
#include <string>
#include <windows.h>
#include <winhttp.h>

using json = nlohmann::json;

bool g_update_available = false;
std::string g_latest_version;
std::string g_update_url;
std::string g_download_url;
bool g_download_progress = false;

static std::atomic<bool> g_checker_running{false};

static std::string wide_to_utf8(const wchar_t* w) {
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    std::string s(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], len, NULL, NULL);
    return s;
}

static std::wstring utf8_to_wide(const char* s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    std::wstring w(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], len);
    return w;
}

static std::string http_get(const wchar_t* host, const wchar_t* path) {
    HINTERNET hSession = WinHttpOpen(L"JamBoard/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }

    DWORD sslFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                     SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                     SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                     SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &sslFlags, sizeof(sslFlags));

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    std::string body;
    char buf[4096];
    DWORD bytesRead = 0;
    while (WinHttpReadData(hRequest, buf, sizeof(buf) - 1, &bytesRead) && bytesRead > 0) {
        buf[bytesRead] = 0;
        body += buf;
        bytesRead = 0;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return body;
}

static bool download_file(const wchar_t* host, const wchar_t* path, const std::string& out_path) {
    HINTERNET hSession = WinHttpOpen(L"JamBoard/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    // Follow redirects
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    // Ignore SSL errors for CDN redirects
    DWORD sslFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                     SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                     SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                     SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &sslFlags, sizeof(sslFlags));

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    HANDLE hFile = CreateFileA(out_path.c_str(), GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    char buf[65536];
    DWORD bytesRead = 0;
    DWORD totalWritten = 0;
    while (WinHttpReadData(hRequest, buf, sizeof(buf), &bytesRead) && bytesRead > 0) {
        DWORD written = 0;
        WriteFile(hFile, buf, bytesRead, &written, NULL);
        totalWritten += written;
        bytesRead = 0;
    }

    CloseHandle(hFile);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    // Installer should be at least 1MB — reject truncated downloads
    if (totalWritten < 1024 * 1024) {
        DeleteFileA(out_path.c_str());
        return false;
    }
    return true;
}

static void check_impl() {
    std::string body = http_get(L"api.github.com",
        L"/repos/project-jam/jamboard/releases/latest");
    if (body.empty()) return;

    try {
        auto j = json::parse(body);
        std::string tag = j.value("tag_name", "");
        if (tag.empty() || tag == JAMBOARD_VERSION) return;

        std::string download_url;
        if (j.contains("assets") && j["assets"].is_array()) {
            for (auto& asset : j["assets"]) {
                std::string name = asset.value("name", "");
                if (name.find("Setup") != std::string::npos ||
                    name.find("setup") != std::string::npos) {
                    download_url = asset.value("browser_download_url", "");
                    break;
                }
            }
        }

        g_latest_version = tag;
        g_update_url = j.value("html_url", "");
        g_download_url = download_url;
        g_update_available = true;
    } catch (...) {}
}

static void check_thread() {
    while (g_checker_running) {
        check_impl();
        for (int i = 0; i < 600 && g_checker_running; i++)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void start_update_checker() {
    if (g_checker_running) return;
    g_checker_running = true;
    std::thread(check_thread).detach();
}

void check_for_updates() {
    std::thread(check_impl).detach();
}

void download_and_install() {
    if (g_download_url.empty()) return;

    auto url_w = utf8_to_wide(g_download_url.c_str());

    // Parse host and path from download URL
    // https://github.com/project-jam/jamboard/releases/download/v2.2/JamBoard_Setup.exe
    std::wstring host = L"github.com";
    std::wstring path;
    size_t path_start = url_w.find(L"/releases/download/");
    if (path_start != std::wstring::npos)
        path = url_w.substr(path_start);

    // Get temp path
    char temp_path[MAX_PATH];
    GetTempPathA(MAX_PATH, temp_path);
    std::string installer_path = std::string(temp_path) + "JamBoard_Setup.exe";

    g_download_progress = true;

    std::thread([host, path, installer_path]() {
        bool ok = download_file(host.c_str(), path.c_str(), installer_path);
        g_download_progress = false;

        if (ok) {
            // Run installer silently (skips welcome wizard)
            ShellExecuteA(NULL, "open", installer_path.c_str(),
                "/SILENT", NULL, SW_SHOWNORMAL);
            // Exit current app so installer can overwrite
            Sleep(1000);
            ExitProcess(0);
        }
    }).detach();
}
