## 语言

我正在学习中文，请使用中文和我交流，我在编写项目的同时可以学习到中文的交流技巧。代码注释与提交信息使用简体中文。请遵循此约定。

# CLAUDE.md

本文件为 Claude Code (claude.ai/code) 在本仓库工作时提供指引。

## 项目概述

iMc 是一个用现代 OpenGL（4.6 core profile）和 C++17 编写的、受 Minecraft 启发的体素渲染引擎。核心特性：

- 基于 section 的区块架构（16×16×16 section，沿 Y 堆叠成 16×CHUNK_HEIGHT×16 的 chunk）
- 多线程区块管线：**两阶段任务**（Task 1 生成方块数据 → Task 2 用自身 + 4 邻居方块数据一次性构建含边界的完整 mesh），由 worker 池执行
- GPU 实例 arena（size-class 分配器）+ `glMultiDrawElementsIndirect`
- 8 字节紧凑 `InstanceData`（`packed32 + blockType16 + textureLayer16`）；世界坐标在 shader 内由 `sectionBases[gl_DrawID]` 重建
- 增量 VBO patch（`glMapBufferRange` + `UNSYNCHRONIZED`）—— 单次改方块只上传变化的面
- 延迟渲染 + HBAO（蓝噪声 + 时域累积）+ 软阴影（PCSS+VSSM + TAA 降噪）+ 昼夜动态光照
- 第一人称玩家控制、AABB 物理、地形生成、粒子、UI、第三人称玩家模型
- **物品系统**：数据/行为分离（`ItemDefinition` + `ItemStack` + 无状态 `Item` 行为），方块物品渲染成真立方体（背包图标 / 掉落物 / 手持），掉落物邻近同类堆叠 + 厚度可视化，背包「光标携带」拖拽（长按脱离 / 拖出丢弃），手持物品复用手臂挥手动画，支持自定义 OBJ 模型物品（如望远镜）
- **存档系统**：Minecraft Anvil 风格的 region 文件（`.mca`）+ LZ4 压缩 + 自动保存
- **局域网联机**：基于 ENet 的 Host/Join 架构，玩家位置同步 + 区块数据同步
- 逐 section 视锥/距离剔除；超出 `renderRadius + EVICT_MARGIN_CHUNKS` 的 GPU slot 驱逐

## 构建系统

- **IDE**：Visual Studio 2022，解决方案 `iMc.sln`
- **平台**：Windows x64，C++17（`stdcpp17`）
- **配置**：Debug 与 Release（x64 为主目标）
- **属性表**：`PropertySheet_debug.props`（Debug）/ `PropertySheet.props`（Release）定义 include/lib 路径与链接库
- **外部库**（从 `D:\library\` 链接）：GLFW、GLEW、GLM、Assimp、jsoncpp 0.5.0、stb_image
- **链接的库**：`glew32[d].lib`、`glfw3.lib`、`opengl32.lib`、`assimp-vc143-mt[d].lib`、`unit.lib`、`json_vc71_libmt[d].lib`
- **网络/压缩库**：ENet（`scr/enet/enet.h`，header-only，`iMc.cpp` 里 `#define ENET_IMPLEMENTATION`）；LZ4（`scr/net/lz4.h`）用于网络与存档的方块数据压缩。两者均随源码内置，无需外链。
- **DLL**：预生成步骤把 `bin/debug/` 或 `bin/release/` 的 DLL 拷到输出目录
- **GL 版本**：上下文请求 **4.6 core**。`gl_DrawID`（GLSL 4.60 内置）必需 —— geometry / shadow 顶点着色器用 `sectionBases[gl_DrawID]` 还原世界空间 section 原点。同时依赖 `glMultiDrawElementsIndirect` + `baseInstance`（4.2/4.3 起进 core）和 SSBO（4.3）。两个顶点着色器都用 `#version 460 core`。
- **构建**：在 VS2022 打开 `iMc.sln`，选 x64 Debug/Release，编译运行
- **换行符**：所有源文件必须用 **CRLF**（Windows）换行。新建 `.h`/`.cpp` 时先转 CRLF（如 `unix2dos file.cpp`）再编译。纯 LF 文件会让 MSVC 误解析，报出诸如 "identifier undeclared"、"not a member of struct" 这类明明源码里有的符号的假错误。
- **文件编码**：含中文（注释）的文件优先 UTF-8 with BOM，与现有文件一致，避免 MSVC 当成 GBK。纯 ASCII 文件可用无 BOM 的 UTF-8。
- **jsoncpp 注意**：内置的是老的 jsoncpp 0.5.0 API。用 `Json::Reader::parse(string, Value)` 和 `reader.getFormatedErrorMessages()`（注意拼写：单 t 的 "Formated"）。新版 `CharReaderBuilder` / `parseFromStream` **不可用**。

## 架构

### 入口与会话流程

`scr/iMc.cpp` —— 仅做最小工作：导出独显标志（`NvOptimusEnablement` / `AmdPowerXpressRequestHighPerformance`）、`#define ENET_IMPLEMENTATION` 引入 ENet 实现、`srand(13)`、构造 `CliManager`、解析命令行、`cli.run()`。

`scr/CliManager.h/.cpp` —— 会话管理器，是真正的程序骨架：
- **持久化 GL 上下文**：`initPersistentContext()` 创建一个 1×1 隐藏窗口作为持久上下文，纹理等容器对象加载到这里；后续每个游戏窗口都共享它（`glfwCreateWindow(..., m_persistentCtx)`），所以返回主菜单再开新世界不必重载纹理。
- **菜单循环**（`run()`）：New World / Load Save / LAN Join / Exit。世界选择、种子输入（FNV-1a 把字符串种子哈希成 64 位）、网络模式（Single / Host）都在这里交互式收集到 `SessionConfig`。
- **命令行直启**：支持 `--host [port]`、`--join <addr> [port]`、`--world <name>`、`--winpos <x> <y>`，跳过交互菜单（用于多开联机调试）。命令行默认端口 **60011**。
- 每个会话：`createWindow()` → 构造 `World` → 若联网调 `world.setupNetworking()` → `runWorldSafe()`（用 SEH `__try/__except` 包住 `world.run()`，崩溃时回菜单而非退出）→ `destroyWindow()`。

