#define BOOST_TEST_MODULE array_queue_stats
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk/array_queue.h>
#include <adk/lock_free_msg_queue.h>
#include <adk/util.h>

#include <map>
#include <set>
#include <string.h>
#include <string>
#include <vector>

#include <fcntl.h> /* For O_* constants */
#include <sys/mman.h>
#include <sys/stat.h> /* For mode constants */
#include <time.h>

using namespace adk;

BOOST_AUTO_TEST_CASE(InitAndBasic)
{
    ArrayQueue<uint64_t>* aq  = ArrayQueue<uint64_t>::GetInstance();
    ArrayQueue<uint64_t>* aq1 = ArrayQueue<uint64_t>::GetInstance();
    ArrayQueue<uint32_t>* aq2 = ArrayQueue<uint32_t>::GetInstance();

    // 元素类型不同，验证单例模式正确性
    BOOST_REQUIRE(aq == aq1);
    BOOST_REQUIRE(reinterpret_cast<void*>(aq) != reinterpret_cast<void*>(aq2));

    // 标识模板参数不同，验证单例模式正确性
    ArrayQueue<uint64_t, 8, 0>* aq3 = ArrayQueue<uint64_t, 8, 0>::GetInstance();
    ArrayQueue<uint64_t, 8, 1>* aq4 = ArrayQueue<uint64_t, 8, 1>::GetInstance();
    BOOST_REQUIRE(aq == aq3);
    BOOST_REQUIRE(reinterpret_cast<void*>(aq3) != reinterpret_cast<void*>(aq4));

    // 初始化，创建共享内存队列
    int32_t ec = aq->Init([](uint8_t index) -> MPSCQueue* {
        std::string shm_name = "shm_array_queue_" + std::to_string(index);
        shm_unlink(shm_name.c_str());

        adk::MQManager* mq_manager = adk::MQManager::Create(shm_name, sizeof(uint64_t), 128, 0);
        MPSCQueue* mq              = mq_manager->CreateSharedMPSCQueue("array_queue", 128 * 1024);
        return mq;
    });
    BOOST_REQUIRE(ec == adk::ErrorCode::kSuccess);

    // 获取数组中的第一个队列
    MPSCQueue* mq = aq->GetQueue(0);

    // 基本使用，Push/Pop
    BOOST_REQUIRE(aq->Push(1) == adk::ErrorCode::kSuccess);
    uint64_t val = 0;
    BOOST_REQUIRE(aq->Pop(val) == adk::ErrorCode::kSuccess);
    BOOST_REQUIRE(val == 1);

    // Push三次验证队列长度
    BOOST_REQUIRE(aq->Push(2) == adk::ErrorCode::kSuccess);
    BOOST_REQUIRE(aq->Push(3) == adk::ErrorCode::kSuccess);
    BOOST_REQUIRE(aq->Push(4) == adk::ErrorCode::kSuccess);
    BOOST_REQUIRE(mq->length() == 3);

    // Pop出元素验证元素值和队列长度
    BOOST_REQUIRE(aq->Pop(val) == adk::ErrorCode::kSuccess);
    BOOST_REQUIRE(val == 2);
    BOOST_REQUIRE(aq->Pop(val) == adk::ErrorCode::kSuccess);
    BOOST_REQUIRE(val == 3);
    BOOST_REQUIRE(aq->Pop(val) == adk::ErrorCode::kSuccess);
    BOOST_REQUIRE(val == 4);
    BOOST_REQUIRE(mq->length() == 0);
    BOOST_REQUIRE(aq->Pop(val) == adk::ErrorCode::kQueueEmpty);

    BOOST_REQUIRE(aq->GetNrQueueUsed() == 1);
    BOOST_REQUIRE(aq1->GetNrQueueUsed() == 1);
}

struct TestData
{
    uint64_t id;
    uint64_t seq;
};

