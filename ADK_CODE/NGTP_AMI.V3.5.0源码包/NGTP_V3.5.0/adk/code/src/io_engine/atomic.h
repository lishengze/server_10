#ifndef ADK_IMPL_ATOMIC_H_
#define ADK_IMPL_ATOMIC_H_

#include <stdint.h>
#include <assert.h>

namespace adk_impl
{

class Atomic
{
public:
    using ref_type = uint32_t;

    Atomic()
    {
        reference_ = new ref_type;
        assert(reference_);
        *reference_ = 1;
    }

    ~Atomic()
    {
        Reset();
    }

    Atomic(const Atomic& atomic)
    {
        reference_ = atomic.reference_;
        __sync_fetch_and_add(reference_, 1);
    }        

    Atomic& operator=(const Atomic& atomic)
    {
        if (&atomic != this)
        {
            Reset();
            reference_ = atomic.reference_;
            __sync_fetch_and_add(reference_, 1);
        }

        return *this;
    }

    void Reset()
    {
        if (*reference_ == 1)
        {
            delete reference_;
        }
        else
        {
            __sync_fetch_and_sub(reference_, 1)
        }
    }

    ref_type reference()
    {
        return *reference_;
    }

private:
    ref_type* reference_;
};

}


#endif