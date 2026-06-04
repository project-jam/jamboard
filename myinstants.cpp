#include "myinstants.h"
#include "import_ffmpeg.h"
#include "httplib.h"
#include <cstdio>
#include <filesystem>
#include <windows.h>
#include <mmsystem.h>
#include <thread>
#include <atomic>

static std::string decode_html_entities(const std::string& s) {
    struct Entity { const char* seq; size_t len; char ch; };
    static const Entity table[] = {
        {"&#x27;", 6, '\''}, {"&#x2F;", 6, '/'}, {"&#x22;", 6, '"'},
        {"&#x3C;", 6, '<'},  {"&#x3E;", 6, '>'}, {"&#x26;", 6, '&'},
        {"&apos;", 6, '\''}, {"&quot;", 6, '"'}, {"&amp;",  5, '&'},
        {"&#39;",  5, '\''}, {"&#34;",  5, '"'}, {"&#38;",  5, '&'},
        {"&#60;",  5, '<'},  {"&#62;",  5, '>'},
        {"&lt;",   4, '<'},  {"&gt;",   4, '>'},
    };
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '&') {
            bool found = false;
            for (const auto& e : table) {
                if (s.compare(i, e.len, e.seq) == 0) {
                    out += e.ch;
                    i += e.len - 1;
                    found = true;
                    break;
                }
            }
            if (!found) out += s[i];
        } else {
            out += s[i];
        }
    }
    return out;
}

static std::atomic<bool> g_preview_playing{false};
static std::string g_preview_wav;

static std::string get_temp_dir() {
    char buf[MAX_PATH];
    GetTempPathA(MAX_PATH, buf);
    return std::string(buf) + "JamBoard_Preview";
}

static std::string safe_filename(const std::string& name) {
    std::string safe;
    for (char c : name) {
        if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
            safe += '_';
        else
            safe += c;
    }
    return safe.empty() ? "preview" : safe;
}

std::vector<MyInstant> myinstants_search(const std::string& query) {
    std::vector<MyInstant> results;

    httplib::SSLClient cli("www.myinstants.com");
    cli.set_connection_timeout(10);
    cli.set_read_timeout(15);
    cli.set_default_headers({
        {"Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"},
        {"Accept-Language", "en-US,en;q=0.5"},
        {"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/137.0.0.0 Safari/537.36"}
    });

    auto res = cli.Get("/en/search/?name=" + query);
    if (!res || res->status != 200) return results;

    const std::string& html = res->body;
    std::string::size_type pos = 0;

    while (true) {
        auto instant_pos = html.find("<div class=\"instant\">", pos);
        if (instant_pos == std::string::npos) break;
        pos = instant_pos + 21;

        auto block_end = html.find("<div class=\"instant\">", pos);
        if (block_end == std::string::npos) block_end = html.size();
        std::string block = html.substr(instant_pos, block_end - instant_pos);

        std::string mp3_url;
        auto play_pos = block.find("play('");
        if (play_pos != std::string::npos) {
            auto start = play_pos + 6;
            auto end = block.find("'", start);
            if (end != std::string::npos) {
                std::string path = block.substr(start, end - start);
                if (path.find(".mp3") != std::string::npos || path.find(".wav") != std::string::npos)
                    mp3_url = "https://www.myinstants.com" + path;
            }
        }

        std::string name;
        auto link_pos = block.find("instant-link");
        if (link_pos != std::string::npos) {
            auto gt = block.find(">", link_pos);
            if (gt != std::string::npos) {
                auto close = block.find("</a>", gt);
                if (close != std::string::npos) {
                    name = block.substr(gt + 1, close - gt - 1);
                    while (!name.empty() && (name.front() == ' ' || name.front() == '\n' || name.front() == '\r' || name.front() == '\t'))
                        name.erase(name.begin());
                    while (!name.empty() && (name.back() == ' ' || name.back() == '\n' || name.back() == '\r' || name.back() == '\t'))
                        name.pop_back();
                    name = decode_html_entities(name);
                }
            }
        }

        if (!name.empty() && !mp3_url.empty()) {
            MyInstant item;
            item.name = name;
            item.mp3_url = mp3_url;
            results.push_back(item);
        }
        if (results.size() >= 20) break;
    }
    return results;
}

bool myinstants_download(const MyInstant& item, const std::string& out_dir) {
    std::string host = "www.myinstants.com";
    std::string path = item.mp3_url;
    auto scheme_end = item.mp3_url.find("://");
    if (scheme_end != std::string::npos) {
        auto host_start = scheme_end + 3;
        auto path_start = item.mp3_url.find("/", host_start);
        if (path_start != std::string::npos) {
            host = item.mp3_url.substr(host_start, path_start - host_start);
            path = item.mp3_url.substr(path_start);
        }
    }

    httplib::SSLClient cli(host);
    cli.set_connection_timeout(10);
    cli.set_read_timeout(30);
    cli.set_default_headers({
        {"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"}
    });

    auto res = cli.Get(path);
    if (!res || res->status != 200) return false;

    std::string safe = safe_filename(item.name);
    std::string ext = ".mp3";
    auto dot_pos = item.mp3_url.rfind('.');
    if (dot_pos != std::string::npos) {
        std::string url_ext = item.mp3_url.substr(dot_pos);
        if (url_ext.size() <= 5) ext = url_ext;
    }

    std::string out_path = out_dir + "\\" + safe + ext;
    FILE* fp = fopen(out_path.c_str(), "wb");
    if (!fp) return false;
    fwrite(res->body.data(), 1, res->body.size(), fp);
    fclose(fp);
    return true;
}

void myinstants_preview(const MyInstant& item) {
    if (g_preview_playing) return;
    g_preview_playing = true;

    std::string temp_dir = get_temp_dir();
    std::filesystem::create_directories(temp_dir);

    std::string safe = safe_filename(item.name);

    std::thread([item, temp_dir, safe]() {
        // Download MP3 to temp
        std::string host = "www.myinstants.com";
        std::string path = item.mp3_url;
        auto scheme_end = item.mp3_url.find("://");
        if (scheme_end != std::string::npos) {
            auto host_start = scheme_end + 3;
            auto path_start = item.mp3_url.find("/", host_start);
            if (path_start != std::string::npos) {
                host = item.mp3_url.substr(host_start, path_start - host_start);
                path = item.mp3_url.substr(path_start);
            }
        }

        httplib::SSLClient cli(host);
        cli.set_connection_timeout(10);
        cli.set_read_timeout(30);
        cli.set_default_headers({
            {"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"}
        });

        auto res = cli.Get(path);
        if (!res || res->status != 200) { g_preview_playing = false; return; }

        std::string mp3_path = temp_dir + "\\" + safe + ".mp3";
        FILE* fp = fopen(mp3_path.c_str(), "wb");
        if (!fp) { g_preview_playing = false; return; }
        fwrite(res->body.data(), 1, res->body.size(), fp);
        fclose(fp);

        // Convert to WAV
        std::string wav_path = temp_dir + "\\" + safe + ".wav";
        ffmpeg_convert_audio(mp3_path, wav_path);

        // Play
        g_preview_wav = wav_path;
        PlaySoundA(wav_path.c_str(), NULL, SND_FILENAME | SND_ASYNC);

        // Wait for playback to finish (estimate: file size / 176400 bytes/sec for 44.1kHz stereo 16-bit)
        // Then clean up the MP3
        Sleep(500);
        DeleteFileA(mp3_path.c_str());

        // Auto-stop flag after a reasonable time
        for (int i = 0; i < 30 && g_preview_playing; i++) Sleep(100);
        g_preview_playing = false;
    }).detach();
}
