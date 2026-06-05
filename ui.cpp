#define _USE_MATH_DEFINES
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "ui.h"
#include "audio_engine.h"
#include "import_ffmpeg.h"
#include "import.h"
#include "config.h"
#include "update_checker.h"
#include "myinstants.h"
#include "imgui.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

#include <GL/gl.h>

#include <cstdlib>
#include <thread>
#include <filesystem>
#include <iostream>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

extern GLFWwindow* g_window;

float user_drag_progress = 0.0f;
static std::string download_error;
bool is_user_dragging_slider = false;
int seek_cooldown_frames = 0;

static char search_filter[128] = "";
static Sound* pending_delete_sound = nullptr;
static Sound* delete_confirm_sound = nullptr;
static char delete_confirm_name[256] = {};
static Sound* renaming_sound = nullptr;
static Sound* properties_sound = nullptr;

static char rename_buf[128] = {};
static bool open_rename_popup = false;
static bool open_properties_popup = false;
static bool open_delete_popup = false;

static char new_folder_buf[128] = {};
static bool open_new_folder_popup = false;
static char rename_folder_buf[128] = {};
static bool open_rename_folder_popup = false;
static std::string renaming_folder_path;

static std::string delete_confirm_folder_path;
static bool open_delete_folder_popup = false;

static unsigned int load_texture_from_file(const char* filename) {
    int width, height, channels;
    unsigned char* data = stbi_load(filename, &width, &height, &channels, 4);
    if (!data) return 0;

    unsigned int texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    return texture_id;
}

void init_ui_textures() {
    for (auto& s : sounds) {
        std::string base = "sounds/";
        if (!s->folder.empty()) base += s->folder + "/";
        base += s->name;
        for (auto ext : { ".ppm", ".jpg", ".jpeg", ".png", ".bmp", ".tga" }) {
            std::string img_path = base + ext;
            if (std::filesystem::exists(img_path)) {
                s->thumb_tex_id = load_texture_from_file(img_path.c_str());
                break;
            }
        }
    }
}

void shutdown_ui() {
    for (auto& s : sounds) {
        if (s->thumb_tex_id != 0)
            glDeleteTextures(1, &s->thumb_tex_id);
    }
}

static std::string path_to_utf8(const std::filesystem::path& p) {
    std::wstring w = p.wstring();
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    if (len == 0) return std::string();
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &result[0], len, NULL, NULL);
    return result;
}

// Helper: format seconds to mm:ss
static std::string fmt_time(float sec) {
    int m = (int)sec / 60;
    int s = (int)sec % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

static void draw_folder_icon(ImDrawList* dl, ImVec2 pos, float size) {
    ImVec2 tl = pos;
    ImVec2 br = ImVec2(pos.x + size, pos.y + size);
    float tab_h = size * 0.25f;
    float tab_w = size * 0.4f;

    // Folder tab (darker)
    ImVec4 tab_col = ImVec4(0.75f, 0.6f, 0.1f, 1.0f);
    dl->AddRectFilled(tl, ImVec2(tl.x + tab_w, tl.y + tab_h), ImGui::ColorConvertFloat4ToU32(tab_col));

    // Folder body
    ImVec4 body_col = ImVec4(0.9f, 0.75f, 0.2f, 1.0f);
    dl->AddRectFilled(ImVec2(tl.x, tl.y + tab_h), br, ImGui::ColorConvertFloat4ToU32(body_col));

    // Border
    ImVec4 border_col = ImVec4(0.6f, 0.48f, 0.08f, 1.0f);
    dl->AddRect(tl, br, ImGui::ColorConvertFloat4ToU32(border_col));
}

// Helper: normalize Unicode — convert Mathematical Alphanumeric Symbols to plain Latin
// so fonts that don't have these niche glyphs can still display the text
static std::string normalize_unicode(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    size_t i = 0;
    while (i < input.size()) {
        unsigned char c = input[i];
        char32_t codepoint = 0;
        int len = 0;

        if (c < 0x80) {
            codepoint = c;
            len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            codepoint = c & 0x1F;
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            codepoint = c & 0x0F;
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            codepoint = c & 0x07;
            len = 4;
        } else {
            i++;
            continue;
        }

        for (int j = 1; j < len && i + j < input.size(); j++) {
            unsigned char b = input[i + j];
            if ((b & 0xC0) != 0x80) { len = j; break; }
            codepoint = (codepoint << 6) | (b & 0x3F);
        }

        // Map Mathematical Alphanumeric Symbols (U+1D400-U+1D6A3) to plain Latin
        if (codepoint >= 0x1D400 && codepoint <= 0x1D6A3) {
            int offset = codepoint - 0x1D400;
            int block_offset = offset % 52;
            if (block_offset < 26)
                codepoint = (char32_t)('A' + block_offset);
            else
                codepoint = (char32_t)('a' + (block_offset - 26));
        }

        // Encode back to UTF-8
        if (codepoint < 0x80) {
            result += (char)codepoint;
        } else if (codepoint < 0x800) {
            result += (char)(0xC0 | (codepoint >> 6));
            result += (char)(0x80 | (codepoint & 0x3F));
        } else if (codepoint < 0x10000) {
            result += (char)(0xE0 | (codepoint >> 12));
            result += (char)(0x80 | ((codepoint >> 6) & 0x3F));
            result += (char)(0x80 | (codepoint & 0x3F));
        } else {
            result += (char)(0xF0 | (codepoint >> 18));
            result += (char)(0x80 | ((codepoint >> 12) & 0x3F));
            result += (char)(0x80 | ((codepoint >> 6) & 0x3F));
            result += (char)(0x80 | (codepoint & 0x3F));
        }

        i += len;
    }
    return result;
}

static std::string display_name(const std::string& name) {
    return normalize_unicode(name);
}

// Helper: reset button
static void ResetBtn(const char* label, float* var, float val) {
    ImGui::SameLine();
    ImGui::PushID(label);
    if (ImGui::SmallButton("R")) { *var = val; save_config_to_json(); }
    ImGui::PopID();
}

// Helper: center button
static void CenterBtn(const char* label, float* var, float val) {
    ImGui::SameLine();
    ImGui::PushID(label);
    if (ImGui::SmallButton("C")) { *var = val; save_config_to_json(); }
    ImGui::PopID();
}

// Helper: fx slider that saves on release
static bool FxSlider(const char* label, float* var, float lo, float hi,
                     const char* fmt = "%.2f", float reset_val = -1.0f)
{
    bool changed = false;
    ImGui::SetNextItemWidth(280);
    if (ImGui::SliderFloat(label, var, lo, hi, fmt))
        changed = true;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        save_config_to_json();
    }
    return changed;
}

// Helper: fx checkbox
static bool FxCheck(const char* label, bool* var) {
    bool r = ImGui::Checkbox(label, var);
    if (r) save_config_to_json();
    return r;
}

// Helper: draw a collapsible fx module header
static bool FxHeader(const char* label) {
    return ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
}

