#pragma once
#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <chrono>

// ── 通用文件热重载（单例，header-only）───────────────────────────────
// 用途：把「保存 JSON → 游戏自动重新读入」这件事做成一处可复用的机制，免去每次
// 改参数都重启。任何配置只要暴露一个「重新读文件」的回调，注册进来即可。
//
// 用法：
//   1) 在配置类首次加载时注册（只注册一次）：
//        HotReload::instance().watch("assert/xxx.json",
//            []{ SomeConfig::reloadFromDisk(); });
//   2) 主循环每帧调一次：HotReload::instance().poll();
//      poll 内部按 m_pollIntervalSec 节流（默认 0.25s 才真正 stat 一次磁盘），
//      发现某文件修改时间变了就调它的回调。
//
// 线程：poll 与所有回调都在主线程执行。故只应挂「主线程消费」的配置
// （held_display、渲染/光照/AO/view_bob 等）。worker 线程读取的字段
// （如 worker_threads、render_radius 这类启动期一次性字段）即便重读也不会即时
// 生效，且不应在此并发改写——详见各配置注册处的注释。
//
// 半写保护：编辑器保存到一半时可能读到残缺 JSON，解析会失败。各配置的
// load/loadFrom 在解析失败时保留旧值即可（RuntimeConfig / HeldDisplay 均如此），
// 下一次 poll（文件已写完）会自动读成功，能自愈。
//
// 注意：刻意不用 std::filesystem::file_time_type::min() 作哨兵——windows.h 会把
// min/max 定义成宏，污染该静态成员调用。改用 bool 标记「是否已有有效时间」。
class HotReload {
public:
    static HotReload& instance() {
        static HotReload inst;
        return inst;
    }

    // 注册一个要监视的文件 + 变更时的回调。注册时记录当前修改时间（不触发回调）。
    // 若注册时文件不可读，haveLast 保持 false，待文件出现后的首次 poll 会触发一次。
    void watch(const std::string& path, std::function<void()> onChange) {
        Entry e;
        e.path = path;
        e.onChange = std::move(onChange);
        e.haveLast = queryTime(path, e.lastWrite);
        m_entries.push_back(std::move(e));
    }

    // 主循环每帧调用。按 m_pollIntervalSec 节流；修改时间变化的文件才调回调。
    void poll() {
        auto now = std::chrono::steady_clock::now();
        if (m_hasLast) {
            float dt = std::chrono::duration<float>(now - m_lastPoll).count();
            if (dt < m_pollIntervalSec) return;
        }
        m_hasLast = true;
        m_lastPoll = now;

        for (auto& e : m_entries) {
            std::filesystem::file_time_type t;
            if (!queryTime(e.path, t)) continue;   // 文件暂不可读（缺失/被独占）：跳过
            if (!e.haveLast || t != e.lastWrite) {
                e.lastWrite = t;
                e.haveLast = true;
                if (e.onChange) e.onChange();
            }
        }
    }

    void setPollInterval(float sec) { m_pollIntervalSec = sec; }

private:
    HotReload() = default;

    // 读文件修改时间。成功返回 true 并写 out；失败（不存在/被占用）返回 false。
    static bool queryTime(const std::string& path, std::filesystem::file_time_type& out) {
        std::error_code ec;
        out = std::filesystem::last_write_time(path, ec);
        return !ec;
    }

    struct Entry {
        std::string path;
        std::function<void()> onChange;
        std::filesystem::file_time_type lastWrite{};
        bool haveLast = false;   // 是否已有一个有效的 lastWrite
    };

    std::vector<Entry> m_entries;
    std::chrono::steady_clock::time_point m_lastPoll{};
    bool  m_hasLast = false;
    float m_pollIntervalSec = 0.25f;   // 节流：多久才真正 stat 一次磁盘
};
