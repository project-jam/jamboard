#include "tts.h"
#include "edge_drm.h"
#include "import_ffmpeg.h"
#include "platform.h"
#include "audio_engine.h"
#include "httplib.h"
#include "https_get.h"
#include "json.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <atomic>
#include <random>
#include <vector>
#include <string>
#include <ctime>
#include <cmath>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "libcrypto.lib")
#pragma comment(lib, "libssl.lib")
#define CLOSE_SOCKET closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#define CLOSE_SOCKET close
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/sha.h>

// ============================================================
// Constants (from rany2/edge-tts)
// ============================================================

static const char* WSS_HOST = "speech.platform.bing.com";
static const int WSS_PORT = 443;
static const char* TRUSTED_CLIENT_TOKEN = "6A5AA1D4EAFF4E9FB37E23D68491D6F4";
static const char* SEC_MS_GEC_VERSION = "2-143.0.3650.75";

static std::atomic<bool> g_tts_playing{false};
bool g_tts_paused = false;
static std::atomic<bool> g_tts_stop_requested{false};

// TTS playback handles
static ma_sound* g_tts_spk_sound = nullptr;
static ma_sound* g_tts_vrt_sound = nullptr;
static bool g_tts_use_virtual = false;
static ma_uint64 g_tts_paused_cursor = 0;
static int64_t g_tts_paused_at_ms = 0;
static bool g_tts_fallback_playing = false;   // Windows: PlaySoundA is running
#ifndef _WIN32
static pid_t g_tts_fallback_pid = -1;          // Unix: player process
#endif

// Word boundary globals
std::vector<TTSWordBoundary> g_tts_word_boundaries;
int64_t g_tts_playback_start_ms = 0;
bool g_tts_is_playing = false;
int g_tts_search_pos = 0;

// ============================================================
// DRM: Sec-MS-GEC token
// ============================================================

// ============================================================
// DRM: Sec-MS-GEC token (v2)
//
// The v2 token is "<CS64>-<SHA256>"; the endpoint answers HTTP 403
// to the old v1 tokens. The digest itself is a modified SHA-256,
// so the implementation lives in edge_drm.h.
// ============================================================

static std::string generate_sec_ms_gec() {
    return edge_drm::generate_sec_ms_gec_v2((double)std::time(nullptr),
                                             TRUSTED_CLIENT_TOKEN);
}

// ============================================================
// UUID
// ============================================================

static std::string generate_uuid_hex() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    const char* hex = "0123456789abcdef";
    std::string s;
    s.reserve(32);
    for (int i = 0; i < 32; i++) s += hex[dis(gen)];
    return s;
}

// ============================================================
// Voice name: en-US-AvaNeural -> Microsoft Server Speech Text to Speech Voice (en-US, AvaNeural)
// ============================================================

static std::string convert_voice_name(const std::string& voice_id) {
    auto first_dash = voice_id.find('-');
    if (first_dash == std::string::npos) return voice_id;

    auto second_dash = voice_id.find('-', first_dash + 1);
    if (second_dash == std::string::npos) return voice_id;

    std::string lang = voice_id.substr(0, first_dash);
    std::string rest = voice_id.substr(first_dash + 1);

    auto neural_pos = rest.rfind("Neural");
    if (neural_pos == std::string::npos) return voice_id;

    auto dash_before_neural = rest.rfind('-', neural_pos);
    if (dash_before_neural == std::string::npos) return voice_id;

    std::string region = rest.substr(0, dash_before_neural);
    std::string name = rest.substr(dash_before_neural + 1);

    auto name_dash = name.find('-');
    if (name_dash != std::string::npos) {
        region = region + "-" + name.substr(0, name_dash);
        name = name.substr(name_dash + 1);
    }

    return "Microsoft Server Speech Text to Speech Voice (" + lang + "-" + region + ", " + name + ")";
}

