#include "World.h"
#include "UI/UIManager.h"
#include "Data.h"
#include "chunk/Chunk.h"
#include "chunk/ChunkDimensions.h"
#include "collision/PhysicsConstants.h"
#include "mode/SkinManager.h"
#include "save/ChunkSaveManager.h"
#include "RuntimeConfig.h"
#include "HotReload.h"
#include "DebugUI.h"
#include "Profiler.h"
#include "net/NetManager.h"
#include "item/ItemRegistry.h"
#include <iomanip>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <cctype>

World::World(GLFWwindow* window_, const std::string& worldName,
             uint64_t seed, bool isNewWorld, NetMode netMode)
    : m_window(window_), m_seed(seed), m_worldName(worldName), m_isNewWorld(isNewWorld),
      m_netMode(netMode)
{
    // Join 模式不创建存档：种子由服务端分发，客户端不需要本地持久化
    if (netMode != NetMode::Join) {
        m_saveManager = std::make_unique<ChunkSaveManager>();
        if (isNewWorld) {
            m_saveManager->createWorld(worldName, seed);
        } else {
            if (!m_saveManager->openWorld(worldName)) {
                std::cerr << "[World] 加载存档失败，将创建新世界\n";
                m_saveManager->createWorld(worldName, seed);
            } else {
                m_seed = m_saveManager->getSeed();
            }
        }
    }

    // 创建摄像机 (临时高空位置，新世界出生点会在区块(0,0)生成后重新计算)
    auto camera = std::make_shared<Camera>();
    camera->Position = glm::vec3(0.0f, 500.0f, 0.0f);

    // 创建玩家对象
    m_player = std::make_shared<Player>(camera, m_window);

    // 设置窗口用户指针，以便回调函数可以访问当前实例
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);
    glfwSetCursorPosCallback(m_window, mouseCallback);
    glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
    glfwSetKeyCallback(m_window, keyCallback);
    glfwSetScrollCallback(m_window, mouseScrollCallback);
    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

bool World::setupNetworking(NetMode mode, uint16_t port,
                            const std::string& joinAddress) {
    m_netMode = mode;
    if (mode == NetMode::None) return true;

    m_netManager = std::make_unique<NetManager>();

    if (mode == NetMode::Host) {
        if (!m_netManager->host(port, m_worldName, static_cast<uint32_t>(m_seed))) {
            std::cerr << "[World] Failed to host on port " << port << std::endl;
            m_netManager.reset();
            m_netMode = NetMode::None;
            return false;
        }
        std::cout << "[World] Hosting on port " << port << std::endl;
    } else if (mode == NetMode::Join) {
        if (!m_netManager->join(joinAddress, port, "Player")) {
            std::cerr << "[World] Failed to join " << joinAddress << ":" << port << std::endl;
            m_netManager.reset();
            m_netMode = NetMode::None;
            return false;
        }
        // 使用服务端分配的种子
        m_seed = m_netManager->getWorldSeed();
        std::cout << "[World] Joined " << joinAddress << ":" << port
                  << " (seed=" << m_seed << ")" << std::endl;
    }
    return true;
}

World::~World() {
    // 先显式销毁 NetManager，再让其余成员按声明逆序析构。
    // 关键：NetManager 析构会走 leave() → persistPlayerInventory → savePlayerInventoryFile，
    // 后者要访问 m_saveManager。而成员按声明逆序析构时 m_saveManager 比 m_netManager 先销毁，
    // 若留给默认析构就会在 m_saveManager 已释放后访问它（use-after-free / 读取访问冲突）。
    // 在析构函数体里 reset()——此刻所有成员都还活着——保证 m_saveManager 仍有效。
    // 正常退出时 run() 已调用过 leave()（m_connected=false），这里再 reset 只是幂等兜底；
    // 崩溃回菜单等异常路径下，这里才真正完成断网 + 持久化。
    if (m_netManager) m_netManager.reset();

    // 清理资源
    if (m_renderSystem) {
        // 注意：RenderSystem是在栈上创建的，不需要手动删除
        // 但我们需要确保指针不再被使用
        m_renderSystem = nullptr;
    }
}

