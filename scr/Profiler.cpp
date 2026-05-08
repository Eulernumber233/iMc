#include "Profiler.h"
#include "RuntimeConfig.h"
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdio>

namespace {
    struct Entry {
        int64_t totalUs = 0;
        int64_t maxUs = 0;
        int     count = 0;
    };

    // key 是 const char* 字面量地址（不是字符串内容），所以同字符串字面量的指针一致 —— OK
    std::unordered_map<const char*, Entry> g_entries;
    std::chrono::steady_clock::time_point g_lastDump = std::chrono::steady_clock::now();
    int g_frameCount = 0;
}

void Profiler::addSample(const char* name, int64_t microseconds) {
    auto& e = g_entries[name];
    e.totalUs += microseconds;
    if (microseconds > e.maxUs) e.maxUs = microseconds;
    ++e.count;
}

void Profiler::frame() {
    ++g_frameCount;
    if (!RuntimeConfig::get().printProfileEverySecond) return;

    auto now = std::chrono::steady_clock::now();
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(now - g_lastDump).count();
    if (sec < 1) return;

    dump();
    g_entries.clear();
    g_lastDump = now;
    g_frameCount = 0;
}

void Profiler::dump() {
    if (g_entries.empty()) return;

    std::vector<std::pair<const char*, Entry>> sorted(g_entries.begin(), g_entries.end());
    std::sort(sorted.begin(), sorted.end(),
        [](const auto& a, const auto& b) { return a.second.totalUs > b.second.totalUs; });

    std::printf("=== Profile (last %d frames) ===\n", g_frameCount);
    std::printf("%-32s %10s %10s %8s %10s\n",
                "name", "total_ms", "avg_us", "count", "max_us");
    for (const auto& kv : sorted) {
        const auto& e = kv.second;
        double totalMs = e.totalUs / 1000.0;
        int64_t avgUs = e.count ? (e.totalUs / e.count) : 0;
        std::printf("%-32s %10.3f %10lld %8d %10lld\n",
                    kv.first, totalMs, (long long)avgUs, e.count, (long long)e.maxUs);
    }
    std::printf("================================\n");
}
