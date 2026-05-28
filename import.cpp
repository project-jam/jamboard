#include "import.h"
#include "data_models.h"
#include <filesystem>
#include <thread>
#include <cstdlib>
#include <algorithm>

void convert_media(const std::string& path)
{
    std::string stem = std::filesystem::path(path).stem().string();
    std::string cmd_audio = "ffmpeg -i \"" + path + "\" -q:a 0 -map a \"sounds/" + stem + ".mp3\" -y > NUL 2>&1";
    std::string cmd_img = "ffmpeg -i \"" + path + "\" -vframes 1 \"sounds/" + stem + ".jpg\" -y > NUL 2>&1";
    system(cmd_audio.c_str());
    system(cmd_img.c_str());
}

void handle_dropped_files(const std::vector<std::string>& paths)
{
    if (is_converting) return;

    is_converting = true;
    std::thread([paths_copy = paths]() {
        for (const auto& path : paths_copy) {
            std::string lower = path;
            for (auto& c : lower) c = (char)std::tolower(c);

            if (lower.ends_with(".mp3") || lower.ends_with(".wav") || lower.ends_with(".ogg"))
            {
                try {
                    std::string dest = "sounds/" + std::filesystem::path(path).filename().string();
                    std::filesystem::copy_file(path, dest, std::filesystem::copy_options::overwrite_existing);
                } catch (...) {}
            }
            else
            {
                convert_media(path);
            }
        }
        is_converting = false;
        needs_sound_reload = true;
    }).detach();
}