int World::run() {
    // 纹理已在持久化 GL 上下文中加载，所有窗口共享，无需重复加载
    BlockFaceType::init_type_map();

    // 初始化渲染系统
    RenderSystem renderSystem(SCR_WIDTH, SCR_HEIGHT);
    m_renderSystem = &renderSystem;
    if (!renderSystem.initialize()) {
        std::cerr << "Failed to initialize render system" << std::endl;
        return -1;
    }

    // 初始化调试面板（ImGui）。此时 GL 上下文 current、GLFW 回调已在构造函数注册，
    // ImGui 会安装链式回调接住并转发它们。
    m_debugUI = std::make_unique<DebugUI>();
    m_debugUI->init(m_window);

    // 设置光照参数
    renderSystem.setLightDirection(glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f)));
    renderSystem.setLightColor(glm::vec3(1.0f, 0.95f, 0.9f));
    renderSystem.setLightIntensity(1.0f);
    renderSystem.setAmbientColor(glm::vec3(0.1f, 0.1f, 0.1f));

    // 初始化区块管理器（半径从 runtime config 读，方便不重编调参）
    m_chunkManager = std::make_shared<ChunkManager>(m_seed);

    // 网络客户端：必须在 initialize 之前设置，否则 initialize 会走本地地形生成
    if (m_netMode == NetMode::Join) {
        m_chunkManager->setNetworkClient(true);
        m_chunkManager->setSaveManager(nullptr);  // 客户端不需要存档
    } else {
        m_chunkManager->setSaveManager(m_saveManager.get());
    }
    m_chunkManager->initialize(RuntimeConfig::get().renderRadius, m_player->getCamera()->Position);
    m_chunkManager->printStats();

    // 网络：连接 ChunkManager（必须在 ChunkManager 创建后）
    if (m_netManager) {
        m_netManager->setChunkManager(m_chunkManager.get());

        // 服务端：开启 chunk 晋升事件记录，供 NetChunkSync 增量推送
        //（避免每帧全量扫描全部 chunk 造成稳态掉帧）。
        if (m_netMode == NetMode::Host) {
            m_chunkManager->setTrackPromotions(true);
        }

        // 客户端：设置网络 chunk 请求回调（ChunkManager 需要 chunk 时发送 CHUNK_REQUEST）
        if (m_netMode == NetMode::Join) {
            m_chunkManager->setNetworkChunkRequester(
                [this](int chunkX, int chunkZ) {
                    m_netManager->sendChunkRequest(chunkX, chunkZ);
                });
        }

        // Host / Join 都设置方块修改 sink：用户发起的放置/破坏不直接本地生效，
        // 而是交给 NetManager 走"服务端权威"流程（客户端发请求、服务端应用+广播）。
        m_chunkManager->setBlockChangeSink(
            [this](const glm::ivec3& worldPos, BlockState state) {
                m_netManager->requestBlockChange(
                    worldPos.x, worldPos.y, worldPos.z, state.bits);
            });

        // 世界时间同步（需求 3）：
        //  Host —— 权威在本地 RenderSystem，注册 handler 应用客户端发来的 WORLD_CMD；
        //          每帧把 RenderSystem 时间镜像进 WorldState 复制给客户端。
        //  Join —— 时间由网络驱动，RenderSystem 不本地推进；890opl 键改发 WORLD_CMD。
        if (m_netMode == NetMode::Host) {
            m_netManager->setWorldCmdHandler(
                [this](WorldCmdType t, float p) { applyTimeCommand(t, p); });
            // 背包持久化（需求 2）：按玩家名存独立文件 saves/<world>/players/<name>.inv
            m_netManager->setPlayerPersistCallbacks(
                [this](const std::string& name, const InventoryData& inv) {
                    savePlayerInventoryFile(name, inv);
                },
                [this](const std::string& name, InventoryData& out) {
                    return loadPlayerInventoryFile(name, out);
                });
        } else if (m_netMode == NetMode::Join) {
            m_renderSystem->setTimeExternallyDriven(true);
        }

        // 服务端：处理在初始化期间连接的客户端（着色器编译等耗时操作
        // 可能持续数秒，客户端在此期间发送 JOIN_REQUEST 会超时）
        // 使用时间循环而非固定次数，给予 ENET 握手往返足够的间隔
        if (m_netMode == NetMode::Host) {
            std::cout << "[World] Servicing pending connections..." << std::endl;
            auto start = std::chrono::steady_clock::now();
            while (std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start).count() < 800) {
                m_netManager->update();
                // 每轮 update 后短暂 yield，让对端有时间响应握手
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            std::cout << "[World] Connection service done, players="
                      << m_netManager->getPlayers().size() << std::endl;
        }

        // 客户端：快速消化服务端推送的初始地形数据
        if (m_netMode == NetMode::Join) {
            std::cout << "[World] Receiving initial chunk data..." << std::endl;
            auto start = std::chrono::steady_clock::now();
            auto lastLog = start;
            int prevLoaded = 0;
            while (std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start).count() < 15000) {
                m_netManager->update();
                m_chunkManager->update(m_player->getCamera());

                int loaded = m_chunkManager->getLoadedChunkCount();

                // 每 1 秒输出一次加载进度
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - lastLog).count() > 1000) {
                    auto pos = m_chunkManager->getLoadedChunkPositions();
                    std::cout << "[World] Init progress: loaded=" << loaded
                              << ", inflight=" << m_chunkManager->getInFlightCount()
                              << ", elapsed="
                              << std::chrono::duration_cast<std::chrono::milliseconds>(
                                     now - start).count() << "ms";
                    if (!pos.empty()) {
                        // 输出已加载区块范围
                        int minX = pos[0].x, maxX = pos[0].x;
                        int minZ = pos[0].y, maxZ = pos[0].y;
                        for (auto& p : pos) {
                            if (p.x < minX) minX = p.x;
                            if (p.x > maxX) maxX = p.x;
                            if (p.y < minZ) minZ = p.y;
                            if (p.y > maxZ) maxZ = p.y;
                        }
                        std::cout << " range=[" << minX << "," << minZ
                                  << "] to [" << maxX << "," << maxZ << "]";
                    }
                    std::cout << std::endl;
                    lastLog = now;
                    prevLoaded = loaded;
                }

                // 玩家周围 5x5 = 25 chunks 全部 loaded 即可开始
                if (loaded >= 25) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            std::cout << "[World] Initial sync done, loaded chunks="
                      << m_chunkManager->getLoadedChunkCount()
                      << ", inflight=" << m_chunkManager->getInFlightCount() << std::endl;
        }
    }

    // 读档：加载玩家位置（存档已有出生点，无需重新计算）
    if (!m_isNewWorld) {
        PlayerSaveData pd;
        if (m_saveManager->loadPlayerState(pd)) {
            m_player->loadSaveData(pd);
        }
        m_spawnFound = true;
    }

    // Join 模式：使用服务端回传的世界名替换本地 session 名
    if (m_netManager && m_netMode == NetMode::Join) {
        const std::string& svrName = m_netManager->getWorldName();
        if (!svrName.empty()) m_worldName = svrName;
    }

    // 确定本地皮肤路径
    std::string localSkinPath = "assert/mode/player/wide/steve.png";
    m_localSkinName = "Steve";
    if (m_netManager && m_netMode == NetMode::Join) {
        m_localSkinName = m_netManager->getLocalSkinName();
        if (!m_localSkinName.empty()) {
            std::string p = SkinManager::instance().getSkinPath(m_localSkinName);
            if (!p.empty()) localSkinPath = p;
        }
    }

    // 初始化玩家
    m_player->initialize(renderSystem.getUIManager(), localSkinPath);

    // Host：恢复本地玩家上次的背包（若有独立存档文件；ItemRegistry 此时已加载）
    if (m_netMode == NetMode::Host && m_netManager) {
        if (auto* lp = m_netManager->getLocalPlayer()) {
            InventoryData inv;
            if (loadPlayerInventoryFile(lp->playerName, inv)) applyNetInventory(inv);
        }
    }

    // 掉落物管理器（单机本地实体），注入给玩家用于 F 丢弃 / 破坏掉落
    m_droppedItems = std::make_unique<DroppedItemManager>();
    m_player->setDroppedItemManager(m_droppedItems.get());

    // 掉落物网络同步接线（Host 权威广播 / Join 客户端渲染 + 请求生成）
    if (m_netManager) setupDroppedItemNetworking();

    // 设置方块选择回调
    m_player->setOnBlockSelectedCallback([&renderSystem](const glm::ivec3& blockPos) {
        renderSystem.setSelectedBlock(blockPos);
    });

    m_player->setOnBlockClearedCallback([&renderSystem]() {
        renderSystem.clearSelectedBlock();
    });

    // 主循环
    while (!glfwWindowShouldClose(m_window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        static float lastFrame = currentFrame; // 初始化为当前时间，避免第一帧deltaTime过大
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // 限制deltaTime，避免卡顿导致物理计算错误
        if (deltaTime > 0.1f) {
            deltaTime = 0.1f; // 最大100ms
        }

        // 热重载：帧首检查被监视的配置文件（held_display.json / runtime_config.json）
        // 是否被改动，改了就自动重读（内部按 0.25s 节流）。这样调参不必重启。
        HotReload::instance().poll();

        // o/p 长按调太阳速度（持续按住每帧轮询，越按越快）
        updateSunSpeedKeys(deltaTime);

        // 显示FPS
        showFPS();

        // 网络：帧首 poll + dispatch
        if (m_netManager) {
            m_netManager->update();
        }

        // 更新玩家状态（包括移动、交互等）
        m_player->update(deltaTime, *m_chunkManager, renderSystem);
        auto camera = m_player->getCamera();

        // 网络：同步本地玩家位置 / 运动状态 / 挥手到 NetState
        if (m_netManager && (m_netMode == NetMode::Host || m_netMode == NetMode::Join)) {
            auto* netState = m_netManager->getLocalNetState();
            if (netState) {
                netState->setPosition(m_player->getPosition());
                netState->setLook(camera->Yaw, camera->Pitch);

                // 运动状态（供远程端步态动画复现走/跑/蹲）
                uint8_t flags = 0;
                if (m_player->isOnGround())  flags |= PlayerFlagBits::ON_GROUND;
                if (m_player->isCrouching()) flags |= PlayerFlagBits::CROUCHING;
                if (m_player->isRunning())   flags |= PlayerFlagBits::RUNNING;
                netState->setMotion(m_player->getVelocity(), flags);

                // 挥手事件计数：本地动画器每挥一次自增，低字节变化才发（可靠通道）
                uint8_t swing = (uint8_t)m_player->getAnimator().getSwingCount();
                if (swing != netState->m_swingCounter) {
                    netState->setSwingCounter(swing);
                }
            }

            // 世界时间同步：Host 把权威时间镜像进 WorldState；Join 读 WorldState 灌回 RS。
            if (m_renderSystem) {
                if (auto* ws = m_netManager->getWorldState()) {
                    if (m_netMode == NetMode::Host) {
                        ws->mirror(m_renderSystem->getWorldTime(),
                                   m_renderSystem->getTimeScale(),
                                   m_renderSystem->isSunMoving());
                    } else if (m_netMode == NetMode::Join) {
                        m_renderSystem->setWorldTime(ws->timeHours());
                        m_renderSystem->setSunMoving(ws->sunMoving());
                    }
                }
            }

            // 背包同步：本地背包/手持物 → netState（客户端上报服务端持久化）
            syncPlayerNetInventory();

            // 客户端：收到服务端恢复的背包则应用（消费一次）
            if (m_netMode == NetMode::Join) {
                InventoryData restored;
                if (m_netManager->takeRestoredInventory(restored)) {
                    applyNetInventory(restored);
                }
            }
        }

        // 阶段 B：服务端每帧把所有玩家（Host + 远程）的位置作为"数据相关"加载中心，
        // 灌给 ChunkManager（决定生成/落盘/卸载/推送；mesh 仍只看本机相机）。
        // 必须在 chunkManager->update() 之前。客户端/单机不灌（内部退化为本机相机）。
        if (m_netManager && m_netMode == NetMode::Host) {
            std::vector<ChunkManager::LoadCenter> centers;
            int hostRadius = m_chunkManager->getRenderRadius();
            for (auto& [id, p] : m_netManager->getPlayers()) {
                bool isLocal = (id == m_netManager->getLocalPlayerId());
                glm::vec3 pos = isLocal ? m_player->getPosition() : p->getRenderPosition();
                int radius = isLocal ? hostRadius : p->renderRadius;
                if (radius <= 0) radius = hostRadius;  // 远程未上报则用 Host 半径兜底
                centers.push_back({
                    glm::ivec2(
                        (int)std::floor(pos.x / (float)ChunkConstants::CHUNK_WIDTH),
                        (int)std::floor(pos.z / (float)ChunkConstants::CHUNK_DEPTH)),
                    radius });
            }
            m_chunkManager->setLoadCenters(std::move(centers));
        }

        // 更新区块管理器（根据玩家位置更新可见区块）
        m_chunkManager->update(camera);

        // 更新掉落物（物理 + 拾取）。背包开启时仍继续，使「光标腾出一格后可继续吸附」
        // 的交互（需求 4）成立，且与 MC 单机一致。
        if (m_droppedItems) {
            m_droppedItems->update(deltaTime, *m_chunkManager, *m_player);
        }

        // Host：定时批量把掉落物位置/数量同步给客户端（10Hz，不可靠通道）
        if (m_netMode == NetMode::Host && m_netManager && m_droppedItems) {
            m_droppedSyncTimer += deltaTime;
            if (m_droppedSyncTimer >= 0.1f) {
                m_droppedSyncTimer = 0.0f;
                broadcastDroppedSync();
            }
        }

        // 背包交互（长按脱离到光标 / 光标图标跟随）
        if (m_inventoryOpen) {
            updateInventory(deltaTime);
        }

        // 新世界：等待区块(0,0)生成后，计算出生点（柱顶最高非空气方块上方）
        if (!m_spawnFound && m_isNewWorld) {
            Chunk* spawnChunk = m_chunkManager->getChunkAnyState(glm::ivec2(0, 0));
            if (spawnChunk && spawnChunk->isMeshReady()) {
                bool placed = false;
                for (int y = ChunkConstants::CHUNK_HEIGHT - 1; y >= 0; --y) {
                    if (spawnChunk->getBlock(0, y, 0).type() != BLOCK_AIR) {
                        float groundTop = (float)(y + 1);
                        float playerY = groundTop + PhysicsConstants::PLAYER_HEIGHT_STANDING * 0.5f;
                        m_player->setPosition(glm::vec3(0.5f, playerY, 0.5f));
                        m_spawnFound = true;
                        placed = true;
                        std::cout << "[World] 出生点: y=" << playerY
                                  << " (方块顶=" << groundTop << ")" << std::endl;
                        break;
                    }
                }
                if (!placed) {
                    // 全空气柱（不太可能），用默认高度
                    m_player->setPosition(glm::vec3(0.5f,
                        (float)ChunkConstants::CHUNK_HEIGHT * 0.5f, 0.5f));
                    m_spawnFound = true;
                }
            }
        }

        // 获取视图和投影矩阵
        glm::mat4 view = camera->GetViewMatrix();
        glm::mat4 projection = glm::perspective(glm::radians(45.0f),
            (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 1000.0f);

        // 渲染帧
        renderSystem.render(*m_chunkManager, view, projection, camera, deltaTime, m_player.get(),
                            m_netManager.get(), m_droppedItems.get());

        // 调试面板（F1）：场景渲染之后、上屏之前叠加绘制。隐藏时内部直接返回（近零开销）。
        if (m_debugUI) {
            ItemStack* held = m_player ? m_player->getSelectedStack() : nullptr;
            const ItemDefinition* heldDef = (held && !held->empty()) ? held->def : nullptr;
            int fbw = 0, fbh = 0;
            glfwGetFramebufferSize(m_window, &fbw, &fbh);
            m_debugUI->draw(heldDef, fbw, fbh);
        }

        {
            PROFILE_SCOPE("swapBuffers");
            glfwSwapBuffers(m_window);
        }
        glfwPollEvents();

        // 每秒打印一次 profiler（如 RuntimeConfig 启用）
        Profiler::frame();
    }

    // 关闭调试面板：必须在窗口 / GL 上下文销毁前（删除 ImGui 的 GL 对象）
    if (m_debugUI) {
        m_debugUI->shutdown();
        m_debugUI.reset();
    }

    // 退出保存
    if (m_saveManager && m_saveManager->isWorldOpen()) {
        PlayerSaveData pd = m_player->getSaveData();
        m_saveManager->savePlayerState(pd);
        // Host：把本地玩家背包也存进独立文件（必须在 leave() 前——leave 会清空玩家表，
        // 之后 getLocalPlayer() 返回 nullptr）。
        if (m_netMode == NetMode::Host && m_netManager) {
            if (auto* lp = m_netManager->getLocalPlayer()) {
                InventoryData inv;
                buildLocalInventoryData(inv);
                savePlayerInventoryFile(lp->playerName, inv);
            }
        }
        // 关服/断网：趁存档仍打开（isWorldOpen==true）持久化所有在线远程玩家背包，再断开。
        // 放在 closeWorld() 之前，否则 persist 会因存档已关而静默失败；
        // 也放在析构之前，避免析构期成员逆序销毁导致 saveManager 先于 netManager 释放。
        if (m_netManager) m_netManager->leave();
        m_chunkManager->saveAllDirtyChunks();
        m_saveManager->closeWorld();
    } else if (m_netManager) {
        // Join 等无本地存档模式：仍需正常断网（无远程背包要持久化）。
        m_netManager->leave();
    }

    return 0;
}

// 背包开合：切换光标模式、UI 可见性、冻结/恢复玩家操作
void World::toggleInventory() {
    if (!m_player) return;
    m_inventoryOpen = !m_inventoryOpen;
    auto inv = m_player->getInventoryUI();
    auto hb  = m_player->getHotbar();
    if (m_inventoryOpen) {
        m_player->clearInput();                   // 清掉按住的移动键，避免冻结期间持续生效
        m_player->syncHotbarUI();                 // 打开时刷新面板数据
        if (inv) inv->visible = true;
        if (hb)  hb->visible = false;             // 面板自带 hotbar 行，隐藏底部 hotbar
        if (m_renderSystem) {
            if (auto ch = m_renderSystem->getUIManager().getComponent("crosshair"))
                ch->visible = false;              // 背包开时隐藏准星
        }
        glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        glfwSetCursorPos(m_window, SCR_WIDTH * 0.5, SCR_HEIGHT * 0.5);
    } else {
        // 关背包：光标携带的物品尽量塞回背包，塞不下的丢到世界
        m_player->returnCursorToInventory();
        m_invPressSlot = -1; m_invDetached = false; m_invLmbHeld = false;
        if (inv) { inv->setCursorStack(m_player->getCursorStack()); inv->visible = false; }
        if (hb)  hb->visible = true;
        if (m_renderSystem) {
            if (auto ch = m_renderSystem->getUIManager().getComponent("crosshair"))
                ch->visible = true;               // 恢复准星
        }
        glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        m_player->resetMouseLook();               // 避免视角跳变
    }
}

// 背包每帧更新：长按计时 → 越过阈值把物品脱离到光标；同步光标图标内容 / 位置
void World::updateInventory(float deltaTime) {
    if (!m_inventoryOpen || !m_player) return;
    auto inv = m_player->getInventoryUI();
    if (!inv) return;

    if (m_invLmbHeld && !m_invDetached && !m_player->hasCursorStack() &&
        m_invPressSlot >= 0) {
        m_invPressTime += deltaTime;
        const auto& invData = m_player->getInventory();
        bool slotHasItem = (m_invPressSlot < (int)invData.size()) &&
                           !invData[m_invPressSlot].empty();
        if (slotHasItem && m_invPressTime >= LONG_PRESS_SEC) {
            m_player->pickUpToCursor(m_invPressSlot); // 脱离到光标（源格清空）
            m_invDetached = true;
        }
    }

    inv->setCursorStack(m_player->getCursorStack());
    inv->updateCursorPos(m_lastMouseUI);
}

void World::processMouse(double xpos, double ypos) {
    // 调试面板可见：不转动视角（ImGui 通过链式回调仍收到鼠标位置用于操作面板）
    if (m_debugUI && m_debugUI->isVisible()) return;
    if (m_inventoryOpen) {
        // 背包开：记录鼠标 UI 坐标（y 翻转），驱动光标携带图标跟随
        m_lastMouseUI = glm::vec2((float)xpos, (float)(SCR_HEIGHT - ypos));
        if (m_player) {
            auto inv = m_player->getInventoryUI();
            if (inv) inv->updateCursorPos(m_lastMouseUI);
        }
        return;
    }
    if (m_player) {
        m_player->processMouseMovement(xpos, ypos);
    }
}

void World::processMouseButton(int button, int action) {
    // 调试面板可见：点击交给 ImGui（链式回调），不触发游戏破坏/放置
    if (m_debugUI && m_debugUI->isVisible()) return;
    if (m_inventoryOpen) {
        if (m_player && button == GLFW_MOUSE_BUTTON_LEFT) {
            auto inv = m_player->getInventoryUI();
            if (inv) {
                double mx, my; glfwGetCursorPos(m_window, &mx, &my);
                glm::vec2 ui((float)mx, (float)(SCR_HEIGHT - my));
                m_lastMouseUI = ui;
                int slot = inv->slotAt(ui);
                if (action == GLFW_PRESS) {
                    m_invLmbHeld = true;
                    if (m_player->hasCursorStack()) {
                        // 光标已携带物品：点击落下 / 拖出面板丢弃
                        if (slot >= 0)                    m_player->placeCursorToSlot(slot);
                        else if (!inv->panelContains(ui)) m_player->dropCursorStack();
                        m_invPressSlot = -1; m_invDetached = false;
                    } else {
                        // 光标空：记录按压格，等待长按脱离（或快速点击时在释放里即时拿取）
                        m_invPressSlot = slot;
                        m_invPressTime = 0.0f;
                        m_invDetached  = false;
                    }
                } else if (action == GLFW_RELEASE) {
                    m_invLmbHeld = false;
                    if (m_invDetached && m_player->hasCursorStack()) {
                        // 本次长按已脱离到光标：释放时落下 / 拖出丢弃 / 面板内空处放回源格
                        if (slot >= 0)                    m_player->placeCursorToSlot(slot);
                        else if (!inv->panelContains(ui)) m_player->dropCursorStack();
                        else if (m_invPressSlot >= 0)     m_player->placeCursorToSlot(m_invPressSlot);
                    } else if (!m_player->hasCursorStack() && m_invPressSlot >= 0 &&
                               m_invPressSlot == slot) {
                        // 快速点击（未达长按阈值）：即时把整栈拿到光标，持续携带
                        m_player->pickUpToCursor(m_invPressSlot);
                    }
                    m_invPressSlot = -1; m_invDetached = false;
                }
                inv->setCursorStack(m_player->getCursorStack());
                inv->updateCursorPos(ui);
            }
        }
        return;
    }
    if (m_player) {
        m_player->processMouseButton(button, action);
    }
}

void World::processKey(int key, int action) {
    // F1：开合调试面板。可见时释放光标操作面板；关闭时若背包也没开则重新锁定光标，
    // 并重置鼠标视角首帧标记避免视角跳变。
    if (key == GLFW_KEY_F1 && action == GLFW_PRESS) {
        if (m_debugUI) {
            m_debugUI->toggle(m_window, !m_inventoryOpen);
            if (!m_debugUI->isVisible() && m_player) m_player->resetMouseLook();
        }
        return;
    }
    // 调试面板打开时也放行 F3：方便一边开面板一边切第一/第三人称对照调 TRS。
    if (key == GLFW_KEY_F3 && action == GLFW_PRESS && m_debugUI && m_debugUI->isVisible()) {
        if (m_player) m_player->toggleThirdPerson();
        return;
    }
    // 调试面板可见时，其余键盘不转发给游戏（ImGui 仍通过链式回调收到输入，可在面板里打字）。
    if (m_debugUI && m_debugUI->isVisible()) return;

    // ESC：背包开着则先关背包，否则退出
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        if (m_inventoryOpen) { toggleInventory(); return; }
        glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        return;
    }
    // E：普通模式下开合背包（观察者模式 E 用于调速，保持原行为）
    if (key == GLFW_KEY_E && action == GLFW_PRESS && m_player &&
        m_player->getMoveMode() == Player::MoveMode::Normal) {
        toggleInventory();
        return;
    }
    // 背包开启时：F 丢弃鼠标悬停格的一个物品；其余按键冻结（不转发给玩家）
    if (m_inventoryOpen) {
        if (key == GLFW_KEY_F && action == GLFW_PRESS && m_player) {
            auto inv = m_player->getInventoryUI();
            if (inv) {
                double mx, my; glfwGetCursorPos(m_window, &mx, &my);
                int slot = inv->slotAt(glm::vec2((float)mx, (float)(SCR_HEIGHT - my)));
                if (slot >= 0) m_player->dropFromSlot(slot, 1);
            }
        }
        return;
    }

    // 全局按键处理
    if (key == GLFW_KEY_G && action == GLFW_PRESS) {
        if (m_renderSystem) {
            m_renderSystem->toggleWeather();
        }
    }
    if (key == GLFW_KEY_F3 && action == GLFW_PRESS) {
        if (m_player) {
            m_player->toggleThirdPerson();
        }
    }
    // L：时间是否流动的开关（关 = 冻结时间）
    if (key == GLFW_KEY_L && action == GLFW_PRESS) {
        if (isTimeNetClient()) {
            // 客户端：发命令给服务端，取当前 WorldState 状态取反
            auto* ws = m_netManager->getWorldState();
            bool nowMoving = ws ? ws->sunMoving() : true;
            m_netManager->sendWorldCmd(WorldCmdType::SetMoving, nowMoving ? 0.0f : 1.0f);
        } else if (m_renderSystem) {
            m_renderSystem->toggleSunMoving();
            std::cout << "[时间] " << (m_renderSystem->isSunMoving() ? "继续流动" : "已暂停")
                      << "（当前 " << formatWorldTime(m_renderSystem->getWorldTime()) << "）" << std::endl;
        }
    }
    // 8/9/0：预设时间 早上7点 / 正午12点 / 晚上7点，并顺便暂停时间（方便定格观察光照）
    if (action == GLFW_PRESS && (key == GLFW_KEY_8 || key == GLFW_KEY_9 || key == GLFW_KEY_0)) {
        float hour = (key == GLFW_KEY_8) ? 7.0f : (key == GLFW_KEY_9) ? 12.0f : 19.0f;
        if (isTimeNetClient()) {
            m_netManager->sendWorldCmd(WorldCmdType::SetTime, hour);
            m_netManager->sendWorldCmd(WorldCmdType::SetMoving, 0.0f);  // 预设顺便暂停
        } else if (m_renderSystem) {
            m_renderSystem->setWorldTime(hour);
            m_renderSystem->setSunMoving(false);   // 预设顺便暂停
            std::cout << "[时间] 跳到 " << formatWorldTime(hour) << "（已暂停）" << std::endl;
        }
    }
    // O/P 松开时输出最终的时间流逝速度（按住期间连续调整，松开报告结果）
    if (action == GLFW_RELEASE && (key == GLFW_KEY_O || key == GLFW_KEY_P)) {
        if (m_renderSystem) {
            printTimeFlowSpeed();
        }
    }

    // 转发给玩家处理
    if (m_player) {
        m_player->processKey(key, action);
    }
}

