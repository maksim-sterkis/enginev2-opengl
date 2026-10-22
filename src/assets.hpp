#pragma once
#include "glad/glad.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <limits>
#include "ecs.hpp" // InstanceData

constexpr uint32_t INVALID_ASSET_ID = std::numeric_limits<uint32_t>::max();

struct Mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    int index_count = 0;
    float radius = 0.0f; // Bounding sphere radius around the mesh origin (for frustum culling)
    bool double_sided = false;
};

struct AssetPool {
    std::vector<Mesh> meshes;

    // All textures live in one GL_TEXTURE_2D_ARRAY; a "texture id" is a layer index.
    // Every entity can therefore be drawn in one batch regardless of texture.
    GLuint texture_array = 0;
    int texture_size = 0;       // width == height of every layer (set by the first texture)
    int texture_layers = 0;     // layers in use
    int max_texture_layers = 64; // capacity allocated up front
    uint32_t white_texture_layer = INVALID_ASSET_ID;
    bool texture_mipmaps_dirty = false;
    std::vector<glm::vec4> texture_uv_transforms; // xy = content scale, zw = content offset
    std::vector<uint8_t> texture_has_alpha;

    // De-duplication by path
    std::unordered_map<std::string, uint32_t> mesh_lookup;
    std::unordered_map<std::string, uint32_t> texture_lookup;
};

struct ObjSubmesh {
    uint32_t mesh_id = INVALID_ASSET_ID;
    uint32_t texture_id = INVALID_ASSET_ID;
};

struct ObjSceneAsset {
    std::vector<ObjSubmesh> submeshes;
    glm::vec3 bounds_min = glm::vec3(0.0f);
    glm::vec3 bounds_max = glm::vec3(0.0f);
};

void assets_init(AssetPool* pool);
// We pass instance_vbo so the mesh VAO can configure the per-instance attributes (see InstanceData)
uint32_t assets_load_obj(AssetPool* pool, const std::string& path, GLuint instance_vbo);
// Loads an OBJ as one mesh per material, with each part using its MTL diffuse map.
bool assets_load_obj_scene(AssetPool* pool, const std::string& path, GLuint instance_vbo,
                           ObjSceneAsset* out_scene, std::string* error = nullptr);
// Returns the layer index inside the pool's texture array. Textures are fit without
// changing aspect ratio; an optional MTL opacity map is combined into the diffuse alpha.
uint32_t assets_load_texture(AssetPool* pool, const std::string& path,
                             const std::string& alpha_path = {});
// Generate the shared texture array's mipmaps once after all scene textures are uploaded.
bool assets_finalize_texture_uploads(AssetPool* pool);
