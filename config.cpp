#include "config.h"
#include "data_models.h"
#include "json.hpp"
#include <fstream>

using json = nlohmann::json;

void save_config_to_json() {
    json j;

    j["hardware"]["selected_speaker_idx"] = selected_speaker_idx;
    j["hardware"]["selected_virtual_idx"] = selected_virtual_idx;
    j["hardware"]["selected_mic_idx"] = selected_mic_idx;
    j["hardware"]["master_volume"] = master_volume;
    j["hardware"]["stop_all_on_new_play"] = stop_all_on_new_play;
    j["hardware"]["mic_muted"] = mic_muted;
    j["hardware"]["virtual_muted"] = virtual_muted;
    j["hardware"]["deafen"] = deafen;
    j["hardware"]["program_files"] = program_files_mode;
    j["hardware"]["scrub_enabled"] = scrub_enabled;
    j["hardware"]["mic_passthrough_enabled"] = mic_passthrough_enabled;
    j["hardware"]["vis_fps_mode"] = vis_fps_mode;
    j["hardware"]["vis_fps"] = vis_fps;

    for (const auto& s : sounds) {
        json sj;
        sj["play_mode"] = (int)s->play_mode;
        sj["loop_track"] = s->loop_track;
        sj["overlap_enabled"] = s->overlap_enabled;
        sj["muted"] = s->muted;
        sj["fx_enabled"] = s->fx_enabled;
        sj["trim_enabled"] = s->trim_enabled;
        sj["trim_start"] = s->trim_start;
        sj["trim_end"] = s->trim_end;
        sj["volume_amp"] = s->volume_amp;
        sj["hotkey"] = s->hotkey;
        sj["web_url"] = s->web_url;

        sj["fx"]["pan"] = s->fx.pan;
        sj["fx"]["fade_in"] = s->fx.fade_in;
        sj["fx"]["fade_out"] = s->fx.fade_out;
        sj["fx"]["playback_speed"] = s->fx.playback_speed;
        sj["fx"]["pitch_semitones"] = s->fx.pitch_semitones;
        sj["fx"]["bass_boost"] = s->fx.bass_boost;
        sj["fx"]["custom_reverb"] = s->fx.custom_reverb;
        sj["fx"]["reverb_room_size"] = s->fx.reverb_room_size;
        sj["fx"]["reverb_damping"] = s->fx.reverb_damping;
        sj["fx"]["reverb_wet"] = s->fx.reverb_wet;
        sj["fx"]["bitcrusher"] = s->fx.bitcrusher;
        sj["fx"]["bitcrusher_bits"] = s->fx.bitcrusher_bits;

        sj["fx"]["eq_enabled"] = s->fx.eq_enabled;
        sj["fx"]["eq_low_gain"] = s->fx.eq_low_gain;
        sj["fx"]["eq_mid_gain"] = s->fx.eq_mid_gain;
        sj["fx"]["eq_high_gain"] = s->fx.eq_high_gain;
        sj["fx"]["eq_mid_freq"] = s->fx.eq_mid_freq;

        sj["fx"]["lowpass_enabled"] = s->fx.lowpass_enabled;
        sj["fx"]["lowpass_cutoff"] = s->fx.lowpass_cutoff;
        sj["fx"]["highpass_enabled"] = s->fx.highpass_enabled;
        sj["fx"]["highpass_cutoff"] = s->fx.highpass_cutoff;

        sj["fx"]["bpf_enabled"] = s->fx.bpf_enabled;
        sj["fx"]["bpf_cutoff"] = s->fx.bpf_cutoff;
        sj["fx"]["bpf_order"] = s->fx.bpf_order;
        sj["fx"]["notch_enabled"] = s->fx.notch_enabled;
        sj["fx"]["notch_freq"] = s->fx.notch_freq;
        sj["fx"]["notch_q"] = s->fx.notch_q;
        sj["fx"]["loshelf_enabled"] = s->fx.loshelf_enabled;
        sj["fx"]["loshelf_gain"] = s->fx.loshelf_gain;
        sj["fx"]["loshelf_freq"] = s->fx.loshelf_freq;
        sj["fx"]["loshelf_slope"] = s->fx.loshelf_slope;
        sj["fx"]["hishelf_enabled"] = s->fx.hishelf_enabled;
        sj["fx"]["hishelf_gain"] = s->fx.hishelf_gain;
        sj["fx"]["hishelf_freq"] = s->fx.hishelf_freq;
        sj["fx"]["hishelf_slope"] = s->fx.hishelf_slope;

        sj["fx"]["echo_enabled"] = s->fx.echo_enabled;
        sj["fx"]["echo_delay"] = s->fx.echo_delay;
        sj["fx"]["echo_feedback"] = s->fx.echo_feedback;
        sj["fx"]["echo_mix"] = s->fx.echo_mix;

        sj["fx"]["distortion_enabled"] = s->fx.distortion_enabled;
        sj["fx"]["distortion_drive"] = s->fx.distortion_drive;
        sj["fx"]["distortion_tone"] = s->fx.distortion_tone;
        sj["fx"]["distortion_mix"] = s->fx.distortion_mix;

        sj["fx"]["saturation_enabled"] = s->fx.saturation_enabled;
        sj["fx"]["saturation_drive"] = s->fx.saturation_drive;
        sj["fx"]["saturation_warmth"] = s->fx.saturation_warmth;
        sj["fx"]["saturation_mix"] = s->fx.saturation_mix;

        sj["fx"]["flanger_enabled"] = s->fx.flanger_enabled;
        sj["fx"]["flanger_rate"] = s->fx.flanger_rate;
        sj["fx"]["flanger_depth"] = s->fx.flanger_depth;

        sj["fx"]["chorus_enabled"] = s->fx.chorus_enabled;
        sj["fx"]["chorus_rate"] = s->fx.chorus_rate;
        sj["fx"]["chorus_depth"] = s->fx.chorus_depth;
        sj["fx"]["chorus_mix"] = s->fx.chorus_mix;

        sj["fx"]["phaser_enabled"] = s->fx.phaser_enabled;
        sj["fx"]["phaser_rate"] = s->fx.phaser_rate;
        sj["fx"]["phaser_depth"] = s->fx.phaser_depth;
        sj["fx"]["phaser_feedback"] = s->fx.phaser_feedback;

        sj["fx"]["tremolo_enabled"] = s->fx.tremolo_enabled;
        sj["fx"]["tremolo_rate"] = s->fx.tremolo_rate;
        sj["fx"]["tremolo_depth"] = s->fx.tremolo_depth;

        sj["fx"]["ringmod_enabled"] = s->fx.ringmod_enabled;
        sj["fx"]["ringmod_freq"] = s->fx.ringmod_freq;
        sj["fx"]["ringmod_mix"] = s->fx.ringmod_mix;

        sj["fx"]["autowah_enabled"] = s->fx.autowah_enabled;
        sj["fx"]["autowah_rate"] = s->fx.autowah_rate;
        sj["fx"]["autowah_depth"] = s->fx.autowah_depth;
        sj["fx"]["autowah_resonance"] = s->fx.autowah_resonance;

        sj["fx"]["stereo_widener_enabled"] = s->fx.stereo_widener_enabled;
        sj["fx"]["stereo_width"] = s->fx.stereo_width;

        sj["fx"]["compressor_enabled"] = s->fx.compressor_enabled;
        sj["fx"]["compressor_threshold"] = s->fx.compressor_threshold;
        sj["fx"]["compressor_ratio"] = s->fx.compressor_ratio;
        sj["fx"]["compressor_attack"] = s->fx.compressor_attack;
        sj["fx"]["compressor_release"] = s->fx.compressor_release;
        sj["fx"]["compressor_makeup"] = s->fx.compressor_makeup;

        sj["fx"]["noise_gate_enabled"] = s->fx.noise_gate_enabled;
        sj["fx"]["noise_gate_threshold"] = s->fx.noise_gate_threshold;
        sj["fx"]["noise_gate_attack"] = s->fx.noise_gate_attack;
        sj["fx"]["noise_gate_release"] = s->fx.noise_gate_release;

        sj["fx"]["limiter_enabled"] = s->fx.limiter_enabled;
        sj["fx"]["limiter_threshold"] = s->fx.limiter_threshold;
        sj["fx"]["limiter_release"] = s->fx.limiter_release;

        j["profiles"][s->name] = sj;
    }

    std::ofstream file("config.json");
    if (file.is_open()) file << j.dump(4);
}

