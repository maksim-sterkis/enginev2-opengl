#include "glad/glad.h"
#include "renderer.hpp"
#include "ui.hpp"
#include "assets.hpp"
#include "ecs.hpp"
#include <GLFW/glfw3.h>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <thread>
#ifdef __APPLE__
#include <CoreVideo/CoreVideo.h>
#include <condition_variable>
#include <mutex>

// Hardware VBlank synchronizer using macOS CVDisplayLink.
// Fires on the display hardware's exact vertical blank interrupt (60 Hz, 120 Hz ProMotion,
// external displays, etc.) with zero busy-waiting or software clock drift.
struct MacVSync {
    CVDisplayLinkRef link = nullptr;
    std::mutex mtx;
    std::condition_variable cv;
    uint64_t vblank_counter = 0;

    static CVReturn callback(CVDisplayLinkRef, const CVTimeStamp*, const CVTimeStamp*,
                             CVOptionFlags, CVOptionFlags*, void* ctx) {
        auto* self = static_cast<MacVSync*>(ctx);
        {
            std::lock_guard<std::mutex> lock(self->mtx);
            self->vblank_counter++;
        }
        self->cv.notify_all();
        return kCVReturnSuccess;
    }

    void init() {
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wdeprecated-declarations"
        if (CVDisplayLinkCreateWithActiveCGDisplays(&link) == kCVReturnSuccess && link) {
            CVDisplayLinkSetOutputCallback(link, &MacVSync::callback, this);
            CVDisplayLinkStart(link);
        }
        #pragma clang diagnostic pop
    }

    void wait_for_vblank() {
        if (!link) return;
        std::unique_lock<std::mutex> lock(mtx);
        uint64_t start = vblank_counter;
        cv.wait_for(lock, std::chrono::milliseconds(50), [&] { return vblank_counter != start; });
    }

    void shutdown() {
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wdeprecated-declarations"
        if (link) {
            CVDisplayLinkStop(link);
            CVDisplayLinkRelease(link);
            link = nullptr;
        }
        #pragma clang diagnostic pop
    }
};
#endif

// Refresh rate (Hz) of the monitor that contains most of the window.
// Falls back to the primary monitor, then to 60.
static int get_window_refresh_rate(GLFWwindow* window) {
    int wx, wy, ww, wh;
    glfwGetWindowPos(window, &wx, &wy);
    glfwGetWindowSize(window, &ww, &wh);

    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    GLFWmonitor* best = nullptr;
    long best_overlap = -1;
    for (int i = 0; i < count; i++) {
        int mx, my;
        glfwGetMonitorPos(monitors[i], &mx, &my);
        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        if (!mode) continue;
        long ox = std::max(0, std::min(wx + ww, mx + mode->width) - std::max(wx, mx));
        long oy = std::max(0, std::min(wy + wh, my + mode->height) - std::max(wy, my));
        if (ox * oy > best_overlap) {
            best_overlap = ox * oy;
            best = monitors[i];
        }
    }
    if (!best) best = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = best ? glfwGetVideoMode(best) : nullptr;
    return (mode && mode->refreshRate > 0) ? mode->refreshRate : 60;
}

