#ifndef _LICENSE_IMPL_FILE_H__
#define _LICENSE_IMPL_FILE_H__
#include <string>
#include "lic_util.h"
#include <fstream>

namespace adk_impl
{
namespace lic
{

#pragma pack(push)  // 保存对齐状态
#pragma pack(8)  // 设定为8字节对齐

// header   28
struct LicenseHeader
{
    uint32_t tail_offset;       // LicenseTail在文件中的起始位置
    uint32_t lic_length;        // License 大小
    uint64_t last_run_time;     // 上一次调用接口的时间
    uint64_t total_run_time;    // 累计运行时间
    char eigen_val[8];          // 特征值  ARCH_LIC
};

//  tail
struct LicenseTail
{
    char customer[64];          // 客户号
    char version[256];			// 版本号
    char seed_key[256];         // 证书秘钥种子
};

constexpr size_t HeaderLength = sizeof(LicenseHeader);
constexpr size_t TailLength = sizeof(LicenseTail);
#pragma pack(pop)  // 恢复对齐状态

enum OpenMode
{
    kUpdate = 0,
    kWrite = 1,
};


class LicenseFile
{
public:
    static LicenseFile* GetInstance();

    ErrorCode_def OpenFile(const std::string& file_name, const OpenMode mode);

    void Close();

    ErrorCode_def ReadLicence(std::string& text);
    
    void ReadHeader(LicenseHeader& header);

    void ReadTail(LicenseTail& tail);

    void UpdateHeader(const LicenseHeader& header);

    void UpdateTail(const LicenseTail& tail);

    // 更新License文件内容 成功返回true;失败返回false
    bool UpdateLicense(const std::string& text);

    // 写入License文件内容 成功返回true;失败返回false
    bool WriteLicense(const LicenseTail& tail, const std::string& text);

    std::string GenerateKey(const char* seed, const std::string& salt_str);

    std::string EncryptStr(const std::string& orig_str);

    std::string DecryptStr(const std::string& encrypt_str);

    const LicenseHeader& Header()
    {
        return lic_header_;
    }

    void UpdateTime(uint64_t time);

    void OnExit();

protected:
    LicenseFile();

private:
    static LicenseFile* lic_file_inst_;

    uint32_t file_end_;     //文件的末尾或者长度
    uint32_t lic_offset_;   //License 的起始位置
    uint32_t lic_length_;   //License 的文件大小
    std::fstream    lic_file_;
    LicenseHeader   lic_header_;
    LicenseTail     lic_tail_;
    std::string     lic_key_;
};
}
}
#endif