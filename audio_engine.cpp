#define MINIAUDIO_IMPLEMENTATION
#include "audio_engine.h"
#include "config.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <windows.h>

// Convert UTF-8 path to 8.3 short path so miniaudio (which uses fopen) can handle unicode
static std::string short_path(const std::string& utf8) {
    wchar_t wide[MAX_PATH];
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide, MAX_PATH);
    if (len == 0) return utf8;
    wchar_t short_w[MAX_PATH];
    DWORD slen = GetShortPathNameW(wide, short_w, MAX_PATH);
    if (slen == 0) return utf8;
    char result[MAX_PATH * 3];
    int rlen = WideCharToMultiByte(CP_UTF8, 0, short_w, -1, result, sizeof(result), NULL, NULL);
    if (rlen == 0) return utf8;
    return result;
}

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

// ================= INIT =================

bool init_audio_system()
{
    ma_result result = ma_context_init(NULL, 0, NULL, &context);
    if (result != MA_SUCCESS) {
        std::cout << "Failed to init miniaudio context\n";
        return false;
    }

    result = ma_context_get_devices(
        &context,
        &playbackDevices,
        &playbackDeviceCount,
        &captureDevices,
        &captureDeviceCount
    );

    if (result != MA_SUCCESS) {
        std::cout << "Failed to get devices\n";
        return false;
    }

    return true;
}

// Microphone passthrough callback
void mic_passthrough_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    if (pInput == NULL || pOutput == NULL)
        return;
    if (mic_muted) {
        memset(pOutput, 0, frameCount * 2 * sizeof(float));
    } else {
        memcpy(pOutput, pInput, frameCount * 2 * sizeof(float));
    }
}

// ================= ROUTING =================

void shutdown_audio_routing()
{
    if (mic_device_initialized) {
        ma_device_uninit(&mic_pipe_device);
        mic_device_initialized = false;
    }

    if (virtual_engine_initialized) {
        ma_engine_uninit(&engine_virtual);
        virtual_engine_initialized = false;
    }

    if (speakers_engine_initialized) {
        ma_engine_uninit(&engine_speakers);
        speakers_engine_initialized = false;
    }
}

bool setup_audio_routing()
{
    shutdown_audio_routing();

    ma_engine_config configSpk = ma_engine_config_init();
    if (selected_speaker_idx > 0 &&
        selected_speaker_idx - 1 < (int)playbackDeviceCount)
    {
        configSpk.pPlaybackDeviceID = &playbackDevices[selected_speaker_idx - 1].id;
    }

    if (ma_engine_init(&configSpk, &engine_speakers) == MA_SUCCESS)
        speakers_engine_initialized = true;

    if (selected_virtual_idx > 0 &&
        selected_virtual_idx - 1 < (int)playbackDeviceCount)
    {
        ma_engine_config configVrt = ma_engine_config_init();
        configVrt.pPlaybackDeviceID = &playbackDevices[selected_virtual_idx - 1].id;

        if (ma_engine_init(&configVrt, &engine_virtual) == MA_SUCCESS)
            virtual_engine_initialized = true;
    }

    if (mic_passthrough_enabled &&
        selected_mic_idx > 0 &&
        selected_mic_idx - 1 < (int)captureDeviceCount &&
        selected_virtual_idx > 0 &&
        selected_virtual_idx - 1 < (int)playbackDeviceCount)
    {
        ma_device_config devConfig =
            ma_device_config_init(ma_device_type_duplex);
        devConfig.capture.pDeviceID =
            &captureDevices[selected_mic_idx - 1].id;
        devConfig.playback.pDeviceID =
            &playbackDevices[selected_virtual_idx - 1].id;
        devConfig.capture.format = ma_format_f32;
        devConfig.capture.channels = 2;
        devConfig.playback.format = ma_format_f32;
        devConfig.playback.channels = 2;
        devConfig.dataCallback = mic_passthrough_callback;

        if (ma_device_init(&context, &devConfig, &mic_pipe_device) == MA_SUCCESS)
        {
            ma_device_start(&mic_pipe_device);
            mic_device_initialized = true;
        }
    }

    return true;
}

