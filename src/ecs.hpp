#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <cstdint>

// GPU-facing per-instance data (48 bytes instead of a 64-byte mat4).
// The vertex shader rebuilds the transform from these.
struct InstanceData {
    glm::vec4 pos_layer; // xyz = world position, w = texture array layer
    glm::vec4 rot;       // quaternion (x, y, z, w)
    glm::vec4 scale;     // xyz = scale, w unused (keeps 16-byte alignment)
};

struct ECS {
    std::vector<uint32_t> active_entities; // dense list of live entity ids
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> rotations; // euler angles in degrees
    std::vector<glm::vec3> scales;
    std::vector<uint32_t> render_mesh_ids;
    std::vector<uint32_t> render_texture_ids;

    // Cached GPU instance data. Only recomputed when transform_dirty[ent] is set.
    std::vector<InstanceData> instance_cache;
    std::vector<uint8_t> transform_dirty;

    // Entity pool bookkeeping
    std::vector<uint32_t> free_ids;     // recycled ids available for reuse
    std::vector<uint32_t> active_slot;  // id -> index in active_entities (for O(1) removal)
    std::vector<uint8_t> alive;         // id -> 1 if alive

    uint32_t next_entity_id = 0;
};

void ecs_init(ECS* ecs);
uint32_t ecs_create_entity(ECS* ecs);
// O(1) removal: the id goes back into a free list and is reused by the next create.
void ecs_destroy_entity(ECS* ecs, uint32_t id);
// Call after changing positions/rotations/scales/texture ids of an entity so
// its cached instance data is rebuilt on the next frame.
inline void ecs_mark_dirty(ECS* ecs, uint32_t id) { ecs->transform_dirty[id] = 1; }
