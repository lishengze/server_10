#include <adk/bitmap.h>
#include <adk_pack/bitmap.h>
#include <sys/prctl.h>

namespace adk 
{

    BitMap* BitMap::Create(uint32_t nr_valid_bits)
    {
    	return (adk::BitMap*)(adk_impl::BitMap::Create(nr_valid_bits));
    }

    BitMap* BitMap::Create(void* addr, uint32_t nr_valid_bits)
    {
    	return (adk::BitMap*)(adk_impl::BitMap::Create(addr, nr_valid_bits));
    }

    void BitMap::Destroy(BitMap* inst)
    {
        adk_impl::BitMap::Destroy((adk_impl::BitMap*)inst);
    }

    void BitMap::SetUnsafe(uint64_t pos)
    {
    	((adk_impl::BitMap*)this)->SetUnsafe(pos);
    }

    void BitMap::Set(uint64_t pos)
    {
    	((adk_impl::BitMap*)this)->Set(pos);
    }

    void BitMap::ClearUnsafe(uint64_t pos)
    {
    	((adk_impl::BitMap*)this)->ClearUnsafe(pos);	
    }

    void BitMap::Clear(uint64_t pos)
    {
    	((adk_impl::BitMap*)this)->Clear(pos);
    }

    void BitMap::SetRange(uint64_t start, uint64_t len)
    {
    	((adk_impl::BitMap*)this)->SetRange(start, len);	
    }

    void BitMap::ClearRange(uint64_t start, uint64_t len)
    {
    	((adk_impl::BitMap*)this)->ClearRange(start, len);
    }

    uint64_t BitMap::Get(uint64_t pos)
    {
    	return ((adk_impl::BitMap*)this)->Get(pos);
    }
}