// ============================================================
// Text chunking (from rany2/edge-tts)
//   Splits into <=4096 byte chunks at natural boundaries,
//   respecting UTF-8 and XML entities.
// ============================================================

static std::string remove_incompatible_characters(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (unsigned char c : text) {
        int code = (int)c;
        if ((code <= 8) || (code >= 11 && code <= 12) || (code >= 14 && code <= 31))
            result += ' ';
        else
            result += c;
    }
    return result;
}

static std::vector<std::string> split_text_by_byte_length(const std::string& text, size_t byte_length) {
    std::vector<std::string> chunks;
    std::string remaining = text;

    while (remaining.size() > byte_length) {
        // Find last newline within limit
        size_t split_at = remaining.rfind('\n', byte_length);
        if (split_at == std::string::npos) {
            // Find last space within limit
            split_at = remaining.rfind(' ', byte_length);
        }
        if (split_at == std::string::npos || split_at == 0) {
            // No natural boundary — find safe UTF-8 split
            split_at = byte_length;
            while (split_at > 0 && ((unsigned char)remaining[split_at] & 0xC0) == 0x80)
                split_at--;
            if (split_at == 0) split_at = byte_length; // fallback
        }

        // Don't split inside XML entity like &amp;
        while (split_at > 0) {
            size_t amp = remaining.rfind('&', split_at);
            if (amp == std::string::npos) break;
            size_t semi = remaining.find(';', amp);
            if (semi == std::string::npos || semi >= split_at) {
                split_at = amp; // move before the &
            } else {
                break; // entity is fully contained
            }
        }

        std::string chunk = remaining.substr(0, split_at);
        // Trim whitespace
        size_t start = chunk.find_first_not_of(" \t\n\r");
        size_t end = chunk.find_last_not_of(" \t\n\r");
        if (start != std::string::npos)
            chunks.push_back(chunk.substr(start, end - start + 1));

        remaining = remaining.substr(split_at);
    }

    // Trim and push remaining
    size_t start = remaining.find_first_not_of(" \t\n\r");
    size_t end = remaining.find_last_not_of(" \t\n\r");
    if (start != std::string::npos)
        chunks.push_back(remaining.substr(start, end - start + 1));

    return chunks;
}

// ============================================================
// XML escape
// ============================================================

static std::string xml_escape(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  result += "&amp;";  break;
            case '<':  result += "&lt;";   break;
            case '>':  result += "&gt;";   break;
            case '"':  result += "&quot;";  break;
            case '\'': result += "&apos;"; break;
            default:   result += c;        break;
        }
    }
    return result;
}

// ============================================================
// TLS WebSocket helpers
// ============================================================

static bool tls_send_all(SSL* ssl, const void* buf, size_t len) {
    const char* p = (const char*)buf;
    while (len > 0) {
        int n = SSL_write(ssl, p, (int)len);
        if (n <= 0) return false;
        p += n;
        len -= n;
    }
    return true;
}

static bool ws_send_frame(SSL* ssl, int opcode, const void* data, size_t len) {
    std::vector<unsigned char> header;
    header.push_back(0x80 | (opcode & 0x0F));

    if (len < 126) {
        header.push_back(0x80 | (unsigned char)len);
    } else if (len < 65536) {
        header.push_back(0x80 | 126);
        header.push_back((unsigned char)((len >> 8) & 0xFF));
        header.push_back((unsigned char)(len & 0xFF));
    } else {
        header.push_back(0x80 | 127);
        for (int i = 7; i >= 0; i--)
            header.push_back((unsigned char)((len >> (8 * i)) & 0xFF));
    }

    unsigned char mask[4];
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 255);
        for (int i = 0; i < 4; i++) mask[i] = (unsigned char)dis(gen);
    }
    header.insert(header.end(), mask, mask + 4);

    std::vector<unsigned char> masked(len);
    const unsigned char* src = (const unsigned char*)data;
    for (size_t i = 0; i < len; i++) masked[i] = src[i] ^ mask[i % 4];

    if (!tls_send_all(ssl, header.data(), header.size())) return false;
    if (len > 0 && !tls_send_all(ssl, masked.data(), masked.size())) return false;
    return true;
}

