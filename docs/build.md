# Building EngineV2

## Build and run on macOS

From the repository root, configure and build a Release executable:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENGINE_BUILD_APP_BUNDLE=OFF
cmake --build build -j8
./build/EngineV2
```

Run the executable from the repository root so its relative `assets/` and `shaders/` paths resolve.

## Create a macOS release image

Run the packaging script with the release version:

```sh
scripts/make_dmg.sh v0.2.0
```

This creates `dist/EngineV2-v0.2.0.dmg`, containing the versioned `EngineV2.app` bundle, assets, and shaders. The script builds in Release mode and reuses local dependency checkouts when available. The packaged app loads its assets locally and does not need a network connection at launch.
