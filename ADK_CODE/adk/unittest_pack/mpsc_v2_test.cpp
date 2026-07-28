#define BOOST_TEST_MODULE mpsc_test_cases
//#include <boost/test/included/unit_test.hpp>

#include <adk_pack/error_code.h>
#include <adk_pack/lock_free_unbounded_queue_variant.h>
#include <boost/test/included/unit_test.hpp>
#include <map>
#include <set>
#include <string>
#include <string.h>
#include <thread>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>

#define MY_REQUIRE(exp) do{if(!(exp)){BOOST_REQUIRE(false);}}while(0) 

volatile bool is_bug_on = false;
using namespace adk::variant;
using adk::ErrorCode;


const static uint64_t kMaxDataLen = 1024lu * 2;


/**
 * TestItem 用于测试入队和出队的内容的正确性
 *     入队的时候 data_ 存放一段随机的内容, 然后 hash_ 存入这一段随机内容的哈希值
 *     出队的时候重新根据 data_ 来计算哈希值, 看是否与 hash_ 字段一致 
*/
struct TestItem
{
    uint64_t hash_;
    char data_[kMaxDataLen];

    void Init()
    {
        std::string s;
        MakeContentRandomLen(kMaxDataLen, kMaxDataLen, s, hash_);
        memcpy(data_, s.c_str(), s.length());
    }

    TestItem()                              = default;
    TestItem(const TestItem&)               = default;
    TestItem(TestItem&&)                    = default;
    TestItem& operator=(const TestItem&)    = default;
    TestItem& operator=(TestItem&&)         = default;
    ~TestItem()                             = default;

    bool CheckContent() const
    {
        std::string s(data_, kMaxDataLen);
        auto carried_hash    = hash_;
        auto calculated_hash = std::hash<std::string>()(s);
        return carried_hash == calculated_hash;
    }

    static void MakeContent(uint64_t data_len, std::string& rs, uint64_t& hash_int)
    {  // 制造一个随机的字符串, 并计算出它的哈希值
        char temp[kMaxDataLen];
        int fd = open("/dev/random", O_RDONLY | O_CLOEXEC , 0755);
        MY_REQUIRE(fd > 0);
        read(fd, temp, data_len);
        close(fd);
        rs       = std::string(temp, data_len);
        hash_int = std::hash<std::string>()(rs);
    }

    static void MakeContentRandomLen(uint64_t len_min, uint64_t len_max, std::string& rs, uint64_t& hash_int)
    {
        len_max = std::min(len_max, kMaxDataLen);
        struct timespec t;
        clock_gettime(CLOCK_REALTIME, &t);
        srandom((uint64_t)t.tv_sec * 1000000000 + t.tv_nsec);
        uint64_t data_len =  (len_min == len_max) ? len_min : len_min + random() % (len_max - len_min);
        MakeContent(data_len, rs, hash_int);
    }
};


void QConcumserT(MPSCUnboundedQueue<TestItem>* mq, uint64_t total_msgs)
{
    uint64_t counter = 0;
    while (counter != total_msgs)
    {
        //TestItem ti;
        TestItem* ti = nullptr;
        ti = mq->Top();
        //std::cout << "==============Pop result:" << ti << std::endl;
        if (ti)
        {
            counter++;
            //std::cout << counter << std::endl;
            auto ret = ti->CheckContent();
            MY_REQUIRE(ret == true);
            mq->Commit(ti);
        }else
        {
            //std::cout << "Queue Empty!" << std::endl;
        }
    }
}

void QProducerT(MPSCUnboundedQueue<TestItem>* mq, uint64_t total_msgs)
{
    uint64_t counter = 0;
    while (counter++ != total_msgs)
    {
        auto* ti = mq->Alloc();
        new ((void*)ti) TestItem;
        ti->Init();
        auto ret = mq->Post(ti);
        //std::cout << "--------------Post result:" << ret << std::endl;
        MY_REQUIRE(ret == ErrorCode::kSuccess);
        //if(ret != ErrorCode::kSuccess)
        //{
        //    BOOST_REQUIRE(false);
        //}
    }
}

BOOST_AUTO_TEST_CASE(mpsc_alloc_post)
{
    // 启动 kProducerNumber 个生产者线程
    constexpr uint64_t kProducerNumber = 6;

    // 每个生产者生产 kElementCounter 个TestItem
    constexpr uint64_t kElementCounter = 10000;

    std::cout << "start MPSCUnboundedQueue test, element counter: " << kElementCounter
              << "\t thread num: " << kProducerNumber << std::endl;
    MPSCUnboundedQueue<TestItem>* mq_p = MPSCUnboundedQueue<TestItem>::Create("test_mpsc");
    MY_REQUIRE(mq_p->length() == 0);

    std::vector<std::thread> p_vec;
    // 创建多个线程，竞争同一个队列的入队操作
    for (uint64_t i = 0; i < kProducerNumber; ++i)
    {
        p_vec.emplace_back(std::thread(QProducerT, mq_p, kElementCounter));
    }

    MY_REQUIRE(mq_p->length() >= 0);
    MY_REQUIRE(mq_p->length() <= kProducerNumber * kElementCounter);

    std::thread c = std::thread(QConcumserT, mq_p, kProducerNumber * kElementCounter);

    for (std::thread& thrd : p_vec)
    {
        thrd.join();
    }

    MY_REQUIRE(mq_p->length() < kProducerNumber * kElementCounter);

    c.join();

    MY_REQUIRE(mq_p->length() == 0);
}
