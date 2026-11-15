// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/CodeAllocator.h"

#include "Luau/CodeGenCommon.h"

#if defined(LUAU_T24_LARGE_NATIVE_MODULE_RESEARCH)
#include <memory>
#endif
#include <string.h>

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

const size_t kPageSize = 4096;
#else
#include <sys/mman.h>
#include <unistd.h>

#if defined(__FreeBSD__) && !(_POSIX_C_SOURCE >= 200112L)
const size_t kPageSize = getpagesize();
#else
const size_t kPageSize = sysconf(_SC_PAGESIZE);
#endif
#endif

#ifdef __APPLE__
extern "C" void sys_icache_invalidate(void* start, size_t len);
#endif


#if defined(_WIN32)
static uint8_t* allocatePagesImpl(size_t size)
{
    CODEGEN_ASSERT(size == Luau::CodeGen::CodeAllocator::alignToPageSize(size));

    return (uint8_t*)VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

static void freePagesImpl(uint8_t* mem, size_t size)
{
    CODEGEN_ASSERT(size == Luau::CodeGen::CodeAllocator::alignToPageSize(size));

    if (VirtualFree(mem, 0, MEM_RELEASE) == 0)
        CODEGEN_ASSERT(!"failed to deallocate block memory");
}

[[nodiscard]] static bool makePagesExecutable(uint8_t* mem, size_t size)
{
    CODEGEN_ASSERT((uintptr_t(mem) & (kPageSize - 1)) == 0);
    CODEGEN_ASSERT(size == Luau::CodeGen::CodeAllocator::alignToPageSize(size));

    DWORD oldProtect;
    return VirtualProtect(mem, size, PAGE_EXECUTE_READ, &oldProtect) != 0;
}

[[nodiscard]] static bool makePagesNotExecutable(uint8_t* mem, size_t size)
{
    CODEGEN_ASSERT((uintptr_t(mem) & (kPageSize - 1)) == 0);
    CODEGEN_ASSERT(size == Luau::CodeGen::CodeAllocator::alignToPageSize(size));

    DWORD oldProtect;
    return VirtualProtect(mem, size, PAGE_READWRITE, &oldProtect) != 0;
}

[[nodiscard]] static bool makePagesReadOnly(uint8_t* mem, size_t size)
{
    CODEGEN_ASSERT((uintptr_t(mem) & (kPageSize - 1)) == 0);
    CODEGEN_ASSERT(size == Luau::CodeGen::CodeAllocator::alignToPageSize(size));

    DWORD oldProtect;
    return VirtualProtect(mem, size, PAGE_READONLY, &oldProtect) != 0;
}

static void flushInstructionCache(uint8_t* mem, size_t size)
{
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP | WINAPI_PARTITION_SYSTEM)
    if (FlushInstructionCache(GetCurrentProcess(), mem, size) == 0)
        CODEGEN_ASSERT(!"Failed to flush instruction cache");
#endif
}
#else
static uint8_t* allocatePagesImpl(size_t size)
{
    CODEGEN_ASSERT(size == Luau::CodeGen::CodeAllocator::alignToPageSize(size));

#ifdef __APPLE__
    void* result = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
#else
    void* result = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
#endif

    return (result == MAP_FAILED) ? nullptr : static_cast<uint8_t*>(result);
}

static void freePagesImpl(uint8_t* mem, size_t size)
{
    CODEGEN_ASSERT(size == Luau::CodeGen::CodeAllocator::alignToPageSize(size));

    if (munmap(mem, size) != 0)
        CODEGEN_ASSERT(!"Failed to deallocate block memory");
}

[[nodiscard]] static bool makePagesExecutable(uint8_t* mem, size_t size)
{
    CODEGEN_ASSERT((uintptr_t(mem) & (kPageSize - 1)) == 0);
    CODEGEN_ASSERT(size == Luau::CodeGen::CodeAllocator::alignToPageSize(size));

    return mprotect(mem, size, PROT_READ | PROT_EXEC) == 0;
}

[[nodiscard]] static bool makePagesNotExecutable(uint8_t* mem, size_t size)
{
    CODEGEN_ASSERT((uintptr_t(mem) & (kPageSize - 1)) == 0);
    CODEGEN_ASSERT(size == Luau::CodeGen::CodeAllocator::alignToPageSize(size));

    return mprotect(mem, size, PROT_READ | PROT_WRITE) == 0;
}

[[nodiscard]] static bool makePagesReadOnly(uint8_t* mem, size_t size)
{
    CODEGEN_ASSERT((uintptr_t(mem) & (kPageSize - 1)) == 0);
    CODEGEN_ASSERT(size == Luau::CodeGen::CodeAllocator::alignToPageSize(size));

    return mprotect(mem, size, PROT_READ) == 0;
}

