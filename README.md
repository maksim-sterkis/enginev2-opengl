# EngineV2

A custom, high-performance 3D graphics engine written in C++17 and OpenGL 3.3 Core, built around Data-Oriented Design (DOD) principles.

## Build and run on macOS

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENGINE_BUILD_APP_BUNDLE=OFF
cmake --build build -j8
./build/EngineV2
```

The DMG packaging script opts into a macOS `.app` bundle automatically.

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
- **Flight Camera**: 6DOF movement (`W`/`A`/`S`/`D`/`Space`/`Left Shift`) that works with or without the mouse lock. `P` toggles mouse-look, which uses raw mouse motion. World units are meters; the mouse wheel adjusts speed by 0.5 m/s per notch (0.5 to 50 m/s) when the cursor is not over the HUD. `ESC` opens the exit menu, and pressing it again closes the menu and locks the mouse.
- **Scene Switching**: The cube grid is the default scene. Press `2` for the textured Sponza atrium or `1` to return; each scene keeps its camera and inactive-scene simulation pauses. Sponza model attribution is in `assets/sponza/ATTRIBUTION.md`.
- **Scene Scale and Materials**: Each cube is 1 m on each side. Sponza's source units are interpreted as centimeters and scaled to meters. Diffuse and opacity maps preserve their aspect ratio and alpha-cutout surfaces render two-sided.
- **Fixed-Size HUD**: The ImGui HUD keeps its logical dimensions when the window is resized and uses the window's OS content scale for its font atlas.
- **Data-Oriented Design**: Flat arrays, plain structs, and persistent scratch buffers to avoid per-frame allocations.
