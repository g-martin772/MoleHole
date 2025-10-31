#pragma once
#include <array>
#include <atomic>
#include <memory>

namespace Threading {

/**
 * Triple buffer implementation for lock-free concurrent access.
 * Provides separate read and write buffers with atomic swapping.
 * 
 * Usage:
 * - Simulation thread: getWriteBuffer() -> modify -> commitWriteBuffer()
 * - Render thread: swapReadBuffer() -> getReadBuffer() -> read
 * - UI thread: getUIBuffer() -> read (reads from current read buffer)
 */
template<typename T>
class TripleBuffer {
public:
    TripleBuffer() {
        // Initialize all three buffers
        for (int i = 0; i < 3; i++) {
            m_buffers[i] = std::make_unique<T>();
        }
    }
    
    // Get buffer for writing (simulation thread)
    T* getWriteBuffer() {
        int index = m_writeIndex.load(std::memory_order_relaxed);
        return m_buffers[index].get();
    }
    
    // Commit written buffer (simulation thread)
    // Makes the written buffer available for reading
    void commitWriteBuffer() {
        int currentWrite = m_writeIndex.load(std::memory_order_relaxed);
        int currentSwap = m_swapIndex.exchange(currentWrite, std::memory_order_release);
        m_writeIndex.store(currentSwap, std::memory_order_relaxed);
    }
    
    // Get buffer for reading (render thread)
    T* getReadBuffer() {
        int index = m_readIndex.load(std::memory_order_acquire);
        return m_buffers[index].get();
    }
    
    // Swap to latest committed buffer (render thread)
    // Should be called before getReadBuffer() to get latest data
    void swapReadBuffer() {
        int currentRead = m_readIndex.load(std::memory_order_relaxed);
        int currentSwap = m_swapIndex.exchange(currentRead, std::memory_order_acquire);
        m_readIndex.store(currentSwap, std::memory_order_relaxed);
    }
    
    // Get buffer for UI (main thread) - reads from current read buffer
    const T* getUIBuffer() const {
        int index = m_readIndex.load(std::memory_order_acquire);
        return m_buffers[index].get();
    }
    
    // Check if new data is available
    bool hasNewData() const {
        return m_readIndex.load(std::memory_order_acquire) != 
               m_swapIndex.load(std::memory_order_acquire);
    }
    
private:
    std::array<std::unique_ptr<T>, 3> m_buffers;
    std::atomic<int> m_readIndex{0};   // Current read buffer
    std::atomic<int> m_writeIndex{1};  // Current write buffer
    std::atomic<int> m_swapIndex{2};   // Middle buffer for swapping
};

} // namespace Threading
