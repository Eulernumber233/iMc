#include "Profiler.h"
#include "RuntimeConfig.h"
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdio>

namespace {
    struct TimeEntry {
        int64_t totalUs = 0;
        int64_t maxUs = 0;
        int     count = 0;
    };

    struct CounterEntry {
        int64_t total = 0;
        int64_t max = 0;
        int     count = 0;
    };

    std::unordered_map<const char*, TimeEntry>    g_timeEntries;
    std::unordered_map<const char*, CounterEntry> g_counters;
    std::chrono::steady_clock::time_point g_lastDump = std::chrono::steady_clock::now();
    int g_frameCount = 0;
}

void Profiler::addSample(const char* name, int64_t microseconds) {
    auto& e = g_timeEntries[name];
    e.totalUs += microseconds;
    if (microseconds > e.maxUs) e.maxUs = microseconds;
    ++e.count;
}

void Profiler::addCounter(const char* name, int64_t value) {
    auto& c = g_counters[name];
    c.total += value;
    if (value > c.max) c.max = value;
    ++c.count;
}

void Profiler::frame() {
    ++g_frameCount;
    if (!RuntimeConfig::get().printProfileEverySecond) return;

    auto now = std::chrono::steady_clock::now();
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(now - g_lastDump).count();
    if (sec < 1) return;

    dump();
    g_timeEntries.clear();
    g_counters.clear();
    g_lastDump = now;
    g_frameCount = 0;
}

void Profiler::dump() {
    // ---- 计时表 ----
    if (!g_timeEntries.empty()) {
        std::vector<std::pair<const char*, TimeEntry>> sorted(
            g_timeEntries.begin(), g_timeEntries.end());
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.second.totalUs > b.second.totalUs; });

        std::printf("=== Profile Timing (last %d frames) ===\n", g_frameCount);
        std::printf("%-34s %10s %10s %8s %10s\n",
                    "name", "total_ms", "avg_us", "count", "max_us");
        for (const auto& kv : sorted) {
            const auto& e = kv.second;
            double totalMs = e.totalUs / 1000.0;
            int64_t avgUs = e.count ? (e.totalUs / e.count) : 0;
            std::printf("%-34s %10.3f %10lld %8d %10lld\n",
                        kv.first, totalMs, (long long)avgUs, e.count, (long long)e.maxUs);
        }
        std::printf("==========================================\n");
    }

    // ---- 计数表 ----
    if (!g_counters.empty()) {
        std::vector<std::pair<const char*, CounterEntry>> sorted(
            g_counters.begin(), g_counters.end());
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.second.total > b.second.total; });

        std::printf("=== Profile Counters (last %d frames) ===\n", g_frameCount);
        std::printf("%-34s %10s %10s %8s %10s\n",
                    "name", "total", "avg", "count", "max");
        for (const auto& kv : sorted) {
            const auto& c = kv.second;
            int64_t avg = c.count ? (c.total / c.count) : 0;
            std::printf("%-34s %10lld %10lld %8d %10lld\n",
                        kv.first, (long long)c.total, (long long)avg, c.count, (long long)c.max);
        }
        std::printf("===========================================\n");
    }
}
