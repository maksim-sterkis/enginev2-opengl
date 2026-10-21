#include "assets.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

void assets_init(AssetPool* pool) {
    pool->meshes.reserve(128);
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
        return 0;
    }

    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    float max_radius_sq = 0.0f;

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            float vx = attrib.vertices[3 * index.vertex_index + 0];
            float vy = attrib.vertices[3 * index.vertex_index + 1];
            float vz = attrib.vertices[3 * index.vertex_index + 2];
            max_radius_sq = std::max(max_radius_sq, vx * vx + vy * vy + vz * vz);
            vertices.push_back(vx);
            vertices.push_back(vy);
            vertices.push_back(vz);
            
            if (index.texcoord_index >= 0) {
                vertices.push_back(attrib.texcoords[2 * index.texcoord_index + 0]);
                vertices.push_back(attrib.texcoords[2 * index.texcoord_index + 1]);
            } else {
                vertices.push_back(0.0f);
                vertices.push_back(0.0f);
            }
            indices.push_back(indices.size());
        }
    }

    Mesh m;
    m.index_count = indices.size();
    m.radius = std::sqrt(max_radius_sq);

    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glGenBuffers(1, &m.ebo);

    glBindVertexArray(m.vao);

    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Per-instance attributes (locations 2,3,4), one InstanceData (48 bytes) per instance:
    //   2: vec4 pos_layer, 3: vec4 rotation quaternion, 4: vec4 scale
    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo);
    const GLsizei stride = sizeof(InstanceData);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(InstanceData, pos_layer));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(InstanceData, rot));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(InstanceData, scale));

    glVertexAttribDivisor(2, 1);
    glVertexAttribDivisor(3, 1);
    glVertexAttribDivisor(4, 1);

    glBindVertexArray(0);

    uint32_t id = pool->meshes.size();
    pool->meshes.push_back(m);
    pool->mesh_lookup[path] = id;
    return id;
}

// Bilinear resample of an RGBA8 image (used only when a texture doesn't match the array's layer size)
static std::vector<unsigned char> resample_rgba(const unsigned char* src, int sw, int sh, int dw, int dh) {
    std::vector<unsigned char> out((size_t)dw * dh * 4);
    for (int y = 0; y < dh; y++) {
        float fy = (y + 0.5f) * sh / dh - 0.5f;
        int y0 = std::clamp((int)std::floor(fy), 0, sh - 1);
        int y1 = std::min(y0 + 1, sh - 1);
        float ty = std::clamp(fy - y0, 0.0f, 1.0f);
        for (int x = 0; x < dw; x++) {
            float fx = (x + 0.5f) * sw / dw - 0.5f;
            int x0 = std::clamp((int)std::floor(fx), 0, sw - 1);
            int x1 = std::min(x0 + 1, sw - 1);
            float tx = std::clamp(fx - x0, 0.0f, 1.0f);
            for (int c = 0; c < 4; c++) {
                float a = src[((size_t)y0 * sw + x0) * 4 + c] * (1 - tx) + src[((size_t)y0 * sw + x1) * 4 + c] * tx;
                float b = src[((size_t)y1 * sw + x0) * 4 + c] * (1 - tx) + src[((size_t)y1 * sw + x1) * 4 + c] * tx;
                out[((size_t)y * dw + x) * 4 + c] = (unsigned char)(a * (1 - ty) + b * ty + 0.5f);
            }
        }
    }
    return out;
}

uint32_t assets_load_texture(AssetPool* pool, const std::string& path) {
    auto cached = pool->texture_lookup.find(path);
    if (cached != pool->texture_lookup.end()) {
        return cached->second;
    }

    if (pool->texture_layers >= pool->max_texture_layers) {
        std::cerr << "Texture array full (" << pool->max_texture_layers << " layers): " << path << std::endl;
        return 0;
    }

    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(true);
    // Always load as RGBA so every layer has the same format
    unsigned char *data = stbi_load(path.c_str(), &width, &height, &nrChannels, 4);
    if (!data) {
        std::cerr << "Failed to load texture: " << path << std::endl;
        return 0;
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

    const unsigned char* upload = data;
    std::vector<unsigned char> resized;
    if (width != pool->texture_size || height != pool->texture_size) {
        std::cerr << "Resampling " << path << " (" << width << "x" << height << ") to "
                  << pool->texture_size << "x" << pool->texture_size << std::endl;
        resized = resample_rgba(data, width, height, pool->texture_size, pool->texture_size);
        upload = resized.data();
    }

    uint32_t layer = pool->texture_layers++;
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, pool->texture_size, pool->texture_size, 1,
                    GL_RGBA, GL_UNSIGNED_BYTE, upload);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    stbi_image_free(data);

    pool->texture_lookup[path] = layer;
    return layer;
}