### 核心系统

- **World**（`scr/World.h/.cpp`）—— 顶层编排器。拥有 Player、ChunkManager、RenderSystem、（联网时）NetManager、（非 Join 模式时）ChunkSaveManager。设置 GLFW 输入回调并转发给 Player。`NetMode` 枚举：`None`（单机）/ `Host`（开房）/ `Join`（加入）。
  - **构造**：Join 模式不创建存档（种子由服务端分发，客户端不本地持久化）；其余模式创建 `ChunkSaveManager` 并 `createWorld` 或 `openWorld`。
  - **`run()` 初始化顺序**：建 RenderSystem（栈上）→ 建 ChunkManager → **Join 模式必须在 `initialize()` 前 `setNetworkClient(true)`**（否则会走本地地形生成）→ `chunkManager->initialize()` → `netManager->setChunkManager()`。
    - Host：进 800ms 的 `netManager->update()` 循环消化握手（着色器编译等耗时操作期间客户端的 JOIN_REQUEST 不会超时）。
    - Join：进最多 15 秒的初始地形同步循环，直到 `getLoadedChunkCount() >= 25`（玩家周围 5×5）。
    - 读档玩家位置 / 皮肤。
  - **主循环**（每帧严格顺序）：`netManager->update()`（帧首 poll+dispatch）→ `player->update()` → 把本地玩家位置/朝向写入 `NetState` → **`chunkManager->update(camera)`** → 新世界出生点计算 → `renderSystem.render(..., netManager)` → swap + poll → `Profiler::frame()`。
  - **退出**：保存玩家状态 + `chunkManager->saveAllDirtyChunks()` + 关存档。

- **Player**（`scr/Player.h/.cpp`）—— 整合相机、物理、移动（行走/奔跑/下蹲/观察者模式）、物品栏（hotbar）、方块交互（raycast 放置/破坏）、AABB 碰撞。三段速移动 + 双击冲刺。`getSaveData()` / `loadSaveData(PlayerSaveData)` 做存档；`getPosition()` / `setPosition()` 供网络同步（传送）。第三人称由 `toggleThirdPerson()`（F3）切换。
  - **走路镜头抖动**（`updateViewBob`）：仅本地第一人称叠加到相机（`m_viewBobOffset`，不改 `m_position`，故网络对端看不到；第三人称不加，同原版 MC）。相位按水平位移推进（步频同步），垂直 2× 频率、左右 1× 频率、前后随垂直同频；奔跑更强。参数 `view_bob_enabled/scale/run_scale`。
  - **贴地探测**（`updatePhysics`）：落地碰撞把玩家推到地面上方 `PHYSICS_BIAS`(0.001m)，而高帧率下每帧重力下坠量 g·dt² 小于此间隙 → 玩家悬空、`m_onGround` 反复 true/false 抖动（带动落地动画/镜头/模型抖动）。修复：上一帧着地且未上升时强制至少下探 `GROUND_STICK`(0.02m)，保证每帧稳定碰到地面。

- **ChunkManager**（`scr/chunk/ChunkManager.h/.cpp`）—— 区块生命周期编排。**两阶段任务管线**（注意：与旧的 "build + 异步 stitch" 设计不同，已重构为下列两阶段）：
  - **Task 1（`JOB_BUILD`）**：worker 上跑 `TerrainGenerator::fillChunkBuffer`（或从磁盘 / 网络拿数据）填一个临时整 chunk buffer，再切片成 **16 个 section `BlockBox`**（`ChunkBoxes`），**不含任何 mesh**。结果进 `m_pendingBlockData`，主线程消化后存入 `m_blockReady`（`BlockReadyEntry`，持有 `ChunkBoxes` + 4-bit `neighborBlockReady` 标记哪些邻居方向已就绪）。
  - **Task 2（`JOB_MESH`）**：当一个 chunk **自身和 4 个横向邻居的方块数据都就绪**时，投递 mesh 任务。`MeshBuildInput` 携带自身 16 个 box（直接共享给生成出来的 Section，零拷贝）+ 4 邻居各 16 个 box（`shared_ptr`）。worker 把 self box `setBox` 进 Section，读邻居边界时对邻居对应 section 的 box **持读锁拷出一层**，一次性构建**含全部内部面 + 全部跨 chunk 边界面**的 16 个 section。结果进 `m_pendingMeshResults`。**没有独立的 stitch 阶段** —— 边界在 Task 2 里就一次缝好了，因为 worker 此时能（持读锁安全地）看到邻居方块数据。
  - **状态容器**：
    - `m_inFlight`（`ChunkKey → 时间戳`）：Task 1 在途（生成/磁盘/网络），带 `INFLIGHT_TIMEOUT_SEC = 5.0` 超时重试（主要服务网络客户端丢包重请求）。
    - `m_blockReady`（`ChunkKey → BlockReadyEntry`）：方块数据就绪、等邻居凑齐以投 Task 2。**这是 worker mesh 任务读取的方块数据来源**。
    - `m_meshInFlight`（`set<ChunkKey>`）：Task 2 在途。
    - `m_loadedChunks`（`ChunkKey → unique_ptr<Chunk>`）：mesh 完整，可渲染可交互。
    - `m_activeChunks`（`vector<Chunk*>`）：`m_loadedChunks` 中在玩家渲染半径内的子集，逻辑更新 + 渲染候选。
  - 拥有 GPU `ChunkArena` 和 `SectionKey → Slot` 映射（每 section 一个 slot）。
  - **每帧流程**：`integrateBlockData`（drain Task 1 结果 → 存入 `m_blockReady` → `notifyNeighborsBlockReady` → `checkAndSubmitMesh` 投递够格的 Task 2，受 `MAX_BLOCK_INTEGRATE_PER_FRAME = 8` 配额）→ `integrateMeshResults`（drain Task 2 结果 → `loadMeshResult` 装入 Chunk 放进 `m_loadedChunks` → `linkNeighbors`，受 `MAX_MESH_INTEGRATE_PER_FRAME = 4` 配额）→ `requestMissingChunks`（从中心向外按 Chebyshev 环补满在途队列）→ `updateActiveChunks` → `rebuildDrawCommands`。
  - `rebuildDrawCommands`：pass 1 上传脏 section（受 evict 半径与 `m_maxUploadsPerFrame` 过滤）；pass 2 遍历 `m_activeChunks` 构建 indirect 命令缓冲 + 并行的 `m_sectionBases` 数组（上传到 `binding=0` 的 SSBO，shader 内由 `gl_DrawID` 索引）。每 pass（geometry / shadow）发一次 `glMultiDrawElementsIndirect`，命令按 section 粒度。
  - **GPU slot 驱逐**：超出 `renderRadius + EVICT_MARGIN_CHUNKS` 的 chunk 释放其 section arena slot（`evictFarChunkSlots`），CPU 数据保留；`Section::notifyGpuSlotReleased` 清增量状态，重新入界时强制全量重传。
  - **远距离卸载**：超出 `renderRadius + UNLOAD_MARGIN_CHUNKS` 的 chunk 整体卸载（`unloadDistantChunks`），卸载前 dirty chunk 会存盘。
  - **存档集成**：`setSaveManager()`、`saveAllDirtyChunks()`、`doAutoSave()`（按 `auto_save_interval_sec` 定时）。Task 1 在 worker 里会先尝试 `ChunkSaveManager::loadChunk`，磁盘没有才地形生成。
  - **网络集成**：`setNetworkClient(true)` 让客户端跳过本地地形生成；`setNetworkChunkRequester(fn)` 注册回调，缺 chunk 时发 `CHUNK_REQUEST`；`importChunkData(x, z, blocks)` 把网络收到的方块数据塞进管线（绕过生成）；`forceChunkLoad(pos)` 供服务端按需响应。

