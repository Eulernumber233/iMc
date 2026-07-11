#include "ChunkWorkerPool.h"
#include "Chunk.h"
#include "../generate/TerrainGenerator.h"
#include "../save/ChunkSaveManager.h"
#include "../net/lz4.h"
#include "../net/NetChunkSync.h"  // ChunkSyncFormat::FLAG_HAS_DATA
#include <iostream>
#include <cstring>
#include <deque>
#include <bitset>
#include <unordered_set>
#include <shared_mutex>

ChunkWorkerPool::ChunkWorkerPool() = default;

ChunkWorkerPool::~ChunkWorkerPool() {
    stop();
}

void ChunkWorkerPool::start(const TerrainGenerator* generator, int numThreads) {
    if (!m_workers.empty()) return;
    m_generator = generator;
    m_stop.store(false);

    if (numThreads <= 0) numThreads = 2;

    m_workers.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i) {
        m_workers.emplace_back([this] { this->workerMain(); });
    }
}

void ChunkWorkerPool::stop() {
    if (m_workers.empty()) return;
    m_stop.store(true);
    m_jobCV.notify_all();
    for (auto& t : m_workers) {
        if (t.joinable()) t.join();
    }
    m_workers.clear();

    {
        std::lock_guard<std::mutex> lk(m_jobMutex);
        m_jobs.clear();
    }
    {
        std::lock_guard<std::mutex> lk(m_lightJobMutex);
        m_lightJobs.clear();
    }
    {
        std::lock_guard<std::mutex> lk(m_blockDoneMutex);
        m_blockDone.clear();
    }
    {
        std::lock_guard<std::mutex> lk(m_meshDoneMutex);
        m_meshDone.clear();
    }
    {
        std::lock_guard<std::mutex> lk(m_lightDoneMutex);
        m_lightDone.clear();
    }
    m_pending.store(0);
}

void ChunkWorkerPool::submitBuild(const glm::ivec2& pos) {
    Job j;
    j.kind = JOB_BUILD;
    j.pos = pos;
    {
        std::lock_guard<std::mutex> lk(m_jobMutex);
        m_jobs.push_back(std::move(j));
    }
    m_pending.fetch_add(1, std::memory_order_relaxed);
    m_jobCV.notify_one();
}

void ChunkWorkerPool::submitMeshBuild(const MeshBuildInput& input) {
    {
        std::lock_guard<std::mutex> lk(m_jobMutex);
        Job j;
        j.kind = JOB_MESH;
        j.meshInput = input;
        m_jobs.push_back(std::move(j));
    }
    m_jobCV.notify_one();
}

void ChunkWorkerPool::submitNetImport(int chunkX, int chunkZ, std::vector<uint8_t>&& serialized) {
    Job j;
    j.kind = JOB_NET_IMPORT;
    j.pos = glm::ivec2(chunkX, chunkZ);
    j.netData = std::move(serialized);
    {
        std::lock_guard<std::mutex> lk(m_jobMutex);
        m_jobs.push_back(std::move(j));
    }
    m_pending.fetch_add(1, std::memory_order_relaxed);
    m_jobCV.notify_one();
}

void ChunkWorkerPool::submitLightBuild(LightBuildInput&& input) {
    {
        std::lock_guard<std::mutex> lk(m_lightJobMutex);
        m_lightJobs.push_back(std::move(input));
    }
    m_jobCV.notify_all();  // 唤醒所有 worker，其中一个会取走 light job
}

std::vector<std::unique_ptr<BlockDataResult>> ChunkWorkerPool::drainBlockData() {
    std::deque<std::unique_ptr<BlockDataResult>> tmp;
    {
        std::lock_guard<std::mutex> lk(m_blockDoneMutex);
        tmp.swap(m_blockDone);
    }
    std::vector<std::unique_ptr<BlockDataResult>> out;
    out.reserve(tmp.size());
    for (auto& p : tmp) out.push_back(std::move(p));
    return out;
}

std::vector<std::unique_ptr<ChunkBuildResult>> ChunkWorkerPool::drainMeshResults() {
    std::deque<std::unique_ptr<ChunkBuildResult>> tmp;
    {
        std::lock_guard<std::mutex> lk(m_meshDoneMutex);
        tmp.swap(m_meshDone);
    }
    std::vector<std::unique_ptr<ChunkBuildResult>> out;
    out.reserve(tmp.size());
    for (auto& p : tmp) out.push_back(std::move(p));
    return out;
}

