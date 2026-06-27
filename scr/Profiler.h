#pragma once
#include <chrono>
#include <cstdint>
#include <string>

// 轻量 CPU profiler。
//   - 每个被测区段在 main thread 的累计耗时聚合到一个全局表
//   - 每秒由 Profiler::frame() 触发一次打印（如果 RuntimeConfig 启用）
//   - 不加锁，仅供主线程区段使用（worker 线程的耗时另行处理）
//
// 调用树输出：ScopedTimer 维护一个主线程调用栈，记录每个区段的"父区段"。
//   dump 时，一个区段当且仅当它【只被唯一一个父区段调用】且【非递归】时，
//   以 " -name" 缩进列在其父之下，并附带"占父 %"；否则平铺为根节点。
//   多处调用 / 递归 / 顶层区段都作为根节点，按 total 降序排列。
//
// 用法：
//   void foo() {
//       PROFILE_SCOPE("foo");          // 计时
//       ...
//   }
//   Profiler::addCounter("bar", 42);   // 计数（独立表格）
class Profiler {
public:
    // 主线程每帧调一次（在帧首尾都行）。负责每秒触发打印 + 重置累计。
    static void frame();

    // 累加一次耗时（μs）。name 为字符串字面量（按指针身份聚合），
    // parent 为调用栈上紧邻的上层区段（无则 nullptr）。一般由 ScopedTimer 自动调。
    static void addSample(const char* name, const char* parent, int64_t microseconds);

    // 累加一次计数值（与计时表分开，输出独立表格）。
    static void addCounter(const char* name, int64_t value);

    // 立即打印一次当前累计（不重置）
    static void dump();

    // 调用栈维护（仅 ScopedTimer 内部使用，主线程单线程，无锁）。
    static const char* pushScope(const char* name);  // 返回入栈前的栈顶（即本区段的父）
    static void popScope();

private:
    Profiler() = default;
};

class ScopedTimer {
public:
    explicit ScopedTimer(const char* name) noexcept
        : m_name(name),
          m_parent(Profiler::pushScope(name)),
          m_start(std::chrono::steady_clock::now()) {}

    ~ScopedTimer() noexcept {
        auto end = std::chrono::steady_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - m_start).count();
        Profiler::popScope();
        Profiler::addSample(m_name, m_parent, us);
    }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    const char* m_name;
    const char* m_parent;
    std::chrono::steady_clock::time_point m_start;
};

// 便利宏：在变量名上拼接行号避免冲突
#define PROFILE_TOKEN_PASTE2(a, b) a##b
#define PROFILE_TOKEN_PASTE(a, b) PROFILE_TOKEN_PASTE2(a, b)
#define PROFILE_SCOPE(name) ScopedTimer PROFILE_TOKEN_PASTE(_prof_, __LINE__){ name }
