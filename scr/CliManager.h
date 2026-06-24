#pragma once

#include "World.h"
#include <string>
#include <cstdint>

struct CmdlineArgs {
    NetMode netMode = NetMode::None;
    uint16_t port = 0;
    std::string joinAddr;
    std::string worldName;
};

struct SessionConfig {
    std::string worldName;
    uint64_t seed = 0;
    bool isNewWorld = false;
    NetMode netMode = NetMode::None;
    uint16_t netPort = 0;
    std::string joinAddress;
};

class CliManager {
public:
    void parseCmdline(int argc, char* argv[]);

    int run();

private:
    CmdlineArgs m_cmdline;

    int readLineOrDefault(const std::string& prompt, int defaultVal);
    void parseAddr(const std::string& line, std::string& ip, uint16_t& port);

    bool showMainMenu(SessionConfig& cfg);
    bool doLanJoin(SessionConfig& cfg);
    bool doWorldSelect(SessionConfig& cfg);
    bool doNetworkMode(SessionConfig& cfg);

    bool initPersistentContext();
    void destroyPersistentContext();

    GLFWwindow* createWindow();
    void destroyWindow(GLFWwindow* window);

    bool m_glfwInitialized = false;
    GLFWwindow* m_persistentCtx = nullptr;
};
