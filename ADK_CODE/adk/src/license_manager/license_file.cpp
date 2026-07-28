#include <adk/lic_util.h>
#include <adk/license_file.h>
#include <iostream>

using namespace std;

namespace adk_impl
{
namespace lic
{
LicenseFile* LicenseFile::lic_file_inst_ = nullptr;

// 获取 LicenseFile实例指针函数
LicenseFile* LicenseFile::GetInstance()
{
    return nullptr;
}

// LicenseFile构造函数
LicenseFile::LicenseFile()
{
}

// Open License文件 函数
ErrorCode_def LicenseFile::OpenFile(const std::string& file_name, const OpenMode mode)
{
    return ErrorCode::kSuccess;
}

void LicenseFile::Close()
{
}

// 读取License文件内容
ErrorCode_def LicenseFile::ReadLicence(std::string& text)
{
    return ErrorCode::kSuccess;
}

// 读取 License文件中的 LicenseHeader 结构体数据
void LicenseFile::ReadHeader(LicenseHeader& header)
{
}

// 读取 License文件中的 LicenseTail 结构体数据
void LicenseFile::ReadTail(LicenseTail& tail)
{
}

// 更新 License文件中的 LicenseTail 结构体数据
void LicenseFile::UpdateTail(const LicenseTail& tail)
{
}

// 更新 License文件中的 LicenseHeader 结构体数据
void LicenseFile::UpdateHeader(const LicenseHeader& header)
{
}

// 更新 License文件内容
bool LicenseFile::UpdateLicense(const std::string& text)
{
    return true;
}

// 写入 License文件内容
bool LicenseFile::WriteLicense(const LicenseTail& tail, const std::string& text)
{
    return true;
}

// 根据 LicenseTail结构体中 证书秘钥种子：char seed_key[256] 和 客户号：char customer[64] 生成密钥
string LicenseFile::GenerateKey(const char* seed, const string& salt_str)
{
    return "";
}

// 加密函数，用于加密 License文件 内容
string LicenseFile::EncryptStr(const string& orig_str)
{
    return "";
}

// 解密函数，用于解密 License文件 内容
string LicenseFile::DecryptStr(const string& encrypt_str)
{
    return "";
}

// 更新 License文件中的 LicenseHeader 结构体的 total_run_time 和 last_run_time 变量
void LicenseFile::UpdateTime(uint64_t time)
{
}

void LicenseFile::OnExit()
{
}
}
}