// ---------------------------------------------------------
// Main Entry Point
// ---------------------------------------------------------
int main() {
  // Initialize GLFW
  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW" << std::endl;
    return -1;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
  glfwWindowHint(GLFW_DEPTH_BITS, 32); // Request maximum depth precision for Reverse-Z

  GLFWwindow *window = glfwCreateWindow(800, 600, "EngineV2", NULL, NULL);
  if (!window) {
    std::cerr << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return -1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1); // Enable VSync by default to prevent thermal throttling
  std::cout << "Display refresh rate: " << get_window_refresh_rate(window) << " Hz" << std::endl;
#ifdef __APPLE__
  MacVSync mac_vsync;
  mac_vsync.init();
#endif

  // Initialize GLAD
  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cerr << "Failed to initialize GLAD" << std::endl;
    return -1;
  }

  // Initialize UI layer
  ui_init(window);

  // Initialize our flat data structure
  RenderState render_state;
  init_renderer(&render_state);

  AssetPool asset_pool;
  assets_init(&asset_pool);

  ECS ecs;
  ecs_init(&ecs);

  // Load Default Assets
  uint32_t cube_mesh = assets_load_obj(&asset_pool, "assets/cube.obj", render_state.instance_vbo);
  uint32_t crate_tex = assets_load_texture(&asset_pool, "assets/texture.jpg");

  // Spawn 10,000 entities for a stress test
  int grid_size = 100;
  float spacing = 10.0f; // Expanded spacing to demonstrate 100,000 unit draw distance
  for (int x = -grid_size / 2; x < grid_size / 2; x++) {
      for (int z = -grid_size / 2; z < grid_size / 2; z++) {
          uint32_t ent = ecs_create_entity(&ecs);
          ecs.render_mesh_ids[ent] = cube_mesh;
          ecs.render_texture_ids[ent] = crate_tex;
          
          // Add some cool sine-wave vertical variance
          float y_pos = (sin(x * 0.2f) + cos(z * 0.2f)) * 10.0f;
          ecs.positions[ent] = glm::vec3(x * spacing, y_pos - 15.0f, z * spacing); // Move down a bit so we spawn above them
      }
  }

  double last_time = glfwGetTime();

  bool mouse_locked = false;
  bool p_pressed = false;
  double last_x = 400, last_y = 300;
  bool first_mouse = true;

  if (glfwRawMouseMotionSupported()) {
      glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
  }

  // Main loop
  while (!glfwWindowShouldClose(window)) {
    double current_time = glfwGetTime();
    float delta_time = static_cast<float>(current_time - last_time);
    last_time = current_time;

    // Toggle Mouse Lock with P
    if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS) {
        if (!p_pressed) {
            mouse_locked = !mouse_locked;
            glfwSetInputMode(window, GLFW_CURSOR, mouse_locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            first_mouse = true;
            p_pressed = true;
        }
    } else {
        p_pressed = false;
    }

    // Unlock mouse with ESC
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        if (mouse_locked) {
            mouse_locked = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            first_mouse = true;
        }
    }

    // Camera Input
    if (mouse_locked) {
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);

        if (first_mouse) {
            last_x = xpos;
            last_y = ypos;
            first_mouse = false;
        }

        float xoffset = xpos - last_x;
        float yoffset = last_y - ypos; 
        last_x = xpos;
        last_y = ypos;

        xoffset *= render_state.camera.sensitivity;
        yoffset *= render_state.camera.sensitivity;

        render_state.camera.yaw += xoffset;
        render_state.camera.pitch += yoffset;

        if (render_state.camera.pitch > 89.0f) render_state.camera.pitch = 89.0f;
        if (render_state.camera.pitch < -89.0f) render_state.camera.pitch = -89.0f;

        glm::vec3 front;
        front.x = cos(glm::radians(render_state.camera.yaw)) * cos(glm::radians(render_state.camera.pitch));
        front.y = sin(glm::radians(render_state.camera.pitch));
        front.z = sin(glm::radians(render_state.camera.yaw)) * cos(glm::radians(render_state.camera.pitch));
        render_state.camera.front = glm::normalize(front);

        float velocity = render_state.camera.speed * delta_time;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            render_state.camera.position += render_state.camera.front * velocity;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            render_state.camera.position -= render_state.camera.front * velocity;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            render_state.camera.position -= glm::normalize(glm::cross(render_state.camera.front, render_state.camera.up)) * velocity;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            render_state.camera.position += glm::normalize(glm::cross(render_state.camera.front, render_state.camera.up)) * velocity;
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
            render_state.camera.position += render_state.camera.up * velocity;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
            render_state.camera.position -= render_state.camera.up * velocity;
    }

    // Start UI Frame
    ui_new_frame();

    glfwGetFramebufferSize(window, &render_state.window_width, &render_state.window_height);

    // Draw main frame
    draw_frame(&render_state, &ecs, &asset_pool);

    // Draw UI
    ui_draw_debug_window(delta_time, window, &render_state);
    ui_render();

    glfwSwapBuffers(window);
    glfwPollEvents();

    // Hardware VBlank synchronization on macOS via CVDisplayLink
    if (render_state.vsync_enabled) {
#ifdef __APPLE__
        mac_vsync.wait_for_vblank();
#endif
    }
  }

  // Shutdown UI
  ui_shutdown();
#ifdef __APPLE__
  mac_vsync.shutdown();
#endif

  glfwTerminate();
  return 0;
}
