#include "import.h"
#include "import_ffmpeg.h"
#include "data_models.h"
#include <filesystem>
#include <thread>
#include <algorithm>
#include <windows.h>

static std::filesystem::path utf8_to_path(const std::string& utf8) {
    wchar_t wide[MAX_PATH];
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide, MAX_PATH);
    if (len == 0) return std::filesystem::path(utf8);
    return std::filesystem::path(wide);
}

static std::string path_to_utf8(const std::filesystem::path& p) {
    std::wstring w = p.wstring();
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    if (len == 0) return std::string();
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &result[0], len, NULL, NULL);
    return result;
}

void convert_media(const std::string& path)
{
    std::string stem = path_to_utf8(std::filesystem::path(path).stem());
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
    // Resolve to absolute path so output dirs work regardless of CWD
    char buf[MAX_PATH];
    GetFullPathNameA(".", MAX_PATH, buf, NULL);
    return buf;
}

static bool ytdlp_exists() {
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::string exe_dir = std::filesystem::path(buf).parent_path().string();
    std::string path = exe_dir + "\\yt-dlp.exe";
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void download_from_url(const std::string& url, bool& out_success, std::string& out_error) {
    out_success = false;
    out_error.clear();

    if (!ytdlp_exists()) {
        out_error = "yt-dlp.exe not found. Run 'make deps' or reinstall JamBoard.";
        return;
    }

    char exe_buf[MAX_PATH];
    GetModuleFileNameA(NULL, exe_buf, MAX_PATH);
    std::string exe_dir = std::filesystem::path(exe_buf).parent_path().string();
    std::string ytdlp = exe_dir + "\\yt-dlp.exe";
    if (GetFileAttributesA(ytdlp.c_str()) == INVALID_FILE_ATTRIBUTES)
        ytdlp = "yt-dlp.exe";

    std::string data_dir = get_data_dir();
    std::string tmp_dir = data_dir + "\\ytdlp_tmp";
    std::string sounds_dir = data_dir + "\\sounds";
    std::filesystem::create_directories(tmp_dir);
    std::filesystem::create_directories(sounds_dir);

    // Download audio (native format, no ffmpeg.exe needed)
    std::string audio_cmd = "\"" + ytdlp + "\" --quiet -f bestaudio --no-playlist -o \"" +
        tmp_dir + "\\%(title)s.%(ext)s\" \"" + url + "\"";
    if (!run_process(audio_cmd)) {
        out_error = "yt-dlp audio download failed. Check URL or internet connection.";
        std::error_code ec;
        std::filesystem::remove_all(tmp_dir, ec);
        return;
    }

    // Find and move downloaded files to sounds/
    for (auto& entry : std::filesystem::directory_iterator(tmp_dir)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        auto stem_u8 = path_to_utf8(entry.path().stem());

        // Sanitize filename — remove characters illegal in Windows filenames
        std::string safe_name;
        for (unsigned char c : stem_u8) {
            if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
                safe_name += '_';
            else
                safe_name += c;
        }

        std::string lower_ext = ext;
        for (auto& c : lower_ext) c = (char)std::tolower(c);

        if (lower_ext == ".wav") {
            std::filesystem::path dest = utf8_to_path(sounds_dir + "\\" + safe_name + ".wav");
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
            if (ec) { tmp_audio = path_to_utf8(entry.path()); }
            ffmpeg_convert_audio(tmp_audio, sounds_dir + "\\" + safe_name + ".wav");
        }
        else if (lower_ext == ".jpg" || lower_ext == ".jpeg" || lower_ext == ".png" ||
                 lower_ext == ".ppm") {
            std::filesystem::path dest = utf8_to_path(sounds_dir + "\\" + safe_name + ext);
            std::error_code ec;
            if (lower_ext != ".ppm") {
                std::filesystem::copy_file(entry.path(), dest, std::filesystem::copy_options::overwrite_existing, ec);
            } else {
                std::filesystem::rename(entry.path(), dest, ec);
            }
        }
    }

    std::error_code ec;
    std::filesystem::remove_all(tmp_dir, ec);
    out_success = true;
}

void handle_dropped_files(const std::vector<std::string>& paths)
{
    if (is_converting) return;

    is_converting = true;
    std::thread([paths_copy = paths]() {
        std::string dest_dir = "sounds";
        if (!current_folder.empty())
            dest_dir += "/" + current_folder;
        std::filesystem::create_directories(dest_dir);
        for (const auto& path : paths_copy) {
            std::string lower = path;
            for (auto& c : lower) c = (char)std::tolower(c);

            if (lower.ends_with(".mp3") || lower.ends_with(".wav") || lower.ends_with(".ogg"))
            {
                std::filesystem::path dest = utf8_to_path(dest_dir + "/" + path_to_utf8(std::filesystem::path(path).filename()));
                std::error_code ec;
                std::filesystem::copy_file(path, dest, std::filesystem::copy_options::overwrite_existing, ec);
            }
            else
            {
                // For non-audio files, convert to the dest_dir
                std::string stem = path_to_utf8(std::filesystem::path(path).stem());
                ffmpeg_convert_audio(path, dest_dir + "/" + stem + ".wav");
                ffmpeg_extract_thumbnail(path, dest_dir + "/" + stem + ".ppm");
            }
        }
        is_converting = false;
        needs_sound_reload = true;
    }).detach();
}
