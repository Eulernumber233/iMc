#include "CliManager.h"
#include "save/ChunkSaveManager.h"
#include "Data.h"
#include "RuntimeConfig.h"
#include "Shader.h"
#include "TextureMgr.h"
#include "item/ItemRegistry.h"
#include <iostream>
#include <random>
#include <unordered_set>
#include <windows.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "stb_image.h"

namespace {
    constexpr uint16_t DEFAULT_PORT = 60011;

    void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
        glViewport(0, 0, width, height);
    }

    bool setWindowIcon(GLFWwindow* window, const char* iconPath) {
        int width, height, channels;
        unsigned char* pixels = stbi_load(iconPath, &width, &height, &channels, 4);
        if (!pixels) {
            std::cerr << "Failed to load icon: " << iconPath << std::endl;
            return false;
        }
        GLFWimage icon;
        icon.width = width;
        icon.height = height;
        icon.pixels = pixels;
        glfwSetWindowIcon(window, 1, &icon);
        stbi_image_free(pixels);
        return true;
    }

    // FNV-1a 64-bit
    uint64_t hashStringToSeed(const std::string& s) {
        uint64_t h = 14695981039346656037ULL;
        for (char c : s) {
            h ^= (uint64_t)(unsigned char)c;
            h *= 1099511628211ULL;
        }
        return h;
    }
}

// ============================================================================
// Cmdline parsing
// ============================================================================

void CliManager::parseCmdline(int argc, char* argv[]) {
    m_cmdline = CmdlineArgs{};
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--host") {
            m_cmdline.netMode = NetMode::Host;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                m_cmdline.port = static_cast<uint16_t>(std::atoi(argv[++i]));
            } else {
                m_cmdline.port = DEFAULT_PORT;
            }
        } else if (arg == "--join") {
            m_cmdline.netMode = NetMode::Join;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                m_cmdline.joinAddr = argv[++i];
            }
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                m_cmdline.port = static_cast<uint16_t>(std::atoi(argv[++i]));
            } else {
                m_cmdline.port = DEFAULT_PORT;
            }
        } else if (arg == "--world") {
            if (i + 1 < argc) m_cmdline.worldName = argv[++i];
        } else if (arg == "--winpos") {
            if (i + 2 < argc) {
                m_cmdline.winPosX = std::atoi(argv[++i]);
                m_cmdline.winPosY = std::atoi(argv[++i]);
            }
        } else if (arg == "--rebuild-shaders") {
            // 强制重编着色器：忽略并删除磁盘缓存，从源码重编后重写缓存（覆盖配置文件）
            Shader::setForceRecompile(true);
        } else if (arg == "--no-rebuild-shaders") {
            // 强制走磁盘缓存查找（覆盖配置文件里的 force_recompile_shaders=true）
            Shader::setForceRecompile(false);
        }
    }
}

// ============================================================================
// Input helpers
// ============================================================================

int CliManager::readLineOrDefault(const std::string& prompt, int defaultVal) {
    std::cout << prompt;
    std::string line;
    std::getline(std::cin, line);
    if (line.empty()) return defaultVal;
    try { return std::stoi(line); }
    catch (...) { return defaultVal; }
}

void CliManager::parseAddr(const std::string& line, std::string& ip, uint16_t& port) {
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
        ip = line.substr(0, colon);
        try { port = static_cast<uint16_t>(std::stoi(line.substr(colon + 1))); }
        catch (...) { port = DEFAULT_PORT; }
    } else {
        ip = line;
        port = DEFAULT_PORT;
    }
}

// ============================================================================
// Window management
// ============================================================================

bool CliManager::initPersistentContext() {
    if (!glfwInit()) {
        std::cerr << "GLFW init failed" << std::endl;
        return false;
    }
    m_glfwInitialized = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);

    m_persistentCtx = glfwCreateWindow(1, 1, "persistent", NULL, NULL);
    if (!m_persistentCtx) {
        std::cerr << "Persistent GL context creation failed" << std::endl;
        glfwTerminate();
        m_glfwInitialized = false;
        return false;
    }
    glfwMakeContextCurrent(m_persistentCtx);
    glfwSwapInterval(0);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "GLEW init (persistent) failed" << std::endl;
        glfwDestroyWindow(m_persistentCtx);
        m_persistentCtx = nullptr;
        glfwTerminate();
        m_glfwInitialized = false;
        return false;
    }

    // Preload textures into persistent context; all shared windows will reuse them
    auto texMgr = TextureMgr::GetInstance();
    if (RuntimeConfig::get().verboseTextureLoading)
        std::cout << "[CliManager] Textures loaded into persistent GL context" << std::endl;

    // 加载物品注册表（图标 GL 纹理进持久上下文，随窗口共享）。调试模式下只加载
    // load_in_debug 标记的少量物品，避免每次启动加载全量 600+ 张图标。
    ItemRegistry::instance().load();
    return true;
}

