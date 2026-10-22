#include "assets.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <map>
#include <limits>
#include <utility>
#include <vector>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

void assets_init(AssetPool* pool) {
    pool->meshes.reserve(128);
}

static uint32_t upload_mesh(AssetPool* pool, const std::vector<float>& vertices,
                            const std::vector<unsigned int>& indices, GLuint instance_vbo,
                            bool double_sided = false) {
    if (vertices.empty() || indices.empty()) return INVALID_ASSET_ID;

    Mesh mesh;
    mesh.index_count = static_cast<int>(indices.size());
    mesh.double_sided = double_sided;
    float max_radius_sq = 0.0f;
    for (size_t i = 0; i + 2 < vertices.size(); i += 5) {
        const float x = vertices[i];
        const float y = vertices[i + 1];
        const float z = vertices[i + 2];
        max_radius_sq = std::max(max_radius_sq, x * x + y * y + z * z);
    }
    mesh.radius = std::sqrt(max_radius_sq);

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);
    glBindVertexArray(mesh.vao);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Per-instance attributes (locations 2-5).
    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo);
    const GLsizei stride = sizeof(InstanceData);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(InstanceData, pos_layer));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(InstanceData, rot));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(InstanceData, scale));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(InstanceData, uv_transform));
    glVertexAttribDivisor(2, 1);
    glVertexAttribDivisor(3, 1);
    glVertexAttribDivisor(4, 1);
    glVertexAttribDivisor(5, 1);
    glBindVertexArray(0);

    const uint32_t id = static_cast<uint32_t>(pool->meshes.size());
    pool->meshes.push_back(mesh);
    return id;
}

uint32_t assets_load_obj(AssetPool* pool, const std::string& path, GLuint instance_vbo) {
    // De-duplicate: loading the same file twice returns the existing mesh
    auto cached = pool->mesh_lookup.find(path);
    if (cached != pool->mesh_lookup.end()) {
        return cached->second;
    }

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str())) {
        std::cerr << "Failed to load OBJ: " << warn << err << std::endl;
        return INVALID_ASSET_ID;
    }

    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            if (index.vertex_index < 0 ||
                static_cast<size_t>(index.vertex_index) * 3 + 2 >= attrib.vertices.size()) {
                std::cerr << "OBJ contains an invalid vertex index: " << path << std::endl;
                return INVALID_ASSET_ID;
            }
            float vx = attrib.vertices[3 * index.vertex_index + 0];
            float vy = attrib.vertices[3 * index.vertex_index + 1];
            float vz = attrib.vertices[3 * index.vertex_index + 2];
            vertices.push_back(vx);
            vertices.push_back(vy);
            vertices.push_back(vz);
            
            if (index.texcoord_index >= 0 &&
                static_cast<size_t>(index.texcoord_index) * 2 + 1 < attrib.texcoords.size()) {
                vertices.push_back(attrib.texcoords[2 * index.texcoord_index + 0]);
                vertices.push_back(attrib.texcoords[2 * index.texcoord_index + 1]);
            } else {
                vertices.push_back(0.0f);
                vertices.push_back(0.0f);
            }
            indices.push_back(indices.size());
        }
    }

    uint32_t id = upload_mesh(pool, vertices, indices, instance_vbo);
    if (id == INVALID_ASSET_ID) {
        std::cerr << "OBJ contains no renderable triangles: " << path << std::endl;
        return INVALID_ASSET_ID;
    }
    pool->mesh_lookup[path] = id;
    return id;
}

static uint32_t add_white_texture_layer(AssetPool* pool) {
    if (pool->white_texture_layer != INVALID_ASSET_ID) return pool->white_texture_layer;
    if (pool->texture_layers >= pool->max_texture_layers) return INVALID_ASSET_ID;

    if (pool->texture_array == 0) {
        pool->texture_size = 1;
        glGenTextures(1, &pool->texture_array);
        glBindTexture(GL_TEXTURE_2D_ARRAY, pool->texture_array);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 1, 1, pool->max_texture_layers,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
        glBindTexture(GL_TEXTURE_2D_ARRAY, pool->texture_array);
    }

    const size_t pixel_count = static_cast<size_t>(pool->texture_size) * pool->texture_size;
    std::vector<unsigned char> white(pixel_count * 4, 255);
    const uint32_t layer = static_cast<uint32_t>(pool->texture_layers++);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, pool->texture_size,
                    pool->texture_size, 1, GL_RGBA, GL_UNSIGNED_BYTE, white.data());
    pool->white_texture_layer = layer;
    pool->texture_uv_transforms.emplace_back(1.0f, 1.0f, 0.0f, 0.0f);
    pool->texture_has_alpha.push_back(0);
    pool->texture_mipmaps_dirty = true;
    return layer;
}

