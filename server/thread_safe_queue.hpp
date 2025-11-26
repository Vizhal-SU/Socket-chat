#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <stop_token>

template<typename T>
class ThreadSafeQueue {
public:
    void push(const T& item) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            queue_.push(item);
        }
        cond_.notify_one();
    }

    // This pop will wait until an item is available OR a stop is requested.
    // It returns true if an item was popped, false if a stop was requested.
    bool pop(T& item, std::stop_token token) {
        std::unique_lock<std::mutex> lock(mtx_);
        
        // Wait until one of three things is true:
        // 1. The queue is not empty.
        // 2. A stop has been requested.
        // The lambda predicate handles spurious wakeups correctly.
        cond_.wait(lock, [this, &token]{ 
            return !queue_.empty() || token.stop_requested(); 
        });

        // If we woke up because of a stop request and the queue is empty, exit.
        if (token.stop_requested() && queue_.empty()) {
            return false;
        }

        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

private:
    std::queue<T> queue_;
    std::mutex mtx_;
    std::condition_variable cond_;
};