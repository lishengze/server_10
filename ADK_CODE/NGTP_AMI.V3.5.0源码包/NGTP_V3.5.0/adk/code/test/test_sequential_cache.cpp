/**
 * @breif      test sequential cache
 * @author     zhaonan@archforce.com.cn
 * @date       2017/09/28
 */
#include <adk/sequential_cache.h>
#include <boost/thread/thread.hpp>

#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <iostream>

struct AppData
{
    uint64_t cache_seq;
    uint64_t counter;
};

boost::mutex g_data_lock;
bool         is_served_by_master = true;

// configure test case
const uint64_t simulate_conn_error_at = 100000 * 5;
const uint32_t simulate_delay_us = 2000000; // 2 second, roughly 200000 messages;
#define ASSERT_BUG(condition) if (!(condition)) {   \
    std::cout << "Bug On, pid = " << getpid() << std::endl;    \
    sleep(1000);    \
}

const uint64_t simulate_eos_flag = -1ul;

// simulate tcp write
static void Write(adk::SPSCQueue<AppData*>* channel, AppData* app_data)
{
    while (channel->Push(app_data) != adk::ErrorCode::kSuccess) ADK_PAUSE();
}

void MasterThreadMain(adk::SequentialCache<AppData>* cache, adk::SPSCQueue<AppData*>* channel)
{
    // to save the breakpoint, using a vector if there are more than one connections.
    uint64_t app_data_seq = 0; 

    while (true)
    {
        AppData* app_data = cache->MasterNext(app_data_seq, [cache, channel](adk::SlaveJoinEvent& join_event){
            // print the join index, the master finish the late join from here, see comment (1) bellow
            std::cout << "join_event.join_index = " << join_event.join_index << std::endl;
            {
                // here is a demo how to pretect shared connection status!
                boost::mutex::scoped_lock lock_guard(g_data_lock);
                is_served_by_master = true;    
            }

            // let's transfer the last amount of messages.
            // note, txw_tail is the transmit windows tail
            uint64_t txw_tail = cache->MasterLastIndex();
            uint64_t merge_break_point = join_event.join_index - 1;
            while ((++merge_break_point) <= txw_tail)
            {
                AppData* data = cache->MasterAt(merge_break_point);
                ASSERT_BUG(data != NULL);

                data->cache_seq = merge_break_point;    // not necessary
                Write(channel, data);                   // simulate tcp write
            }
        });

        if (!app_data)
        {
            // the master was notified to exit!
            break;
        }

        app_data->cache_seq = app_data_seq;
        if (app_data->counter == simulate_conn_error_at)
        {
            // send a special message to simulate tcp EOS, 
            AppData* eos = new AppData;
            eos->cache_seq = simulate_eos_flag;
            Write(channel, eos);

            // here is a demo how to pretect shared connection status!
            boost::mutex::scoped_lock lock_guard(g_data_lock);
            is_served_by_master = false;
        }

        if (!is_served_by_master)
        {
            // the connection was not available, master has nothing to do.
            continue;
        }

        Write(channel, app_data); // simulate tcp connection write
    }
}

void SlaveThreadMain(uint64_t break_point, adk::SequentialCache<AppData>* cache, adk::SPSCQueue<AppData*>* channel)
{
    // delay to longer the distance between master and slave 
    usleep(simulate_delay_us);  

    // setup the breakpoint
    auto slave_hdl = cache->NewSlave(break_point, NULL);
    uint64_t counter = 0;
    while (true)
    {
        AppData* app_data = cache->SlaveNext(slave_hdl, counter);
        if (app_data == NULL)
        {
            // the slave is close enough to the master
            // it turn to master to finished the last job, see comment (1) above
            break;
        }

        app_data->cache_seq = counter;
        Write(channel, app_data);
    }
    cache->DeleteSlave(slave_hdl);
}

// simulate tcp read
static AppData* Read(adk::SPSCQueue<AppData*>* channel)
{
    AppData* ret;
    while (channel->Pop(ret) != adk::ErrorCode::kSuccess)
    {
        usleep(0);
    }
    return ret;
}

void ClientThreadMain(adk::SequentialCache<AppData>* cache, adk::SPSCQueue<AppData*>* channel)
{
    uint64_t counter = 0;
    while (true)
    {
        ++counter;

        auto* app_data = Read(channel); // simulate read tcp connection
        if (app_data == NULL)
        {
            return;
        }

        if (app_data->cache_seq == simulate_eos_flag)   // simulate tcp EOS
        {
            // simulate reconnect, when reconnected, a slave shall be launched to serve
            // here counter is the break point, aka the expect message to first receive
            boost::thread(boost::bind(SlaveThreadMain, counter, cache, channel));
            delete app_data;
            --counter;
            continue;
        }

        ASSERT_BUG(app_data->counter == app_data->cache_seq);
        ASSERT_BUG(app_data->counter == counter);
        delete app_data;
    }
}


int main(int argc, char const *argv[])
{
    adk::SequentialCache<AppData> cache;
    int32_t ec = cache.Init();
    ASSERT_BUG(ec == adk::ErrorCode::kSuccess);

    adk::SPSCQueue<AppData*>* channel = adk::SPSCQueue<AppData*>::Create("tcp_connection", 8192);

    boost::thread master = boost::thread(boost::bind(MasterThreadMain, &cache, channel));   // lanch server side
    boost::thread client = boost::thread(boost::bind(ClientThreadMain, &cache, channel));   // lanch client side

    adk::SimpleRateController<> rate_ctrl(300000);  // run for 160 seconds, with speed 100000
    uint64_t counter = 0;
    while (true)
    {
        AppData* data = new AppData;
        data->counter = ++counter;                  // gen app data, with continuous seq which saved in $counter
        data->cache_seq = 0;
        uint32_t ec = cache.Insert(data);           // insert app data into sequential cache
        if (ec != adk::ErrorCode::kSuccess)
        {
            // break when there are no spaces left
            break;
        }
        rate_ctrl.Wait();
    }

    adk::SCacheStats stats;
    cache.GetStats(stats);
    std::cout << "stats.cache_usage = " << stats.cache_usage << ", "
              << "stats.nr_slave_joins = " << stats.nr_slave_joins << ", "
              << "stats.nr_slaves = " << stats.nr_slaves << std::endl;

    // simulate cleanup procedure, notify master to exit!
    cache.ReleaseRxThread();
    return 0;
}

