#include "import.h"
#include "import_ffmpeg.h"
#include "data_models.h"
#include "platform.h"
#include <filesystem>
#include <thread>
#include <algorithm>

#ifdef _WIN32
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
#else
static std::filesystem::path utf8_to_path(const std::string& utf8) { return std::filesystem::path(utf8); }
static std::string path_to_utf8(const std::filesystem::path& p) { return p.string(); }
#endif

void convert_media(const std::string& path)
{
    std::string stem = path_to_utf8(std::filesystem::path(path).stem());
    std::string root = app_data_dir().empty() ? std::string(".") : app_data_dir();
    std::string sounds = root + "/sounds";
    std::error_code ec;
    std::filesystem::create_directories(sounds, ec);
    ffmpeg_convert_audio(path, sounds + "/" + stem + ".wav");
    ffmpeg_extract_thumbnail(path, sounds + "/" + stem + ".ppm");
}

static std::string get_import_data_dir() {
    if (program_files_mode) {
        return ::get_data_dir();
    }
    if (!app_data_dir().empty()) {
        return app_data_dir();
    }
    return get_cwd();
}

// Locate the yt-dlp executable.
// Priority: $JAMBOARD_YTDLP -> bundled next to the app -> system $PATH.
// The $PATH lookup lets distro packages depend on the system yt-dlp
// instead of shipping a second copy.
static std::string find_ytdlp() {
    if (const char* env = getenv("JAMBOARD_YTDLP")) {
        if (env[0] && file_exists(env)) return env;
    }

    std::string exe_dir = get_exe_dir();
#ifdef _WIN32
    std::string bundled = exe_dir + "\\yt-dlp.exe";
    if (file_exists(bundled)) return bundled;
    std::string sys = find_in_path("yt-dlp");
    if (!sys.empty()) return sys;
    return "yt-dlp.exe";
#else
    std::string bundled = exe_dir + "/yt-dlp";
    if (file_exists(bundled)) return bundled;
    std::string sys = find_in_path("yt-dlp");
    if (!sys.empty()) return sys;
    return "yt-dlp";
#endif
}

void download_from_url(const std::string& url, bool& out_success, std::string& out_error) {
    out_success = false;
    out_error.clear();

    std::string ytdlp = find_ytdlp();
    if (ytdlp.empty() || !file_exists(ytdlp)) {
#ifdef _WIN32
        out_error = "yt-dlp.exe not found. Run 'make deps' or reinstall JamBoard.";
#else
        out_error = "yt-dlp not found. Install it (e.g. 'sudo apt install yt-dlp') or run 'make deps'.";
#endif
        return;
    }

    std::string data_dir = get_import_data_dir();
#ifdef _WIN32
    std::string tmp_dir = data_dir + "\\ytdlp_tmp";
    std::string sounds_dir = data_dir + "\\sounds";
#else
    std::string tmp_dir = data_dir + "/ytdlp_tmp";
    std::string sounds_dir = data_dir + "/sounds";
#endif
    std::filesystem::create_directories(tmp_dir);
    std::filesystem::create_directories(sounds_dir);

    // Download audio (native format, no ffmpeg.exe needed)
    std::vector<std::string> ytdlp_args = {
        ytdlp,
        "--quiet",
        "-f", "bestaudio",
        "--no-playlist",
        "-o", tmp_dir + "/%(title)s.%(ext)s",
        url
    };
    if (run_process_args(ytdlp_args) != 0) {
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

        // Sanitize filename — remove characters illegal in filenames
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
            std::filesystem::path dest = utf8_to_path(sounds_dir + "/" + safe_name + ".wav");
            std::error_code ec;
            std::filesystem::rename(entry.path(), dest, ec);
            if (ec) std::filesystem::copy_file(entry.path(), dest, std::filesystem::copy_options::overwrite_existing, ec);
        }
        else if (lower_ext == ".webm" || lower_ext == ".m4a" || lower_ext == ".opus" ||
                 lower_ext == ".mp3" || lower_ext == ".ogg" || lower_ext == ".flac") {
            // yt-dlp gave us a non-WAV audio — convert it
            std::string tmp_audio = tmp_dir + "/tmp_convert" + ext;
            std::error_code ec;
            std::filesystem::rename(entry.path(), tmp_audio, ec);
            if (ec) { tmp_audio = path_to_utf8(entry.path()); }
            ffmpeg_convert_audio(tmp_audio, sounds_dir + "/" + safe_name + ".wav");
        }
        else if (lower_ext == ".jpg" || lower_ext == ".jpeg" || lower_ext == ".png" ||
                 lower_ext == ".ppm") {
            std::filesystem::path dest = utf8_to_path(sounds_dir + "/" + safe_name + ext);
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

            auto has_ext = [](const std::string& s, const std::string& ext) {
                return s.size() >= ext.size() && s.compare(s.size() - ext.size(), ext.size(), ext) == 0;
            };
            if (has_ext(lower, ".mp3") || has_ext(lower, ".wav") || has_ext(lower, ".ogg"))
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
