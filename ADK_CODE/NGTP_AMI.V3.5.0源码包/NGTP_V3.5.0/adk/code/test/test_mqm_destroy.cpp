#include <assert.h>
#include <iostream>

#include <boost/function.hpp>
#include <boost/thread/thread.hpp>

#include <adk/mem_pool.h>
#include <adk/lock_free_msg_queue.h>
#include <adk/util.h>

int main(int argc, char const *argv[])
{
    for (int32_t i = 0; i < 10; ++i)
    {
        adk::MQManager* mqm = adk::MQManager::Create("test_destroy", 10, 1024);
        adk::MPSCQueue* mq = mqm->CreateSharedMPSCQueue("test_shm_queue", 1024);
        ADK_NOTUSE(mq);

        assert(adk::MQManager::Destroy("test_destroy") == adk::ErrorCode::kSuccess);

        if (i == 0)
        {
            std::cout << "please check the process memory maps" << std::endl;
            sleep(20);
        }
    }
    return 0;
}