template <size_t N>
void Producer(ArrayQueue<TestData, N>* aq, uint64_t total, uint64_t id)
{
    adk::SimpleRateController<> rate_ctrl(1000000);
    TestData data;
    data.id          = id;
    uint64_t counter = 0;
    do
    {
        rate_ctrl.Wait();
        ++counter;
        data.seq = counter;
        while (aq->Push(data) != ErrorCode::kSuccess)
        {
            usleep(10);
        }

    } while ((--total) != 0);
}

template <size_t N>
void Consumer(ArrayQueue<TestData, N>* aq, uint64_t total, uint16_t thread_num)
{
    uint32_t nr_pop = 0;
    struct timespec begin, end, begin1, end1;
    std::vector<uint64_t> pdata(thread_num, 0);
    std::vector<uint64_t> seq_vec;
    uint64_t tmp_total = total;
    seq_vec.reserve(thread_num);
    TestData data;
    clock_gettime(CLOCK_REALTIME, &begin);
    clock_gettime(CLOCK_REALTIME, &begin1);
    do
    {
        while (aq->Pop(data) != adk::ErrorCode::kSuccess);
        ++nr_pop;
        // std::cout << "id:" << data.id << ", seq:" << data.seq << std::endl;
        uint64_t seq     = (++pdata[data.id]);
        seq_vec[data.id] = seq;
        if (seq != data.seq)
        {
            std::cout << data.seq << ", expected: " << seq << std::endl;
            abort();
        }

        if (nr_pop % 50000000 == 0)
        {
            clock_gettime(CLOCK_REALTIME, &end);
            uint64_t spend = (end.tv_sec - begin.tv_sec) * 1000 * 1000 * 1000 + end.tv_nsec - begin.tv_nsec;
            std::cout << "pop speed: " << static_cast<double>(50000000) / spend * 1000 * 100 << " w/s" << std::endl;
            clock_gettime(CLOCK_REALTIME, &begin);
        }

    } while ((--total) != 0);
    clock_gettime(CLOCK_REALTIME, &end1);
    uint64_t spend = (end1.tv_sec - begin1.tv_sec) * 1000 * 1000 * 1000 + end1.tv_nsec - begin1.tv_nsec;
    std::cout << "[complete] pop speed: " << static_cast<double>(tmp_total) / spend * 1000 * 100 << " w/s" << std::endl;
}

// 模板参数N=0，测试数组中只有一个队列的情况
BOOST_AUTO_TEST_CASE(Zero)
{
    ArrayQueue<TestData, 0>* aq = ArrayQueue<TestData, 0>::GetInstance();
    // 初始化，创建普通内存队列
    int32_t ec = aq->Init([](uint8_t index) -> MPSCQueue* {
        std::string queue_name = "zero_" + std::to_string(index);
        MPSCQueue* mq          = MPSCQueue::Create(queue_name, sizeof(TestData), 8192);
        return mq;
    });
    BOOST_REQUIRE(ec == adk::ErrorCode::kSuccess);

    // 三个线程，t1/t2/t3->MPSC
    boost::thread t1 = boost::thread(Producer<0>, aq, 1000, 0);
    boost::thread t2 = boost::thread(Producer<0>, aq, 1000, 1);
    boost::thread t3 = boost::thread(Producer<0>, aq, 1000, 2);
    t1.join();
    t2.join();
    t3.join();

    // 获取数组中的第一个队列
    MPSCQueue* mq = aq->GetQueue(0);
    BOOST_REQUIRE(mq->length() == 3000);
    BOOST_REQUIRE(aq->GetNrQueueUsed() == 1);

    // 消费元素，检查元素正确性
    Consumer<0>(aq, 1000 * 3, 3);
}

