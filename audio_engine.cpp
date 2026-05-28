#define MINIAUDIO_IMPLEMENTATION
#include "audio_engine.h"
#include "config.h"

#include <filesystem>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <GL/gl.h>

ma_context context;
ma_device_info* playbackDevices = nullptr;
ma_uint32 playbackDeviceCount = 0;
ma_device_info* captureDevices = nullptr;
ma_uint32 captureDeviceCount = 0;

ma_engine engine_speakers;
ma_engine engine_virtual;
ma_device mic_pipe_device;

bool speakers_engine_initialized = false;
bool virtual_engine_initialized = false;
bool mic_device_initialized = false;

// Global variable definitions
std::vector<std::unique_ptr<Sound>> sounds;
Sound* current_selected_sound = nullptr;

int selected_speaker_idx = -1;
int selected_virtual_idx = -1;
int selected_mic_idx = -1;

float master_volume = 1.0f;

bool stop_all_on_new_play = false;

std::atomic<bool> is_converting(false);
std::atomic<bool> needs_sound_reload(false);

Sound* g_capturing_hotkey_sound = nullptr;

// Microphone passthrough callback
void mic_passthrough_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    if (pInput == NULL || pOutput == NULL)
        return;

    // Assumes float stereo
    memcpy(pOutput, pInput, frameCount * 2 * sizeof(float));
}

bool init_audio_system()
{
    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
        std::cerr << "Failed to initialize audio context." << std::endl;
        return false;
    }

    ma_context_get_devices(
        &context,
        &playbackDevices,
        &playbackDeviceCount,
        &captureDevices,
        &captureDeviceCount
    );

    std::cout << "Audio system initialized." << std::endl;
    std::cout << "Playback devices: " << playbackDeviceCount << std::endl;
    std::cout << "Capture devices: " << captureDeviceCount << std::endl;

    return true;
}

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
    std::cout << "--- Applying Audio Routing ---" << std::endl;

    shutdown_audio_routing();

    // =========================================================
    // Speakers Engine
    // =========================================================

    ma_engine_config configSpk = ma_engine_config_init();

    if (selected_speaker_idx > 0 &&
        selected_speaker_idx - 1 < (int)playbackDeviceCount)
    {
        configSpk.pPlaybackDeviceID =
            &playbackDevices[selected_speaker_idx - 1].id;

        std::cout << "Speakers -> "
                  << playbackDevices[selected_speaker_idx - 1].name
                  << std::endl;
    }
    else {
        std::cout << "Speakers -> Default Device" << std::endl;
    }

    if (ma_engine_init(&configSpk, &engine_speakers) == MA_SUCCESS) {
        speakers_engine_initialized = true;
        ma_engine_set_volume(&engine_speakers, master_volume);
    }
    else {
        std::cerr << "Failed to initialize speakers engine." << std::endl;
    }

    // =========================================================
    // Virtual Engine
    // =========================================================

    if (selected_virtual_idx > 0 &&
        selected_virtual_idx - 1 < (int)playbackDeviceCount)
    {
        ma_engine_config configVrt = ma_engine_config_init();

        configVrt.pPlaybackDeviceID =
            &playbackDevices[selected_virtual_idx - 1].id;

        std::cout << "Virtual -> "
                  << playbackDevices[selected_virtual_idx - 1].name
                  << std::endl;

        if (ma_engine_init(&configVrt, &engine_virtual) == MA_SUCCESS) {
            virtual_engine_initialized = true;
            ma_engine_set_volume(&engine_virtual, master_volume);
        }
        else {
            std::cerr << "Failed to initialize virtual engine." << std::endl;
        }
    }
    else {
        std::cout << "Virtual routing disabled." << std::endl;
    }

    // =========================================================
    // Mic Passthrough
    // =========================================================

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

        std::cout << "Mic passthrough: "
                  << captureDevices[selected_mic_idx - 1].name
                  << " -> "
                  << playbackDevices[selected_virtual_idx - 1].name
                  << std::endl;

        if (ma_device_init(&context, &devConfig, &mic_pipe_device) == MA_SUCCESS)
        {
            ma_device_start(&mic_pipe_device);
            mic_device_initialized = true;
        }
        else {
            std::cerr << "Failed to initialize mic passthrough." << std::endl;
        }
    }

    return true;
}

