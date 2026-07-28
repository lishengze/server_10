#define BOOST_TEST_MODULE mpsc_queue_stats
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk/lock_free_msg_queue.h>

#include <set>
#include <string>
#include <vector>
#include <map>

using namespace adk;

BOOST_AUTO_TEST_CASE(InitAndBasic)
{
	BOOST_REQUIRE(sizeof(MPSCQueue) == sizeof(SPSCQueue<uint32_t>));

	MPSCQueue* mq = MPSCQueue::Create<uint32_t>("ABC", 8192);
	BOOST_REQUIRE(mq->avg_lat_buf_ == nullptr);
	BOOST_REQUIRE(mq->avg_lat_buf_index_ == 0);
	BOOST_REQUIRE(mq->avg_lat_buf_index_save_ == 0);

	// 没有allocate内存也可以使用
	adk::QueueLatStats stats;
	mq->CalcLatency(stats);


	// 校验有申请对应的指标统计内存
	MPSCQueue* mq2 = MPSCQueue::Create<uint32_t>("ABC", 8192, ADK_LFMQ_FLAG_CREATE_AVG_LAT_BUF);
	BOOST_REQUIRE(mq2->avg_lat_buf_ != nullptr);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_ == 0);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_save_ == 0);

	// 第一个消息
	BOOST_REQUIRE(mq2->PushTsc(1) == adk::ErrorCode::kSuccess);

	adk::Entry* entry;
	BOOST_REQUIRE(mq2->WaitEntryTsc<true>(&entry) == adk::ErrorCode::kSuccess);

	// 维护索引
	mq2->SaveLatency(entry);
	BOOST_REQUIRE(mq2->FreeEntryTsc(entry) == adk::ErrorCode::kSuccess);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_ == 1);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_save_ == 0);

	// 只有一个消息，校验指标正确性
	mq2->CalcLatency(stats);
	BOOST_REQUIRE(stats.avg == stats.max);
	BOOST_REQUIRE(stats.avg == stats.min);
	BOOST_REQUIRE(stats.errors == 0);

	BOOST_REQUIRE(mq2->avg_lat_buf_index_ == 1);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_save_ == 1);

	// == 第2/3/4个消息
	BOOST_REQUIRE(mq2->PushTsc(2) == adk::ErrorCode::kSuccess);
	BOOST_REQUIRE(mq2->PushTsc(3) == adk::ErrorCode::kSuccess);
	BOOST_REQUIRE(mq2->PushTsc(4) == adk::ErrorCode::kSuccess);

	// 人为制造延迟
	usleep(50);
	BOOST_REQUIRE(mq2->WaitEntryTsc<true>(&entry) == adk::ErrorCode::kSuccess);
	mq2->SaveLatency(entry);
	BOOST_REQUIRE(mq2->FreeEntryTsc(entry) == adk::ErrorCode::kSuccess);

	usleep(100);
	BOOST_REQUIRE(mq2->WaitEntryTsc<true>(&entry) == adk::ErrorCode::kSuccess);
	mq2->SaveLatency(entry);
	BOOST_REQUIRE(mq2->FreeEntryTsc(entry) == adk::ErrorCode::kSuccess);

	usleep(150);
	BOOST_REQUIRE(mq2->WaitEntryTsc<true>(&entry) == adk::ErrorCode::kSuccess);
	mq2->SaveLatency(entry);
	BOOST_REQUIRE(mq2->FreeEntryTsc(entry) == adk::ErrorCode::kSuccess);

	BOOST_REQUIRE(mq2->avg_lat_buf_index_ == 4);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_save_ == 1);

	// 只有3个消息，校验指标正确性
	mq2->CalcLatency(stats);
	BOOST_REQUIRE(stats.avg < stats.max);
	BOOST_REQUIRE(stats.avg > stats.min);
	BOOST_REQUIRE(stats.errors == 0);

	BOOST_REQUIRE(mq2->avg_lat_buf_index_ == 4);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_save_ == 4);

	// 让队列和指标缓存都roll over，然后测试
	uint32_t end = ADK_LFMQ_AVG_LAT_BUF_SIZE / 8192;
	for (uint32_t j = 0; j != end; ++j)
	{
		uint32_t i = 0;
		do
		{
			++i;
		} while (mq2->PushTsc(i) == adk::ErrorCode::kSuccess);

		BOOST_REQUIRE(i == 8193);

		i = 0;
		do {
			++i;
			if (mq2->WaitEntryTsc<true>(&entry) == adk::ErrorCode::kSuccess)
			{
				char* buf = entry->buffer;
				uint32_t value = *(uint32_t*)buf;
				BOOST_REQUIRE(value == i);
				mq2->SaveLatency(entry);
				BOOST_REQUIRE(mq2->FreeEntryTsc(entry) == adk::ErrorCode::kSuccess);
				continue;
			}
			break;
		} while (1);

		BOOST_REQUIRE(i == 8193);
	}

	BOOST_REQUIRE(mq2->avg_lat_buf_index_ == 4 + ADK_LFMQ_AVG_LAT_BUF_SIZE);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_save_ == 4);

	mq2->CalcLatency(stats);
	BOOST_REQUIRE(stats.avg < stats.max);
	BOOST_REQUIRE(stats.avg > stats.min);
	BOOST_REQUIRE_EQUAL(stats.errors, 0);	

	BOOST_REQUIRE(mq2->avg_lat_buf_index_ == 4 + ADK_LFMQ_AVG_LAT_BUF_SIZE);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_save_ == 4 + ADK_LFMQ_AVG_LAT_BUF_SIZE);

	// 让指标缓存overflow，然后测试
	for (uint32_t j = 0; j != end + 1; ++j)
	{
		uint32_t i = 0;
		do
		{
			++i;
		} while (mq2->PushTsc(i) == adk::ErrorCode::kSuccess);

		BOOST_REQUIRE(i == 8193);

		usleep(0);
		i = 0;
		do {
			++i;
			if (mq2->WaitEntryTsc<true>(&entry) == adk::ErrorCode::kSuccess)
			{
				mq2->SaveLatency(entry);
				BOOST_REQUIRE(mq2->FreeEntryTsc(entry) == adk::ErrorCode::kSuccess);
				continue;
			}
			break;
		} while (1);

		BOOST_REQUIRE(i == 8193);
	}

	BOOST_REQUIRE(mq2->avg_lat_buf_index_ == 4 + ADK_LFMQ_AVG_LAT_BUF_SIZE * 2 + 8192);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_save_ == 4 + ADK_LFMQ_AVG_LAT_BUF_SIZE);

	mq2->CalcLatency(stats);
	BOOST_REQUIRE(stats.avg < stats.max);
	BOOST_REQUIRE(stats.avg > stats.min);
	BOOST_REQUIRE(stats.errors == 0);

	BOOST_REQUIRE(mq2->avg_lat_buf_index_ == 4 + ADK_LFMQ_AVG_LAT_BUF_SIZE * 2 + 8192);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_save_ == 4 + ADK_LFMQ_AVG_LAT_BUF_SIZE * 2 + 8192);
}

