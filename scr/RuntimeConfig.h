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

    int renderRadius = 8;                // 渲染半径（chunk），视野内加载 (2r+1)^2 个 chunk
    int maxInflightRequests = 32;        // 同时投递的最大 build 任务数，超过则暂停请求新 chunk
    int maxUploadsPerFrame = 8;          // 每帧最多上传多少个脏 section 到 GPU
    int workerThreads = 0;               // 0 = 由 hardware_concurrency 自动决定

    // 下方 section 剔除：maxDownSections = renderRadius × verticalCullRatio
    // 例：8×0.5=4 → 相机所在 section 向下 4 个以外的不渲染。0 或负值 = 不限制
    float verticalCullRatio = 0.5f;

    bool printProfileEverySecond = false; // 每秒输出 profiler 汇总
    // 细粒度计时/计数开关：关闭时（默认）完全绕过热路径里的细分插桩
    // （cullPass 每 chunk 的 rdc.cull.* 计时、getVisibleSectionMask 每 section 的 vis.* 计数），
    // 连 steady_clock::now() 都不调，零开销。需要排查 cullPass 内部分布时再开。
    bool profileDetailed = false;
    bool verboseTextureLoading = false;   // 输出纹理加载详情日志
    bool verboseShaderLoading = false;    // 输出着色器编译详情日志

    // 各向异性过滤等级（方块纹理数组）。去远处地形闪烁的核心手段。
    // <=0 或缺省 = 取硬件支持的最大值（通常 16）；填具体值则取 min(该值, 硬件最大)。
    float anisotropy = 0.0f;

    // 存档：自动保存间隔（秒）。设为 0 禁用定时自动保存（区块卸载时仍会保存）
    int autoSaveIntervalSec = 60;

    // ---- 阴影（阶段 2：PCSS/PCF + 蓝噪声 + TAA 降噪）----
    // 单帧故意少样本（噪声大但便宜），靠 TAA 跨帧累积变干净。调参不必重编。
    int   shadowBlockerSamples = 8;     // blocker search 抽样数（估遮挡物平均深度）
    int   shadowFilterSamples  = 8;     // PCF filter 抽样数（半影内可见度平均）
    float shadowLightSize      = 0.008f;// 光源"大小"（阴影贴图 UV），决定半影强度

    // 温存/落盘半径余量：chunk 离开渲染半径后，在 renderRadius + retainMarginChunks 内
    // 仍保留在内存（不渲染、不卸载、不落盘），形成"温存区"吸收边界抖动；
    // 超出此半径才真正落盘 + 卸载。调大 = 更省磁盘 IO/地形重生成，但占更多内存。
    int retainMarginChunks = 6;

private:
    RuntimeConfig() = default;
    void loadFrom(const std::string& path);
};
