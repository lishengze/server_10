#include <assert.h>

#include <iostream>

#include <adk/array_queue.h>
#include <adk/lock_free_msg_queue.h>
#include <adk/util.h>

// placement new 
// 使用同一块共享内存进行 placement new，验证得到的数据
class Data
{
public:
    uint32_t a_;
    char c_;
    float f_;
    uint64_t* ptr_;

public:
    Data(uint32_t a, char c, float f, uint64_t* ptr)
        : a_(a),
          c_(c),
          f_(f),
          ptr_(ptr)
    {}
        
    ~Data() = default;

    void Print()
    {
        std::cout << "a_: " << a_ << ", &a_: " << (void*)&a_ << std::endl;
        std::cout << "c_: " << c_ << ", &c_: " << (void*)&c_ << std::endl;
        std::cout << "f_: " << f_ << ", &f_: " << (void*)&f_ << std::endl;
        std::cout << "ptr_: " << (void*)ptr_ << ", *ptr_: " << *ptr_ << std::endl;
    }
};


int CreatePlacementNew()
{
    std::string shm_name("placement_new");
    shm_unlink(shm_name.c_str());
    uint32_t shm_size = sizeof(Data) + sizeof(uint64_t);
    void* pp = adk::ShmFactory::Create(shm_name, shm_size);
    assert(pp != nullptr);

    uint64_t* ptr = reinterpret_cast<uint64_t*>((char*)pp + sizeof(Data));
    Data* p_data = new (pp) Data(1, 'a', 3.14, ptr); 
    while (true)
    {
        std::cout << "================ [create] " << std::endl;
        p_data->Print();
        ++(p_data->a_);
        ++(p_data->f_);
        ++(*(p_data->ptr_));
        sleep(5);
    }

    return 0;
}

int AttachPlacementNew()
{
    std::string shm_name("placement_new");
    void* pp = adk::ShmFactory::Attach(shm_name);
    assert(pp != nullptr);

    uint64_t* ptr = reinterpret_cast<uint64_t*>((char*)pp + sizeof(Data));
    Data* c_data = new (pp) Data(100, 'z', 300.14, ptr); 
    while (true)
    {
        std::cout << "================ [attach] " << std::endl;
        c_data->Print();
        sleep(1);
    }

    return 0;
}


void Producer(adk::ArrayQueue<uint64_t>* aq, uint64_t total, uint64_t id)
{
    adk::SimpleRateController<> rate_ctrl(10000);
    uint64_t counter = 0;
    do
    {
        rate_ctrl.Wait();
        ++counter;
        while (aq->Push(counter) != adk::ErrorCode::kSuccess)
        {
            usleep(10);
        }

    } while ((--total) != 0);
    
}


void Consumer(adk::ArrayQueue<uint64_t>* aq)
{
    uint64_t nr_pop = 0;
    uint64_t data;
    while (true)
    {
        while (aq->Pop(data) != adk::ErrorCode::kSuccess);
        ++nr_pop;
        if (nr_pop % 50000 == 0)
        {
            std::cout << "pop count: " << nr_pop << std::endl;
        }
    }
}


void BasicTest()
{
    adk::ArrayQueue<uint64_t>* aq = adk::ArrayQueue<uint64_t>::GetInstance();
    int32_t ec = aq->Init([](uint8_t index) -> adk::MPSCQueue* {
        std::string queue_name = "basic_test_" + std::to_string(index);
        adk::MPSCQueue* mq     = adk::MPSCQueue::Create(queue_name, sizeof(uint64_t), 8192);
        return mq;
    });
    if (ec != adk::ErrorCode::kSuccess)
    {
        std::cout << "Init array queue failed" << std::endl;
        return;
    }

    std::vector<std::thread*> thread_vec;
    for (uint32_t index = 1; index <= 12; ++index)
    {
        std::thread* t1 = new std::thread(std::bind(Producer, aq, 1000000, index));
        thread_vec.push_back(t1);
    }

    std::thread t5(Consumer, aq);

    while (true)
    {
        // do nothing
        sleep(1);
    }
}

