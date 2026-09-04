#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

namespace gateway
{

template <typename T>
class ThreadSafeQueue
{
public:

    void push(T value)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(value));
        }

        condition_.notify_one();
    }

    T pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);

        condition_.wait(lock,
            [this]
            {
                return !queue_.empty();
            });

        T value = std::move(queue_.front());
        queue_.pop();

        return value;
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<T> queue_;
};

}
