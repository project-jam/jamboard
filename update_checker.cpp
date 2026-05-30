#include "update_checker.h"
#include "json.hpp"
#include <thread>
#include <atomic>
#include <chrono>
#include <string>
#include <fstream>
#include <curl/curl.h>
#include <windows.h>

using json = nlohmann::json;

bool g_update_available = false;
std::string g_latest_version;
std::string g_update_url;
std::string g_download_url;
bool g_download_progress = false;

static std::atomic<bool> g_checker_running{false};

static size_t write_string(void* ptr, size_t size, size_t nmemb, void* userdata) {
    ((std::string*)userdata)->append((char*)ptr, size * nmemb);
    return size * nmemb;
}

static size_t write_file(void* ptr, size_t size, size_t nmemb, void* userdata) {
    FILE* fp = (FILE*)userdata;
    return fwrite(ptr, size, nmemb, fp);
}

static std::string http_get(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return (res == CURLE_OK) ? body : "";
}

static bool download_file(const std::string& url, const std::string& out_path) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    FILE* fp = fopen(out_path.c_str(), "wb");
    if (!fp) { curl_easy_cleanup(curl); return false; }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        DeleteFileA(out_path.c_str());
        return false;
    }

    // Verify file is at least 1MB (reject truncated downloads)
    HANDLE hFile = CreateFileA(out_path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(hFile, NULL);
    CloseHandle(hFile);
    if (size < 1024 * 1024) {
        DeleteFileA(out_path.c_str());
        return false;
    }
    return true;
}

static void check_impl() {
    std::string body = http_get("https://api.github.com/repos/project-jam/jamboard/releases/latest");
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
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (g_checker_running) return;
    g_checker_running = true;
    std::thread(check_thread).detach();
}

void check_for_updates() {
    std::thread(check_impl).detach();
}

void download_and_install() {
    if (g_download_url.empty()) return;

    char temp_path[MAX_PATH];
    GetTempPathA(MAX_PATH, temp_path);
    std::string installer_path = std::string(temp_path) + "JamBoard_Setup.exe";

    g_download_progress = true;

    std::thread([installer_path]() {
        bool ok = download_file(g_download_url, installer_path);
        g_download_progress = false;

        if (ok) {
            ShellExecuteA(NULL, "runas", installer_path.c_str(),
                "/SILENT /SUPPRESSMSGBOXES", NULL, SW_SHOWNORMAL);
            Sleep(1000);
            ExitProcess(0);
        }
    }).detach();
}