BOOST_AUTO_TEST_CASE(multithread)
{
    // 1个SPSC + 1个MPSC
    ArrayQueue<TestData, 1>* aq = ArrayQueue<TestData, 1>::GetInstance();
    // 初始化，创建普通内存队列
    int32_t ec = aq->Init([](uint8_t index) -> MPSCQueue* {
        std::string queue_name = "multithread_" + std::to_string(index);
        MPSCQueue* mq          = MPSCQueue::Create(queue_name, sizeof(TestData), 8192);
        return mq;
    });
    BOOST_REQUIRE(ec == adk::ErrorCode::kSuccess);

    // 三个线程，t1->SPSC，t2/t3->MPSC
    boost::thread t1 = boost::thread(Producer<1>, aq, 1000, 0);
    boost::thread t2 = boost::thread(Producer<1>, aq, 1000, 1);
    boost::thread t3 = boost::thread(Producer<1>, aq, 1000, 2);
    t1.join();
    t2.join();
    t3.join();

    // 检查队列长度
    MPSCQueue* mq0 = aq->GetQueue(0);
    BOOST_REQUIRE(mq0->length() == 1000);
    MPSCQueue* mq1 = aq->GetQueue(1);
    BOOST_REQUIRE(mq1->length() == 2000);
    BOOST_REQUIRE(aq->GetNrQueueUsed() == 2);

    // 消费元素，检查元素正确性
    Consumer<1>(aq, 1000 * 3, 3);
}

BOOST_AUTO_TEST_CASE(ConcurrentTest)
{
    // 1个SPSC + 1个MPSC
    ArrayQueue<TestData, 1>* aq = ArrayQueue<TestData, 1>::GetInstance();
    int32_t ec                  = aq->Init([](uint8_t index) -> MPSCQueue* {
        std::string queue_name = "ConcurrentTest_" + std::to_string(index);
        MPSCQueue* mq          = MPSCQueue::Create(queue_name, sizeof(TestData), 81920);
        return mq;
    });
    BOOST_REQUIRE(ec == adk::ErrorCode::kSuccess);

    // 4个线程同时操作，t1->SPSC，t2/t3->MPSC，t4轮询取出元素并验证正确性
    boost::thread t1 = boost::thread(Producer<1>, aq, 10000, 0);
    boost::thread t2 = boost::thread(Producer<1>, aq, 10000, 1);
    boost::thread t3 = boost::thread(Producer<1>, aq, 10000, 2);
    boost::thread t4 = boost::thread(Consumer<1>, aq, 30000, 3);
    t1.join();
    t2.join();
    t3.join();
    t4.join();
}

BOOST_AUTO_TEST_CASE(PopPerfermance)
{
    // 8个SPSC + 2个MPSC
    ArrayQueue<TestData, 16>* aq = ArrayQueue<TestData, 16>::GetInstance();
    int32_t ec                   = aq->Init([](uint8_t index) -> MPSCQueue* {
        std::string queue_name = "PopPerfermance_" + std::to_string(index);
        MPSCQueue* mq          = MPSCQueue::Create(queue_name, sizeof(TestData), 1000000);
        return mq;
    });
    BOOST_REQUIRE(ec == adk::ErrorCode::kSuccess);

    std::vector<boost::thread> thr_vec;
    // 16个线程同时操作，[t1-t8]->SPSC，[t9-t10]->MPSC，t11轮询取出元素并验证正确性
    for (int i = 0; i < 16; ++i)
    {
        boost::thread tp = boost::thread(Producer<16>, aq, 100000000, i);
        thr_vec.push_back(std::move(tp));
    }
    // sleep(1);

    boost::thread tc = boost::thread(Consumer<16>, aq, 100000000 * 16, 17);
    for (int i = 0; i < 16; ++i)
    {
        thr_vec[i].join();
    }
    tc.join();
}

