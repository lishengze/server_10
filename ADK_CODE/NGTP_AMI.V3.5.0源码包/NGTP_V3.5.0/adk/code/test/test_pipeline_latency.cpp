#include <adk/pipeline.h>
#include <adk/arch/generic.h>

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
        ADK_MB();
        stat_buf_[counter_] = adk::GetTSC() - message;
        ++counter_;

        if (counter_ == burst_)
        {
            qsort(stat_buf_, counter_, sizeof(int64_t), CompareLatency);
            int64_t sum = 0;
            while (--counter_ >= 0)
            {
                sum += stat_buf_[counter_];
            }

            std::cout << "min = " << stat_buf_[0] << ", max = " << stat_buf_[6999]
                      << ", avg = " << sum / 7000 
                      << ", 50 = " << stat_buf_ [7000 /2] 
                      << ", 90 = " << stat_buf_ [7000 * 90 / 100]
                      << ", 95 = " << stat_buf_ [7000 * 95 / 100]
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
    one_stage.SetBackoffPolicy<adk::policy::Pause>();
    PipelineEntrance<int64_t, 1>* entrance;
    Pipeline pipeline;

    entrance = pipeline.CreateEntrance<Pipeline::kMessaging>(one_stage.Prev(), 0, 8192);

    pipeline.Start();

    int64_t begin;
    while (true)
    {
        int32_t counter = 0;
        while (++counter <= g_msg_burst)
        {
            begin = adk::GetTSC();
            ADK_MB();
            entrance->Forward(begin);
        }

        usleep(vm["usleep-interval"].as<uint32_t>());
    }

    return 0;
}