static bool ws_send_text(SSL* ssl, const std::string& text) {
    return ws_send_frame(ssl, 0x1, text.data(), text.size());
}

static int ws_recv_frame(SSL* ssl, std::vector<unsigned char>& payload) {
    struct pollfd pfd;
    pfd.fd = SSL_get_fd(ssl);
    pfd.events = POLLIN;
    if (poll(&pfd, 1, 30000) <= 0) return -1;

    unsigned char hdr[2];
    if (SSL_read(ssl, hdr, 2) != 2) return -1;

    int opcode = hdr[0] & 0x0F;
    uint64_t payload_len = hdr[1] & 0x7F;

    if (payload_len == 126) {
        unsigned char ext[2];
        if (SSL_read(ssl, ext, 2) != 2) return -1;
        payload_len = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (payload_len == 127) {
        unsigned char ext[8];
        if (SSL_read(ssl, ext, 8) != 8) return -1;
        payload_len = 0;
        for (int i = 0; i < 8; i++) payload_len = (payload_len << 8) | ext[i];
    }

    payload.resize(payload_len);
    if (payload_len > 0) {
        size_t total = 0;
        while (total < payload_len) {
            int n = SSL_read(ssl, payload.data() + (int)total, (int)(payload_len - total));
            if (n <= 0) return -1;
            total += n;
        }
    }

    return opcode;
}

static std::string get_header(const std::string& data, const std::string& key) {
    std::string prefix = key + ":";
    size_t pos = data.find(prefix);
    if (pos == std::string::npos) return "";
    pos += prefix.size();
    size_t end = data.find("\r\n", pos);
    if (end == std::string::npos) return data.substr(pos);
    return data.substr(pos, end - pos);
}

// ============================================================
// Base64 encode
// ============================================================

static std::string base64_encode_raw(const unsigned char* data, size_t len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned int n = (unsigned int)data[i] << 16;
        if (i + 1 < len) n |= (unsigned int)data[i + 1] << 8;
        if (i + 2 < len) n |= (unsigned int)data[i + 2];
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += (i + 1 < len) ? table[(n >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? table[n & 0x3F] : '=';
    }
    return out;
}

// ============================================================
// Date string
// ============================================================

static std::string date_to_string() {
    time_t now = time(nullptr);
    struct tm* g = gmtime(&now);
    char buf[128];
    strftime(buf, sizeof(buf), "%a %b %d %Y %H:%M:%S GMT+0000 (Coordinated Universal Time)", g);
    return buf;
}

// ============================================================
// WebSocket connect (TCP + TLS + WS upgrade)
//   Returns SSL* on success, sets *sock_out.
//   Returns nullptr on failure.
// ============================================================

static SSL* ws_connect(SSL_CTX* ctx_out, int* sock_out) {
    struct addrinfo hints = {}, *res;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    std::string port_str = std::to_string(WSS_PORT);
    if (getaddrinfo(WSS_HOST, port_str.c_str(), &hints, &res) != 0) return nullptr;

    // Try every resolved address. Hosts often advertise an AAAA record first
    // even on machines without IPv6 connectivity, so falling back to the
    // next address is required for the connection to succeed.
    int sock = -1;
    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        int s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s < 0) continue;
        if (connect(s, p->ai_addr, p->ai_addrlen) == 0) {
            sock = s;
            break;
        }
        CLOSE_SOCKET(s);
    }
    freeaddrinfo(res);
    if (sock < 0) return nullptr;

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { CLOSE_SOCKET(sock); return nullptr; }
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);
    SSL_set_tlsext_host_name(ssl, WSS_HOST);
    if (SSL_connect(ssl) <= 0) { SSL_free(ssl); SSL_CTX_free(ctx); CLOSE_SOCKET(sock); return nullptr; }

    // WS key
    unsigned char key_raw[16];
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 255);
        for (int i = 0; i < 16; i++) key_raw[i] = (unsigned char)dis(gen);
    }
    std::string b64_key = base64_encode_raw(key_raw, 16);
    std::string conn_id = generate_uuid_hex();
    std::string sec_gec = generate_sec_ms_gec();
    std::string muid = generate_uuid_hex();

    std::string upgrade =
        std::string("GET /consumer/speech/synthesize/readaloud/edge/v1?"
                    "TrustedClientToken=") + TRUSTED_CLIENT_TOKEN +
        "&ConnectionId=" + conn_id +
        "&Sec-MS-GEC=" + sec_gec +
        "&Sec-MS-GEC-Version=" + SEC_MS_GEC_VERSION +
        " HTTP/1.1\r\n"
        "Host: " + std::string(WSS_HOST) + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + b64_key + "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Pragma: no-cache\r\n"
        "Cache-Control: no-cache\r\n"
        "Origin: chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold\r\n"
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
        " (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36"
        " Edg/143.0.0.0\r\n"
        "Cookie: muid=" + muid + ";\r\n"
        "\r\n";

    if (!tls_send_all(ssl, upgrade.data(), upgrade.size())) {
        SSL_free(ssl); SSL_CTX_free(ctx); CLOSE_SOCKET(sock); return nullptr;
    }

    // Read HTTP 101
    std::string response;
    while (response.find("\r\n\r\n") == std::string::npos) {
        char c;
        if (SSL_read(ssl, &c, 1) <= 0) { SSL_free(ssl); SSL_CTX_free(ctx); CLOSE_SOCKET(sock); return nullptr; }
        response += c;
    }
    if (response.find("101") == std::string::npos) {
        SSL_free(ssl); SSL_CTX_free(ctx); CLOSE_SOCKET(sock); return nullptr;
    }

    *sock_out = sock;
    ctx_out = ctx; // caller owns
    return ssl;
}

