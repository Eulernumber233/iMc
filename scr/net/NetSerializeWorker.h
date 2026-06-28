#pragma once

#include "../enet/enet.h"
#include "../chunk/BlockBox.h"
#include <cstdint>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

// ============================================================================
// NetSerializeWorker: 专用网络序列化线程
// ----------------------------------------------------------------------------
// 把 chunk 序列化 + LZ4 压缩从主线程剥离到一个独立 worker 线程。
//
// 设计理由（见 docs/net_thread_design.md 阶段 1）：参考原版 MC 的子系统隔离实践
// ——网络收发/序列化与 worldgen/mesh 线程池分离，避免「玩家加入」时一次性提交几百个
// 序列化任务把 mesh 线程池饿死。同时本线程是阶段 2「独立网络线程」的种子。
//
// 安全性：序列化只读 BlockBox（自带 shared_mutex，读读共享）。主线程投递任务时拷
// shared_ptr<BlockBox>（引用计数 +1），即使该 chunk 在序列化期间被卸载，box 也活到
// 序列化结束，不悬挂。enet_peer_send 仍只在主线程做（取回完成结果后），故 ENetHost
// 单线程访问不变。
// ============================================================================

class NetSerializeWorker {
public:
    // 序列化任务：主线程产，worker 消费
    struct Job {
        int chunkX = 0, chunkZ = 0;
        ChunkBoxes boxes;                 // shared_ptr 数组，引用计数保活
        std::vector<ENetPeer*> targets;   // 发给哪些 peer（主线程算好相关性）
        // peer 指针的有效性由主线程在「取回完成结果时」对 alive set 复核。
    };

    // 完成结果：worker 产，主线程消费后 enet_peer_send
    struct Result {
        int chunkX = 0, chunkZ = 0;
        std::vector<ENetPeer*> targets;
        std::vector<uint8_t> payload;     // 已序列化 + LZ4 压缩的「裸」chunk 数据
                                          // （不含 NetMessage header，发送时由主线程包一层）
    };

    NetSerializeWorker() = default;
    ~NetSerializeWorker();

    NetSerializeWorker(const NetSerializeWorker&) = delete;
    NetSerializeWorker& operator=(const NetSerializeWorker&) = delete;

    // 启动 / 停止 worker 线程
    void start();
    void stop();
    bool isRunning() const { return m_running.load(std::memory_order_relaxed); }

    // 主线程投递一个序列化任务（move 进队列）
    void submit(Job&& job);

    // 主线程每帧取走至多 maxResults 个完成结果（O(结果数)，不阻塞）。
    // 返回实际取走的个数。
    void drainResults(std::vector<Result>& out, size_t maxResults);

    // 当前队列待处理任务数（监控用）
    size_t pendingCount() const;

private:
    void workerMain();

    // 把一个 chunk 的 ChunkBoxes 序列化 + LZ4 压缩为裸 payload。
    // 与原 NetChunkSync::serializeChunkFromBlocks 同格式：
    //   [chunkX:i32][chunkZ:i32][numSections:u8]
    //   每 section: [sectionY:u8][flags:u8]( [dataLen:u16][lz4 blocks] )
    static void serialize(const Job& job, std::vector<uint8_t>& out);

    std::thread m_thread;
    std::atomic<bool> m_stop{ false };
    std::atomic<bool> m_running{ false };

    // 任务队列
    mutable std::mutex m_jobMutex;
    std::condition_variable m_jobCV;
    std::deque<Job> m_jobs;

    // 完成队列
    std::mutex m_doneMutex;
    std::deque<Result> m_done;
};