// o/p 调时间比例（固定灵敏度）。主循环每帧轮询持续按住状态。
// o = 加快时间流逝（time_scale +），p = 减慢（time_scale -，可越过 0 变负 → 时间倒流）。
// 比例上限由 RenderSystem::adjustTimeScale 内部夹紧（kTimeScaleMax）。
void World::updateSunSpeedKeys(float deltaTime) {
    if (!m_renderSystem || !m_window) return;

    bool oHeld = glfwGetKey(m_window, GLFW_KEY_O) == GLFW_PRESS;
    bool pHeld = glfwGetKey(m_window, GLFW_KEY_P) == GLFW_PRESS;

    // 只允许单方向：同时按或都不按 → 不调
    int dir = 0;
    if (oHeld && !pHeld) dir = +1;
    else if (pHeld && !oHeld) dir = -1;
    if (dir == 0) return;

    // 固定灵敏度：每现实秒把 time_scale 调整 kRate（游戏小时/现实秒 每秒）
    const float kRate = 0.4f;
    float delta = (float)dir * kRate * deltaTime;
    if (isTimeNetClient()) {
        // 客户端：高频增量走不可靠通道（丢一两个无所谓，服务端复制回来会纠正）
        m_netManager->sendWorldCmd(WorldCmdType::AdjustScale, delta, false);
    } else {
        m_renderSystem->adjustTimeScale(delta);
    }
}