- **Chunk**（`scr/chunk/Chunk.h/.cpp`）—— `SECTION_COUNT`（HEIGHT=256 时为 16）个 Section 的容器。持有 4 个横向邻居指针（`m_neighbors[4]`，±X/±Z）用于跨 chunk 交互路由。`m_nonEmptyMask` 位掩码：第 i 位置 1 表示 section[i] 有可见面，每次改 mesh 后刷新。`setBlockAndUpdate` 路由到所属 section 并更新 6 邻居（处理跨 section + 跨 chunk）。`isMeshReady()` 区分"方块数据就绪" vs "mesh 完整"。

- **Section**（`scr/chunk/Section.h/.cpp`）—— GPU mesh 存储单元。持有：
  - `m_box: std::shared_ptr<BlockBox>` —— section 的 16-bit/格 方块状态数据 + 一把读写锁，打包在 `BlockBox`（`scr/chunk/BlockBox.h`）里（见下方「区块数据唯一来源」）。方块数组每格低 8 位 `BlockType`，bits 8–11 `orient`，bits 12–15 预留。公开 API **只认 `BlockState`** —— `getBlock` 返回 `BlockState`，`setBlock(x,y,z, BlockState)` 写完整状态（持 `m_box->mutex` 写锁）。`addFaceLocal(x,y,z, BlockFace, BlockState)` 同理（orient 从 state 读出后塞进 `InstanceData.packed`）。`shared_ptr` 让 `adoptFrom` / `setBox` 是指针移动而非拷贝。Section 显式禁拷贝（防两个 Section 共享同一 box）、保留 move。
  - `m_instanceData`（可见面向量）和 `m_PosToInstanceIndex`（`BlockFaceLocKey → index` 映射）。
  - `isEmpty()` 是"无可见面"的权威信号（查 index map，不查 vector）。
  - 增量上传状态机：`m_dirty`（本帧需上传）/ `m_dirtyIndices`（上次上传后改动的索引，供 `ChunkArena::patch`）/ `m_freeSlots`（`BLOCK_ERRER` 占位位置，`addFaceLocal` 复用）/ `m_fullRebuildPending`（强制下次走全量 `reupload`）/ `clearDirty()`（上传成功后重置）。
  - **建图即 reserve**：`rebuildVisibilityInternal` 末尾为 `m_instanceData` 和 `m_PosToInstanceIndex` 各多 reserve 1024 项。这在 worker 线程做，使 Task 2 缝边界面（每 section 最多约 1024 个边界面）时不触发堆分配和 rehash —— 全局 malloc 锁下的堆分配曾是实测瓶颈。

- **ChunkArena**（`scr/chunk/ChunkArena.h/.cpp`）—— GPU VBO 分配器。Size-class 分配器，类 `{64, 256, 768, 1536, 3072, 6144, 12288}` 个实例，每类一条 free list；新分配优先从 bump cursor 取，freed slot 留在所属类（不合并，保持 alloc/free O(1)）。分配用 1.5× 超额吸收 section 增长。后备 VBO 可用 `glCopyBufferSubData` 增长且保留所有活 slot 偏移。两条上传路径：
  - `reupload(slot, data, count)` —— 全量 `glBufferSubData`；count 超容量则换更大 size class。
  - `patch(slot, data, dirtyIndices, indexCount, newCount)` —— 增量上传。用 `GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT`（无 `INVALIDATE_RANGE`，未触碰字节保留旧 GPU 值）map `[minIdx, maxIdx]` 一次，只写脏位置，unmap。**调用方须保证 GPU 当前没在读该 slot** —— `ChunkManager::update` 在帧首、`RenderSystem::render` 前跑，故成立。

- **ChunkWorkerPool**（`scr/chunk/ChunkWorkerPool.h/.cpp`）—— 线程池（worker 数由 `worker_threads` 决定，0=自动），两种任务：
  - `JOB_BUILD`（Task 1）：`submitBuild(pos)` → worker 先试 `ChunkSaveManager::loadChunk`，没有则 `TerrainGenerator::fillChunkBuffer`，产出 `BlockDataResult`（纯方块数据）。
  - `JOB_MESH`（Task 2）：`submitMeshBuild(MeshBuildInput)` → worker 用自身 + 4 邻居方块数据构建含边界的 16 个 section，产出 `ChunkBuildResult`。
  - 单 `m_jobs` deque（FIFO，两类任务等优先）+ 条件变量。两个完成队列：`m_blockDone` / `m_meshDone` 都是 `std::deque<std::unique_ptr<...>>`（unique_ptr 让重对象构造在锁外，锁内只 push 指针）。主线程 `drainBlockData()` / `drainMeshResults()` 用 swap O(1) 取走。
  - 三把锁：`m_jobMutex`（任务队列）、`m_blockDoneMutex`、`m_meshDoneMutex`。
  - `setSaveManager(sm)` 让 worker 能在 Task 1 里读盘。