void clean_sound_voices(Sound& sound)
{
    sound.active_voices.erase(
        std::remove_if(
            sound.active_voices.begin(),
            sound.active_voices.end(),
            [](SoundVoice& voice)
            {
                bool spk_done = (voice.spk == nullptr) || ma_sound_at_end(voice.spk);
                bool vrt_done = (voice.vrt == nullptr) || ma_sound_at_end(voice.vrt);

                if (spk_done && voice.spk) {
                    ma_sound_uninit(voice.spk);
                    delete voice.spk;
                    voice.spk = nullptr;
                }

                if (vrt_done && voice.vrt) {
                    ma_sound_uninit(voice.vrt);
                    delete voice.vrt;
                    voice.vrt = nullptr;
                }

                return (voice.spk == nullptr && voice.vrt == nullptr);
            }
        ),
        sound.active_voices.end()
    );
}

void set_master_volume(float vol)
{
    master_volume = vol;
    if (speakers_engine_initialized)
        ma_engine_set_volume(&engine_speakers, master_volume);
    if (virtual_engine_initialized)
        ma_engine_set_volume(&engine_virtual, master_volume);
}

void destroy_sound_fx_chain(Sound& sound);

void delete_sound(Sound* sound)
{
    if (!sound) return;

    std::string name = sound->name;

    // Clean up voices and fx chain
    destroy_sound_fx_chain(*sound);

    // Free visual decoder
    if (sound->vis_ready) {
        ma_decoder_uninit(&sound->vis_decoder);
        sound->vis_ready = false;
    }

    // Delete texture (OpenGL)
    if (sound->thumb_tex_id != 0) {
        glDeleteTextures(1, &sound->thumb_tex_id);
        sound->thumb_tex_id = 0;
    }

    // Delete audio files from disk
    for (auto& ext : {".mp3", ".wav", ".ogg"}) {
        std::string path = "sounds/" + name + ext;
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    std::string jpg_path = "sounds/" + name + ".jpg";
    std::error_code ec;
    std::filesystem::remove(jpg_path, ec);

    // Remove from sounds vector
    for (auto it = sounds.begin(); it != sounds.end(); ++it)
    {
        if (it->get() == sound)
        {
            if (current_selected_sound == sound)
                current_selected_sound = nullptr;
            sounds.erase(it);
            break;
        }
    }

    save_config_to_json();
}

void rename_sound(Sound* sound, const std::string& new_name)
{
    if (!sound || new_name.empty()) return;

    std::string old_name = sound->name;
    if (old_name == new_name) return;

    // Stop playback
    for (auto& voice : sound->active_voices)
    {
        if (voice.spk) { ma_sound_stop(voice.spk); ma_sound_uninit(voice.spk); delete voice.spk; voice.spk = nullptr; }
        if (voice.vrt) { ma_sound_stop(voice.vrt); ma_sound_uninit(voice.vrt); delete voice.vrt; voice.vrt = nullptr; }
    }
    sound->active_voices.clear();

    // Free old decoder
    if (sound->vis_ready) {
        ma_decoder_uninit(&sound->vis_decoder);
        sound->vis_ready = false;
    }

    // Determine extension from current path
    std::string ext = ".mp3";
    if (sound->path.ends_with(".wav")) ext = ".wav";
    else if (sound->path.ends_with(".ogg")) ext = ".ogg";

    std::string old_audio = "sounds/" + old_name + ext;
    std::string new_audio = "sounds/" + new_name + ext;
    std::string old_jpg   = "sounds/" + old_name + ".jpg";
    std::string new_jpg   = "sounds/" + new_name + ".jpg";

    // Rename files on disk
    std::error_code ec;
    std::filesystem::rename(old_audio, new_audio, ec);
    if (std::filesystem::exists(old_jpg))
        std::filesystem::rename(old_jpg, new_jpg, ec);

    // Update sound metadata
    sound->name = new_name;
    sound->path = new_audio;

    // Reinit decoder for waveform visualization
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    if (ma_decoder_init_file(new_audio.c_str(), &config, &sound->vis_decoder) == MA_SUCCESS)
        sound->vis_ready = true;

    save_config_to_json();
}

void play_sound(Sound& sound)
{
    if (sound.muted) return;

    current_selected_sound = &sound;

    if (stop_all_on_new_play) {
        for (auto& s : sounds) {
            if (s.get() != &sound)
                stop_sound(*s);
        }
    }

    if (!sound.overlap_enabled)
        stop_sound(sound);

    SoundVoice new_voice{};

    // =========================================================
    // Speakers Playback (routed through fx chain group)
    // =========================================================

    if (speakers_engine_initialized)
    {
        new_voice.spk = new ma_sound();

        ma_sound_group* group = (sound.fx_chain && sound.fx_chain->initialized)
            ? &sound.fx_chain->group : NULL;

        if (ma_sound_init_from_file(
                &engine_speakers,
                sound.path.c_str(),
                0,
                group,
                NULL,
                new_voice.spk) == MA_SUCCESS)
        {
            ma_sound_set_volume(new_voice.spk, sound.volume_amp);
            ma_sound_set_pitch(new_voice.spk, sound.fx.playback_speed);

            if (sound.trim_enabled && sound.trim_start > 0.0f)
            {
                float total_sec = 0.0f;

                ma_sound_get_length_in_seconds(
                    new_voice.spk,
                    &total_sec
                );

                ma_uint32 sr =
                    ma_engine_get_sample_rate(&engine_speakers);

                ma_sound_seek_to_pcm_frame(
                    new_voice.spk,
                    (ma_uint64)(sound.trim_start * total_sec * sr)
                );
            }
            else if (sound.loop_track)
            {
                ma_sound_set_looping(new_voice.spk, MA_TRUE);
            }

            ma_sound_start(new_voice.spk);
        }
        else {
            delete new_voice.spk;
            new_voice.spk = nullptr;
        }
    }

    // =========================================================
    // Virtual Playback (direct, no fx chain)
    // =========================================================

    if (virtual_engine_initialized)
    {
        new_voice.vrt = new ma_sound();

        if (ma_sound_init_from_file(
                &engine_virtual,
                sound.path.c_str(),
                0,
                NULL,
                NULL,
                new_voice.vrt) == MA_SUCCESS)
        {
            ma_sound_set_volume(new_voice.vrt, sound.volume_amp);
            ma_sound_set_pitch(new_voice.vrt, sound.fx.playback_speed);

            if (sound.trim_enabled && sound.trim_start > 0.0f)
            {
                float total_sec = 0.0f;

                ma_sound_get_length_in_seconds(
                    new_voice.vrt,
                    &total_sec
                );

                ma_uint32 sr =
                    ma_engine_get_sample_rate(&engine_virtual);

                ma_sound_seek_to_pcm_frame(
                    new_voice.vrt,
                    (ma_uint64)(sound.trim_start * total_sec * sr)
                );
            }
            else if (sound.loop_track)
            {
                ma_sound_set_looping(new_voice.vrt, MA_TRUE);
            }

            ma_sound_start(new_voice.vrt);
        }
        else {
            delete new_voice.vrt;
            new_voice.vrt = nullptr;
        }
    }

    sound.active_voices.push_back(new_voice);
}

void stop_sound(Sound& sound)
{
    for (auto& voice : sound.active_voices)
    {
        if (voice.spk) {
            ma_sound_stop(voice.spk);
            ma_sound_uninit(voice.spk);
            delete voice.spk;
        }

        if (voice.vrt) {
            ma_sound_stop(voice.vrt);
            ma_sound_uninit(voice.vrt);
            delete voice.vrt;
        }
    }

    sound.active_voices.clear();
}

void toggle_pause_sound(Sound& sound)
{
    for (auto& voice : sound.active_voices)
    {
        if (!voice.spk)
            continue;

        if (ma_sound_is_playing(voice.spk))
        {
            ma_sound_stop(voice.spk);

            if (voice.vrt)
                ma_sound_stop(voice.vrt);
        }
        else
        {
            ma_sound_start(voice.spk);

            if (voice.vrt)
                ma_sound_start(voice.vrt);
        }
    }
}

bool is_sound_playing(Sound& sound)
{
    if (sound.active_voices.empty())
        return false;

    if (!sound.active_voices.back().spk)
        return false;

    return ma_sound_is_playing(sound.active_voices.back().spk);
}

void load_sounds(const std::string& folder)
{
    if (!std::filesystem::exists(folder))
        std::filesystem::create_directory(folder);

    for (const auto& entry : std::filesystem::directory_iterator(folder))
    {
        if (!entry.is_regular_file())
            continue;

        std::string path = entry.path().string();

        if (path.ends_with(".mp3") ||
            path.ends_with(".wav") ||
            path.ends_with(".ogg"))
        {
            auto s = std::make_unique<Sound>();
            s->path = path;
            s->name = entry.path().stem().string();

            ma_decoder_config config =
                ma_decoder_config_init(ma_format_f32, 0, 0);

            if (ma_decoder_init_file(
                    path.c_str(),
                    &config,
                    &s->vis_decoder) == MA_SUCCESS)
            {
                s->vis_ready = true;
            }
            sounds.push_back(std::move(s));
        }
    }
}

// =========================================================
// FX Chain (Real-time DSP Node Graph)
// =========================================================

void init_sound_fx_chain(Sound& sound)
{
    if (!speakers_engine_initialized) return;
    if (sound.fx_chain) return;

    auto chain = new SoundFxChain();

    ma_node_graph* graph = ma_engine_get_node_graph(&engine_speakers);
    ma_node* endpoint = ma_engine_get_endpoint(&engine_speakers);
    ma_uint32 sr = ma_engine_get_sample_rate(&engine_speakers);
    ma_uint32 ch = 2;

    // Group (input junction for all voices of this sound)
    {
        ma_sound_group_config cfg = ma_sound_group_config_init_2(&engine_speakers);
        cfg.flags = MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT;
        ma_sound_group_init_ex(&engine_speakers, &cfg, &chain->group);
    }

    // HPF
    {
        ma_hpf_node_config cfg = ma_hpf_node_config_init(ch, sr, 20.0, 2);
        ma_hpf_node_init(graph, &cfg, NULL, &chain->hpf);
    }

    // LPF
    {
        ma_lpf_node_config cfg = ma_lpf_node_config_init(ch, sr, 20000.0, 2);
        ma_lpf_node_init(graph, &cfg, NULL, &chain->lpf);
    }

    // BPF
    {
        ma_bpf_node_config cfg = ma_bpf_node_config_init(ch, sr, 1000.0, 2);
        ma_bpf_node_init(graph, &cfg, NULL, &chain->bpf);
    }

    // Notch
    {
        ma_notch_node_config cfg = ma_notch_node_config_init(ch, sr, 1.0, 1000.0);
        ma_notch_node_init(graph, &cfg, NULL, &chain->notch);
    }

    // LoShelf (EQ low band)
    {
        ma_loshelf_node_config cfg = ma_loshelf_node_config_init(ch, sr, 0.0, 0.707, 300.0);
        ma_loshelf_node_init(graph, &cfg, NULL, &chain->loshelf_eq);
    }

    // Peak (EQ mid band)
    {
        ma_peak_node_config cfg = ma_peak_node_config_init(ch, sr, 0.0, 0.707, 1000.0);
        ma_peak_node_init(graph, &cfg, NULL, &chain->peak_mid);
    }

    // HiShelf (EQ high band)
    {
        ma_hishelf_node_config cfg = ma_hishelf_node_config_init(ch, sr, 0.0, 0.707, 3000.0);
        ma_hishelf_node_init(graph, &cfg, NULL, &chain->hishelf_eq);
    }

    // LoShelf (new)
    {
        ma_loshelf_node_config cfg = ma_loshelf_node_config_init(ch, sr, 0.0, 0.707, 300.0);
        ma_loshelf_node_init(graph, &cfg, NULL, &chain->loshelf_extra);
    }

    // HiShelf (new)
    {
        ma_hishelf_node_config cfg = ma_hishelf_node_config_init(ch, sr, 0.0, 0.707, 3000.0);
        ma_hishelf_node_init(graph, &cfg, NULL, &chain->hishelf_extra);
    }

    // Delay
    {
        ma_delay_node_config cfg = ma_delay_node_config_init(ch, sr, (ma_uint32)(0.3 * sr), 0.4f);
        ma_delay_node_init(graph, &cfg, NULL, &chain->delay);
    }

    // Wire: group → HPF → LPF → BPF → Notch → LoShelf(EQ) → Peak(Mid) → HiShelf(EQ) → LoShelf(extra) → HiShelf(extra) → Delay → endpoint
    ma_node_attach_output_bus(&chain->group,       0, &chain->hpf,         0);
    ma_node_attach_output_bus(&chain->hpf,          0, &chain->lpf,         0);
    ma_node_attach_output_bus(&chain->lpf,          0, &chain->bpf,         0);
    ma_node_attach_output_bus(&chain->bpf,          0, &chain->notch,       0);
    ma_node_attach_output_bus(&chain->notch,        0, &chain->loshelf_eq,  0);
    ma_node_attach_output_bus(&chain->loshelf_eq,   0, &chain->peak_mid,    0);
    ma_node_attach_output_bus(&chain->peak_mid,     0, &chain->hishelf_eq,  0);
    ma_node_attach_output_bus(&chain->hishelf_eq,   0, &chain->loshelf_extra, 0);
    ma_node_attach_output_bus(&chain->loshelf_extra, 0, &chain->hishelf_extra, 0);
    ma_node_attach_output_bus(&chain->hishelf_extra, 0, &chain->delay,       0);
    ma_node_attach_output_bus(&chain->delay,         0, endpoint,            0);

    chain->initialized = true;
    sound.fx_chain = chain;
}

void init_all_sound_fx_chains()
{
    for (auto& s : sounds)
        init_sound_fx_chain(*s);
}

void destroy_sound_fx_chain(Sound& sound)
{
    if (!sound.fx_chain) return;
    // Stop all active voices first (they may be attached to the group)
    for (auto& voice : sound.active_voices)
    {
        if (voice.spk) { ma_sound_stop(voice.spk); ma_sound_uninit(voice.spk); delete voice.spk; }
        if (voice.vrt) { ma_sound_stop(voice.vrt); ma_sound_uninit(voice.vrt); delete voice.vrt; }
    }
    sound.active_voices.clear();
    delete sound.fx_chain;
    sound.fx_chain = nullptr;
}

void destroy_all_sound_fx_chains()
{
    for (auto& s : sounds)
        destroy_sound_fx_chain(*s);
}

void update_sound_fx_chain(Sound& sound)
{
    if (!sound.fx_chain || !sound.fx_chain->initialized) return;

    auto& ch = *sound.fx_chain;
    auto& fx = sound.fx;
    ma_uint32 sr = ma_engine_get_sample_rate(&engine_speakers);
    ma_uint32 channels = 2;

    // HPF
    {
        double cutoff = fx.highpass_enabled ? (double)fx.highpass_cutoff : 5.0;
        ma_hpf_config cfg = ma_hpf_config_init(ma_format_f32, channels, sr, cutoff, 2);
        ma_hpf_node_reinit(&cfg, &ch.hpf);
    }

    // LPF
    {
        double cutoff = fx.lowpass_enabled ? (double)fx.lowpass_cutoff : (double)(sr * 0.49);
        ma_lpf_config cfg = ma_lpf_config_init(ma_format_f32, channels, sr, cutoff, 2);
        ma_lpf_node_reinit(&cfg, &ch.lpf);
    }

    // BPF
    {
        double cutoff = fx.bpf_enabled ? (double)fx.bpf_cutoff : (double)(sr * 0.49);
        ma_bpf_config cfg = ma_bpf_config_init(ma_format_f32, channels, sr, cutoff, fx.bpf_enabled ? fx.bpf_order : 1);
        ma_bpf_node_reinit(&cfg, &ch.bpf);
    }

    // Notch
    {
        double freq = fx.notch_enabled ? (double)fx.notch_freq : 20.0;
        double q = fx.notch_enabled ? (double)fx.notch_q : 0.1;
        ma_notch2_config cfg = ma_notch2_config_init(ma_format_f32, channels, sr, q, freq);
        ma_notch_node_reinit(&cfg, &ch.notch);
    }

    // LoShelf (EQ low band)
    {
        double gain = fx.eq_enabled ? (double)fx.eq_low_gain : 0.0;
        ma_loshelf2_config cfg = ma_loshelf2_config_init(ma_format_f32, channels, sr, gain, 1.0, 300.0);
        ma_loshelf_node_reinit(&cfg, &ch.loshelf_eq);
    }

    // Peak (EQ mid band)
    {
        double gain = fx.eq_enabled ? (double)fx.eq_mid_gain : 0.0;
        ma_peak2_config cfg = ma_peak2_config_init(ma_format_f32, channels, sr, gain, 0.707, (double)fx.eq_mid_freq);
        ma_peak_node_reinit(&cfg, &ch.peak_mid);
    }

    // HiShelf (EQ high band)
    {
        double gain = fx.eq_enabled ? (double)fx.eq_high_gain : 0.0;
        ma_hishelf2_config cfg = ma_hishelf2_config_init(ma_format_f32, channels, sr, gain, 1.0, 3000.0);
        ma_hishelf_node_reinit(&cfg, &ch.hishelf_eq);
    }

    // LoShelf (new extra)
    {
        double gain = fx.loshelf_enabled ? (double)fx.loshelf_gain : 0.0;
        ma_loshelf2_config cfg = ma_loshelf2_config_init(ma_format_f32, channels, sr, gain, (double)fx.loshelf_slope, (double)fx.loshelf_freq);
        ma_loshelf_node_reinit(&cfg, &ch.loshelf_extra);
    }

    // HiShelf (new extra)
    {
        double gain = fx.hishelf_enabled ? (double)fx.hishelf_gain : 0.0;
        ma_hishelf2_config cfg = ma_hishelf2_config_init(ma_format_f32, channels, sr, gain, (double)fx.hishelf_slope, (double)fx.hishelf_freq);
        ma_hishelf_node_reinit(&cfg, &ch.hishelf_extra);
    }

    // Delay
    {
        float wet = fx.echo_enabled ? fx.echo_mix : 0.0f;
        ma_delay_node_set_wet(&ch.delay, wet);
        ma_delay_node_set_dry(&ch.delay, 1.0f);
        ma_delay_node_set_decay(&ch.delay, fx.echo_feedback);
    }
}

void update_all_sound_fx_chains()
{
    for (auto& s : sounds)
        update_sound_fx_chain(*s);
}