// 客户端(Join)且有网络管理器：时间由网络驱动，按键改发 WORLD_CMD 而非本地应用。
bool World::isTimeNetClient() const {
    return m_netManager && m_netMode == NetMode::Join;
}

// Host：应用客户端发来的世界时间命令到本地权威 RenderSystem（下一帧 mirror 复制回所有端）。
void World::applyTimeCommand(WorldCmdType t, float p) {
    if (!m_renderSystem) return;
    switch (t) {
    case WorldCmdType::SetTime:     m_renderSystem->setWorldTime(p);          break;
    case WorldCmdType::AdjustScale: m_renderSystem->adjustTimeScale(p);       break;
    case WorldCmdType::SetMoving:   m_renderSystem->setSunMoving(p != 0.0f);  break;
    }
}

// ============================================================================
// 背包同步 / 持久化（需求 2）
// ============================================================================

std::string World::localHeldItemId() const {
    if (!m_player) return std::string();
    const ItemStack* s = m_player->getSelectedStack();
    return (s && s->def) ? s->def->id : std::string();
}

void World::buildLocalInventoryData(InventoryData& out) const {
    out.slots.clear();
    if (!m_player) return;
    const auto& inv = m_player->getInventory();
    out.slots.resize(inv.size());
    for (size_t i = 0; i < inv.size(); ++i) {
        const ItemStack& st = inv[i];
        if (st.def && st.count > 0) {
            out.slots[i].id = st.def->id;
            out.slots[i].count = (uint16_t)st.count;
            out.slots[i].durability = (uint16_t)st.durability;
        }
        // 否则留空（id="" count=0）
    }
}

