# EngineV2

A custom, high-performance 3D graphics engine written in C++17 and OpenGL 3.3 Core, built around Data-Oriented Design (DOD) principles.

## Core Features

- **Reverse-Z Depth Buffering**: Reverse-Z depth (`glDepthRange(1.0, 0.0)`, `GL_GREATER`) with a 32-bit depth buffer for a very long draw distance without z-fighting.
- **Compact Instanced Rendering**: Each instance is 48 bytes (position and texture layer, rotation quaternion, scale) instead of a 64-byte `mat4`. The vertex shader rebuilds the transform on the GPU, and the instance buffer is orphaned each upload.
- **Dirty-Flagged Transform Cache**: Per-entity instance data is cached and only recomputed when the entity is marked dirty.
- **Entity Pool**: Struct-of-Arrays entity store with O(1) swap-remove destruction and free-list id recycling.
- **Frustum & Back-Face Culling**: Six-plane frustum extraction tested against per-mesh bounding spheres before sorting and uploading, plus hardware `GL_CULL_FACE`.
- **Texture Array Batching**: All textures live in one `GL_TEXTURE_2D_ARRAY` with mipmaps and anisotropic filtering where supported. Entities carry a layer index, so draws batch by mesh only. Meshes and textures are de-duplicated by path.
- **Entity Motion**: A button gives every cube a random direction and speed (no collisions), and another stops them.
- **Frame Pacing**: Unlimited FPS by default. VSync mode combines `glfwSwapInterval` with a macOS `CVDisplayLink` wait to hold the display's refresh rate.
- **Developer HUD**: A stock Dear ImGui window showing average FPS, 1% and 0.1% lows, visible and total entity counts, CPU stage timings (motion, build, upload, draw submit), culling toggles, viewport mode, camera speed and input state.
- **Flight Camera**: 6DOF movement (`W`/`A`/`S`/`D`/`Space`/`Left Shift`) that works with or without the mouse lock. `P` toggles mouse-look, which uses raw mouse motion. The mouse wheel adjusts speed by 0.5 per notch (0.5 to 50) when the cursor is not over the HUD. `ESC` opens the exit menu, and pressing it again closes the menu and locks the mouse.
- **Data-Oriented Design**: Flat arrays, plain structs, and persistent scratch buffers to avoid per-frame allocations.
