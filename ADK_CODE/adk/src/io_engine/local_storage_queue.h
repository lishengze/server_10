#ifndef ADK_IMPL_LOCAL_STORAGE_QUEUE_H_
#define ADK_IMPL_LOCAL_STORAGE_QUEUE_H_

#include <adk/error_code.h>
#include <adk/arch/generic.h>

namespace adk_impl
{

template<typename ElementType, uint32_t kQueueSize>
class LocalStorageQueue
{
public:
    inline int32_t TryPush(const ElementType& element)
    {
        assert(length_ <= kQueueSize);
        if (ADK_UNLIKELY(kQueueSize == length_))
        {
            return ErrorCode::kQueueFull;
        }

        const auto tail = cursor_ + length_++;
        if (ADK_UNLIKELY(tail >= kQueueSize))
        {
            storage_[tail - kQueueSize] = element;
        }
        else
        {
            storage_[tail] = element;
        }

        return ErrorCode::kSuccess;
    }

    int32_t TryPush(ElementType&& element)
    {
        assert(length_ <= kQueueSize);
        if (ADK_UNLIKELY(kQueueSize == length_))
        {
            return ErrorCode::kQueueFull;
        }

        const auto tail = cursor_ + length_++;
        if (ADK_UNLIKELY(tail >= kQueueSize))
        {
            storage_[tail - kQueueSize] = element;
        }
        else
        {
            storage_[tail] = element;
        }

        return ErrorCode::kSuccess;
    }

    int32_t TryPop(ElementType& element)
    {
        assert(cursor_ < kQueueSize);
        if (ADK_UNLIKELY(0 == length_))
        {
            return ErrorCode::kQueueEmpty;
        }

        element = storage_[cursor_++];
        if (cursor_ == kQueueSize)
        {
            cursor_ = 0;
        }

        --length_;
        return ErrorCode::kSuccess;
    }

    uint32_t length() const
    {
        return length_;
    }

private:
    uint32_t    cursor_ = 0;
    uint32_t    length_ = 0;
    ElementType storage_[kQueueSize];
};

}

#endif