- **RenderSystem**（`scr/render/RenderSystem.h/.cpp`）—— 延迟渲染管线：
  1. **G-Buffer pass**（position / normal / albedo / properties）—— 一次 `glMultiDrawElementsIndirect` 画所有可见 section
  2. **HBAO pass**（单帧地平线角 AO）+ AO 时域累积 + blur
  3. **Shadow map pass**（定向光，PCSS+VSSM）—— 复用同一 MDI 命令缓冲 + 同一 SectionBases SSBO
  4. **延迟光照 pass**（写入 `m_lightingFBO`）
  5. blit 到 `m_compositeFBO`（带深度，支持后续正向渲染）
  6. **正向 pass**：方块选中轮廓、3D 模型、**远程玩家模型**、粒子、UI
  7. blit 到默认帧缓冲上屏
  - **昼夜动态光照**：太阳绕 YZ 平面旋转，`m_sunIntensity` 平滑过渡昼夜，`m_sunWarmth` 控制色温（冷白 ↔ 暖橙）。完全在 RenderSystem 内管理。`toggleWeather()`（G 键）切换天气。
  - **远程玩家渲染**：`renderRemotePlayers(NetManager*, ...)` 遍历 `NetManager::getPlayers()`，用 `m_remotePlayerModel` + 按皮肤名缓存的纹理（`m_skinTextures`）绘制其他玩家。
  - `BlockRenderer` 用一个绑定到 arena VBO 的 VAO（arena 增长时重绑）。共享面 quad 在静态 VBO+EBO。逐实例属性从 arena 经 `baseInstance` 取：location 5 = `packed`（uint32，含 x/y/z/face/orient），location 7 = `blockType`（uint16），location 8 = `textureLayer`（uint16）。世界空间方块中心在顶点着色器里 `sectionBases[gl_DrawID].xyz + (lx, ly, lz) + 0.5` 重建。geometry / shadow pass 画前都 `glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sectionBaseSSBO)`。

### 区块数据唯一来源（`BlockBox` + section 读写锁）

历史上 chunk 的方块数据曾有两份：玩家修改的 `Section::m_blocks` 和一份「为邻居 mesh 准备的快照」`Chunk::m_fullBlockData`。这会导致：玩家改了边界方块后，邻居做 Task 2 时读到过期快照，生成出错误的边界可见面（多面 / 缺面）—— 单人模式碰不到，多人服务端会暴露。现已重构为**唯一数据源**：

- **`BlockBox`**（`scr/chunk/BlockBox.h`）：一个 section（16³）的 `std::array<BlockState, 4096> blocks` + 一把 `std::shared_mutex mutex`，打包在一起。含 `shared_mutex` 故不可移动 / 拷贝，**全程只通过 `std::shared_ptr<BlockBox>` 传递**（`ChunkBoxes = array<shared_ptr<BlockBox>, 16>`）。
- **唯一所有者**：Task 1 产出 box → `m_blockReady` 持有 → Task 2 把 self box 直接 `setBox` 进生成的 Section → `adoptSections` move 进 loaded chunk。**从生成到 loaded 全程同一份 box，无第二份快照。** 玩家修改、邻居读边界、网络序列化、存盘都走它。
- **锁规则**：
  - 玩家改方块 `Section::setBlock` 持 **写锁**（`unique_lock`）。
  - worker 读邻居边界（`ChunkWorkerPool` 内 `copyBoundaryLayerFromBox`）持 **读锁**（`shared_lock`）—— 两个邻居同时做 Task 2 都读同一 box 是读读共享、不互斥。
  - 网络 `serializeChunkFromBlocks` 持读锁拷 section 快照。
  - 主线程内部串行读（`getBlockAt` 碰撞 / raycast、`saveChunkToDisk` 存盘）**不加锁** —— 它们与玩家改方块同为主线程、天然串行，与 worker 的读锁是读读共享。
- **生命周期**：worker 投递时拷邻居 `shared_ptr`（引用计数 +1）。即使邻居 chunk 在 worker 读边界期间被卸载，box 也活到读取结束，**不会悬挂**。
- self box 在本 chunk LOADED 前玩家无法触碰，故 worker 读 / 写 self 不加锁。

### 网络模块（`scr/net/`）

基于 **ENet**（UDP，可靠/不可靠通道分离）的客户端-服务端局域网联机。Host 是权威端（持有存档），Join 是纯客户端（不落盘，从服务端拿种子和区块）。

- **NetManager**（`NetManager.h/.cpp`）—— 顶层网络管理器。
  - `host(port, worldName, seed)` / `join(ip, port, playerName)` / `update()`（每帧 poll + dispatch + 发送本地脏属性）/ `leave()`。
  - 玩家管理：`getLocalPlayer()` / `getPlayer(id)` / `getPlayers()`（`id → unique_ptr<NetPlayer>`）。服务端维护 `m_peerToPlayer`（`ENetPeer* → playerId`）。
  - 状态：`isHosting()` / `isConnected()` / `getLocalPlayerId()` / `getWorldSeed()` / `getWorldName()` / `getLocalNetState()` / `getLocalSkinName()`。
  - 地形同步入口：`setChunkManager(cm)`（内部 `m_chunkSync.init`）、`sendChunkData` / `broadcastChunkData` / `sendChunkRequest`。
  - 消息分发 `dispatchMessage` 按 `NetMsgType` 路由到一组 handler（服务端 / 客户端各一套）。