bool assets_load_obj_scene(AssetPool* pool, const std::string& path, GLuint instance_vbo,
                           ObjSceneAsset* out_scene, std::string* error) {
    if (!pool || !out_scene) {
        if (error) *error = "Invalid OBJ scene output or asset pool.";
        return false;
    }
    *out_scene = ObjSceneAsset{};

    const std::filesystem::path obj_path(path);
    const std::string material_dir = obj_path.parent_path().string();
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;
    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str(),
                          material_dir.c_str())) {
        if (error) *error = "Failed to load OBJ '" + path + "': " + warn + err;
        return false;
    }
    if (!warn.empty()) std::cerr << "OBJ warning for " << path << ": " << warn << std::endl;
    if (attrib.vertices.empty()) {
        if (error) *error = "OBJ has no vertex data: " + path;
        return false;
    }
    if (materials.empty()) {
        if (error) *error = "OBJ has no materials; expected its MTL file for the textured scene: " + path;
        return false;
    }

    glm::vec3 bounds_min(std::numeric_limits<float>::max());
    glm::vec3 bounds_max(std::numeric_limits<float>::lowest());
    for (size_t i = 0; i + 2 < attrib.vertices.size(); i += 3) {
        const glm::vec3 position(attrib.vertices[i], attrib.vertices[i + 1], attrib.vertices[i + 2]);
        bounds_min = glm::min(bounds_min, position);
        bounds_max = glm::max(bounds_max, position);
    }

    std::vector<uint32_t> material_textures(materials.size(), INVALID_ASSET_ID);
    bool needs_white_texture = false;
    for (size_t i = 0; i < materials.size(); ++i) {
        std::string diffuse_name = materials[i].diffuse_texname;
        if (diffuse_name.empty()) {
            needs_white_texture = true;
            continue;
        }
        std::replace(diffuse_name.begin(), diffuse_name.end(), '\\', '/');
        std::filesystem::path diffuse_path(diffuse_name);
        if (diffuse_path.is_relative()) diffuse_path = obj_path.parent_path() / diffuse_path;
        const std::string resolved_path = diffuse_path.lexically_normal().generic_string();
        std::string alpha_name = materials[i].alpha_texname;
        std::string resolved_alpha_path;
        if (!alpha_name.empty()) {
            std::replace(alpha_name.begin(), alpha_name.end(), '\\', '/');
            std::filesystem::path alpha_path(alpha_name);
            if (alpha_path.is_relative()) alpha_path = obj_path.parent_path() / alpha_path;
            resolved_alpha_path = alpha_path.lexically_normal().generic_string();
        }
        material_textures[i] = assets_load_texture(pool, resolved_path, resolved_alpha_path);
        if (material_textures[i] == INVALID_ASSET_ID) {
            if (error) *error = "Failed to load diffuse texture '" + resolved_path +
                                "' for OBJ material '" + materials[i].name + "'.";
            return false;
        }
    }

    struct MaterialGeometry {
        std::vector<float> vertices;
        std::vector<unsigned int> indices;
    };
    std::map<int, MaterialGeometry> geometry_by_material;
    for (const auto& shape : shapes) {
        size_t index_offset = 0;
        for (size_t face = 0; face < shape.mesh.num_face_vertices.size(); ++face) {
            const int material_id = face < shape.mesh.material_ids.size()
                ? shape.mesh.material_ids[face] : -1;
            if (material_id != -1 &&
                (material_id < 0 || static_cast<size_t>(material_id) >= materials.size())) {
                if (error) *error = "OBJ references an invalid material index: " + path;
                return false;
            }
            if (material_id == -1) needs_white_texture = true;

            const size_t face_vertex_count = shape.mesh.num_face_vertices[face];
            if (index_offset + face_vertex_count > shape.mesh.indices.size()) {
                if (error) *error = "OBJ has malformed face indices: " + path;
                return false;
            }
            MaterialGeometry& geometry = geometry_by_material[material_id];
            for (size_t corner = 0; corner < face_vertex_count; ++corner) {
                const tinyobj::index_t& index = shape.mesh.indices[index_offset + corner];
                if (index.vertex_index < 0 ||
                    static_cast<size_t>(index.vertex_index) * 3 + 2 >= attrib.vertices.size()) {
                    if (error) *error = "OBJ has an invalid vertex index: " + path;
                    return false;
                }
                const size_t vertex_offset = static_cast<size_t>(index.vertex_index) * 3;
                geometry.vertices.push_back(attrib.vertices[vertex_offset]);
                geometry.vertices.push_back(attrib.vertices[vertex_offset + 1]);
                geometry.vertices.push_back(attrib.vertices[vertex_offset + 2]);
                if (index.texcoord_index >= 0 &&
                    static_cast<size_t>(index.texcoord_index) * 2 + 1 < attrib.texcoords.size()) {
                    const size_t texcoord_offset = static_cast<size_t>(index.texcoord_index) * 2;
                    geometry.vertices.push_back(attrib.texcoords[texcoord_offset]);
                    geometry.vertices.push_back(attrib.texcoords[texcoord_offset + 1]);
                } else {
                    geometry.vertices.push_back(0.0f);
                    geometry.vertices.push_back(0.0f);
                }
                geometry.indices.push_back(static_cast<unsigned int>(geometry.indices.size()));
            }
            index_offset += face_vertex_count;
        }
    }

    if (geometry_by_material.empty()) {
        if (error) *error = "OBJ contains no renderable faces: " + path;
        return false;
    }
    if (needs_white_texture) {
        const uint32_t white_layer = add_white_texture_layer(pool);
        if (white_layer == INVALID_ASSET_ID) {
            if (error) *error = "Texture array is full; cannot add the untextured material fallback.";
            return false;
        }
        for (uint32_t& texture_id : material_textures) {
            if (texture_id == INVALID_ASSET_ID) texture_id = white_layer;
        }
    }

    ObjSceneAsset loaded_scene;
    loaded_scene.bounds_min = bounds_min;
    loaded_scene.bounds_max = bounds_max;
    for (const auto& [material_id, geometry] : geometry_by_material) {
        const uint32_t texture_id = material_id < 0
            ? pool->white_texture_layer : material_textures[static_cast<size_t>(material_id)];
        if (texture_id == INVALID_ASSET_ID) {
            if (error) *error = "OBJ material has no valid diffuse texture: " + path;
            return false;
        }
        const bool double_sided = texture_id < pool->texture_has_alpha.size() &&
                                  pool->texture_has_alpha[texture_id] != 0;
        const uint32_t mesh_id = upload_mesh(pool, geometry.vertices, geometry.indices,
                                             instance_vbo, double_sided);
        if (mesh_id == INVALID_ASSET_ID) {
            if (error) *error = "Failed to upload an OBJ material submesh: " + path;
            return false;
        }
        loaded_scene.submeshes.push_back({mesh_id, texture_id});
    }
    *out_scene = std::move(loaded_scene);
    return true;
}

