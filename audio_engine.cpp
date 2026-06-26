#define MINIAUDIO_IMPLEMENTATION
#include "audio_engine.h"
#include "config.h"
#include <iostream>
#include <cstring>
#include <cmath>
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

    float vol = sound.muted ? 0.0f : sound.volume_amp;
    float pitch = sound.fx_enabled
        ? sound.fx.playback_speed * std::pow(2.0f, sound.fx.pitch_semitones / 12.0f)
        : 1.0f;
    float pan = sound.fx.pan;

    SoundVoice v;

    if (speakers_engine_initialized) {
        v.spk = new ma_sound();
        ma_sound_init_from_file(&engine_speakers,
            short_path(sound.path).c_str(),
            0,
            NULL,
            NULL,
            v.spk);

        ma_sound_set_volume(v.spk, vol);
        ma_sound_set_pitch(v.spk, pitch);
        ma_sound_set_pan(v.spk, pan);
        if (sound.loop_track && !sound.trim_enabled)
            ma_sound_set_looping(v.spk, MA_TRUE);
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

        ma_sound_set_volume(v.vrt, vol);
        ma_sound_set_pitch(v.vrt, pitch);
        ma_sound_set_pan(v.vrt, pan);
        if (sound.loop_track && !sound.trim_enabled)
            ma_sound_set_looping(v.vrt, MA_TRUE);
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
    v.cached_vol = vol;
    v.cached_pitch = pitch;
    v.cached_pan = pan;
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
        std::filesystem::create_directories(folder);

    for (auto& entry : std::filesystem::recursive_directory_iterator(folder))
    {
        if (!entry.is_regular_file()) continue;

        auto ext = entry.path().extension().string();

        if (ext == ".mp3" || ext == ".wav" || ext == ".ogg")
        {
            auto s = std::make_unique<Sound>();
            s->name = path_to_utf8(entry.path().stem());
            s->path = path_to_utf8(entry.path());

            // Compute folder relative to the root sounds/ directory
            std::string rel = std::filesystem::relative(entry.path(), folder).string();
            for (auto& c : rel) if (c == '\\') c = '/';
            auto parent = std::filesystem::path(rel).parent_path().string();
            s->folder = parent;

            ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
            if (ma_decoder_init_file(short_path(s->path).c_str(), &cfg, &s->vis_decoder) == MA_SUCCESS)
                s->vis_ready = true;

            sounds.push_back(std::move(s));
        }
    }
}

