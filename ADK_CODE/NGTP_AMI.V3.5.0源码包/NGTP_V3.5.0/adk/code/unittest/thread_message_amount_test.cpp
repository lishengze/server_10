//
// Created by lzn on 12/9/19.
//
#define BOOST_TEST_MODULE thread
#include <boost/test/included/unit_test.hpp>

#include <adk/thread.h>


ADK_THREAD_MESSAGE(Test_0){};
ADK_THREAD_MESSAGE(Test_1){};
ADK_THREAD_MESSAGE(Test_2){};
ADK_THREAD_MESSAGE(Test_3){};
ADK_THREAD_MESSAGE(Test_4){};
ADK_THREAD_MESSAGE(Test_5){};
ADK_THREAD_MESSAGE(Test_6){};
ADK_THREAD_MESSAGE(Test_7){};
ADK_THREAD_MESSAGE(Test_8){};
ADK_THREAD_MESSAGE(Test_9){};
ADK_THREAD_MESSAGE(Test_10){};
ADK_THREAD_MESSAGE(Test_11){};
ADK_THREAD_MESSAGE(Test_12){};
ADK_THREAD_MESSAGE(Test_13){};
ADK_THREAD_MESSAGE(Test_14){};
ADK_THREAD_MESSAGE(Test_15){};
ADK_THREAD_MESSAGE(Test_16){};
ADK_THREAD_MESSAGE(Test_17){};
ADK_THREAD_MESSAGE(Test_18){};
ADK_THREAD_MESSAGE(Test_19){};
ADK_THREAD_MESSAGE(Test_20){};
ADK_THREAD_MESSAGE(Test_21){};
ADK_THREAD_MESSAGE(Test_22){};
ADK_THREAD_MESSAGE(Test_23){};
ADK_THREAD_MESSAGE(Test_24){};
ADK_THREAD_MESSAGE(Test_25){};
ADK_THREAD_MESSAGE(Test_26){};
ADK_THREAD_MESSAGE(Test_27){};
ADK_THREAD_MESSAGE(Test_28){};
ADK_THREAD_MESSAGE(Test_29){};
ADK_THREAD_MESSAGE(Test_30){};
ADK_THREAD_MESSAGE(Test_31){};
ADK_THREAD_MESSAGE(Test_32){};
ADK_THREAD_MESSAGE(Test_33){};
ADK_THREAD_MESSAGE(Test_34){};
ADK_THREAD_MESSAGE(Test_35){};
ADK_THREAD_MESSAGE(Test_36){};
ADK_THREAD_MESSAGE(Test_37){};
ADK_THREAD_MESSAGE(Test_38){};
ADK_THREAD_MESSAGE(Test_39){};

ADK_DEFINE_THREAD(TestThread, "TestThread")
{
public:
    int32_t OnInit()
    {
        block_point_ = 0;
        return adk::ErrorCode::kSuccess;
    }

    int32_t OnInitOnce()
    {
        return adk::ErrorCode::kSuccess;
    }

    ADK_DEFINE_MESSAGE_HANDLER(
            (OnMessage, Test_0),
            (OnMessage, Test_1),
            (OnMessage, Test_2),
            (OnMessage, Test_3),
            (OnMessage, Test_4),
            (OnMessage, Test_5),
            (OnMessage, Test_6),
            (OnMessage, Test_7),
            (OnMessage, Test_8),
            (OnMessage, Test_9),
            (OnMessage, Test_10),
            (OnMessage, Test_11),
            (OnMessage, Test_12),
            (OnMessage, Test_13),
            (OnMessage, Test_14),
            (OnMessage, Test_15),
            (OnMessage, Test_16),
            (OnMessage, Test_17),
            (OnMessage, Test_18),
            (OnMessage, Test_19),
            (OnMessage, Test_20),
            (OnMessage, Test_21),
            (OnMessage, Test_22),
            (OnMessage, Test_23),
            (OnMessage, Test_24),
            (OnMessage, Test_25),
            (OnMessage, Test_26),
            (OnMessage, Test_27),
            (OnMessage, Test_28),
            (OnMessage, Test_29),
            (OnMessage, Test_30),
            (OnMessage, Test_31),
            (OnMessage, Test_32),
            (OnMessage, Test_33),
            (OnMessage, Test_34),
            (OnMessage, Test_35),
            (OnMessage, Test_36),
            (OnMessage, Test_37),
            (OnMessage, Test_38),
            (OnMessage, Test_39)
    )

private:
    int32_t block_point_;
};

