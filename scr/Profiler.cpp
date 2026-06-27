#include "Profiler.h"
#include "RuntimeConfig.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
    // 按 (name, parent) 聚合的计时项。name/parent 都是字符串字面量指针（按身份比较）。
    struct EdgeKey {
        const char* name;
        const char* parent;  // nullptr = 顶层（无父）
        bool operator==(const EdgeKey& o) const {
            return name == o.name && parent == o.parent;
        }
    };
    struct EdgeKeyHash {
        size_t operator()(const EdgeKey& k) const {
            // 指针哈希混合
            size_t h1 = std::hash<const void*>{}(k.name);
            size_t h2 = std::hash<const void*>{}(k.parent);
            return h1 ^ (h2 * 0x9E3779B97F4A7C15ull);
        }
    };

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

    // 每条调用边 (parent → name) 的累计耗时。
    std::unordered_map<EdgeKey, TimeEntry, EdgeKeyHash> g_edges;
    std::unordered_map<const char*, CounterEntry>       g_counters;

    // 主线程调用栈（ScopedTimer 维护）。存当前活跃区段名，栈顶即下一个区段的父。
    std::vector<const char*> g_scopeStack;

    std::chrono::steady_clock::time_point g_lastDump = std::chrono::steady_clock::now();
    int g_frameCount = 0;
}

const char* Profiler::pushScope(const char* name) {
    const char* parent = g_scopeStack.empty() ? nullptr : g_scopeStack.back();
    g_scopeStack.push_back(name);
    return parent;
}

void Profiler::popScope() {
    if (!g_scopeStack.empty()) g_scopeStack.pop_back();
}

void Profiler::addSample(const char* name, const char* parent, int64_t microseconds) {
    auto& e = g_edges[EdgeKey{ name, parent }];
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
    g_edges.clear();
    g_counters.clear();
    g_lastDump = now;
    g_frameCount = 0;
}

namespace {
    // 聚合到 name 维度的总量（跨所有父），以及该 name 出现过的"父集合"。
    struct NameAgg {
        int64_t totalUs = 0;
        int64_t maxUs = 0;
        int     count = 0;
        std::unordered_set<const char*> parents;  // 出现过的所有父（含 nullptr 用哨兵表示）
        bool    selfParent = false;               // 是否直接递归（父 == 自己）
    };

    // 嵌套判定：name 当且仅当
    //   - 恰有一个父，且该父非 nullptr（即不是顶层）
    //   - 非递归（父集合里不含自己）
    // 时，可作为子节点缩进在其唯一父之下；否则作为根节点平铺。
    bool isNestable(const NameAgg& a, const char*& outParent) {
        if (a.selfParent) return false;
        if (a.parents.size() != 1) return false;
        const char* p = *a.parents.begin();
        if (p == nullptr) return false;
        outParent = p;
        return true;
    }

    // 打印一行计时（缩进 depth 层，子节点带 "占父%"）。
    void printTimeRow(const char* name, const TimeEntry& e, int depth,
                      int64_t parentTotalUs) {
        // 缩进：每层 " -"
        char indent[64] = { 0 };
        int pos = 0;
        for (int d = 0; d < depth && pos < (int)sizeof(indent) - 3; ++d) {
            indent[pos++] = ' ';
            indent[pos++] = '-';
        }
        indent[pos] = '\0';

        // name 列宽对齐：缩进 + 名字共占 34
        char label[160];
        std::snprintf(label, sizeof(label), "%s%s", indent, name);

        double totalMs = e.totalUs / 1000.0;
        int64_t avgUs = e.count ? (e.totalUs / e.count) : 0;

        if (depth > 0 && parentTotalUs > 0) {
            double pct = 100.0 * (double)e.totalUs / (double)parentTotalUs;
            std::printf("%-34s %10.3f %10lld %8d %10lld %7.1f%%\n",
                        label, totalMs, (long long)avgUs, e.count,
                        (long long)e.maxUs, pct);
        } else {
            std::printf("%-34s %10.3f %10lld %8d %10lld %8s\n",
                        label, totalMs, (long long)avgUs, e.count,
                        (long long)e.maxUs, "-");
        }
    }
}

void Profiler::dump() {
    // ---- 计时表（调用树）----
    if (!g_edges.empty()) {
        // 1) 聚合到 name 维度，并收集每个 name 的父集合 / 递归标记。
        std::unordered_map<const char*, NameAgg> agg;
        for (const auto& kv : g_edges) {
            const EdgeKey& k = kv.first;
            const TimeEntry& e = kv.second;
            NameAgg& a = agg[k.name];
            a.totalUs += e.totalUs;
            if (e.maxUs > a.maxUs) a.maxUs = e.maxUs;
            a.count += e.count;
            a.parents.insert(k.parent);
            if (k.parent == k.name) a.selfParent = true;
        }

        // 2) 建立 父name → [子name...] 的可嵌套关系（仅对 isNestable 的 name）。
        std::unordered_map<const char*, std::vector<const char*>> children;
        std::unordered_set<const char*> nested;  // 被作为子节点的 name（不在根列表出现）
        for (const auto& kv : agg) {
            const char* name = kv.first;
            const char* parent = nullptr;
            if (isNestable(kv.second, parent)) {
                children[parent].push_back(name);
                nested.insert(name);
            }
        }

        // 3) 根节点 = 所有未被嵌套的 name，按 total 降序。
        std::vector<const char*> roots;
        for (const auto& kv : agg) {
            if (!nested.count(kv.first)) roots.push_back(kv.first);
        }
        auto byTotalDesc = [&](const char* x, const char* y) {
            return agg[x].totalUs > agg[y].totalUs;
        };
        std::sort(roots.begin(), roots.end(), byTotalDesc);
        for (auto& kv : children) {
            std::sort(kv.second.begin(), kv.second.end(), byTotalDesc);
        }

        // 4) 递归打印。子节点的 TimeEntry 用其在【该父下】的边数据；
        //    若该 name 只有唯一父（isNestable 保证），边数据 == name 聚合数据。
        std::printf("=== Profile Timing (last %d frames) ===\n", g_frameCount);
        std::printf("%-34s %10s %10s %8s %10s %8s\n",
                    "name", "total_ms", "avg_us", "count", "max_us", "of_parent");

        // 用显式栈做 DFS，避免深递归；记录 (name, depth, parentTotalUs)。
        struct Frame { const char* name; int depth; int64_t parentTotal; };
        // 注意：roots 要逆序压栈以保持降序输出。
        std::vector<Frame> stack;
        for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
            stack.push_back(Frame{ *it, 0, 0 });
        }
        while (!stack.empty()) {
            Frame f = stack.back();
            stack.pop_back();

            const NameAgg& a = agg[f.name];
            TimeEntry rowEntry;
            rowEntry.totalUs = a.totalUs;
            rowEntry.maxUs   = a.maxUs;
            rowEntry.count   = a.count;
            printTimeRow(f.name, rowEntry, f.depth, f.parentTotal);

            auto ci = children.find(f.name);
            if (ci != children.end()) {
                // 逆序压栈，弹出时即为降序。
                for (auto it = ci->second.rbegin(); it != ci->second.rend(); ++it) {
                    stack.push_back(Frame{ *it, f.depth + 1, a.totalUs });
                }
            }
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