void World::applyNetInventory(const InventoryData& inv) {
    if (!m_player) return;
    auto& dst = m_player->getInventory();
    for (size_t i = 0; i < dst.size(); ++i) {
        if (i >= inv.slots.size()) { dst[i].clear(); continue; }
        const auto& sl = inv.slots[i];
        if (sl.id.empty() || sl.count == 0) { dst[i].clear(); continue; }
        const ItemDefinition* def = ItemRegistry::instance().get(sl.id);
        if (!def) { dst[i].clear(); continue; }
        ItemStack st(def, (int)sl.count);
        st.durability = (int)sl.durability;
        dst[i] = st;
    }
    m_player->syncHotbarUI();
}

// 每帧：本地背包/手持物 → netState。手持物两端都广播；整背包仅客户端上报服务端持久化。
void World::syncPlayerNetInventory() {
    if (!m_netManager) return;
    auto* netState = m_netManager->getLocalNetState();
    if (!netState) return;

    std::string held = localHeldItemId();
    if (held != netState->m_heldItemId) netState->setHeldItem(held);

    if (m_netMode == NetMode::Join) {
        InventoryData inv;
        buildLocalInventoryData(inv);
        if (inv != netState->m_inventory) netState->setInventory(inv);
    }
}

// 玩家名 → 合法文件名（LAN 名简单，稳妥起见把非字母数字替换成 '_'）
static std::string sanitizePlayerName(const std::string& n) {
    std::string r;
    for (char c : n) r += (std::isalnum((unsigned char)c) ? c : '_');
    if (r.empty()) r = "player";
    return r;
}

