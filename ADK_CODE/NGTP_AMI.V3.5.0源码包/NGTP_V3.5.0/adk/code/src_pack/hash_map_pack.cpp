#include <adk/hash_map.h>
#include <adk_pack/hash_map.h>

namespace adk
{

namespace impl
{

using OrderMapImpl = adk_impl::HashMap<boost::array<char, 6>, boost::array<char, 10>, void*>;

OrderMap* OrderMap::Create(uint32_t bucket_init_num, uint16_t deep_limit, uint16_t bucket_ext_num)
{
    return (OrderMap*)(OrderMapImpl::Create(bucket_init_num, deep_limit, bucket_ext_num));
}

int32_t OrderMap::Insert(const boost::array<char, 6>& key_1, const boost::array<char, 10>& key_2, void* value)
{
    return reinterpret_cast<OrderMapImpl*>(this)->Insert(key_1, key_2, value);
}

int32_t OrderMap::Find(const boost::array<char, 6>& key_1, const boost::array<char, 10>& key_2, void*** value)
{
    return reinterpret_cast<OrderMapImpl*>(this)->Find(key_1, key_2, value);
}

using StdOrderMapImpl = adk_impl::HashMap<std::array<char, 6>, std::array<char, 10>, void*>;

StdOrderMap* StdOrderMap::Create(uint32_t bucket_init_num, uint16_t deep_limit, uint16_t bucket_ext_num)
{
    return (StdOrderMap*)(StdOrderMapImpl::Create(bucket_init_num, deep_limit, bucket_ext_num));
}

int32_t StdOrderMap::Insert(const std::array<char, 6>& key_1, const std::array<char, 10>& key_2, void* value)
{
    return reinterpret_cast<StdOrderMapImpl*>(this)->Insert(key_1, key_2, value);
}

int32_t StdOrderMap::Find(const std::array<char, 6>& key_1, const std::array<char, 10>& key_2, void*** value)
{
    return reinterpret_cast<StdOrderMapImpl*>(this)->Find(key_1, key_2, value);
}

using StdQuoteOrderMapImpl = adk_impl::HashMap<std::array<char, 12>, std::array<char, 8>, void*>;

StdQuoteOrderMap* StdQuoteOrderMap::Create(uint32_t bucket_init_num, uint16_t deep_limit, uint16_t bucket_ext_num)
{
    return (StdQuoteOrderMap*)(StdQuoteOrderMapImpl::Create(bucket_init_num, deep_limit, bucket_ext_num));
}

int32_t StdQuoteOrderMap::Insert(const std::array<char, 12>& key_1, const std::array<char, 8>& key_2, void* value)
{
    return reinterpret_cast<StdQuoteOrderMapImpl*>(this)->Insert(key_1, key_2, value);
}

int32_t StdQuoteOrderMap::Find(const std::array<char, 12>& key_1, const std::array<char, 8>& key_2, void*** value)
{
    return reinterpret_cast<StdQuoteOrderMapImpl*>(this)->Find(key_1, key_2, value);
}

using RuleV1MapImpl = adk_impl::HashMap<boost::array<char, 6>, uint8_t, uint16_t>;

RuleV1Map* RuleV1Map::Create(uint32_t bucket_init_num, uint16_t deep_limit, uint16_t bucket_ext_num)
{
    return (RuleV1Map*)(RuleV1MapImpl::Create(bucket_init_num, deep_limit, bucket_ext_num));
}

int32_t RuleV1Map::Insert(const boost::array<char, 6>& key_1, const uint8_t key_2, const uint16_t value)
{
    return reinterpret_cast<RuleV1MapImpl*>(this)->Insert(key_1, key_2, value);
}

int32_t RuleV1Map::Find(const boost::array<char, 6>& key_1, const uint8_t key_2, uint16_t** value)
{
    return reinterpret_cast<RuleV1MapImpl*>(this)->Find(key_1, key_2, value);
}

using StdRuleV1MapImpl = adk_impl::HashMap<std::array<char, 6>, uint8_t, uint16_t>;

StdRuleV1Map* StdRuleV1Map::Create(uint32_t bucket_init_num, uint16_t deep_limit, uint16_t bucket_ext_num)
{
    return (StdRuleV1Map*)(StdRuleV1MapImpl::Create(bucket_init_num, deep_limit, bucket_ext_num));
}

int32_t StdRuleV1Map::Insert(const std::array<char, 6>& key_1, const uint8_t key_2, const uint16_t value)
{
    return reinterpret_cast<StdRuleV1MapImpl*>(this)->Insert(key_1, key_2, value);
}

int32_t StdRuleV1Map::Find(const std::array<char, 6>& key_1, const uint8_t key_2, uint16_t** value)
{
    return reinterpret_cast<StdRuleV1MapImpl*>(this)->Find(key_1, key_2, value);
}


using RuleV2MapImpl = adk_impl::HashMap<boost::array<char, 6>, uint32_t, uint16_t>;

RuleV2Map* RuleV2Map::Create(uint32_t bucket_init_num, uint16_t deep_limit, uint16_t bucket_ext_num)
{
    return (RuleV2Map*)(RuleV2MapImpl::Create(bucket_init_num, deep_limit, bucket_ext_num));
}

int32_t RuleV2Map::Insert(const boost::array<char, 6>& key_1, const uint32_t key_2, const uint16_t value)
{
    return reinterpret_cast<RuleV2MapImpl*>(this)->Insert(key_1, key_2, value);
}

int32_t RuleV2Map::Find(const boost::array<char, 6>& key_1, const uint32_t key_2, uint16_t** value)
{
    return reinterpret_cast<RuleV2MapImpl*>(this)->Find(key_1, key_2, value);
}

using StdRuleV2MapImpl = adk_impl::HashMap<std::array<char, 6>, uint32_t, uint16_t>;

StdRuleV2Map* StdRuleV2Map::Create(uint32_t bucket_init_num, uint16_t deep_limit, uint16_t bucket_ext_num)
{
    return (StdRuleV2Map*)(StdRuleV2MapImpl::Create(bucket_init_num, deep_limit, bucket_ext_num));
}

int32_t StdRuleV2Map::Insert(const std::array<char, 6>& key_1, const uint32_t key_2, const uint16_t value)
{
    return reinterpret_cast<StdRuleV2MapImpl*>(this)->Insert(key_1, key_2, value);
}

int32_t StdRuleV2Map::Find(const std::array<char, 6>& key_1, const uint32_t key_2, uint16_t** value)
{
    return reinterpret_cast<StdRuleV2MapImpl*>(this)->Find(key_1, key_2, value);
}


using CancelInfoMapImpl = adk_impl::HashMap<boost::array<char, 6>, boost::array<char, 10>, int64_t>;

CancelInfoMap* CancelInfoMap::Create(uint32_t bucket_init_num, uint16_t deep_limit, uint16_t bucket_ext_num)
{
    return (CancelInfoMap*)(CancelInfoMapImpl::Create(bucket_init_num, deep_limit, bucket_ext_num));
}

int32_t CancelInfoMap::Insert(const boost::array<char, 6>& key_1, const boost::array<char, 10>& key_2, const int64_t value)
{
    return reinterpret_cast<CancelInfoMapImpl*>(this)->Insert(key_1, key_2, value);
}

int32_t CancelInfoMap::Find(const boost::array<char, 6>& key_1, const boost::array<char, 10>& key_2, int64_t** value)
{
    return reinterpret_cast<CancelInfoMapImpl*>(this)->Find(key_1, key_2, value);
}

using StdCancelInfoMapImpl = adk_impl::HashMap<std::array<char, 6>, std::array<char, 10>, int64_t>;

StdCancelInfoMap* StdCancelInfoMap::Create(uint32_t bucket_init_num, uint16_t deep_limit, uint16_t bucket_ext_num)
{
    return (StdCancelInfoMap*)(StdCancelInfoMapImpl::Create(bucket_init_num, deep_limit, bucket_ext_num));
}

int32_t StdCancelInfoMap::Insert(const std::array<char, 6>& key_1, const std::array<char, 10>& key_2, const int64_t value)
{
    return reinterpret_cast<StdCancelInfoMapImpl*>(this)->Insert(key_1, key_2, value);
}

int32_t StdCancelInfoMap::Find(const std::array<char, 6>& key_1, const std::array<char, 10>& key_2, int64_t** value)
{
    return reinterpret_cast<StdCancelInfoMapImpl*>(this)->Find(key_1, key_2, value);
}

}

}