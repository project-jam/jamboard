#include "update_checker.h"
#include "httplib.h"
#include "json.hpp"
#include "platform.h"
#include "data_models.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <string>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>

using json = nlohmann::json;

bool g_update_available = false;
std::string g_latest_version;
std::string g_update_url;
std::string g_download_url;
std::string g_release_notes;
bool g_download_progress = false;
std::string g_update_check_status;

static std::atomic<bool> g_checker_running{false};

static std::string http_get(const std::string& path) {
    httplib::SSLClient cli("api.github.com");
    cli.set_connection_timeout(10);
    cli.set_read_timeout(30);
    cli.set_default_headers({
        {"Accept", "application/vnd.github.v3+json"},
        {"User-Agent", std::string("JamBoard/") + JAMBOARD_VERSION + " (https://github.com/project-jam/jamboard)"}
    });

    auto res = cli.Get(path);
    if (!res || res->status == 403 || res->status == 404 || res->status != 200) return "";
    return res->body;
}

static bool download_file(const std::string& host, const std::string& path, const std::string& out_path) {
    httplib::SSLClient cli(host);
    cli.set_connection_timeout(10);
    cli.set_read_timeout(300);
    cli.set_default_headers({{"User-Agent", std::string("JamBoard/") + JAMBOARD_VERSION}});

    auto res = cli.Get(path);
    if (!res) return false;

    // Follow redirects (301, 302, 303, 307, 308)
    int attempts = 0;
    while ((res->status == 301 || res->status == 302 || res->status == 303 ||
            res->status == 307 || res->status == 308) && attempts < 5) {
        std::string location;
        for (auto& h : res->headers) {
            if (h.first == "Location" || h.first == "location") {
                location = h.second;
                break;
            }
        }
        if (location.empty()) break;

        // Parse new host and path from location
        std::string new_host = host;
        std::string new_path = location;
        if (location.find("://") != std::string::npos) {
            auto scheme_end = location.find("://");
            auto host_start = scheme_end + 3;
            auto path_start = location.find("/", host_start);
            if (path_start != std::string::npos) {
                new_host = location.substr(host_start, path_start - host_start);
                new_path = location.substr(path_start);
            } else {
                new_host = location.substr(host_start);
                new_path = "/";
            }
        }

        httplib::SSLClient new_cli(new_host);
        new_cli.set_connection_timeout(10);
        new_cli.set_read_timeout(300);
        new_cli.set_default_headers({{"User-Agent", std::string("JamBoard/") + JAMBOARD_VERSION}});
        res = new_cli.Get(new_path);
        if (!res) return false;
        attempts++;
    }

    if (res->status != 200) return false;

    FILE* fp = fopen(out_path.c_str(), "wb");
    if (!fp) return false;
    fwrite(res->body.data(), 1, res->body.size(), fp);
    fclose(fp);

    // Check file size — reject if too small (probably an error page)
    struct stat st;
    if (stat(out_path.c_str(), &st) != 0 || st.st_size < 1024 * 1024) {
        delete_file(out_path);
        return false;
    }
    return true;
}

static bool is_newer_version(const std::string& tag) {
    auto parse = [](const std::string& v) -> int {
        std::string s = v;
        if (s.size() > 0 && (s[0] == 'v' || s[0] == 'V')) s = s.substr(1);
        // Ignore pre-release suffixes such as "-beta1" or "-rc2".
        size_t dash = s.find_first_of("-+ ");
        if (dash != std::string::npos) s = s.substr(0, dash);
        int major = 0, minor = 0, patch = 0;
        sscanf(s.c_str(), "%d.%d.%d", &major, &minor, &patch);
        return major * 10000 + minor * 100 + patch;
    };
    return parse(tag) > parse(JAMBOARD_VERSION);
}

static void check_impl() {
    g_update_check_status = "Checking...";
    std::string body = http_get("/repos/project-jam/jamboard/releases/latest");
    if (body.empty() || body[0] != '{') {
        g_update_check_status = "Check failed (network error)";
        return;
    }

    try {
        auto j = json::parse(body);
        if (!j.contains("tag_name")) { g_update_check_status = "Check failed (bad response)"; return; }
        std::string tag = j.value("tag_name", "");
        if (tag.empty()) { g_update_check_status = "Up to date (no release found)"; return; }
        if (!is_newer_version(tag)) { g_update_check_status = "Up to date (" + tag + ")"; return; }

        std::string download_url;
        if (j.contains("assets") && j["assets"].is_array()) {
            for (auto& asset : j["assets"]) {
                std::string name = asset.value("name", "");
#ifdef _WIN32
                if (name.find("Setup") != std::string::npos || name.find(".exe") != std::string::npos) {
#else
                if (name.find("linux") != std::string::npos || name.find("Linux") != std::string::npos) {
#endif
                    download_url = asset.value("browser_download_url", "");
                    break;
                }
            }
        }

        g_latest_version = tag;
        g_update_url = j.value("html_url", "");
        g_download_url = download_url;
        g_release_notes = j.value("body", "");
        g_update_available = true;
        g_update_check_status = "Update available: " + tag;
    } catch (...) {
        g_update_check_status = "Check failed (parse error)";
    }
}

static void check_thread() {
    while (g_checker_running) {
        if (!update_check_enabled) {
            g_update_check_status = "Update checks are disabled";
            g_checker_running = false;
            break;
        }
        check_impl();
        // Re-check every 6 hours. The app is version locked to the build it
        // was shipped as, so there is no need to poll GitHub constantly.
        for (int i = 0; i < 6 * 60 * 60 && g_checker_running; i++) {
            if (!update_check_enabled) break;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void start_update_checker() {
    if (g_checker_running) return;
    // Distro packages are updated by the package manager, so the automatic
    // check can be switched off entirely in the settings.
    if (!update_check_enabled) {
        g_update_check_status = "Update checks are disabled";
        return;
    }
    g_checker_running = true;
    std::thread(check_thread).detach();
}

void check_for_updates() {
    std::thread(check_impl).detach();
}

void download_and_install() {
    if (g_download_url.empty()) return;

    // On Linux the app is normally installed from a package, so replacing
    // the binary behind the package manager would break it. Point the user
    // at the release page instead.
#ifndef _WIN32
    if (!g_update_url.empty()) open_with_system(g_update_url);
    return;
#else

    std::string host = "github.com";
    std::string path = g_download_url;
    auto scheme_end = g_download_url.find("://");
    if (scheme_end != std::string::npos) {
        auto host_start = scheme_end + 3;
        auto path_start = g_download_url.find("/", host_start);
        if (path_start != std::string::npos) {
            host = g_download_url.substr(host_start, path_start - host_start);
            path = g_download_url.substr(path_start);
        }
    }

    std::string installer_path = get_temp_dir() + "/JamBoard_update";
    installer_path += ".exe";

    g_download_progress = true;
    std::thread([host, path, installer_path]() {
        bool ok = download_file(host, path, installer_path);
        g_download_progress = false;
        if (ok) {
            open_with_system(installer_path);
            // Wait for installer to start, then exit
            sleep_ms(3000);
            ExitProcess(0);
        }
    }).detach();
#endif
}