void World::savePlayerInventoryFile(const std::string& name, const InventoryData& inv) {
    if (!m_saveManager || !m_saveManager->isWorldOpen()) return;
    std::string dir = m_saveManager->getSavesRoot() + "/" +
                      m_saveManager->getWorldName() + "/players";
    ChunkSaveManager::makeDir(dir);
    MemoryStream s;
    NetTypeSerializer<InventoryData>::write(s, inv);
    std::ofstream f(dir + "/" + sanitizePlayerName(name) + ".inv", std::ios::binary);
    if (f) f.write((const char*)s.data(), (std::streamsize)s.size());
}

bool World::loadPlayerInventoryFile(const std::string& name, InventoryData& out) {
    if (!m_saveManager || !m_saveManager->isWorldOpen()) return false;
    std::string path = m_saveManager->getSavesRoot() + "/" +
                       m_saveManager->getWorldName() + "/players/" +
                       sanitizePlayerName(name) + ".inv";
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
    if (buf.empty()) return false;
    MemoryStream s;
    s.writeBytes(buf.data(), buf.size());
    NetTypeSerializer<InventoryData>::read(s, out);
    return true;
}

// ============================================================================
// 掉落物网络同步（需求 3）
// ============================================================================

void World::setupDroppedItemNetworking() {
    if (!m_netManager || !m_droppedItems) return;

    // 收发游戏消息（掉落物）的统一入口
    m_netManager->setGameMessageHandler(
        [this](NetMsgType t, MemoryStream& p) { handleGameMessage(t, p); });

    if (m_netMode == NetMode::Host) {
        // 服务端权威：生成/销毁广播给所有客户端
        m_droppedItems->setServerCallbacks(
            [this](const DroppedItem& it) {
                MemoryStream s;
                s.writePod((uint8_t)NetObjType::DroppedItem);
                s.writePod(it.netId);
                s.writeString(it.stack.def ? it.stack.def->id : std::string());
                s.writePod((uint16_t)it.stack.count);
                s.writePod((uint16_t)it.stack.durability);
                s.writePod(it.pos.x); s.writePod(it.pos.y); s.writePod(it.pos.z);
                m_netManager->broadcast(NetMsgType::SPAWN_OBJECT, s, true);
            },
            [this](uint16_t netId) {
                MemoryStream s;
                s.writePod(netId);
                m_netManager->broadcast(NetMsgType::DESTROY_OBJECT, s, true);
            });
    } else if (m_netMode == NetMode::Join) {
        // 客户端：不本地模拟，丢弃/破坏改为向服务端请求生成
        m_droppedItems->setClientMode(true);
        m_droppedItems->setDropRequestCallback(
            [this](const ItemStack& st, const glm::vec3& pos, const glm::vec3& vel) {
                MemoryStream s;
                s.writeString(st.def ? st.def->id : std::string());
                s.writePod((uint16_t)st.count);
                s.writePod((uint16_t)st.durability);
                s.writePod(pos.x); s.writePod(pos.y); s.writePod(pos.z);
                s.writePod(vel.x); s.writePod(vel.y); s.writePod(vel.z);
                m_netManager->sendToServer(NetMsgType::DROP_REQUEST, s, true);
            });
    }
}

