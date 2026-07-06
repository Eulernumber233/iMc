#pragma once
#include <string>

// 运行时可调参数。第一次访问时延迟从 assert/runtime_config.json 加载；
// 文件缺失或字段缺失时回退到代码默认值。
//
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

    // 强制重新编译着色器：true 则忽略并删除磁盘缓存(cache/shaders/*.bin)，从源码重编后重写缓存。
    // 改了着色器源码后置 true 跑一次即可（缓存键只哈希文件名、不哈希内容）。
    // 注意：命令行 --rebuild-shaders / --no-rebuild-shaders 优先级高于此字段。
    bool forceRecompileShaders = false;

    // 调试模式：为 true 时物品注册表只加载标记了 load_in_debug 的物品图标，
    // 避免每次启动都加载 assert/minecraft/textures/item 下的全量图标（600+ 张）。
    // 正式发布时置 false 加载全量资源。
    bool debugMode = true;

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

    // ---- CSM 级联阴影（阶段 3）----
    // 把单张 shadow map 换成按视距分级的多张，根治远处阴影边缘随视角晃的"波浪"。
    // 级联数 / 分辨率改后需重建 CSM target（重启最稳）；lambda / 距离每帧重算即生效。
    int   csmCascadeCount = 4;          // 级联数（1~CASCADE_COUNT 编译期上限），超上限被截断
    float csmSplitLambda  = 0.7f;       // 切分混合：1=纯对数(近处极密)，0=纯均匀
    int   csmShadowSize   = 2048;       // 每级联分辨率（重建 target 才生效）
    float shadowMaxDistance = 180.0f;   // 阴影总覆盖距离（最远级联远边界，米）

    // ---- AO（阶段 2：HBAO + 蓝噪声 + 时域累积）----
    // 单帧少方向/少步数（噪声大但便宜），靠时域累积降噪。AO 纯几何恒定，收敛快。
    int   aoDirections = 4;             // HBAO 采样方向数
    int   aoSteps      = 4;             // 每方向步进次数
    float aoRadius     = 0.8f;          // 采样半径（世界尺度，米）
    float aoIntensity  = 1.5f;          // 遮蔽强度（指数，越大越黑）
    float aoBias       = 0.1f;          // 切线角 bias（弧度），抑制自遮挡暗带
    // AO 随昼夜淡出：白天 AO 全效(=1)，夜晚淡到此弱底值。解决"夜晚没光却有强阴影"的违和。
    // effectiveAO = mix(1.0, ao, aoStrength)，aoStrength = mix(aoNightStrength, 1.0, sunIntensity)
    float aoNightStrength = 0.25f;      // 夜晚 AO 强度底值（0=夜晚完全无 AO，1=不淡出）

    // ---- 直接光照能量分配（参考原版 MC：环境光 + 阳光两份，按昼夜分配）----
    // 模型：result = budget * (ambientWeight*albedo*ao + sunWeight*NdotL*visibility*albedo)
    //   ambientWeight = mix(ambientNight, ambientDay, sunIntensity)  —— 始终保留的底光，
    //     夜晚/清晨较大（保证未受阳光直射处也够亮），正午较小（让阳光主导）
    //   sunWeight     = sunStrength * sunIntensity                   —— 阳光份额，夜晚=0，
    //     地平线附近由 sunIntensity 平滑过渡（清晨/傍晚不突变）
    // 整体亮度调 lightBudget；嫌阴影处太黑调大 ambientDay/ambientNight；嫌阳光太弱调 sunStrength。
    // 近似能量守恒：ambientDay + sunStrength ≈ 1 时，正午完全受光面约等于 lightBudget。
    float lightBudget   = 1.25f;        // 总光照预算（整体亮度旋钮，>1 提亮）
    float ambientDay    = 0.55f;        // 白天环境光占比（未受阳光处的底光，调大→阴影更亮）
    float ambientNight  = 0.40f;        // 夜晚环境光占比（夜间底光）
    float sunStrength    = 0.55f;       // 阳光份额（正午满日照时的直射功率）

    // 时间比例：1 现实秒 = 多少游戏小时。默认 0.2 → 一整天 24h 约 120 现实秒。
    // o/p 键运行时可调（可负=时间倒流），此处是初值。
    float timeScale = 0.2f;

    // ---- 走路镜头抖动（view bobbing）----
    // 仅本地第一人称生效（第三人称不加，同原版 MC）；只改相机、不改玩家实际位置，
    // 故网络对端看不到。奔跑时更强。调 viewBobScale 即可整体加减，0 = 关闭。
    bool  viewBobEnabled  = true;   // 总开关
    float viewBobScale    = 1.0f;   // 幅度总比例（调试旋钮，0=无抖动）
    float viewBobRunScale = 1.6f;   // 奔跑时的额外幅度倍率

    // 启动时禁用输入法(IME)：解除游戏窗口与 IME 上下文的关联，避免进入游戏后
    // 输入法切到拼音吞掉 WASD、需先按一次 Shift 才能移动。默认开启。
    bool disableIme = true;

    // 温存/落盘半径余量：chunk 离开渲染半径后，在 renderRadius + retainMarginChunks 内
    // 仍保留在内存（不渲染、不卸载、不落盘），形成"温存区"吸收边界抖动；
    // 超出此半径才真正落盘 + 卸载。调大 = 更省磁盘 IO/地形重生成，但占更多内存。
    int retainMarginChunks = 6;

private:
    RuntimeConfig() = default;
    void loadFrom(const std::string& path);
    // 内部可变单例：get() 返回它的 const 引用；热重载回调用它就地重读 JSON。
    static RuntimeConfig& mutableInstance();
};