void Producer(MPSCQueue* mq, uint64_t total)
{
	do
	{
		while (mq->PushTsc(total) != adk::ErrorCode::kSuccess);
	} while ((--total) != 0);
}

uint64_t g_counter = 0;
void Consumer(MPSCQueue* mq, uint64_t total)
{
	do
	{
		adk::Entry* entry;
		while (mq->WaitEntryTsc<true>(&entry) != adk::ErrorCode::kSuccess) 
		{
			for (uint32_t j = 0; j != 32; ++j)
				ADK_PAUSE();
		}

		++g_counter;

		char* buf = entry->buffer;
		if (total != *(uint64_t*)buf)
			abort();
		mq->SaveLatency(entry);

		mq->FreeEntryTsc(entry);
	} while ((--total) != 0);
}

BOOST_AUTO_TEST_CASE(QueueIndexOverflow)
{
	MPSCQueue* mq2 = MPSCQueue::Create<uint64_t>("ABC", 8192*8, ADK_LFMQ_FLAG_CREATE_AVG_LAT_BUF);
	BOOST_REQUIRE(mq2->avg_lat_buf_ != nullptr);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_ == 0);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_save_ == 0);	

	mq2->ReinitQueueStatus((1ul << 32) - 100000000ul);

	// 测试pos超过32位的情况
	boost::thread t1 = boost::thread(Producer, mq2, (100000000ul + 20000000));
	boost::thread t2 = boost::thread(Consumer, mq2, (100000000ul + 20000000));

	// uint64_t counter = 0;
	// uint64_t g_counter_saved = 0;
	// while (1)
	// {
	// 	g_counter_saved = g_counter;
	// 	std::cout << "g_counter = "  << (g_counter_saved - counter) << std::endl;
	// 	counter = g_counter_saved;
	// 	sleep(1);
	// }

	t1.join();
	t2.join();
}