void World::handleGameMessage(NetMsgType type, MemoryStream& payload) {
    if (!m_droppedItems) return;

    switch (type) {
    case NetMsgType::SPAWN_OBJECT: {  // 客户端：生成掉落物
        uint8_t tag = payload.readPod<uint8_t>();
        if ((NetObjType)tag != NetObjType::DroppedItem) return;
        uint16_t netId = payload.readPod<uint16_t>();
        std::string id = payload.readString();
        uint16_t count = payload.readPod<uint16_t>();
        uint16_t dur   = payload.readPod<uint16_t>();
        glm::vec3 pos;
        pos.x = payload.readPod<float>(); pos.y = payload.readPod<float>(); pos.z = payload.readPod<float>();
        const ItemDefinition* def = ItemRegistry::instance().get(id);
        if (!def) return;
        ItemStack st(def, (int)count);
        st.durability = (int)dur;
        m_droppedItems->netSpawn(netId, st, pos);
        break;
    }
    case NetMsgType::DESTROY_OBJECT: {  // 客户端：销毁掉落物
        uint16_t netId = payload.readPod<uint16_t>();
        m_droppedItems->netDespawn(netId);
        break;
    }
    case NetMsgType::DROPPED_SYNC: {  // 客户端：批量位置/数量
        uint16_t n = payload.readPod<uint16_t>();
        for (uint16_t i = 0; i < n; ++i) {
            uint16_t netId = payload.readPod<uint16_t>();
            glm::vec3 pos;
            pos.x = payload.readPod<float>(); pos.y = payload.readPod<float>(); pos.z = payload.readPod<float>();
            uint16_t count = payload.readPod<uint16_t>();
            m_droppedItems->netApply(netId, pos, (int)count);
        }
        break;
    }
    case NetMsgType::DROP_REQUEST: {  // 服务端：客户端请求生成，权威落地
        std::string id = payload.readString();
        uint16_t count = payload.readPod<uint16_t>();
        uint16_t dur   = payload.readPod<uint16_t>();
        glm::vec3 pos, vel;
        pos.x = payload.readPod<float>(); pos.y = payload.readPod<float>(); pos.z = payload.readPod<float>();
        vel.x = payload.readPod<float>(); vel.y = payload.readPod<float>(); vel.z = payload.readPod<float>();
        const ItemDefinition* def = ItemRegistry::instance().get(id);
        if (!def) return;
        ItemStack st(def, (int)count);
        st.durability = (int)dur;
        m_droppedItems->spawn(st, pos, vel);  // 服务端权威生成 → onSpawn 广播
        break;
    }
    default: break;
    }
}

