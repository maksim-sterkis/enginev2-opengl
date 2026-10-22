# File guide

What each file does and how they connect.

## Flow at a glance

```
main.cpp
  ├─ init window/GL (GLFW, glad) and Dear ImGui (ui.cpp)
  ├─ init_renderer ........ renderer.cpp  → shader.cpp compiles shaders/*
  ├─ assets_load_* ........ assets.cpp    → fills AssetPool (meshes + texture array)
  ├─ load scenes .......... cube grid and material-grouped Sponza submeshes
  └─ every frame:
       input → active scene / camera / ESC menu
       ecs_update_motion ............ ecs.cpp (active scene only)
       draw_frame ................... renderer.cpp (cull → batch → upload → draw)
       ui_draw_debug_window ......... ui.cpp (HUD, coordinates, CPU/GPU timings)
       swap buffers (+ VSync wait)
```

Dependency direction: `main.cpp` uses everything. `renderer` uses `assets` and `ecs`. `assets` uses `ecs.hpp` (for `InstanceData`). `ui` uses `renderer.hpp` and `ecs.hpp`. `ecs` and `shader` depend on nothing else in the project.

## Source files (`src/`)

### `main.cpp`
Program entry point and main loop.
- Creates the GLFW window (OpenGL 3.3 core, 32-bit depth), loads GL through glad, sets the swap interval (unlimited by default).
- Builds the `RenderState`, `AssetPool` and two `SceneState`s. The cube scene loads `assets/cube.obj` and `assets/texture.jpg`, then spawns the 100×100 cube grid. The Sponza scene loads the bundled OBJ/MTL and textures as material-grouped submeshes, recenters its bounds, and scales the source centimeter units to meters.
- Starts in the cube scene. Key-down presses of `1` and `2` switch between the cube grid and Sponza. Each scene keeps its own camera; only the active scene's simulation updates. Sponza starts at `(-12, -6, 1)` meters from its bounds center, with yaw `-15.5°` and pitch `11.3°`.
- Per frame: handles scene shortcuts, ESC (toggles the exit menu, frees or locks the mouse), `P` (mouse lock), camera look and movement, then updates the active scene, draws the frame, draws the UI, and swaps buffers.
- Contains `MacVSync`, a `CVDisplayLink` wrapper used to wait for the display's vertical blank when VSync mode is on, and the refresh-rate helper.
- Talks to the UI through `RenderState` fields (`exit_menu_open`, `request_mouse_lock`, `vsync_enabled`).

### `renderer.hpp` / `renderer.cpp`
Owns rendering state and draws a frame.
- `RenderState`: shader program and uniform locations, the instance VBO, window size, viewport mode, active `Camera`, culling and VSync flags, UI/main-loop flags, CPU `RenderStats`, and an asynchronous GPU timer-query ring.
- `init_renderer`: compiles the shader program (via `shader.cpp`), allocates the instance buffer, sets up reverse-Z.
- `draw_frame`: sets depth and cull state, builds the projection/view matrices, extracts the frustum planes, culls entities against mesh bounding spheres, sorts by mesh, rebuilds cached `InstanceData` for dirty entities, uploads the visible instances (orphaning the buffer), and issues one instanced draw per mesh. It records CPU timings in `RenderStats`; GPU mesh-pass timings are read from timer queries in later frames so the HUD does not stall waiting for them.
- `shutdown_renderer`: releases the timer queries, instance buffer and shader program while the GL context is still active.

### `ecs.hpp` / `ecs.cpp`
The entity store, as flat Struct-of-Arrays.
- Per-entity arrays: positions, rotations (Euler degrees), scales, velocities, mesh ids, texture ids.
- `InstanceData`: the 48-byte GPU layout (`pos_layer`, `rot`, `scale`). Each entity has a cached copy plus a dirty flag.
- Entity pool: `ecs_create_entity` reuses ids from a free list; `ecs_destroy_entity` swap-removes from the dense `active_entities` list.
- Motion: `ecs_randomize_velocities`, `ecs_stop_velocities`, `ecs_update_motion` (integrates velocity into position and updates the cached position directly).
- `ecs_mark_dirty` must be called after changing an entity's rotation, scale or texture so its instance data is rebuilt.

### `assets.hpp` / `assets.cpp`
Loads and stores GPU assets.
- `Mesh`: VAO/VBO/EBO, index count, and a bounding-sphere radius (used by culling).
- `AssetPool`: all meshes, one `GL_TEXTURE_2D_ARRAY` (the first texture sets the layer size, later ones are resampled to match, 64 layers), and path lookups for de-duplication.
- `assets_load_obj`: parses an OBJ with `tiny_obj_loader.h`, uploads vertices, computes the bounding radius, and sets up the per-instance attributes (locations 2–4) to read from the instance VBO.
- `assets_load_texture`: decodes with `stb_image.h`, uploads into the array, generates mipmaps, and returns the layer index used as the "texture id".

### `shader.hpp` / `shader.cpp`
Small helpers: read a file to a string, compile a shader, link a program. Used by `renderer.cpp`.

### `ui.hpp` / `ui.cpp`
Dear ImGui setup and the HUD.
- `ui_init` / `ui_shutdown` / `ui_new_frame` / `ui_render` wrap the ImGui GLFW and OpenGL3 backends.
- `ui_draw_debug_window`: the metrics window (FPS and lows, culling toggles, entity counts, CPU timings and asynchronous GPU timing), camera coordinates relative to the active scene origin, an orientation axis widget, a copy-spawn-coordinates button, cube-scene velocity controls, camera speed, input state, and the exit menu.
- Reads `RenderState` and `RenderStats`, and can change `RenderState` and the `ECS`.

### Third-party / generated
- `glad/` (`glad.c`, `glad.h`, `khrplatform.h`): OpenGL function loader.
- `stb_image.h`: single-header image decoder.
- `tiny_obj_loader.h`: single-header OBJ parser.

## Shaders (`shaders/`)
- `triangle.vert`: reads the mesh position and UV plus the per-instance position/layer, rotation quaternion and scale; rebuilds the world position and applies `u_view` and `u_proj`.
- `triangle.frag`: samples the `sampler2DArray` using the UV and the per-instance layer.

## Assets (`assets/`)
- `cube.obj`: the unit cube mesh.
- `texture.jpg`: the cube texture (1024×1024).
- `sponza/`: bundled Sponza OBJ, MTL, diffuse/opacity textures, and attribution. The model's bounds center is the HUD coordinate origin; its source files and CC BY 3.0 attribution are documented in `sponza/ATTRIBUTION.md`.

## Build files
- `CMakeLists.txt`: fetches GLFW, Dear ImGui (docking branch) and GLM with `FetchContent`, compiles everything under `src/` plus the ImGui sources, and links OpenGL and CoreVideo on macOS.
- `scripts/make_dmg.sh`: makes a Release-mode macOS `.app` and a versioned DMG in `dist/` (for example, `scripts/make_dmg.sh v0.2.0`).
- `imgui.ini`: ImGui window layout saved at runtime.
