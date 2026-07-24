#include <stdint.h>
#include <unistd.h>

#include <thread>
#include <string>
#include <vector>
#include <iostream>

#include <adk_pack/lock_free_msg_queue.h>

volatile bool g_is_running = false;

void Delay(uint32_t delay)
{
	volatile uint64_t delay_counter = 0;
	for (uint64_t index = 0; index < delay; ++index)
	{
		++delay_counter;
	}
}

template<typename QueueType, int32_t Producer, int32_t Consumer>
void TestBody(QueueType* queue)
{
	std::vector<std::thread> send_thds;
	std::vector<std::thread> recv_thds;

	g_is_running = true;
	uint64_t total_counter[Producer] = { 0 };
	for (int32_t index = 0; index < Producer; ++index)
	{
		send_thds.push_back(std::thread([&](int32_t thd_index) {
			uint64_t  producer_counter = thd_index;
			uint64_t& counter = total_counter[thd_index];
			do
			{
				if (adk::ErrorCode::kSuccess == queue->Push(producer_counter))
				{
					producer_counter += Producer;
					++counter;
				}
				else
				{
				    Delay(100);
				}
			} while (g_is_running);
		}, index));
	}
	
	for (int32_t index = 0; index < Consumer; ++index)
	{
		recv_thds.push_back(std::thread([&]() {
			uint64_t total_counter[Producer];
			
			for (int32_t index = 0; index < Producer; ++index)
			{
				total_counter[index] = index;
			}
			
			uint64_t consumer;
			do
			{
				if (adk::ErrorCode::kSuccess == queue->Pop(consumer))
				{
					uint64_t& temp = total_counter[consumer & (Producer - 1)];
					if (1 == Consumer)
					{
						if (temp != consumer)
						{
							std::cout << "BUG ON!!!" << "pop_value = " << consumer
									  << ", expect_value = " << temp << std::endl;
							g_is_running = false;
							break;
						}
						
						temp = consumer + Producer;
					}
					else
					{
						if (consumer < temp)
						{
							std::cout << "BUG ON!!!" << "pop_value = " << consumer
									  << ", last_consumer = " << temp << std::endl;
							g_is_running = false;
							break;
						}
						
						temp = consumer;
					}
				}
				else
				{
					Delay(100);
				}
			} while (g_is_running);
		}));
	}
	
	std::thread observer_thd = std::thread([&]() {
		uint64_t counter = 0;
		do
		{
			sleep(1);
			
			uint64_t temp = 0;
			for (int32_t index = 0; index < Producer; ++index)
			{
				temp += total_counter[index];
			}
			
			std::cout << "counter diff = " << temp - counter << std::endl;
			counter = temp;
		} while (g_is_running);
	});
	
	observer_thd.join();
	
	for (auto& thd : send_thds)
	{
		thd.join();
	}
	
	for (auto& thd : recv_thds)
	{
		thd.join();
	}
}

int main(int32_t argc, char* argv[])
{
	if (argc < 2)
	{
		std::cout << "Please input queue type" << std::endl;
	}
	
	if (std::string(argv[1]) == std::string("SPSC"))
	{
		std::cout << "start spsc queue test" << std::endl;
		
		adk::SPSCQueue<uint64_t>* queue = adk::SPSCQueue<uint64_t>::Create("SPSC", sizeof(uint64_t), 8192);
		TestBody<adk::SPSCQueue<uint64_t>, 1, 1>(queue);
	}
	else if (std::string(argv[1]) == std::string("MPSC"))
	{
		std::cout << "start mpsc queue test" << std::endl;
		adk::MPSCQueue* queue = adk::MPSCQueue::Create("MPSC", sizeof(uint64_t), 8192);
		TestBody<adk::MPSCQueue, 2, 1>(queue);
	}
	else if (std::string(argv[1]) == std::string("SPMC"))
	{
		std::cout << "start spmc queue test" << std::endl;
		adk::SPMCQueue* queue = adk::SPMCQueue::Create("SPMC", sizeof(uint64_t), 8192);
		TestBody<adk::SPMCQueue, 1, 2>(queue);
	}
	else
	{
		std::cout << "not support " << argv[1] << " test" << std::endl;
	}
	
	return 0;
}