static void ws_disconnect(SSL* ssl, SSL_CTX* ctx, int sock) {
    {
        std::vector<unsigned char> close_frame = {0x88, 0x80, 0, 0, 0, 0};
        tls_send_all(ssl, close_frame.data(), close_frame.size());
    }
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    CLOSE_SOCKET(sock);
}

// ============================================================
// Send one SSML chunk and receive audio back
// ============================================================

static bool send_ssml_and_recv_audio(SSL* ssl, const std::string& conn_id,
                                     const std::string& voice,
                                     const std::string& chunk,
                                     int volume, int pitch, int rate,
                                     std::vector<unsigned char>& audio_out,
                                     std::vector<TTSWordBoundary>& words_out)
{
    std::string ts = date_to_string();
    std::string pitch_str = (pitch >= 0 ? "+" : "") + std::to_string(pitch) + "Hz";
    std::string rate_str = (rate >= 0 ? "+" : "") + std::to_string(rate) + "%";
    std::string volume_str = (volume >= 0 ? "+" : "") + std::to_string(volume) + "%";

    std::string ssml =
        "X-RequestId:" + conn_id + "\r\n"
        "Content-Type:application/ssml+xml\r\n"
        "X-Timestamp:" + ts + "Z\r\n"
        "Path:ssml\r\n\r\n"
        "<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='en-US'>"
        "<voice name='" + voice + "'>"
        "<prosody pitch='" + pitch_str + "' "
        "rate='" + rate_str + "' "
        "volume='" + volume_str + "'>"
        + chunk +
        "</prosody>"
        "</voice>"
        "</speak>";

    if (!ws_send_text(ssl, ssml)) return false;

    for (int turn_count = 0; turn_count < 10000; turn_count++) {
        std::vector<unsigned char> payload;
        int opcode = ws_recv_frame(ssl, payload);
        if (opcode < 0) return false;
        if (opcode == 0x8) return false;

        if (opcode == 0x2 && payload.size() >= 2) {
            uint16_t header_len = ((uint16_t)payload[0] << 8) | payload[1];
            if (header_len + 2 <= payload.size()) {
                std::string headers(payload.begin() + 2, payload.begin() + 2 + header_len);
                if (get_header(headers, "Path") == "audio") {
                    audio_out.insert(audio_out.end(),
                        payload.begin() + 2 + header_len, payload.end());
                }
            }
        }

        if (opcode == 0x1) {
            std::string msg(payload.begin(), payload.end());
            if (msg.find("Path:turn.end") != std::string::npos) break;

            // Parse word boundary metadata
            if (msg.find("Path:audio.metadata") != std::string::npos) {
                size_t json_start = msg.find("\r\n\r\n");
                if (json_start != std::string::npos) {
                    json_start += 4;
                    std::string json_str = msg.substr(json_start);
                    try {
                        auto j = nlohmann::json::parse(json_str);
                        if (j.contains("Metadata") && j["Metadata"].is_array()) {
                            for (auto& meta : j["Metadata"]) {
                                if (meta.contains("Type") && meta["Type"] == "WordBoundary") {
                                    if (meta.contains("Data")) {
                                        auto& data = meta["Data"];
                                        int64_t offset_ticks = data.value("Offset", 0LL);
                                        int64_t duration_ticks = data.value("Duration", 0LL);
                                        std::string word;
                                        if (data.contains("text") && data["text"].contains("Text")) {
                                            word = data["text"]["Text"].get<std::string>();
                                        }
                                        TTSWordBoundary wb;
                                        wb.offset_ms = offset_ticks / 10000;
                                        wb.duration_ms = duration_ticks / 10000;
                                        wb.text = word;
                                        words_out.push_back(wb);
                                    }
                                }
                            }
                        }
                    } catch (...) {}
                }
            }
        }
    }

    return true;
}