void AnonShmTest()
{
    // 匿名共享内存
    adk::ArrayQueue<uint64_t>* aq = adk::ArrayQueue<uint64_t>::GetInstance(true);

    std::string shm_name("mq_manager");
    shm_unlink(shm_name.c_str());
    adk::MQManager* mq_manager = adk::MQManager::Create(shm_name, sizeof(uint64_t), 64, 0);
    int32_t ec = aq->Init([mq_manager](uint32_t index) -> adk::MPSCQueue* {
        adk::MPSCQueue* mq = mq_manager->CreateSharedMPSCQueue("queue_" + std::to_string(index), 8192);
        return mq;
    });
    if (ec != adk::ErrorCode::kSuccess)
    {
        std::cout << "Init array queue failed" << std::endl;
        return;
    }

    pid_t pid = fork();
    if (pid == -1)
    {
        std::cout << "fork error " << std::strerror(errno) << std::endl;
    }
    else if (pid == 0)
    {
        // child process
        Consumer(aq);
    }

    // parent process
    std::vector<std::thread*> thread_vec;
    for (uint32_t index = 1; index <= 12; ++index)
    {
        std::thread* t1 = new std::thread(std::bind(Producer, aq, 1000000, index));
        thread_vec.push_back(t1);
    }

    while (true)
    {
        // do nothing
        sleep(1);
    }
}

void NamedShmTest()
{
    adk::ArrayQueue<uint64_t>* aq = adk::ArrayQueue<uint64_t>::GetInstance(true, "NamedShm");
    std::string shm_name("mq_manager");
    shm_unlink(shm_name.c_str());
    adk::MQManager* mq_manager = adk::MQManager::Create(shm_name, sizeof(uint64_t), 64, 0);
    int32_t ec = aq->Init([mq_manager](uint32_t index) -> adk::MPSCQueue* {
        adk::MPSCQueue* mq = mq_manager->CreateSharedMPSCQueue("queue_" + std::to_string(index), 8192);
        return mq;
    });
    if (ec != adk::ErrorCode::kSuccess)
    {
        std::cout << "Init array queue failed" << std::endl;
        return;
    }

    std::vector<std::thread*> thread_vec;
    for (uint32_t index = 1; index <= 12; ++index)
    {
        std::thread* t1 = new std::thread(std::bind(Producer, aq, 1000000, index));
        thread_vec.push_back(t1);
    }

    while (true)
    {
        // do nothing
        sleep(1);
    }
}

void AttachShmTest()
{
    adk::MQManager* mq_manager = adk::MQManager::Attach("mq_manager");
    if (mq_manager == nullptr)
    {
        std::cout << "attach mq manager failed" << std::endl;
        return;
    }

    adk::ArrayQueue<uint64_t>* aq = adk::ArrayQueue<uint64_t>::Attach("NamedShm", [mq_manager](uint32_t index) -> adk::MPSCQueue* {
        adk::MPSCQueue* mq = mq_manager->AttachSharedMPSCQueue("queue_" + std::to_string(index));
        return mq;
    });
    if (aq == nullptr)
    {
        std::cout << "attach array queue failed "  << std::endl;
        return;
    }

    Consumer(aq);
}

void AttachShmTest2()
{
    adk::MQManager* mq_manager = adk::MQManager::Attach("mq_manager");
    adk::ArrayQueue<uint64_t>* aq = adk::ArrayQueue<uint64_t>::Attach("NamedShm");
    if (aq == nullptr)
    {
        std::cout << "attach array queue failed "  << std::endl;
        return;
    }

    // Attach 和 Init 分开执行
    aq->Init([mq_manager](uint32_t index) -> adk::MPSCQueue* {
        adk::MPSCQueue* mq = mq_manager->AttachSharedMPSCQueue("queue_" + std::to_string(index));
        return mq;
    });

    Consumer(aq);
}

void AttachShmProducer1()
{
    adk::MQManager* mq_manager = adk::MQManager::Attach("mq_manager");
    if (mq_manager == nullptr)
    {
        std::cout << "attach mq manager failed" << std::endl;
        return;
    }

    adk::ArrayQueue<uint64_t>* aq = adk::ArrayQueue<uint64_t>::Attach("NamedShm", [mq_manager](uint32_t index) -> adk::MPSCQueue* {
        adk::MPSCQueue* mq = mq_manager->AttachSharedMPSCQueue("queue_" + std::to_string(index));
        return mq;
    });
    if (aq == nullptr)
    {
        std::cout << "attach array queue failed "  << std::endl;
        return;
    }

    std::thread t1(std::bind(Producer, aq, 1000000, 1));
    while (true)
    {
        // do nothing
        sleep(1);
    }
}

int main(int argc, char const *argv[])
{
    if (argc != 2)
    {
        std::cout << "Usage: test_array_queue [mode]" << std::endl;
        return -1;
    }

    int mode = atoi(argv[1]);
    std::cout << "mode: " << mode << std::endl;

    switch (mode)
    {
    case 0:
        BasicTest();
        break;
    case 1:
        AnonShmTest();
        break;
    case 2:
        NamedShmTest();
        break;
    case 3:
        AttachShmTest();
        break;
    case 4:
        AttachShmTest2();
        break;
    case 5:
        AttachShmProducer1();
        break;

    default:
        break;
    }

    return 0;
}