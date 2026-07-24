#define BOOST_TEST_MODULE message_pool

#include <vector>
#include <message_pool.h>

#include <boost/format.hpp>
#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_CASE(test_MessagePool)
{
    constexpr uint32_t kBlockSize = 1024;
    constexpr uint32_t kBlockNum = 1024;

    adk::io_engine::TxMessagePool tx_message_pool;

    adk::io_engine::MessageImpl* tx_buffer_array[kBlockNum];
    for (uint32_t index = 0; index < kBlockNum; ++index)
    {
        const auto allocate_len = kBlockSize + index * 1024;
        auto* const message = tx_message_pool.NewMessage(allocate_len);
        BOOST_CHECK(message);
        BOOST_CHECK(message->is_direction_tx());
        BOOST_CHECK_GE(message->capacity(), allocate_len);
        BOOST_CHECK_EQUAL(message->data_len(), 0);
        BOOST_CHECK_EQUAL(message->consume_len(), 0);
        memset(message->data(), 0, message->capacity());
        tx_buffer_array[index] = message;
    }

    adk::io_engine::RxMessagePool rx_message_pool;
    rx_message_pool.Init(kBlockSize, kBlockNum);

    BOOST_CHECK_GE(rx_message_pool.message_capacity(), kBlockSize);

    adk::io_engine::MessageImpl* rx_buffer_array[kBlockNum];
    for (uint32_t index = 0; index < kBlockNum; ++index)
    {
        auto* const message = rx_message_pool.NewMessage(nullptr);
        BOOST_CHECK(message);
        BOOST_CHECK(!message->is_direction_tx());
        BOOST_CHECK_EQUAL(message->capacity(), rx_message_pool.message_capacity());
        BOOST_CHECK_EQUAL(message->data_len(), 0);
        BOOST_CHECK_EQUAL(message->consume_len(), 0);
        memset(message->data(), 0, message->capacity());
        rx_buffer_array[index] = message;
    }

    for (uint32_t index = 0; index < kBlockNum; ++index)
    {
        tx_message_pool.DeleteMessage(tx_buffer_array[index]);
        rx_message_pool.DeleteMessage(rx_buffer_array[index]);
    }
}