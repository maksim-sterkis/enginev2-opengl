#include "ui.hpp"
#include "renderer.hpp"
#include "ecs.hpp"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

void ui_init(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // Keyboard navigation is intentionally OFF: the camera uses WASD/Space/Shift and Space
    // would otherwise activate whichever widget was last clicked.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking

    // Stock Dear ImGui look
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
}

void ui_shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void ui_new_frame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ui_render() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// Exit / pause menu. The main loop owns `exit_menu_open` (ESC toggles it).
static void draw_exit_menu(GLFWwindow* window, RenderState* state) {
    if (state->exit_menu_open && !ImGui::IsPopupOpen("Exit?")) {
        ImGui::OpenPopup("Exit?");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Exit?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    // Closed from the main loop (ESC pressed again)
    if (!state->exit_menu_open) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::Text("Are you sure you want to exit the engine?");
    ImGui::Separator();

    float button_width = 120.0f;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float total_width = (button_width * 2.0f) + spacing;
    float offset = (ImGui::GetWindowSize().x - total_width) * 0.5f;
    ImGui::SetCursorPosX(offset);

    if (ImGui::Button("Exit", ImVec2(button_width, 0))) {
        glfwSetWindowShouldClose(window, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(button_width, 0))) {
        state->exit_menu_open = false;
        state->request_mouse_lock = true;
    }
    ImGui::EndPopup();
}

void ui_draw_debug_window(float delta_time, GLFWwindow* window, RenderState* state, ECS* ecs) {
    ImGuiIO& io = ImGui::GetIO();

    // Camera speed via mouse wheel, but never while the cursor is over a UI window
    if (!io.WantCaptureMouse && !state->exit_menu_open && io.MouseWheel != 0.0f) {
        float steps = std::max(1.0f, std::round(std::fabs(io.MouseWheel)));
        float dir = io.MouseWheel > 0.0f ? 1.0f : -1.0f;
        state->camera.speed = std::clamp(state->camera.speed + 0.5f * steps * dir, 0.5f, 50.0f);
    }

    ImGui::Begin("Engine Metrics & Input");

    // Performance: 100ms Rolling Average FPS
    static float display_fps = 0.0f;
    static float accum_time = 0.0f;
    static int frame_count = 0;
    accum_time += delta_time;
    frame_count++;
    if (accum_time >= 0.1f) {
        display_fps = (float)frame_count / accum_time;
        accum_time = 0.0f;
        frame_count = 0;
    }

    ImGui::Text("FPS (100ms Avg): %.1f", display_fps);
    ImGui::Text("Frame Time: %.3f ms", display_fps > 0.0f ? (1000.0f / display_fps) : 0.0f);

    // Distribution tracking for lows (recomputed 4x per second, not every frame)
    const int MAX_FRAMES = 1000;
    static float frame_times_arr[MAX_FRAMES];
    static int frame_idx = 0;
    static int frame_count_tot = 0;
    frame_times_arr[frame_idx] = delta_time;
    frame_idx = (frame_idx + 1) % MAX_FRAMES;
    if (frame_count_tot < MAX_FRAMES) frame_count_tot++;

    static float low_1_fps = 0.0f, low_0_1_fps = 0.0f;
    static float lows_timer = 0.0f;
    lows_timer += delta_time;
    if (lows_timer >= 0.25f && frame_count_tot > 0) {
        lows_timer = 0.0f;
        static std::vector<float> sorted_times;
        sorted_times.assign(frame_times_arr, frame_times_arr + frame_count_tot);
        std::sort(sorted_times.begin(), sorted_times.end(), std::greater<float>());
        float t1 = sorted_times[(size_t)(frame_count_tot * 0.01f)];
        float t01 = sorted_times[(size_t)(frame_count_tot * 0.001f)];
        low_1_fps = t1 > 0.0f ? 1.0f / t1 : 0.0f;
        low_0_1_fps = t01 > 0.0f ? 1.0f / t01 : 0.0f;
    }
    ImGui::Text("1%% Low FPS: %.1f", low_1_fps);
    ImGui::Text("0.1%% Low FPS: %.1f", low_0_1_fps);

    ImGui::Separator();

    // Engine settings
    int fps_mode = state->vsync_enabled ? 1 : 0; // 0 = Unlimited, 1 = VSync
    if (ImGui::Combo("FPS Mode", &fps_mode, "Unlimited\0VSync\0")) {
        glfwSwapInterval(fps_mode);
        state->vsync_enabled = (fps_mode == 1);
    }

    int vmode = (int)state->viewport_mode;
    if (ImGui::Combo("Viewport Mode", &vmode, "Dynamic FOV\0Forced 16:9\0")) {
        state->viewport_mode = (ViewportMode)vmode;
    }

    ImGui::Checkbox("Back-face Culling", &state->enable_backface_culling);
    ImGui::Checkbox("Frustum Culling", &state->enable_frustum_culling);

    ImGui::Separator();

    // Render stats (CPU-side timings; GPU work is asynchronous)
    const RenderStats& rs = state->stats;
    ImGui::Text("Entities: %d visible / %d total", rs.visible_entities, rs.total_entities);
    ImGui::Text("CPU motion update: %.3f ms", state->motion_ms);
    ImGui::Text("CPU build (cull/sort/matrices): %.3f ms", rs.build_ms);
    ImGui::Text("CPU upload: %.3f ms", rs.upload_ms);
    ImGui::Text("CPU draw submit: %.3f ms", rs.draw_ms);

    ImGui::Separator();

    // Simulation
    if (ImGui::Button("Randomize Cube Velocities")) {
        ecs_randomize_velocities(ecs);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop Cubes")) {
        ecs_stop_velocities(ecs);
    }

    ImGui::Separator();

    // Camera
    ImGui::SliderFloat("Camera Speed", &state->camera.speed, 0.5f, 50.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);

    ImGui::Separator();

    // Mouse State
    ImGui::Text("Mouse X: %.1f, Y: %.1f", io.MousePos.x, io.MousePos.y);
    ImGui::Text("Left Click: %s", ImGui::IsMouseDown(ImGuiMouseButton_Left) ? "DOWN" : "UP");
    ImGui::Text("Right Click: %s", ImGui::IsMouseDown(ImGuiMouseButton_Right) ? "DOWN" : "UP");

    ImGui::Separator();

    // Keyboard State
    std::string keys_down = "Keys Down:";
    for (int i = ImGuiKey_NamedKey_BEGIN; i < ImGuiKey_NamedKey_END; i++) {
        if (ImGui::IsKeyDown((ImGuiKey)i)) {
            const char* key_name = ImGui::GetKeyName((ImGuiKey)i);
            if (key_name) {
                keys_down += " [";
                keys_down += key_name;
                keys_down += "]";
            }
        }
    }
    ImGui::TextWrapped("%s", keys_down.c_str());

    ImGui::End();

    draw_exit_menu(window, state);
}