// Fit into the square array layer without distorting the image. The padding
// extends edge pixels so filtering and mipmaps do not pull in a black border.
static std::vector<unsigned char> resample_rgba_aspect_fit(const unsigned char* src, int sw, int sh,
                                                           int size, glm::vec4* uv_transform) {
    const float fit = std::min(static_cast<float>(size) / sw, static_cast<float>(size) / sh);
    const int content_w = std::clamp(static_cast<int>(std::lround(sw * fit)), 1, size);
    const int content_h = std::clamp(static_cast<int>(std::lround(sh * fit)), 1, size);
    const int offset_x = (size - content_w) / 2;
    const int offset_y = (size - content_h) / 2;
    *uv_transform = glm::vec4(static_cast<float>(content_w) / size,
                              static_cast<float>(content_h) / size,
                              static_cast<float>(offset_x) / size,
                              static_cast<float>(offset_y) / size);

    std::vector<unsigned char> out(static_cast<size_t>(size) * size * 4);
    for (int y = 0; y < size; y++) {
        float fy = (y - offset_y + 0.5f) * sh / content_h - 0.5f;
        fy = std::clamp(fy, 0.0f, static_cast<float>(sh - 1));
        const int y0 = static_cast<int>(std::floor(fy));
        const int y1 = std::min(y0 + 1, sh - 1);
        const float ty = fy - y0;
        for (int x = 0; x < size; x++) {
            float fx = (x - offset_x + 0.5f) * sw / content_w - 0.5f;
            fx = std::clamp(fx, 0.0f, static_cast<float>(sw - 1));
            const int x0 = static_cast<int>(std::floor(fx));
            const int x1 = std::min(x0 + 1, sw - 1);
            const float tx = fx - x0;
            for (int c = 0; c < 4; c++) {
                const float a = src[(static_cast<size_t>(y0) * sw + x0) * 4 + c] * (1 - tx) +
                                src[(static_cast<size_t>(y0) * sw + x1) * 4 + c] * tx;
                const float b = src[(static_cast<size_t>(y1) * sw + x0) * 4 + c] * (1 - tx) +
                                src[(static_cast<size_t>(y1) * sw + x1) * 4 + c] * tx;
                out[(static_cast<size_t>(y) * size + x) * 4 + c] =
                    static_cast<unsigned char>(a * (1 - ty) + b * ty + 0.5f);
            }
        }
    }
    return out;
}

