#pragma once
#include <string>
#include <vector>

void handle_dropped_files(const std::vector<std::string>& paths);
void convert_media(const std::string& path);
bool download_from_url(const std::string& url);