void apply_volumes()
{
    float spk_vol = deafen ? 0.0f : master_volume;
    float vrt_vol = (deafen || virtual_muted) ? 0.0f : master_volume;
    if (speakers_engine_initialized)
        ma_engine_set_volume(&engine_speakers, spk_vol);
    if (virtual_engine_initialized)
        ma_engine_set_volume(&engine_virtual, vrt_vol);
}

void set_master_volume(float vol)
{
    master_volume = vol;
    apply_volumes();
}

void set_deafen(bool on)
{
    deafen = on;
    apply_volumes();
}

void set_virtual_muted(bool on)
{
    virtual_muted = on;
    apply_volumes();
}

// ================= SOUND CONTROL =================

void play_sound(Sound& sound)
{
    if (!sound.overlap_enabled)
        stop_sound(sound);

    const size_t MAX_VOICES = 8;
    if (sound.active_voices.size() >= MAX_VOICES)
        return;

    SoundVoice v;

    if (speakers_engine_initialized) {
        v.spk = new ma_sound();
        ma_sound_init_from_file(&engine_speakers,
            short_path(sound.path).c_str(),
            0,
            NULL,
            NULL,
            v.spk);

        ma_sound_start(v.spk);

        if (sound.trim_enabled) {
            ma_uint64 total_frames = 0;
            ma_sound_get_length_in_pcm_frames(v.spk, &total_frames);
            if (total_frames > 0) {
                ma_uint64 start = (ma_uint64)(sound.trim_start * total_frames);
                ma_sound_seek_to_pcm_frame(v.spk, start);
            }
        }
    }

    if (virtual_engine_initialized) {
        v.vrt = new ma_sound();
        ma_sound_init_from_file(&engine_virtual,
            short_path(sound.path).c_str(),
            0,
            NULL,
            NULL,
            v.vrt);

        ma_sound_start(v.vrt);

        if (sound.trim_enabled) {
            ma_uint64 total_frames = 0;
            ma_sound_get_length_in_pcm_frames(v.vrt, &total_frames);
            if (total_frames > 0) {
                ma_uint64 start = (ma_uint64)(sound.trim_start * total_frames);
                ma_sound_seek_to_pcm_frame(v.vrt, start);
            }
        }
    }

    v.play_start = std::chrono::steady_clock::now();
    v.paused = false;
    sound.active_voices.push_back(v);
}

void stop_sound(Sound& sound)
{
    for (auto& v : sound.active_voices) {
        if (v.spk) {
            ma_sound_stop(v.spk);
            ma_sound_uninit(v.spk);
            delete v.spk;
        }
        if (v.vrt) {
            ma_sound_stop(v.vrt);
            ma_sound_uninit(v.vrt);
            delete v.vrt;
        }
    }
    sound.active_voices.clear();
}

void toggle_pause_sound(Sound& sound)
{
    if (sound.active_voices.empty()) return;

    auto& v = sound.active_voices.back();

    if (!v.paused) {
        // Pausing — capture current position
        auto elapsed = std::chrono::steady_clock::now() - v.play_start;
        float spd = sound.fx_enabled ? sound.fx.playback_speed : 1.0f;
        v.paused_cursor = std::chrono::duration<float>(elapsed).count() * spd;
        v.paused = true;
        if (v.spk) ma_sound_stop(v.spk);
        if (v.vrt) ma_sound_stop(v.vrt);
    } else {
        // Resuming — reset play_start so clock continues from paused position
        float spd = sound.fx_enabled ? sound.fx.playback_speed : 1.0f;
        v.play_start = std::chrono::steady_clock::now()
            - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<float>(v.paused_cursor / spd));
        v.paused = false;
        if (v.spk) ma_sound_start(v.spk);
        if (v.vrt) ma_sound_start(v.vrt);
    }
}

bool is_sound_playing(Sound& sound)
{
    if (sound.active_voices.empty()) return false;
    if (!sound.active_voices.back().spk) return false;
    return ma_sound_is_playing(sound.active_voices.back().spk);
}