std::vector<std::unique_ptr<LightBuildResult>> ChunkWorkerPool::drainLightResults() {
    std::deque<std::unique_ptr<LightBuildResult>> tmp;
    {
        std::lock_guard<std::mutex> lk(m_lightDoneMutex);
        tmp.swap(m_lightDone);
    }
    std::vector<std::unique_ptr<LightBuildResult>> out;
    out.reserve(tmp.size());
    for (auto& p : tmp) out.push_back(std::move(p));
    return out;
}

void ChunkWorkerPool::workerMain() {
    while (true) {
        // ── 优先检查 Task 3 光照队列 ──
        // 仅一个 worker 可同时执行 Task 3（通过 m_lightJobRunning 互斥）。
        // 这样不同 chunk 的光照 BFS 不会同时读写相同的光照输出缓冲。
        {
            std::unique_lock<std::mutex> lk(m_lightJobMutex);
            if (!m_lightJobs.empty() && !m_lightJobRunning.load(std::memory_order_relaxed)) {
                m_lightJobRunning.store(true, std::memory_order_relaxed);
                LightBuildInput input = std::move(m_lightJobs.front());
                m_lightJobs.pop_front();
                lk.unlock();

                auto result = std::make_unique<LightBuildResult>();
                lightBuildOne(input, *result);

                {
                    std::lock_guard<std::mutex> dlk(m_lightDoneMutex);
                    m_lightDone.push_back(std::move(result));
                }
                m_lightJobRunning.store(false, std::memory_order_relaxed);
                m_jobCV.notify_all();  // 唤醒其他 worker 检查 light 队列
                continue;
            }
        }

        // ── 常规任务 ──
        Job job;
        {
            std::unique_lock<std::mutex> lk(m_jobMutex);
            m_jobCV.wait(lk, [this] {
                return m_stop.load() || !m_jobs.empty()
                    || (!m_lightJobs.empty() && !m_lightJobRunning.load(std::memory_order_relaxed));
            });
            if (m_stop.load() && m_jobs.empty()) {
                // 再次检查 light 队列：stop 时先 drain 完 light jobs
                bool hasLight = false;
                {
                    std::lock_guard<std::mutex> llk(m_lightJobMutex);
                    hasLight = !m_lightJobs.empty();
                }
                if (!hasLight) return;
                continue;  // 有 light job → 回到循环顶部取走
            }
            if (m_jobs.empty()) continue;  // 被 light job 唤醒但被其他 worker 取走了
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }

        if (job.kind == JOB_BUILD) {
            auto result = std::make_unique<BlockDataResult>();
            result->pos = job.pos;
            buildOne(job.pos, *result);
            {
                std::lock_guard<std::mutex> lk(m_blockDoneMutex);
                m_blockDone.push_back(std::move(result));
            }
            m_pending.fetch_sub(1, std::memory_order_relaxed);
        }
        else if (job.kind == JOB_NET_IMPORT) {
            auto result = std::make_unique<BlockDataResult>();
            result->pos = job.pos;
            netImportOne(job.pos, job.netData, *result);
            {
                std::lock_guard<std::mutex> lk(m_blockDoneMutex);
                m_blockDone.push_back(std::move(result));
            }
            m_pending.fetch_sub(1, std::memory_order_relaxed);
        }
        else {
            // JOB_MESH
            auto result = std::make_unique<ChunkBuildResult>();
            result->pos = job.meshInput.pos;
            meshBuildOne(job.meshInput, *result);
            {
                std::lock_guard<std::mutex> lk(m_meshDoneMutex);
                m_meshDone.push_back(std::move(result));
            }
        }
    }
}

// ============================================================================
// Task 1：生成/加载方块数据（纯方块，不涉及光照）
// ============================================================================

void ChunkWorkerPool::buildOne(const glm::ivec2& pos, BlockDataResult& out) const {
    constexpr int VOL = ChunkConstants::CHUNK_VOLUME;

    auto tmp = std::make_unique<BlockState[]>(VOL);

    bool loadedFromDisk = false;
    if (m_saveManager) {
        loadedFromDisk = m_saveManager->loadChunk(pos, tmp.get());
    }
    if (!loadedFromDisk) {
        m_generator->fillChunkBuffer(tmp.get(), pos);
    }

    splitChunkBufferToBoxes(tmp.get(), out.boxes);
}

