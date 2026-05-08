#pragma once
#include <string>

// 运行时可调参数。第一次访问时延迟从 assert/runtime_config.json 加载；
// 文件缺失或字段缺失时回退到代码默认值。
//
// 想改 RENDER_RADIUS 等参数：编辑 assert/runtime_config.json 即可，
// 不需要重新编译任何 cpp。
class RuntimeConfig {
public:
    static const RuntimeConfig& get();

    int renderRadius = 8;
    int maxInflightRequests = 32;
    int maxUploadsPerFrame = 8;
    int workerThreads = 0;     // 0 = 由 hardware_concurrency 自动决定

    // 调试输出：每秒一次的 profiler 总结
    bool printProfileEverySecond = false;

private:
    RuntimeConfig() = default;
    void loadFrom(const std::string& path);
};
