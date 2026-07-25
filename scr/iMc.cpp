// 强制使用独立显卡
extern "C" { __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001; }
extern "C" { __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1; }

#define _WINSOCK_DEPRECATED_NO_WARNINGS
// 强制 ENet 使用 IPv4
#define ENET_IPV4_ONLY
#define ENET_IMPLEMENTATION
#include "enet/enet.h"

#include "CliManager.h"
#include <cstdlib>

int main(int argc, char* argv[]) {
    srand(13);

    CliManager cli;
    cli.parseCmdline(argc, argv);  
    return cli.run();
}
