#pragma once
#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>

class ThreadManager {
private:
    struct Task {
        std::function<void()> func;
        uint32_t priority;  // 优先级：0=最高，越大越低
        uint64_t id;

        bool operator<(const Task& other) const {
            // 优先级队列是最大堆，我们想要最小优先级的先出列
            return priority > other.priority;
        }
    };

    // 三个线程池
    std::thread renderThread;
    std::thread chunkThread;
    std::thread gameLogicThread;

    // 任务队列
    std::priority_queue<Task> renderTasks;
    std::priority_queue<Task> chunkTasks;
    std::priority_queue<Task> gameLogicTasks;

    // 同步原语
    std::mutex renderMutex, chunkMutex, gameLogicMutex;
    std::condition_variable renderCV, chunkCV, gameLogicCV;

    // 控制标志
    std::atomic<bool> running{ false };
    std::atomic<uint64_t> nextTaskId{ 1 };

    // 帧同步
    std::atomic<uint64_t> currentFrame{ 0 };
    std::atomic<uint64_t> renderFrame{ 0 };
    std::atomic<bool> frameReady{ false };
    std::condition_variable frameCV;
    std::mutex frameMutex;

public:
    ThreadManager() {
        startThreads();
    }

    ~ThreadManager() {
        stopThreads();
    }

    // 提交任务
    template<typename Func>
    auto submitToRender(Func&& func, uint32_t priority = 10)
        -> std::future<decltype(func())> {
        return submitTask(std::forward<Func>(func), priority,
            renderTasks, renderMutex, renderCV);
    }

    template<typename Func>
    auto submitToChunk(Func&& func, uint32_t priority = 10)
        -> std::future<decltype(func())> {
        return submitTask(std::forward<Func>(func), priority,
            chunkTasks, chunkMutex, chunkCV);
    }

    template<typename Func>
    auto submitToGameLogic(Func&& func, uint32_t priority = 10)
        -> std::future<decltype(func())> {
        return submitTask(std::forward<Func>(func), priority,
            gameLogicTasks, gameLogicMutex, gameLogicCV);
    }

    // 等待下一帧
    void waitForNextFrame() {
        std::unique_lock<std::mutex> lock(frameMutex);
        frameCV.wait(lock, [this]() { return frameReady.load(); });
        frameReady.store(false);

        // 更新帧计数
        renderFrame.store(currentFrame.load());
        currentFrame.fetch_add(1);
    }

    // 标记帧就绪
    void signalFrameReady() {
        {
            std::lock_guard<std::mutex> lock(frameMutex);
            frameReady.store(true);
        }
        frameCV.notify_all();
    }

private:
    template<typename Func>
    auto submitTask(Func&& func, uint32_t priority,
        std::priority_queue<Task>& queue,
        std::mutex& mutex,
        std::condition_variable& cv)
        -> std::future<decltype(func())> {

        using ResultType = decltype(func());

        auto task = std::make_shared<std::packaged_task<ResultType()>>(
            std::forward<Func>(func)
        );

        std::future<ResultType> result = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mutex);
            queue.push(Task{
                [task]() { (*task)(); },
                priority,
                nextTaskId.fetch_add(1)
                });
        }

        cv.notify_one();
        return result;
    }

    void startThreads() {
        running = true;

        // 启动渲染线程
        renderThread = std::thread([this]() {
            renderWorker();
            });

        // 启动区块线程
        chunkThread = std::thread([this]() {
            chunkWorker();
            });

        // 启动游戏逻辑线程
        gameLogicThread = std::thread([this]() {
            gameLogicWorker();
            });
    }

    void renderWorker() {
        while (running) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(renderMutex);
                renderCV.wait(lock, [this]() {
                    return !renderTasks.empty() || !running;
                    });

                if (!running) break;

                task = renderTasks.top();
                renderTasks.pop();
            }

            // 执行任务
            task.func();
        }
    }

    void chunkWorker() {
        while (running) {
            Task task;

            {
                std::unique_lock<std::mutex> lock(chunkMutex);
                chunkCV.wait(lock, [this]() {
                    return !chunkTasks.empty() || !running;
                    });

                if (!running) break;

                task = chunkTasks.top();
                chunkTasks.pop();
            }

            // 执行区块相关任务
            task.func();
        }
    }

    void gameLogicWorker() {
        while (running) {
            Task task;

            {
                std::unique_lock<std::mutex> lock(gameLogicMutex);
                gameLogicCV.wait(lock, [this]() {
                    return !gameLogicTasks.empty() || !running;
                    });

                if (!running) break;

                task = gameLogicTasks.top();
                gameLogicTasks.pop();
            }

            // 执行游戏逻辑任务
            task.func();
        }
    }

    void stopThreads() {
        running = false;

        // 唤醒所有线程
        renderCV.notify_all();
        chunkCV.notify_all();
        gameLogicCV.notify_all();

        // 等待线程结束
        if (renderThread.joinable()) renderThread.join();
        if (chunkThread.joinable()) chunkThread.join();
        if (gameLogicThread.joinable()) gameLogicThread.join();

    }
};