#pragma once

#include "NetCommon.h"
#include "../enet/enet.h"
#include <vector>
#include <deque>
#include <cstdint>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

// 网络事件（阶段 2：data 由网络线程从 ENetPacket 拷贝进来并立即 destroy packet，
// 故本结构【自持】数据，跨线程传递不依赖 ENetPacket 生命周期）。
struct NetEvent {
    enum Type : uint8_t { None, Connected, Disconnected, Data };
    Type type = None;
    ENetPeer* peer = nullptr;          // 不透明 key：主线程不解引用，仅作 map key + 回传
    std::vector<uint8_t> data;         // 仅 Data 事件有效，自持拷贝
};

// ============================================================================
// NetTransport: ENet 薄封装 + 专用网络线程（Netty 模型）
// ----------------------------------------------------------------------------
// 阶段 2：ENetHost 及所有 enet_host_service/flush/peer_send/packet_* 调用都在专用网络
// 线程上执行。主线程只通过两个加锁队列与之通信：
//   - 出站队列 m_outQueue：主线程把「已编码消息 + 目标 peer + 通道/可靠性」塞进去，
//     网络线程取出做 enet_peer_send。
//   - 入站队列 m_inQueue：网络线程 enet_host_service 收到的事件（连接/断开/数据）解析后
//     塞进去，主线程 drainInbound 取走 dispatch。
//
// ENetPeer* 的生命周期由网络线程管（ENet 内部 host->peers[] 数组，host 销毁前不释放），
// 主线程把它当不透明 key 传递，对已断开 peer 的 send 是安全 no-op。
//
// 调用约束：
//  - init / createServer / createClient / connect / destroyHost / shutdown 必须在
//    网络线程【未启动】时（startNetThread 之前 / stopNetThread 之后）由主线程调用。
//  - sendReliable / sendUnreliable / drainInbound 在网络线程运行期间由主线程调用，线程安全。
//  - flush() 在阶段 2 为信号唤醒（网络线程每轮自动 flush），主线程调用安全。
// ============================================================================

class NetTransport {
public:
    NetTransport() = default;
    ~NetTransport();

    // 禁止拷贝
    NetTransport(const NetTransport&) = delete;
    NetTransport& operator=(const NetTransport&) = delete;

    // ---- 生命周期 ----
    bool init();
    void shutdown();

    // ---- Host 创建（主线程，网络线程启动前）----
    bool createServer(uint16_t port, uint32_t maxClients = NetConstants::DEFAULT_MAX_CLIENTS);
    bool createClient();
    void destroyHost();

    // ---- 连接 (客户端，网络线程启动前)----
    ENetPeer* connect(const char* ip, uint16_t port);

    // ---- 网络线程 ----
    void startNetThread();
    void stopNetThread();
    bool isNetThreadRunning() const { return m_threadRunning.load(std::memory_order_relaxed); }

    // ---- 断开 ----
    void disconnectPeer(ENetPeer* peer);
    void disconnectAll();

    // ---- 入站：主线程每帧取走网络线程解析好的事件 ----
    void drainInbound(std::vector<NetEvent>& outEvents);

    // ---- 发送（线程安全：入出站队列）----
    void sendReliable(ENetPeer* peer, const void* data, size_t len);
    void sendUnreliable(ENetPeer* peer, const void* data, size_t len);
    void flush();  // 阶段 2：唤醒网络线程尽快发送（每轮自动 flush，故多为信号语义）

    // ---- 状态 ----
    bool isServer() const { return m_isServer; }
    ENetHost* getHost() { return m_host; }
    bool isInitialized() const { return m_initialized; }

private:
    // 出站消息：主线程产，网络线程消费
    struct OutboundMsg {
        ENetPeer* peer = nullptr;
        std::vector<uint8_t> data;
        bool reliable = true;
    };

    void netThreadMain();
    // 网络线程内：把就绪事件收进入站队列（持 m_inMutex）
    void serviceOnce(uint32_t timeoutMs);

    ENetHost* m_host = nullptr;
    bool m_isServer = false;
    bool m_initialized = false;

    // 网络线程
    std::thread m_netThread;
    std::atomic<bool> m_threadStop{ false };
    std::atomic<bool> m_threadRunning{ false };

    // 出站队列
    std::mutex m_outMutex;
    std::condition_variable m_outCV;
    std::deque<OutboundMsg> m_outQueue;

    // 入站队列
    std::mutex m_inMutex;
    std::deque<NetEvent> m_inQueue;
};
