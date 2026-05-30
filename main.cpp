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
    io.Fonts->AddFontFromFileTTF(font_path.c_str(), 16.0f, &font_cfg, ranges);

    ImGui_ImplGlfw_InitForOpenGL(g_window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    init_ui_textures();

    const int DEAFEN_KEY = GLFW_KEY_F13;
    static bool prev_deafen_key = false;

    while (!glfwWindowShouldClose(g_window)) {
        glfwPollEvents();

        // Deafen toggle hotkey
        {
            bool held = glfwGetKey(g_window, DEAFEN_KEY) == GLFW_PRESS;
            if (held && !prev_deafen_key) {
                set_deafen(!deafen);
                save_config_to_json();
            }
            prev_deafen_key = held;
        }

        if (g_capturing_hotkey_sound) {
            for (int k = GLFW_KEY_SPACE; k < GLFW_KEY_LAST; k++) {
                if (glfwGetKey(g_window, k) == GLFW_PRESS) {
                    g_capturing_hotkey_sound->hotkey = k;
                    save_config_to_json();
                    g_capturing_hotkey_sound = nullptr;
                    break;
                }
            }
        } else {
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
                    }
                    prev_keys[s->hotkey] = held;
                }
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        draw_ui();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(g_window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(g_window);
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
