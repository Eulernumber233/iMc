// rendering/render_data_manager.hpp
#pragma once
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "../chunk/BlockType.h"
enum class DataState {
    CLEAN,      // 数据是干净的，可以直接渲染
    DIRTY,      // 数据已过期，需要重新计算
    UPDATING    // 数据正在后台计算中
};
class RenderDataManager {
private:
    // 线程安全的双缓冲结构
    struct RenderBuffer {
        std::unordered_map<BlockFaceType, std::vector<glm::mat4>> instanceData;
        std::atomic<uint32_t> readyCount{ 0 };  // 已准备好的数据块数量
        std::atomic<bool> isReady{ false };     // 整个缓冲区是否就绪
        uint64_t frameId{ 0 };                  // 对应的帧ID
    };

    // 双缓冲区
    RenderBuffer buffers[2];
    std::atomic<int> currentReadBuffer{ 0 };
    std::atomic<int> currentWriteBuffer{ 1 };

    // 区块数据变更队列
    struct ChunkUpdate {
        uint64_t chunkId;
        glm::ivec2 chunkPos;
        DataState state;
        std::vector<glm::mat4> newMatrices;  // 如果状态是增量更新
    };

    std::vector<ChunkUpdate> pendingUpdates;
    std::mutex updateMutex;
    std::condition_variable updateCV;

    // 工作线程
    std::vector<std::thread> workerThreads;
    std::atomic<bool> running{ false };

    // 按材质分组的GPU缓冲区
    struct GPUBuffer {
        GLuint vbo{ 0 };
        GLuint instanceVbo{ 0 };
        size_t capacity{ 0 };
        size_t size{ 0 };

        void ensureCapacity(size_t required) {
            if (required <= capacity) return;

            // 扩大缓冲区（按1.5倍增长）
            size_t newCapacity = std::max(capacity * 3 / 2, required);
            resize(newCapacity);
        }

        void resize(size_t newCapacity) {
            if (newCapacity == capacity) return;

            // 重新分配GPU内存
            if (instanceVbo) {
                glDeleteBuffers(1, &instanceVbo);
            }

            glGenBuffers(1, &instanceVbo);
            glBindBuffer(GL_ARRAY_BUFFER, instanceVbo);
            glBufferData(GL_ARRAY_BUFFER,
                newCapacity * sizeof(glm::mat4),
                nullptr, GL_DYNAMIC_DRAW);

            capacity = newCapacity;
        }
    };

    std::unordered_map<BlockFaceType, GPUBuffer> gpuBuffers;

public:
    RenderDataManager() {
        // 初始化GPU缓冲区
        initGPUBuffers();

        // 启动工作线程
        startWorkers(2);  // 2个工作线程
    }

    ~RenderDataManager() {
        stopWorkers();
        cleanupGPUBuffers();
    }

    // 主线程：提交区块变更
    void submitChunkUpdate(uint64_t chunkId, const glm::ivec2& pos,
        DataState state,
        const std::vector<glm::mat4>* newData = nullptr) {
        std::lock_guard<std::mutex> lock(updateMutex);

        ChunkUpdate update;
        update.chunkId = chunkId;
        update.chunkPos = pos;
        update.state = state;

        if (newData && (state == DataState::DIRTY || state == DataState::UPDATING)) {
            update.newMatrices = *newData;  // 复制数据
        }

        pendingUpdates.push_back(update);
        updateCV.notify_one();  // 唤醒工作线程
    }

    // 工作线程：处理更新
    void workerThread(int threadId) {
        while (running) {
            std::vector<ChunkUpdate> localUpdates;

            {
                std::unique_lock<std::mutex> lock(updateMutex);
                updateCV.wait(lock, [this]() {
                    return !pendingUpdates.empty() || !running;
                    });

                if (!running) break;

                // 批量获取更新
                localUpdates.swap(pendingUpdates);
            }

            // 处理更新
            processUpdates(localUpdates, threadId);
        }
    }

    // 渲染线程：获取准备好的数据
    const RenderBuffer& getReadyBuffer() {
        return buffers[currentReadBuffer];
    }