#define BOOST_TEST_IGNORE_NON_ZERO_CHILD_CODE
BOOST_AUTO_TEST_CASE(Fork)
{
    ArrayQueue<TestData, 4>* aq = ArrayQueue<TestData, 4>::GetInstance(true);
    // 初始化，创建共享内存队列
    int32_t ec = aq->Init([](uint8_t index) -> MPSCQueue* {
        std::string shm_name = "fork_array_queue_" + std::to_string(index);
        shm_unlink(shm_name.c_str());

        adk::MQManager* mq_manager = adk::MQManager::Create(shm_name, sizeof(TestData), 128, 0);
        MPSCQueue* mq              = mq_manager->CreateSharedMPSCQueue("array_queue", 128 * 1024);
        return mq;
    });
    BOOST_REQUIRE(ec == adk::ErrorCode::kSuccess);

    pid_t pid = fork();
    if (pid == -1)
    {
        std::cout << "fork error" << std::endl;
        exit(-1);
    }
    else if (pid == 0)
    {
        // child process
        sleep(10);

        // 获取数组中的第一个队列
        // BOOST_REQUIRE(aq->GetNrQueueUsed() == 3);
        // MPSCQueue* mq = aq->GetQueue(0);
        // BOOST_REQUIRE(mq->length() == 1000);

        // mq = aq->GetQueue(1);
        // BOOST_REQUIRE(mq->length() == 1000);
        
        // mq = aq->GetQueue(2);
        // BOOST_REQUIRE(mq->length() == 1000);

        // // 消费元素，检查元素正确性
        // Consumer<4>(aq, 1000 * 3, 3);
        // std::cout << "child process exit" << std::endl;

        BOOST_REQUIRE(aq->GetNrQueueUsed() == 1);
        Consumer<4>(aq, 1000, 1);
        std::cout << "child process exit" << std::endl;
        sleep(5);
    }

    // parent process
    // 三个线程，t1/t2/t3->SPSC
    // boost::thread t1 = boost::thread(Producer<4>, aq, 1000, 0);
    // boost::thread t2 = boost::thread(Producer<4>, aq, 1000, 1);
    // boost::thread t3 = boost::thread(Producer<4>, aq, 1000, 2);
    // t1.join();
    // t2.join();
    // t3.join();

    Producer<4>(aq, 1000, 0);
    std::cout << "father process wait to exit" << std::endl;
    sleep(30);
}

BOOST_AUTO_TEST_CASE(ShmType)
{
    ArrayQueue<TestData>* aq = ArrayQueue<TestData>::GetInstance(true);
    ArrayQueue<TestData, 4>* aq1 = ArrayQueue<TestData, 4>::GetInstance(true);
    BOOST_REQUIRE(reinterpret_cast<void*>(aq) != reinterpret_cast<void*>(aq1));

    ArrayQueue<TestData, 4, 1>* aq2 = ArrayQueue<TestData, 4, 1>::GetInstance(true);
    BOOST_REQUIRE(reinterpret_cast<void*>(aq1) != reinterpret_cast<void*>(aq2));
    BOOST_REQUIRE(reinterpret_cast<void*>(aq) != reinterpret_cast<void*>(aq2));

    ArrayQueue<uint32_t>* aq3 = ArrayQueue<uint32_t>::GetInstance(true);
    ArrayQueue<int32_t>* aq4 = ArrayQueue<int32_t>::GetInstance(true);
    BOOST_REQUIRE(reinterpret_cast<void*>(aq3) != reinterpret_cast<void*>(aq4));

    ArrayQueue<int64_t>* aq5 = ArrayQueue<int64_t>::GetInstance(true);
    BOOST_REQUIRE(reinterpret_cast<void*>(aq4) != reinterpret_cast<void*>(aq5));
    BOOST_REQUIRE(reinterpret_cast<void*>(aq3) != reinterpret_cast<void*>(aq5));

    ArrayQueue<float>* aq6 = ArrayQueue<float>::GetInstance(true);
    ArrayQueue<double>* aq7 = ArrayQueue<double>::GetInstance(true);
    BOOST_REQUIRE(reinterpret_cast<void*>(aq6) != reinterpret_cast<void*>(aq7));
}