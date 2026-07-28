#ifndef ADK_IMPL_LICENSE_H_
#define ADK_IMPL_LICENSE_H_
#include <string>
#include <vector>
#include <boost/property_tree/ptree.hpp>
#include <adk/property.h>
#include <adk/log.h>

namespace adk_impl
{
namespace lic
{

namespace Segment
{
    const std::string GenericSegment = "Generic";
    const std::string ServiceSegment = "Service";
    const std::string UserSegment = "UserDefined";

}

const std::string kLicenseFile("ADK_LIC_FILE");
//singleton
class License
{
    ADK_LOG_DECLARE_AC(60001);
public:
    /**
     * @breaf  获取实例，线程安全
     *
     * @param[in]   path  证书路径
     *
     * @note     若path为空，将会读取libadk.so中的证书信息
     */
    static License* GetInstance(const std::string& path = std::string());

    /**
     * @breaf   检查是否到期
     * 
     * @note    检查license是否到期, 到期之后可以调用该接口决定是否继续许可使用
     */
    bool IsExpired();

    /**
     * @breaf   检查是否过截止日期
     * 
     * @note    检查是否过截止日期, 到期之后可以调用该接口决定是否继续许可使用
     */
    bool IsDeadline();

    /**
     * @breaf   检查设备ID是否一致
     *
     * @note    检查内容包括硬盘UUID，主板信息
     */
    bool CheckDeviceID();

    /**
     * @breaf   获取license到期剩余时间
     * 
     * @return  license到期剩余天数
     */
    uint64_t RemainTime();

    /**
     * @breaf   获取license上次运行时间 
     * 
     * @return  license上次运行的时间，格式: YYYYMMDDHHMMSS
     */
    std::string GetLastRunTime();

    /**
     * @breaf   获取到期日 YYYYMMDDHHMMSS
     * 
     * @return  license到期日期字符串，YYYYMMDDHHMMSS
     */
    std::string GetExpiredDate();

    /**
     * @breaf   获取截止日 
     * 
     * @return  license截止日期字符串，YYYYMMDDHHMMSS
     */
    std::string GetDeadline();

    /**
     * @breaf   获取设备ID
     * 
     * @return  磁盘UUID字符串数组
     */
    std::vector<std::string> GetDeviceID();

    /**
     * @breaf   获取机器标识ID
     * 
     * @return  机器标识ID字符串数组
     */
    std::vector<std::string> GetMachineID();

    /**
     * @breaf   获取Cpu Name
     * 
     * @return  cpu名字字符串数组
     */
    std::vector<std::string> GetCpuName();

    /**
     * @breaf   获取license类型
     * 
     * @return  license到期后行为字符串
     */
    std::string GetLicenseType();

    /**
     * @breaf   获取Host限制数量
     * 
     * @return  Host的限制数量
     */
    uint32_t GetHostCountLimit();

    /**
     * @breaf   获取Context限制数量
     * 
     * @return  Context的限制数量
     */
    uint32_t GetContextCountLimit();

    /**
     * @breaf   获取通用段信息
     * 
     * @return  Property
     */
    Property GetGenericProperty();

    /**
     * @breaf   获取产品自定义段属性
     *
     * @return  Property
     */
    Property GetProductProperty();

    /**
     * @breaf   对字符串加密
     *
     * @param[in]   in_str  原始字符串
     *
     * @param[in]   salt_str  混淆用的字符串
     *
     * @return  加密后的字符串，通过.size() 获取真实长度
     */
    std::string Encrypt(const std::string& in_str, const std::string& salt_str);

    /**
     * @breaf   对字符串解密
     *
     * @param[in]   in_str  加密的字符串
     *
     * @param[in]   salt_str  混淆用的字符串
     *
     * @return  原始字符串
     */
    std::string Decrypt(const std::string& in_str, const std::string& salt_str);

    /**
     * @breaf   进程退出，更新license文件
     */
    void OnExit();

protected:
    License();

    int32_t Init(const std::string& path);

public:
    void UpdateTime();


private:
    static License* lic_inst_;
    uint64_t    time_cuurent_;
    uint64_t    expire_time_;
    uint64_t    deadline_time_;
    uint64_t    last_run_time_;

    std::vector<std::string>  uuid_vec_;
    
    Property lic_property_;
    Property generic_property_;
    Property service_property_;
    Property user_property_;
};


}
}
#endif
