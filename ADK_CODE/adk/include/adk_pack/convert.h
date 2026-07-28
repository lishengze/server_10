/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_CONVERT_H_
#define ADK_CONVERT_H_

#include "arch/generic.h"

#include <stdint.h>
#include <assert.h>

#include <string>

namespace adk
{

constexpr int32_t kDecimalismBase = 10;
constexpr int32_t kUint64MaxLength = 20;
constexpr int32_t kDecConvertTblLength = 191;

class DecConvertTbl
{
public:
    DecConvertTbl()
    {
        int32_t  index = 0;
        int64_t factor = 1;
        for (int32_t factor_index = 0; factor_index < kUint64MaxLength - 1; ++factor_index)
        {
            const int64_t factor_base = factor;

            table_[index++] = 0;
            for (int32_t ns_index = 1; ns_index < 10; ++ns_index, ++index)
            {
                table_[index] = factor;
                factor += factor_base;
            }
        }

        table_[index] = factor;
    }
    int64_t table_[kDecConvertTblLength];
};

class DecimalConvert
{
public:
    ///> Fast-numeric-string-to-int
    inline static int32_t String8ToInt(const char* const num)
    {
        int64_t sum = *(int64_t*)num;
        sum = (sum & 0x0F0F0F0F0F0F0F0F) * 2561 >> 8;
        sum = (sum & 0x00FF00FF00FF00FF) * 6553601 >> 16;
        sum = (sum & 0x0000FFFF0000FFFF) * 42949672960001 >> 32;
        return (int32_t)sum;
    }

    inline static int16_t String4ToInt(const char* const num)
    {
        int32_t sum = *(int32_t*)num;
        sum = (sum & 0x0F0F0F0F) * 2561 >> 8;
        sum = (sum & 0x00FF00FF) * 6553601 >> 16;
        return (int16_t)sum;
    }

    inline static int64_t StringToIntRaw(const char* const num_str, size_t length)
    {
        int64_t result = 0;
        for (size_t index=0; index < length; ++index)
        {
            result *= 10;
            result += (num_str[index] & 0x0F);
        }

        return result;
    }

    inline static int64_t StringToIntRaw(const std::string& num_str)
    {
        return StringToIntRaw(num_str.c_str(), num_str.length());
    }

    template<int32_t length>
    inline static int64_t StringToIntRaw(const char* const num_str)
    {
        if (8 == length)
        {
            return String8ToInt(num_str);
        }
        else if (4 == length)
        {
            return String4ToInt(num_str);
        }

        return StringToIntRaw(num_str, length);
    }

    inline static int64_t StringToInt(const char* const num_str, size_t length)
    {
        int64_t result = 0;
        int32_t factor_base = 0;
        for (int32_t index = int32_t(length - 1); index >= 0; --index)
        {
            result += dec_convert_table_.table_[factor_base + (num_str[index] & 0x0F)];
            factor_base += kDecimalismBase;
        }
        return result;
    }

    inline static int64_t StringToInt(const std::string& num_str)
    {
        return StringToInt(num_str.c_str(), num_str.length());
    }

    template<int32_t length>
    inline static int64_t StringToInt(const char* const num_str)
    {
        if (8 == length)
        {
            return String8ToInt(num_str);
        }
        else if (4 == length)
        {
            return String4ToInt(num_str);
        }

        return StringToInt(num_str, length);
    }

private:
    static DecConvertTbl dec_convert_table_;
};

class FloatConvert
{
public:
    template<int32_t coefficient>
    static int64_t Convert(const char* const float_str)
    {
        assert(float_str);

        const char* begin;
        const char* end;
        const char* point;
        
        bool is_negative;
        const char* cur = float_str;

        // start character: '+'(2B) '-'(2D) '0~9'
        do {
            if ((*cur != ' ') && (*cur != '0'))
            {
                break;
            }
        } while (*(++cur) != '\0');

        if ((is_negative = ('-' == *cur)) || ('+' == *cur))
        {
            ++cur;
        }

        if (ADK_UNLIKELY((*cur < '0') || (*cur > '9')) && ADK_UNLIKELY('.' != *cur))
        {
            return 0;
        }

        begin = cur;
        end = cur;
        point = nullptr;

        // search character end
        do {
            if (ADK_UNLIKELY('.' == *end))
            {
                if (ADK_UNLIKELY(point != nullptr))
                {
                    break;
                }    
                point = end;
            }
            else if (ADK_UNLIKELY(*end < '0' || *end > '9'))
            {
                break;
            }
        } while (*(++end) != '\0');

        if (ADK_UNLIKELY(begin == end))
        {
            return 0;
        }

        if (ADK_UNLIKELY(nullptr == point))
        {
            point = end;
        }

        const int64_t result = Convert<coefficient>(begin, point, end);
        return ADK_UNLIKELY(is_negative) ? -result : result;
    }

    template<int32_t coefficient>
    static int64_t RawConvert(const char* const begin, const char* const end)
    {
        int64_t result = 0;
        const char* cur;
        for (cur = begin; cur < end; ++cur)
        {
            if ('.' == *cur)
            {
                ++cur;
                break;
            }

            result *= 10;
            result += (*cur & 0x0F);
        }

        for (int32_t index=0; index<coefficient; ++index)
        {
            result *= 10;
            if (cur < end)
            {
                result += (*(cur++) & 0x0F);
            }
        }
        return result + ((*cur & 0x0F) > 4);
    }

protected:
    template<int32_t coefficient>
    static int64_t Convert(const char* const begin, const char* const point, const char* const end)
    {
        int64_t result = 0;

        const char* cur;
        for (cur = begin; cur < point; ++cur)
        {
            result *= 10;
            result += (*cur & 0x0F);
        }

        cur += (point != end);

        for (int32_t index = 0; index < coefficient; ++index)
        {
            result *= 10;
            if (cur < end)
            {
                result += (*(cur++) & 0x0F);
            }
        }

        return result + (((uint8_t)(*cur - '5')) < 5);
    }
};

}

#endif