- **NetTransport**（`NetTransport.h/.cpp`）—— ENet 薄封装。`createServer` / `createClient` / `connect` / `poll`（非阻塞收事件）/ `sendReliable` / `sendUnreliable` / `flush`。
- **NetMessage / NetCommon**（`NetMessage.h/.cpp`、`NetCommon.h`）—— 消息编解码与协议常量。
  - 消息格式：4 字节 header（`uint8 msgType` + `uint8 reserved` + `uint16 payloadLen`）+ payload。
  - 通道：`CHANNEL_RELIABLE=0`（可靠有序，JOIN/CHUNK/重要属性）、`CHANNEL_UNRELIABLE=1`（不可靠无序，高频位置同步）。
  - `MAX_MSG_PAYLOAD=32768`、`DEFAULT_MAX_CLIENTS=32`、`NetConstants::DEFAULT_PORT=60011`（与 CliManager 命令行/菜单默认端口一致）。
  - `NetMsgType`：`JOIN_REQUEST/ACCEPT/DENY`、`PLAYER_JOINED/LEFT/LIST`、`PROPERTY_SYNC`、`CHUNK_DATA/REQUEST/RESPONSE`、`BLOCK_CHANGE`（预留）、`CHAT_MESSAGE`（预留）、`PING`、`CUSTOM`。便捷工厂 `NetMessage::joinRequest(...)` 等。
- **NetObject / NetObjectManager**（`NetObject.h/.cpp`、`NetObjectManager.h/.cpp`）—— 属性复制框架。`NetObject` 用宏注册可复制属性（带可靠性级别 + `OnRep` 回调），脏标记驱动；`NetObjectManager` 管对象生命周期、每帧收集脏属性打包成 `PROPERTY_SYNC`。
- **NetPlayer**（`NetPlayer.h/.cpp`）—— 网络玩家对象，内含 `PlayerNetState`（`NetObject` 子类，复制 `position` / `yaw` / `pitch`，均为 Unreliable，`OnRep` 更新渲染缓存）。本地玩家每帧把位置/朝向写进它，远端收到后驱动 `RenderSystem::renderRemotePlayers`。
- **NetChunkSync**（`NetChunkSync.h/.cpp`）—— 地形同步。
  - 服务端：`pushChunks()`（增量推新晋升到 loaded 的 chunk）、`pushAllChunks(peer)`（新玩家加入时全量推、按距离排序）、`handleChunkRequest`（按需响应，优先从 loaded 序列化，否则从 block-ready 数据，没有则挂 pending 等就绪）。per-peer `m_sentChunks` 去重。
  - 客户端：`onChunkData(data, len)` 解析（可能是多 chunk 批量），逐 chunk 调 `ChunkManager::importChunkData`。
  - chunk 序列化：每 section 用 LZ4 压缩 4096 个 `BlockState`，`FLAG_HAS_DATA` 区分全空气 section。
- **NetSerializer**（`NetSerializer.h`）—— `MemoryStream` + 各类型读写（含 `glm::vec3`、`BlockState`）。

### 存档模块（`scr/save/`）

Minecraft Anvil 风格的区块持久化。目录结构：`saves/<worldName>/world.json` + `saves/<worldName>/region/r.<rx>.<rz>.mca`。

- **ChunkSaveManager**（`ChunkSaveManager.h/.cpp`）—— 高层世界级存档。
  - 世界管理：`listWorlds(savesRoot="saves")`（静态，扫描返回 `WorldInfo{name, path, seed, lastPlayed}`）、`createWorld(name, seed)` / `openWorld(name)` / `closeWorld()`。
  - 区块 I/O（**线程安全**，`m_ioMutex` 串行化）：`loadChunk(pos, outBuf)` / `saveChunk(pos, buf)` / `chunkExists(pos)`，buffer 大小必须 = `CHUNK_VOLUME`（65536）。**worker 线程在 Task 1 里调 `loadChunk`，故必须线程安全。**
  - 玩家状态：`loadPlayerState(PlayerSaveData&)` / `savePlayerState(PlayerSaveData)`，存进 `world.json`。`PlayerSaveData = {posX/Y/Z, yaw, pitch}`。
  - region 缓存：`unordered_map<int64 key, unique_ptr<RegionFile>>`，key = `(rx<<32)|rz`。
  - chunk → region 定位：`regionX = floorDiv(chunkX, 32)`，本地坐标 `localX = chunkX - rx*32`。
  - chunk 内用 **TLV**（Tag-Length-Value）编码：header tag + 每 section 一个 blocks tag（LZ4 压缩的 4096 `BlockState`），便于将来扩展字段。
- **RegionFile**（`RegionFile.h/.cpp`）—— 底层 `.mca` 文件 I/O，格式借鉴 Minecraft Anvil。
  - 常量：`REGION_SIZE=32`（32×32=1024 chunk/region）、`SECTOR_SIZE=4096`、`HEADER_BYTES=8192`、`MAX_CHUNKS=1024`。
  - 8KB header：1024 个 offset（低 24bit = sector 起始偏移，高 8bit = sector 个数，0=未使用）+ 1024 个 timestamp。
  - chunk 数据：`4B uncompressed_size + 1B compression(0=none/1=zlib) + N 字节数据`，按 4KB sector 对齐。
  - `readChunk` / `writeChunk` / `hasChunk`；`allocateSectors` 用 best-fit 找空闲区间或追加到末尾。
  - 本类**不加锁**，由 `ChunkSaveManager` 的 `m_ioMutex` 负责串行化。

### 支撑系统

