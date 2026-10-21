#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

struct TTSVoice {
    const char* id;
    const char* name;
    const char* lang;
};

struct TTSWordBoundary {
    int64_t offset_ms;  // start time in ms (from audio start)
    int64_t duration_ms; // duration in ms
    std::string text;    // the word/text segment
};

// Microsoft Edge TTS neural voices — the ones streamers use for donations
static const TTSVoice TTS_VOICES[] = {
    {"en-US-AvaNeural",              "Ava (US Female)",              "en-US"},
    {"en-US-GuyNeural",              "Guy (US Male)",                "en-US"},
    {"en-US-JennyNeural",            "Jenny (US Female)",            "en-US"},
    {"en-US-AriaNeural",             "Aria (US Female)",             "en-US"},
    {"en-US-DavisNeural",            "Davis (US Male)",              "en-US"},
    {"en-US-JaneNeural",             "Jane (US Female)",             "en-US"},
    {"en-US-JasonNeural",            "Jason (US Male)",              "en-US"},
    {"en-US-SaraNeural",             "Sara (US Female)",             "en-US"},
    {"en-US-TonyNeural",             "Tony (US Male)",               "en-US"},
    {"en-GB-SoniaNeural",            "Sonia (UK Female)",            "en-GB"},
    {"en-GB-RyanNeural",             "Ryan (UK Male)",               "en-GB"},
    {"en-AU-NatashaNeural",          "Natasha (AU Female)",          "en-AU"},
    {"en-AU-WilliamNeural",          "William (AU Male)",            "en-AU"},
    {"es-ES-ElviraNeural",           "Elvira (ES Female)",           "es-ES"},
    {"fr-FR-DeniseNeural",           "Denise (FR Female)",           "fr-FR"},
    {"de-DE-KatjaNeural",            "Katja (DE Female)",            "de-DE"},
    {"ja-JP-NanamiNeural",           "Nanami (JP Female)",           "ja-JP"},
    {"ko-KR-SunHiNeural",            "SunHi (KR Female)",            "ko-KR"},
    {"zh-CN-XiaoxiaoNeural",         "Xiaoxiao (CN Female)",         "zh-CN"},
    {"pt-BR-FranciscaNeural",        "Francisca (BR Female)",        "pt-BR"},
    {"ru-RU-SvetlanaNeural",         "Svetlana (RU Female)",         "ru-RU"},
    {"hi-IN-SwaraNeural",            "Swara (IN Female)",            "hi-IN"},
};
static const int TTS_VOICE_COUNT = sizeof(TTS_VOICES) / sizeof(TTS_VOICES[0]);

// Generate TTS audio using Microsoft Edge TTS neural voices.
// volume: -100 to +100 (default 0). pitch: -50 to +50 in Hz (default 0).
// rate: -100 to +100 in percent (default 0).
bool tts_generate_edge(const std::string& text, const std::string& voice_id,
                       const std::string& out_path,
                       int volume = 0, int pitch = 0, int rate = 0);

// Preview TTS audio (plays it through speakers).
void tts_preview(const std::string& text, const std::string& voice_id,
                 int volume = 0, int pitch = 0, int rate = 0);

// Generate TTS and play through both speakers AND virtual mic.
void tts_talk(const std::string& text, const std::string& voice_id,
              int volume = 0, int pitch = 0, int rate = 0);

// Translate text using Google Translate (auto-detect source, target lang).
// Returns translated text, or original text on failure.
std::string tts_translate(const std::string& text, const std::string& target_lang = "en");

// Word boundary tracking for TTS playback highlighting
extern std::vector<TTSWordBoundary> g_tts_word_boundaries;
extern int64_t g_tts_playback_start_ms;  // timestamp when playback started
extern bool g_tts_is_playing;
extern int g_tts_search_pos;  // sequential search position for word highlighting

// Returns the index of the word currently being spoken (-1 if none).
int tts_get_current_word_index();

// Pause/resume TTS playback.
void tts_pause();

// Applies the volume, pitch and rate sliders to the sound that is
// currently playing, so the changes are heard right away.
// volume: -100 to +100 (default 0). pitch: -50 to +50 in Hz (default 0).
// rate: -100 to +100 in percent (default 0).
void tts_set_live_params(int volume, int pitch, int rate);

// Stop TTS playback immediately.
void tts_stop();

extern bool g_tts_paused;