void clean_sound_voices(Sound& sound)
{
    sound.active_voices.erase(
        std::remove_if(sound.active_voices.begin(), sound.active_voices.end(),
            [](SoundVoice& v) {
                if (v.paused) return false;
                return (!v.spk || !ma_sound_is_playing(v.spk)) &&
                       (!v.vrt || !ma_sound_is_playing(v.vrt));
            }),
        sound.active_voices.end()
    );
}

// ================= STUB FX (REAL LATER) =================

void init_all_sound_fx_chains() {}
void destroy_all_sound_fx_chains() {}
void update_all_sound_fx_chains() {}

// ================= SOUND MANAGEMENT =================

void load_sounds(const std::string& folder)
{
    sounds.clear();

    if (!std::filesystem::exists(folder))
        std::filesystem::create_directory(folder);

    for (auto& entry : std::filesystem::directory_iterator(folder))
    {
        if (!entry.is_regular_file()) continue;

        auto ext = entry.path().extension().string();

        if (ext == ".mp3" || ext == ".wav" || ext == ".ogg")
        {
            auto s = std::make_unique<Sound>();
            s->name = path_to_utf8(entry.path().stem());
            s->path = path_to_utf8(entry.path());

            ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
            if (ma_decoder_init_file(short_path(s->path).c_str(), &cfg, &s->vis_decoder) == MA_SUCCESS)
                s->vis_ready = true;

            sounds.push_back(std::move(s));
        }
    }
}

void delete_sound(Sound* sound)
{
    // Close vis_decoder first — Windows can't delete open files
    if (sound->vis_ready) {
        ma_decoder_uninit(&sound->vis_decoder);
        sound->vis_ready = false;
    }

    stop_sound(*sound);

    std::error_code ec;
    std::string stem = "sounds/" + sound->name;
    for (auto ext : { ".mp3", ".wav", ".ogg", ".jpg", ".jpeg", ".png", ".bmp", ".tga", ".ppm" })
        std::filesystem::remove(utf8_to_path(stem + ext), ec);

    sounds.erase(
        std::remove_if(sounds.begin(), sounds.end(),
            [&](auto& s) { return s.get() == sound; }),
        sounds.end()
    );
    save_config_to_json();
}

void rename_sound(Sound* sound, const std::string& new_name)
{
    std::string old_stem = "sounds/" + sound->name;
    std::string new_stem = "sounds/" + new_name;

    auto ext = std::filesystem::path(sound->path).extension().string();

    // Stop any active voices to release file handles
    for (auto& v : sound->active_voices) {
        if (v.spk) { ma_sound_stop(v.spk); ma_sound_uninit(v.spk); delete v.spk; v.spk = nullptr; }
        if (v.vrt) { ma_sound_stop(v.vrt); ma_sound_uninit(v.vrt); delete v.vrt; v.vrt = nullptr; }
    }
    sound->active_voices.clear();

    // Close vis decoder to release file handle
    bool had_vis = sound->vis_ready;
    if (had_vis) {
        ma_decoder_uninit(&sound->vis_decoder);
        sound->vis_ready = false;
    }

    std::error_code ec;
    std::filesystem::rename(utf8_to_path(sound->path), utf8_to_path(new_stem + ext), ec);
    if (ec) {
        if (had_vis) {
            ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
            if (ma_decoder_init_file(short_path(sound->path).c_str(), &cfg, &sound->vis_decoder) == MA_SUCCESS)
                sound->vis_ready = true;
        }
        return;
    }

    std::filesystem::rename(utf8_to_path(old_stem + ".jpg"), utf8_to_path(new_stem + ".jpg"), ec);
    ec.clear();
    std::filesystem::rename(utf8_to_path(old_stem + ".ppm"), utf8_to_path(new_stem + ".ppm"), ec);

    sound->name = new_name;
    sound->path = new_stem + ext;

    // Re-init vis decoder with new path
    if (had_vis) {
        ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
        if (ma_decoder_init_file(short_path(sound->path).c_str(), &cfg, &sound->vis_decoder) == MA_SUCCESS)
            sound->vis_ready = true;
    }

    save_config_to_json();
}
