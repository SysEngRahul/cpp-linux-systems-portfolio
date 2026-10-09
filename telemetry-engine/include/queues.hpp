// queues.hpp - two bounded MPMC queues with an identical interface:
//
//   bool push(const T&)  blocks (backpressure) while full; false if closed
//   bool pop(T&)         blocks while empty; false once closed AND drained
//   void close()         call only after every producer has finished
//
// MutexQueue     : ring buffer + std::mutex + 2 condition variables
// LockFreeQueue  : Vyukov bounded MPMC ring buffer (per-cell sequence
//                  numbers, CAS on head/tail), spin/yield when full/empty
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "telemetry.hpp"

// ------------------------------------------------------------ MutexQueue ----
template <typename T>
class MutexQueue {
public:
    static constexpr const char* kName = "mutex";

    explicit MutexQueue(size_t capacity)
        : buf_(capacity < 1 ? 1 : capacity), cap_(buf_.size()) {}

    bool push(const T& v) {
        std::unique_lock<std::mutex> lk(m_);
        not_full_.wait(lk, [&] { return count_ < cap_ || closed_; });
        if (closed_) return false;
        buf_[tail_] = v;
        tail_ = (tail_ + 1) % cap_;
        ++count_;
        lk.unlock();
        not_empty_.notify_one();
        return true;
    }

    bool pop(T& out) {
        std::unique_lock<std::mutex> lk(m_);
        not_empty_.wait(lk, [&] { return count_ > 0 || closed_; });
        if (count_ == 0) return false;  // closed and drained
        out = buf_[head_];
        head_ = (head_ + 1) % cap_;
        --count_;
        lk.unlock();
        not_full_.notify_one();
        return true;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lk(m_);
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

private:
    std::mutex m_;
    std::condition_variable not_full_, not_empty_;
    std::vector<T> buf_;
    size_t cap_;
    size_t head_ = 0, tail_ = 0, count_ = 0;
    bool closed_ = false;
};

// --------------------------------------------------------- LockFreeQueue ----
namespace detail {

class Backoff {
public:
    void pause() {
        if (n_ < 64) {
            ++n_;
            cpu_relax();
        } else {
            std::this_thread::yield();
        }
    }

private:
    unsigned n_ = 0;
};

// Dmitry Vyukov's bounded MPMC queue. Capacity is rounded up to a power of 2.
template <typename T>
class MpmcRing {
public:
    explicit MpmcRing(size_t capacity) {
        size_t cap = 2;
        while (cap < capacity) cap <<= 1;
        mask_ = cap - 1;
        cells_.reset(new Cell[cap]);
        for (size_t i = 0; i < cap; ++i)
            cells_[i].seq.store(i, std::memory_order_relaxed);
        enq_.store(0, std::memory_order_relaxed);
        deq_.store(0, std::memory_order_relaxed);
    }

    bool try_push(const T& v) {
        Cell* cell;
        size_t pos = enq_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &cells_[pos & mask_];
            const size_t seq = cell->seq.load(std::memory_order_acquire);
            const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (dif == 0) {
                if (enq_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    break;               // slot claimed
            } else if (dif < 0) {
                return false;            // full
            } else {
                pos = enq_.load(std::memory_order_relaxed);
            }
        }
        cell->data = v;
        cell->seq.store(pos + 1, std::memory_order_release);  // publish
        return true;
    }

    bool try_pop(T& out) {
        Cell* cell;
        size_t pos = deq_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &cells_[pos & mask_];
            const size_t seq = cell->seq.load(std::memory_order_acquire);
            const intptr_t dif =
                static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            if (dif == 0) {
                if (deq_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    break;               // slot claimed
            } else if (dif < 0) {
                return false;            // empty
            } else {
                pos = deq_.load(std::memory_order_relaxed);
            }
        }
        out = cell->data;
        cell->seq.store(pos + mask_ + 1, std::memory_order_release);  // recycle
        return true;
    }

private:
    struct Cell {
        std::atomic<size_t> seq;
        T data;
    };
    std::unique_ptr<Cell[]> cells_;
    size_t mask_;
    alignas(64) std::atomic<size_t> enq_;
    alignas(64) std::atomic<size_t> deq_;
};

}  // namespace detail

template <typename T>
class LockFreeQueue {
public:
    static constexpr const char* kName = "lockfree";

    explicit LockFreeQueue(size_t capacity) : ring_(capacity) {}

    bool push(const T& v) {
        detail::Backoff b;
        while (!ring_.try_push(v)) {
            if (closed_.load(std::memory_order_acquire)) return false;
            b.pause();
        }
        return true;
    }

    bool pop(T& out) {
        detail::Backoff b;
        for (;;) {
            // Read 'closed' BEFORE trying to pop: close() happens after all
            // producers are joined, so if we saw closed==true and the ring
            // is then empty, it is really drained.
            const bool closed = closed_.load(std::memory_order_acquire);
            if (ring_.try_pop(out)) return true;
            if (closed) return false;
            b.pause();
        }
    }

    void close() { closed_.store(true, std::memory_order_release); }

private:
    detail::MpmcRing<T> ring_;
    std::atomic<bool> closed_{false};
};