// ============================================================================
// 网络导入（客户端）：解压单 chunk 序列化字节 → 16 个 BlockBox
// ============================================================================

void ChunkWorkerPool::netImportOne(const glm::ivec2& pos,
    const std::vector<uint8_t>& serialized,
    BlockDataResult& out) {
    constexpr int VOL = ChunkConstants::CHUNK_VOLUME;
    constexpr int BLOCK_SIZE = Section::VOLUME * sizeof(BlockState);

    auto blockBuf = std::make_unique<BlockState[]>(VOL);
    std::memset(blockBuf.get(), 0, VOL * sizeof(BlockState));  // 默认 AIR

    const uint8_t* data = serialized.data();
    size_t len = serialized.size();
    size_t p = 0;

    if (len < 9) {
        splitChunkBufferToBoxes(blockBuf.get(), out.boxes);
        return;
    }
    p += 8;  // 4+4 字节 chunkX/Z
    uint8_t numSections = data[p++];

    std::vector<uint8_t> decompressBuf(BLOCK_SIZE);

    for (uint8_t i = 0; i < numSections && p < len; ++i) {
        if (p >= len) break;
        uint8_t sy = data[p++];
        if (p >= len) break;
        uint8_t flags = data[p++];

        if ((flags & ChunkSyncFormat::FLAG_HAS_DATA) == 0) {
            continue;  // 全空气 section
        }

        if (p + 2 > len) break;
        uint16_t dataLen = static_cast<uint16_t>(data[p])
            | (static_cast<uint16_t>(data[p + 1]) << 8);
        p += 2;
        if (p + dataLen > len) break;

        int decompressedSize = LZ4_decompress_safe(
            reinterpret_cast<const char*>(data + p),
            reinterpret_cast<char*>(decompressBuf.data()),
            dataLen, BLOCK_SIZE);
        p += dataLen;

        if (decompressedSize != BLOCK_SIZE) continue;

        if (sy < (ChunkConstants::CHUNK_HEIGHT / Section::HEIGHT)) {
            int sectionStart = sy * Section::VOLUME;
            std::memcpy(blockBuf.get() + sectionStart, decompressBuf.data(), BLOCK_SIZE);
        }
    }

    splitChunkBufferToBoxes(blockBuf.get(), out.boxes);
}

// ============================================================================
// Task 2：完整可见面生成（内部 + 全部边界，纯几何，不涉及光照）
// ============================================================================

namespace {
    // 从邻居 section 的 BlockBox 持读锁拷出一个横向边界面（一层）。
    void copyBoundaryLayerFromBox(const std::shared_ptr<BlockBox>& box,
        BlockFace face, BlockState* out) {
        constexpr int W = ChunkConstants::CHUNK_WIDTH;
        constexpr int D = ChunkConstants::CHUNK_DEPTH;
        constexpr int H = ChunkConstants::SECTION_HEIGHT;
        auto sidx = [](int x, int y, int z) { return (y * D + z) * W + x; };

        std::shared_lock<std::shared_mutex> lk(box->mutex);
        const auto& b = box->blocks;
        switch (face) {
        case RIGHT: // x = W-1 平面
            for (int y = 0; y < H; ++y)
                for (int z = 0; z < D; ++z) out[y * D + z] = b[sidx(W - 1, y, z)];
            break;
        case LEFT:  // x = 0 平面
            for (int y = 0; y < H; ++y)
                for (int z = 0; z < D; ++z) out[y * D + z] = b[sidx(0, y, z)];
            break;
        case FRONT: // z = D-1 平面
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) out[y * W + x] = b[sidx(x, y, D - 1)];
            break;
        case BACK:  // z = 0 平面
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) out[y * W + x] = b[sidx(x, y, 0)];
            break;
        default: break;
        }
    }
} // namespace