BOOST_AUTO_TEST_CASE(InvalidData)
{
	MPSCQueue* mq2 = MPSCQueue::Create<uint64_t>("ABC", 8192*8, ADK_LFMQ_FLAG_CREATE_AVG_LAT_BUF);
	BOOST_REQUIRE(mq2->avg_lat_buf_ != nullptr);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_ == 0);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_save_ == 0);

	// 测试stat buffer中有错误数据
	mq2->avg_lat_buf_[0] = 0;
	mq2->avg_lat_buf_[1] = (1u<<31) >> ADK_MQ_TSC_PRECISION;
	mq2->avg_lat_buf_[2] = ((1u<<31) - 1) >> ADK_MQ_TSC_PRECISION;
	mq2->avg_lat_buf_index_ = 3;
	QueueLatStats stats;
	mq2->CalcLatency(stats);
	BOOST_REQUIRE_EQUAL(stats.errors, 2);
	BOOST_REQUIRE_EQUAL(stats.avg, (((1ul << 31) - 1) >> ADK_MQ_TSC_PRECISION) << ADK_MQ_TSC_PRECISION);
	BOOST_REQUIRE_EQUAL(stats.min, (((1ul << 31) - 1) >> ADK_MQ_TSC_PRECISION) << ADK_MQ_TSC_PRECISION);
	BOOST_REQUIRE_EQUAL(stats.max, (((1ul << 31) - 1) >> ADK_MQ_TSC_PRECISION) << ADK_MQ_TSC_PRECISION);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_ == 3);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_save_ == 3);

	// 测试Push和WaitEntryTsc混用
	BOOST_REQUIRE(mq2->Push(10) == adk::ErrorCode::kSuccess);
		
	adk::Entry* entry;
	BOOST_REQUIRE(mq2->WaitEntryTsc<true>(&entry) == adk::ErrorCode::kSuccess);
	
	BOOST_REQUIRE((entry->pos & ADK_MQ_TSC_MASK) == 0);
	mq2->SaveLatency(entry);

	char* buf;
	buf = entry->buffer;
	BOOST_REQUIRE(*(uint64_t*)buf == 10);

	BOOST_REQUIRE(mq2->FreeEntryTsc(entry) == adk::ErrorCode::kSuccess);

	mq2->CalcLatency(stats);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_ == 4);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_save_ == 4);

	mq2->ReinitQueueStatus(1ul<<33);
	BOOST_REQUIRE(mq2->Push(20) == adk::ErrorCode::kSuccess);
	BOOST_REQUIRE(mq2->WaitEntryTsc<true>(&entry) == adk::ErrorCode::kSuccess);
	BOOST_REQUIRE(mq2->FreeEntryTsc(entry) == adk::ErrorCode::kSuccess);
	buf = entry->buffer;
	BOOST_REQUIRE(*(uint64_t*)buf == 20);

	// 测试PushTsc和Push混用
	BOOST_REQUIRE(mq2->PushTsc(30) == adk::ErrorCode::kSuccess);
	BOOST_REQUIRE(mq2->WaitEntryTsc<true>(&entry) == adk::ErrorCode::kSuccess);
	BOOST_REQUIRE(mq2->FreeEntryTsc(entry) == adk::ErrorCode::kSuccess);
	buf = entry->buffer;
	BOOST_REQUIRE(*(uint64_t*)buf == 30);

	BOOST_REQUIRE(mq2->mem_header_->head == mq2->mem_header_->tail);
	BOOST_REQUIRE(mq2->mem_header_->reserve == mq2->mem_header_->release);
	BOOST_REQUIRE((mq2->mem_header_->reserve & ADK_MQ_TSC_MASK) == (1ul << 33));
}