// ============================================================
// Core: connect, send config, iterate chunks, collect audio
// ============================================================

static bool edge_tts_synthesize(const std::string& text, const std::string& voice_id,
                                const std::string& out_mp3,
                                int volume, int pitch, int rate,
                                std::vector<TTSWordBoundary>* words_out = nullptr)
{
    if (text.empty() || voice_id.empty()) return false;

    std::string voice = convert_voice_name(voice_id);
    std::string cleaned = remove_incompatible_characters(text);
    std::string escaped = xml_escape(cleaned);
    auto chunks = split_text_by_byte_length(escaped, 4096);
    if (chunks.empty()) return false;

    // Each chunk gets its own WebSocket connection
    std::vector<unsigned char> audio_data;
    std::vector<TTSWordBoundary> all_words;
    for (size_t i = 0; i < chunks.size(); i++) {
        SSL_CTX* ctx = nullptr;
        int sock = -1;
        SSL* ssl = ws_connect(ctx, &sock);
        if (!ssl) continue;

        // Send speech.config
        {
            std::string ts = date_to_string();
            std::string config =
                "X-Timestamp:" + ts + "\r\n"
                "Content-Type:application/json; charset=utf-8\r\n"
                "Path:speech.config\r\n\r\n"
                "{\"context\":{\"synthesis\":{\"audio\":{\"metadataoptions\":{"
                "\"sentenceBoundaryEnabled\":\"false\",\"wordBoundaryEnabled\":\"true\""
                "},"
                "\"outputFormat\":\"audio-24khz-48kbitrate-mono-mp3\""
                "}}}}";
            if (!ws_send_text(ssl, config)) { ws_disconnect(ssl, ctx, sock); continue; }
        }

        // Send SSML and receive audio
        std::string conn_id = generate_uuid_hex();
        send_ssml_and_recv_audio(ssl, conn_id, voice, chunks[i],
                                 volume, pitch, rate, audio_data, all_words);

        ws_disconnect(ssl, ctx, sock);
    }

    if (audio_data.empty()) return false;

    if (words_out) *words_out = all_words;

    FILE* fp = fopen(out_mp3.c_str(), "wb");
    if (!fp) return false;
    fwrite(audio_data.data(), 1, audio_data.size(), fp);
    fclose(fp);
    return true;
}

