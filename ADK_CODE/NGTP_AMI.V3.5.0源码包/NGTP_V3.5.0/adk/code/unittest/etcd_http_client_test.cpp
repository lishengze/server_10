// etcd_http_client test
#define BOOST_TEST_MODULE etcd_http_client
#include <adk/etcd_http_client.h>
#include <adk/error_code.h>
#include <assert.h>
#include <atomic>
#include <boost/test/included/unit_test.hpp>
#include <chrono>
#include <iostream>

#include <unistd.h>
#include <string.h>

using namespace std;

/// ! 使用该测试用例, 需要手动启动一个DomainServer进行测试
std::string etcd_addr = "127.0.0.1:4200";   // 修改为启动的DomainServer etcd地址, 然后再进行测试

BOOST_AUTO_TEST_CASE(EtcdHttpClient)
{
    // 测试前创建超过1024大小的文件描述符
    int pipefd[1026];
    for (uint32_t i = 0; i < 513; ++i)
    {
        if (pipe(&pipefd[i * 2]) != 0)
        {
            std::cerr << "pipe error, errno: " << strerror(errno) << std::endl;
        }
    }

    adk::EtcdHttpClient client_1(etcd_addr, "domain");

    std::string no_exist_key = "Test/no_exist_key";
    std::string key_1 = "Test/TestKey1-" + std::to_string(time(NULL));
    std::string value_1 = R"({"value":"Test_TTTT1-111"})";
    std::string value_2 = R"({"value":"Test_TTTT1-222"})";

    /// PutValue
    if (client_1.PutValue(key_1, value_1) != adk::ErrorCode::kSuccess)
    {
        std::cout << "NOTICE: " << etcd_addr << " is not a valid etcd, please specify a valid etcd in test file !!!" << std::endl;
        return ;
    }

    std::string value;
    int64_t version = 0;

    /// GetValue
    BOOST_CHECK_EQUAL(client_1.GetValue(no_exist_key, &value, &version), adk::ErrorCode::kKeyNotExist);    // 获取不存在的key值
    BOOST_CHECK_EQUAL(client_1.GetValue(key_1, &value, &version), adk::ErrorCode::kSuccess);  // 获取上次put的key
    BOOST_CHECK_EQUAL(value, value_1);  // 校验value是否正确

    adk::EtcdHttpClient::CheckList check_list;
    adk::EtcdHttpClient::ValueList value_list;
    check_list[key_1] = version - 1;
    value_list[key_1] = value_2;

    /// CheckAndPutValues
    BOOST_CHECK_EQUAL(client_1.CheckAndPutValues(check_list, value_list), adk::ErrorCode::kTryAgain);   // version不对, 返回kTryAgain
    BOOST_CHECK_EQUAL(client_1.GetValue(key_1, &value, &version), adk::ErrorCode::kSuccess);
    BOOST_CHECK_EQUAL(value, value_1);  // 此时获取的value仍是之前的value

    check_list[key_1] = version;
    BOOST_CHECK_EQUAL(client_1.CheckAndPutValues(check_list, value_list), adk::ErrorCode::kSuccess);  // version正确, 写入成功,
    BOOST_CHECK_EQUAL(client_1.GetValue(key_1, &value, &version), adk::ErrorCode::kSuccess);
    BOOST_CHECK_EQUAL(value, value_2);      // 获取到更新后的value值

    /// CheckAndPut写入一个新的key
    version = 0;    // 新key对应的version 应该是0.
    std::string key_2 = "Test/TestKey2-" + std::to_string(time(NULL));
    std::string value_3 = R"({"value":"CheckAndPut a new value."})";
    BOOST_CHECK_EQUAL(client_1.GetValue(key_2, &value, &version), adk::ErrorCode::kKeyNotExist);    // 获取不存在的key值

    check_list.clear(), value_list.clear();
    check_list[key_2] = version;
    value_list[key_2] = value_3;

    BOOST_CHECK_EQUAL(client_1.CheckAndPutValues(check_list, value_list), adk::ErrorCode::kSuccess);
    BOOST_CHECK_EQUAL(client_1.GetValue(key_2, &value, &version), adk::ErrorCode::kSuccess);
    BOOST_CHECK_EQUAL(value, value_3);

    /// 一个无效的etcd地址列表
    std::string invalid_etcd_list = "127.0.0.1:50345,127.0.0.1:32455";
    adk::EtcdHttpClient client_2(invalid_etcd_list, "domain");
    BOOST_CHECK_EQUAL(client_2.GetValue(key_2, &value, &version), adk::ErrorCode::kFailure);

    /// 有效ip地址在 etcd地址列表 最后面的情况
    std::string notall_valid_etcd_list = invalid_etcd_list + "," + etcd_addr;
    adk::EtcdHttpClient client_3(notall_valid_etcd_list, "domain");
    BOOST_CHECK_EQUAL(client_3.GetValue(key_2, &value, &version), adk::ErrorCode::kSuccess);
    BOOST_CHECK_EQUAL(value, value_3);


    check_list.clear(), value_list.clear();
    check_list[key_2] = version;
    value_list[key_2] = value_2;
    BOOST_CHECK_EQUAL(client_1.CheckAndPutValues(check_list, value_list), adk::ErrorCode::kSuccess);
    BOOST_CHECK_EQUAL(client_1.GetValue(key_2, &value, &version), adk::ErrorCode::kSuccess);
    BOOST_CHECK_EQUAL(value, value_2);

    for (uint32_t i = 0; i < 1026; ++i)
    {
        close(pipefd[i]);
    }
}
