# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

iMc is a Minecraft-inspired voxel rendering engine built with modern OpenGL (4.3 core profile) and C++17. Features:
- Section-based chunk architecture (16×16×16 sections, stacked into 16×CHUNK_HEIGHT×16 chunks)
- Multi-threaded chunk generation + mesh build via worker pool
- GPU instance arena (size-class allocator) + `glMultiDrawElementsIndirect`
- Deferred rendering with SSAO and soft shadows (PCSS+VSSM)
- First-person player controls with AABB physics, terrain generation, particles, UI
- Per-section frustum/distance culling

## Build System

- **IDE**: Visual Studio 2022 with `iMc.sln`
- **Platform**: Windows x64, C++17 (`stdcpp17`)
- **Configurations**: Debug and Release (x64 is the primary target)
- **Property Sheets**: `PropertySheet_debug.props` (Debug) / `PropertySheet.props` (Release) define include/lib paths and linked libraries
- **External libraries** (linked from `D:\library\`): GLFW, GLEW, GLM, Assimp, jsoncpp 0.5.0, stb_image
- **Linked libs**: `glew32[d].lib`, `glfw3.lib`, `opengl32.lib`, `assimp-vc143-mt[d].lib`, `unit.lib`, `json_vc71_libmt[d].lib`
- **DLLs**: Pre-build step copies DLLs from `bin/debug/` or `bin/release/` to the output directory
- **GL version**: Context requested at 4.3 core. Required for `glMultiDrawElementsIndirect` + `baseInstance` (in core since 4.2/4.3).
- **Build**: Open `iMc.sln` in VS2022, select x64 Debug/Release, build and run
- **Line endings**: All source files must use **CRLF** (Windows) line endings. When creating new `.h`/`.cpp` files, convert them to CRLF (e.g. `unix2dos file.cpp`) before building. LF-only files can cause MSVC to misparse them, resulting in spurious errors like "identifier undeclared" or "not a member of struct" for symbols that are clearly present in the source.
- **File encoding**: Prefer UTF-8 with BOM for files containing Chinese characters (comments), to match the existing files and avoid MSVC treating them as GBK. Pure-ASCII files can be UTF-8 without BOM.
- **jsoncpp note**: The bundled jsoncpp is the old 0.5.0 API. Use `Json::Reader::parse(string, Value)` and `reader.getFormatedErrorMessages()` (note the typo: "Formated" with one t). The newer `CharReaderBuilder` / `parseFromStream` are NOT available.

## Architecture

### Entry Point and Game Loop

`scr/iMc.cpp` — initializes GLFW (4.3 core context) and GLEW, creates the window, instantiates `World` with a seed, and calls `world.run()` which contains the main loop. The main loop also calls `Profiler::frame()` once per frame for the optional CPU profiler.

### Core Systems

- **World** (`scr/World.h/.cpp`) — top-level orchestrator. Owns the Player, RenderSystem, and ChunkManager. Sets up GLFW input callbacks and delegates them to Player. Runs the main update/render loop. Reads `render_radius` from `RuntimeConfig`.

- **Player** (`scr/Player.h/.cpp`) — integrates camera, physics, movement (walk/run/crouch/spectator modes), inventory (hotbar), block interaction (place/break via raycasting), and AABB collision detection. Three-speed movement system with double-tap sprint.

- **ChunkManager** (`scr/chunk/ChunkManager.h/.cpp`) — orchestrates chunk lifecycle:
  - Submits chunk-build jobs to `ChunkWorkerPool`; integrates completed results in `integrateBuiltChunks` on the main thread
  - Tracks `m_loadedChunks` (all in-memory chunks; no eviction yet) vs `m_activeChunks` (chunks within player render radius — used for logic + rendering candidates)
  - `m_inFlight` set throttles in-flight worker requests to `MAX_INFLIGHT_REQUESTS`
  - Owns the GPU `ChunkArena` and a `SectionKey → Slot` map (slot per section, not per chunk)
  - Each frame: drains worker results → `requestMissingChunks` (refills inflight queue) → `rebuildDrawCommands` (uploads dirty sections, builds indirect command buffer)
  - Issues a single `glMultiDrawElementsIndirect` per pass (geometry / shadow); commands are per-section

- **Chunk** (`scr/chunk/Chunk.h/.cpp`) — container for `SECTION_COUNT` Sections (currently 16 for HEIGHT=256). Holds 4 horizontal neighbor pointers (`m_neighbors[4]` for ±X/±Z) for cross-chunk face stitching. `m_nonEmptyMask` is a bitmask: bit i set means section[i] has visible faces; refreshed after every mesh-mutating operation. `setBlockAndUpdate` routes to the owning section, updates 6 neighbors (handles cross-section + cross-chunk routing). `stitchHorizontalNeighbors` is called after a chunk is built to fix boundary face visibility with already-loaded neighbors.

- **Section** (`scr/chunk/Section.h/.cpp`) — the unit of GPU mesh storage. Holds a 16³ block array, an `InstanceData` vector (visible faces), and a `BlockFaceLocKey → index` map for incremental add/remove. Tracks `m_dirty` (needs re-upload) and `BLOCK_ERRER` placeholder count for compaction. `isEmpty()` is the authoritative "no visible faces" signal (queries the index map, not the vector).

- **ChunkArena** (`scr/chunk/ChunkArena.h/.cpp`) — GPU VBO allocator. Size-class allocator with classes `{64, 256, 768, 1536, 3072, 6144, 12288}` instances; each class has its own free list. New allocations come from the bump cursor first, freed slots stay in their class (no merging — keeps alloc/free O(1)). Allocate uses 1.5× oversize to absorb section growth without reallocation. Backing VBO can grow via `glCopyBufferSubData` while preserving offsets of all live slots.

- **ChunkWorkerPool** (`scr/chunk/ChunkWorkerPool.h/.cpp`) — thread pool (2-8 workers) that runs terrain generation + mesh visibility computation off the main thread. Workers produce `ChunkBuildResult` containing 4 already-meshed sections (intra-section + vertical neighbor faces resolved; horizontal cross-chunk boundaries left default-invisible for the main thread to stitch). Drop-on-arrival: results no longer needed are simply discarded by the main thread.

- **RenderSystem** (`scr/render/RenderSystem.h/.cpp`) — deferred rendering pipeline:
  1. G-Buffer pass (position, normal, albedo, properties) — single `glMultiDrawElementsIndirect` for all visible sections
  2. SSAO pass + blur
  3. Shadow map pass (directional light, PCSS+VSSM) — same MDI command buffer reused
  4. Deferred lighting pass
  5. Forward pass for outlines, models, particles, UI
  - `BlockRenderer` uses one VAO bound to the arena VBO (re-bound when arena grows). The shared face quad lives in a static VBO+EBO; per-instance attributes (position, faceIndex, blockType, textureLayer) are pulled from the arena via `baseInstance`.

### Supporting Systems

- **BlockType** (`scr/chunk/BlockType.h/.cpp`) — block type enum, per-face texture mapping via `BlockFaceType`, block properties. `InstanceData` is the GPU-facing 24-byte struct per visible face.
- **TerrainGenerator** (`scr/generate/`) — Perlin noise-based terrain. `fillChunkBuffer(BlockType*, ivec2)` is the thread-safe path used by workers (no shared mutable state).
- **Collision** (`scr/collision/`) — `AABB` for player collision, `Ray` for block picking, `PhysicsConstants.h`
- **Particle** (`scr/particle/`) — GPU compute shader particles (`GPUParticleSystem`), ECS-based CPU particles (`ECSParticleSystem`), managed by `ParticleManager`
- **UI** (`scr/UI/`) — `UIManager` (singleton) and `UIHotbar`
- **Model** (`scr/mode/`) — Assimp-based model loading
- **Shader** (`scr/Shader.h/.cpp`) — shader program loader
- **TextureMgr** (`scr/TextureMgr.h/.cpp`) — texture array loading from `assert/textures/` configured by `textures_config.json`
- **RuntimeConfig** (`scr/RuntimeConfig.h/.cpp`) — singleton; loads `assert/runtime_config.json` on first access. Holds `render_radius`, `max_inflight_requests`, `max_uploads_per_frame`, `worker_threads`, `print_profile_every_second`. Edit the JSON to change these without recompiling.
- **Profiler** (`scr/Profiler.h/.cpp`) — lightweight CPU profiler. Use `PROFILE_SCOPE("name")` to time a block; `Profiler::frame()` (called once per main-loop iteration) prints aggregated top sections every second when enabled in RuntimeConfig.

### Key Constants

- **Chunk geometry** (`scr/chunk/ChunkDimensions.h`): `CHUNK_WIDTH=16`, `CHUNK_HEIGHT=256`, `CHUNK_DEPTH=16`, `SECTION_HEIGHT=16`, `SECTION_COUNT = CHUNK_HEIGHT / SECTION_HEIGHT`. **This header is intentionally separate from `core.h` to minimize recompile footprint** when chunk dimensions change — only files that explicitly include it are affected.
- **Render**: `MAX_INSTANCES = 1,000,000` (in `scr/core.h` under `RenderConstants`)
- **World**: `WORLD_SEED = 114514`, `RENDER_RADIUS = 8` (default; runtime override via `RuntimeConfig`) — both in `core.h`'s `WorldConstants`
- **Screen / shadow map**: `1200x900` / `4096x4096` (`scr/Data.h`)

### Shaders

All GLSL shaders are in `shader/`. The deferred pipeline uses `g_buffer.*`, `ssao.*`, `ssao_blur.*`, `shadow_mapping_depth.*`, and `deferred_lighting.*`. Forward passes use `outline.*`, `mode.*`, `particle.*`, `ui.*`. `g_buffer.frag` discards `BLOCK_ERRER` placeholder faces — this lets sections defer compaction until many faces are stale.

### Threading Model

- **Main thread**: GL calls, all `setBlockAndUpdate` / raycast / collision / `stitchHorizontalNeighbors` / arena uploads.
- **Worker threads** (ChunkWorkerPool): `TerrainGenerator::fillChunkBuffer` + `Section::rebuildVisibilityInternal`. Workers don't see any `Chunk` object — they produce a self-contained `ChunkBuildResult` whose sections are then move-adopted by a freshly constructed `Chunk` on the main thread.
- **No locks needed** between worker output and main thread: results are pushed into a mutex-guarded queue once per chunk, then drained in bulk.

### Third-party Code in Tree

- `scr/entt/` — EnTT ECS library (header-only, used by particle system)
- `scr/stb_image.h` / `scr/std_image.cpp` — stb_image for texture loading

## Language

The codebase comments and commit messages are in Chinese (Simplified). Follow this convention.