static int indexes[48];

#define MsgHandler(index) \
void TestThread::OnMessage(Test_##index *req)\
{\
    indexes[index] = 1; \
}

MsgHandler(0);
MsgHandler(1);
MsgHandler(2);
MsgHandler(3);
MsgHandler(4);
MsgHandler(5);
MsgHandler(6);
MsgHandler(7);
MsgHandler(8);
MsgHandler(9);
MsgHandler(10);
MsgHandler(11);
MsgHandler(12);
MsgHandler(13);
MsgHandler(14);
MsgHandler(15);
MsgHandler(16);
MsgHandler(17);
MsgHandler(18);
MsgHandler(19);
MsgHandler(20);
MsgHandler(21);
MsgHandler(22);
MsgHandler(23);
MsgHandler(24);
MsgHandler(25);
MsgHandler(26);
MsgHandler(27);
MsgHandler(28);
MsgHandler(29);
MsgHandler(30);
MsgHandler(31);
MsgHandler(32);
MsgHandler(33);
MsgHandler(34);
MsgHandler(35);
MsgHandler(36);
MsgHandler(37);
MsgHandler(38);
MsgHandler(39);

ADK_REGISTER_THREAD_BEGIN()

    (ADK_THREAD_CLASS(TestThread),
         adk::thread::EventMode = adk::thread::kInterrupt,
         adk::thread::InstanceNumber = 1,
         adk::thread::BusyPollNano = adk::thread::Microseconds(200))

ADK_REGISTER_THREAD_END()

#define SendMsgIndex(index) {\
auto sendMsg = Test_##index::New(); \
adk::SendMsg<TestThread>(sendMsg);\
}

BOOST_AUTO_TEST_CASE(pipeline_variant_entrance_sample)
{
    auto& thr_mana = *adk::ThreadManager::Instance();

    for(int i = 0; i < 48; i++)
    {
        indexes[i] = 0;
    }
    thr_mana.Start();

    SendMsgIndex(0)
    SendMsgIndex(1)
    SendMsgIndex(2)
    SendMsgIndex(3)
    SendMsgIndex(4)
    SendMsgIndex(5)
    SendMsgIndex(6)
    SendMsgIndex(7)
    SendMsgIndex(8)
    SendMsgIndex(9)
    SendMsgIndex(10)
    SendMsgIndex(11)
    SendMsgIndex(12)
    SendMsgIndex(13)
    SendMsgIndex(14)
    SendMsgIndex(15)
    SendMsgIndex(16)
    SendMsgIndex(17)
    SendMsgIndex(18)
    SendMsgIndex(19)
    SendMsgIndex(20)
    SendMsgIndex(21)
    SendMsgIndex(22)
    SendMsgIndex(23)
    SendMsgIndex(24)
    SendMsgIndex(25)
    SendMsgIndex(26)
    SendMsgIndex(27)
    SendMsgIndex(28)
    SendMsgIndex(29)
    SendMsgIndex(30)
    SendMsgIndex(31)
    SendMsgIndex(32)
    SendMsgIndex(33)
    SendMsgIndex(34)
    SendMsgIndex(35)
    SendMsgIndex(36)
    SendMsgIndex(37)
    SendMsgIndex(38)
    SendMsgIndex(39)


    sleep(3);

    for(int32_t i = 0; i < 40; i++)
    {
        //printf("Test Index%d\n", i);
        BOOST_REQUIRE(indexes[i] == 1);
    }
}