void ChunkWorkerPool::meshBuildOne(const MeshBuildInput& in, ChunkBuildResult& out) const {
    constexpr int W = ChunkConstants::CHUNK_WIDTH;
    constexpr int D = ChunkConstants::CHUNK_DEPTH;
    constexpr int SEC_H = Section::HEIGHT;

    // self 数据：直接把 Task 1 产出的 16 个 BlockBox 共享给生成出来的 Section（零拷贝）。
    for (int sy = 0; sy < ChunkBuildResult::SECTION_COUNT; ++sy) {
        out.sections[sy].setCoords(in.pos.x, in.pos.y, sy);
        out.sections[sy].setBox(in.self[sy]);
    }

    // 邻居边界层临时缓冲
    BlockState layer[SEC_H * (W > D ? W : D)];

    // 计算每个 section 的可见性（含垂直邻居 + 横向邻居）
    for (int sy = 0; sy < ChunkBuildResult::SECTION_COUNT; ++sy) {
        const Section* above = (sy + 1 < ChunkBuildResult::SECTION_COUNT)
            ? &out.sections[sy + 1] : nullptr;
        const Section* below = (sy > 0) ? &out.sections[sy - 1] : nullptr;

        // 内部 + 垂直边界
        out.sections[sy].rebuildVisibilityInternal(above, below);

        Section& sec = out.sections[sy];

        // +X 边界
        if (in.neighbors[0][sy]) {
            copyBoundaryLayerFromBox(in.neighbors[0][sy], LEFT, layer);
            for (int y = 0; y < SEC_H; ++y)
                for (int z = 0; z < D; ++z)
                    sec.updateFaceWithNeighbor(W - 1, y, z, RIGHT, layer[y * D + z]);
        }
        // -X 边界
        if (in.neighbors[1][sy]) {
            copyBoundaryLayerFromBox(in.neighbors[1][sy], RIGHT, layer);
            for (int y = 0; y < SEC_H; ++y)
                for (int z = 0; z < D; ++z)
                    sec.updateFaceWithNeighbor(0, y, z, LEFT, layer[y * D + z]);
        }
        // +Z 边界
        if (in.neighbors[2][sy]) {
            copyBoundaryLayerFromBox(in.neighbors[2][sy], BACK, layer);
            for (int y = 0; y < SEC_H; ++y)
                for (int x = 0; x < W; ++x)
                    sec.updateFaceWithNeighbor(x, y, D - 1, FRONT, layer[y * W + x]);
        }
        // -Z 边界
        if (in.neighbors[3][sy]) {
            copyBoundaryLayerFromBox(in.neighbors[3][sy], FRONT, layer);
            for (int y = 0; y < SEC_H; ++y)
                for (int x = 0; x < W; ++x)
                    sec.updateFaceWithNeighbor(x, y, 0, BACK, layer[y * W + x]);
        }
    }
}

// ============================================================================
// Task 3：3×3 区块完整光照 BFS
// ============================================================================

namespace {

constexpr int BW = ChunkConstants::CHUNK_WIDTH;   // 16
constexpr int BD = ChunkConstants::CHUNK_DEPTH;   // 16
constexpr int BH = ChunkConstants::CHUNK_HEIGHT;  // 256
constexpr int BSEC_H = ChunkConstants::SECTION_HEIGHT; // 16
constexpr int BSEC_COUNT = BH / BSEC_H;            // 16

// BFS 区域边界：3×3 chunk = 48×256×48 格
constexpr int REGION_W = BW * 3;   // 48
constexpr int REGION_D = BD * 3;   // 48

// 区块布局 → 9 个 chunk 在 3×3 网格中的偏移
// 布局（从 +Z 方向俯视）：
//   [0]=(-1,-1) [1]=(0,-1) [2]=(+1,-1)
//   [3]=(-1, 0) [4]=(0, 0) [5]=(+1, 0)    ← [4] 是 self
//   [6]=(-1,+1) [7]=(0,+1) [8]=(+1,+1)
constexpr int gridX[9] = { -1, 0, +1, -1, 0, +1, -1, 0, +1 };
constexpr int gridZ[9] = { -1, -1, -1, 0, 0, 0, +1, +1, +1 };
constexpr int SELF_IDX = 4;

// 9 个 chunk 的 BlockBox 引用数组
struct ChunkGrid {
    const ChunkBoxes* boxes[9] = {}; // nullptr 表示该 chunk 不存在
    int originX, originZ;             // self chunk 的世界原点（min 坐标）

