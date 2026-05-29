#pragma once
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include "miniaudio.h"

enum PlayMode {
    PLAY_RESTART = 0,
    PLAY_PAUSE,
    PLAY_STOP
};

struct SoundEffectsState {
    float fade_in = 0.0f;
    float fade_out = 0.0f;
    float playback_speed = 1.0f;
    float pitch_semitones = 0.0f; // stacks on top of playback_speed

    // Stereo
    float pan = 0.0f;

    // Standard FX
    bool bass_boost = false;
    bool custom_reverb = false;
    float reverb_room_size = 0.5f;
    float reverb_damping = 0.2f;
    float reverb_wet = 0.3f;

    // Creative FX
    bool bitcrusher = false;
    int bitcrusher_bits = 8;

    // EQ
    bool eq_enabled = false;
    float eq_low_gain = 0.0f;
    float eq_mid_gain = 0.0f;
    float eq_high_gain = 0.0f;
    float eq_mid_freq = 1000.0f;

    // Filters
    bool lowpass_enabled = false;
    float lowpass_cutoff = 8000.0f;
    bool highpass_enabled = false;
    float highpass_cutoff = 20.0f;

    // NEW: Band-Pass Filter
    bool bpf_enabled = false;
    float bpf_cutoff = 1000.0f;
    int bpf_order = 2;

    // NEW: Notch Filter
    bool notch_enabled = false;
    float notch_freq = 1000.0f;
    float notch_q = 1.0f;

    // NEW: Low Shelf Filter
    bool loshelf_enabled = false;
    float loshelf_gain = 0.0f;
    float loshelf_freq = 300.0f;
    float loshelf_slope = 1.0f;

    // NEW: High Shelf Filter
    bool hishelf_enabled = false;
    float hishelf_gain = 0.0f;
    float hishelf_freq = 3000.0f;
    float hishelf_slope = 1.0f;

    // Delay / Echo
    bool echo_enabled = false;
    float echo_delay = 0.3f;
    float echo_feedback = 0.4f;
    float echo_mix = 0.5f;

    // Distortion / Saturation
    bool distortion_enabled = false;
    float distortion_drive = 1.0f;
    float distortion_tone = 0.5f;
    float distortion_mix = 1.0f;

    bool saturation_enabled = false;
    float saturation_drive = 1.0f;
    float saturation_warmth = 0.5f;
    float saturation_mix = 1.0f;

    // Modulation
    bool flanger_enabled = false;
    float flanger_rate = 0.5f;
    float flanger_depth = 0.5f;

    bool chorus_enabled = false;
    float chorus_rate = 0.3f;
    float chorus_depth = 0.5f;
    float chorus_mix = 0.5f;

    bool phaser_enabled = false;
    float phaser_rate = 0.5f;
    float phaser_depth = 0.5f;
    float phaser_feedback = 0.5f;

    bool tremolo_enabled = false;
    float tremolo_rate = 4.0f;
    float tremolo_depth = 0.5f;

    bool ringmod_enabled = false;
    float ringmod_freq = 200.0f;
    float ringmod_mix = 0.5f;

    bool autowah_enabled = false;
    float autowah_rate = 2.0f;
    float autowah_depth = 0.5f;
    float autowah_resonance = 0.5f;

    // Stereo
    bool stereo_widener_enabled = false;
    float stereo_width = 1.0f;

    // Dynamics
    bool compressor_enabled = false;
    float compressor_threshold = -12.0f;
    float compressor_ratio = 4.0f;
    float compressor_attack = 0.005f;
    float compressor_release = 0.2f;
    float compressor_makeup = 1.0f;

    bool noise_gate_enabled = false;
    float noise_gate_threshold = -40.0f;
    float noise_gate_attack = 0.001f;
    float noise_gate_release = 0.05f;

    bool limiter_enabled = false;
    float limiter_threshold = -1.0f;
    float limiter_release = 0.1f;
};

struct SoundVoice {
    ma_sound* spk = nullptr;
    ma_sound* vrt = nullptr;
};

struct SoundFxChain {
    ma_sound_group group;
    ma_hpf_node hpf;
    ma_lpf_node lpf;
    ma_bpf_node bpf;
    ma_notch_node notch;
    ma_loshelf_node loshelf_eq;
    ma_peak_node peak_mid;
    ma_hishelf_node hishelf_eq;
    ma_loshelf_node loshelf_extra;
    ma_hishelf_node hishelf_extra;
    ma_delay_node delay;
    bool initialized = false;

    ~SoundFxChain() {
        if (!initialized) return;
        ma_delay_node_uninit(&delay, NULL);
        ma_hishelf_node_uninit(&hishelf_extra, NULL);
        ma_loshelf_node_uninit(&loshelf_extra, NULL);
        ma_hishelf_node_uninit(&hishelf_eq, NULL);
        ma_peak_node_uninit(&peak_mid, NULL);
        ma_loshelf_node_uninit(&loshelf_eq, NULL);
        ma_notch_node_uninit(&notch, NULL);
        ma_bpf_node_uninit(&bpf, NULL);
        ma_lpf_node_uninit(&lpf, NULL);
        ma_hpf_node_uninit(&hpf, NULL);
        ma_sound_group_uninit(&group);
        initialized = false;
    }
};

struct Sound {
    std::string name;
    std::string path;

    PlayMode play_mode = PLAY_RESTART;
    bool loop_track = false;
    bool overlap_enabled = false;
    bool muted = false;

    bool trim_enabled = false;
    float trim_start = 0.0f;
    float trim_end = 1.0f;
    float volume_amp = 1.0f;

    SoundEffectsState fx;

    int hotkey = -1;
    std::string web_url;

    ma_decoder vis_decoder;
    bool vis_ready = false;
    unsigned int thumb_tex_id = 0;
    SoundFxChain* fx_chain = nullptr;
    std::vector<SoundVoice> active_voices;

    ~Sound() {
        if (vis_ready) {
            ma_decoder_uninit(&vis_decoder);
            vis_ready = false;
        }
    }
};

// Global State Variables shared across files
extern std::vector<std::unique_ptr<Sound>> sounds;
extern Sound* current_selected_sound;

extern int selected_speaker_idx;
extern int selected_virtual_idx;
extern int selected_mic_idx;
extern float master_volume;
extern bool stop_all_on_new_play;

extern std::atomic<bool> is_converting;
extern std::atomic<bool> needs_sound_reload;

extern Sound* g_capturing_hotkey_sound;

extern bool mic_muted;
extern bool virtual_muted;
extern bool deafen;
