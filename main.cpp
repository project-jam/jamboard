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

#include <iostream>
#include <vector>
#include <string>

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
    if (!init_audio_system()) return -1;

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
                        else if (s->play_mode == PLAY_PAUSE) toggle_pause_sound(*s);
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