- **BlockType / BlockState**（`scr/chunk/BlockType.h/.cpp`）—— `BlockType` 是 1 字节枚举（AIR/STONE/…）；`BlockProperties::hasAxis` 标记轴向感知方块（目前只有 `BLOCK_WOOD`）。`BlockState` 是 chunk 子系统通用的 2 字节每格记录：低 8 位 `BlockType`，bits 8–11 `BlockOrient`（`ORIENT_PX..ORIENT_NY`=0..5，`ORIENT_NONE=0xF`），bits 12–15 预留（未来水位/生长/光照）。**`BlockType` 是 chunk 子系统内部实现细节** —— Section / Chunk / ChunkManager / `Ray::HitResult::blockState` 都收发 `BlockState`（用 `.type()` 比对 `BLOCK_AIR` 等）；`BlockType` 只在非 chunk 子系统（物品栏、粒子碎屑、UI）露面。`InstanceData` 是 GPU 面向的 **8 字节** 结构：`uint32 packed`（bits 0–3 lx，4–7 ly，8–11 lz，12–14 face，15–18 orient，19–31 预留）+ `uint16 blockType` + `uint16 textureLayer`。用 `InstanceData::makePacked(...)` 编码、`unpackX/Y/Z/Face` 解码。世界坐标 shader 内重建，不存 CPU。
- **PlacementContext**（`scr/Item.h`）—— `{adjacentPos, hitFace, playerForward}`，由 `Player::tryPlaceBlock` / `tryBreakBlock` 传入 `Item::onRightClick` / `onLeftClick`。`BlockItem::onRightClick` 用 `GetBlockProperties(type).hasAxis` 决定是否从 `hitFace` 推 orient（轴向块如木头）还是写 `ORIENT_NONE`。`BlockFace` 数值 0..5（RIGHT/LEFT/FRONT/BACK/UP/DOWN）刻意与 `BlockOrient` 0..5 相同，`orientFromHitFace` 就是个 `static_cast`。
- **TerrainGenerator**（`scr/generate/`）—— Perlin 噪声地形。`fillChunkBuffer(BlockState*, ivec2)` 是 worker 用的线程安全路径（无共享可变状态），每格写 `ORIENT_NONE` —— 轴向块由放置侧代码产生，不由世界生成产生。
- **Collision**（`scr/collision/`）—— `AABB`（玩家碰撞）、`Ray`（方块拾取）、`PhysicsConstants.h`。
- **Particle**（`scr/particle/`）—— GPU compute shader 粒子（`GPUParticleSystem`）、基于 ECS 的 CPU 粒子（`ECSParticleSystem`），由 `ParticleManager` 管理。
- **Model**（`scr/mode/`）—— Assimp 模型加载（`Mesh`/`Model`），玩家模型 + 骨骼动画 + 皮肤（`PlayerModel`/`PlayerAnimator`/`SkinManager`）。
- **UI**（`scr/UI/`）—— `UIManager`（单例）、`UIHotbar`（底部快捷栏）、`UIInventory`（E 键背包面板，原版 inventory.png 度量）、`UISlot`（自包含格子：图标 + 数量角标 + 耐久条）、`UINumber`（用 ascii.png 字形烘焙数量角标）。`UISlot::setContent` 的 `iconTexOverride` 参数让方块物品用等距立方体图标（见下方物品系统）。
- **Shader**（`scr/Shader.h/.cpp`）—— 着色器程序加载。
- **TextureMgr**（`scr/TextureMgr.h/.cpp`）—— 从 `assert/textures/` 加载纹理数组，由 `textures_config.json` 配置。
- **RuntimeConfig**（`scr/RuntimeConfig.h/.cpp`）—— 单例，首次访问时加载 `assert/runtime_config.json`。改 JSON 即可调参，不必重编。当前字段见下。
- **Profiler**（`scr/Profiler.h/.cpp`）—— 轻量 CPU 分析器。`PROFILE_SCOPE("name")` 计时一个块；`Profiler::frame()`（主循环每次调）在 RuntimeConfig 启用时每秒打印聚合的 top section。

### 物品与掉落物系统（`scr/item/`、`scr/entity/`）

**数据 / 行为分离**（类 UE5 DataAsset）：

- **`ItemDefinition`**（`scr/item/ItemDefinition.h`）—— 物品静态数据资产：id / 显示名 / 图标 / 类别 / 最大堆叠 / 耐久 / 模型类型 / 对应方块类型 / 行为标识。`ItemModelType` 枚举 `EXTRUDED_2D`（挤出 2D 图标）/ `BLOCK_CUBE` / `CUSTOM_MODEL`（外部 OBJ）。运行时字段 `iconTexture`（平面图标）与 `guiIconTexture`（方块等距立方体图标，非方块 = 0）。`isBlockItem()` = `category==BLOCK && blockType!=AIR`。
- **`ItemStack`**（`scr/item/ItemStack.h`）—— 背包每格 / 掉落物运行时内容：`def` 指针 + count + durability。`empty()` / `sameItem()` / `maxStack()`。
- **`Item`**（`scr/Item.h`）—— 无状态行为对象，`ItemFactory` 按 `behavior` 字段绑定；对 `ItemStack` 操作（放置减 count、使用减耐久）。
- **`ItemRegistry`**（`scr/item/ItemRegistry.h/.cpp`）—— 单例，加载 `assert/item_registry.json`（jsoncpp 0.5.0 老 API + 注释剥离）。调试模式（`debug_mode`）下只加载 `load_in_debug` 标记物品的图标 GL 纹理，避免全量 600+ 张。图标注册名带 `itemicon_` 前缀避免与方块纹理撞名。`forEachMutable` 供 RenderSystem 回填 `guiIconTexture`。加图标后跑 `python tools/gen_item_registry.py`（合并式，保留手工编辑）。

**方块物品立方体渲染**（`scr/item/BlockItemModel.h/.cpp`）：为 `category==block` 的物品建居中单位立方体，**逐面纹理层走 `BlockFaceType::getTextureLayer`（与地形同源）**，`block_item.vert/frag` 采样方块纹理数组（`sampler2DArray`）。三处使用：

1. **背包 / 快捷栏图标**：`RenderSystem::generateBlockIcons()` 在 `initialize()` 末尾用离屏 FBO **等距渲染**成 2D 图标纹理，回填 `guiIconTexture`，UISlot 用 `iconTexOverride` 优先取用。**注意**：FBO 原点在左下、UI 采样按纹理 row0=顶，两者 Y 相反，故 ortho 上下边要对调（`glm::ortho(l,r,top,bottom,...)`，top>bottom）令渲染竖直翻转抵消，否则图标上下颠倒。
2. **掉落物**（世界空间）；3. **手持**（相机空间）。
- `BlockItemModel::hasValidTextures` 兜底：`BlockFaceType` 里没注册纹理层的方块（如缺 sand 层）退回挤出 2D 图标，避免整块显示成错误纹理。
- `BlockItemModelCache` 单例（VAO 不跨 GL 上下文，`initialize` 里 `clear()`）。
- 自定义 OBJ 物品（`model_type:custom_model` + `model_path`，如 spyglass）：`RenderSystem::getItemModel` 按路径懒加载缓存 `Model`，用模型着色器 `mode.vert/frag` 渲染。

