// src/task/TaskSystem.cpp
#include "TaskSystem.h"
#include <iostream>

TaskSystem::TaskSystem(int numThreads) {
    workers.reserve(numThreads);
}

TaskSystem::~TaskSystem() {
    stop();
}

void TaskSystem::start() {
    if (running) return;

    running = true;
    for (size_t i = 0; i < workers.capacity(); ++i) {
        workers.emplace_back(&TaskSystem::workerThread, this);
    }
}

void TaskSystem::stop() {
    if (!running) return;

    running = false;
    condition.notify_all();

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers.clear();
}

void TaskSystem::workerThread() {
    while (running) {
        std::unique_lock<std::mutex> lock(queueMutex);
        condition.wait(lock, [this]() {
            return !taskQueue.empty() || !running;
            });

        if (!running) break;

        if (!taskQueue.empty()) {
            Task task = std::move(const_cast<Task&>(taskQueue.top()));
            taskQueue.pop();
            lock.unlock();

            try {
                task.function();
            }
            catch (const std::exception& e) {
                std::cerr << "Task execution error: " << e.what() << std::endl;
            }
        }
    }
}

void TaskSystem::waitAll() {
    while (true) {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (taskQueue.empty()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}