#include "import.h"
#include "import_ffmpeg.h"
#include "data_models.h"
#include <filesystem>
#include <thread>
#include <algorithm>
#include <windows.h>

void convert_media(const std::string& path)
{
    std::string stem = std::filesystem::path(path).stem().string();
    ffmpeg_convert_audio(path, "sounds/" + stem + ".wav");
    ffmpeg_extract_thumbnail(path, "sounds/" + stem + ".ppm");
}

static bool run_process(const std::string& cmd) {
    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::string cmd_copy = cmd;
    if (!CreateProcessA(NULL, &cmd_copy[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 300000);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exit_code == 0;
}

static std::string get_data_dir() {
    if (program_files_mode) {
        char* appdata = getenv("APPDATA");
        if (appdata) return std::string(appdata) + "\\JamBoard";
    }
    return ".";
}

bool download_from_url(const std::string& url) {
    std::string data_dir = get_data_dir();
    std::string tmp_dir = data_dir + "\\ytdlp_tmp";
    std::string sounds_dir = data_dir + "\\sounds";
    std::filesystem::create_directories(tmp_dir);
    std::filesystem::create_directories(sounds_dir);

    // Download audio as WAV
    std::string audio_cmd = "yt-dlp.exe --quiet -x --audio-format wav --no-playlist -o \"" +
        tmp_dir + "\\%(title)s.%(ext)s\" \"" + url + "\"";
    if (!run_process(audio_cmd)) {
        std::error_code ec;
        std::filesystem::remove_all(tmp_dir, ec);
        return false;
    }

    // Download thumbnail
    std::string thumb_cmd = "yt-dlp.exe --quiet --write-thumbnail --convert-thumbnails jpg --skip-download --no-playlist -o \"" +
        tmp_dir + "\\%(title)s\" \"" + url + "\"";
    run_process(thumb_cmd);

    // Find and move downloaded files to sounds/
    for (auto& entry : std::filesystem::directory_iterator(tmp_dir)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        auto stem = entry.path().stem().string();

        // Sanitize filename — remove characters illegal in Windows filenames
        std::string safe_name;
        for (char c : stem) {
            if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
                safe_name += '_';
            else
                safe_name += c;
        }

        std::string lower_ext = ext;
        for (auto& c : lower_ext) c = (char)std::tolower(c);

        if (lower_ext == ".wav") {
            std::string dest = sounds_dir + "\\" + safe_name + ".wav";
            std::error_code ec;
            std::filesystem::rename(entry.path(), dest, ec);
            if (ec) std::filesystem::copy_file(entry.path(), dest, std::filesystem::copy_options::overwrite_existing, ec);
        }
        else if (lower_ext == ".webm" || lower_ext == ".m4a" || lower_ext == ".opus" ||
                 lower_ext == ".mp3" || lower_ext == ".ogg" || lower_ext == ".flac") {
            // yt-dlp gave us a non-WAV audio — convert it
            std::string tmp_audio = tmp_dir + "\\tmp_convert" + ext;
            std::error_code ec;
            std::filesystem::rename(entry.path(), tmp_audio, ec);
            if (ec) { tmp_audio = entry.path().string(); }
            ffmpeg_convert_audio(tmp_audio, sounds_dir + "\\" + safe_name + ".wav");
        }
        else if (lower_ext == ".jpg" || lower_ext == ".jpeg" || lower_ext == ".png" ||
                 lower_ext == ".webp" || lower_ext == ".ppm") {
            if (lower_ext != ".ppm") {
                std::string dest = sounds_dir + "\\" + safe_name + ext;
                std::error_code ec;
                std::filesystem::copy_file(entry.path(), dest, std::filesystem::copy_options::overwrite_existing, ec);
            } else {
                std::string dest = sounds_dir + "\\" + safe_name + ".ppm";
                std::error_code ec;
                std::filesystem::rename(entry.path(), dest, ec);
            }
        }
    }

    std::error_code ec;
    std::filesystem::remove_all(tmp_dir, ec);
    return true;
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
