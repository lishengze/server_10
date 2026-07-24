#define BOOST_TEST_MODULE pipe
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk_pack/spsc_byte_buffer.h>
#include <adk_pack/error_code.h>
#include <adk_pack/arch/generic.h>

#include <set>
#include <string>
#include <vector>
#include <map>
#include <string.h>

using namespace adk;

#define TEST_BB_SIZE (32)

BOOST_AUTO_TEST_CASE(init_and_basic)
{
    bytebuffer::ByteBuffer bb;
    BOOST_REQUIRE(bb.Init(TEST_BB_SIZE) == ErrorCode::kSuccess);

    auto* p = bb.GetBufferProducer();
    auto* c = bb.GetBufferConsumer();
    BOOST_REQUIRE(p != nullptr);
    BOOST_REQUIRE(c != nullptr);

    BOOST_REQUIRE(p->remaining() == TEST_BB_SIZE);
    BOOST_REQUIRE(p->position() == 0);
    BOOST_REQUIRE(p->current() != nullptr);
    BOOST_REQUIRE(p->valid_data() == 0);

    BOOST_REQUIRE(c->remaining() == 0); 
    BOOST_REQUIRE(c->position() == 0);
    BOOST_REQUIRE(c->current() != nullptr);

    BOOST_REQUIRE(p->current() == c->current());

    // basic
    // ====================================================

    char* buffer = p->current();
    memcpy(buffer, "ABCDEFGHIJKL", 12); // read data from network
    p->inc_position(12);                // inc actually read bytes
    p->inc_valid_data(8);               // inc visible bytes

    BOOST_REQUIRE(p->position() == 12);
    BOOST_REQUIRE(p->remaining() == (TEST_BB_SIZE - 12));
    BOOST_REQUIRE(p->valid_data() == 8);

    BOOST_REQUIRE(c->position() == 0);
    BOOST_REQUIRE(c->remaining() == 8);
    BOOST_REQUIRE(memcmp(c->current(), "ABCDEFGH", 8) == 0);

    c->consume_bytes(8);
    BOOST_REQUIRE(c->position() == 8);
    BOOST_REQUIRE(c->remaining() == 0);

    // overflow
    char data[TEST_BB_SIZE];
    memset(data, 'A', TEST_BB_SIZE);
    memset(data + TEST_BB_SIZE - 4, 'E', 4);
    memcpy(p->current(), data, TEST_BB_SIZE);           // read data from network
    p->inc_position(TEST_BB_SIZE);                // inc actually read bytes
    p->set_valid_data(p->position() - 4);         // set visible bytes
    
    BOOST_REQUIRE(p->remaining() <= 0);
    BOOST_REQUIRE(p->valid_data() == (12 + TEST_BB_SIZE - 4));

    BOOST_REQUIRE(p->RenewBuffer() == ErrorCode::kSuccess);
    BOOST_REQUIRE(p->remaining() == TEST_BB_SIZE - 4);
    BOOST_REQUIRE(p->position() == 4);
    BOOST_REQUIRE(p->current() != nullptr);
    BOOST_REQUIRE(p->valid_data() == 0);
    BOOST_REQUIRE(memcmp(p->current() - 4, "EEEE", 4) == 0);

    char data_b[TEST_BB_SIZE];
    memset(data_b, 'B', 32);

    memcpy(p->current(), data_b, TEST_BB_SIZE);           // read data from network
    p->inc_position(TEST_BB_SIZE);                  // inc actually read bytes
    p->set_valid_data(p->position());               // set visible bytes

    BOOST_REQUIRE(p->position() == 4 + TEST_BB_SIZE);
    BOOST_REQUIRE(p->remaining() <= 0);
    BOOST_REQUIRE(p->valid_data() == 4 + TEST_BB_SIZE);

    BOOST_REQUIRE(p->RenewBuffer(adk::bytebuffer::kNonblock) == ErrorCode::kFailure);
    BOOST_REQUIRE(p->position() == 4 + TEST_BB_SIZE);
    BOOST_REQUIRE(p->remaining() <= 0);
    BOOST_REQUIRE(p->valid_data() == 4 + TEST_BB_SIZE);

    BOOST_REQUIRE_EQUAL(c->position(), 8);
    BOOST_REQUIRE(c->remaining() == (12 + TEST_BB_SIZE - 4 - 8));
    BOOST_REQUIRE_EQUAL(c->position(), 8);
    BOOST_REQUIRE(memcmp(c->current(), "IJKL", 4) == 0);
    BOOST_REQUIRE(memcmp(c->current() + 4, data, TEST_BB_SIZE - 4) == 0);

    BOOST_REQUIRE(c->remaining() == (12 + TEST_BB_SIZE - 4 - 8));
    c->consume_bytes(c->remaining());
    BOOST_REQUIRE(c->position() == 12 + TEST_BB_SIZE - 4);
    // turn over
    BOOST_REQUIRE(c->remaining() == 4 + TEST_BB_SIZE);
    BOOST_REQUIRE(c->position() == 0);
    BOOST_REQUIRE(c->remaining() == 4 + TEST_BB_SIZE);

    BOOST_REQUIRE(memcmp(c->current(), "EEEE", 4) == 0);
    BOOST_REQUIRE(memcmp(c->current() + 4, data_b, TEST_BB_SIZE) == 0);

    // turn over
    BOOST_REQUIRE(p->RenewBuffer(adk::bytebuffer::kNonblock) == ErrorCode::kSuccess);
    BOOST_REQUIRE(p->position() == 0);
    BOOST_REQUIRE(p->remaining() == TEST_BB_SIZE);
    BOOST_REQUIRE(p->valid_data() == 0);

    memcpy(p->current(), data_b, TEST_BB_SIZE);
    p->inc_position(TEST_BB_SIZE);
    p->inc_valid_data(TEST_BB_SIZE);

    BOOST_REQUIRE(p->position() == TEST_BB_SIZE);
    BOOST_REQUIRE(p->remaining() <= 0);
    BOOST_REQUIRE(p->valid_data() == TEST_BB_SIZE);
    BOOST_REQUIRE(p->RenewBuffer(adk::bytebuffer::kNonblock) == ErrorCode::kFailure);
    BOOST_REQUIRE(p->RenewBuffer(adk::bytebuffer::kNonblock) == ErrorCode::kFailure);
    BOOST_REQUIRE(p->RenewBuffer(adk::bytebuffer::kNonblock) == ErrorCode::kFailure);

    BOOST_REQUIRE(c->position() == 0);
    BOOST_REQUIRE(c->remaining() == 4 + TEST_BB_SIZE);
    c->consume_bytes(c->remaining());
    BOOST_REQUIRE(c->position() == 4 + TEST_BB_SIZE);
    // turn over
    BOOST_REQUIRE(c->remaining() == TEST_BB_SIZE);
    BOOST_REQUIRE(c->position() == 0);
    BOOST_REQUIRE(memcmp(c->current(), data_b, TEST_BB_SIZE) == 0);
    c->consume_bytes(c->remaining());
    BOOST_REQUIRE(c->remaining() == 0);
    BOOST_REQUIRE(c->remaining() == 0);
    BOOST_REQUIRE(c->position() == TEST_BB_SIZE);

    BOOST_REQUIRE(p->remaining() <= 0);
    BOOST_REQUIRE(p->RenewBuffer(adk::bytebuffer::kNonblock) == ErrorCode::kSuccess);
    BOOST_REQUIRE(p->position() == 0);
    BOOST_REQUIRE(p->remaining() == TEST_BB_SIZE);
    BOOST_REQUIRE(p->valid_data() == 0);

    BOOST_REQUIRE(c->remaining() == 0);
    BOOST_REQUIRE(c->position() == 0);
    BOOST_REQUIRE(c->remaining() == 0);
    BOOST_REQUIRE(c->remaining() == 0);

    p->AppendBytes("CCDDEE", 6);
    BOOST_REQUIRE(p->position() == 6);
    BOOST_REQUIRE(p->remaining() == TEST_BB_SIZE - 6);
    BOOST_REQUIRE(p->valid_data() == 0);

    p->Clear();
    BOOST_REQUIRE(p->position() == 0);
    BOOST_REQUIRE(p->remaining() == TEST_BB_SIZE);
    BOOST_REQUIRE(p->valid_data() == 0);    

    p->AppendBytes("FFGGJJ", 6);
    BOOST_REQUIRE(p->position() == 6);
    BOOST_REQUIRE(p->remaining() == TEST_BB_SIZE - 6);
    BOOST_REQUIRE(p->valid_data() == 0);        

    p->IncValidData(6);
    BOOST_REQUIRE(p->position() == 6);
    BOOST_REQUIRE(p->remaining() == TEST_BB_SIZE - 6);
    BOOST_REQUIRE(p->valid_data() == 6);

    BOOST_REQUIRE(c->remaining() == 6);
    BOOST_REQUIRE(c->position() == 0);
    BOOST_REQUIRE(memcmp(c->Current(), "FFGGJJ", 6) == 0);
    c->ConsumeBytes(6);
    BOOST_REQUIRE(c->remaining() == 0);
    BOOST_REQUIRE(c->position() == 6);

    p->AppendBytes("KKGG", 4);
    BOOST_REQUIRE(p->position() == 10);
    BOOST_REQUIRE(p->remaining() == TEST_BB_SIZE - 10);
    BOOST_REQUIRE(p->valid_data() == 6);
    BOOST_REQUIRE(p->AppendBytesSize() == 4);
}

