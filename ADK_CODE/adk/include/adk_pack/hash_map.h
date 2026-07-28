/**
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved.
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_HASH_MAP_H_
#define ADK_HASH_MAP_H_

#include "error_code.h"

#include <boost/array.hpp>
#include <array>

namespace adk
{

namespace impl
{

class OrderMap
{
public:
    static OrderMap* Create(uint32_t bucket_init_num, uint16_t deep_limit, uint16_t bucket_ext_num);

    int32_t Insert(const boost::array<char, 6>& key_1, const boost::array<char, 10>& key_2, void* value);

    int32_t Find(const boost::array<char, 6>& key_1, const boost::array<char, 10>& key_2, void*** value);
};

class StdOrderMap
{
public:
    static StdOrderMap* Create(uint32_t bucket_init_num, uint16_t deep_limit, uint16_t bucket_ext_num);

    int32_t Insert(const std::array<char, 6>& key_1, const std::array<char, 10>& key_2, void* value);

    int32_t Find(const std::array<char, 6>& key_1, const std::array<char, 10>& key_2, void*** value);
};

class StdQuoteOrderMap
{
public:
    static StdQuoteOrderMap* Create(uint32_t bucket_init_num, uint16_t deep_limit, uint16_t bucket_ext_num);

    int32_t Insert(const std::array<char, 12>& key_1, const std::array<char, 8>& key_2, void* value);

    int32_t Find(const std::array<char, 12>& key_1, const std::array<char, 8>& key_2, void*** value);
};

class RuleV1Map
{
public:
    static RuleV1Map* Create(uint32_t bucket_init_num, uint16_t deep_limit, uint16_t bucket_ext_num);

    int32_t Insert(const boost::array<char, 6>& key_1, const uint8_t key_2, const uint16_t value);

    int32_t Find(const boost::array<char, 6>& key_1, const uint8_t key_2, uint16_t** value);
};

class StdRuleV1Map
{
public:
    static StdRuleV1Map* Create(uint32_t bucket_init_num, uint16_t deep_limit, uint16_t bucket_ext_num);

    int32_t Insert(const std::array<char, 6>& key_1, const uint8_t key_2, const uint16_t value);

    int32_t Find(const std::array<char, 6>& key_1, const uint8_t key_2, uint16_t** value);
};

class RuleV2Map
{
public:
    static RuleV2Map* Create(uint32_t bucket_init_num, uint16_t deep_limit, uint16_t bucket_ext_num);

    int32_t Insert(const boost::array<char, 6>& key_1, const uint32_t key_2, const uint16_t value);

    int32_t Find(const boost::array<char, 6>& key_1, const uint32_t key_2, uint16_t** value);
};

class StdRuleV2Map
{
public:
    static StdRuleV2Map* Create(uint32_t bucket_init_num, uint16_t deep_limit, uint16_t bucket_ext_num);

    int32_t Insert(const std::array<char, 6>& key_1, const uint32_t key_2, const uint16_t value);

    int32_t Find(const std::array<char, 6>& key_1, const uint32_t key_2, uint16_t** value);
};

class CancelInfoMap
{
public:
    static CancelInfoMap* Create(uint32_t bucket_init_num, uint16_t deep_limit, uint16_t bucket_ext_num);

    int32_t Insert(const boost::array<char, 6>& key_1, const boost::array<char, 10>& key_2, const int64_t value);

    int32_t Find(const boost::array<char, 6>& key_1, const boost::array<char, 10>& key_2, int64_t** value);
};

class StdCancelInfoMap
{
public:
    static StdCancelInfoMap* Create(uint32_t bucket_init_num, uint16_t deep_limit, uint16_t bucket_ext_num);

    int32_t Insert(const std::array<char, 6>& key_1, const std::array<char, 10>& key_2, const int64_t value);

    int32_t Find(const std::array<char, 6>& key_1, const std::array<char, 10>& key_2, int64_t** value);
};

}

template<typename _Kty1, typename _Kty2, typename _Vty>
class HashMap
{
public:
    static HashMap* Create(uint32_t bucket_init_num, uint16_t deep_limit, uint16_t bucket_ext_num)
    {
        return nullptr;
    }

    int32_t Insert(const _Kty1& key_1, const _Kty2& key_2, const _Vty& value)
    {
        return ErrorCode::kFailure;
    }

    int32_t Find(const _Kty1& key_1, const _Kty2& key_2, _Vty** value)
    {
        return ErrorCode::kFailure;
    }
};

template<typename _Vty>
class HashMap<boost::array<char, 6>, boost::array<char, 10>, _Vty*>
{
private:
    typedef boost::array<char, 6>  _Kty1;
    typedef boost::array<char, 10> _Kty2;
    typedef impl::OrderMap         HashMapImpl;

public:
    static HashMap* Create(uint32_t bucket_init_num, uint16_t deep_limit = 4, uint16_t bucket_ext_num = 8)
    {
        return (HashMap*)HashMapImpl::Create(bucket_init_num, deep_limit, bucket_ext_num);
    }

    int32_t Insert(const _Kty1& key_1, const _Kty2& key_2, _Vty* value)
    {
        return reinterpret_cast<HashMapImpl*>(this)->Insert(key_1, key_2, (void*)value);
    }

    int32_t Find(const _Kty1& key_1, const _Kty2& key_2, _Vty*** value)
    {
        return reinterpret_cast<HashMapImpl*>(this)->Find(key_1, key_2, (void***)value);
    }
};

template<typename _Vty>
class HashMap<std::array<char, 6>, std::array<char, 10>, _Vty*>
{
private:
    typedef std::array<char, 6>  _Kty1;
    typedef std::array<char, 10> _Kty2;
    typedef impl::StdOrderMap    HashMapImpl;

public:
    static HashMap* Create(uint32_t bucket_init_num, uint16_t deep_limit = 4, uint16_t bucket_ext_num = 8)
    {
        return (HashMap*)HashMapImpl::Create(bucket_init_num, deep_limit, bucket_ext_num);
    }

    int32_t Insert(const _Kty1& key_1, const _Kty2& key_2, _Vty* value)
    {
        return reinterpret_cast<HashMapImpl*>(this)->Insert(key_1, key_2, (void*)value);
    }

    int32_t Find(const _Kty1& key_1, const _Kty2& key_2, _Vty*** value)
    {
        return reinterpret_cast<HashMapImpl*>(this)->Find(key_1, key_2, (void***)value);
    }
};

template<typename _Vty>
class HashMap<std::array<char, 12>, std::array<char, 8>, _Vty*>
{
private:
    typedef std::array<char, 12>        _Kty1;
    typedef std::array<char, 8>         _Kty2;
    typedef impl::StdQuoteOrderMap      HashMapImpl;

public:
    static HashMap* Create(uint32_t bucket_init_num, uint16_t deep_limit = 4, uint16_t bucket_ext_num = 8)
    {
        return (HashMap*)HashMapImpl::Create(bucket_init_num, deep_limit, bucket_ext_num);
    }

    int32_t Insert(const _Kty1& key_1, const _Kty2 key_2, _Vty* value)
    {
        return reinterpret_cast<HashMapImpl*>(this)->Insert(key_1, key_2, (void*)value);
    }

    int32_t Find(const _Kty1& key_1, const _Kty2 key_2, _Vty*** value)
    {
        return reinterpret_cast<HashMapImpl*>(this)->Find(key_1, key_2, (void***)value);
    }
};

template<>
class HashMap<boost::array<char, 6>, boost::array<char, 10>, int64_t>
{
private:
    typedef boost::array<char, 6>  _Kty1;
    typedef boost::array<char, 10> _Kty2;
    typedef impl::CancelInfoMap    HashMapImpl;

public:
    static HashMap* Create(uint32_t bucket_init_num, uint16_t deep_limit = 4, uint16_t bucket_ext_num = 8)
    {
        return (HashMap*)HashMapImpl::Create(bucket_init_num, deep_limit, bucket_ext_num);
    }

    int32_t Insert(const _Kty1& key_1, const _Kty2& key_2, const int64_t value)
    {
        return reinterpret_cast<HashMapImpl*>(this)->Insert(key_1, key_2, value);
    }

    int32_t Find(const _Kty1& key_1, const _Kty2& key_2, int64_t** value)
    {
        return reinterpret_cast<HashMapImpl*>(this)->Find(key_1, key_2, value);
    }
};

template<>
class HashMap<std::array<char, 6>, std::array<char, 10>, int64_t>
{
private:
    typedef std::array<char, 6>         _Kty1;
    typedef std::array<char, 10>        _Kty2;
    typedef impl::StdCancelInfoMap      HashMapImpl;

public:
    static HashMap* Create(uint32_t bucket_init_num, uint16_t deep_limit = 4, uint16_t bucket_ext_num = 8)
    {
        return (HashMap*)HashMapImpl::Create(bucket_init_num, deep_limit, bucket_ext_num);
    }

    int32_t Insert(const _Kty1& key_1, const _Kty2& key_2, const int64_t value)
    {
        return reinterpret_cast<HashMapImpl*>(this)->Insert(key_1, key_2, value);
    }

    int32_t Find(const _Kty1& key_1, const _Kty2& key_2, int64_t** value)
    {
        return reinterpret_cast<HashMapImpl*>(this)->Find(key_1, key_2, value);
    }
};

template<>
class HashMap<boost::array<char, 6>, uint8_t, uint16_t>
{
private:
    typedef boost::array<char, 6>  _Kty1;
    typedef uint8_t                _Kty2;
    typedef impl::RuleV1Map        HashMapImpl;

public:
    static HashMap* Create(uint32_t bucket_init_num, uint16_t deep_limit = 4, uint16_t bucket_ext_num = 8)
    {
        return (HashMap*)HashMapImpl::Create(bucket_init_num, deep_limit, bucket_ext_num);
    }

    int32_t Insert(const _Kty1& key_1, const _Kty2 key_2, const uint16_t value)
    {
        return reinterpret_cast<HashMapImpl*>(this)->Insert(key_1, key_2, value);
    }

    int32_t Find(const _Kty1& key_1, const _Kty2 key_2, uint16_t** value)
    {
        return reinterpret_cast<HashMapImpl*>(this)->Find(key_1, key_2, value);
    }
};

template<>
class HashMap<std::array<char, 6>, uint8_t, uint16_t>
{
private:
    typedef std::array<char, 6>  _Kty1;
    typedef uint8_t              _Kty2;
    typedef impl::StdRuleV1Map   HashMapImpl;

public:
    static HashMap* Create(uint32_t bucket_init_num, uint16_t deep_limit = 4, uint16_t bucket_ext_num = 8)
    {
        return (HashMap*)HashMapImpl::Create(bucket_init_num, deep_limit, bucket_ext_num);
    }

    int32_t Insert(const _Kty1& key_1, const _Kty2 key_2, const uint16_t value)
    {
        return reinterpret_cast<HashMapImpl*>(this)->Insert(key_1, key_2, value);
    }

    int32_t Find(const _Kty1& key_1, const _Kty2 key_2, uint16_t** value)
    {
        return reinterpret_cast<HashMapImpl*>(this)->Find(key_1, key_2, value);
    }
};

template<>
class HashMap<boost::array<char, 6>, uint32_t, uint16_t>
{
private:
    typedef boost::array<char, 6>  _Kty1;
    typedef uint32_t               _Kty2;
    typedef impl::RuleV2Map        HashMapImpl;

public:
    static HashMap* Create(uint32_t bucket_init_num, uint16_t deep_limit = 4, uint16_t bucket_ext_num = 8)
    {
        return (HashMap*)HashMapImpl::Create(bucket_init_num, deep_limit, bucket_ext_num);
    }

    int32_t Insert(const _Kty1& key_1, const _Kty2 key_2, const uint16_t value)
    {
        return reinterpret_cast<HashMapImpl*>(this)->Insert(key_1, key_2, value);
    }

    int32_t Find(const _Kty1& key_1, const _Kty2 key_2, uint16_t** value)
    {
        return reinterpret_cast<HashMapImpl*>(this)->Find(key_1, key_2, value);
    }
};

template<>
class HashMap<std::array<char, 6>, uint32_t, uint16_t>
{
private:
    typedef std::array<char, 6>     _Kty1;
    typedef uint32_t                _Kty2;
    typedef impl::StdRuleV2Map      HashMapImpl;

public:
    static HashMap* Create(uint32_t bucket_init_num, uint16_t deep_limit = 4, uint16_t bucket_ext_num = 8)
    {
        return (HashMap*)HashMapImpl::Create(bucket_init_num, deep_limit, bucket_ext_num);
    }

    int32_t Insert(const _Kty1& key_1, const _Kty2 key_2, const uint16_t value)
    {
        return reinterpret_cast<HashMapImpl*>(this)->Insert(key_1, key_2, value);
    }

    int32_t Find(const _Kty1& key_1, const _Kty2 key_2, uint16_t** value)
    {
        return reinterpret_cast<HashMapImpl*>(this)->Find(key_1, key_2, value);
    }
};

}

#endif
