#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GL/gl.h>

#include "audio_engine.h"
#include "config.h"
#include "ui.h"
#include "import.h"
#include "update_checker.h"

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <cstdlib>

static bool prev_keys[512] = {};
GLFWwindow* g_window = nullptr;

void drop_callback(GLFWwindow* window, int count, const char** paths)
{
    std::vector<std::string> files;
    for (int i = 0; i < count; i++)
        files.push_back(paths[i]);
    handle_dropped_files(files);
}

int main() {
#ifdef _WIN32
    // If sounds/ isn't writable (e.g. installed in Program Files), use AppData
    auto writable = [](const std::string& dir) -> bool {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) return false;
        std::string test = dir + "\\__w_test";
        std::ofstream tf(test);
        if (!tf.is_open()) return false;
        tf.close();
        std::filesystem::remove(test, ec);
        return true;
    };
    if (!writable("sounds")) {
        char* appdata = getenv("APPDATA");
        if (appdata) {
            std::string dir = std::string(appdata) + "\\JamBoard";
            std::filesystem::create_directories(dir + "\\sounds");
            std::filesystem::current_path(dir);
            program_files_mode = true;
        }
    }
#endif

    if (!init_audio_system()) return -1;

    start_update_checker();

    load_sounds("sounds");
    load_config_from_json();
    if (!setup_audio_routing()) {
        std::cerr << "FATAL: No audio output engines initialized. Sound will not play." << std::endl;
    }
    apply_volumes();
    init_all_sound_fx_chains();

    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    g_window = glfwCreateWindow(900, 650, "JamBoard", NULL, NULL);
    if (!g_window) { glfwTerminate(); return -1; }

    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1);

    glfwSetDropCallback(g_window, drop_callback);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig font_cfg;
    font_cfg.OversampleH = 1;
    font_cfg.OversampleV = 1;
    font_cfg.PixelSnapH = true;
    char exe_buf[MAX_PATH];
    GetModuleFileNameA(NULL, exe_buf, MAX_PATH);
    std::string font_path = std::filesystem::path(exe_buf).parent_path().string() + "\\zpix.ttf";
    static const ImWchar ranges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement
        0x2000, 0x206F, // General Punctuation
        0x3000, 0x30FF, // CJK Symbols and Punctuation, Hiragana, Katakana
        0x31F0, 0x31FF, // Katakana Phonetic Extensions
        0xFF00, 0xFFEF, // Halfwidth and Fullwidth Forms
        0x4E00, 0x9FFF, // CJK Unified Ideographs
        0xF900, 0xFAFF, // CJK Compatibility Ideographs
        0xFE30, 0xFE4F, // CJK Compatibility Forms
        0x2E80, 0x2EFF, // CJK Radicals Supplement
        0x2F00, 0x2FDF, // Kangxi Radicals
        0x31C0, 0x31EF, // CJK Strokes
        0x2010, 0x2027, // General Punctuation (dashes, etc.)
        0x2030, 0x205E, // General Punctuation (per mille, etc.)
        0, 0
    };
    io.Fonts->AddFontFromFileTTF(font_path.c_str(), 12.0f, &font_cfg, ranges);

    // Load system fallback font for glyphs zpix doesn't cover (Cyrillic, Arabic, Thai, etc.)
    {
        const char* fallback_path = nullptr;
#ifdef _WIN32
        fallback_path = "C:\\Windows\\Fonts\\segoeui.ttf";
#elif __APPLE__
        fallback_path = "/System/Library/Fonts/Supplemental/Arial.ttf";
#else
        static const char* linux_fonts[] = {
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
        };
        for (auto p : linux_fonts) {
            if (std::filesystem::exists(p)) { fallback_path = p; break; }
        }
#endif
        if (fallback_path && std::filesystem::exists(fallback_path)) {
            ImFontConfig fallback_cfg;
            fallback_cfg.MergeMode = true;
            fallback_cfg.PixelSnapH = true;
            fallback_cfg.OversampleH = 1;
            fallback_cfg.OversampleV = 1;
            static const ImWchar fallback_ranges[] = {
                0x0020, 0x00FF,
                0x0100, 0x024F,
                0x0250, 0x02AF,
                0x0370, 0x03FF,
                0x0400, 0x052F,
                0x0530, 0x058F,
                0x0590, 0x05FF,
                0x0600, 0x06FF,
                0x0750, 0x077F,
                0x08A0, 0x08FF,
                0x0900, 0x097F,
                0x0980, 0x09FF,
                0x0A00, 0x0AFF,
                0x0B00, 0x0B7F,
                0x0B80, 0x0BFF,
                0x0C00, 0x0C7F,
                0x0C80, 0x0CFF,
                0x0D00, 0x0D7F,
                0x0E00, 0x0E7F,
                0x0E80, 0x0EFF,
                0x1000, 0x109F,
                0x10A0, 0x10FF,
                0x1100, 0x11FF,
                0x1200, 0x137F,
                0x13A0, 0x13FF,
                0x1400, 0x167F,
                0x1E00, 0x1EFF,
                0x2000, 0x206F,
                0x2070, 0x209F,
                0x20A0, 0x20CF,
                0x2100, 0x214F,
                0x2150, 0x218F,
                0x2190, 0x21FF,
                0x2200, 0x22FF,
                0x2300, 0x23FF,
                0x2400, 0x243F,
                0x2460, 0x24FF,
                0x2500, 0x257F,
                0x2580, 0x259F,
                0x25A0, 0x25FF,
                0x2600, 0x26FF,
                0x2700, 0x27BF,
                0x27C0, 0x27EF,
                0x27F0, 0x27FF,
                0x2900, 0x297F,
                0x2980, 0x29FF,
                0x2A00, 0x2AFF,
                0x2C00, 0x2C5F,
                0x2C60, 0x2C7F,
                0x2C80, 0x2CFF,
                0x2D00, 0x2D2F,
                0x2D30, 0x2D7F,
                0x2D80, 0x2DDF,
                0x2E00, 0x2E7F,
                0xA000, 0xA4CF,
                0xAC00, 0xD7AF,
                0x1D000, 0x1D2FF, // Byzantine Musical Symbols, Mathematical Alphanumeric Symbols
                0x1D300, 0x1D37F, // Tai Xuan Jing Symbols
                0x1F000, 0x1F02F, // Mahjong Tiles
                0x1F030, 0x1F09F, // Domino Tiles
                0x1F0A0, 0x1F0FF, // Playing Cards
                0x1F100, 0x1F1FF, // Enclosed Alphanumeric Supplement
                0x1F200, 0x1F2FF, // Enclosed Ideographic Supplement
                0x1F300, 0x1F5FF, // Miscellaneous Symbols and Pictographs
                0x1F600, 0x1F64F, // Emoticons
                0x1F680, 0x1F6FF, // Transport and Map Symbols
                0x1F700, 0x1F77F, // Alchemical Symbols
                0x1F780, 0x1F7FF, // Geometric Shapes Extended
                0x1F800, 0x1F8FF, // Supplemental Arrows-C
                0x1F900, 0x1F9FF, // Supplemental Symbols and Pictographs
                0x1FA00, 0x1FA6F, // Chess Symbols
                0x1FA70, 0x1FAFF, // Symbols and Pictographs Extended-A
                0, 0
            };
            io.Fonts->AddFontFromFileTTF(fallback_path, 12.0f, &fallback_cfg, fallback_ranges);
        }
    }

    ImGui_ImplGlfw_InitForOpenGL(g_window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    init_ui_textures();

    const int DEAFEN_KEY = GLFW_KEY_F13;
    static bool prev_deafen_key = false;
    int idle_frames = 0;
    bool has_any_hotkey = false;

    while (!glfwWindowShouldClose(g_window)) {
        // Idle detection: when no audio and no interaction, throttle to ~5fps
        bool is_idle = (idle_frames > 120);
        if (is_idle) {
            glfwWaitEventsTimeout(0.2);
        } else {
            glfwPollEvents();
        }

        // Deafen toggle hotkey
        {
            bool held = glfwGetKey(g_window, DEAFEN_KEY) == GLFW_PRESS;
            if (held && !prev_deafen_key) {
                set_deafen(!deafen);
                save_config_to_json();
                idle_frames = 0;
            }
            prev_deafen_key = held;
        }

        if (g_capturing_hotkey_sound) {
            for (int k = GLFW_KEY_SPACE; k < GLFW_KEY_LAST; k++) {
                if (glfwGetKey(g_window, k) == GLFW_PRESS) {
                    g_capturing_hotkey_sound->hotkey = k;
                    save_config_to_json();
                    g_capturing_hotkey_sound = nullptr;
                    has_any_hotkey = true;
                    break;
                }
            }
        } else {
            // Recompute hotkey presence when sounds change
            if (has_any_hotkey == false && !sounds.empty()) {
                for (auto& s : sounds) {
                    if (s->hotkey >= 0) { has_any_hotkey = true; break; }
                }
            }
            if (has_any_hotkey) {
                for (auto& s : sounds) {
                    if (s->hotkey >= 0 && s->hotkey < 512) {
                        bool held = glfwGetKey(g_window, s->hotkey) == GLFW_PRESS;
                        if (held && !prev_keys[s->hotkey]) {
                            if (s->play_mode == PLAY_RESTART) play_sound(*s);
                            else if (s->play_mode == PLAY_PAUSE) {
                                if (!s->active_voices.empty()) toggle_pause_sound(*s);
                                else play_sound(*s);
                            }
                            else if (s->play_mode == PLAY_STOP) {
                                if (is_sound_playing(*s)) stop_sound(*s); else play_sound(*s);
                            }
                            idle_frames = 0;
                        }
                        prev_keys[s->hotkey] = held;
                    }
                }
            }
        }

        // Ctrl+C / Ctrl+X / Ctrl+V clipboard shortcuts
        {
            ImGuiIO& io = ImGui::GetIO();
            bool ctrl = io.KeyCtrl;
            bool c = glfwGetKey(g_window, GLFW_KEY_C) == GLFW_PRESS;
            bool x = glfwGetKey(g_window, GLFW_KEY_X) == GLFW_PRESS;
            bool v = glfwGetKey(g_window, GLFW_KEY_V) == GLFW_PRESS;
            static bool prev_c = false, prev_x = false, prev_v = false;

            if (ctrl && !io.WantCaptureKeyboard) {
                if (c && !prev_c && current_selected_sound) {
                    clipboard_type = CLIP_SOUND;
                    clipboard_source_path = current_selected_sound->path;
                    clipboard_source_name = current_selected_sound->name;
                    clipboard_is_cut = false;
                    idle_frames = 0;
                }
                if (x && !prev_x && current_selected_sound) {
                    clipboard_type = CLIP_SOUND;
                    clipboard_source_path = current_selected_sound->path;
                    clipboard_source_name = current_selected_sound->name;
                    clipboard_is_cut = true;
                    idle_frames = 0;
                }
                if (v && !prev_v && clipboard_type != CLIP_NONE) {
                    paste_clipboard();
                    needs_sound_reload = true;
                    idle_frames = 0;
                }
            }
            prev_c = c; prev_x = x; prev_v = v;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        bool has_active_audio = draw_ui();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(g_window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(g_window);

        // Track idle state
        if (has_active_audio || io.WantCaptureMouse || io.WantCaptureKeyboard) {
            idle_frames = 0;
        } else {
            idle_frames++;
        }
    }

    shutdown_ui();
    for (auto& s : sounds) stop_sound(*s);
    destroy_all_sound_fx_chains();
    shutdown_audio_routing();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(g_window);
    glfwTerminate();

    return 0;
}