struct TestData
{
	uint64_t id;
	uint64_t seq;
};

volatile bool g_begin_concurrent_test = false;

template<int select>
void Producer1(MPSCQueue* mq, uint64_t total, uint64_t id)
{
	while (!g_begin_concurrent_test);

	TestData data;
	data.id = id;
	uint64_t counter = 0;
	do
	{
		++counter;
		data.seq = counter;
		if (select == 0)
		{
			while (mq->PushTsc(data) != adk::ErrorCode::kSuccess);	
		}
		else
		{
			while (mq->Push(data) != adk::ErrorCode::kSuccess);	
		}
		
	} while ((--total) != 0);
}

uint64_t g_pdata[2];

template<int select>
void Consumer1(MPSCQueue* mq, uint64_t total)
{
	while (!g_begin_concurrent_test);

	TestData data;
	do
	{
		// 以下都是AMI中可能使用的情况组合
		if (select == 1)
		{
			while (mq->PopTsc<false>(data) != adk::ErrorCode::kSuccess);
			
			uint64_t seq = (++g_pdata[data.id]);
			if (seq != data.seq)
				abort();
			continue;
		}
		else if (select == 2)
		{
			while (mq->Pop(data) != adk::ErrorCode::kSuccess);
			
			uint64_t seq = (++g_pdata[data.id]);
			if (seq != data.seq)
				abort();
			continue;	
		}

		adk::Entry* entry;
		while (mq->WaitEntryTsc<true>(&entry) != adk::ErrorCode::kSuccess) 
		{
			for (uint32_t j = 0; j != 32; ++j)
				ADK_PAUSE();
		}
		char* buf = entry->buffer;
		data = *(TestData*)buf;

		uint64_t seq = (++g_pdata[data.id]);
		if (seq != data.seq)
			abort();

		mq->FreeEntryTsc(entry);
	} while ((--total) != 0);
}

BOOST_AUTO_TEST_CASE(ConcurrentTest)
{
	// 测试PushTsc并发
	MPSCQueue* mq2 = MPSCQueue::Create<TestData>("ABC", 8192*8);
	BOOST_REQUIRE(mq2->avg_lat_buf_ == nullptr);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_ == 0);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_save_ == 0);	

	boost::thread t1 = boost::thread(Producer1<0>, mq2, 200000000, 0);
	boost::thread t2 = boost::thread(Producer1<0>, mq2, 200000000, 1);
	boost::thread t3 = boost::thread(Consumer1<0>, mq2, 400000000);

	sleep(1);
	g_begin_concurrent_test = true;

	while (1)
	{
		sleep(1);
		std::cout << "g_pdata[0] == " << g_pdata[0] << " g_pdata[1]" << g_pdata[1] << std::endl;
		if (t3.try_join_for(boost::chrono::nanoseconds(1000000)))
		{
			std::cout << "g_pdata[0] == " << g_pdata[0] << " g_pdata[1]" << g_pdata[1] << std::endl;
			break;
		}
	}
	t1.join();
	t2.join();


	// 测试Push和PushTsc并发
	memset(g_pdata, 0, sizeof(g_pdata));
	g_begin_concurrent_test = false;
	boost::thread t4 = boost::thread(Producer1<1>, mq2, 200000000, 0);
	boost::thread t5 = boost::thread(Producer1<0>, mq2, 200000000, 1);
	boost::thread t6 = boost::thread(Consumer1<0>, mq2, 400000000);

	sleep(1);
	g_begin_concurrent_test = true;
	while (1)
	{
		sleep(1);
		std::cout << "g_pdata[0] == " << g_pdata[0] << " g_pdata[1]" << g_pdata[1] << std::endl;
		if (t6.try_join_for(boost::chrono::nanoseconds(1000000)))
		{
			std::cout << "g_pdata[0] == " << g_pdata[0] << " g_pdata[1]" << g_pdata[1] << std::endl;
			break;
		}
	}
	t4.join();
	t5.join();

	// 测试Push和PopTsc并发
	memset(g_pdata, 0, sizeof(g_pdata));
	g_begin_concurrent_test = false;
	boost::thread t7 = boost::thread(Producer1<1>, mq2, 200000000, 0);
	boost::thread t8 = boost::thread(Producer1<0>, mq2, 200000000, 1);
	boost::thread t9 = boost::thread(Consumer1<1>, mq2, 400000000);

	sleep(1);
	g_begin_concurrent_test = true;
	while (1)
	{
		sleep(1);
		std::cout << "g_pdata[0] == " << g_pdata[0] << " g_pdata[1]" << g_pdata[1] << std::endl;
		if (t7.try_join_for(boost::chrono::nanoseconds(1000000)))
		{
			std::cout << "g_pdata[0] == " << g_pdata[0] << " g_pdata[1]" << g_pdata[1] << std::endl;
			break;
		}
	}
	t8.join();
	t9.join();
}