**掉落物**（`scr/entity/DroppedItem.h`、`DroppedItemManager.h/.cpp`）—— 单机本地实体（**不联网同步**）：重力 + AABB 落地 + 旋转/浮动动画 + 靠近玩家自动拾取。**堆叠**：同类掉落物先相互吸引（`ATTRACT_RANGE`）聚拢，再 `mergeNearby` 合并（上限 = `maxStack`）；渲染按 count 分 1~5 视觉层，立方体在 x/z 抖动叠高、挤出卡片沿本地 z 铺开，形成厚度感。破坏方块掉落 / F 键丢弃 / 拖拽丢弃都生成掉落物。

**背包「光标携带」拖拽**（`Player::m_cursorStack` + `World::updateInventory`）：鼠标本身作为一个物品格。长按某格 >0.18s → 该格整栈**脱离**到 `m_cursorStack`（**不占背包格**，源格清空 → 背包腾一格可继续吸附掉落物，故背包开时不再冻结掉落物 update）；光标图标（`UIInventory::m_cursorSlot`，含数量角标）随鼠标移动；释放落到目标格（放下 / 合并 / 交换）或**拖出面板外**（`panelContains` 判定）则丢弃到世界；关背包时 `returnCursorToInventory` 收尾（尽量塞回，塞不下丢出）。`Player` API：`pickUpToCursor` / `placeCursorToSlot` / `dropCursorStack` / `returnCursorToInventory`。

**手持物品（第一人称）**：持物时**隐藏手臂**（否则重叠），物品复用手臂挥手动画——`PlayerModel::advanceHandSwing` 抽出挥手状态机返回 `HandSwing{pitch,roll,lift}`，**每帧只推进一次**（空手→`drawFirstPersonHand` 内部推进；持物→`RenderSystem::renderFirstPersonHand` 推进并把 `swingMat` 叠加到物品变换）。破坏方块 = 长按左键 → 持续挥。

### 运行时配置（`assert/runtime_config.json`）

| JSON 字段 | 默认值 | 含义 |
|---|---|---|
| `render_radius` | 16 | 渲染半径（chunk），加载 (2r+1)² 个 chunk |
| `vertical_cull_ratio` | 0.5 | 下方 section 剔除比例：maxDownSections = render_radius × 此值 |
| `worker_threads` | 2 | worker 线程数，0=自动（hardware_concurrency） |
| `max_inflight_requests` | 32 | 同时在途的最大 build 任务数 |
| `max_uploads_per_frame` | 16 | 每帧最多上传多少脏 section 到 GPU arena |
| `auto_save_interval_sec` | 60 | 自动保存间隔（秒），0=禁用定时（卸载仍存盘） |
| `print_profile_every_second` | false | 每秒打印 profiler 汇总 |
| `profile_detailed` | false | 开启 cullPass/可见性的细粒度计时+计数（`rdc.cull.*` / `vis.*`）；关闭时这些热路径插桩完全绕过（零开销） |
| `verbose_texture_loading` | false | 输出纹理加载详情 |
| `verbose_shader_loading` | false | 输出着色器编译详情 |
| `view_bob_enabled` | true | 走路镜头抖动总开关（仅本地第一人称，第三人称不加，同原版 MC；只改相机不改玩家位置，网络对端看不到） |
| `view_bob_scale` | 1.0 | 镜头抖动幅度总比例（调试旋钮），0=关 |
| `view_bob_run_scale` | 1.6 | 奔跑时镜头抖动的额外幅度倍率 |
| `disable_ime` | true | 启动时禁用输入法(IME)：解除窗口与 IME 关联，避免进游戏后输入法切拼音吞掉 WASD、要先按 Shift 才能移动 |

### 关键常量

- **区块几何**（`scr/chunk/ChunkDimensions.h`）：`CHUNK_WIDTH=16`、`CHUNK_HEIGHT=256`、`CHUNK_DEPTH=16`、`CHUNK_VOLUME=65536`、`SECTION_HEIGHT=16`、`SECTION_COUNT=16`。**此头刻意独立于 `core.h`** 以最小化区块维度变动时的重编范围 —— 只有显式 include 它的文件受影响。
- **ChunkManager 半径余量**（`scr/chunk/ChunkManager.h`）：`EVICT_MARGIN_CHUNKS=2`（超此释放 GPU slot，CPU 数据留）、`BLOCK_PRELOAD_MARGIN=1`（build 请求外扩一圈，让边缘 chunk 凑齐 4 邻居方块数据投 Task 2）、`UNLOAD_MARGIN_CHUNKS=6`（超此整体卸载 chunk）。
- **每帧消化预算**（`scr/chunk/ChunkManager.h`）：`MAX_BLOCK_INTEGRATE_PER_FRAME=8`、`MAX_MESH_INTEGRATE_PER_FRAME=4`，把多 worker 同时完成的尖峰摊到多帧。`INFLIGHT_TIMEOUT_SEC=5.0`（在途超时重请求）。
- **World**（`scr/core.h` 的 `WorldConstants`）：`WORLD_SEED=114514`、`RENDER_RADIUS=8`（默认，运行时由 RuntimeConfig 覆盖）。注：`RenderConstants::MAX_INSTANCES` 已弃用。
- **屏幕 / 阴影贴图**（`scr/Data.h`）：`SCR_WIDTH×SCR_HEIGHT = 1200×900`，`SHADOW_WIDTH×SHADOW_HEIGHT = 4096×4096`，`MAX_SHADOW_DISTANCE=180.0`。
- **网络**（`scr/net/NetCommon.h`）：`MAX_MSG_PAYLOAD=32768`、`DEFAULT_MAX_CLIENTS=32`、`DEFAULT_PORT=60011`（与 CliManager 命令行/菜单默认端口一致）。

### 着色器

所有 GLSL 在 `shader/`。延迟管线用 `g_buffer.*`、`hbao.*`（单帧 HBAO）、`ao_accumulate.frag`（AO 时域累积）、`hbao_blur.*`、`shadow_mapping_depth.*`、`shadow_visibility.frag` + `shadow_accumulate.frag`（PCSS + 阴影时域累积）、`deferred_lighting.*`、`taa_resolve.*`（最终画面 TAA）。正向 pass 用 `outline.*`、`mode.*`（3D 模型 / 玩家 / 手持 OBJ 物品）、`item.*`（挤出 2D 物品，alpha discard）、`block_item.*`（方块物品立方体，采样方块纹理数组 `sampler2DArray`）、`particle.*`、`ui.*`。

