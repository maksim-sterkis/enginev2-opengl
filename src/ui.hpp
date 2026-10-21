#pragma once

struct GLFWwindow;
struct RenderState; // Forward declaration
struct ECS;

// Initialize the ImGui context and backends
void ui_init(GLFWwindow* window);

// Shutdown the ImGui context and backends
void ui_shutdown();

// Start a new ImGui frame
void ui_new_frame();

// Render the ImGui draw data
void ui_render();

// Draw our custom debug/metrics window
void ui_draw_debug_window(float delta_time, GLFWwindow* window, RenderState* state, ECS* ecs);