void CliManager::destroyPersistentContext() {
    if (m_persistentCtx) {
        glfwMakeContextCurrent(m_persistentCtx);
        glfwDestroyWindow(m_persistentCtx);
        m_persistentCtx = nullptr;
    }
    if (m_glfwInitialized) {
        glfwTerminate();
        m_glfwInitialized = false;
    }
}

GLFWwindow* CliManager::createWindow() {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);

    // 共享持久化上下文，纹理等容器对象在所有窗口间共享
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Open Window", NULL, m_persistentCtx);
    if (!window) {
        std::cerr << "Window creation failed" << std::endl;
        return nullptr;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "GLEW init failed" << std::endl;
        glfwDestroyWindow(window);
        return nullptr;
    }

    setWindowIcon(window, "assert/textures/item/Axolotl_Bucket.png");
    if (m_winPosX >= 0 && m_winPosY >= 0) {
        glfwSetWindowPos(window, m_winPosX, m_winPosY);
    }
    return window;
}

void CliManager::destroyWindow(GLFWwindow* window) {
    if (window) {
        glfwDestroyWindow(window);
        // 恢复持久化上下文为 current，确保 GL 操作有有效上下文
        if (m_persistentCtx) {
            glfwMakeContextCurrent(m_persistentCtx);
        }
    }
}

// ============================================================================
// Menu phases
// ============================================================================

bool CliManager::showMainMenu(SessionConfig& cfg) {
    std::cout << std::endl << "=== iMc Voxel Engine ===" << std::endl;
    std::cout << "1. New World" << std::endl;
    std::cout << "2. Load Save" << std::endl;
    std::cout << "3. LAN Join" << std::endl;
    std::cout << "4. Exit" << std::endl;

    int mode = readLineOrDefault("Select (Enter=New World): ", 1);

    if (mode == 4) {
        std::cout << "Goodbye!" << std::endl;
        return false;
    }

    if (mode == 3) {
        return doLanJoin(cfg);
    }

    return doWorldSelect(cfg);
}

bool CliManager::doLanJoin(SessionConfig& cfg) {
    std::cout << "Server address (Enter = 127.0.0.1:60011): ";
    std::string addrLine;
    std::getline(std::cin, addrLine);
    if (addrLine.empty()) {
        cfg.joinAddress = "127.0.0.1";
        cfg.netPort = DEFAULT_PORT;
    } else {
        parseAddr(addrLine, cfg.joinAddress, cfg.netPort);
    }
    cfg.netMode = NetMode::Join;
    // Join 模式：种子由服务端分发，世界名仅用于本地缓存
    cfg.worldName = "JoinSession";
    cfg.seed = 0;
    cfg.isNewWorld = true;
    std::cout << "[CLI] LAN mode, connecting: " << cfg.joinAddress << ":" << cfg.netPort << std::endl;
    return true;
}

bool CliManager::doWorldSelect(SessionConfig& cfg) {
    auto worlds = ChunkSaveManager::listWorlds();
    if (!worlds.empty()) {
        std::cout << std::endl << "Existing worlds:" << std::endl;
        for (size_t i = 0; i < worlds.size(); ++i) {
            std::cout << "  " << (i + 1) << ". " << worlds[i].name
                      << "  (seed: " << worlds[i].seed << ")" << std::endl;
        }
        std::cout << "  0. New World" << std::endl;
        int sel = readLineOrDefault("Enter number (Enter=New World): ", 0);
        if (sel > 0 && sel <= (int)worlds.size()) {
            cfg.worldName = worlds[sel - 1].name;
            cfg.seed = worlds[sel - 1].seed;
            cfg.isNewWorld = false;
            return doNetworkMode(cfg);
        }
    }

    // Create new world
    auto existing = ChunkSaveManager::listWorlds();
    std::unordered_set<std::string> usedNames;
    for (auto& w : existing) usedNames.insert(w.name);

    while (true) {
        std::cout << "World name (Enter = \"New World\"): ";
        std::getline(std::cin, cfg.worldName);
        if (cfg.worldName.empty()) {
            cfg.worldName = "New World";
            int suffix = 1;
            while (usedNames.count(cfg.worldName)) {
                cfg.worldName = "New World_" + std::to_string(suffix++);
            }
            std::cout << "Using name: \"" << cfg.worldName << "\"" << std::endl;
            break;
        }
        if (usedNames.count(cfg.worldName)) {
            std::cout << "Name \"" << cfg.worldName << "\" already exists." << std::endl;
        } else {
            break;
        }
    }

    std::cout << "Seed (Enter = random): ";
    std::string seedLine;
    std::getline(std::cin, seedLine);
    if (seedLine.empty()) {
        std::random_device rd;
        uint32_t lo = (uint32_t)rd(), hi = (uint32_t)rd();
        cfg.seed = ((uint64_t)hi << 32) | lo;
        std::cout << "Random seed: " << cfg.seed << std::endl;
    } else {
        cfg.seed = hashStringToSeed(seedLine);
        std::cout << "Seed: " << cfg.seed << " (from \"" << seedLine << "\")" << std::endl;
    }
    cfg.isNewWorld = true;

    return doNetworkMode(cfg);
}

