#include "audio_engine.h"
#include "data_models.h"
#include <atomic>

// audio engine globals
ma_context context;
ma_device_info* playbackDevices = nullptr;
ma_uint32 playbackDeviceCount = 0;
ma_device_info* captureDevices = nullptr;
ma_uint32 captureDeviceCount = 0;

ma_engine engine_speakers{};
ma_engine engine_virtual{};
ma_device mic_pipe_device{};
ma_device mic_speaker_pipe_device{};

bool speakers_engine_initialized = false;
bool virtual_engine_initialized = false;
bool mic_device_initialized = false;
bool mic_speaker_device_initialized = false;

// MUST match header exactly
std::vector<std::unique_ptr<Sound>> sounds;

bool stop_all_on_new_play = false;
std::atomic<bool> needs_sound_reload = false;
std::atomic<bool> is_converting = false;

int selected_speaker_idx = -1;
int selected_virtual_idx = -1;
int selected_mic_idx = -1;
Sound* g_capturing_hotkey_sound = nullptr;

float master_volume = 1.0f;
Sound* current_selected_sound = nullptr;

bool mic_muted = false;
bool virtual_muted = false;
bool deafen = false;
bool program_files_mode = false;
bool scrub_enabled = false;
bool mic_passthrough_enabled = false;
int vis_fps_mode = 0; // 0 = auto, 1 = manual
int vis_fps = 30;     // manual target FPS
