#pragma once
#include "glad/glad.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "assets.hpp"
#include "ecs.hpp"

enum class ViewportMode {
    DYNAMIC_FOV,
    FORCED_16_9
};

struct Camera {
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f);
    glm::vec3 front = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    float yaw = -90.0f;
    float pitch = 0.0f;
    float speed = 5.0f;
    float sensitivity = 0.1f;
};

// Per-frame CPU-side render statistics (filled in by draw_frame)
struct RenderStats {
    int total_entities = 0;
    int visible_entities = 0;
    float build_ms = 0.0f;   // cull + sort + matrix building
    float upload_ms = 0.0f;  // glBufferSubData
    float draw_ms = 0.0f;    // draw call submission (CPU side only; GPU runs async)
};

struct RenderState {
    GLuint shader_program;
    GLuint u_view_loc;
    GLuint u_proj_loc;
    
    GLuint instance_vbo = 0;
    int max_instances = 100000; // Room for 100k entities per batch
    
    int window_width = 800;
    int window_height = 600;
    ViewportMode viewport_mode = ViewportMode::DYNAMIC_FOV;
    
    Camera camera;

    bool enable_backface_culling = true;
    bool enable_frustum_culling = true;
    bool vsync_enabled = true; // swap interval 1 + frame pacing to the monitor refresh rate
    RenderStats stats;
};

void init_renderer(RenderState* state);
void draw_frame(RenderState* state, ECS* ecs, const AssetPool* pool);