    // 查询 world 位置的方块。超出所有 chunk 范围返回 BLOCK_STONE（视为墙）。
    BlockState query(int wx, int wy, int wz) const {
        // 先判断是否在 self chunk 的 3×3 范围内
        int relX = wx - originX;  // [-16, 31]
        int relZ = wz - originZ;  // [-16, 31]
        if (wy < 0 || wy >= BH) return BlockState{ BLOCK_STONE, ORIENT_NONE };
        // 确定所属的 grid 索引
        int gx = (relX + BW) / BW;  // 0, 1, or 2
        int gz = (relZ + BD) / BD;  // 0, 1, or 2
        if (gx < 0 || gx > 2 || gz < 0 || gz > 2) return BlockState{ BLOCK_STONE, ORIENT_NONE };
        int gi = gz * 3 + gx;
        const ChunkBoxes* cb = boxes[gi];
        if (!cb) return BlockState{ BLOCK_STONE, ORIENT_NONE };
        // 映射到该 chunk 的局部坐标
        int lx = (relX + BW) % BW;
        int lz = (relZ + BD) % BD;
        int sy = wy / BSEC_H;
        if (sy < 0 || sy >= BSEC_COUNT) return BlockState{ BLOCK_STONE, ORIENT_NONE };
        int ly = wy - sy * BSEC_H;
        const auto& box = (*cb)[sy];
        if (!box) return BlockState{};
        return box->blocks[(ly * BD + lz) * BW + lx];
    }
};

// 扫描 9 个 chunk 中所有发光方块，产出 self chunk 的 per-section 光源列表 + 光源世界坐标
void scanAllLightSources(const ChunkGrid& grid,
    std::vector<glm::ivec3>& worldSources,
    std::array<std::shared_ptr<std::vector<uint16_t>>, BSEC_COUNT>& outSelfSources) {
    for (int sy = 0; sy < BSEC_COUNT; ++sy) {
        outSelfSources[sy] = std::make_shared<std::vector<uint16_t>>();
    }

    for (int gi = 0; gi < 9; ++gi) {
        const ChunkBoxes* cb = grid.boxes[gi];
        if (!cb) continue;
        int chunkOriginX = grid.originX + gridX[gi] * BW;
        int chunkOriginZ = grid.originZ + gridZ[gi] * BD;

        for (int sy = 0; sy < BSEC_COUNT; ++sy) {
            const auto& box = (*cb)[sy];
            if (!box) continue;
            int baseY = sy * BSEC_H;
            for (int y = 0; y < BSEC_H; ++y) {
                for (int z = 0; z < BD; ++z) {
                    for (int x = 0; x < BW; ++x) {
                        BlockState state = box->blocks[(y * BD + z) * BW + x];
                        if (!isEmissive(state.type())) continue;

                        int wx = chunkOriginX + x;
                        int wy = baseY + y;
                        int wz = chunkOriginZ + z;
                        worldSources.push_back(glm::ivec3(wx, wy, wz));

                        // 仅 self chunk 的光源加入 per-section 列表
                        if (gi == SELF_IDX) {
                            outSelfSources[sy]->push_back(packChunkLightPos(x, wy, z));
                        }
                    }
                }
            }
        }
    }

    // 清除全空 section 的 shared_ptr
    for (int sy = 0; sy < BSEC_COUNT; ++sy) {
        if (outSelfSources[sy]->empty()) outSelfSources[sy].reset();
    }
}

} // namespace