bool draw_ui() {
    // Process deferred sound deletion (safe, outside of any sound iteration)
    if (pending_delete_sound) {
        if (pending_delete_sound->thumb_tex_id != 0) {
            glDeleteTextures(1, &pending_delete_sound->thumb_tex_id);
        }
        if (current_selected_sound == pending_delete_sound)
            current_selected_sound = nullptr;
        if (properties_sound == pending_delete_sound)
            properties_sound = nullptr;
        if (g_capturing_hotkey_sound == pending_delete_sound)
            g_capturing_hotkey_sound = nullptr;
        delete_sound(pending_delete_sound);
        pending_delete_sound = nullptr;
    }

    if (needs_sound_reload) {
        g_capturing_hotkey_sound = nullptr;
        current_selected_sound = nullptr;
        destroy_all_sound_fx_chains();
        shutdown_audio_routing();
        sounds.clear();
        load_sounds("sounds");
        load_config_from_json();
        init_ui_textures();
        needs_sound_reload = false;
        setup_audio_routing();
        init_all_sound_fx_chains();
    }

    // Update all DSP effect chain parameters in real-time
    update_all_sound_fx_chains();

    bool engine_has_active_audio = false;

    for (auto& s : sounds) {
        clean_sound_voices(*s);
        if (!s->active_voices.empty())
            engine_has_active_audio = true;

        for (auto& voice : s->active_voices) {
            float vol = s->muted ? 0.0f : s->volume_amp;
            float pitch = s->fx_enabled
                ? s->fx.playback_speed * std::pow(2.0f, s->fx.pitch_semitones / 12.0f)
                : 1.0f;
            float pan = s->fx.pan;

            if (vol != voice.cached_vol || pitch != voice.cached_pitch || pan != voice.cached_pan) {
                for (auto sp : { voice.spk, voice.vrt }) {
                    if (!sp) continue;
                    ma_sound_set_volume(sp, vol);
                    ma_sound_set_pitch(sp, pitch);
                    ma_sound_set_pan(sp, pan);
                }
                voice.cached_vol = vol;
                voice.cached_pitch = pitch;
                voice.cached_pan = pan;
            }

            if (s->trim_enabled && voice.spk) {
                if (s->cached_total_frames == 0) {
                    ma_uint64 tf = 0;
                    ma_sound_get_length_in_pcm_frames(voice.spk, &tf);
                    s->cached_total_frames = tf;
                }
                if (s->cached_total_frames > 0) {
                    ma_uint64 cur_frame = 0;
                    ma_sound_get_cursor_in_pcm_frames(voice.spk, &cur_frame);
                    double progress = (double)cur_frame / (double)s->cached_total_frames;
                    if (progress >= (double)s->trim_end) {
                        if (s->loop_track) {
                            ma_uint64 start = (ma_uint64)(s->trim_start * s->cached_total_frames);
                            ma_sound_seek_to_pcm_frame(voice.spk, start);
                            if (voice.vrt)
                                ma_sound_seek_to_pcm_frame(voice.vrt, start);
                        } else {
                            ma_sound_stop(voice.spk);
                            if (voice.vrt) ma_sound_stop(voice.vrt);
                        }
                    }
                }
            }
        }
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    ImGui::Begin("Jamboard", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ImGui::BeginTabBar("MainTabs")) {

        // ==========================================
        // TAB 1: SOUNDBOARD
        // ==========================================
        if (ImGui::BeginTabItem("Soundboard")) {
            float bottom_bar_height = 170.0f;
            float sidebar_left_width = 250.0f;
            float sidebar_right_width = 280.0f;
            float top_zone_height = ImGui::GetContentRegionAvail().y - bottom_bar_height - 8.0f;
            float grid_width = ImGui::GetContentRegionAvail().x - sidebar_left_width - sidebar_right_width - 16.0f;

            // ---------- LEFT SIDEBAR ----------
            ImGui::BeginChild("SidebarLeft", ImVec2(sidebar_left_width, top_zone_height), true);
            if (current_selected_sound) {
                auto& s = *current_selected_sound;

                ImGui::BeginChild("ArtworkContainer", ImVec2(230, 120), true,
                    ImGuiWindowFlags_NoScrollbar);
                if (s.thumb_tex_id != 0)
                    ImGui::Image((ImTextureID)(uintptr_t)s.thumb_tex_id, ImVec2(214, 104));
                else {
                    ImGui::SetCursorPos(ImVec2(75, 45));
                    ImGui::TextDisabled("[ No Artwork ]");
                }
                ImGui::EndChild();

                ImGui::Spacing();
                ImGui::TextWrapped("Track: %s", display_name(s.name).c_str());
                ImGui::Separator();

                // Volume
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("##Vol", &s.volume_amp, 0.0f, 3.0f, "Volume: %.2f")) {}
                if (ImGui::IsItemDeactivatedAfterEdit()) save_config_to_json();
                ResetBtn("##rv", &s.volume_amp, 1.0f);

                // Pan
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("##Pan", &s.fx.pan, -1.0f, 1.0f, "Pan: %.2f")) {}
                if (ImGui::IsItemDeactivatedAfterEdit()) save_config_to_json();
                CenterBtn("##cp", &s.fx.pan, 0.0f);

                // Duration info removed

                ImGui::Separator();

                // Play mode
                int mode = (int)s.play_mode;
                if (ImGui::RadioButton("Restart", &mode, 0)) { s.play_mode = PLAY_RESTART; save_config_to_json(); }
                ImGui::SameLine();
                if (ImGui::RadioButton("Pause", &mode, 1)) { s.play_mode = PLAY_PAUSE; save_config_to_json(); }
                ImGui::SameLine();
                if (ImGui::RadioButton("Stop", &mode, 2)) { s.play_mode = PLAY_STOP; save_config_to_json(); }

                bool ov = s.overlap_enabled;
                if (ImGui::Checkbox("Overlap (Multiplay)", &ov)) { s.overlap_enabled = ov; save_config_to_json(); }
                bool lp = s.loop_track;
                if (ImGui::Checkbox("Loop Track", &lp)) { s.loop_track = lp; save_config_to_json(); }
                bool mu = s.muted;
                if (ImGui::Checkbox("Muted", &mu)) { s.muted = mu; save_config_to_json(); }

                ImGui::Separator();

                // Trimmer
                bool tr = s.trim_enabled;
                if (ImGui::Checkbox("Enable Trimmer", &tr)) { s.trim_enabled = tr; save_config_to_json(); }
                if (s.trim_enabled) {
                    float tot_sec = 0;
                    if (s.vis_ready) {
                        ma_uint64 df;
                        if (ma_decoder_get_length_in_pcm_frames(&s.vis_decoder, &df) == MA_SUCCESS)
                            tot_sec = (float)df / (float)s.vis_decoder.outputSampleRate;
                    }

                    ImGui::Text("Start: %.0f%%", s.trim_start * 100.0f);
                    if (tot_sec > 0) { ImGui::SameLine(); ImGui::TextDisabled("(%s)", fmt_time(s.trim_start * tot_sec).c_str()); }
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderFloat("##TrimStart", &s.trim_start, 0.0f, s.trim_end, "")) {
                        if (s.vis_ready) {
                            ma_uint64 tot = s.vis_decoder.outputSampleRate * 2;
                            s.trim_start = (float)((ma_uint64)(s.trim_start * tot)) / (float)tot;
                        }
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) save_config_to_json();

                    ImGui::Text("End: %.0f%%", s.trim_end * 100.0f);
                    if (tot_sec > 0) { ImGui::SameLine(); ImGui::TextDisabled("(%s)", fmt_time(s.trim_end * tot_sec).c_str()); }
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderFloat("##TrimEnd", &s.trim_end, s.trim_start, 1.0f, "")) {
                        if (s.vis_ready) {
                            ma_uint64 tot = s.vis_decoder.outputSampleRate * 2;
                            s.trim_end = (float)((ma_uint64)(s.trim_end * tot)) / (float)tot;
                        }
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) save_config_to_json();
                }

                ImGui::Separator();

                // Hotkey
                ImGui::Text("Hotkey:");
                if (g_capturing_hotkey_sound == &s) {
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Press a key...");
                    if (ImGui::Button("Cancel")) g_capturing_hotkey_sound = nullptr;
                } else {
                    if (s.hotkey >= 0) {
                        const char* kn = glfwGetKeyName(s.hotkey, 0);
                        ImGui::Text("Key: %s", kn ? kn : "?");
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Clear")) {
                            s.hotkey = -1;
                            save_config_to_json();
                        }
                    } else {
                        if (ImGui::Button("Set Hotkey"))
                            g_capturing_hotkey_sound = &s;
                    }
                }

                if (!s.web_url.empty()) {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Web Preset");
                    ImGui::TextWrapped("%s", s.web_url.c_str());
                }

                ImGui::Separator();
                ImGui::Spacing();
                if (ImGui::Button("Delete This Sound", ImVec2(-1, 28))) {
                    delete_confirm_sound = current_selected_sound;
                    snprintf(delete_confirm_name, sizeof(delete_confirm_name), "%s", display_name(current_selected_sound->name).c_str());
                    open_delete_popup = true;
                }
            } else {
                ImGui::TextDisabled("Select a track\nfrom the grid.");
            }
            ImGui::EndChild();
            ImGui::SameLine();

            // ---------- MIDDLE ZONE: Launch Grid ----------
            ImGui::BeginChild("GridZone", ImVec2(grid_width, top_zone_height), true);

            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##Search", "Search sounds...", search_filter,
                sizeof(search_filter));

            // Collect subdirectories in current folder
            std::vector<std::string> subdirs;
            {
                std::string dir = "sounds";
                if (!current_folder.empty()) dir += "/" + current_folder;
                if (std::filesystem::exists(dir)) {
                    for (auto& entry : std::filesystem::directory_iterator(dir)) {
                        if (entry.is_directory()) {
                            subdirs.push_back(path_to_utf8(entry.path().filename()));
                        }
                    }
                    std::sort(subdirs.begin(), subdirs.end());
                }
            }

            bool has_back = !current_folder.empty();
            bool has_items = has_back || !subdirs.empty() || !sounds.empty();
            bool filtered_empty = has_items && search_filter[0] != '\0';

            // Show empty state if no items at all
            if (!has_items) {
                ImVec2 zone_size = ImGui::GetContentRegionAvail();
                const char* msg = "drag and drop to upload a sound :)";
                ImVec2 text_size = ImGui::CalcTextSize(msg);
                ImGui::SetCursorPos(ImVec2(
                    (zone_size.x - text_size.x) * 0.5f,
                    (zone_size.y - text_size.y) * 0.5f
                ));
                ImGui::TextDisabled("%s", msg);
            }
            // Show filtered empty
            else if (filtered_empty) {
                bool any_match = false;
                for (auto& s : sounds) {
                    std::string lower_name = s->name;
                    std::string lower_filter = search_filter;
                    for (auto& c : lower_name) c = (char)std::tolower(c);
                    for (auto& c : lower_filter) c = (char)std::tolower(c);
                    if (lower_name.find(lower_filter) != std::string::npos) { any_match = true; break; }
                }
                if (!any_match) {
                    ImVec2 zone_size = ImGui::GetContentRegionAvail();
                    const char* msg = "No matching sounds found.";
                    ImVec2 text_size = ImGui::CalcTextSize(msg);
                    ImGui::SetCursorPos(ImVec2(
                        (zone_size.x - text_size.x) * 0.5f,
                        (zone_size.y - text_size.y) * 0.5f
                    ));
                    ImGui::TextDisabled("%s", msg);
                }
            }

            if (has_items) {
                int columns = 3;
                if (grid_width > 650) columns = 5;
                else if (grid_width > 500) columns = 4;
                else if (grid_width > 350) columns = 3;
                else columns = 2;

                if (ImGui::BeginTable("SoundGrid", columns)) {
                    // Back button if inside a folder
                    if (has_back) {
                        ImGui::TableNextColumn();
                        ImGui::PushID(-999);
                        float btn_h = 34.0f;
                        float icon_size = 16.0f;
                        ImVec2 btn_pos = ImGui::GetCursorScreenPos();
                        float btn_w = ImGui::GetContentRegionAvail().x;

                        if (ImGui::InvisibleButton("##back", ImVec2(btn_w, btn_h))) {
                            std::string parent = std::filesystem::path("sounds/" + current_folder).parent_path().string();
                            std::string sounds_root = "sounds";
                            if (parent == sounds_root || parent == ".")
                                current_folder = "";
                            else {
                                // Strip "sounds/" prefix
                                current_folder = parent.substr(sounds_root.length() + 1);
                            }
                            needs_sound_reload = true;
                            current_selected_sound = nullptr;
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted("Go back");
                            ImGui::EndTooltip();
                        }
                        // Drop target — move sound to parent folder
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SOUND_DRAG")) {
                                int sound_idx = *(const int*)payload->Data;
                                if (sound_idx >= 0 && sound_idx < (int)sounds.size()) {
                                    Sound* drag_sound = sounds[sound_idx].get();
                                    std::string parent = std::filesystem::path("sounds/" + current_folder).parent_path().string();
                                    std::string sounds_root = "sounds";
                                    std::string dest_folder = "";
                                    if (parent != sounds_root && parent != ".") {
                                        dest_folder = parent.substr(sounds_root.length() + 1);
                                    }
                                    cut_sound_to_folder(drag_sound, dest_folder);
                                    needs_sound_reload = true;
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        draw_folder_icon(dl, ImVec2(btn_pos.x + 4, btn_pos.y + (btn_h - icon_size) * 0.5f), icon_size);
                        dl->AddText(ImVec2(btn_pos.x + icon_size + 8, btn_pos.y + (btn_h - ImGui::GetTextLineHeight()) * 0.5f),
                            ImGui::GetColorU32(ImGuiCol_Text), "..");
                        ImGui::PopID();
                    }

                    // Folder entries
                    for (auto& folder_name : subdirs) {
                        if (search_filter[0]) {
                            std::string lower = folder_name;
                            std::string lf = search_filter;
                            for (auto& c : lower) c = (char)std::tolower(c);
                            for (auto& c : lf) c = (char)std::tolower(c);
                            if (lower.find(lf) == std::string::npos) continue;
                        }

                        ImGui::TableNextColumn();
                        ImGui::PushID(folder_name.c_str());

                        float btn_h = 34.0f;
                        float icon_size = 16.0f;
                        ImVec2 btn_pos = ImGui::GetCursorScreenPos();
                        float btn_w = ImGui::GetContentRegionAvail().x;

                        bool is_folder_sel = false;
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.3f, 0.1f, 1));
                        if (ImGui::Button(folder_name.c_str(), ImVec2(-1, btn_h))) {
                            if (!current_folder.empty()) current_folder += "/";
                            current_folder += folder_name;
                            needs_sound_reload = true;
                            current_selected_sound = nullptr;
                            search_filter[0] = '\0';
                        }
                        ImGui::PopStyleColor();

                        // Draw folder icon overlay
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        draw_folder_icon(dl, ImVec2(btn_pos.x + 4, btn_pos.y + (btn_h - icon_size) * 0.5f), icon_size);

                        // Drop target
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SOUND_DRAG")) {
                                int sound_idx = *(const int*)payload->Data;
                                if (sound_idx >= 0 && sound_idx < (int)sounds.size()) {
                                    Sound* drag_sound = sounds[sound_idx].get();
                                    std::string dest_folder = current_folder;
                                    if (!dest_folder.empty()) dest_folder += "/";
                                    dest_folder += folder_name;
                                    cut_sound_to_folder(drag_sound, dest_folder);
                                    needs_sound_reload = true;
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        // Right-click on folder
                        if (ImGui::BeginPopupContextItem()) {
                            ImGui::Text("Folder: %s", folder_name.c_str());
                            ImGui::Separator();
                            if (ImGui::MenuItem("Open")) {
                                if (!current_folder.empty()) current_folder += "/";
                                current_folder += folder_name;
                                needs_sound_reload = true;
                                current_selected_sound = nullptr;
                                search_filter[0] = '\0';
                            }
                            if (ImGui::MenuItem("Rename")) {
                                strncpy(rename_folder_buf, folder_name.c_str(), sizeof(rename_folder_buf) - 1);
                                std::string fpath = "sounds/" + current_folder;
                                if (!fpath.empty() && fpath.back() != '/') fpath += "/";
                                fpath += folder_name;
                                renaming_folder_path = fpath;
                                open_rename_folder_popup = true;
                            }
                            if (ImGui::MenuItem("Delete")) {
                                std::string fpath = "sounds/" + current_folder;
                                if (!fpath.empty() && fpath.back() != '/') fpath += "/";
                                fpath += folder_name;
                                delete_confirm_folder_path = fpath;
                                open_delete_folder_popup = true;
                            }
                            ImGui::Separator();
                            if (ImGui::MenuItem("Copy")) {
                                clipboard_type = CLIP_FOLDER;
                                clipboard_source_name = folder_name;
                                clipboard_source_path = "sounds/" + current_folder;
                                if (!clipboard_source_path.empty() && clipboard_source_path.back() != '/')
                                    clipboard_source_path += "/";
                                clipboard_source_path += folder_name;
                                clipboard_is_cut = false;
                            }
                            if (ImGui::MenuItem("Cut")) {
                                clipboard_type = CLIP_FOLDER;
                                clipboard_source_name = folder_name;
                                clipboard_source_path = "sounds/" + current_folder;
                                if (!clipboard_source_path.empty() && clipboard_source_path.back() != '/')
                                    clipboard_source_path += "/";
                                clipboard_source_path += folder_name;
                                clipboard_is_cut = true;
                            }
                            ImGui::EndPopup();
                        }

                        ImGui::PopID();
                    }

                    // Sound entries
                    for (auto& s : sounds) {
                        bool skip = false;
                        if (search_filter[0]) {
                            std::string lower_name = s->name;
                            std::string lower_filter = search_filter;
                            for (auto& c : lower_name) c = (char)std::tolower(c);
                            for (auto& c : lower_filter) c = (char)std::tolower(c);
                            if (lower_name.find(lower_filter) == std::string::npos)
                                skip = true;
                        }

                        ImGui::TableNextColumn();
                        if (skip) continue;

                        // --- Sound button ---
                        ImGui::PushID(s.get());

                        std::string display = display_name(s->name);
                        std::string hotkey_str;
                        if (s->hotkey >= 0) {
                            const char* kn = glfwGetKeyName(s->hotkey, 0);
                            if (kn) { hotkey_str = " ["; hotkey_str += kn; hotkey_str += "]"; }
                        }

                        float btn_w = ImGui::GetContentRegionAvail().x;
                        float text_max = btn_w - ImGui::GetStyle().FramePadding.x * 2.0f;
                        float ellipsis_w = ImGui::CalcTextSize("...").x;
                        float suffix_w = ImGui::CalcTextSize(hotkey_str.c_str()).x;
                        float name_max = text_max - suffix_w;

                        if (name_max > 0 && ImGui::CalcTextSize(display.c_str()).x + ellipsis_w > name_max) {
                            while (!display.empty() && ImGui::CalcTextSize(display.c_str()).x + ellipsis_w > name_max)
                                display.pop_back();
                            display += "...";
                        }
                        std::string label = display + hotkey_str;

                        bool is_playing = !s->active_voices.empty();
                        bool is_sel = (current_selected_sound == s.get());
                        if (is_playing)
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.4f, 0.3f, 1));
                        else if (is_sel)
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1));

                        if (ImGui::Button(label.c_str(), ImVec2(-1, 34))) {
                            if (!ImGui::GetIO().KeyCtrl) {
                                current_selected_sound = s.get();
                                if (s->play_mode == PLAY_RESTART) play_sound(*s);
                                else if (s->play_mode == PLAY_PAUSE) {
                                    if (!s->active_voices.empty()) toggle_pause_sound(*s);
                                    else play_sound(*s);
                                }
                                else if (s->play_mode == PLAY_STOP) {
                                    if (is_sound_playing(*s)) stop_sound(*s);
                                    else play_sound(*s);
                                }
                            } else {
                                current_selected_sound = s.get();
                            }
                        }

                        if (is_playing || is_sel)
                            ImGui::PopStyleColor();

                        // Drag source
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                            int sound_idx = -1;
                            for (int i = 0; i < (int)sounds.size(); i++) {
                                if (sounds[i].get() == s.get()) { sound_idx = i; break; }
                            }
                            ImGui::SetDragDropPayload("SOUND_DRAG", &sound_idx, sizeof(int));
                            ImGui::Text("Move %s", display_name(s->name).c_str());
                            ImGui::EndDragDropSource();
                        }

                        // --- Right-click context menu ---
                        if (ImGui::BeginPopupContextItem()) {
                            current_selected_sound = s.get();
                            ImGui::Text("Sound: %s", display_name(s->name).c_str());
                            ImGui::Separator();
                            if (ImGui::MenuItem("Properties")) {
                                properties_sound = s.get();
                                open_properties_popup = true;
                            }
                            if (ImGui::MenuItem("Rename")) {
                                renaming_sound = s.get();
                                strncpy(rename_buf, s->name.c_str(), sizeof(rename_buf) - 1);
                                open_rename_popup = true;
                            }
                            if (ImGui::MenuItem("Play")) { play_sound(*s); }
                            if (ImGui::MenuItem("Stop")) { stop_sound(*s); }
                            if (ImGui::MenuItem("Set Artwork...")) {
                                char filename[MAX_PATH] = {};
                                OPENFILENAMEA ofn = {};
                                ofn.lStructSize = sizeof(ofn);
                                ofn.hwndOwner = NULL;
                                ofn.lpstrFilter = "Image Files\0*.jpg;*.jpeg;*.png;*.bmp;*.tga;*.ppm\0All Files\0*.*\0";
                                ofn.lpstrFile = filename;
                                ofn.nMaxFile = MAX_PATH;
                                ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
                                if (GetOpenFileNameA(&ofn)) {
                                    std::string ext = std::filesystem::path(filename).extension().string();
                                    std::string dest = "sounds/";
                                    if (!s->folder.empty()) dest += s->folder + "/";
                                    dest += s->name + ext;
                                    std::filesystem::copy_file(filename, dest, std::filesystem::copy_options::overwrite_existing);
                                    if (s->thumb_tex_id != 0) {
                                        glDeleteTextures(1, &s->thumb_tex_id);
                                        s->thumb_tex_id = 0;
                                    }
                                    s->thumb_tex_id = load_texture_from_file(dest.c_str());
                                }
                            }
                            if (s->thumb_tex_id != 0 && ImGui::MenuItem("Remove Artwork")) {
                                glDeleteTextures(1, &s->thumb_tex_id);
                                s->thumb_tex_id = 0;
                                std::string art_base = "sounds/";
                                if (!s->folder.empty()) art_base += s->folder + "/";
                                art_base += s->name;
                                for (auto ext : { ".ppm", ".jpg", ".jpeg", ".png", ".bmp", ".tga" }) {
                                    std::error_code ec;
                                    std::filesystem::remove(art_base + ext, ec);
                                }
                            }
                            ImGui::Separator();
                            bool m = s->muted;
                            if (ImGui::MenuItem("Mute", nullptr, &m)) {
                                s->muted = m; save_config_to_json();
                            }
                            bool fx = s->fx_enabled;
                            if (ImGui::MenuItem("FX Enabled", nullptr, &fx)) {
                                s->fx_enabled = fx;
                                save_config_to_json();
                            }
                            if (ImGui::MenuItem("Clear Hotkey")) {
                                s->hotkey = -1; save_config_to_json();
                            }
                            ImGui::Separator();
                            if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                                clipboard_type = CLIP_SOUND;
                                clipboard_source_path = s->path;
                                clipboard_source_name = s->name;
                                clipboard_is_cut = false;
                            }
                            if (ImGui::MenuItem("Cut", "Ctrl+X")) {
                                clipboard_type = CLIP_SOUND;
                                clipboard_source_path = s->path;
                                clipboard_source_name = s->name;
                                clipboard_is_cut = true;
                            }
                            ImGui::Separator();
                            if (ImGui::MenuItem("Delete Sound", "Del")) {
                                delete_confirm_sound = s.get();
                                snprintf(delete_confirm_name, sizeof(delete_confirm_name), "%s", display_name(s->name).c_str());
                                open_delete_popup = true;
                            }
                            ImGui::EndPopup();
                        }

                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }

            // Right-click on grid background (empty area)
            if (ImGui::BeginPopupContextWindow("GridContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverExistingPopup)) {
                if (ImGui::MenuItem("New Folder...")) {
                    new_folder_buf[0] = '\0';
                    open_new_folder_popup = true;
                }
                if (clipboard_type != CLIP_NONE && ImGui::MenuItem("Paste", "Ctrl+V")) {
                    paste_clipboard();
                    needs_sound_reload = true;
                }
                ImGui::EndPopup();
            }

            ImGui::EndChild();
            ImGui::SameLine();

            // ---------- RIGHT SIDEBAR: Live Monitor ----------
            ImGui::BeginChild("SidebarRight", ImVec2(sidebar_right_width, top_zone_height), true);
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Live Channel Monitor");
            ImGui::Separator();

            int active_count = 0;
            for (auto& s : sounds) {
                for (auto& voice : s->active_voices) {
                    active_count++;
                    ImGui::PushID(active_count);

                    float v_cur = 0.0f, v_tot = 0.0f;
                    if (voice.spk) {
                        if (voice.paused)
                            v_cur = voice.paused_cursor;
                        else {
                            auto el = std::chrono::steady_clock::now() - voice.play_start;
                            v_cur = std::chrono::duration<float>(el).count() * (s->fx_enabled ? s->fx.playback_speed : 1.0f);
                        }
                        if (s->cached_total_frames == 0) {
                            ma_uint64 pf = 0;
                            if (ma_sound_get_length_in_pcm_frames(voice.spk, &pf) == MA_SUCCESS && pf > 0)
                                s->cached_total_frames = pf;
                        }
                        if (s->cached_total_frames > 0)
                            v_tot = (float)s->cached_total_frames / (float)ma_engine_get_sample_rate(&engine_speakers);
                    }

                    // Clickable name — selects sound without playing
                    bool is_sel = (current_selected_sound == s.get());
                    if (is_sel) {
                        ImGui::PushStyleColor(ImGuiCol_Button,
                            ImVec4(0.2f, 0.5f, 0.8f, 1));
                    }
                    float name_max_w = sidebar_right_width - 65.0f - 55.0f - ImGui::GetStyle().FramePadding.x * 6.0f;
                    std::string display = display_name(s->name);
                    float text_w = ImGui::CalcTextSize(display.c_str()).x;
                    if (text_w > name_max_w && name_max_w > 0) {
                        float ellipsis_w = ImGui::CalcTextSize("...").x;
                        while (!display.empty() && ImGui::CalcTextSize(display.c_str()).x + ellipsis_w > name_max_w)
                            display.pop_back();
                        display += "...";
                    }
                    if (ImGui::SmallButton(display.c_str())) {
                        current_selected_sound = s.get();
                    }
                    if (is_sel) ImGui::PopStyleColor();

                    ImGui::SameLine();
                    if (v_tot > 0.0f)
                        ImGui::ProgressBar(v_cur / v_tot, ImVec2(-65, 14), "");
                    else
                        ImGui::ProgressBar(0.0f, ImVec2(-65, 14), "Streaming");
                    ImGui::SameLine();

                    if (ImGui::Button("KILL", ImVec2(55, 16))) {
                        if (voice.spk) {
                            ma_sound_stop(voice.spk);
                            ma_sound_uninit(voice.spk);
                            delete voice.spk;
                            voice.spk = nullptr;
                        }
                        if (voice.vrt) {
                            ma_sound_stop(voice.vrt);
                            ma_sound_uninit(voice.vrt);
                            delete voice.vrt;
                            voice.vrt = nullptr;
                        }
                    }
                    ImGui::PopID();
                }
            }
            if (active_count == 0)
                ImGui::TextDisabled("\n   No tracks running.\n   Engine Idle.");
            ImGui::EndChild();

            // ---------- BOTTOM BAR ----------
            ImGui::BeginChild("BottomZone", ImVec2(0, bottom_bar_height), true);
            if (ImGui::Button("STOP ALL", ImVec2(100, 28)))
                for (auto& s : sounds) stop_sound(*s);
            ImGui::SameLine();
            if (ImGui::Checkbox("Stop others on play", &stop_all_on_new_play))
                save_config_to_json();
            ImGui::SameLine();
            ImGui::Spacing(); ImGui::SameLine();
            bool is_deafened = deafen;
            if (is_deafened)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1));
            if (ImGui::Button(deafen ? "DEAFENED" : "DEAFEN", ImVec2(120, 28))) {
                set_deafen(!deafen);
                save_config_to_json();
            }
            if (is_deafened)
                ImGui::PopStyleColor();

            float cur_sec = 0.0f, tot_sec = 0.0f, current_progress = 0.0f;
            ma_sound* tv = (current_selected_sound &&
                !current_selected_sound->active_voices.empty())
                ? current_selected_sound->active_voices.back().spk : nullptr;

            if (tv && current_selected_sound) {
                auto& voice = current_selected_sound->active_voices.back();
                if (voice.paused)
                    cur_sec = voice.paused_cursor;
                else {
                    auto elapsed = std::chrono::steady_clock::now() - voice.play_start;
                    cur_sec = std::chrono::duration<float>(elapsed).count()
                        * (current_selected_sound->fx_enabled ? current_selected_sound->fx.playback_speed : 1.0f);
                }
                if (current_selected_sound->cached_total_frames == 0) {
                    ma_uint64 tf = 0;
                    if (ma_sound_get_length_in_pcm_frames(tv, &tf) == MA_SUCCESS && tf > 0)
                        current_selected_sound->cached_total_frames = tf;
                }
                if (current_selected_sound->cached_total_frames > 0) {
                    float sr = (float)ma_engine_get_sample_rate(&engine_speakers);
                    tot_sec = (float)current_selected_sound->cached_total_frames / sr;
                } else if (current_selected_sound->vis_ready) {
                    ma_uint64 df;
                    if (ma_decoder_get_length_in_pcm_frames(
                            &current_selected_sound->vis_decoder, &df) == MA_SUCCESS && df > 0)
                        tot_sec = (float)df / (float)current_selected_sound->vis_decoder.outputSampleRate;
                }
                if (tot_sec > 0.0f) current_progress = cur_sec / tot_sec;
            }

            if (!is_user_dragging_slider && seek_cooldown_frames == 0)
                user_drag_progress = current_progress;
            if (seek_cooldown_frames > 0) seek_cooldown_frames--;

            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##MasterSeek", &user_drag_progress, 0.0f, 1.0f,
                "Selected Track Timeline");

            if (ImGui::IsItemActive()) {
                is_user_dragging_slider = true;
                if (scrub_enabled && tv && current_selected_sound) {
                    ma_uint64 total_frames = current_selected_sound->cached_total_frames;
                    if (total_frames == 0) {
                        if (ma_sound_get_length_in_pcm_frames(tv, &total_frames) == MA_SUCCESS)
                            current_selected_sound->cached_total_frames = total_frames;
                    }
                    if (total_frames > 0) {
                        ma_uint64 target = (ma_uint64)(user_drag_progress * total_frames);
                        ma_sound_seek_to_pcm_frame(
                            current_selected_sound->active_voices.back().spk, target);
                        if (current_selected_sound->active_voices.back().vrt)
                            ma_sound_seek_to_pcm_frame(
                                current_selected_sound->active_voices.back().vrt, target);
                        float sr = (float)ma_engine_get_sample_rate(&engine_speakers);
                        float spd = current_selected_sound->fx_enabled ? current_selected_sound->fx.playback_speed : 1.0f;
                        current_selected_sound->active_voices.back().play_start =
                            std::chrono::steady_clock::now()
                            - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                std::chrono::duration<float>((float)target / sr / spd));
                    }
                }
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && tv && current_selected_sound) {
                ma_uint64 total_frames = current_selected_sound->cached_total_frames;
                if (total_frames == 0) {
                    if (ma_sound_get_length_in_pcm_frames(tv, &total_frames) == MA_SUCCESS)
                        current_selected_sound->cached_total_frames = total_frames;
                }
                if (total_frames > 0) {
                    ma_uint64 target = (ma_uint64)(user_drag_progress * total_frames);
                    ma_sound_seek_to_pcm_frame(
                        current_selected_sound->active_voices.back().spk, target);
                    if (current_selected_sound->active_voices.back().vrt)
                        ma_sound_seek_to_pcm_frame(
                            current_selected_sound->active_voices.back().vrt, target);
                    float sr = (float)ma_engine_get_sample_rate(&engine_speakers);
                    float spd = current_selected_sound->fx_enabled ? current_selected_sound->fx.playback_speed : 1.0f;
                    current_selected_sound->active_voices.back().play_start =
                        std::chrono::steady_clock::now()
                        - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                            std::chrono::duration<float>((float)target / sr / spd));
                }
                seek_cooldown_frames = 5;
                is_user_dragging_slider = false;
            }

            ImGui::Spacing();
            ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
            ImVec2 canvas_size = ImGui::GetContentRegionAvail();
            canvas_size.y = 75.0f;

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(canvas_pos,
                ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                IM_COL32(12, 12, 18, 255));

            float mid_y = canvas_pos.y + (canvas_size.y / 2.0f);

            // Real-time PCM visualizer — sliding window over cached buffer
            {
                int num_bars = (int)(canvas_size.x / 2.8f);
                if (num_bars < 12) num_bars = 12;
                if (num_bars > 180) num_bars = 180;

                // Big 3-second cache so we barely ever seek
                static std::vector<float> cache;
                static ma_uint64 cache_start = 0;
                static ma_uint64 cache_frames = 0;
                static Sound* cache_sound = nullptr;
                static int seek_cooldown = 0;

                float amps[180] = {};
                bool live = false;

                // Visualizer throttle — configurable FPS or auto mode
                static double last_vis_time = 0.0;
                double now = ImGui::GetTime();
                double vis_interval;
                if (vis_fps_mode == 0) {
                    // Auto: 30fps when audio active, 10fps when idle
                    vis_interval = engine_has_active_audio ? (1.0 / 30.0) : (1.0 / 10.0);
                } else if (vis_fps <= 0) {
                    vis_interval = 0.0; // uncapped
                } else {
                    vis_interval = 1.0 / (double)vis_fps;
                }
                bool vis_update = (now - last_vis_time) >= vis_interval;
                if (vis_update) last_vis_time = now;

                if (engine_has_active_audio && current_selected_sound &&
                    current_selected_sound->vis_ready && tot_sec > 0.0f)
                {
                    ma_decoder* dec = &current_selected_sound->vis_decoder;
                    ma_uint64 total_frames;
                    if (ma_decoder_get_length_in_pcm_frames(dec, &total_frames) == MA_SUCCESS && total_frames > 0) {
                        ma_uint64 cur_frame = (ma_uint64)(current_progress * total_frames);
                        if (cur_frame >= total_frames) cur_frame = total_frames - 1;
                        ma_uint32 ch = dec->outputChannels;
                        if (ch < 1) ch = 2;

                        if (vis_update) {
                            // Refill when outside safe range
                            if (seek_cooldown > 0) seek_cooldown--;
                            bool refill = (cache_sound != current_selected_sound) ||
                                          cache_frames == 0 ||
                                          cur_frame < cache_start ||
                                          cur_frame + ch * 256 > cache_start + cache_frames;

                            if (refill && seek_cooldown == 0) {
                                seek_cooldown = 15;
                                ma_uint64 cache_len = 144000 / ch; // ~3s @ 48kHz
                                if (cache_len > 48000) cache_len = 48000;

                                ma_uint64 seek_to = cur_frame;
                                if (seek_to + cache_len > total_frames)
                                    seek_to = (total_frames > cache_len) ? total_frames - cache_len : 0;

                                if (ma_decoder_seek_to_pcm_frame(dec, seek_to) == MA_SUCCESS) {
                                    cache.resize(cache_len * ch);
                                    ma_uint64 frames_read = 0;
                                    ma_decoder_read_pcm_frames(dec, cache.data(), cache_len, &frames_read);
                                    cache_frames = frames_read;
                                    cache_start = seek_to;
                                    cache_sound = current_selected_sound;
                                }
                            }

                            // Read sliding window from cache
                            if (cache_frames > 0 && cache_sound == current_selected_sound) {
                                int off = (int)((cur_frame - cache_start) * ch);
                                if (off < 0) off = 0;
                                int total = (int)(cache_frames * ch);
                                int avail = total - off;
                                if (avail > num_bars) {
                                    int spb = avail / num_bars;
                                    if (spb < 1) spb = 1;

                                    for (int b = 0; b < num_bars; b++) {
                                        float sum = 0;
                                        int cnt = 0;
                                        for (int j = 0; j < spb; j++) {
                                            int idx = off + b * spb + j;
                                            if (idx >= 0 && idx < total) {
                                                sum += std::fabs(cache[idx]);
                                                cnt++;
                                            }
                                        }
                                        amps[b] = cnt > 0 ? (sum / cnt) : 0.0f;
                                    }

                                    // Adaptive normalization: scale so the loudest bar fills ~90%
                                    float peak = 0.001f;
                                    for (int b = 0; b < num_bars; b++)
                                        if (amps[b] > peak) peak = amps[b];
                                    float gain = (peak > 0.001f) ? (0.9f / peak) : 4.0f;
                                    if (gain > 8.0f) gain = 8.0f;
                                    for (int b = 0; b < num_bars; b++) {
                                        amps[b] *= gain;
                                        if (amps[b] > 1.0f) amps[b] = 1.0f;
                                    }

                                    live = true;
                                }
                            }
                        } else {
                            live = true;
                        }
                    }
                }

                // Frequency-spread bars + fast reaction
                static float smooth[180];
                static float peak_hold = 0.0f;
                float time = (float)ImGui::GetTime();

                for (int b = 0; b < num_bars; b++) {
                    float tgt;
                    if (live && amps[b] > 0.0f) {
                        // Emphasize transients: square the amp for more dynamic range
                        tgt = amps[b] * amps[b];
                        // LPF: lower bars get a boost (bass emphasis)
                        tgt *= 1.0f + 0.5f * (1.0f - b / (float)num_bars);
                        if (tgt > 1.0f) tgt = 1.0f;
                    } else {
                        tgt = 0.02f + 0.015f * std::sin(time * 2.0f + b * 0.3f);
                    }

                    // Fast attack (0.8), slower decay (0.2) for bouncing effect
                    float rate = (tgt > smooth[b]) ? 0.8f : 0.18f;
                    if (!live) rate = 0.08f;
                    smooth[b] += (tgt - smooth[b]) * rate;
                    float amp = smooth[b];
                    if (amp > 1.0f) amp = 1.0f;

                    float h = amp * (canvas_size.y * 0.5f - 3);
                    if (h < 0.5f) h = 0.5f;

                    float bar_w = canvas_size.x / (float)num_bars;
                    float x = canvas_pos.x + b * bar_w;

                    ImU32 col = live
                        ? IM_COL32(30 + (int)(60 * amp), 120 + (int)(80 * amp), 235,
                                   180 + (int)(75 * amp))
                        : IM_COL32(45, 50, 60, 80);

                    dl->AddLine(ImVec2(x + 1, mid_y - h), ImVec2(x + 1, mid_y + h), col,
                        bar_w > 2 ? bar_w - 1 : 1.5f);
                }
            }
            ImGui::Dummy(canvas_size);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // ==========================================
        // TAB 2: FX PROCESSING RACK
        // ==========================================
        if (ImGui::BeginTabItem("FX Processing Rack")) {
            ImGui::BeginChild("TargetList", ImVec2(200, 0), true);
            ImGui::Text("Target List");
            ImGui::Separator();
            for (auto& s : sounds) {
                bool sel = (current_selected_sound == s.get());
                if (ImGui::Selectable(display_name(s->name).c_str(), sel))
                    current_selected_sound = s.get();

                if (!s->fx_enabled) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("[FX OFF]");
                    continue;
                }

                // Show fx indicator dots
                int count = 0;
                if (s->fx.custom_reverb) count++;
                if (s->fx.echo_enabled) count++;
                if (s->fx.distortion_enabled) count++;
                if (s->fx.saturation_enabled) count++;
                if (s->fx.flanger_enabled) count++;
                if (s->fx.chorus_enabled) count++;
                if (s->fx.phaser_enabled) count++;
                if (s->fx.tremolo_enabled) count++;
                if (s->fx.ringmod_enabled) count++;
                if (s->fx.autowah_enabled) count++;
                if (s->fx.bitcrusher) count++;
                if (s->fx.bass_boost) count++;
                if (s->fx.eq_enabled) count++;
                if (s->fx.lowpass_enabled) count++;
                if (s->fx.highpass_enabled) count++;
                if (s->fx.bpf_enabled) count++;
                if (s->fx.notch_enabled) count++;
                if (s->fx.loshelf_enabled) count++;
                if (s->fx.hishelf_enabled) count++;
                if (s->fx.compressor_enabled) count++;
                if (s->fx.noise_gate_enabled) count++;
                if (s->fx.limiter_enabled) count++;
                if (s->fx.stereo_widener_enabled) count++;
                if (count > 0) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.4f, 1, 0.4f, 1), "(%d)", count);
                }
            }
            ImGui::EndChild();
            ImGui::SameLine();

            ImGui::BeginChild("FXModules", ImVec2(0, 0), true);
            if (current_selected_sound) {
                auto& s = *current_selected_sound;
                auto& fx = s.fx;
                auto save = []{ save_config_to_json(); };

                ImGui::Text("Selected: %s", display_name(s.name).c_str());
                bool fx_en = s.fx_enabled;
                if (ImGui::Checkbox("FX Enabled", &fx_en)) {
                    s.fx_enabled = fx_en;
                    save_config_to_json();
                }
                ImGui::Separator();

                // --- Volume & Pan (always available) ---
                if (FxHeader("Volume && Pan")) {
                    FxSlider("Volume", &s.volume_amp, 0.0f, 3.0f);
                    ResetBtn("##fv", &s.volume_amp, 1.0f);

                    FxSlider("Pan", &fx.pan, -1.0f, 1.0f);
                    CenterBtn("##pan", &fx.pan, 0.0f);
                }

                if (!s.fx_enabled) {
                    ImGui::TextDisabled("FX is disabled for this sound.");
                } else {

                // --- Playback Speed ---
                if (FxHeader("Playback Speed")) {
                    FxSlider("Speed", &fx.playback_speed, 0.1f, 4.0f, "%.2fx");
                    ResetBtn("##spd", &fx.playback_speed, 1.0f);
                }

                // --- Pitch (Semitones) ---
                if (FxHeader("Pitch Shift")) {
                    FxSlider("Semitones", &fx.pitch_semitones, -12.0f, 12.0f, "%.1f st");
                    CenterBtn("##pst", &fx.pitch_semitones, 0.0f);
                    float ratio = std::pow(2.0f, fx.pitch_semitones / 12.0f);
                    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1, 1), "  Ratio: %.4fx", ratio);
                }

                // --- Equalizer ---
                if (FxHeader("Equalizer (3-Band)")) {
                    FxCheck("Enable EQ", &fx.eq_enabled);
                    if (fx.eq_enabled) {
                        ImGui::Indent();
                        FxSlider("Low Gain (dB)", &fx.eq_low_gain, -12.0f, 12.0f);
                        FxSlider("Mid Gain (dB)", &fx.eq_mid_gain, -12.0f, 12.0f);
                        FxSlider("High Gain (dB)", &fx.eq_high_gain, -12.0f, 12.0f);
                        FxSlider("Mid Frequency", &fx.eq_mid_freq, 200.0f, 8000.0f, "%.0f Hz");
                        ImGui::Unindent();
                    }
                }

                // --- Filters ---
                if (FxHeader("Filters")) {
                    FxCheck("Low-Pass Filter", &fx.lowpass_enabled);
                    if (fx.lowpass_enabled) {
                        ImGui::Indent();
                        FxSlider("LPF Cutoff", &fx.lowpass_cutoff, 20.0f, 20000.0f, "%.0f Hz");
                        ImGui::Unindent();
                    }
                    FxCheck("High-Pass Filter", &fx.highpass_enabled);
                    if (fx.highpass_enabled) {
                        ImGui::Indent();
                        FxSlider("HPF Cutoff", &fx.highpass_cutoff, 20.0f, 20000.0f, "%.0f Hz");
                        ImGui::Unindent();
                    }
                    FxCheck("Band-Pass Filter", &fx.bpf_enabled);
                    if (fx.bpf_enabled) {
                        ImGui::Indent();
                        FxSlider("BPF Center", &fx.bpf_cutoff, 20.0f, 20000.0f, "%.0f Hz");
                        ImGui::Unindent();
                    }
                    FxCheck("Notch Filter", &fx.notch_enabled);
                    if (fx.notch_enabled) {
                        ImGui::Indent();
                        FxSlider("Notch Freq", &fx.notch_freq, 20.0f, 20000.0f, "%.0f Hz");
                        FxSlider("Notch Q", &fx.notch_q, 0.1f, 20.0f);
                        ImGui::Unindent();
                    }
                    FxCheck("Low Shelf", &fx.loshelf_enabled);
                    if (fx.loshelf_enabled) {
                        ImGui::Indent();
                        FxSlider("Shelf Gain (dB)", &fx.loshelf_gain, -12.0f, 12.0f);
                        FxSlider("Shelf Freq", &fx.loshelf_freq, 20.0f, 20000.0f, "%.0f Hz");
                        FxSlider("Shelf Slope", &fx.loshelf_slope, 0.5f, 4.0f);
                        ImGui::Unindent();
                    }
                    FxCheck("High Shelf", &fx.hishelf_enabled);
                    if (fx.hishelf_enabled) {
                        ImGui::Indent();
                        FxSlider("Shelf Gain (dB)", &fx.hishelf_gain, -12.0f, 12.0f);
                        FxSlider("Shelf Freq", &fx.hishelf_freq, 20.0f, 20000.0f, "%.0f Hz");
                        FxSlider("Shelf Slope", &fx.hishelf_slope, 0.5f, 4.0f);
                        ImGui::Unindent();
                    }
                }

                // --- Reverb ---
                if (FxHeader("Reverb")) {
                    FxCheck("Enable Reverb", &fx.custom_reverb);
                    if (fx.custom_reverb) {
                        ImGui::Indent();
                        FxSlider("Room Size", &fx.reverb_room_size, 0.0f, 1.0f);
                        FxSlider("Damping", &fx.reverb_damping, 0.0f, 1.0f);
                        FxSlider("Wet Mix", &fx.reverb_wet, 0.0f, 1.0f);
                        ImGui::Unindent();
                    }
                }

                // --- Delay / Echo ---
                if (FxHeader("Delay / Echo")) {
                    FxCheck("Enable Delay", &fx.echo_enabled);
                    if (fx.echo_enabled) {
                        ImGui::Indent();
                        FxSlider("Delay Time", &fx.echo_delay, 0.01f, 2.0f, "%.2f s");
                        FxSlider("Feedback", &fx.echo_feedback, 0.0f, 0.99f);
                        FxSlider("Mix", &fx.echo_mix, 0.0f, 1.0f);
                        ImGui::Unindent();
                    }
                }

                // --- Distortion & Saturation ---
                if (FxHeader("Distortion && Saturation")) {
                    FxCheck("Enable Distortion", &fx.distortion_enabled);
                    if (fx.distortion_enabled) {
                        ImGui::Indent();
                        FxSlider("Drive", &fx.distortion_drive, 0.0f, 10.0f);
                        ResetBtn("##dd", &fx.distortion_drive, 1.0f);
                        FxSlider("Tone", &fx.distortion_tone, 0.0f, 1.0f);
                        FxSlider("Mix", &fx.distortion_mix, 0.0f, 1.0f);
                        ImGui::Unindent();
                    }

                    FxCheck("Enable Saturation", &fx.saturation_enabled);
                    if (fx.saturation_enabled) {
                        ImGui::Indent();
                        FxSlider("Drive", &fx.saturation_drive, 0.0f, 10.0f);
                        ResetBtn("##sd", &fx.saturation_drive, 1.0f);
                        FxSlider("Warmth", &fx.saturation_warmth, 0.0f, 1.0f);
                        FxSlider("Mix", &fx.saturation_mix, 0.0f, 1.0f);
                        ImGui::Unindent();
                    }
                }

                // --- Modulation ---
                if (FxHeader("Modulation")) {
                    FxCheck("Flanger", &fx.flanger_enabled);
                    if (fx.flanger_enabled) {
                        ImGui::Indent();
                        FxSlider("Rate", &fx.flanger_rate, 0.01f, 10.0f, "%.2f Hz");
                        FxSlider("Depth", &fx.flanger_depth, 0.0f, 1.0f);
                        ImGui::Unindent();
                    }

                    FxCheck("Chorus", &fx.chorus_enabled);
                    if (fx.chorus_enabled) {
                        ImGui::Indent();
                        FxSlider("Rate", &fx.chorus_rate, 0.01f, 10.0f, "%.2f Hz");
                        FxSlider("Depth", &fx.chorus_depth, 0.0f, 1.0f);
                        FxSlider("Mix", &fx.chorus_mix, 0.0f, 1.0f);
                        ImGui::Unindent();
                    }

                    FxCheck("Phaser", &fx.phaser_enabled);
                    if (fx.phaser_enabled) {
                        ImGui::Indent();
                        FxSlider("Rate", &fx.phaser_rate, 0.01f, 10.0f, "%.2f Hz");
                        FxSlider("Depth", &fx.phaser_depth, 0.0f, 1.0f);
                        FxSlider("Feedback", &fx.phaser_feedback, 0.0f, 0.99f);
                        ImGui::Unindent();
                    }

                    FxCheck("Tremolo", &fx.tremolo_enabled);
                    if (fx.tremolo_enabled) {
                        ImGui::Indent();
                        FxSlider("Rate", &fx.tremolo_rate, 0.1f, 20.0f, "%.1f Hz");
                        FxSlider("Depth", &fx.tremolo_depth, 0.0f, 1.0f);
                        ImGui::Unindent();
                    }

                    FxCheck("Ring Modulator", &fx.ringmod_enabled);
                    if (fx.ringmod_enabled) {
                        ImGui::Indent();
                        FxSlider("Frequency", &fx.ringmod_freq, 20.0f, 2000.0f, "%.0f Hz");
                        FxSlider("Mix", &fx.ringmod_mix, 0.0f, 1.0f);
                        ImGui::Unindent();
                    }

                    FxCheck("Auto-wah", &fx.autowah_enabled);
                    if (fx.autowah_enabled) {
                        ImGui::Indent();
                        FxSlider("Rate", &fx.autowah_rate, 0.1f, 10.0f, "%.1f Hz");
                        FxSlider("Depth", &fx.autowah_depth, 0.0f, 1.0f);
                        FxSlider("Resonance", &fx.autowah_resonance, 0.0f, 1.0f);
                        ImGui::Unindent();
                    }

                    FxCheck("Bit Crusher", &fx.bitcrusher);
                    if (fx.bitcrusher) {
                        ImGui::Indent();
                        ImGui::SetNextItemWidth(250);
                        int bits = fx.bitcrusher_bits;
                        if (ImGui::SliderInt("Bit Depth", &bits, 1, 16)) {
                            fx.bitcrusher_bits = bits;
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit()) save();
                        ResetBtn("##bt", (float*)&fx.bitcrusher_bits, 8.0f);
                        ImGui::SameLine(); ImGui::Text(" bits");
                        ImGui::Unindent();
                    }

                    FxCheck("Bass Boost", &fx.bass_boost);
                }

                // --- Stereo ---
                if (FxHeader("Stereo Processing")) {
                    FxCheck("Stereo Widener", &fx.stereo_widener_enabled);
                    if (fx.stereo_widener_enabled) {
                        ImGui::Indent();
                        FxSlider("Width", &fx.stereo_width, 0.0f, 2.0f);
                        ResetBtn("##sw", &fx.stereo_width, 1.0f);
                        ImGui::Unindent();
                    }
                }

                // --- Dynamics ---
                if (FxHeader("Dynamics")) {
                    FxCheck("Compressor", &fx.compressor_enabled);
                    if (fx.compressor_enabled) {
                        ImGui::Indent();
                        FxSlider("Threshold", &fx.compressor_threshold, -60.0f, 0.0f, "%.1f dB");
                        FxSlider("Ratio", &fx.compressor_ratio, 1.0f, 20.0f, "%.1f:1");
                        FxSlider("Attack", &fx.compressor_attack, 0.001f, 0.1f, "%.3f s");
                        FxSlider("Release", &fx.compressor_release, 0.01f, 1.0f, "%.2f s");
                        FxSlider("Makeup Gain", &fx.compressor_makeup, 0.0f, 4.0f);
                        ImGui::Unindent();
                    }

                    FxCheck("Noise Gate", &fx.noise_gate_enabled);
                    if (fx.noise_gate_enabled) {
                        ImGui::Indent();
                        FxSlider("Threshold", &fx.noise_gate_threshold, -80.0f, 0.0f, "%.1f dB");
                        FxSlider("Attack", &fx.noise_gate_attack, 0.0001f, 0.05f, "%.4f s");
                        FxSlider("Release", &fx.noise_gate_release, 0.001f, 0.5f, "%.3f s");
                        ImGui::Unindent();
                    }

                    FxCheck("Limiter", &fx.limiter_enabled);
                    if (fx.limiter_enabled) {
                        ImGui::Indent();
                        FxSlider("Threshold", &fx.limiter_threshold, -12.0f, 0.0f, "%.1f dB");
                        FxSlider("Release", &fx.limiter_release, 0.01f, 0.5f, "%.2f s");
                        ImGui::Unindent();
                    }
                }
                } // end fx_enabled else
            } else {
                ImGui::TextDisabled("Select a sound from the left list.");
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // ==========================================
        // TAB 3: MEDIA IMPORTER
        // ==========================================
        if (ImGui::BeginTabItem("Media Importer")) {
            static char path[512] = "";
            ImGui::InputText("Path or URL", path, 512);
            ImGui::Spacing();

            std::string input = path;
            bool is_url = input.find("http://") == 0 || input.find("https://") == 0;

            if (is_url) {
                if (!is_converting && ImGui::Button("Download & Import", ImVec2(200, 40))) {
                    is_converting = true;
                    download_error.clear();
                    std::thread([input]() {
                        bool success;
                        std::string error;
                        download_from_url(input, success, error);
                        if (!success) download_error = error;
                        is_converting = false;
                        needs_sound_reload = true;
                    }).detach();
                }
                if (is_converting) ImGui::TextColored(ImVec4(1, 1, 0, 1), "Downloading...");
                if (!download_error.empty())
                    ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", download_error.c_str());
            } else {
                if (!is_converting && ImGui::Button("Convert & Import", ImVec2(200, 40))) {
                    std::string p = path;
                    if (std::filesystem::exists(p)) {
                        is_converting = true;
                        std::thread([p]() {
                            auto b_u8 = std::filesystem::path(p).stem().u8string();
                            std::string b((const char*)b_u8.data(), b_u8.size());
                            ffmpeg_convert_audio(p, "sounds/" + b + ".wav");
                            ffmpeg_extract_thumbnail(p, "sounds/" + b + ".ppm");
                            is_converting = false;
                            needs_sound_reload = true;
                        }).detach();
                    }
                }
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Search MyInstants.com");

            static char mi_search[128] = "";
            static std::vector<MyInstant> mi_results;
            static bool mi_searching = false;
            static std::string mi_error;
            static int mi_downloading_idx = -1;

            ImGui::SetNextItemWidth(350);
            ImGui::InputTextWithHint("##mi_search", "Search sounds...", mi_search, 128);
            ImGui::SameLine();

            if (mi_searching) {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "Searching...");
            } else {
                bool do_search = ImGui::Button("Search", ImVec2(70, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter);
                if (do_search && strlen(mi_search) > 0) {
                    mi_searching = true;
                    mi_error.clear();
                    std::string q = mi_search;
                    std::thread([q]() {
                        mi_results = myinstants_search(q);
                        if (mi_results.empty()) mi_error = "No results found.";
                        mi_searching = false;
                    }).detach();
                }
            }
            ImGui::SameLine();
            if (!mi_results.empty() && ImGui::Button("Clear", ImVec2(50, 0))) {
                mi_results.clear();
                mi_error.clear();
            }

            if (!mi_error.empty())
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", mi_error.c_str());

            if (!mi_results.empty()) {
                ImGui::TextDisabled("%d results", (int)mi_results.size());
                ImGui::BeginChild("MyInstantsResults", ImVec2(0, 200), true);
                for (int i = 0; i < (int)mi_results.size(); i++) {
                    auto& item = mi_results[i];
                    bool is_dl = (mi_downloading_idx == i);

                    if (is_dl) {
                        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Importing...");
                    } else {
                        ImGui::PushID(i);
                        if (ImGui::SmallButton("Preview")) {
                            myinstants_preview(item);
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Import")) {
                            mi_downloading_idx = i;
                            std::thread([item, i]() {
                                std::string data_dir;
                                if (program_files_mode) {
                                    char* appdata = getenv("APPDATA");
                                    if (appdata) data_dir = std::string(appdata) + "\\JamBoard";
                                } else {
                                    char buf[MAX_PATH];
                                    GetFullPathNameA(".", MAX_PATH, buf, NULL);
                                    data_dir = buf;
                                }
                                std::string sounds_dir = data_dir + "\\sounds";
                                std::filesystem::create_directories(sounds_dir);
                                myinstants_download(item, sounds_dir);
                                mi_downloading_idx = -1;
                                needs_sound_reload = true;
                            }).detach();
                        }
                        ImGui::SameLine();
                        ImGui::TextWrapped("%s", item.name.c_str());
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "Tip: You can also drag & drop audio/video files onto the window.");
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "Paste a YouTube/URL link to download audio directly.");
            ImGui::EndTabItem();
        }

        // ==========================================
        // TAB 4: SETTINGS
        // ==========================================
        if (ImGui::BeginTabItem("Settings")) {
            ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f),
                "Changes below apply after clicking \"Apply Hardware Routing Changes\".");
            ImGui::Spacing();

            ImGui::SetNextItemWidth(400);
            if (ImGui::SliderFloat("Master Volume", &master_volume, 0.0f, 2.0f))
                set_master_volume(master_volume);
            if (ImGui::IsItemDeactivatedAfterEdit()) save_config_to_json();
            ImGui::Spacing();

            ImGui::SetNextItemWidth(400);
            if (ImGui::Combo("Primary Output (Your Headphones)",
                &selected_speaker_idx,
                [](void*, int idx) -> const char* {
                    static char buf[512];
                    if (idx == 0) return "Default Windows Playback Device";
                    int dev = idx - 1;
                    if (dev >= (int)playbackDeviceCount) return "?";
                    if (playbackDevices[dev].isDefault)
                        snprintf(buf, sizeof(buf), "%s [System Default]", playbackDevices[dev].name);
                    else
                        snprintf(buf, sizeof(buf), "%s", playbackDevices[dev].name);
                    return buf;
                }, nullptr, playbackDeviceCount + 1)) {}

            ImGui::SetNextItemWidth(400);
            if (ImGui::Combo("Secondary Output (Virtual Cable)",
                &selected_virtual_idx,
                [](void*, int idx) -> const char* {
                    static char buf[512];
                    if (idx == 0) return "[Disabled] Do Not Route to Virtual Input";
                    int dev = idx - 1;
                    if (dev >= (int)playbackDeviceCount) return "?";
                    if (playbackDevices[dev].isDefault)
                        snprintf(buf, sizeof(buf), "%s [System Default]", playbackDevices[dev].name);
                    else
                        snprintf(buf, sizeof(buf), "%s", playbackDevices[dev].name);
                    return buf;
                }, nullptr, playbackDeviceCount + 1)) {}

            ImGui::SetNextItemWidth(400);
            if (ImGui::Combo("Microphone Input",
                &selected_mic_idx,
                [](void*, int idx) -> const char* {
                    static char buf[512];
                    if (idx == 0) return "[Disabled] Do Not Inject Microphone";
                    int dev = idx - 1;
                    if (dev >= (int)captureDeviceCount) return "?";
                    if (captureDevices[dev].isDefault)
                        snprintf(buf, sizeof(buf), "%s [System Default]", captureDevices[dev].name);
                    else
                        snprintf(buf, sizeof(buf), "%s", captureDevices[dev].name);
                    return buf;
                }, nullptr, captureDeviceCount + 1)) {}

            bool mic_pt = mic_passthrough_enabled;
            if (ImGui::Checkbox("Enable Mic Passthrough", &mic_pt)) {
                mic_passthrough_enabled = mic_pt;
                setup_audio_routing();
                save_config_to_json();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(person's voice goes to virtual cable)");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            bool mic_m = mic_muted;
            if (ImGui::Checkbox("Mute Microphone (Passthrough)", &mic_m)) {
                mic_muted = mic_m;
                save_config_to_json();
            }
            bool virt_m = virtual_muted;
            if (ImGui::Checkbox("Mute Virtual Output", &virt_m)) {
                set_virtual_muted(virt_m);
                save_config_to_json();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            bool scr = scrub_enabled;
            if (ImGui::Checkbox("Real-time Scrubber (seek while dragging timeline)", &scr)) {
                scrub_enabled = scr;
                save_config_to_json();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Visualizer Performance");
            const char* fps_modes[] = { "Auto (30fps active / 10fps idle)", "Manual" };
            ImGui::SetNextItemWidth(400);
            if (ImGui::Combo("Visualizer FPS Mode", &vis_fps_mode, fps_modes, 2))
                save_config_to_json();

            if (vis_fps_mode == 1) {
                ImGui::SetNextItemWidth(400);
                const char* fps_labels[] = { "Uncapped", "15 FPS", "20 FPS", "24 FPS", "30 FPS", "60 FPS" };
                int fps_values[] = { 0, 15, 20, 24, 30, 60 };
                int current = 0;
                for (int i = 0; i < 6; i++) {
                    if (vis_fps == fps_values[i]) { current = i; break; }
                }
                if (ImGui::Combo("Visualizer FPS", &current, fps_labels, 6)) {
                    vis_fps = fps_values[current];
                    save_config_to_json();
                }
                ImGui::TextDisabled("Lower FPS = less CPU usage");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Check for Updates")) {
                check_for_updates();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Current: %s", JAMBOARD_VERSION);
            if (!g_update_check_status.empty()) {
                ImGui::SameLine();
                ImGui::Text("| %s", g_update_check_status.c_str());
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Apply Hardware Routing Changes", ImVec2(240, 45))) {
                setup_audio_routing();
                save_config_to_json();
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // Confirm Delete popup — open deferred (safe, outside any popup scope)
    if (open_delete_popup) {
        ImGui::OpenPopup("Confirm Delete");
        open_delete_popup = false;
    }
    if (delete_confirm_sound && ImGui::BeginPopupModal("Confirm Delete", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Are you sure you want to delete \"%s\"?", delete_confirm_name);
        ImGui::Spacing();
        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            pending_delete_sound = delete_confirm_sound;
            delete_confirm_sound = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            delete_confirm_sound = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Rename Sound popup — open deferred (safe, outside any popup scope)
    if (open_rename_popup) {
        ImGui::OpenPopup("Rename Sound");
        open_rename_popup = false;
    }
    if (renaming_sound && ImGui::BeginPopupModal("Rename Sound", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Enter new name for \"%s\":", display_name(renaming_sound->name).c_str());
        ImGui::Spacing();
        ImGui::SetNextItemWidth(300);
        bool confirm = ImGui::InputText("##rename", rename_buf, sizeof(rename_buf),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        bool ok = ImGui::Button("OK", ImVec2(80, 0));
        ImGui::SameLine();
        bool cancel = ImGui::Button("Cancel", ImVec2(80, 0));
        if (confirm || ok)
        {
            if (rename_buf[0] != '\0')
                rename_sound(renaming_sound, rename_buf);
            renaming_sound = nullptr;
            ImGui::CloseCurrentPopup();
        }
        if (cancel)
        {
            renaming_sound = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (open_properties_popup) {
        ImGui::OpenPopup("Properties");
        open_properties_popup = false;
    }
    if (properties_sound && ImGui::BeginPopupModal("Properties", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto& s = *properties_sound;
        ImGui::Text("Name: %s", display_name(s.name).c_str());
        ImGui::Text("Path: %s", s.path.c_str());

        if (s.vis_ready) {
            ma_uint64 df;
            if (ma_decoder_get_length_in_pcm_frames(&s.vis_decoder, &df) == MA_SUCCESS && df > 0)
                ImGui::Text("Duration: %s", fmt_time((float)df / (float)s.vis_decoder.outputSampleRate).c_str());
        }

        ImGui::Text("Play Mode: %s", s.play_mode == PLAY_RESTART ? "Restart" : s.play_mode == PLAY_PAUSE ? "Pause" : "Stop");
        ImGui::Text("Hotkey: %s", s.hotkey >= 0 ? glfwGetKeyName(s.hotkey, 0) : "None");
        ImGui::Text("Loop: %s", s.loop_track ? "Yes" : "No");
        ImGui::Text("Overlap: %s", s.overlap_enabled ? "Yes" : "No");
        ImGui::Text("Trim: %s", s.trim_enabled ? "Yes" : "No");
        ImGui::Text("FX: %s", s.fx_enabled ? "Enabled" : "Disabled");

        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            properties_sound = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (!open_properties_popup) {
        properties_sound = nullptr;
    }

    // New Folder popup
    if (open_new_folder_popup) {
        ImGui::OpenPopup("New Folder");
        open_new_folder_popup = false;
    }
    if (ImGui::BeginPopupModal("New Folder", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter folder name:");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(300);
        bool confirm = ImGui::InputText("##newfolder", new_folder_buf, sizeof(new_folder_buf),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        bool ok = ImGui::Button("Create", ImVec2(80, 0));
        ImGui::SameLine();
        bool cancel = ImGui::Button("Cancel", ImVec2(80, 0));
        if (confirm || ok) {
            if (new_folder_buf[0] != '\0')
                create_folder(new_folder_buf);
            ImGui::CloseCurrentPopup();
        }
        if (cancel) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Rename Folder popup
    if (open_rename_folder_popup) {
        ImGui::OpenPopup("Rename Folder");
        open_rename_folder_popup = false;
    }
    if (ImGui::BeginPopupModal("Rename Folder", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter new folder name:");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(300);
        bool confirm = ImGui::InputText("##renamefolder", rename_folder_buf, sizeof(rename_folder_buf),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        bool ok = ImGui::Button("OK", ImVec2(80, 0));
        ImGui::SameLine();
        bool cancel = ImGui::Button("Cancel", ImVec2(80, 0));
        if (confirm || ok) {
            if (rename_folder_buf[0] != '\0')
                rename_folder(renaming_folder_path, rename_folder_buf);
            ImGui::CloseCurrentPopup();
        }
        if (cancel) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Delete Folder confirmation popup
    if (open_delete_folder_popup) {
        ImGui::OpenPopup("Confirm Delete Folder");
        open_delete_folder_popup = false;
    }
    if (ImGui::BeginPopupModal("Confirm Delete Folder", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete folder and all its contents?");
        ImGui::TextDisabled("%s", delete_confirm_folder_path.c_str());
        ImGui::Spacing();
        bool yes = ImGui::Button("Delete", ImVec2(80, 0));
        ImGui::SameLine();
        bool no = ImGui::Button("Cancel", ImVec2(80, 0));
        if (yes) {
            delete_folder(delete_confirm_folder_path);
            needs_sound_reload = true;
            delete_confirm_folder_path.clear();
            ImGui::CloseCurrentPopup();
        }
        if (no) {
            delete_confirm_folder_path.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (g_update_available) {
        ImGui::OpenPopup("Update Available");
        g_update_available = false;
    }
    if (ImGui::BeginPopupModal("Update Available", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("A new version of JamBoard is available!");
        ImGui::Spacing();
        ImGui::Text("Current: %s", JAMBOARD_VERSION);
        ImGui::Text("Latest:  %s", g_latest_version.c_str());
        ImGui::Spacing();
        if (g_download_progress) {
            ImGui::TextDisabled("Downloading...");
        } else {
            if (ImGui::Button("Download & Install", ImVec2(160, 0))) {
                download_and_install();
            }
            ImGui::SameLine();
            if (ImGui::Button("Ignore", ImVec2(80, 0))) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
    return engine_has_active_audio;
}