BOOST_AUTO_TEST_CASE(ConcurrentTestOldMethod)
{
	memset(g_pdata, 0, sizeof(g_pdata));
	// 测试Push/Pop并发
	MPSCQueue* mq2 = MPSCQueue::Create<TestData>("ABC", 8192*8);
	BOOST_REQUIRE(mq2->avg_lat_buf_ == nullptr);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_ == 0);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_save_ == 0);	

	boost::thread t1 = boost::thread(Producer1<1>, mq2, 200000000, 0);
	boost::thread t2 = boost::thread(Producer1<1>, mq2, 200000000, 1);
	boost::thread t3 = boost::thread(Consumer1<2>, mq2, 400000000);

	sleep(1);
	g_begin_concurrent_test = true;

	while (1)
	{
		sleep(1);
		std::cout << "g_pdata[0] == " << g_pdata[0] << " g_pdata[1]" << g_pdata[1] << std::endl;
		if (t3.try_join_for(boost::chrono::nanoseconds(1000000)))
		{
			std::cout << "g_pdata[0] == " << g_pdata[0] << " g_pdata[1]" << g_pdata[1] << std::endl;
			break;
		}
	}
	t1.join();
	t2.join();
}

BOOST_AUTO_TEST_CASE(ConcurrentTestOldMethodOverflow)
{
	memset(g_pdata, 0, sizeof(g_pdata));
	// 测试Push/Pop并发
	MPSCQueue* mq2 = MPSCQueue::Create<TestData>("ABC", 8192*8);
	BOOST_REQUIRE(mq2->avg_lat_buf_ == nullptr);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_ == 0);
	BOOST_REQUIRE(mq2->avg_lat_buf_index_save_ == 0);	

	// 让32bit溢出
	mq2->ReinitQueueStatus((1ul << 32) - 1ul);

	boost::thread t1 = boost::thread(Producer1<1>, mq2, 200000000, 0);
	boost::thread t2 = boost::thread(Producer1<1>, mq2, 200000000, 1);
	boost::thread t3 = boost::thread(Consumer1<2>, mq2, 400000000);

	sleep(1);
	g_begin_concurrent_test = true;

	while (1)
	{
		sleep(1);
		std::cout << "g_pdata[0] == " << g_pdata[0] << " g_pdata[1]" << g_pdata[1] << std::endl;
		if (t3.try_join_for(boost::chrono::nanoseconds(1000000)))
		{
			std::cout << "g_pdata[0] == " << g_pdata[0] << " g_pdata[1]" << g_pdata[1] << std::endl;
			break;
		}
	}
	t1.join();
	t2.join();
}