uint32_t assets_load_texture(AssetPool* pool, const std::string& path, const std::string& alpha_path) {
    std::string cache_key = path;
    cache_key.push_back('\n');
    cache_key += alpha_path;
    auto cached = pool->texture_lookup.find(cache_key);
    if (cached != pool->texture_lookup.end()) {
        return cached->second;
    }

    if (pool->texture_layers >= pool->max_texture_layers) {
        std::cerr << "Texture array full (" << pool->max_texture_layers << " layers): " << path << std::endl;
        return INVALID_ASSET_ID;
    }

    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char *data = stbi_load(path.c_str(), &width, &height, &nrChannels, 4);
    if (!data) {
        std::cerr << "Failed to load texture: " << path << std::endl;
        return INVALID_ASSET_ID;
    }

    if (!alpha_path.empty()) {
        int alpha_width, alpha_height, alpha_channels;
        unsigned char* alpha_data = stbi_load(alpha_path.c_str(), &alpha_width, &alpha_height,
                                              &alpha_channels, 4);
        if (!alpha_data) {
            std::cerr << "Failed to load alpha map: " << alpha_path << std::endl;
            stbi_image_free(data);
            return INVALID_ASSET_ID;
        }
        for (int y = 0; y < height; ++y) {
            const int alpha_y = std::min(alpha_height - 1,
                static_cast<int>((static_cast<double>(y) + 0.5) * alpha_height / height));
            for (int x = 0; x < width; ++x) {
                const int alpha_x = std::min(alpha_width - 1,
                    static_cast<int>((static_cast<double>(x) + 0.5) * alpha_width / width));
                const size_t pixel = static_cast<size_t>(y) * width + x;
                const unsigned char opacity =
                    alpha_data[(static_cast<size_t>(alpha_y) * alpha_width + alpha_x) * 4];
                // MTL map_d is the material opacity source and takes precedence
                // over any alpha channel already present in the diffuse image.
                data[pixel * 4 + 3] = opacity;
            }
        }
        stbi_image_free(alpha_data);
    }

    bool has_alpha = false;
    for (size_t pixel = 0, count = static_cast<size_t>(width) * height; pixel < count; ++pixel) {
        if (data[pixel * 4 + 3] < 255) {
            has_alpha = true;
            break;
        }
    }

    // The first texture defines the layer size; allocate the whole array up front
    // (glTexStorage3D needs GL 4.2, macOS stops at 4.1, so use glTexImage3D).
    if (pool->texture_array == 0) {
        pool->texture_size = std::max(width, height);
        glGenTextures(1, &pool->texture_array);
        glBindTexture(GL_TEXTURE_2D_ARRAY, pool->texture_array);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, pool->texture_size, pool->texture_size,
                     pool->max_texture_layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Anisotropic filtering, if the driver exposes it (GL_EXT_texture_filter_anisotropic)
        const GLenum TEXTURE_MAX_ANISOTROPY = 0x84FE;
        const GLenum MAX_TEXTURE_MAX_ANISOTROPY = 0x84FF;
        while (glGetError() != GL_NO_ERROR) {}
        GLfloat max_aniso = 0.0f;
        glGetFloatv(MAX_TEXTURE_MAX_ANISOTROPY, &max_aniso);
        if (glGetError() == GL_NO_ERROR && max_aniso > 1.0f) {
            glTexParameterf(GL_TEXTURE_2D_ARRAY, TEXTURE_MAX_ANISOTROPY, std::min(max_aniso, 8.0f));
        }
    } else {
        glBindTexture(GL_TEXTURE_2D_ARRAY, pool->texture_array);
    }

    glm::vec4 uv_transform(1.0f, 1.0f, 0.0f, 0.0f);
    const unsigned char* upload = data;
    std::vector<unsigned char> resized;
    if (width != pool->texture_size || height != pool->texture_size) {
        resized = resample_rgba_aspect_fit(data, width, height, pool->texture_size, &uv_transform);
        upload = resized.data();
    }

    uint32_t layer = pool->texture_layers++;
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, pool->texture_size, pool->texture_size, 1,
                    GL_RGBA, GL_UNSIGNED_BYTE, upload);
    pool->texture_uv_transforms.push_back(uv_transform);
    pool->texture_has_alpha.push_back(has_alpha ? 1 : 0);
    pool->texture_mipmaps_dirty = true;
    stbi_image_free(data);

    pool->texture_lookup[cache_key] = layer;
    return layer;
}

bool assets_finalize_texture_uploads(AssetPool* pool) {
    if (!pool || pool->texture_array == 0) return false;
    if (!pool->texture_mipmaps_dirty) return true;
    glBindTexture(GL_TEXTURE_2D_ARRAY, pool->texture_array);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    pool->texture_mipmaps_dirty = false;
    return true;
}