void load_config_from_json() {
    std::ifstream file("config.json");
    if (!file.is_open()) return;

    json j;
    file >> j;

    if (j.contains("hardware")) {
        selected_speaker_idx = j["hardware"].value("selected_speaker_idx", -1);
        selected_virtual_idx = j["hardware"].value("selected_virtual_idx", -1);
        selected_mic_idx = j["hardware"].value("selected_mic_idx", -1);
        master_volume = j["hardware"].value("master_volume", 1.0f);
        stop_all_on_new_play = j["hardware"].value("stop_all_on_new_play", false);
        mic_muted = j["hardware"].value("mic_muted", false);
        virtual_muted = j["hardware"].value("virtual_muted", false);
        deafen = j["hardware"].value("deafen", false);
        scrub_enabled = j["hardware"].value("scrub_enabled", false);
        mic_passthrough_enabled = j["hardware"].value("mic_passthrough_enabled", false);
        vis_fps_mode = j["hardware"].value("vis_fps_mode", 0);
        vis_fps = j["hardware"].value("vis_fps", 30);
    }

    if (j.contains("profiles")) {
        for (auto& s : sounds) {
            if (!j["profiles"].contains(s->name)) continue;
            auto sj = j["profiles"][s->name];

            s->play_mode = (PlayMode)sj.value("play_mode", 0);
            s->loop_track = sj.value("loop_track", false);
            s->overlap_enabled = sj.value("overlap_enabled", false);
            s->muted = sj.value("muted", false);
            s->fx_enabled = sj.value("fx_enabled", true);
            s->trim_enabled = sj.value("trim_enabled", false);
            s->trim_start = sj.value("trim_start", 0.0f);
            s->trim_end = sj.value("trim_end", 1.0f);
            s->volume_amp = sj.value("volume_amp", 1.0f);
            s->hotkey = sj.value("hotkey", -1);
            s->web_url = sj.value("web_url", "");

            if (!sj.contains("fx")) continue;
            auto fx = sj["fx"];

            s->fx.pan = fx.value("pan", 0.0f);
            s->fx.fade_in = fx.value("fade_in", 0.0f);
            s->fx.fade_out = fx.value("fade_out", 0.0f);
            s->fx.playback_speed = fx.value("playback_speed", 1.0f);
            s->fx.pitch_semitones = fx.value("pitch_semitones", 0.0f);
            s->fx.bass_boost = fx.value("bass_boost", false);
            s->fx.custom_reverb = fx.value("custom_reverb", false);
            s->fx.reverb_room_size = fx.value("reverb_room_size", 0.5f);
            s->fx.reverb_damping = fx.value("reverb_damping", 0.2f);
            s->fx.reverb_wet = fx.value("reverb_wet", 0.3f);
            s->fx.bitcrusher = fx.value("bitcrusher", false);
            s->fx.bitcrusher_bits = fx.value("bitcrusher_bits", 8);

            s->fx.eq_enabled = fx.value("eq_enabled", false);
            s->fx.eq_low_gain = fx.value("eq_low_gain", 0.0f);
            s->fx.eq_mid_gain = fx.value("eq_mid_gain", 0.0f);
            s->fx.eq_high_gain = fx.value("eq_high_gain", 0.0f);
            s->fx.eq_mid_freq = fx.value("eq_mid_freq", 1000.0f);

            s->fx.lowpass_enabled = fx.value("lowpass_enabled", false);
            s->fx.lowpass_cutoff = fx.value("lowpass_cutoff", 8000.0f);
            s->fx.highpass_enabled = fx.value("highpass_enabled", false);
            s->fx.highpass_cutoff = fx.value("highpass_cutoff", 20.0f);

            s->fx.bpf_enabled = fx.value("bpf_enabled", false);
            s->fx.bpf_cutoff = fx.value("bpf_cutoff", 1000.0f);
            s->fx.bpf_order = fx.value("bpf_order", 2);
            s->fx.notch_enabled = fx.value("notch_enabled", false);
            s->fx.notch_freq = fx.value("notch_freq", 1000.0f);
            s->fx.notch_q = fx.value("notch_q", 1.0f);
            s->fx.loshelf_enabled = fx.value("loshelf_enabled", false);
            s->fx.loshelf_gain = fx.value("loshelf_gain", 0.0f);
            s->fx.loshelf_freq = fx.value("loshelf_freq", 300.0f);
            s->fx.loshelf_slope = fx.value("loshelf_slope", 1.0f);
            s->fx.hishelf_enabled = fx.value("hishelf_enabled", false);
            s->fx.hishelf_gain = fx.value("hishelf_gain", 0.0f);
            s->fx.hishelf_freq = fx.value("hishelf_freq", 3000.0f);
            s->fx.hishelf_slope = fx.value("hishelf_slope", 1.0f);

            s->fx.echo_enabled = fx.value("echo_enabled", false);
            s->fx.echo_delay = fx.value("echo_delay", 0.3f);
            s->fx.echo_feedback = fx.value("echo_feedback", 0.4f);
            s->fx.echo_mix = fx.value("echo_mix", 0.5f);

            s->fx.distortion_enabled = fx.value("distortion_enabled", false);
            s->fx.distortion_drive = fx.value("distortion_drive", 1.0f);
            s->fx.distortion_tone = fx.value("distortion_tone", 0.5f);
            s->fx.distortion_mix = fx.value("distortion_mix", 1.0f);

            s->fx.saturation_enabled = fx.value("saturation_enabled", false);
            s->fx.saturation_drive = fx.value("saturation_drive", 1.0f);
            s->fx.saturation_warmth = fx.value("saturation_warmth", 0.5f);
            s->fx.saturation_mix = fx.value("saturation_mix", 1.0f);

            s->fx.flanger_enabled = fx.value("flanger_enabled", false);
            s->fx.flanger_rate = fx.value("flanger_rate", 0.5f);
            s->fx.flanger_depth = fx.value("flanger_depth", 0.5f);

            s->fx.chorus_enabled = fx.value("chorus_enabled", false);
            s->fx.chorus_rate = fx.value("chorus_rate", 0.3f);
            s->fx.chorus_depth = fx.value("chorus_depth", 0.5f);
            s->fx.chorus_mix = fx.value("chorus_mix", 0.5f);

            s->fx.phaser_enabled = fx.value("phaser_enabled", false);
            s->fx.phaser_rate = fx.value("phaser_rate", 0.5f);
            s->fx.phaser_depth = fx.value("phaser_depth", 0.5f);
            s->fx.phaser_feedback = fx.value("phaser_feedback", 0.5f);

            s->fx.tremolo_enabled = fx.value("tremolo_enabled", false);
            s->fx.tremolo_rate = fx.value("tremolo_rate", 4.0f);
            s->fx.tremolo_depth = fx.value("tremolo_depth", 0.5f);

            s->fx.ringmod_enabled = fx.value("ringmod_enabled", false);
            s->fx.ringmod_freq = fx.value("ringmod_freq", 200.0f);
            s->fx.ringmod_mix = fx.value("ringmod_mix", 0.5f);

            s->fx.autowah_enabled = fx.value("autowah_enabled", false);
            s->fx.autowah_rate = fx.value("autowah_rate", 2.0f);
            s->fx.autowah_depth = fx.value("autowah_depth", 0.5f);
            s->fx.autowah_resonance = fx.value("autowah_resonance", 0.5f);

            s->fx.stereo_widener_enabled = fx.value("stereo_widener_enabled", false);
            s->fx.stereo_width = fx.value("stereo_width", 1.0f);

            s->fx.compressor_enabled = fx.value("compressor_enabled", false);
            s->fx.compressor_threshold = fx.value("compressor_threshold", -12.0f);
            s->fx.compressor_ratio = fx.value("compressor_ratio", 4.0f);
            s->fx.compressor_attack = fx.value("compressor_attack", 0.005f);
            s->fx.compressor_release = fx.value("compressor_release", 0.2f);
            s->fx.compressor_makeup = fx.value("compressor_makeup", 1.0f);

            s->fx.noise_gate_enabled = fx.value("noise_gate_enabled", false);
            s->fx.noise_gate_threshold = fx.value("noise_gate_threshold", -40.0f);
            s->fx.noise_gate_attack = fx.value("noise_gate_attack", 0.001f);
            s->fx.noise_gate_release = fx.value("noise_gate_release", 0.05f);

            s->fx.limiter_enabled = fx.value("limiter_enabled", false);
            s->fx.limiter_threshold = fx.value("limiter_threshold", -1.0f);
            s->fx.limiter_release = fx.value("limiter_release", 0.1f);
        }
    }
}
