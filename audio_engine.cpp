#define MINIAUDIO_IMPLEMENTATION
#include "audio_engine.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <filesystem>

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
    memcpy(pOutput, pInput, frameCount * 2 * sizeof(float));
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

    if (selected_mic_idx > 0 &&
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

void set_master_volume(float vol)
{
    master_volume = vol;
    if (speakers_engine_initialized)
        ma_engine_set_volume(&engine_speakers, vol);
    if (virtual_engine_initialized)
        ma_engine_set_volume(&engine_virtual, vol);
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
            sound.path.c_str(),
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
            sound.path.c_str(),
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

    if (v.spk) {
        ma_bool32 playing = ma_sound_is_playing(v.spk);
        if (playing) ma_sound_stop(v.spk);
        else ma_sound_start(v.spk);
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

    for (auto& entry : std::filesystem::directory_iterator(folder))
    {
        if (!entry.is_regular_file()) continue;

        auto path = entry.path().string();

        if (path.ends_with(".mp3") ||
            path.ends_with(".wav") ||
            path.ends_with(".ogg"))
        {
            auto s = std::make_unique<Sound>();
            s->name = entry.path().stem().string();
            s->path = path;

            ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
            if (ma_decoder_init_file(path.c_str(), &cfg, &s->vis_decoder) == MA_SUCCESS)
                s->vis_ready = true;

            sounds.push_back(std::move(s));
        }
    }
}

void delete_sound(Sound* sound)
{
    sounds.erase(
        std::remove_if(sounds.begin(), sounds.end(),
            [&](auto& s) { return s.get() == sound; }),
        sounds.end()
    );
}

void rename_sound(Sound* sound, const std::string& new_name)
{
    sound->name = new_name;
}
