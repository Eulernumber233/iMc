# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

iMc is a Minecraft-inspired voxel rendering engine built with modern OpenGL (3.3+ core profile) and C++17. It features terrain generation, deferred rendering with SSAO and soft shadows (PCSS+VSSM), instanced rendering, dynamic chunk management, and first-person player controls with physics.

## Build System

- **IDE**: Visual Studio 2022 with `iMc.sln`
- **Platform**: Windows x64, C++17 (`stdcpp17`)
- **Configurations**: Debug and Release (x64 is the primary target)
- **Property Sheets**: `PropertySheet_debug.props` (Debug) / `PropertySheet.props` (Release) define include/lib paths and linked libraries
- **External libraries** (linked from `D:\library\`): GLFW, GLEW, GLM, Assimp, libjson, stb_image
- **Linked libs**: `glew32[d].lib`, `glfw3.lib`, `opengl32.lib`, `assimp-vc143-mt[d].lib`, `unit.lib`, `json_vc71_libmt[d].lib`
- **DLLs**: Pre-build step copies DLLs from `bin/debug/` or `bin/release/` to the output directory
- **Build**: Open `iMc.sln` in VS2022, select x64 Debug/Release, build and run
- **Line endings**: All source files must use **CRLF** (Windows) line endings. When creating new `.h`/`.cpp` files, convert them to CRLF (e.g. `unix2dos file.cpp`) before building. LF-only files can cause MSVC to misparse them, resulting in spurious errors like "identifier undeclared" or "not a member of struct" for symbols that are clearly present in the source.
- **File encoding**: Prefer UTF-8 with BOM for files containing Chinese characters (comments), to match the existing files and avoid MSVC treating them as GBK. Pure-ASCII files can be UTF-8 without BOM.

## Architecture

### Entry Point and Game Loop

`scr/iMc.cpp` — initializes GLFW/GLEW, creates the window, instantiates `World` with a seed, and calls `world.run()` which contains the main loop.

### Core Systems

- **World** (`scr/World.h/.cpp`) — top-level orchestrator. Owns the Player, RenderSystem, and ChunkManager. Sets up GLFW input callbacks and delegates them to Player. Runs the main update/render loop.
- **Player** (`scr/Player.h/.cpp`) — integrates camera, physics, movement (walk/run/crouch/spectator modes), inventory (hotbar), block interaction (place/break via raycasting), and AABB collision detection. Has a three-speed movement system with double-tap sprint.
- **ChunkManager** (`scr/chunk/ChunkManager.h/.cpp`) — manages chunk loading/unloading around the camera, frustum culling for visible chunks, and merges per-chunk instance data into a single render buffer. Uses a mutex for thread-safe render data access.
- **Chunk** (`scr/chunk/Chunk.h/.cpp`) — stores a 16x64x16 voxel grid. Performs visible-face culling (only generates faces adjacent to air). Outputs `InstanceData` for instanced rendering.
- **RenderSystem** (`scr/render/RenderSystem.h/.cpp`) — implements a deferred rendering pipeline:
  1. G-Buffer pass (position, normal, albedo, properties)
  2. SSAO pass + blur
  3. Shadow map pass (directional light, PCSS+VSSM)
  4. Deferred lighting pass
  5. Forward pass for outlines, models, particles, UI

### Supporting Systems

- **BlockType** (`scr/chunk/BlockType.h/.cpp`) — block type enum, per-face texture mapping via `BlockFaceType`, block properties (transparent, solid, emissive). `InstanceData` struct carries per-face render info.
- **TerrainGenerator** (`scr/generate/`) — Perlin noise-based terrain generation with multi-octave layering
- **Collision** (`scr/collision/`) — `AABB` for player collision, `Ray` for block picking, `PhysicsConstants.h` for tunable physics parameters
- **Particle** (`scr/particle/`) — GPU compute shader particles (`GPUParticleSystem`), ECS-based CPU particles (`ECSParticleSystem`), managed by `ParticleManager` (weather effects, block debris)
- **UI** (`scr/UI/`) — `UIManager` (singleton) and `UIHotbar` for HUD rendering
- **Model** (`scr/mode/`) — Assimp-based 3D model/mesh loading (used for held items like spyglass)
- **Shader** (`scr/Shader.h/.cpp`) — shader program loader, takes a list of `{stage, filepath}` pairs
- **TextureMgr** (`scr/TextureMgr.h/.cpp`) — texture array loading from `assert/textures/` configured by `textures_config.json`

### Key Constants

- Chunk size: 16x64x16 (defined in `scr/core.h` under `ChunkConstants`)
- Screen: 1200x900, shadow map: 4096x4096 (defined in `scr/Data.h`)
- Max instances: 1,000,000 (in `scr/core.h` under `RenderConstants`)

### Shaders

All GLSL shaders are in `shader/`. The deferred pipeline uses `g_buffer.*`, `ssao.*`, `ssao_blur.*`, `shadow_mapping_depth.*`, and `deferred_lighting.*`. Forward passes use `outline.*`, `mode.*`, `particle.*`, `ui.*`.

### Third-party Code in Tree

- `scr/entt/` — EnTT ECS library (header-only, used by particle system)
- `scr/stb_image.h` / `scr/std_image.cpp` — stb_image for texture loading

## Language

The codebase comments and commit messages are in Chinese (Simplified). Follow this convention.