static void flushInstructionCache(uint8_t* mem, size_t size)
{
#ifdef __EMSCRIPTEN__
#elif defined(__APPLE__)
    sys_icache_invalidate(mem, size);
#else
    __builtin___clear_cache((char*)mem, (char*)mem + size);
#endif
}
#endif

namespace Luau
{
namespace CodeGen
{

#if defined(LUAU_T24_LARGE_NATIVE_MODULE_RESEARCH)
static bool tryAlignToPageSize(size_t size, size_t& aligned)
{
    if (size > SIZE_MAX - (kPageSize - 1))
        return false;

    aligned = CodeAllocator::alignToPageSize(size);
    return true;
}
#endif

size_t CodeAllocator::alignToPageSize(size_t size)
{
    return (size + kPageSize - 1) & ~(kPageSize - 1);
}

CodeAllocator::CodeAllocator(size_t blockSize, size_t maxTotalSize)
    : CodeAllocator(blockSize, maxTotalSize, nullptr, nullptr)
{
}

CodeAllocator::CodeAllocator(size_t blockSize, size_t maxTotalSize, AllocationCallback* allocationCallback, void* allocationCallbackContext)
    : blockSize{blockSize}
    , maxTotalSize{maxTotalSize}
    , allocationCallback{allocationCallback}
    , allocationCallbackContext{allocationCallbackContext}
{
    CODEGEN_ASSERT(blockSize > kMaxReservedDataSize);
    CODEGEN_ASSERT(maxTotalSize >= blockSize);
}

CodeAllocator::~CodeAllocator()
{
    if (destroyBlockUnwindInfo)
    {
        for (void* unwindInfo : unwindInfos)
            destroyBlockUnwindInfo(context, unwindInfo);
    }

    CODEGEN_ASSERT(liveAllocations == 0);

    for (uint8_t* block : blocks)
        freePages(block, blockSize);

#if defined(LUAU_T24_LARGE_NATIVE_MODULE_RESEARCH)
    for (const LargeBlock& block : largeBlocks)
    {
        if (block.unwindInfo && destroyBlockUnwindInfo)
            destroyBlockUnwindInfo(context, block.unwindInfo);
        freePages(block.memory, block.size);
    }
#endif
}

CodeAllocationData CodeAllocator::allocate(const uint8_t* data, size_t dataSize, const uint8_t* code, size_t codeSize)
{
#if defined(LUAU_T24_LARGE_NATIVE_MODULE_RESEARCH)
    // Validate every addition/rounding used below before any pointer or data
    // access. Reserve the same maximum unwind prefix as ordinary allocations.
    size_t reservedDataSize;
    size_t maximumUsedSize;
    size_t requiredSize;
    if ((dataSize == 0 && codeSize == 0) || dataSize > SIZE_MAX - kMaxReservedDataSize ||
        !tryAlignToPageSize(kMaxReservedDataSize + dataSize, reservedDataSize) || codeSize > SIZE_MAX - reservedDataSize ||
        !tryAlignToPageSize(reservedDataSize + codeSize, requiredSize))
        return {};

    maximumUsedSize = dataSize ? reservedDataSize + codeSize : kMaxReservedDataSize + codeSize;
    if (maximumUsedSize > blockSize)
    {
        // Code-only modules do not need the data/code page split. The callback
        // reservation still precedes their first instruction in this block.
        if (dataSize == 0 && !tryAlignToPageSize(kMaxReservedDataSize + codeSize, requiredSize))
            return {};
        return allocateLargeModule(data, dataSize, code, codeSize, requiredSize);
    }
#endif
    size_t startOffset = 0;
    size_t codeOffset;
    size_t dataOffset;
    size_t pageAlignedSize;
    size_t totalSize;
    auto availableSize = [&]()
    {
#if defined(LUAU_T24_LARGE_NATIVE_MODULE_RESEARCH)
        if (!blockPos)
            return size_t(0);
#endif
        return size_t(blockEnd - blockPos);
    };

    if (dataSize != 0)
    {
        // Data and code sections occupy separate page ranges so that data can be made read-only
        // and code can be made executable independently. The code section starts on the first page
        // boundary after the unwind info header and data.

        // Function has to fit into a single block with unwinding information
        if (alignToPageSize(kMaxReservedDataSize + dataSize) + codeSize > blockSize)
            return {};

        // We might need a new block
        if (alignToPageSize(dataSize) + codeSize > availableSize())
        {
            if (!allocateNewBlock(startOffset))
                return {};

            CODEGEN_ASSERT(alignToPageSize(startOffset + dataSize) + codeSize <= size_t(blockEnd - blockPos));
        }

        codeOffset = alignToPageSize(startOffset + dataSize);
        dataOffset = codeOffset - dataSize;
        totalSize = alignToPageSize(dataSize) + codeSize;
        pageAlignedSize = alignToPageSize(codeOffset + codeSize);
    }
    else
    {
        // No data to protect — code starts directly after the unwind info header
        totalSize = codeSize;

        if (totalSize > blockSize - kMaxReservedDataSize)
            return {};

        if (totalSize > availableSize())
        {
            if (!allocateNewBlock(startOffset))
                return {};

            CODEGEN_ASSERT(totalSize <= size_t(blockEnd - blockPos));
        }

        dataOffset = startOffset;
        codeOffset = startOffset;
        pageAlignedSize = alignToPageSize(startOffset + totalSize);
    }

    CODEGEN_ASSERT((uintptr_t(blockPos) & (kPageSize - 1)) == 0); // Allocation starts on page boundary

    if (dataSize != 0)
        memcpy(blockPos + dataOffset, data, dataSize);
    if (codeSize != 0)
        memcpy(blockPos + codeOffset, code, codeSize);

    if (dataSize != 0)
    {
        // Make data pages read-only and code pages executable independently
        if (!makePagesReadOnly(blockPos, codeOffset))
            return {};
        if (!makePagesExecutable(blockPos + codeOffset, pageAlignedSize - codeOffset))
            return {};
    }
    else
    {
        if (!makePagesExecutable(blockPos, pageAlignedSize))
            return {};
    }

    liveAllocations++;

    flushInstructionCache(blockPos + codeOffset, codeSize);

    CodeAllocationData result;

    result.start = blockPos + startOffset;
    result.size = totalSize;
    result.codeStart = blockPos + codeOffset;

    result.allocationStart = blockPos;
    result.allocationSize = pageAlignedSize;

    // Ensure that future allocations from the block start from a page boundary.
    // This is important since we use W^X, and writing to the previous page would require briefly removing
    // executable bit from it, which may result in access violations if that code is being executed concurrently.
    if (pageAlignedSize <= size_t(blockEnd - blockPos))
    {
        blockPos += pageAlignedSize;
        CODEGEN_ASSERT((uintptr_t(blockPos) & (kPageSize - 1)) == 0);
        CODEGEN_ASSERT(blockPos <= blockEnd);
    }
    else
    {
        // Future allocations will need to allocate fresh blocks
        blockPos = blockEnd;
    }

    return result;
}

#if defined(LUAU_T24_LARGE_NATIVE_MODULE_RESEARCH)
CodeAllocationData CodeAllocator::allocateLargeModule(
    const uint8_t* data, size_t dataSize, const uint8_t* code, size_t codeSize, size_t allocationSize
)
{
    // Include actual mapped page sizes, not nominal ordinary-block sizes, and
    // use subtraction so a caller-supplied limit cannot wrap the budget check.
    if (allocationSize > maxTotalSize || allocatedPageBytes > maxTotalSize - allocationSize)
        return {};

    // Reserve bookkeeping before owning pages or registered unwind state.
    largeBlocks.reserve(largeBlocks.size() + 1);
    uint8_t* block = allocatePages(allocationSize);
    if (!block)
        return {};

    size_t startOffset = 0;
    void* unwindInfo = nullptr;
    auto rollback = [&](uint8_t* memory)
    {
        if (unwindInfo && destroyBlockUnwindInfo)
            destroyBlockUnwindInfo(context, unwindInfo);
        freePages(memory, allocationSize);
    };
    std::unique_ptr<uint8_t, decltype(rollback)> pending(block, rollback);

    if (createBlockUnwindInfo)
        unwindInfo = createBlockUnwindInfo(context, block, allocationSize, startOffset);

    if ((createBlockUnwindInfo && !unwindInfo) || startOffset > kMaxReservedDataSize)
        return {};
    startOffset = (startOffset + (kCodeAlignment - 1)) & ~(kCodeAlignment - 1);

    size_t codeOffset = dataSize ? alignToPageSize(startOffset + dataSize) : startOffset;
    size_t dataOffset = dataSize ? codeOffset - dataSize : startOffset;
    size_t pageAlignedSize = alignToPageSize(codeOffset + codeSize);
    CODEGEN_ASSERT(pageAlignedSize <= allocationSize);

    if (dataSize)
        memcpy(block + dataOffset, data, dataSize);
    if (codeSize)
        memcpy(block + codeOffset, code, codeSize);

    // No writable/executable overlap: constants and code receive independent
    // protections, exactly as in the ordinary-block path.
    bool protectedPages = dataSize ? makePagesReadOnly(block, codeOffset) && makePagesExecutable(block + codeOffset, pageAlignedSize - codeOffset)
                                   : makePagesExecutable(block, pageAlignedSize);
    if (!protectedPages)
        return {};

    largeBlocks.push_back({block, allocationSize, unwindInfo});
    pending.release();
    allocatedPageBytes += allocationSize;
    liveAllocations++;
    flushInstructionCache(block + codeOffset, codeSize);

    CodeAllocationData result;
    result.start = block + startOffset;
    result.size = codeOffset - startOffset + codeSize;
    result.codeStart = block + codeOffset;
    result.allocationStart = block;
    result.allocationSize = allocationSize;
    return result;
}
#endif

void CodeAllocator::deallocate(CodeAllocationData codeAllocationData)
{
    if (codeAllocationData.allocationStart == nullptr)
        return;

    [[maybe_unused]] bool result = makePagesNotExecutable(codeAllocationData.allocationStart, codeAllocationData.allocationSize);
    CODEGEN_ASSERT(result);

    CODEGEN_ASSERT(liveAllocations != 0);
    liveAllocations--;

    // TODO: new allocations should be able to reuse the freed pages (but note that first block page contains unwind data)
}

bool CodeAllocator::allocateNewBlock(size_t& unwindInfoSize)
{
    // Stop allocating once we reach a global limit
#if defined(LUAU_T24_LARGE_NATIVE_MODULE_RESEARCH)
    size_t pageAlignedSize;
    if (!tryAlignToPageSize(blockSize, pageAlignedSize) || pageAlignedSize > maxTotalSize ||
        allocatedPageBytes > maxTotalSize - pageAlignedSize)
        return false;

    blocks.reserve(blocks.size() + 1);
    if (createBlockUnwindInfo)
        unwindInfos.reserve(unwindInfos.size() + 1);

    uint8_t* block = allocatePages(blockSize);
    if (!block)
        return false;

    void* unwindInfo = nullptr;
    auto rollback = [&](uint8_t* memory)
    {
        if (unwindInfo && destroyBlockUnwindInfo)
            destroyBlockUnwindInfo(context, unwindInfo);
        freePages(memory, blockSize);
    };
    std::unique_ptr<uint8_t, decltype(rollback)> pending(block, rollback);

    if (createBlockUnwindInfo)
    {
        unwindInfo = createBlockUnwindInfo(context, block, blockSize, unwindInfoSize);
        if (!unwindInfo || unwindInfoSize > kMaxReservedDataSize)
            return false;

        unwindInfoSize = (unwindInfoSize + (kCodeAlignment - 1)) & ~(kCodeAlignment - 1);
        unwindInfos.push_back(unwindInfo);
    }

    blocks.push_back(block);
    pending.release();
    blockPos = block;
    blockEnd = block + blockSize;
    allocatedPageBytes += pageAlignedSize;
    return true;
#else
    if ((blocks.size() + 1) * blockSize > maxTotalSize)
        return false;

    uint8_t* block = allocatePages(blockSize);
    if (!block)
        return false;

    blockPos = block;
    blockEnd = block + blockSize;

    blocks.push_back(block);

    if (createBlockUnwindInfo)
    {
        void* unwindInfo = createBlockUnwindInfo(context, block, blockSize, unwindInfoSize);

        // 'Round up' to preserve alignment of the following data and code
        unwindInfoSize = (unwindInfoSize + (kCodeAlignment - 1)) & ~(kCodeAlignment - 1);

        CODEGEN_ASSERT(unwindInfoSize <= kMaxReservedDataSize);

        if (!unwindInfo)
            return false;

        unwindInfos.push_back(unwindInfo);
    }

    return true;
#endif
}

uint8_t* CodeAllocator::allocatePages(size_t size) const
{
    const size_t pageAlignedSize = alignToPageSize(size);

    uint8_t* const mem = allocatePagesImpl(pageAlignedSize);
    if (mem == nullptr)
        return nullptr;

    if (allocationCallback)
        allocationCallback(allocationCallbackContext, nullptr, 0, mem, pageAlignedSize);

    return mem;
}

void CodeAllocator::freePages(uint8_t* mem, size_t size) const
{
    const size_t pageAlignedSize = alignToPageSize(size);

    if (allocationCallback)
        allocationCallback(allocationCallbackContext, mem, pageAlignedSize, nullptr, 0);

    freePagesImpl(mem, pageAlignedSize);
}

} // namespace CodeGen
} // namespace Luau
