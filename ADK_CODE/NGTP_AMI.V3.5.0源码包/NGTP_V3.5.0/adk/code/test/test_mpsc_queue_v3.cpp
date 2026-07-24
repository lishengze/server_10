#include <adk/error_code.h>
#include <adk/lock_free_msg_queue.h>
#include <assert.h>
#include <stdint.h>
#include <boost/thread/thread.hpp>
#include <chrono>
#include <iostream>

using namespace adk;

void Producer(adk::MPSCQueue* mq, uint64_t init, uint64_t step, uint64_t end) {
  uint64_t sqn = init;
  uint64_t backoff = 0;
  while (true) {
    backoff = 1;
    while (mq->Push(sqn) != ErrorCode::kSuccess) {
      for (uint32_t i = 0; i < backoff; ++i) ADK_PAUSE();
      backoff <<= 1;
    }
    sqn += step;
    if (sqn >= end) break;
  }
}

void Consumer(adk::MPSCQueue* mq, uint64_t end) {
  uint64_t counter = 0;
  uint64_t backoff = 0;
  while (true) {
    backoff = 128;
    while (mq->Pop(counter) != ErrorCode::kSuccess) {
      for (uint32_t i = 0; i < backoff; ++i) ADK_PAUSE();
      backoff <<= 1;
    }
    if (++counter >= end) break;
  }
}

int main(int argc, char const* argv[]) {
  constexpr uint64_t sum = 1 << 25Ul;
  {
    adk::MPSCQueue* mq =
        adk::MPSCQueue::Create("test1p1c", sizeof(uint64_t), 8192);
    boost::thread c_thread = boost::thread(Consumer, mq, sum);
    std::vector<boost::thread> producers;

    auto begin = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1; ++i) {
      producers.emplace_back(Producer, mq, 0, 1, sum);
    }
    for (auto& t : producers) {
      t.join();
    }
    auto end = std::chrono::high_resolution_clock::now();
    c_thread.join();
    auto delta = end - begin;
    std::cout << "1P1C through: " << sum * 1e9 / delta.count() << std::endl;

    delete mq;
  }

  {
    adk::MPSCQueue* mq =
        adk::MPSCQueue::Create("test2p1c", sizeof(uint64_t), 8192);
    boost::thread c_thread = boost::thread(Consumer, mq, sum);
    std::vector<boost::thread> producers;

    auto begin = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 2; ++i) {
      producers.emplace_back(Producer, mq, i, 2, sum);
    }
    for (auto& t : producers) {
      t.join();
    }
    auto end = std::chrono::high_resolution_clock::now();
    c_thread.join();
    auto delta = end - begin;
    std::cout << "2P1C through: " << sum * 1e9 / delta.count() << std::endl;
    delete mq;
  }

  {
    adk::MPSCQueue* mq =
        adk::MPSCQueue::Create("test4p1c", sizeof(uint64_t), 8192);
    boost::thread c_thread = boost::thread(Consumer, mq, sum);
    std::vector<boost::thread> producers;

    auto begin = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 4; ++i) {
      producers.emplace_back(Producer, mq, i, 4, sum);
    }
    for (auto& t : producers) {
      t.join();
    }
    auto end = std::chrono::high_resolution_clock::now();
    c_thread.join();
    auto delta = end - begin;
    std::cout << "4P1C through: " << sum * 1e9 / delta.count() << std::endl;
    delete mq;
  }

  {
    adk::MPSCQueue* mq =
        adk::MPSCQueue::Create("test8p1c", sizeof(uint64_t), 8192);
    boost::thread c_thread = boost::thread(Consumer, mq, sum);
    std::vector<boost::thread> producers;

    auto begin = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 8; ++i) {
      producers.emplace_back(Producer, mq, i, 8, sum);
    }
    for (auto& t : producers) {
      t.join();
    }
    auto end = std::chrono::high_resolution_clock::now();
    c_thread.join();
    auto delta = end - begin;
    std::cout << "8P1C through: " << sum * 1e9 / delta.count() << std::endl;
    delete mq;
  }

  {
    adk::MPSCQueue* mq =
        adk::MPSCQueue::Create("test16p1c", sizeof(uint64_t), 8192);
    boost::thread c_thread = boost::thread(Consumer, mq, sum);
    std::vector<boost::thread> producers;

    auto begin = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 16; ++i) {
      producers.emplace_back(Producer, mq, i, 16, sum);
    }
    for (auto& t : producers) {
      t.join();
    }
    auto end = std::chrono::high_resolution_clock::now();
    c_thread.join();
    auto delta = end - begin;
    std::cout << "16P1C through: " << sum * 1e9 / delta.count() << std::endl;
    delete mq;
  }

  return 0;
}

// clang-format off
// result
// ➜  test git:(develop) ✗ numactl -N 1 ./bin/gcc-4.8.5/release/debug-symbols-on/threading-multi/test_mpsc_queue_v3
// 1P1C through: 5.51969e+07
// 2P1C through: 9.69435e+06
// 4P1C through: 6.99362e+06
// 8P1C through: 4.84172e+06
// 16P1C through: 3.13178e+06
// clang-format on