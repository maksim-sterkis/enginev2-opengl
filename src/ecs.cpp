#include "ecs.hpp"

void ecs_init(ECS* ecs) {
    // Pre-allocate some memory to avoid reallocation stutters
    int max_ents = 10000;
    ecs->active_entities.reserve(max_ents);
    ecs->positions.reserve(max_ents);
    ecs->rotations.reserve(max_ents);
    ecs->scales.reserve(max_ents);
    ecs->render_mesh_ids.reserve(max_ents);
    ecs->render_texture_ids.reserve(max_ents);
    ecs->instance_cache.reserve(max_ents);
    ecs->transform_dirty.reserve(max_ents);
    ecs->active_slot.reserve(max_ents);
    ecs->alive.reserve(max_ents);
}

uint32_t ecs_create_entity(ECS* ecs) {
    uint32_t id;
    if (!ecs->free_ids.empty()) {
        // Reuse a recycled slot: reset it to defaults
        id = ecs->free_ids.back();
        ecs->free_ids.pop_back();
        ecs->positions[id] = glm::vec3(0.0f);
        ecs->rotations[id] = glm::vec3(0.0f);
        ecs->scales[id] = glm::vec3(1.0f);
        ecs->render_mesh_ids[id] = 0;
        ecs->render_texture_ids[id] = 0;
        ecs->transform_dirty[id] = 1;
        ecs->alive[id] = 1;
        ecs->active_slot[id] = (uint32_t)ecs->active_entities.size();
        ecs->active_entities.push_back(id);
        return id;
    }

    id = ecs->next_entity_id++;
    ecs->active_slot.push_back((uint32_t)ecs->active_entities.size());
    ecs->active_entities.push_back(id);
    ecs->alive.push_back(1);
    
    // Default components
    ecs->positions.push_back(glm::vec3(0.0f));
    ecs->rotations.push_back(glm::vec3(0.0f));
    ecs->scales.push_back(glm::vec3(1.0f));
    
    // Default missing mesh/texture (assume 0 is valid or placeholder)
    ecs->render_mesh_ids.push_back(0);
    ecs->render_texture_ids.push_back(0);

    ecs->instance_cache.push_back(InstanceData{});
    ecs->transform_dirty.push_back(1);
    
    return id;
}

void ecs_destroy_entity(ECS* ecs, uint32_t id) {
    if (id >= ecs->alive.size() || !ecs->alive[id]) return;

    // Swap-remove from the dense active list, keeping it contiguous
    uint32_t slot = ecs->active_slot[id];
    uint32_t last_id = ecs->active_entities.back();
    ecs->active_entities[slot] = last_id;
    ecs->active_slot[last_id] = slot;
    ecs->active_entities.pop_back();

    ecs->alive[id] = 0;
    ecs->free_ids.push_back(id);
}
