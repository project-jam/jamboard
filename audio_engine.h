#pragma once
#include "data_models.h"

#include <string>

extern ma_context context;
extern ma_device_info* playbackDevices;
extern ma_uint32 playbackDeviceCount;
extern ma_device_info* captureDevices;
extern ma_uint32 captureDeviceCount;

extern ma_engine engine_speakers;
extern ma_engine engine_virtual;
extern ma_device mic_pipe_device;
extern ma_device mic_speaker_pipe_device;

extern bool speakers_engine_initialized;
extern bool virtual_engine_initialized;
extern bool mic_device_initialized;
extern bool mic_speaker_device_initialized;
extern bool mic_passthrough_enabled;

bool init_audio_system();
void shutdown_audio_routing();
bool setup_audio_routing();

void load_sounds(const std::string& folder);
void play_sound(Sound& sound);
void stop_sound(Sound& sound);
void toggle_pause_sound(Sound& sound);
bool is_sound_playing(Sound& sound);
void clean_sound_voices(Sound& sound);
void set_master_volume(float vol);
void apply_volumes();
void set_deafen(bool on);
void set_virtual_muted(bool on);
void delete_sound(Sound* sound);
void rename_sound(Sound* sound, const std::string& new_name);

void init_sound_fx_chain(Sound& sound);
void init_all_sound_fx_chains();
void destroy_all_sound_fx_chains();
void update_sound_fx_chain(Sound& sound);
void update_all_sound_fx_chains();
