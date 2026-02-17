#include "Memory.h"

static HeapAllocator s_HeapAllocator;
static MemoryStats s_Stats = { 0, 0, 0, 0 };

HeapAllocator::HeapAllocator()
    : m_UsedMemory(0)
    , m_TotalMemory(0)
{
}

HeapAllocator::~HeapAllocator()
{
}

void* HeapAllocator::Allocate(size_t size, size_t alignment)
{
    if (size == 0)
        return nullptr;

    void* ptr = nullptr;

#ifdef _WIN32
    ptr = _aligned_malloc(size, alignment);
#else
    if (posix_memalign(&ptr, alignment, size) != 0)
        ptr = nullptr;
#endif

    if (ptr)
    {
        m_UsedMemory += size;
        m_TotalMemory += size;

        s_Stats.totalAllocations++;
        s_Stats.currentUsedMemory += size;
        if (s_Stats.currentUsedMemory > s_Stats.peakMemory)
            s_Stats.peakMemory = s_Stats.currentUsedMemory;
    }

    return ptr;
}

void HeapAllocator::Deallocate(void* ptr)
{
    if (!ptr)
        return;

#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif

    s_Stats.totalDeallocations++;
}

void HeapAllocator::Reset()
{
    m_UsedMemory = 0;
}

StackAllocator::StackAllocator(const size_t size)
    : m_pMemory(nullptr)
    , m_Size(size)
    , m_Offset(0)
{
    m_pMemory = std::malloc(size);
    if (!m_pMemory)
        throw std::bad_alloc();
}

StackAllocator::~StackAllocator()
{
    if (m_pMemory)
        std::free(m_pMemory);
}

void* StackAllocator::Allocate(const size_t size, const size_t alignment)
{
    if (size == 0)
        return nullptr;

    uintptr_t currentAddr = reinterpret_cast<uintptr_t>(m_pMemory) + m_Offset;
    uintptr_t alignedAddr = (currentAddr + alignment - 1) & ~(alignment - 1);
    size_t adjustment = alignedAddr - currentAddr;

    if (m_Offset + adjustment + size > m_Size)
        return nullptr;

    m_Offset += adjustment;
    auto ptr = reinterpret_cast<void*>(static_cast<char*>(m_pMemory) + m_Offset);
    m_Offset += size;

    s_Stats.totalAllocations++;
    s_Stats.currentUsedMemory += size;
    if (s_Stats.currentUsedMemory > s_Stats.peakMemory)
        s_Stats.peakMemory = s_Stats.currentUsedMemory;

    return ptr;
}

void StackAllocator::Deallocate(void* ptr)
{
}

void StackAllocator::Reset()
{
    m_Offset = 0;
}

StackAllocator::Marker StackAllocator::GetMarker() const
{
    return Marker{ m_Offset };
}

void StackAllocator::FreeToMarker(const Marker marker)
{
    if (marker.offset <= m_Size)
        m_Offset = marker.offset;
}

ArenaAllocator::ArenaAllocator(const size_t blockSize)
    : m_pCurrentBlock(nullptr)
    , m_pFirstBlock(nullptr)
    , m_BlockSize(blockSize)
    , m_UsedMemory(0)
    , m_TotalMemory(0)
{
    m_pFirstBlock = AllocateBlock(blockSize);
    m_pCurrentBlock = m_pFirstBlock;
}

ArenaAllocator::~ArenaAllocator()
{
    Reset();

    Block* block = m_pFirstBlock;
    while (block)
    {
        Block* next = block->next;
        std::free(block->memory);
        delete block;
        block = next;
    }
}

void* ArenaAllocator::Allocate(const size_t size, const size_t alignment)
{
    if (size == 0)
        return nullptr;

    if (m_pCurrentBlock)
    {
        uintptr_t currentAddr = reinterpret_cast<uintptr_t>(m_pCurrentBlock->memory) + m_pCurrentBlock->offset;
        uintptr_t alignedAddr = (currentAddr + alignment - 1) & ~(alignment - 1);
        size_t adjustment = alignedAddr - currentAddr;

        if (m_pCurrentBlock->offset + adjustment + size <= m_pCurrentBlock->size)
        {
            m_pCurrentBlock->offset += adjustment;
            auto ptr = reinterpret_cast<void*>(static_cast<char*>(m_pCurrentBlock->memory) + m_pCurrentBlock->offset);
            m_pCurrentBlock->offset += size;
            m_UsedMemory += size;

            s_Stats.totalAllocations++;
            s_Stats.currentUsedMemory += size;
            if (s_Stats.currentUsedMemory > s_Stats.peakMemory)
                s_Stats.peakMemory = s_Stats.currentUsedMemory;

            return ptr;
        }
    }

    size_t newBlockSize = size > m_BlockSize ? size : m_BlockSize;
    Block* newBlock = AllocateBlock(newBlockSize);

    if (m_pCurrentBlock)
        m_pCurrentBlock->next = newBlock;

    m_pCurrentBlock = newBlock;

    void* ptr = m_pCurrentBlock->memory;
    m_pCurrentBlock->offset = size;
    m_UsedMemory += size;

    s_Stats.totalAllocations++;
    s_Stats.currentUsedMemory += size;
    if (s_Stats.currentUsedMemory > s_Stats.peakMemory)
        s_Stats.peakMemory = s_Stats.currentUsedMemory;

    return ptr;
}

void ArenaAllocator::Deallocate(void* ptr)
{
}

void ArenaAllocator::Reset()
{
    Block* block = m_pFirstBlock;
    while (block)
    {
        block->offset = 0;
        block = block->next;
    }

    m_pCurrentBlock = m_pFirstBlock;
    m_UsedMemory = 0;
}

ArenaAllocator::Block* ArenaAllocator::AllocateBlock(const size_t minSize)
{
    auto block = new Block();
    block->size = minSize;
    block->memory = std::malloc(minSize);
    block->offset = 0;
    block->next = nullptr;

    if (!block->memory)
    {
        delete block;
        throw std::bad_alloc();
    }

    m_TotalMemory += minSize;
    return block;
}


HeapAllocator& Memory::GetHeapAllocator()
{
    return s_HeapAllocator;
}

MemoryStats Memory::GetStats()
{
    return s_Stats;
}

void Memory::PrintMemoryStats()
{
    spdlog::info("=== Memory Statistics ===");
    spdlog::info("Total Allocations: {}", s_Stats.totalAllocations);
    spdlog::info("Total Deallocations: {}", s_Stats.totalDeallocations);
    spdlog::info("Current Used Memory: {} bytes ({:.2f} MB)",
                 s_Stats.currentUsedMemory,
                 s_Stats.currentUsedMemory / (1024.0 * 1024.0));
    spdlog::info("Peak Memory: {} bytes ({:.2f} MB)",
                 s_Stats.peakMemory,
                 s_Stats.peakMemory / (1024.0 * 1024.0));
    spdlog::info("Heap Allocator Used: {} bytes ({:.2f} MB)",
                 s_HeapAllocator.GetUsedMemory(),
                 s_HeapAllocator.GetUsedMemory() / (1024.0 * 1024.0));
    spdlog::info("Heap Allocator Total: {} bytes ({:.2f} MB)",
                 s_HeapAllocator.GetTotalMemory(),
                 s_HeapAllocator.GetTotalMemory() / (1024.0 * 1024.0));
}
