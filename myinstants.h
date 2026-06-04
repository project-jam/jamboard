#pragma once
#include <string>
#include <vector>

struct MyInstant {
    std::string name;
    std::string mp3_url;
};

std::vector<MyInstant> myinstants_search(const std::string& query);
bool myinstants_download(const MyInstant& item, const std::string& out_dir);
void myinstants_preview(const MyInstant& item);