- `g_buffer.vert` 和 `shadow_mapping_depth.vert` 是 `#version 460 core`。两者解包逐实例 `packed` 属性（x/y/z/face）并从 `binding=0` 的 SSBO 读 `sectionBases[gl_DrawID].xyz` 还原世界空间方块中心。其余着色器保持原版本。
- `g_buffer.frag` 丢弃 `BLOCK_ERRER` 占位面 —— 这让 section 可以把复用/释放的面 slot 留在原地而不必急着 compact。`Section::removeFaceLocal` 把 slot 标 ERRER 并把索引压入 `m_freeSlots` 供 `addFaceLocal` 回收。

### 线程模型

- **主线程**：所有 GL 调用、`setBlockAndUpdate` / raycast / 碰撞 / arena 上传、`m_loadedChunks` / `m_blockReady` 容器变更、管线调度、网络 poll/dispatch、玩家更新。
- **Worker 线程**（ChunkWorkerPool）：
  - `JOB_BUILD`（Task 1）：`ChunkSaveManager::loadChunk`（线程安全）或 `TerrainGenerator::fillChunkBuffer`，切片产出 16 个 `BlockBox`。worker 看不到任何 `Chunk` 对象。
  - `JOB_MESH`（Task 2）：共享 self 的 16 个 box（无锁，self 还没 LOADED 玩家碰不到），对邻居 box **持读锁拷边界一层**构建含边界的完整 mesh，产 `ChunkBuildResult`。
- **方块数据并发**（见「区块数据唯一来源」）：玩家改方块持 section 写锁，worker 读邻居边界持读锁；二者互斥但临界区极短（写=改一格，读=拷一层 256 格），读读共享。主线程内部读不加锁。
- **无锁不变量**（设计快的根本）：
  - worker 永不触碰 `m_loadedChunks` 的 mesh；玩家交互与渲染只碰 `m_loadedChunks`。读写容器分离。worker 唯一会读到 LOADED chunk 数据的路径是「读邻居 box 边界」，已由 section 读写锁保护。
  - 重活（mesh 数据分配、Section 移动构造、`ChunkBuildResult` 构造）都在锁外；锁只保护 O(1)~O(一层) 的方块数据访问。
- **存档 I/O 线程安全**：`ChunkSaveManager` 用 `m_ioMutex` 串行化 `loadChunk`/`saveChunk`/region 缓存访问，因为 worker 线程会在 Task 1 里读盘。

### 帧序不变量

`World::run` 每帧在 `renderSystem.render(...)` **之前**调 `m_chunkManager->update(camera)`。这是承重设计：`ChunkArena::patch` 用 `GL_MAP_UNSYNCHRONIZED_BIT`，假设 GPU 当前没在读被 patch 的 slot。因为重入 `update` 时上一帧的绘制保证已完成，故成立。**不要在两个绘制 pass 之间调 `ChunkManager::update`（或任何会到达 `uploadSection` 的东西）。**

### GL 状态约定（改了就要恢复）

OpenGL 状态是全局的、跨 pass 持续的。**任何一个 pass / 渲染函数若改动了全局 GL 开关，完成后必须把它恢复回进入时的状态**，绝不把改动的状态泄漏给下一个 pass。这条覆盖但不限于：

- `glEnable/glDisable(GL_DEPTH_TEST)`、`glDepthMask`、`glDepthFunc`
- `glEnable/glDisable(GL_CULL_FACE)`、`glCullFace`
- `glEnable/glDisable(GL_BLEND)`、`glBlendFunc`
- `glColorMask`、`glPolygonMode`、`glViewport`、`glClearColor` 等

**为什么必须严格执行**（一次真实事故）：全屏 quad pass（SSAO/AO 累积/TAA/阴影累积）画前会 `glDisable(GL_DEPTH_TEST)`，因为全屏四边形不需要深度。曾有一个新增的 `aoAccumulatePass` 关了深度测试**却没恢复**，状态泄漏到紧随其后的 `sunShineShadowMap`。而 **OpenGL 在深度测试关闭时不写深度缓冲**（`glDepthMask(GL_TRUE)` 单独不够，必须 `GL_DEPTH_TEST` 也开着才会写深度）——于是阴影深度图 `glClear` 成 1.0 后几何一个深度都没写进去，得到一张空深度图，PCSS 永远找不到 blocker，整个场景退化成「只有亮暗、没有阴影」。这个 bug 隔着两个 pass、表现在完全无关的阴影系统上，极难定位。

**落地规则**：

1. **配对**：每个 `glDisable(GL_DEPTH_TEST)` 结尾配 `glEnable(GL_DEPTH_TEST)`；blend / cull / depthmask 同理。粒子等内部改 `glDepthMask(GL_FALSE)` 的渲染器，返回前自行恢复 `GL_TRUE`。
2. **生产者自保**：依赖某状态才能正确工作的 pass，**进入时显式设置**自己需要的状态，不假设上游留对了。典型：写深度的 pass（geometryPass / 阴影深度 pass）进入时显式 `glEnable(GL_DEPTH_TEST)` + `glDepthMask(GL_TRUE)` 再 `glClear(GL_DEPTH_BUFFER_BIT)`——否则上游漏掉的关闭状态会让深度 clear/write 静默失效。
3. 这两条是「双保险」：配对让状态不泄漏，生产者自保让即便泄漏也不致命。新增任何 pass 都按此办。

### 树内第三方代码

- `scr/enet/enet.h` —— ENet（UDP 网络库，header-only，`iMc.cpp` 里 `#define ENET_IMPLEMENTATION`）
- `scr/net/lz4.h` —— LZ4 压缩（网络 + 存档的方块数据压缩）
- `scr/entt/` —— EnTT ECS 库（header-only，粒子系统用）
- `scr/stb_image.h` / `scr/std_image.cpp` —— stb_image 纹理加载


