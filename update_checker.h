#pragma once
#include <string>

#ifndef JAMBOARD_VERSION
#define JAMBOARD_VERSION "v0.0"
#endif

extern bool g_update_available;
extern std::string g_latest_version;
extern std::string g_update_url;
extern std::string g_download_url;
extern bool g_download_progress;
extern std::string g_update_check_status;

void start_update_checker();
void check_for_updates();
void download_and_install();
