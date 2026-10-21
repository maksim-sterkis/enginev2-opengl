#pragma once
#include "glad/glad.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include "ecs.hpp" // InstanceData

struct Mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    int index_count = 0;
    float radius = 0.0f; // Bounding sphere radius around the mesh origin (for frustum culling)
};

struct AssetPool {
    std::vector<Mesh> meshes;

    // All textures live in one GL_TEXTURE_2D_ARRAY; a "texture id" is a layer index.
    // Every entity can therefore be drawn in one batch regardless of texture.
    GLuint texture_array = 0;
    int texture_size = 0;       // width == height of every layer (set by the first texture)
    int texture_layers = 0;     // layers in use
    int max_texture_layers = 64; // capacity allocated up front

    // De-duplication by path
    std::unordered_map<std::string, uint32_t> mesh_lookup;
    std::unordered_map<std::string, uint32_t> texture_lookup;
};

void assets_init(AssetPool* pool);
// We pass instance_vbo so the mesh VAO can configure the per-instance attributes (see InstanceData)
uint32_t assets_load_obj(AssetPool* pool, const std::string& path, GLuint instance_vbo);
// Returns the layer index inside the pool's texture array. Textures whose size differs from
// the first loaded one are resampled to fit.
uint32_t assets_load_texture(AssetPool* pool, const std::string& path);
