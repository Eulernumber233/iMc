#pragma once
#include <variant>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include "../chunk/ChunkManager.h"

// 消息类型
enum class MessageType {
    CHUNK_LOAD_REQUEST,      // 加载区块请求
    CHUNK_UNLOAD_REQUEST,    // 卸载区块请求
    CHUNK_GENERATED,         // 区块已生成
    RENDER_DATA_READY,       // 渲染数据就绪
    PLAYER_MOVED,           // 玩家移动
    BLOCK_MODIFIED,         // 方块被修改
    EXIT                    // 退出信号
};

// 消息数据结构
struct ChunkLoadRequest {
    glm::ivec2 chunkPos;
    uint32_t priority;
};

struct ChunkUnloadRequest {
    glm::ivec2 chunkPos;
};

struct ChunkGenerated {
    glm::ivec2 chunkPos;
    std::shared_ptr<Chunk> chunk;
};

struct RenderDataReady {
    uint64_t frameId;
    std::shared_ptr<RenderData> data;
};

struct PlayerMoved {
    glm::vec3 position;
    uint64_t timestamp;
};

struct BlockModified {
    glm::ivec3 blockPos;
    BlockType oldType;
    BlockType newType;
};

using MessageData = std::variant<
    ChunkLoadRequest,
    ChunkUnloadRequest,
    ChunkGenerated,
    RenderDataReady,
    PlayerMoved,
    BlockModified
>;

// 消息结构
struct Message {
    MessageType type;
    MessageData data;
    uint64_t id;
    uint64_t timestamp;
};

// 线程安全的消息队列
class MessageQueue {
private:
    std::queue<Message> queue;
    std::mutex mutex;
    std::condition_variable condition;
    std::atomic<uint64_t> messageIdCounter{ 0 };

public:
    void push(MessageType type, MessageData data) {
        std::lock_guard<std::mutex> lock(mutex);
        uint64_t id = ++messageIdCounter;
        Message msg;
        msg.type = type;
        msg.data = std::move(data);
        msg.id = id;
        msg.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        queue.push(std::move(msg));
        condition.notify_one();
    }

    bool tryPop(Message& message) {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty()) return false;
        message = std::move(queue.front());
        queue.pop();
        return true;
    }

    void waitAndPop(Message& message) {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [this]() { return !queue.empty(); });
        message = std::move(queue.front());
        queue.pop();
    }

    bool empty() {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.empty();
    }

    size_t size(){
        std::lock_guard<std::mutex> lock(mutex);
        return queue.size();
    }
};