    // 交换缓冲区（通常在帧结束时调用）
    void swapBuffers() {
        // 确保写入缓冲区已准备好
        RenderBuffer& writeBuffer = buffers[currentWriteBuffer];

        if (writeBuffer.isReady.load()) {
            // 交换缓冲区
            int oldRead = currentReadBuffer.exchange(currentWriteBuffer);
            currentWriteBuffer = oldRead;

            // 重置新的写入缓冲区
            RenderBuffer& newWriteBuffer = buffers[currentWriteBuffer];
            newWriteBuffer.isReady.store(false);
            newWriteBuffer.readyCount.store(0);
            newWriteBuffer.frameId++;

            // 清空数据（但保留内存容量）
            for (auto& pair : newWriteBuffer.instanceData) {
                pair.second.clear();
            }
        }
    }

private:
    void processUpdates(const std::vector<ChunkUpdate>& updates, int threadId) {
        RenderBuffer& writeBuffer = buffers[currentWriteBuffer];

        for (const auto& update : updates) {
            if (update.state == DataState::CLEAN) {
                // 不需要处理
                continue;
            }

            // 根据状态处理
            if (update.state == DataState::DIRTY) {
                // 需要重新计算整个区块
                recalculateChunk(update.chunkId, update.chunkPos);
            }
            else if (update.state == DataState::UPDATING && !update.newMatrices.empty()) {
                // 增量更新：将新数据合并到缓冲区
                mergeIncrementalUpdate(update, writeBuffer);
            }

            // 更新进度
            uint32_t progress = writeBuffer.readyCount.fetch_add(1) + 1;

            // 检查是否所有更新都处理完成
            if (progress == updates.size()) {
                writeBuffer.isReady.store(true);
            }
        }
    }

    void mergeIncrementalUpdate(const ChunkUpdate& update, RenderBuffer& buffer) {
        // 关键优化：直接使用移动语义，避免拷贝
        for (size_t i = 0; i < update.newMatrices.size(); i += 6) {
            // 假设每个方块6个面，按面类型分组
            BlockFaceType types[] = {
                {BLOCK_STONE, RIGHT}, {BLOCK_STONE, LEFT},
                {BLOCK_STONE, FRONT}, {BLOCK_STONE, BACK},
                {BLOCK_STONE, UP},    {BLOCK_STONE, DOWN}
            };

            for (int face = 0; face < 6; ++face) {
                auto& targetList = buffer.instanceData[types[face]];
                targetList.push_back(update.newMatrices[i + face]);
            }
        }
    }

    void recalculateChunk(uint64_t chunkId, const glm::ivec2& pos) {
        // 这里应该从ChunkManager获取数据并重新计算
        // 但为了性能，最好在Chunk级缓存结果
    }

    void initGPUBuffers() {
        // 为每种方块面类型创建GPU缓冲区
        BlockFaceType baseTypes[] = {
            {BLOCK_STONE, RIGHT}, {BLOCK_STONE, LEFT},
            {BLOCK_STONE, FRONT}, {BLOCK_STONE, BACK},
            {BLOCK_STONE, UP},    {BLOCK_STONE, DOWN},
            {BLOCK_GRASS, RIGHT}, {BLOCK_GRASS, LEFT},
            {BLOCK_GRASS, FRONT}, {BLOCK_GRASS, BACK},
            {BLOCK_GRASS, UP},    {BLOCK_GRASS, DOWN},
            // 其他方块类型...
        };

        for (const auto& type : baseTypes) {
            gpuBuffers[type].resize(1000);  // 初始容量1000个实例
        }
    }

    void startWorkers(int count) {
        running = true;

        for (int i = 0; i < count; ++i) {
            workerThreads.emplace_back([this, i]() {
                workerThread(i);
                });
        }
    }

    void stopWorkers() {
        running = false;
        updateCV.notify_all();

        for (auto& thread : workerThreads) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        workerThreads.clear();
    }

    void cleanupGPUBuffers() {
        for (auto& pair : gpuBuffers) {
            if (pair.second.instanceVbo) {
                glDeleteBuffers(1, &pair.second.instanceVbo);
            }
        }
    }
};