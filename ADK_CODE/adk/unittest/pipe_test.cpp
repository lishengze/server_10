#define BOOST_TEST_MODULE pipe
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk/pipe.h>
#include <adk/error_code.h>

#include <set>
#include <string>
#include <vector>
#include <map>

using namespace adk;

BOOST_AUTO_TEST_CASE(read_side_first)
{
    Pipe* pobj = Pipe::Create("/adk_test", PipeFlag::kReadOnly);
    BOOST_REQUIRE(pobj != nullptr);

    char buf[1024];
    uint32_t len = 10;
    BOOST_REQUIRE(pobj->Read(buf, len, 0) == ErrorCode::kFailure);
    BOOST_REQUIRE(len == 10);

    Pipe* pobj_w = Pipe::Create("/adk_test", PipeFlag::kWriteOnly);
    BOOST_REQUIRE(pobj_w != nullptr);

    BOOST_REQUIRE(pobj->Read(buf, len, 0) == ErrorCode::kWouldblock);
    BOOST_REQUIRE(len == 10);

    std::string content("hello world");
    uint32_t wsize = content.size();
    BOOST_REQUIRE(pobj_w->Write(content.c_str(), wsize, 0) == ErrorCode::kSuccess);
    BOOST_REQUIRE(wsize == content.size());

    len = 100;
    BOOST_REQUIRE(pobj->Read(buf, len, 0) == ErrorCode::kSuccess);
    BOOST_REQUIRE(len == content.size());

    delete pobj;
    delete pobj_w;
    BOOST_REQUIRE(Pipe::Destroy("/adk_test") == ErrorCode::kSuccess);
}


BOOST_AUTO_TEST_CASE(write_side_first)
{
    Pipe* pobj = Pipe::Create("/adk_test", PipeFlag::kWriteOnly);
    BOOST_REQUIRE(pobj == nullptr);

    char buf[1024];
    uint32_t len = 10;
    Pipe* pobj_r = Pipe::Create("/adk_test", PipeFlag::kReadOnly);
    BOOST_REQUIRE(pobj_r != nullptr);

    pobj = Pipe::Create("/adk_test", PipeFlag::kWriteOnly);
    BOOST_REQUIRE(pobj != nullptr);

    delete pobj_r;

    BOOST_REQUIRE(pobj->Write(buf, len, 0) == ErrorCode::kFailure);
    BOOST_REQUIRE(len == 10);

    pobj_r = Pipe::Create("/adk_test", PipeFlag::kReadOnly);
    BOOST_REQUIRE(pobj_r != nullptr);

    BOOST_REQUIRE(pobj->Write(buf, len, 0) == ErrorCode::kSuccess);
    BOOST_REQUIRE(len == 10);

    delete pobj_r;
    delete pobj;
    BOOST_REQUIRE(Pipe::Destroy("/adk_test") == ErrorCode::kSuccess);
}

BOOST_AUTO_TEST_CASE(write_side_first_with_timeout)
{
    Pipe* pobj = Pipe::Create("/adk_test", PipeFlag::kWriteOnly);
    BOOST_REQUIRE(pobj == nullptr);

    const int32_t MAX_BUF_LEN = 1024*65;
    char buf[MAX_BUF_LEN];

    Pipe* pobj_r = Pipe::Create("/adk_test", PipeFlag::kReadOnly);
    BOOST_REQUIRE(pobj_r != nullptr);

    pobj = Pipe::Create("/adk_test", PipeFlag::kWriteOnly);
    BOOST_REQUIRE(pobj != nullptr);

    volatile bool is_running = true;

    auto consumer = [&](){
        uint32_t len = 1024;
        while (is_running)
        {
            pobj_r->Read(buf, len, 10000);
        }
    };

    auto producer = [&](){
        uint32_t len = MAX_BUF_LEN;
        while (is_running)
        {
            // give 1s timeout, expect len equal to MAX_BUF_LEN
            pobj->Write(buf, len, 1000000000);
            BOOST_REQUIRE(len == MAX_BUF_LEN);
            is_running = false;
        }
    };

    boost::thread th1(consumer);
    boost::thread th2(producer);

    th1.join();
    th2.join();

    delete pobj_r;
    delete pobj;
    BOOST_REQUIRE(Pipe::Destroy("/adk_test") == ErrorCode::kSuccess);

}

BOOST_AUTO_TEST_CASE(destroy_and_create)
{
    char buf[1024];
    uint32_t len = 10;
    Pipe* pobj_r = Pipe::Create("/adk_test", PipeFlag::kReadOnly);
    BOOST_REQUIRE(pobj_r != nullptr);

    Pipe* pobj = Pipe::Create("/adk_test", PipeFlag::kWriteOnly);
    BOOST_REQUIRE(pobj != nullptr);

    delete pobj_r;
    BOOST_REQUIRE(Pipe::Destroy("/adk_test") == ErrorCode::kSuccess);

    BOOST_REQUIRE(pobj->Write(buf, len, 0) == ErrorCode::kFailure);
    BOOST_REQUIRE(len == 10);

    pobj_r = Pipe::Create("/adk_test", PipeFlag::kReadOnly);
    BOOST_REQUIRE(pobj_r != nullptr);

    BOOST_REQUIRE(pobj->Write(buf, len, 0) == ErrorCode::kFailure);
    BOOST_REQUIRE(len == 10);

    delete pobj;
    pobj = Pipe::Create("/adk_test", PipeFlag::kWriteOnly);
    BOOST_REQUIRE(pobj != nullptr);
    BOOST_REQUIRE(pobj->Write(buf, len, 0) == ErrorCode::kSuccess);
    BOOST_REQUIRE(len == 10);
}
