#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <atomic>

namespace Threading {

template<typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() : m_accepting(true) {}
    
    ~ThreadSafeQueue() {
        shutdown();
    }
    
    // Push item to queue
    void push(T&& item) {
        if (!m_accepting.load(std::memory_order_acquire)) {
            return; // Don't accept new items during shutdown
        }
        
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push(std::forward<T>(item));
        }
        m_cv.notify_one();
    }
    
    // Pop item from queue (blocking)
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] {
            return !m_queue.empty() || !m_accepting.load(std::memory_order_acquire);
        });
        
        if (!m_accepting.load(std::memory_order_acquire) && m_queue.empty()) {
            return std::nullopt; // Signal thread to exit
        }
        
        T item = std::move(m_queue.front());
        m_queue.pop();
        return item;
    }
    
    // Try to pop without blocking
    bool tryPop(T& item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) {
            return false;
        }
        
        item = std::move(m_queue.front());
        m_queue.pop();
        return true;
    }
    
    // Check if queue is empty
    bool empty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }
    
    // Get queue size
    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }
    
    // Shutdown queue (wake all waiting threads)
    void shutdown() {
        m_accepting.store(false, std::memory_order_release);
        m_cv.notify_all();
    }
    
    // Check if accepting new items
    bool isAccepting() const {
        return m_accepting.load(std::memory_order_acquire);
    }
    
private:
    std::queue<T> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_accepting;
};

} // namespace Threading