void World::broadcastDroppedSync() {
    if (!m_netManager || m_netMode != NetMode::Host || !m_droppedItems) return;
    const auto& items = m_droppedItems->items();
    MemoryStream s;
    s.writePod((uint16_t)items.size());
    for (const auto& it : items) {
        s.writePod(it.netId);
        s.writePod(it.pos.x); s.writePod(it.pos.y); s.writePod(it.pos.z);
        s.writePod((uint16_t)it.stack.count);
    }
    m_netManager->broadcast(NetMsgType::DROPPED_SYNC, s, false);  // 高频走不可靠
}

// 把 0-24 浮点世界时间格式化成 "HH:MM"。
std::string World::formatWorldTime(float hour) {
    hour = std::fmod(hour, 24.0f);
    if (hour < 0.0f) hour += 24.0f;
    int h = (int)hour;
    int m = (int)((hour - h) * 60.0f);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    return std::string(buf);
}

// 输出当前时间流逝速度，用现实世界单位表示（"X 游戏小时 / 现实秒"，并附"一昼夜约 Y 现实秒"）。
void World::printTimeFlowSpeed() {
    if (!m_renderSystem) return;
    float ts = m_renderSystem->getTimeScale();   // 游戏小时 / 现实秒
    std::cout << std::fixed << std::setprecision(3);
    if (std::fabs(ts) < 1e-4f) {
        std::cout << "[时间] 流速 ≈ 0（几乎静止）" << std::endl;
        return;
    }
    float secPerDay = 24.0f / std::fabs(ts);     // 一昼夜需要多少现实秒
    std::cout << "[时间] 流速 " << ts << " 游戏小时/现实秒"
              << (ts < 0 ? "（倒流）" : "")
              << "，一昼夜约 " << secPerDay << " 现实秒" << std::endl;
}

void World::processMouseScroll(double xoffset, double yoffset) {
    // 调试面板可见：滚轮交给 ImGui，不切换 hotbar
    if (m_debugUI && m_debugUI->isVisible()) return;
    if (m_player) {
        m_player->processMouseScroll(xoffset, yoffset);
    }
}

// 静态回调函数实现
void World::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void World::mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    World* world = static_cast<World*>(glfwGetWindowUserPointer(window));
    if (world) {
        world->processMouse(xpos, ypos);
    }
}

void World::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    World* world = static_cast<World*>(glfwGetWindowUserPointer(window));
    if (world) {
        world->processMouseButton(button, action);
    }
}

void World::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    World* world = static_cast<World*>(glfwGetWindowUserPointer(window));
    if (world) {
        world->processKey(key, action);
    }
}

void World::mouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    World* world = static_cast<World*>(glfwGetWindowUserPointer(window));
    if (world) {
        world->processMouseScroll(xoffset, yoffset);
    }
}

void World::showFPS() {
    static int frameCount = 0;
    static double fpsUpdateInterval = 0.5; // 每0.5秒更新
    static double fpsLastUpdate = glfwGetTime();

    double currentTime = glfwGetTime();
    frameCount++;
    if (currentTime - fpsLastUpdate >= fpsUpdateInterval) {
        double fps = frameCount / (currentTime - fpsLastUpdate);
        std::stringstream ss;
        ss << std::fixed << std::setprecision(3);
        if (m_netMode == NetMode::None) {
            ss << "Local - " << m_worldName << " - FPS: " << fps;
        } else if (m_netMode == NetMode::Host) {
            ss << "Host - " << m_worldName << " - Steve - FPS: " << fps;
        } else {
            uint16_t cid = m_netManager ? m_netManager->getLocalPlayerId() : 0;
            ss << "Client_" << cid << " - " << m_worldName << " - " << m_localSkinName << " - FPS: " << fps;
        }
        glfwSetWindowTitle(m_window, ss.str().c_str());

        // 重置计数器和时间
        frameCount = 0;
        fpsLastUpdate = currentTime;
    }
}

void World::RenderQuad() {
    static GLuint quadVAO = 0;
    static GLuint quadVBO;
    if (quadVAO == 0) {
        GLfloat quadVertices[] = {
            // Positions        // Texture Coords
            -1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
            1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        };
        // Setup plane VAO
        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (GLvoid*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
    }

    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}