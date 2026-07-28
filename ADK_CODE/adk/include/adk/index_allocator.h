#ifndef ADK_IMPL_INDEX_ALLOCATOR_H_
#define ADK_IMPL_INDEX_ALLOCATOR_H_

#include "arch/generic.h"

#include <limits>

namespace adk_impl
{

inline bool IsEmpty(uint64_t loader)
{
    return 0 == loader;
}

inline bool IsSet(uint64_t loader, int32_t bit_index)
{
    return !!(loader & (static_cast<uint64_t>(1) << bit_index));
}

inline void SetBit(uint64_t& loader, int32_t bit_index)
{
    assert(!IsSet(loader, bit_index));
    loader |= (static_cast<uint64_t>(1) << bit_index);
}

inline void UnsetBit(uint64_t& loader, int32_t bit_index)
{
    assert(IsSet(loader, bit_index));
    loader -= (static_cast<uint64_t>(1) << bit_index);
}

static constexpr uint32_t BitsShift(uint32_t value)
{
    return (value > 1) ? 1 + BitsShift(value >> 1) : 0;
}

template<size_t kMaxIndexSize, typename Blank = void>
class IndexAllocator
{
public:
    IndexAllocator()
    {
        for (auto i = 0; i < kLoaderNumber - 1; ++i)
        {
            loaders_[i] = std::numeric_limits<uint64_t>::max();
        }

        constexpr auto kLeftBitsSize = kMaxIndexSize - (kLoaderNumber - 1) * kBitsSize;

        /** kLeftBitsSize <= 64, 
         *> if (64 == kLeftBitsSize) 
         *>     loaders_[kLoaderNumber - 1] = (uint64_t)-1 符合预期
         */
        loaders_[kLoaderNumber - 1] = (static_cast<uint64_t>(1) << kLeftBitsSize) - 1;
    }

    inline int32_t Allocate()
    {
        const auto index = PreAlloc();
        if (index >= 0)
        {
            Post(index);
            return index;
        }

        return -1;
    }

    inline void Free(int32_t index)
    {
        assert((index >= 0) && (index < kMaxIndexSize));

        const auto loader_index = index >> kBitsShift;
        auto& loader = loaders_[loader_index];
        if (ADK_UNLIKELY(IsEmpty(loader)))
        {
            sample_.Free(loader_index);
        }

        SetBit(loader, index & kBitsMask);
    }

    inline int32_t PreAlloc()
    {
        const auto loader_index = sample_.PreAlloc();
        if (loader_index >= 0)
        {
            assert(loader_index < kLoaderNumber);
            const auto& loader = loaders_[loader_index];

            assert(!IsEmpty(loader));
            return (loader_index << kBitsShift) + __builtin_ctzll(loader);
        }

        return -1;
    }

    inline void Post(int32_t index)
    {
        assert((index >= 0) && (index < kMaxIndexSize));

        const auto loader_index = index >> kBitsShift;
        auto& loader = loaders_[loader_index];
        UnsetBit(loader, index & kBitsMask);

        if (ADK_UNLIKELY(IsEmpty(loader)))
        {
            sample_.Post(loader_index);
        }
    }

private:
    static constexpr auto kBitsSize = sizeof(uint64_t) * 8;
    static constexpr auto kBitsMask = kBitsSize - 1;
    static constexpr auto kBitsShift = BitsShift(kBitsSize);
    static constexpr auto kLoaderNumber = (kMaxIndexSize + kBitsSize - 1) / kBitsSize;

    IndexAllocator<kLoaderNumber> sample_;
    uint64_t                      loaders_[kLoaderNumber];
};

template<size_t kMaxIndexSize>
class IndexAllocator<kMaxIndexSize, typename std::enable_if<kMaxIndexSize <= sizeof(uint64_t) * 8>::type>
{
public:
    IndexAllocator()
    {
        loader_ = (static_cast<uint64_t>(1) << kMaxIndexSize) - 1;
    }

    inline int32_t Allocate()
    {
        const auto index = PreAlloc();
        if (index >= 0)
        {
            Post(index);
        }

        return index;
    }

    inline void Free(int32_t index)
    {
        assert((index >= 0) && (index < kMaxIndexSize));
        SetBit(loader_, index);
    }

    inline int32_t PreAlloc()
    {
        return !IsEmpty(loader_) ? __builtin_ctzll(loader_) : -1;
    }

    inline void Post(int32_t index)
    {
        assert((index >= 0) && (index < kMaxIndexSize));
        UnsetBit(loader_, index);
    }

private:
    uint64_t loader_;
};

}

#endif