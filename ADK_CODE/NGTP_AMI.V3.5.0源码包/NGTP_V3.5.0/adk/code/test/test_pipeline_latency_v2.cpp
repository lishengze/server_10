#include "../include/adk/pipeline.h"

#include <time.h>
#include <stdio.h>

#include <iostream>

#include <boost/program_options.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/thread/thread.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace po = boost::program_options;

using namespace adk;

int32_t g_msg_burst;

int64_t time_now()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

int CompareLatency(const void *a, const void *b)
{
    const int64_t a_v = *reinterpret_cast<const int64_t*>(a);
    const int64_t b_v = *reinterpret_cast<const int64_t*>(b);
    return a_v - b_v;
}

class IOSW : public StageWorker<ADK_IO(int64_t, int64_t)>
{
public:
    IOSW(const std::string& name)
        :   StageWorker<ADK_IO(int64_t, int64_t)>(name)
    {}

    ~IOSW()
    {}  

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {
        Forward(message);
    }
};

class ISW : public StageWorker<ADK_INPUT(int64_t)>
{
public:
    ISW(const std::string& name)
        :   StageWorker<ADK_INPUT(int64_t)>(name)
    {
        counter_ = 0;
        stat_buf_ = new int64_t [g_msg_burst + 128];
        burst_ = g_msg_burst;
    }

    ~ISW()
    {}  

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {
        stat_buf_[counter_] = time_now() - message;
        ++counter_;

        if (counter_ == burst_)
        {
            qsort(stat_buf_, counter_, sizeof(int64_t), CompareLatency);
            int64_t sum = 0;
            while (--counter_ >= 0)
            {
                sum += stat_buf_[counter_];
            }

            std::cout << "min = " << stat_buf_[0] << ", max = " << stat_buf_[burst_ - 1]
                      << ", avg = " << sum / burst_ 
                      << ", 50 = " << stat_buf_ [burst_ /2] 
                      << ", 90 = " << stat_buf_ [burst_ * 90 / 100]
                      << ", 95 = " << stat_buf_ [burst_ * 95 / 100]
                      << std::endl;

            counter_ = 0;
        }
    }

private:
    int32_t  counter_;
    int32_t  burst_;
    int64_t* stat_buf_;
};


int main(int argc, char const *argv[])
{
    /*
    struct sched_param param;
    param.sched_priority = 90;
    (::sched_setscheduler(0, SCHED_FIFO, &param));*/

    po::options_description desc("Allowed options", 120);   // parse command line
    desc.add_options()
    ("help,h", "show this information")
    ("test-one-stage",  "")
    ("test-two-stage",  "")
    ("message-burst", po::value<uint32_t>()->default_value(7000), "")
    ("usleep-interval", po::value<uint32_t>()->default_value(1000000), "")
    ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm); 

    if (vm.count("help"))
    {
        std::cout << desc << std::endl;
        return 0;
    }

    g_msg_burst = vm["message-burst"].as<uint32_t>();


    ISW one_stage("one_stage");
    one_stage.SetBackoffPolicy<policy::Pause>();
    one_stage.SetRealtime(90);
    PipelineEntrance<int64_t, 1>* entrance;
    Pipeline pipeline;

    entrance = pipeline.CreateEntrance<Pipeline::kMessaging>(one_stage.Prev(), 0, 8192);

    /*
    struct sched_param param;
        param.sched_priority = 90;
            (::sched_setscheduler(0, SCHED_FIFO, &param));*/

    pipeline.Start();

    sleep(3);

    int64_t begin;

/*    param.sched_priority = 0;
    (::sched_setscheduler(0, SCHED_OTHER, &param));*/
    entrance->ChangeThisThreadToRealtime(90);
    while (true)
    {
        int32_t counter = 0;
        while (++counter <= g_msg_burst)
        {
            begin = time_now();
            entrance->Forward(begin);
            if ((counter % 2) == 0)
                usleep(0);
        }

        usleep(vm["usleep-interval"].as<uint32_t>());
    }

    return 0;
}