void ChunkWorkerPool::lightBuildOne(const LightBuildInput& in, LightBuildResult& out) {
    out.pos = in.pos;

    // ── 1. 构建 9-chunk 查询网格 ──────────────────────────────────
    // Moore 邻居索引 mi → 3×3 grid 索引 gi：
    //   mi:  0(-1,-1) 1(0,-1) 2(+1,-1) 3(-1,0) 4(+1,0) 5(-1,+1) 6(0,+1) 7(+1,+1)
    //   gi:      0        1        2        3       5        6        7        8
    // 即 mi<4 时 gi=mi，mi>=4 时 gi=mi+1（跳过中间的 self=grid[4]）
    ChunkGrid grid;
    grid.originX = in.pos.x * BW;
    grid.originZ = in.pos.y * BD;
    grid.boxes[SELF_IDX] = &in.self;
    for (int mi = 0; mi < 8; ++mi) {
        int gi = (mi < 4) ? mi : (mi + 1);
        grid.boxes[gi] = &in.neighbors[mi];
    }

    // ── 2. 扫描光源 ──────────────────────────────────────────────
    std::vector<glm::ivec3> worldSources;
    scanAllLightSources(grid, worldSources, out.sectionLightSources);

    if (worldSources.empty()) return;  // 无光源 → 全零光照

    // ── 3. 初始化输出 buffer ─────────────────────────────────────
    for (int sy = 0; sy < BSEC_COUNT; ++sy) {
        out.sectionLightData[sy] = std::make_shared<SectionLightData>();
        out.sectionLightData[sy]->fill(0u);
    }

    const int selfMinX = grid.originX;
    const int selfMaxX = selfMinX + BW - 1;
    const int selfMinZ = grid.originZ;
    const int selfMaxZ = selfMinZ + BD - 1;

    // ── 4. 逐光源 BFS（每源小 visited 数组，分离传播门控与输出写入）──
    // 设计要点：
    //   - 每源独立 BFS。visited 按该源包围盒分配（≤31³≈30KB），远小于旧的
    //     全局 48×256×48（576KB）。
    //   - 传播门控（visited）与输出写入（writeLightMax）解耦：
    //     - 中心 chunk 内：max-clamp 写入 + visited 推入队列
    //     - 中心 chunk 外：visited 推入队列（允许 BFS 穿越邻居 chunk 格抵达中心）
    //     这修复了「光源在邻居 chunk → BFS 第一步就断掉」的跨区块 bug。
    //   - 逐分量 max（非 packed uint32 比较）保证多色光源正确混合。
    static const glm::ivec3 bfsDirs[6] = {
        {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
    };

    struct BfsEntry {
        glm::ivec3 pos;
        int        dist;
        float      srcRadius;
        glm::vec3  srcBrightness;
    };

    // 逐光源环形缓冲 + visited（thread_local，复用）
    static thread_local std::vector<BfsEntry> s_ring;
    s_ring.reserve(65536);
    static thread_local std::vector<uint8_t> s_visited;

    // 计算 world pos 是否在中心 chunk 范围内
    auto inCenterChunk = [&](int wx, int wy, int wz) -> bool {
        return wx >= selfMinX && wx <= selfMaxX &&
               wz >= selfMinZ && wz <= selfMaxZ &&
               wy >= 0 && wy < BH;
    };

    // 逐分量 max 写入（仅对中心 chunk 格有效）
    auto writeLightMax = [&](int wx, int wy, int wz, const glm::vec3& light) -> bool {
        if (!inCenterChunk(wx, wy, wz)) return false;
        int lx = wx - selfMinX;
        int lz = wz - selfMinZ;
        int sy = wy / BSEC_H;
        int ly = wy - sy * BSEC_H;
        int cellIdx = (ly * BD + lz) * BW + lx;
        uint32_t oldPacked = (*out.sectionLightData[sy])[cellIdx];
        float oldR = float(oldPacked & 0xFFu) / 255.0f;
        float oldG = float((oldPacked >> 8) & 0xFFu) / 255.0f;
        float oldB = float((oldPacked >> 16) & 0xFFu) / 255.0f;
        float newR = oldR > light.r ? oldR : light.r;
        float newG = oldG > light.g ? oldG : light.g;
        float newB = oldB > light.b ? oldB : light.b;
        if (newR <= oldR && newG <= oldG && newB <= oldB) return false;
        uint8_t pr = uint8_t(glm::clamp(int(newR * 255.0f + 0.5f), 0, 255));
        uint8_t pg = uint8_t(glm::clamp(int(newG * 255.0f + 0.5f), 0, 255));
        uint8_t pb = uint8_t(glm::clamp(int(newB * 255.0f + 0.5f), 0, 255));
        (*out.sectionLightData[sy])[cellIdx] =
            uint32_t(pr) | (uint32_t(pg) << 8) | (uint32_t(pb) << 16) | 0xFF000000u;
        return true;
    };

    // ── 逐光源 BFS 主循环 ────────────────────────────────────────
    for (const auto& src : worldSources) {
        BlockState st = grid.query(src.x, src.y, src.z);
        const LightDef& def = getLightDefForBlock(st.type());
        if (def.intensity <= 0.0f) continue;

        glm::vec3 srcLight = def.color * def.intensity;
        float srcRadius = def.radius;
        int srcR = (int)std::ceil(srcRadius);

        // ── 每源包围盒（限制在 3×3 chunk 内）──
        int bminX = max(src.x - srcR, selfMinX - BW);
        int bmaxX = min(src.x + srcR, selfMaxX + BW);
        int bminY = max(src.y - srcR, 0);
        int bmaxY = min(src.y + srcR, BH - 1);
        int bminZ = max(src.z - srcR, selfMinZ - BD);
        int bmaxZ = min(src.z + srcR, selfMaxZ + BD);
        int bw = bmaxX - bminX + 1;
        int bh = bmaxY - bminY + 1;
        int bd = bmaxZ - bminZ + 1;

        // 每源独立 visited（复用 thread_local，≤31³≈30KB）
        s_visited.assign((size_t)bw * bh * bd, 0);
        auto visitIdx = [&](int wx, int wy, int wz) -> int {
            return ((wy - bminY) * bd + (wz - bminZ)) * bw + (wx - bminX);
        };
        // 光源格：如在中心 chunk 则写入；无论如何入队
        writeLightMax(src.x, src.y, src.z, srcLight);
        s_visited[visitIdx(src.x, src.y, src.z)] = 1;

        s_ring.clear();
        s_ring.push_back(BfsEntry{ src, 0, srcRadius, srcLight });
        size_t head = 0;
        static constexpr size_t kMaxIterPerSource = 1 << 20; // ~1M 防御上限

        while (head < s_ring.size()) {
            if (head > kMaxIterPerSource) break;  // 防御性截断
            BfsEntry cur = s_ring[head++];

            if (cur.dist >= (int)cur.srcRadius) continue;

            int nextDist = cur.dist + 1;
            float falloff = 1.0f - (float)nextDist / cur.srcRadius;
            if (falloff <= 0.0f) continue;

            glm::vec3 nextLight = cur.srcBrightness * falloff;
            float maxComp = nextLight.r;
            if (nextLight.g > maxComp) maxComp = nextLight.g;
            if (nextLight.b > maxComp) maxComp = nextLight.b;
            if (maxComp < 0.005f) continue;

            for (const auto& dir : bfsDirs) {
                glm::ivec3 nextPos = cur.pos + dir;

                // 区域检查（3×3 chunk）
                int rx = nextPos.x - (selfMinX - BW);
                int rz = nextPos.z - (selfMinZ - BD);
                if (rx < 0 || rx >= REGION_W || rz < 0 || rz >= REGION_D) continue;
                if (nextPos.y < 0 || nextPos.y >= BH) continue;

                // visited 检查（传播门控）
                int idx = visitIdx(nextPos.x, nextPos.y, nextPos.z);
                if (s_visited[idx]) continue;

                // 方块透光检查
                BlockState ns = grid.query(nextPos.x, nextPos.y, nextPos.z);
                if (ns.type() != BLOCK_AIR && ns.type() != BLOCK_ERRER) {
                    if (!GetBlockProperties(ns.type()).isTransparent) {
                        s_visited[idx] = 1;  // 不透明块标记 visited，不传播
                        continue;
                    }
                }

                // 标记 visited + 入队（无论是否在中心 chunk）
                s_visited[idx] = 1;

                // 尝试写入光照（仅在中心 chunk 内有效）
                bool improved = writeLightMax(nextPos.x, nextPos.y, nextPos.z, nextLight);

                // 传播条件：光照有提升 或 格子在中心 chunk 外（需穿越到达中心）
                if (improved || !inCenterChunk(nextPos.x, nextPos.y, nextPos.z)) {
                    s_ring.push_back(BfsEntry{ nextPos, nextDist, srcRadius, cur.srcBrightness });
                }
            }
        }
    }

    // ── 5. 清除全零 section ──
    for (int sy = 0; sy < BSEC_COUNT; ++sy) {
        auto& sp = out.sectionLightData[sy];
        if (!sp) continue;
        bool any = false;
        for (uint32_t v : *sp) {
            if ((v & 0x00FFFFFFu) != 0) { any = true; break; }
        }
        if (!any) sp.reset();
    }
}