void stop_all_sounds()
{
    for (auto& s : sounds) stop_sound(*s);
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
    std::string stem = "sounds/" + sound->folder;
    if (!stem.empty()) stem += "/";
    stem += sound->name;
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
    std::string dir_prefix = "sounds/";
    if (!sound->folder.empty()) dir_prefix += sound->folder + "/";
    std::string old_stem = dir_prefix + sound->name;
    std::string new_stem = dir_prefix + new_name;

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

// ================= FOLDER MANAGEMENT =================

static std::string get_sounds_dir() {
    std::string dir = "sounds";
    if (!current_folder.empty())
        dir = "sounds/" + current_folder;
    return dir;
}

void create_folder(const std::string& name)
{
    std::string dir = get_sounds_dir() + "/" + name;
    std::filesystem::create_directories(dir);
}

void delete_folder(const std::string& folder_path)
{
    std::error_code ec;
    // Clean up orphaned profile keys before deleting the folder
    std::string folder = folder_path.length() > 7 ? folder_path.substr(7) : "";
    remove_profiles_folder(folder);
    std::filesystem::remove_all(utf8_to_path(folder_path), ec);
}

void rename_folder(const std::string& old_path, const std::string& new_name)
{
    std::error_code ec;
    std::string parent = std::filesystem::path(old_path).parent_path().string();
    // Rekey all profiles under the old folder name before renaming
    std::string old_folder = old_path.length() > 7 ? old_path.substr(7) : "";
    std::string new_folder = parent.length() > 7 ? parent.substr(7) : "";
    if (!new_folder.empty()) new_folder += "/";
    new_folder += new_name;
    rekey_profiles_folder(old_folder, new_folder);
    std::filesystem::rename(utf8_to_path(old_path), utf8_to_path(parent + "/" + new_name), ec);
}

// ================= COPY / CUT / PASTE =================

static void copy_sound_files(const std::string& src_stem, const std::string& dest_stem)
{
    std::error_code ec;
    for (auto ext : { ".mp3", ".wav", ".ogg", ".jpg", ".jpeg", ".png", ".bmp", ".tga", ".ppm" }) {
        std::string src = src_stem + ext;
        std::string dst = dest_stem + ext;
        if (std::filesystem::exists(utf8_to_path(src)))
            std::filesystem::copy_file(utf8_to_path(src), utf8_to_path(dst),
                std::filesystem::copy_options::overwrite_existing, ec);
    }
}

static void move_sound_files(const std::string& src_stem, const std::string& dest_stem)
{
    std::error_code ec;
    for (auto ext : { ".mp3", ".wav", ".ogg", ".jpg", ".jpeg", ".png", ".bmp", ".tga", ".ppm" }) {
        std::string src = src_stem + ext;
        std::string dst = dest_stem + ext;
        if (std::filesystem::exists(utf8_to_path(src)))
            std::filesystem::rename(utf8_to_path(src), utf8_to_path(dst), ec);
    }
}

static void copy_directory_recursive(const std::filesystem::path& src, const std::filesystem::path& dst) {
    std::error_code ec;
    std::filesystem::create_directories(dst, ec);
    for (auto& entry : std::filesystem::recursive_directory_iterator(src, ec)) {
        auto rel = std::filesystem::relative(entry.path(), src, ec);
        auto target = dst / rel;
        if (entry.is_directory()) {
            std::filesystem::create_directories(target, ec);
        } else {
            std::filesystem::copy_file(entry.path(), target,
                std::filesystem::copy_options::overwrite_existing, ec);
        }
    }
}

void copy_sound_to_folder(Sound* sound, const std::string& dest_folder)
{
    std::string src_stem = "sounds/" + sound->folder;
    if (!src_stem.empty() && src_stem.back() != '/') src_stem += "/";
    src_stem += sound->name;

    std::string dest_stem = "sounds/" + dest_folder;
    if (!dest_stem.empty() && dest_stem.back() != '/') dest_stem += "/";
    dest_stem += sound->name;

    copy_sound_files(src_stem, dest_stem);

    std::string old_key = sound->folder.empty() ? sound->name : sound->folder + "/" + sound->name;
    std::string new_key = dest_folder.empty() ? sound->name : dest_folder + "/" + sound->name;
    copy_profile_key(old_key, new_key);
}

void cut_sound_to_folder(Sound* sound, const std::string& dest_folder)
{
    if (sound->vis_ready) {
        ma_decoder_uninit(&sound->vis_decoder);
        sound->vis_ready = false;
    }
    stop_sound(*sound);

    std::string src_stem = "sounds/" + sound->folder;
    if (!src_stem.empty() && src_stem.back() != '/') src_stem += "/";
    src_stem += sound->name;

    std::string dest_stem = "sounds/" + dest_folder;
    if (!dest_stem.empty() && dest_stem.back() != '/') dest_stem += "/";
    dest_stem += sound->name;

    move_sound_files(src_stem, dest_stem);

    auto ext = std::filesystem::path(sound->path).extension().string();
    sound->path = dest_stem + ext;
    sound->folder = dest_folder;

    if (std::filesystem::exists(utf8_to_path(sound->path))) {
        ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
        if (ma_decoder_init_file(short_path(sound->path).c_str(), &cfg, &sound->vis_decoder) == MA_SUCCESS)
            sound->vis_ready = true;
    }

    save_config_to_json();
}

void paste_clipboard()
{
    if (clipboard_type == CLIP_NONE) return;

    if (clipboard_type == CLIP_SOUND) {
        Sound* src = nullptr;
        for (auto& s : sounds) {
            if (s->path == clipboard_source_path) { src = s.get(); break; }
        }

        if (clipboard_is_cut) {
            if (src) {
                cut_sound_to_folder(src, current_folder);
            } else {
                // Sound not in current view (navigated away) — move files directly
                std::string ext = std::filesystem::path(clipboard_source_path).extension().string();
                std::string dest_stem = "sounds/" + current_folder;
                if (!dest_stem.empty() && dest_stem.back() != '/') dest_stem += "/";
                dest_stem += clipboard_source_name;

                std::error_code ec;
                std::string src_base = clipboard_source_path.substr(0, clipboard_source_path.length() - ext.length());
                std::string dst_base = dest_stem;

                std::string exts[] = { ext, ".jpg", ".jpeg", ".png", ".bmp", ".tga", ".ppm" };
                for (auto& e : exts) {
                    std::string s_file = src_base + e;
                    std::string d_file = dst_base + e;
                    if (std::filesystem::exists(utf8_to_path(s_file)))
                        std::filesystem::rename(utf8_to_path(s_file), utf8_to_path(d_file), ec);
                }

                // Rekey profile: derive old folder from source path
                std::string old_stem = clipboard_source_path.substr(0, clipboard_source_path.length() - ext.length());
                std::string old_rel = old_stem.length() > 7 ? old_stem.substr(7) : "";
                std::string old_folder;
                auto slash_pos = old_rel.rfind('/');
                if (slash_pos != std::string::npos) old_folder = old_rel.substr(0, slash_pos);
                rekey_profiles_folder(old_folder, current_folder);
            }
            clipboard_type = CLIP_NONE;
        } else {
            if (src) {
                copy_sound_to_folder(src, current_folder);
            } else {
                // Sound not in current view — copy files directly
                std::string ext = std::filesystem::path(clipboard_source_path).extension().string();
                std::string dest_stem = "sounds/" + current_folder;
                if (!dest_stem.empty() && dest_stem.back() != '/') dest_stem += "/";
                dest_stem += clipboard_source_name;

                std::error_code ec;
                std::string src_base = clipboard_source_path.substr(0, clipboard_source_path.length() - ext.length());
                std::string dst_base = dest_stem;

                std::string exts[] = { ext, ".jpg", ".jpeg", ".png", ".bmp", ".tga", ".ppm" };
                for (auto& e : exts) {
                    std::string s_file = src_base + e;
                    std::string d_file = dst_base + e;
                    if (std::filesystem::exists(utf8_to_path(s_file)))
                        std::filesystem::copy_file(utf8_to_path(s_file), utf8_to_path(d_file),
                            std::filesystem::copy_options::overwrite_existing, ec);
                }

                // Copy profile: derive old folder from source path
                std::string old_stem = clipboard_source_path.substr(0, clipboard_source_path.length() - ext.length());
                std::string old_rel = old_stem.length() > 7 ? old_stem.substr(7) : "";
                std::string old_folder;
                auto slash_pos = old_rel.rfind('/');
                if (slash_pos != std::string::npos) old_folder = old_rel.substr(0, slash_pos);
                copy_profiles_folder(old_folder, current_folder);
            }
        }
    }
    else if (clipboard_type == CLIP_FOLDER) {
        if (clipboard_is_cut) {
            std::string dest = "sounds/" + current_folder;
            if (!dest.empty()) dest += "/";
            dest += clipboard_source_name;
            std::error_code ec;
            std::filesystem::rename(utf8_to_path(clipboard_source_path), utf8_to_path(dest), ec);
            // Rekey all profiles under the old folder to the new location
            std::string old_folder = clipboard_source_path.length() > 7
                ? clipboard_source_path.substr(7) : "";
            std::string new_folder = current_folder.empty()
                ? clipboard_source_name
                : current_folder + "/" + clipboard_source_name;
            rekey_profiles_folder(old_folder, new_folder);
            clipboard_type = CLIP_NONE;
        } else {
            std::string dest = "sounds/" + current_folder;
            if (!dest.empty()) dest += "/";
            dest += clipboard_source_name;
            std::error_code ec;
            copy_directory_recursive(utf8_to_path(clipboard_source_path),
                utf8_to_path(dest));
            // Copy all profiles under the old folder to the new location
            std::string src_folder = clipboard_source_path.length() > 7
                ? clipboard_source_path.substr(7) : "";
            std::string dest_folder = current_folder.empty()
                ? clipboard_source_name
                : current_folder + "/" + clipboard_source_name;
            copy_profiles_folder(src_folder, dest_folder);
        }
    }

    needs_sound_reload = true;
}

void copy_folder_to(const std::string& folder_name, const std::string& dest_folder)
{
    std::string src = "sounds/" + folder_name;
    std::string dst = "sounds/" + dest_folder;
    if (!dst.empty()) dst += "/";
    dst += folder_name;
    std::error_code ec;
    copy_directory_recursive(utf8_to_path(src), utf8_to_path(dst));
}

void cut_folder_to(const std::string& folder_name, const std::string& dest_folder)
{
    std::string src = "sounds/" + folder_name;
    std::string dst = "sounds/" + dest_folder;
    if (!dst.empty()) dst += "/";
    dst += folder_name;
    std::error_code ec;
    std::filesystem::rename(utf8_to_path(src), utf8_to_path(dst), ec);
}