bool CliManager::doNetworkMode(SessionConfig& cfg) {
    std::cout << std::endl << "=== Network Mode ===" << std::endl;
    std::cout << "1. Single Player" << std::endl;
    std::cout << "2. Host" << std::endl;

    int netChoice = readLineOrDefault("Select (Enter=Single): ", 1);

    if (netChoice == 2) {
        cfg.netMode = NetMode::Host;
        std::cout << "Port (Enter = " << DEFAULT_PORT << "): ";
        std::string portLine;
        std::getline(std::cin, portLine);
        if (portLine.empty()) cfg.netPort = DEFAULT_PORT;
        else cfg.netPort = static_cast<uint16_t>(std::stoi(portLine));
    }
    return true;
}

// ============================================================================
// Main loop
// ============================================================================

namespace {
    void runWorldSafe(World& world) {
        __try {
            world.run();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            std::cerr << "[CLI] Fatal exception 0x" << std::hex
                      << GetExceptionCode() << " in game loop, returning to menu"
                      << std::endl;
        }
    }
}

int CliManager::run() {
    if (!initPersistentContext()) {
        std::cerr << "[CLI] Failed to init persistent GL context" << std::endl;
        return -1;
    }

    bool skipInteractive = (m_cmdline.netMode != NetMode::None);

    while (true) {
        SessionConfig cfg;
        NetMode netMode;
        uint16_t netPort;
        std::string joinAddress;
        std::string worldName;
        uint64_t seed;
        bool isNewWorld;

        if (skipInteractive) {
            netMode = m_cmdline.netMode;
            netPort = m_cmdline.port;
            joinAddress = m_cmdline.joinAddr;
            worldName = m_cmdline.worldName;

            if (netMode == NetMode::Join) {
                // Join 模式：种子由服务端分发，无需本地生成
                if (worldName.empty()) {
                    worldName = "JoinSession";
                }
                seed = 0;
                isNewWorld = true;
                std::cout << "[CLI] Joining " << joinAddress << ":" << netPort << std::endl;
            } else {
                if (worldName.empty()) {
                    auto existing = ChunkSaveManager::listWorlds();
                    std::unordered_set<std::string> used;
                    for (auto& w : existing) used.insert(w.name);
                    worldName = "New World";
                    int suffix = 1;
                    while (used.count(worldName)) {
                        worldName = "New World_" + std::to_string(suffix++);
                    }
                }
                std::random_device rd;
                uint32_t lo = (uint32_t)rd(), hi = (uint32_t)rd();
                seed = ((uint64_t)hi << 32) | lo;
                isNewWorld = true;
                std::cout << "[CLI] Quick start, world: \"" << worldName
                          << "\", seed: " << seed << std::endl;
            }
        } else {
            if (!showMainMenu(cfg))
                break;

            netMode = cfg.netMode;
            netPort = cfg.netPort;
            joinAddress = cfg.joinAddress;
            worldName = cfg.worldName;
            seed = cfg.seed;
            isNewWorld = cfg.isNewWorld;
        }

        // 设置窗口初始位置（命令行或菜单指定）
        m_winPosX = m_cmdline.winPosX;
        m_winPosY = m_cmdline.winPosY;

        // Create window -> create world -> run
        GLFWwindow* window = createWindow();
        if (!window) return -1;

        World world(window, worldName, seed, isNewWorld, netMode);

        if (netMode != NetMode::None) {
            if (!world.setupNetworking(netMode, netPort, joinAddress)) {
                if (skipInteractive) {
                    std::cout << "Network start failed, returning to menu." << std::endl;
                    skipInteractive = false;
                    m_cmdline.netMode = NetMode::None;
                } else if (netMode == NetMode::Host) {
                    std::cout << "Host creation failed, returning to menu." << std::endl;
                } else {
                    std::cout << "Join failed, returning to menu." << std::endl;
                }
                destroyWindow(window);
                continue;
            }
        }

        runWorldSafe(world);

        destroyWindow(window);

        skipInteractive = false;
        m_cmdline.netMode = NetMode::None;
    }

    destroyPersistentContext();
    return 0;
}
