# EngineV2

A custom, high-performance 3D graphics engine written in C++17 and OpenGL 3.3 Core, built around strict Data-Oriented Design (DOD) principles.

## Core Features

- **3D Graphics Pipeline & Reverse-Z Depth**: Programmable vertex and fragment shaders with Reverse-Z depth buffering (`glDepthRange(1.0, 0.0)`, `GL_GREATER`) for near-infinite draw distances (`100,000` units) without z-fighting.
- **Compact Instanced Rendering (`InstanceData`)**: Packs per-instance transforms into 48 bytes (`vec4` position + texture layer, `vec4` rotation quaternion, `vec4` scale) instead of 64-byte `mat4` matrices, reconstructing world transforms on the GPU inside the vertex shader and streaming updates via buffer orphaning (`GL_STREAM_DRAW`).
- **Dirty-Flagged Transform Cache & O(1) ECS Pool**: Struct-of-Arrays (SoA) entity store with O(1) swap-remove destruction, free-list ID recycling, and cached GPU `InstanceData` that only recomputes quaternions/transforms when an entity is marked dirty (`ecs_mark_dirty`).
- **Frustum & Back-Face Culling**: Gribb–Hartmann 6-plane frustum extraction tested against per-mesh bounding spheres before sorting or uploading instance buffers, paired with hardware `GL_CULL_FACE` (`GL_BACK`, `GL_CCW`).
- **2D Texture Arrays & Asset Deduplication**: Consolidates loaded textures into a single `GL_TEXTURE_2D_ARRAY` (`sampler2DArray`) with mipmapping, anisotropic filtering, automatic bilinear resampling when dimensions differ, and path-based mesh/texture deduplication so all textured entities batch by mesh alone.
- **Hardware VBlank Synchronization (macOS `CVDisplayLink`)**: Uses Apple's `CoreVideo` `CVDisplayLink` hardware vertical blank interrupt alongside `glfwSwapInterval` to lock frame pacing to the physical display refresh rate (60 Hz, 120 Hz ProMotion, or external monitors) with zero busy-waiting.
- **Developer HUD & Frame Telemetry**: Integrated `Dear ImGui` panel displaying 100ms rolling average FPS, 1% and 0.1% low FPS (over a 1,000-frame window), visible/total entity counts, CPU stage timings (`build`, `upload`, `draw submit`), live culling toggles, viewport modes (Dynamic FOV vs. Forced 16:9), and input state.
- **First-Person Flight Camera**: Unlocked 6DOF camera (`W`/`A`/`S`/`D`/`Space`/`Left Shift`, toggle mouse lock with `P`, unlock/exit prompt with `ESC`) using `GLFW_RAW_MOUSE_MOTION` for unaccelerated mouse look.
- **Data-Oriented Design (DOD)**: Flat Struct-of-Arrays (`ECS`), POD structs, persistent scratch buffers to eliminate per-frame heap allocations, and cache-friendly transformation pipelines.

## Building & Running

```sh
cmake -S . -B build
cmake --build build -j 8
./build/EngineV2
```