struct MessageHeader
{
    uint32_t body_len;
};

void ProducerMain(bytebuffer::Producer* p, int32_t nr_msgs)
{
    srandom(time(nullptr));
    char network_buffer[1024];
    do {

        // simulate a copy from last position
        auto* header = (MessageHeader*)(&network_buffer[0]);
        header->body_len = random() % 128;
        p->AppendBytes(header, header->body_len + sizeof(MessageHeader));
        
        // if the copy bytes larger than "header->body_len + sizeof(MessageHeader)"
        // only increase valid message len
        // using p->ValidDataEnd() to calc the edge of an valid message 
        ADK_BARRIER();
        
        p->IncValidData(header->body_len + sizeof(MessageHeader));
        
        // p->inc_valid_data(header->body_len + sizeof(MessageHeader));
        // int32_t left_size = p->position() - p->valid_data();

        // if (p->remaining() <= 0)
        // {
                // block mode
                // the new bytebuffer already contains the left_size
        //     p->RenewBuffer();
        // }
    } while ((--nr_msgs) > 0);
}

void ConsumerMain(bytebuffer::Consumer* c, int32_t nr_msgs)
{
    do {
        if (c->Remaining() > 0)
        {
            auto* header = (MessageHeader*)c->Current();
            
            // process one message
            // xxxxxx;
            
            c->ConsumeBytes(header->body_len + sizeof(MessageHeader));
            --nr_msgs;
        }
        
        ADK_PAUSE();
    } while (nr_msgs > 0);
}

#define TEST_BB_SIZE_PERF (1024)
BOOST_AUTO_TEST_CASE(test_sp_sc)
{
    bytebuffer::ByteBuffer bb;
    BOOST_REQUIRE(bb.Init(TEST_BB_SIZE_PERF) == ErrorCode::kSuccess);

    auto* p = bb.GetBufferProducer();
    auto* c = bb.GetBufferConsumer();

    boost::thread pt = boost::thread(ProducerMain, p, 1000000000);
    boost::thread ct = boost::thread(ConsumerMain, c, 1000000000);

    pt.join();
    ct.join();


    // 确保内部的buffer会反转
    c->Remaining();
    BOOST_REQUIRE(p->current() == c->current());
}