// ============================================================
// Non-blocking TTS playback with pause/stop
// ============================================================

static void tts_play_cleanup_internal() {
    if (g_tts_spk_sound) {
        ma_sound_stop(g_tts_spk_sound);
        ma_sound_uninit(g_tts_spk_sound);
        delete g_tts_spk_sound;
        g_tts_spk_sound = nullptr;
    }
    if (g_tts_vrt_sound) {
        ma_sound_stop(g_tts_vrt_sound);
        ma_sound_uninit(g_tts_vrt_sound);
        delete g_tts_vrt_sound;
        g_tts_vrt_sound = nullptr;
    }
#ifdef _WIN32
    if (g_tts_fallback_playing) {
        PlaySoundA(NULL, NULL, 0);
        g_tts_fallback_playing = false;
    }
#else
    if (g_tts_fallback_pid > 0) {
        kill(g_tts_fallback_pid, SIGTERM);
        int st = 0;
        waitpid(g_tts_fallback_pid, &st, 0);
        g_tts_fallback_pid = -1;
    }
#endif
}

// Start playback through the system command line players. Used when the
// miniaudio engines could not be initialised, so the user still hears audio.
static bool tts_play_start_fallback(const std::string& path) {
#ifdef _WIN32
    if (PlaySoundA(path.c_str(), NULL, SND_FILENAME | SND_ASYNC)) {
        g_tts_fallback_playing = true;
        return true;
    }
    return false;
#else
    std::string cmd = "paplay \"" + path + "\" 2>/dev/null || aplay -q \"" + path +
                      "\" 2>/dev/null || ffplay -nodisp -autoexit -loglevel quiet \"" + path + "\" 2>/dev/null";

    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        _exit(127);
    }
    g_tts_fallback_pid = pid;
    return true;
#endif
}

static bool tts_play_start(const std::string& path, bool play_virtual) {
    tts_play_cleanup_internal();
    g_tts_use_virtual = play_virtual;
    g_tts_paused_cursor = 0;

    bool started = false;

    if (speakers_engine_initialized) {
        g_tts_spk_sound = new ma_sound();
        if (ma_sound_init_from_file(&engine_speakers, path.c_str(), 0, NULL, NULL, g_tts_spk_sound) == MA_SUCCESS) {
            ma_sound_start(g_tts_spk_sound);
            started = true;
        } else {
            delete g_tts_spk_sound;
            g_tts_spk_sound = nullptr;
        }
    }

    if (play_virtual && virtual_engine_initialized) {
        g_tts_vrt_sound = new ma_sound();
        if (ma_sound_init_from_file(&engine_virtual, path.c_str(), 0, NULL, NULL, g_tts_vrt_sound) == MA_SUCCESS) {
            ma_sound_start(g_tts_vrt_sound);
            started = true;
        } else {
            delete g_tts_vrt_sound;
            g_tts_vrt_sound = nullptr;
        }
    }

    // No usable audio engine - fall back to the system players so the
    // user still gets sound instead of silence.
    if (!started) {
        started = tts_play_start_fallback(path);
    }

    return started;
}

static bool tts_play_is_playing() {
#ifdef _WIN32
    if (g_tts_fallback_playing) return true;
#else
    if (g_tts_fallback_pid > 0) {
        int st = 0;
        pid_t r = waitpid(g_tts_fallback_pid, &st, WNOHANG);
        if (r == g_tts_fallback_pid) {
            g_tts_fallback_pid = -1;
            return false;
        }
        return true;
    }
#endif
    bool spk_done = !g_tts_spk_sound || !ma_sound_is_playing(g_tts_spk_sound);
    bool vrt_done = !g_tts_vrt_sound || !ma_sound_is_playing(g_tts_vrt_sound);
    return !spk_done || !vrt_done;
}

