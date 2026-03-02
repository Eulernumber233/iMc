// src/task/TaskSystem.h
#pragma once
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <atomic>

class TaskSystem {
private:
    struct Task {
        std::function<void()> function;
        uint32_t priority{ 0 };
        uint64_t id{ 0 };

        bool operator<(const Task& other) const {
            return priority < other.priority;
        }
    };

    std::vector<std::thread> workers;
    std::priority_queue<Task> taskQueue;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> running{ false };
    std::atomic<uint64_t> taskIdCounter{ 0 };

public:
    TaskSystem(int numThreads);
    ~TaskSystem();

    void start();
    void stop();

    template<typename F>
    uint64_t submit(F&& task, uint32_t priority = 0) {
        std::lock_guard<std::mutex> lock(queueMutex);
        uint64_t id = ++taskIdCounter;
        taskQueue.push({ std::forward<F>(task), priority, id });
        condition.notify_one();
        return id;
    }

    void waitAll();

private:
    void workerThread();
};