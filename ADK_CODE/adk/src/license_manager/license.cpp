#include <adk/license.h>
#include <adk/license_file.h>
#include <adk/lic_util.h>
#include <iostream>

using namespace std;

namespace adk_impl
{
namespace lic
{
    License* License::lic_inst_ = nullptr;
    LicenseFile* lic_file_inst_ = nullptr;
    
    License::License()
    {
    }

    // License初始化函数，参数为license文件路径
    int32_t License::Init(const std::string& path)
    {
        return ErrorCode::kSuccess;
    }

    // 获取 License类 实例指针
    License * License::GetInstance(const std::string& path)
    {
        return nullptr;
    }

    // 检查是否过期
    bool License::IsExpired()
    {
        return true;
    }

    // 检查是否到了截止日
    bool License::IsDeadline()
    {
        return true;
    }

    // 检查设备ID是否一致 检查内容包括硬盘UUID，主板信息
    bool License::CheckDeviceID()
    {
        return true;
    }

    uint64_t License::RemainTime()
    {
        return uint64_t(-1);
    }

    std::string License::GetLastRunTime()
    {
        return "";
    }

    std::string License::GetExpiredDate()
    {
        return "";
    }

    std::string License::GetDeadline()
    {
        return "";
    }

    std::vector<std::string> License::GetDeviceID()
    {
        return std::vector<std::string>();
    }

    std::vector<std::string> License::GetMachineID()
    {
        return std::vector<std::string>();
    }

    std::vector<std::string> License::GetCpuName()
    {
        return std::vector<std::string>();
    }

    std::string License::GetLicenseType()
    {
        return "";
    }

    uint32_t License::GetHostCountLimit()
    {
        return uint32_t(0);
    }

    uint32_t License::GetContextCountLimit()
    {
        return uint32_t(0);
    }

    Property License::GetGenericProperty()
    {
        return Property();
    }

    Property License::GetProductProperty()
    {
        return Property();
    }

    std::string License::Encrypt(const std::string& in_str, const std::string& salt_str)
    {
        return "";
    }

    std::string License::Decrypt(const std::string& in_str, const std::string& salt_str)
    {
        return "";
    }

    // 更新license中的时间
    void License::UpdateTime()
    {
    }

    void License::OnExit()
    {
    }
}
}

