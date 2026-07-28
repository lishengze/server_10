#include <msg_dumper_c_api.h>

#include <adk/util.h>


extern "C" bool DumpMsgAsPlainString(
            const char* msg_data, unsigned long data_len,
            unsigned long* output_len, char* output)
{
    static uint32_t counter = 0;

    if (counter == 0)
    {
        counter = 1; // 模拟无法解析
        return false;
    }

    if (counter == 1)
    {
        *output_len = 1024*64;  // 模拟需要更大的output buffer
        counter = 2;
        return true;
    }

    if (counter == 2)
    {
        counter == 0;   // 重置状态
    }

    auto* output_temp = adk::MemoryHexDump(msg_data, data_len);

    *output_len = 0;  // trigger error
    memcpy(output, output_temp, *output_len);
    return true;
}

