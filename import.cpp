#include "import.h"
#include "import_ffmpeg.h"
#include "data_models.h"
#include <filesystem>
#include <thread>
#include <algorithm>

void convert_media(const std::string& path)
{
    std::string stem = std::filesystem::path(path).stem().string();
    ffmpeg_convert_audio(path, "sounds/" + stem + ".wav");
    ffmpeg_extract_thumbnail(path, "sounds/" + stem + ".ppm");
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
