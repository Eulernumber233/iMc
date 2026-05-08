#pragma once
#include <chrono>
#include <cstdint>
#include <string>

// 轻量 CPU profiler。
//   - 每个被测区段在 main thread 的累计耗时聚合到一个全局表
//   - 每秒由 Profiler::frame() 触发一次打印（如果 RuntimeConfig 启用）
//   - 不加锁，仅供主线程区段使用（worker 线程的耗时另行处理）
//
// 用法：
//   void foo() {
//       PROFILE_SCOPE("foo");
//       ...
//   }
class Profiler {
public:
    // 主线程每帧调一次（在帧首尾都行）。负责每秒触发打印 + 重置累计。
    static void frame();

    // 在某区段结束时累加一次耗时。一般通过 ScopedTimer 自动调。
    static void addSample(const char* name, int64_t microseconds);

    // 立即打印一次当前累计（不重置）
    static void dump();

private:
    Profiler() = default;
};

class ScopedTimer {
public:
    explicit ScopedTimer(const char* name) noexcept
        : m_name(name), m_start(std::chrono::steady_clock::now()) {}

    ~ScopedTimer() noexcept {
        auto end = std::chrono::steady_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - m_start).count();
        Profiler::addSample(m_name, us);
    }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    const char* m_name;
    std::chrono::steady_clock::time_point m_start;
};

// 便利宏：在变量名上拼接行号避免冲突
#define PROFILE_TOKEN_PASTE2(a, b) a##b
#define PROFILE_TOKEN_PASTE(a, b) PROFILE_TOKEN_PASTE2(a, b)
#define PROFILE_SCOPE(name) ScopedTimer PROFILE_TOKEN_PASTE(_prof_, __LINE__){ name }
