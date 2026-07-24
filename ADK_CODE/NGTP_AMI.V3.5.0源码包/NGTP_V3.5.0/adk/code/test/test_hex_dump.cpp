#include <adk/util.h>

int main(int argc, char const *argv[])
{
    uint32_t width = 1;
    if (argc != 1)
        width = atoi(argv[1]);

    uint32_t memory1[8] = { 0x12345678, 0x12345678, 0x12345678, 0x12345678, 
                            0x12345678, 0x12345678, 0x12345678, 0x12345678 };
    printf("%s\n", adk::MemoryHexDump(memory1, sizeof(memory1), width));


    uint32_t memory2[4] = { 0x12345678, 0x12345678, 0x12345678, 0x12345678};
    printf("%s\n", adk::MemoryHexDump(memory2, sizeof(memory2), width));


    uint32_t memory3[16] = { 0x12345678, 0x12345678, 0x12345678, 0x12345678, 
                             0x12345678, 0x12345678, 0x12345678, 0x12345678,
                             0x12345678, 0x12345678, 0x12345678, 0x12345678, 
                             0x12345678, 0x12345678, 0x12345678, 0x12345678 };
    printf("%s\n", adk::MemoryHexDump(memory3, sizeof(memory3), width));

    uint32_t memory4[1] = { 0x12345678 };
    printf("%s\n", adk::MemoryHexDump(memory1, sizeof(memory4), width));                             

    uint32_t memory5[2] = { 0x12345678, 0x12345678 };
    printf("%s\n", adk::MemoryHexDump(memory1, sizeof(memory5), width));     

    uint32_t memory6[3] = { 0x12345678, 0x12345678, 0x12345678 };
    printf("%s\n", adk::MemoryHexDump(memory1, sizeof(memory6), width));     

    const char* hello = "hello world!12";
    printf("%s\n", adk::MemoryHexDump(hello, strlen(hello), width));

    const char* hello1 = "hello world!1";
    printf("%s\n", adk::MemoryHexDump(hello1, strlen(hello1), width));

    const char* text = "this is a test for hex dump";
    printf("%s\n", adk::MemoryHexDump(text, strlen(text), width));

    const char* text1 = "this is a test for hex dump, it really works";
    printf("%s\n", adk::MemoryHexDump(text1, strlen(text1), width));

    const char* text2 = "this is a test for hex dump, it r";
    printf("%s\n", adk::MemoryHexDump(text2, strlen(text2), width));

    return 0;
}

