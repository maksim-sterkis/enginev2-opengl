#include "renderer.hpp"
#include "shader.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

void init_renderer(RenderState* state) {
    state->shader_program = create_shader_program("shaders/triangle.vert", "shaders/triangle.frag");

    glGenBuffers(1, &state->instance_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, state->instance_vbo);
    glBufferData(GL_ARRAY_BUFFER, state->max_instances * sizeof(InstanceData), nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Reverse-Z initialization
    glDepthRange(1.0, 0.0);
    glClearDepth(0.0);
    glDepthFunc(GL_GREATER);

    state->u_view_loc = glGetUniformLocation(state->shader_program, "u_view");
    state->u_proj_loc = glGetUniformLocation(state->shader_program, "u_proj");
}

void draw_frame(RenderState* state, ECS* ecs, const AssetPool* pool) {
    using Clock = std::chrono::steady_clock;
    auto ms_since = [](Clock::time_point t) {
        return std::chrono::duration<float, std::milli>(Clock::now() - t).count();
    };
    auto build_start = Clock::now();

    RenderStats& stats = state->stats;
    stats = RenderStats{};
    stats.total_entities = (int)ecs->active_entities.size();

    glEnable(GL_DEPTH_TEST);
    // Re-assert Reverse-Z in case ImGui or another state machine changes it
    glDepthRange(1.0, 0.0);
    glClearDepth(0.0);
    glDepthFunc(GL_GREATER);

    // Back-face culling (cube.obj uses counter-clockwise winding for front faces)
    if (state->enable_backface_culling) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
    } else {
        glDisable(GL_CULL_FACE);
    }

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (state->window_width <= 0 || state->window_height <= 0) {
        stats.build_ms = ms_since(build_start);
        return;
    }

    float aspect = 1.0f;
    if (state->viewport_mode == ViewportMode::DYNAMIC_FOV) {
        glViewport(0, 0, state->window_width, state->window_height);
        aspect = (float)state->window_width / (float)state->window_height;
    } else {
        int view_w = state->window_width;
        int view_h = state->window_width * 9 / 16;
        if (view_h > state->window_height) {
            view_h = state->window_height;
            view_w = state->window_height * 16 / 9;
        }
        int vp_x = (state->window_width - view_w) / 2;
        int vp_y = (state->window_height - view_h) / 2;
        glViewport(vp_x, vp_y, view_w, view_h);
        aspect = 16.0f / 9.0f;
    }

    // Set far plane to 100,000 for infinite horizon
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100000.0f);
    glm::mat4 view = glm::lookAt(state->camera.position, state->camera.position + state->camera.front, state->camera.up);

    glUseProgram(state->shader_program);
    glUniformMatrix4fv(state->u_view_loc, 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(state->u_proj_loc, 1, GL_FALSE, &projection[0][0]);

    // Extract the 6 frustum planes from the view-projection matrix (Gribb-Hartmann).
    // Each plane is (a,b,c,d) with a normalized normal pointing into the frustum.
    // Reverse-Z is done through glDepthRange, so clip space is the standard -w..w.
    glm::vec4 frustum[6];
    {
        glm::mat4 vp = projection * view;
        auto row = [&](int i) { return glm::vec4(vp[0][i], vp[1][i], vp[2][i], vp[3][i]); };
        frustum[0] = row(3) + row(0); // left
        frustum[1] = row(3) - row(0); // right
        frustum[2] = row(3) + row(1); // bottom
        frustum[3] = row(3) - row(1); // top
        frustum[4] = row(3) + row(2); // near
        frustum[5] = row(3) - row(2); // far
        for (auto& p : frustum) {
            p /= glm::length(glm::vec3(p));
        }
    }

    auto sphere_visible = [&](const glm::vec3& center, float radius) {
        for (const auto& p : frustum) {
            if (p.x * center.x + p.y * center.y + p.z * center.z + p.w < -radius) {
                return false;
            }
        }
        return true;
    };

    // DoD Instanced Batcher
    struct RenderData {
        uint32_t entity;
        uint32_t mesh;
    };
    
    // Persistent scratch buffers: avoids per-frame heap allocations
    static std::vector<RenderData> render_list;
    static std::vector<InstanceData> instances;
    render_list.clear();
    instances.clear();
    render_list.reserve(ecs->active_entities.size());
    instances.reserve(ecs->active_entities.size());

    // Frustum cull before sorting and before touching any instance data
    for (auto ent : ecs->active_entities) {
        uint32_t mesh = ecs->render_mesh_ids[ent];
        if (mesh >= pool->meshes.size() || ecs->render_texture_ids[ent] >= (uint32_t)pool->texture_layers) {
            continue;
        }
        if (state->enable_frustum_culling) {
            const glm::vec3& s = ecs->scales[ent];
            float max_scale = std::max(std::abs(s.x), std::max(std::abs(s.y), std::abs(s.z)));
            if (!sphere_visible(ecs->positions[ent], pool->meshes[mesh].radius * max_scale)) {
                continue;
            }
        }
        render_list.push_back({ent, mesh});
    }
    stats.visible_entities = (int)render_list.size();

    // Textures live in one array (per-instance layer), so only the mesh splits batches
    std::sort(render_list.begin(), render_list.end(), [](const RenderData& a, const RenderData& b){
        return a.mesh < b.mesh;
    });

    if (render_list.empty()) {
        stats.build_ms = ms_since(build_start);
        return;
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, pool->texture_array);

    uint32_t current_mesh = render_list[0].mesh;
    
    auto flush_batch = [&]() {
        if (instances.empty()) return;

        const bool cull_backfaces = state->enable_backface_culling &&
                                    !pool->meshes[current_mesh].double_sided;
        if (cull_backfaces) glEnable(GL_CULL_FACE);
        else glDisable(GL_CULL_FACE);
        
        auto upload_start = Clock::now();
        glBindBuffer(GL_ARRAY_BUFFER, state->instance_vbo);
        // Orphan the old storage so the driver doesn't stall waiting for the GPU
        // to finish reading last frame's instance data, then fill the fresh storage.
        const GLsizeiptr bytes = instances.size() * sizeof(InstanceData);
        glBufferData(GL_ARRAY_BUFFER, bytes, nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, instances.data());
        stats.upload_ms += ms_since(upload_start);

        auto draw_start = Clock::now();
        glBindVertexArray(pool->meshes[current_mesh].vao);
        glDrawElementsInstanced(GL_TRIANGLES, pool->meshes[current_mesh].index_count, GL_UNSIGNED_INT, 0, instances.size());
        stats.draw_ms += ms_since(draw_start);
        
        instances.clear();
    };

    for(const auto& item : render_list) {
        if (item.mesh != current_mesh) {
            flush_batch();
            current_mesh = item.mesh;
        }

        uint32_t ent = item.entity;
        // Only rebuild cached instance data for entities that changed
        if (ecs->transform_dirty[ent]) {
            const glm::vec3& r = ecs->rotations[ent];
            glm::quat q(1.0f, 0.0f, 0.0f, 0.0f);
            if (r.x != 0.0f || r.y != 0.0f || r.z != 0.0f) {
                // Same composition order as the previous Rx * Ry * Rz matrix chain
                q = glm::angleAxis(glm::radians(r.x), glm::vec3(1, 0, 0)) *
                    glm::angleAxis(glm::radians(r.y), glm::vec3(0, 1, 0)) *
                    glm::angleAxis(glm::radians(r.z), glm::vec3(0, 0, 1));
            }
            InstanceData& d = ecs->instance_cache[ent];
            d.pos_layer = glm::vec4(ecs->positions[ent], (float)ecs->render_texture_ids[ent]);
            d.rot = glm::vec4(q.x, q.y, q.z, q.w);
            d.scale = glm::vec4(ecs->scales[ent], 0.0f);
            const uint32_t texture_id = ecs->render_texture_ids[ent];
            d.uv_transform = texture_id < pool->texture_uv_transforms.size()
                ? pool->texture_uv_transforms[texture_id]
                : glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
            ecs->transform_dirty[ent] = 0;
        }
        instances.push_back(ecs->instance_cache[ent]);
    }
    
    // Flush the final batch
    flush_batch();

    // Build time = everything except upload and draw submission
    stats.build_ms = ms_since(build_start) - stats.upload_ms - stats.draw_ms;
}
