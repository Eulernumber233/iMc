#include "ChunkWorkerPool.h"
#include "Chunk.h"
#include "../generate/TerrainGenerator.h"
#include "../save/ChunkSaveManager.h"
#include "../net/lz4.h"
#include "../net/NetChunkSync.h"  // ChunkSyncFormat::FLAG_HAS_DATA
#include <iostream>
#include <cstring>
#include <deque>
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
        std::lock_guard<std::mutex> lk(m_blockDoneMutex);
        m_blockDone.clear();
    }
    {
        std::lock_guard<std::mutex> lk(m_meshDoneMutex);
        m_meshDone.clear();
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
    // 与 JOB_BUILD 一样计入 pending（产出走 block 完成队列，主线程同一条 integrate 路径消化）
    m_pending.fetch_add(1, std::memory_order_relaxed);
    m_jobCV.notify_one();
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

void ChunkWorkerPool::workerMain() {
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(m_jobMutex);
            m_jobCV.wait(lk, [this] { return m_stop.load() || !m_jobs.empty(); });
            if (m_stop.load() && m_jobs.empty()) return;
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
        } else if (job.kind == JOB_NET_IMPORT) {
            // 客户端网络导入：解压 + 切片在 worker 做，产出与 JOB_BUILD 同型，走同一完成队列。
            auto result = std::make_unique<BlockDataResult>();
            result->pos = job.pos;
            netImportOne(job.pos, job.netData, *result);
            {
                std::lock_guard<std::mutex> lk(m_blockDoneMutex);
                m_blockDone.push_back(std::move(result));
            }
            m_pending.fetch_sub(1, std::memory_order_relaxed);
        } else {
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
// Task 1：生成/加载方块数据（不构建 mesh）
// ============================================================================

namespace {
    // 扫描整 chunk buffer 中的发光方块，生成光源列表（在 worker 线程执行）
    void scanLightSources(const BlockState* buf, const glm::ivec2& pos,
                          std::vector<LightSource>& out) {
        constexpr int W = ChunkConstants::CHUNK_WIDTH;
        constexpr int D = ChunkConstants::CHUNK_DEPTH;
        constexpr int H = ChunkConstants::CHUNK_HEIGHT;
        for (int y = 0; y < H; ++y) {
            for (int z = 0; z < D; ++z) {
                for (int x = 0; x < W; ++x) {
                    BlockState state = buf[(y * D + z) * W + x];
                    BlockProperties props = GetBlockProperties(state.type());
                    if (props.emissive > 0.0f) {
                        glm::ivec3 worldPos(pos.x * W + x, y, pos.y * D + z);
                        LightSource src = getBlockLightDef(state.type(), worldPos);
                        if (src.intensity > 0.0f) {
                            out.push_back(src);
                        }
                    }
                }
            }
        }
    }

    // ── 区块内 BFS 光照传播（worker 线程，无需任何锁）────────────────────
    // 从本区块内的光源做 BFS，范围限定在 chunk 世界坐标边界内。
    // 结果写入 out，每个 section 分配 shared_ptr<SectionLightData>（与 BlockBox 模式一致）。
    void propagateIntraChunkLights(const BlockState* chunkBuf,
                                    const glm::ivec2& chunkPos,
                                    const std::vector<LightSource>& sources,
                                    ChunkLightData& out) {
        constexpr int W = ChunkConstants::CHUNK_WIDTH;   // 16
        constexpr int D = ChunkConstants::CHUNK_DEPTH;   // 16
        constexpr int H = ChunkConstants::CHUNK_HEIGHT;  // 256
        constexpr int SEC_H = ChunkConstants::SECTION_HEIGHT; // 16
        constexpr int SEC_COUNT = H / SEC_H;             // 16

        if (sources.empty()) return;

        // 为每个 section 分配零初始化数据
        for (int sy = 0; sy < SEC_COUNT; ++sy) {
            out[sy] = std::make_shared<SectionLightData>();
            out[sy]->fill(0u);
        }

        // chunk 世界坐标范围
        const int worldMinX = chunkPos.x * W;
        const int worldMaxX = worldMinX + W - 1;
        const int worldMinZ = chunkPos.y * D;
        const int worldMaxZ = worldMinZ + D - 1;

        // BlockQuery：直接从 chunkBuf 读取，无锁。超出 chunk 范围视为不透明。
        auto blockQuery = [&](int wx, int wy, int wz) -> BlockState {
            if (wx < worldMinX || wx > worldMaxX ||
                wz < worldMinZ || wz > worldMaxZ ||
                wy < 0 || wy >= H) {
                return BlockState{ BLOCK_STONE, ORIENT_NONE };
            }
            int lx = wx - worldMinX;
            int lz = wz - worldMinZ;
            return chunkBuf[(wy * D + lz) * W + lx];
        };

        // 写入光照值（max 混合）
        auto writeLight = [&](int wx, int wy, int wz, const glm::vec3& light) {
            if (wx < worldMinX || wx > worldMaxX ||
                wz < worldMinZ || wz > worldMaxZ) return;
            if (wy < 0 || wy >= H) return;
            int lx = wx - worldMinX;
            int lz = wz - worldMinZ;
            int sy = wy / SEC_H;
            if (wy < 0) sy--;
            if (sy < 0 || sy >= SEC_COUNT) return;
            int ly = wy - sy * SEC_H;
            uint8_t r = uint8_t(glm::clamp(int(light.r * 255.0f + 0.5f), 0, 255));
            uint8_t g = uint8_t(glm::clamp(int(light.g * 255.0f + 0.5f), 0, 255));
            uint8_t b = uint8_t(glm::clamp(int(light.b * 255.0f + 0.5f), 0, 255));
            uint32_t packed = uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | 0xFF000000u;
            int cellIdx = (ly * D + lz) * W + lx;
            uint32_t& cell = (*out[sy])[cellIdx];
            if (packed > cell) cell = packed;
        };

        static const glm::ivec3 bfsDirs[6] = {
            {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
        };

        struct BfsEntry { glm::ivec3 pos; int dist; };

        for (const auto& src : sources) {
            if (src.intensity <= 0.0f) continue;

            std::deque<BfsEntry> queue;
            std::unordered_set<uint64_t> visited;

            auto packPos = [](const glm::ivec3& p) -> uint64_t {
                uint64_t x = (uint64_t)(uint32_t)p.x & 0xFFFFFFu;
                uint64_t y = (uint64_t)(uint32_t)(p.y & 0xFFF);
                uint64_t z = (uint64_t)(uint32_t)p.z & 0xFFFFFFu;
                return x | (y << 24) | (z << 36);
            };

            writeLight(src.pos.x, src.pos.y, src.pos.z, src.color * src.intensity);
            queue.push_back({ src.pos, 0 });
            visited.insert(packPos(src.pos));

            while (!queue.empty()) {
                auto [curPos, curDist] = queue.front();
                queue.pop_front();

                if (curDist >= (int)src.radius) continue;

                int nextDist = curDist + 1;
                float d = (float)nextDist;
                float falloff = 1.0f - d / src.radius;
                if (falloff <= 0.0f) continue;
                glm::vec3 nextLight = src.color * src.intensity * falloff;

                for (const auto& dir : bfsDirs) {
                    glm::ivec3 nextPos = curPos + dir;
                    auto key = packPos(nextPos);
                    if (visited.count(key)) continue;

                    BlockState ns = blockQuery(nextPos.x, nextPos.y, nextPos.z);
                    if (ns.type() != BLOCK_AIR && ns.type() != BLOCK_ERRER) {
                        BlockProperties np = GetBlockProperties(ns.type());
                        if (!np.isTransparent) {
                            visited.insert(key);
                            continue;
                        }
                    }

                    visited.insert(key);
                    writeLight(nextPos.x, nextPos.y, nextPos.z, nextLight);
                    queue.push_back({ nextPos, nextDist });
                }
            }
        }

        // 清除全零 section 的 shared_ptr（视为无光照）
        for (int sy = 0; sy < SEC_COUNT; ++sy) {
            if (!out[sy]) continue;
            bool any = false;
            for (uint32_t v : *out[sy]) {
                if (v != 0) { any = true; break; }
            }
            if (!any) out[sy].reset();
        }
    }

} // namespace

void ChunkWorkerPool::buildOne(const glm::ivec2& pos, BlockDataResult& out) const {
    constexpr int VOL = ChunkConstants::CHUNK_VOLUME;

    // 地形生成器 / 存档接口按「整 chunk 连续 buffer」工作（布局 (worldY*D+z)*W+x）。
    // 先填这个临时大 buffer，再切片进 16 个 section BlockBox。
    // 切片发生在 worker 线程，不占主线程；之后 Task 2 全程零拷贝共享这些 box。
    auto tmp = std::make_unique<BlockState[]>(VOL);

    bool loadedFromDisk = false;
    if (m_saveManager) {
        loadedFromDisk = m_saveManager->loadChunk(pos, tmp.get());
    }
    if (!loadedFromDisk) {
        m_generator->fillChunkBuffer(tmp.get(), pos);
    }

    splitChunkBufferToBoxes(tmp.get(), out.boxes);

    // 扫描发光方块（萤石等 emissive > 0），产出光源列表供主线程注册
    scanLightSources(tmp.get(), pos, out.lightSources);

    // Task 1 区块内 BFS：从本区块光源出发，仅在 chunk 范围内洪水填充光照。
    // 无锁（worker 独享此 chunk buffer），结果供 Task 2 跨区块边界 BFS 使用。
    propagateIntraChunkLights(tmp.get(), pos, out.lightSources, out.sectionLightData);
}

// ============================================================================
// 网络导入（客户端）：解压单 chunk 序列化字节 → 16 个 BlockBox
// ============================================================================

void ChunkWorkerPool::netImportOne(const glm::ivec2& pos,
                                   const std::vector<uint8_t>& serialized,
                                   BlockDataResult& out) {
    // 格式（与 NetSerializeWorker::serialize 一致）：
    //   [chunkX:i32][chunkZ:i32][numSections:u8]
    //   每 section：[sy:u8][flags:u8]( [dataLen:u16][lz4 blocks] )
    // 解压进整 chunk buffer（默认 AIR），再切片成 16 个 box。
    constexpr int VOL = ChunkConstants::CHUNK_VOLUME;
    constexpr int BLOCK_SIZE = Section::VOLUME * sizeof(BlockState);

    auto blockBuf = std::make_unique<BlockState[]>(VOL);
    std::memset(blockBuf.get(), 0, VOL * sizeof(BlockState));  // 默认 AIR

    const uint8_t* data = serialized.data();
    size_t len = serialized.size();
    size_t p = 0;

    // 跳过 chunkX/Z（投递时已知 pos，这里只为对齐格式），读 numSections
    if (len < 9) {
        // 数据损坏：boxes 留空（全 nullptr），主线程 integrate 仍会把它当 block-ready 空 chunk。
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

        if (decompressedSize != BLOCK_SIZE) continue;  // 解压失败，跳过此 section

        if (sy < (ChunkConstants::CHUNK_HEIGHT / Section::HEIGHT)) {
            int sectionStart = sy * Section::VOLUME;
            std::memcpy(blockBuf.get() + sectionStart, decompressBuf.data(), BLOCK_SIZE);
        }
    }

    splitChunkBufferToBoxes(blockBuf.get(), out.boxes);

    // 扫描发光方块（网络导入的 chunk 可能含玩家放置的萤石）
    scanLightSources(blockBuf.get(), pos, out.lightSources);

    // 区块内 BFS 光照传播（与 buildOne 一致）
    propagateIntraChunkLights(blockBuf.get(), pos, out.lightSources, out.sectionLightData);
}

// ============================================================================
// Task 2：完整可见面生成（内部 + 全部边界）
// ============================================================================

namespace {
    // 从邻居 section 的 BlockBox 持读锁拷出一个横向边界面（一层）。
    // 与玩家修改该 box 的写锁互斥；与其他 worker 的读共享。
    // 输出布局：RIGHT/LEFT 按 [y*DEPTH+z]；FRONT/BACK 按 [y*WIDTH+x]。
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
// ── Task 2 跨区块边界光照传播 ─────────────────────────────────────────
// 1. 浅拷贝 selfLightData（shared_ptr，仅指针复制）
// 2. 导入 4 邻居边界光照层（1 步衰减写入 self 边界格，max 混合）
// 3. 从所有非零格 BFS 使边界光向内正确传播（查 self 方块，无锁）
static void propagateCrossChunkLights(const MeshBuildInput& in,
                                       ChunkBuildResult& out) {
    constexpr int W = ChunkConstants::CHUNK_WIDTH;
    constexpr int D = ChunkConstants::CHUNK_DEPTH;
    constexpr int SEC_H = ChunkConstants::SECTION_HEIGHT;
    constexpr int SEC_COUNT = ChunkConstants::CHUNK_HEIGHT / SEC_H;
    constexpr int CELLS = W * SEC_H * D; // 4096
    constexpr float kCrossoverRadius = 8.0f;

    // ── 1. 基底：浅拷贝 selfLightData（shared_ptr 共享，无数据拷贝）──
    for (int sy = 0; sy < SEC_COUNT; ++sy) {
        if (in.selfLightData[sy]) {
            out.sectionLightData[sy] = in.selfLightData[sy];
        }
    }

    // chunk 世界坐标范围
    const int worldMinX = in.pos.x * W;
    const int worldMinZ = in.pos.y * D;

    // 从 self box 读方块（无锁）
    auto selfBlock = [&](int lx, int ly, int lz, int sy) -> BlockState {
        if (!in.self[sy]) return BlockState{ BLOCK_AIR, ORIENT_NONE };
        return in.self[sy]->blocks[(ly * D + lz) * W + lx];
    };

    // 确保 section 已分配并写光照（max 混合）；已存在则直接写
    auto writeSelfLight = [&](int lx, int ly, int lz, int sy, const glm::vec3& light) {
        if (lx < 0 || lx >= W || lz < 0 || lz >= D || sy < 0 || sy >= SEC_COUNT) return;
        if (ly < 0 || ly >= SEC_H) return;
        if (!out.sectionLightData[sy]) {
            out.sectionLightData[sy] = std::make_shared<SectionLightData>();
            out.sectionLightData[sy]->fill(0u);
        }
        uint8_t r = uint8_t(glm::clamp(int(light.r * 255.0f + 0.5f), 0, 255));
        uint8_t g = uint8_t(glm::clamp(int(light.g * 255.0f + 0.5f), 0, 255));
        uint8_t b = uint8_t(glm::clamp(int(light.b * 255.0f + 0.5f), 0, 255));
        uint32_t packed = uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | 0xFF000000u;
        int cellIdx = (ly * D + lz) * W + lx;
        uint32_t& cell = (*out.sectionLightData[sy])[cellIdx];
        if (packed > cell) cell = packed;
    };

    // 读 self 光照值
    auto readSelfLight = [&](int lx, int ly, int lz, int sy) -> glm::vec3 {
        if (lx < 0 || lx >= W || lz < 0 || lz >= D || sy < 0 || sy >= SEC_COUNT) return glm::vec3(0);
        if (ly < 0 || ly >= SEC_H) return glm::vec3(0);
        auto& sp = out.sectionLightData[sy];
        if (!sp) return glm::vec3(0);
        int cellIdx = (ly * D + lz) * W + lx;
        uint32_t v = (*sp)[cellIdx];
        return glm::vec3(float(v & 0xFFu) / 255.0f,
                          float((v >> 8) & 0xFFu) / 255.0f,
                          float((v >> 16) & 0xFFu) / 255.0f);
    };

    // ── 2. 导入 4 邻居边界光照 ──
    static const float kOneStepFalloff = 0.90f;

    for (int d = 0; d < 4; ++d) {
        for (int sy = 0; sy < SEC_COUNT; ++sy) {
            const auto& boundary = in.neighborBoundaryLight[d][sy];
            if (boundary.empty()) continue;

            if (d == 0) {
                for (int y = 0; y < SEC_H; ++y)
                    for (int z = 0; z < D; ++z) {
                        uint32_t v = boundary[y * D + z];
                        if ((v & 0x00FFFFFFu) == 0) continue;
                        glm::vec3 nb(float(v & 0xFFu)/255.f, float((v>>8)&0xFFu)/255.f, float((v>>16)&0xFFu)/255.f);
                        writeSelfLight(W - 1, y, z, sy, nb * kOneStepFalloff);
                    }
            } else if (d == 1) {
                for (int y = 0; y < SEC_H; ++y)
                    for (int z = 0; z < D; ++z) {
                        uint32_t v = boundary[y * D + z];
                        if ((v & 0x00FFFFFFu) == 0) continue;
                        glm::vec3 nb(float(v & 0xFFu)/255.f, float((v>>8)&0xFFu)/255.f, float((v>>16)&0xFFu)/255.f);
                        writeSelfLight(0, y, z, sy, nb * kOneStepFalloff);
                    }
            } else if (d == 2) {
                for (int y = 0; y < SEC_H; ++y)
                    for (int x = 0; x < W; ++x) {
                        uint32_t v = boundary[y * W + x];
                        if ((v & 0x00FFFFFFu) == 0) continue;
                        glm::vec3 nb(float(v & 0xFFu)/255.f, float((v>>8)&0xFFu)/255.f, float((v>>16)&0xFFu)/255.f);
                        writeSelfLight(x, y, D - 1, sy, nb * kOneStepFalloff);
                    }
            } else {
                for (int y = 0; y < SEC_H; ++y)
                    for (int x = 0; x < W; ++x) {
                        uint32_t v = boundary[y * W + x];
                        if ((v & 0x00FFFFFFu) == 0) continue;
                        glm::vec3 nb(float(v & 0xFFu)/255.f, float((v>>8)&0xFFu)/255.f, float((v>>16)&0xFFu)/255.f);
                        writeSelfLight(x, y, 0, sy, nb * kOneStepFalloff);
                    }
            }
        }
    }

    // ── 3. BFS 向内传播 ──
    struct VSource { glm::ivec3 pos; glm::vec3 color; };
    std::vector<VSource> seeds;
    seeds.reserve(4096);

    for (int sy = 0; sy < SEC_COUNT; ++sy) {
        auto& sp = out.sectionLightData[sy];
        if (!sp) continue;
        int baseY = sy * SEC_H;
        for (int i = 0; i < CELLS; ++i) {
            uint32_t v = (*sp)[i];
            if ((v & 0x00FFFFFFu) == 0) continue;
            int lx = i % W, lz = (i / W) % D, ly = i / (W * D);
            seeds.push_back({ glm::ivec3(worldMinX + lx, baseY + ly, worldMinZ + lz),
                              glm::vec3(float(v & 0xFFu)/255.f, float((v>>8)&0xFFu)/255.f, float((v>>16)&0xFFu)/255.f) });
        }
    }
    if (seeds.empty()) return;

    static const glm::ivec3 bfsDirs[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    struct BfsEntry { glm::ivec3 pos; int dist; };
    std::unordered_set<uint64_t> visited;
    auto packPos = [](const glm::ivec3& p) -> uint64_t {
        return ((uint64_t)(uint32_t)p.x & 0xFFFFFFu) | (((uint64_t)(uint32_t)(p.y & 0xFFF)) << 24) | (((uint64_t)(uint32_t)p.z & 0xFFFFFFu) << 36);
    };

    std::deque<BfsEntry> queue;
    for (const auto& seed : seeds) {
        auto key = packPos(seed.pos);
        if (visited.count(key)) continue;
        visited.insert(key);
        queue.push_back({ seed.pos, 0 });

        while (!queue.empty()) {
            auto [curPos, curDist] = queue.front();
            queue.pop_front();
            if (curDist >= (int)kCrossoverRadius) continue;

            int curSY = curPos.y / SEC_H;
            if (curPos.y < 0) curSY--;
            int curLY = curPos.y - curSY * SEC_H;
            glm::vec3 curLight = readSelfLight(curPos.x - worldMinX, curLY, curPos.z - worldMinZ, curSY);
            float maxComp = (glm::max)(curLight.r, (glm::max)(curLight.g, curLight.b));
            if (maxComp < 0.005f) continue;

            int nextDist = curDist + 1;
            float falloff = 1.0f - (float)nextDist / kCrossoverRadius;
            if (falloff <= 0.0f) continue;
            glm::vec3 nextLight = curLight * falloff;

            for (const auto& dir : bfsDirs) {
                glm::ivec3 nextPos = curPos + dir;
                auto nkey = packPos(nextPos);
                if (visited.count(nkey)) continue;

                int nlx = nextPos.x - worldMinX, nlz = nextPos.z - worldMinZ;
                if (nlx < 0 || nlx >= W || nlz < 0 || nlz >= D) continue;
                int nsy = nextPos.y / SEC_H;
                if (nextPos.y < 0) nsy--;
                if (nsy < 0 || nsy >= SEC_COUNT) continue;
                int nly = nextPos.y - nsy * SEC_H;
                if (nly < 0 || nly >= SEC_H) continue;

                BlockState ns = selfBlock(nlx, nly, nlz, nsy);
                if (ns.type() != BLOCK_AIR && ns.type() != BLOCK_ERRER) {
                    BlockProperties np = GetBlockProperties(ns.type());
                    if (!np.isTransparent) { visited.insert(nkey); continue; }
                }
                visited.insert(nkey);
                writeSelfLight(nlx, nly, nlz, nsy, nextLight);
                queue.push_back({ nextPos, nextDist });
            }
        }
    }

    // ── 4. 清除全零 section ──
    for (int sy = 0; sy < SEC_COUNT; ++sy) {
        auto& sp = out.sectionLightData[sy];
        if (!sp) continue;
        bool any = false;
        for (uint32_t v : *sp) { if ((v & 0x00FFFFFFu) != 0) { any = true; break; } }
        if (!any) sp.reset();
    }
}



} // namespace

void ChunkWorkerPool::meshBuildOne(const MeshBuildInput& in, ChunkBuildResult& out) const {
    constexpr int W = ChunkConstants::CHUNK_WIDTH;
    constexpr int D = ChunkConstants::CHUNK_DEPTH;
    constexpr int SEC_H = Section::HEIGHT;

    // self 数据：直接把 Task 1 产出的 16 个 BlockBox 共享给生成出来的 Section（零拷贝）。
    // self 在本 chunk LOADED 前玩家无法触碰，故此处无需加锁。
    for (int sy = 0; sy < ChunkBuildResult::SECTION_COUNT; ++sy) {
        out.sections[sy].setCoords(in.pos.x, in.pos.y, sy);
        out.sections[sy].setBox(in.self[sy]);
    }

    // 邻居边界层临时缓冲：横向边界一层 = HEIGHT(=16) × {WIDTH 或 DEPTH}(=16) = 256 格。
    // 由 copyBoundaryLayerLocked 持邻居读锁填充：RIGHT/LEFT 按 [y*DEPTH+z]，FRONT/BACK 按 [y*WIDTH+x]。
    BlockState layer[SEC_H * (W > D ? W : D)];

    // 计算每个 section 的可见性（含垂直邻居 + 横向邻居）
    for (int sy = 0; sy < ChunkBuildResult::SECTION_COUNT; ++sy) {
        const Section* above = (sy + 1 < ChunkBuildResult::SECTION_COUNT)
            ? &out.sections[sy + 1] : nullptr;
        const Section* below = (sy > 0) ? &out.sections[sy - 1] : nullptr;

        // 内部 + 垂直边界
        out.sections[sy].rebuildVisibilityInternal(above, below);

        Section& sec = out.sections[sy];

        // +X 边界 (我 x=W-1 的 RIGHT 面 ←→ 邻居[0] x=0 的 LEFT 面)
        if (in.neighbors[0][sy]) {
            copyBoundaryLayerFromBox(in.neighbors[0][sy], LEFT, layer);
            for (int y = 0; y < SEC_H; ++y)
                for (int z = 0; z < D; ++z)
                    sec.updateFaceWithNeighbor(W - 1, y, z, RIGHT, layer[y * D + z]);
        }
        // -X 边界 (我 x=0 的 LEFT 面 ←→ 邻居[1] x=W-1 的 RIGHT 面)
        if (in.neighbors[1][sy]) {
            copyBoundaryLayerFromBox(in.neighbors[1][sy], RIGHT, layer);
            for (int y = 0; y < SEC_H; ++y)
                for (int z = 0; z < D; ++z)
                    sec.updateFaceWithNeighbor(0, y, z, LEFT, layer[y * D + z]);
        }
        // +Z 边界 (我 z=D-1 的 FRONT 面 ←→ 邻居[2] z=0 的 BACK 面)
        if (in.neighbors[2][sy]) {
            copyBoundaryLayerFromBox(in.neighbors[2][sy], BACK, layer);
            for (int y = 0; y < SEC_H; ++y)
                for (int x = 0; x < W; ++x)
                    sec.updateFaceWithNeighbor(x, y, D - 1, FRONT, layer[y * W + x]);
        }
        // -Z 边界 (我 z=0 的 BACK 面 ←→ 邻居[3] z=D-1 的 FRONT 面)
        if (in.neighbors[3][sy]) {
            copyBoundaryLayerFromBox(in.neighbors[3][sy], FRONT, layer);
            for (int y = 0; y < SEC_H; ++y)
                for (int x = 0; x < W; ++x)
                    sec.updateFaceWithNeighbor(x, y, 0, BACK, layer[y * W + x]);
        }
    }

    // ── 跨区块边界光照传播 ──
    // 以 Task 1 区块内 BFS 结果为基底，导入 4 邻居的边界光照层，
    // 在 self chunk 内再做一次 BFS 使边界光向内正确传播。
    propagateCrossChunkLights(in, out);
}