static void tts_play_toggle_pause() {
#ifndef _WIN32
    if (g_tts_fallback_pid > 0) {
        // The fallback player cannot be paused - just let it run.
        return;
    }
#endif
    if (g_tts_fallback_playing) {
        return;
    }

    if (g_tts_paused) {
        // Resume from where we stopped
        if (g_tts_spk_sound) {
            ma_sound_seek_to_pcm_frame(g_tts_spk_sound, g_tts_paused_cursor);
            ma_sound_start(g_tts_spk_sound);
        }
        if (g_tts_vrt_sound) {
            ma_sound_seek_to_pcm_frame(g_tts_vrt_sound, g_tts_paused_cursor);
            ma_sound_start(g_tts_vrt_sound);
        }
        g_tts_paused = false;
    } else {
        // Remember the position, then stop
        if (g_tts_spk_sound) {
            ma_sound_get_cursor_in_pcm_frames(g_tts_spk_sound, &g_tts_paused_cursor);
            ma_sound_stop(g_tts_spk_sound);
        }
        if (g_tts_vrt_sound) {
            ma_sound_get_cursor_in_pcm_frames(g_tts_vrt_sound, &g_tts_paused_cursor);
            ma_sound_stop(g_tts_vrt_sound);
        }
        g_tts_paused = true;
    }
}

static void tts_play_stop() {
    g_tts_stop_requested = true;
    tts_play_cleanup_internal();
}

void tts_play_cleanup() {
    tts_play_cleanup_internal();
}

// ============================================================
// Public API
// ============================================================

bool tts_generate_edge(const std::string& text, const std::string& voice_id,
                       const std::string& out_path,
                       int volume, int pitch, int rate)
{
    return edge_tts_synthesize(text, voice_id, out_path, volume, pitch, rate);
}

static void tts_generate_and_play(const std::string& text, const std::string& voice_id,
                                   int volume, int pitch, int rate, bool play_virtual)
{
    if (g_tts_playing) return;
    g_tts_playing = true;
    g_tts_stop_requested = false;
    g_tts_paused = false;

    std::thread([text, voice_id, volume, pitch, rate, play_virtual]() {
        std::string tmp_dir = get_temp_dir() + "/jamboard_tts";
        std::filesystem::create_directories(tmp_dir);

        std::string mp3_path = tmp_dir + "/tts.mp3";
        std::string wav_path = tmp_dir + "/tts.wav";

        g_tts_word_boundaries.clear();
        g_tts_is_playing = false;
        g_tts_search_pos = 0;

        bool ok = edge_tts_synthesize(text, voice_id, mp3_path, volume, pitch, rate, &g_tts_word_boundaries);
        if (ok && !g_tts_stop_requested) {
            ok = ffmpeg_convert_audio(mp3_path, wav_path);
            std::error_code ec;
            std::filesystem::remove(mp3_path, ec);
        } else {
            std::error_code ec;
            std::filesystem::remove(mp3_path, ec);
        }

        if (ok && !g_tts_stop_requested) {
            g_tts_is_playing = true;
            g_tts_playback_start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            g_tts_paused_at_ms = 0;

            tts_play_start(wav_path, play_virtual);

            while (tts_play_is_playing() && !g_tts_stop_requested) {
                if (g_tts_paused && g_tts_paused_at_ms == 0) {
                    g_tts_paused_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
                } else if (!g_tts_paused && g_tts_paused_at_ms != 0) {
                    // Shift the clock forward by the time spent paused so the
                    // word highlight stays in sync with the audio.
                    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
                    g_tts_playback_start_ms += (now - g_tts_paused_at_ms);
                    g_tts_paused_at_ms = 0;
                }
                sleep_ms(50);
            }

            tts_play_cleanup();
            g_tts_is_playing = false;
            g_tts_word_boundaries.clear();
            std::error_code ec;
            std::filesystem::remove(wav_path, ec);
        }
        g_tts_playing = false;
    }).detach();
}

