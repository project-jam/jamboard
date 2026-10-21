#pragma once
#include <string>
#include <filesystem>
#include <cstdio>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <direct.h>
#define getcwd _getcwd
#define poll WSAPoll
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#endif
#include <vector>

// Get the directory containing the running executable
inline std::string get_exe_dir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path().string();
#else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return ".";
    buf[len] = '\0';
    return std::filesystem::path(buf).parent_path().string();
#endif
}

// Get the current working directory as a string
inline std::string get_cwd() {
    char buf[4096];
    if (getcwd(buf, sizeof(buf)) == nullptr) return ".";
    return buf;
}

// Get the system temp directory
inline std::string get_temp_dir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    GetTempPathA(MAX_PATH, buf);
    return std::string(buf);
#else
    const char* tmp = getenv("TMPDIR");
    if (tmp) return std::string(tmp);
    return "/tmp";
#endif
}

// Get the user's data directory (APPDATA on Windows, XDG_DATA_HOME on Linux)
inline std::string get_data_dir() {
#ifdef _WIN32
    char* appdata = getenv("APPDATA");
    if (appdata) return std::string(appdata) + "\\JamBoard";
    return get_cwd();
#else
    const char* xdg = getenv("XDG_DATA_HOME");
    if (xdg) return std::string(xdg) + "/jamboard";
    const char* home = getenv("HOME");
    if (home) return std::string(home) + "/.local/share/jamboard";
    return get_cwd();
#endif
}

// Root directory that holds config.json and the sounds/ folder.
// Resolved once at startup and cached here.
inline std::string& app_data_dir() {
    static std::string dir;
    return dir;
}

// Decide where user data lives.
//   - Windows: the current folder if writable, else %APPDATA%\JamBoard
//   - Linux/macOS: the current folder for portable installs, but the
//     XDG data dir when the app is installed in a system location.
//   - $JAMBOARD_PORTABLE=1 forces the current folder everywhere.
inline std::string resolve_app_data_dir() {
    const char* portable = getenv("JAMBOARD_PORTABLE");
    if (portable && portable[0] == '1') return get_cwd();

#ifdef _WIN32
    std::string cwd = get_cwd();
    std::error_code ec;
    std::filesystem::create_directories(cwd, ec);
    std::string probe = cwd + "/__w_test";
    std::ofstream tf(probe);
    if (tf.is_open()) {
        tf.close();
        std::filesystem::remove(probe, ec);
        return cwd;
    }
    return get_data_dir();
#else
    std::string exe = get_exe_dir();
    static const char* system_prefixes[] = {
        "/usr/bin", "/usr/local/bin", "/bin", "/sbin", "/opt",
        "/snap", "/var/lib/flatpak", "/var/lib/flatpak/exports"
    };
    for (const char* p : system_prefixes) {
        if (exe.rfind(p, 0) == 0) return get_data_dir();
    }
    // Portable install: keep data next to the current folder
    return get_cwd();
#endif
}

// Full path of the config file
inline std::string config_file_path() {
    return app_data_dir() + "/config.json";
}

// Check if a file exists
inline bool file_exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

// Search $PATH for an executable. Returns the full path, or "" if not found.
inline std::string find_in_path(const std::string& name) {
    if (name.find('/') != std::string::npos)
        return file_exists(name) ? name : "";

    const char* path_env = getenv("PATH");
    if (!path_env) return "";

    std::string path(path_env);
    size_t start = 0;
    while (start <= path.size()) {
        size_t sep = path.find(':', start);
        if (sep == std::string::npos) sep = path.size();
        std::string dir = path.substr(start, sep - start);
        if (!dir.empty()) {
#ifdef _WIN32
            std::string full = dir + "\\" + name + ".exe";
            if (file_exists(full)) return full;
            full = dir + "\\" + name;
            if (file_exists(full)) return full;
#else
            std::string full = dir + "/" + name;
            if (file_exists(full) && access(full.c_str(), X_OK) == 0) return full;
#endif
        }
        if (sep >= path.size()) break;
        start = sep + 1;
    }
    return "";
}

// Delete a file
inline bool delete_file(const std::string& path) {
    std::error_code ec;
    return std::filesystem::remove(path, ec);
}

// Run a process and wait for it, return exit code
inline int run_process(const std::string& cmd) {
#ifdef _WIN32
    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::string cmd_copy = cmd;
    if (!CreateProcessA(NULL, &cmd_copy[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return -1;
    WaitForSingleObject(pi.hProcess, 300000);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exit_code;
#else
    return system(cmd.c_str());
#endif
}

// Run a program with an argument vector (no shell involved).
// Returns the process exit code, or -1 if it could not be started.
inline int run_process_args(const std::vector<std::string>& args) {
    if (args.empty()) return -1;
#ifdef _WIN32
    std::string cmd;
    for (size_t i = 0; i < args.size(); i++) {
        if (i) cmd += " ";
        cmd += "\"" + args[i] + "\"";
    }
    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::string cmd_copy = cmd;
    if (!CreateProcessA(NULL, &cmd_copy[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return -1;
    WaitForSingleObject(pi.hProcess, 300000);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exit_code;
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
#endif
}

// Open a URL or file with the system default application
inline void open_with_system(const std::string& path) {
#ifdef _WIN32
    ShellExecuteA(NULL, "open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
#else
    run_process_args({ "xdg-open", path });
#endif
}

// Show a native file picker dialog and return the selected path (empty on cancel)
inline std::string show_file_picker(const std::string& title, const std::string& filter = "") {
#ifdef _WIN32
    char filename[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrTitle = title.c_str();
    ofn.lpstrFilter = filter.empty() ? "All Files\0*.*\0" : filter.c_str();
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) return filename;
    return "";
#else
    // Use zenity for file picker on Linux
    std::string cmd = "zenity --file-selection --title=\"" + title + "\"";
    if (!filter.empty()) cmd += " --file-filter=\"" + filter + "\"";
    cmd += " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buf[4096];
    std::string result;
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
    pclose(pipe);
    // Remove trailing newline
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
    return result;
#endif
}

// Sleep for milliseconds
inline void sleep_ms(unsigned int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}
