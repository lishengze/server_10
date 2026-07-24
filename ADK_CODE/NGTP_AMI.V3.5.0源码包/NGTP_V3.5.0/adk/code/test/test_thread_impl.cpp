#include "test_thread_define.h"

boost::mutex g_log_mutex;

void MyThread::OnMessage(Cookie* cookie)
{
    LOG_MSG(cookie->value());
};

int32_t TestTT()
{
    return 1;
}
