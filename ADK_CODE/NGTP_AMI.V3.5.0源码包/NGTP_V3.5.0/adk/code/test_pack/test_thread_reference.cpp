#include "test_thread_define.h"

using adk::ThreadManager;

int main(int argc, char const *argv[])
{   
    auto& thr_mana = *ThreadManager::Instance();

    thr_mana.ChangeParams<MyThread>(adk::thread::ParallelInit = true);
    thr_mana.ChangeParams<MyThread>(adk::thread::InstanceNumber = 3);
    thr_mana.ChangeParams<MyThread>(adk::thread::ThreadAffinity = std::string("1-3"));

    thr_mana.Start();

    auto& my_thread = *thr_mana.ThreadInstance<MyThread>();

    for (int32_t i = 0; i < 3; ++i)
    {
        auto tmsg1 = Task::New();
        tmsg1->set_value(10);
        thr_mana.SendMsg<MyThread>(tmsg1, (i % 2));
        sleep(1);

        auto tmsg2 = Cookie::New();
        tmsg2->set_value(20);
        adk::SendMsg<MyThread>(tmsg2);
        sleep(1);


        if ((i % 2) == 0)
        {
            auto event = ConnectReady::NewUnsafe();
            event->set_value(30);
            // adk::SendMsgUnsafe<MyThread>(event);
            my_thread.SendMsg(event);
        }
    }

    std::cout << thr_mana.Dump(true) << std::endl;
    std::cout << thr_mana.Dump() << std::endl;

    thr_mana.Finish();
    std::cout << thr_mana.GetParms<MyThread>() << std::endl;
    thr_mana.Finish();
    return 0;
}

