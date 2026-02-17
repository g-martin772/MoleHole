#pragma once

template<typename T>
using Unique = std::unique_ptr<T>;

template<typename T>
using Ref = std::shared_ptr<T>;

template<typename T>
using WeakRef = std::weak_ptr<T>;

template<typename T, typename... Args>
constexpr Unique<T> CreateUnique(Args&&... args)
{
    return std::make_unique<T>(std::forward<Args>(args)...);
}

template<typename T, typename... Args>
constexpr Ref<T> CreateRef(Args&&... args)
{
    return std::make_shared<T>(std::forward<Args>(args)...);
}

class HeapAllocator
{
public:
    HeapAllocator();
    ~HeapAllocator();

    void* Allocate(size_t size, size_t alignment = alignof(std::max_align_t));
    void Deallocate(void* ptr);
    void Reset();
    size_t GetUsedMemory() const { return m_UsedMemory; }
    size_t GetTotalMemory() const { return m_TotalMemory; }

private:
    size_t m_UsedMemory;
    size_t m_TotalMemory;
};

class StackAllocator
{
public:
    explicit StackAllocator(size_t size);
    ~StackAllocator();

    void* Allocate(size_t size, size_t alignment = alignof(std::max_align_t));
    void Deallocate(void* ptr);
    void Reset();
    size_t GetUsedMemory() const { return m_Offset; }
    size_t GetTotalMemory() const { return m_Size; }

    struct Marker
    {
        size_t offset;
    };

    Marker GetMarker() const;
    void FreeToMarker(Marker marker);

private:
    void* m_pMemory;
    size_t m_Size;
    size_t m_Offset;
};

class ArenaAllocator
{
public:
    explicit ArenaAllocator(size_t blockSize = 64 * 1024);
    ~ArenaAllocator();

    void* Allocate(size_t size, size_t alignment = alignof(std::max_align_t));
    void Deallocate(void* ptr);
    void Reset();
    size_t GetUsedMemory() const { return m_UsedMemory; }
    size_t GetTotalMemory() const { return m_TotalMemory; }

private:
    struct Block
    {
        void* memory;
        size_t size;
        size_t offset;
        Block* next;
    };

    Block* AllocateBlock(size_t minSize);

    Block* m_pCurrentBlock;
    Block* m_pFirstBlock;
    size_t m_BlockSize;
    size_t m_UsedMemory;
    size_t m_TotalMemory;
};

template<typename T, size_t PoolSize = 256>
class PoolAllocator
{
public:
    PoolAllocator()
        : m_UsedCount(0)
    {
        m_pMemory = std::malloc(sizeof(T) * PoolSize);
        if (!m_pMemory)
            throw std::bad_alloc();

        for (size_t i = 0; i < PoolSize - 1; ++i)
        {
            auto slot = static_cast<void**>(GetSlot(i));
            *slot = GetSlot(i + 1);
        }
        auto lastSlot = static_cast<void**>(GetSlot(PoolSize - 1));
        *lastSlot = nullptr;

        m_pFreeList = GetSlot(0);
    }

    ~PoolAllocator()
    {
        std::free(m_pMemory);
    }

    void* Allocate(const size_t size, size_t alignment = alignof(T))
    {
        if (size > sizeof(T) || !m_pFreeList)
            return nullptr;

        void* ptr = m_pFreeList;
        m_pFreeList = *static_cast<void**>(m_pFreeList);
        ++m_UsedCount;
        return ptr;
    }

    void Deallocate(void* ptr)
    {
        if (!ptr)
            return;

        auto slot = static_cast<void**>(ptr);
        *slot = m_pFreeList;
        m_pFreeList = ptr;
        --m_UsedCount;
    }

    void Reset()
    {
        for (size_t i = 0; i < PoolSize - 1; ++i)
        {
            auto slot = static_cast<void**>(GetSlot(i));
            *slot = GetSlot(i + 1);
        }
        auto lastSlot = static_cast<void**>(GetSlot(PoolSize - 1));
        *lastSlot = nullptr;

        m_pFreeList = GetSlot(0);
        m_UsedCount = 0;
    }

    size_t GetUsedMemory() const { return m_UsedCount * sizeof(T); }
    static size_t GetTotalMemory() { return PoolSize * sizeof(T); }

private:
    void* GetSlot(const size_t index) const {
        return static_cast<char*>(m_pMemory) + (index * sizeof(T));
    }

    void* m_pMemory;
    void* m_pFreeList;
    size_t m_UsedCount;
};

template<typename AllocatorType>
class ScopedAllocator
{
public:
    explicit ScopedAllocator(AllocatorType& allocator)
        : m_Allocator(allocator)
    {
        if constexpr (std::is_same_v<AllocatorType, StackAllocator>)
        {
            m_Marker = allocator.GetMarker();
        }
    }

    ~ScopedAllocator()
    {
        if constexpr (std::is_same_v<AllocatorType, StackAllocator>)
        {
            m_Allocator.FreeToMarker(m_Marker);
        }
        else
        {
            m_Allocator.Reset();
        }
    }

    ScopedAllocator(const ScopedAllocator&) = delete;
    ScopedAllocator& operator=(const ScopedAllocator&) = delete;

private:
    AllocatorType& m_Allocator;
    std::conditional_t<
        std::is_same_v<AllocatorType, StackAllocator>,
        StackAllocator::Marker,
        int
    > m_Marker;
};

inline void* AlignPointer(void* ptr, const size_t alignment)
{
    const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    return reinterpret_cast<void*>(aligned);
}

inline size_t AlignSize(const size_t size, const size_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

struct MemoryStats
{
    size_t totalAllocations;
    size_t totalDeallocations;
    size_t currentUsedMemory;
    size_t peakMemory;
};

class Memory
{
public:
    static HeapAllocator& GetHeapAllocator();
    static MemoryStats GetStats();
    static void PrintMemoryStats();
};