void tts_preview(const std::string& text, const std::string& voice_id,
                 int volume, int pitch, int rate) {
    tts_generate_and_play(text, voice_id, volume, pitch, rate, false);
}

void tts_talk(const std::string& text, const std::string& voice_id,
              int volume, int pitch, int rate) {
    tts_generate_and_play(text, voice_id, volume, pitch, rate, true);
}

// ============================================================
// Google Translate (free API, no key)
// ============================================================

static std::string url_encode(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped += (char)c;
        } else if (c == ' ') {
            escaped += '+';
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            escaped += buf;
        }
    }
    return escaped;
}

std::string tts_translate(const std::string& text, const std::string& target_lang) {
    if (text.empty()) return text;

    // Google answers HTTP 429 for clients that use the OpenSSL default
    // cipher list, so https_get sends a curl like configuration.
    // The source language is detected automatically, with "en" as a
    // fallback for when the automatic detection is refused.
    const char* sources[] = { "auto", "en" };

    for (const char* source : sources) {
        std::string path = "/translate_a/single?client=gtx&sl=";
        path += source;
        path += "&tl=" + target_lang + "&dt=t&q=" + url_encode(text);

        HttpResponse res = https_get("translate.googleapis.com", path);
        if (!res.ok || res.body.empty()) continue;
        if (res.body[0] != '[' && res.body[0] != '{') continue;

        // Response: [ [ ["translated","original",...], ... ], null, "detected_lang" ]
        // json[0] = array of segments, each segment[0] = translated text
        std::string result;
        try {
            auto json = nlohmann::json::parse(res.body);
            auto& segments = json[0];
            if (segments.is_array()) {
                for (auto& seg : segments) {
                    if (seg.is_array() && seg.size() >= 1 && seg[0].is_string()) {
                        result += seg[0].get<std::string>();
                    }
                }
            }
        } catch (...) {
            continue;
        }

        if (!result.empty()) return result;
    }

    return text;
}

// ============================================================
// Word boundary tracking
// ============================================================

int tts_get_current_word_index() {
    if (!g_tts_is_playing || g_tts_word_boundaries.empty()) return -1;

    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    int64_t elapsed = now_ms - g_tts_playback_start_ms;

    // Find the last word whose offset <= elapsed
    int idx = -1;
    for (int i = 0; i < (int)g_tts_word_boundaries.size(); i++) {
        if (g_tts_word_boundaries[i].offset_ms <= elapsed) {
            idx = i;
        } else {
            break;
        }
    }
    return idx;
}

void tts_pause() {
    tts_play_toggle_pause();
}

// Live tweaks for the sound that is currently playing.
// miniaudio resamples when the pitch of a sound changes, so the pitch
// and the rate end up on the same control. They are combined here into
// one factor, the volume is independent.
void tts_set_live_params(int volume, int pitch, int rate) {
    float vol = 1.0f + (float)volume / 100.0f;
    if (vol < 0.0f) vol = 0.0f;

    // Rate is a percentage change of the speaking speed, pitch is in Hz.
    float rate_factor = std::pow(2.0f, ((float)rate / 100.0f) * 0.5f);
    float pitch_factor = std::pow(2.0f, ((float)pitch / 50.0f) / 12.0f);
    float factor = rate_factor * pitch_factor;
    if (factor <= 0.01f) factor = 0.01f;

    if (g_tts_spk_sound) ma_sound_set_volume(g_tts_spk_sound, vol);
    if (g_tts_vrt_sound) ma_sound_set_volume(g_tts_vrt_sound, vol);
    if (g_tts_spk_sound) ma_sound_set_pitch(g_tts_spk_sound, factor);
    if (g_tts_vrt_sound) ma_sound_set_pitch(g_tts_vrt_sound, factor);
}

void tts_stop() {
    tts_play_stop();
}
