//
// Created by lzn on 9/17/19.
//
#define BOOST_TEST_MODULE pipeline
#include <boost/test/included/unit_test.hpp>

#include "adk/pipeline_variant.h"

using namespace adk;

template< class T >
class ValueChecker : public StageBase<T, T>
{
public:
    std::vector<T> values;
    bool isReceived = false;
    void Message(const T& in) override
    {
        values.push_back(in);
        isReceived = true;
        this->Forward(in);
    }
};

/**
 * 新的Pipeline类,主要有两种定义方式:
 * 一种是与之前类似的,在类中 Override 相关的方法
 * 另一种是在定义流水线的地方，通过方法原地定义相关的行为．
 * 接下来对具体的使用方法进行说明．
 */

BOOST_AUTO_TEST_CASE(pipeline_variant_entrance_sample)
{
    /*
     * 这里是原地定义的基本的用法
     */
    //Basic Use: Entrance -> NextStage123456

    int receivedValue = 0;
    StageConfig cnf; //通过StageConfig可以配置当前步骤的特性，如是否开新线程运行
    cnf.is_same_context = true; //All Stages are executed in main thread.
    auto ent = Entrance<int>().Build(); //入口的定义，通过模板参数指定可以输出的类型
    auto next = DefaultBuilder<int, int>(cnf)
            .OnMessage([&](const int& msg) -> int{
                receivedValue = msg; //通过OnMessage定义当接到消息时的行为
                return msg;
            }).Build();

    *ent | next; //Connect two stages.　//对两个Stage进行连接

    // ent ----> next

    //在连接完毕后，调用Start对整个流水线进行初始化
    //所有新建线程等操作都会在这条语句中运行。
    ent->Start(); // Call Start to Enable this pipeline (Create work thread...etc...)

    ent->Forward(114);

    BOOST_REQUIRE(receivedValue == 114);

    ent->Stop(); //Stop a Entrance will stop all stage in this pipeline.
}

/**
 * 这里是类定义的使用示例
 * 主要方法是 Override 对应各个事件的虚函数
 * 具体的函数列表如下所示
 */
class CustomStageBasic : public StageBase<int ,int>
{
public:
    /***
     * 在整个流水线启动的时候会调用的方法
     * 所有fork不安全的操作，都建议放在这个方法进行，而不是构造函数
     * @return
     */
    bool Start() override
    {
        //Do Init...
        return StageType::Start();
    }

    /**
     * 收到消息时会调用的函数
     * 需要将消息传送到下一步时，调用 Forward 方法即可
     * @param in 收到的数据
     */
    void Message(const int& in) override;

    /**
     * 当流水线暂停时会调用的方法。
     * 记得不要忘记调用基类的Pause(
     */
    void Pause() override
    {
        //Do Pause...
        StageType::Pause();
    }

    /**
     * 当流水线恢复运行时会调用的方法。
     * 记得不要忘记调用基类的Resume(
     * 注：当一个Stage被暂停时，会丢弃所有接收到的消息。
     */
    void Resume() override
    {
        //Do Resume...
        StageType::Resume();
    }

    /**
     * 当Message()方法在运行时抛出异常会调用的方法。
     * @param exception_info 获取到的关于异常的信息
     */
    void Error(const std::string &exception_info) override
    {
        //Do Error...
        StageType::Error(exception_info);
    }

    /**
     * 当这个流水线被完全的终止时调用的代码。
     * (比如join新建出的线程之类的)
     */
    void Stop() override
    {
        //Do Stop...
        StageType::Stop();
    }
};

void CustomStageBasic::Message(const int& in)
{
    this->Forward(in);
    //Handle this message
    //Example: this->Forward(in); //Forward this message to next stage.
}


BOOST_AUTO_TEST_CASE(pipeline_variant_custom_stage_example)
{
    //Basic Use: Entrance -> NextStage123456
    /*
     * 关于上面类定义方法的使用例子。
     */
    int receivedValue = 0;
    StageConfig cnf;
    cnf.is_same_context = true; //All Stages are executed in main thread.
    auto ent = Entrance<int>().Build();
    auto next = StageBuilder<CustomStageBasic>(cnf).Build();
    //通过StageBuilder引入上面定义的类
    //这里auto也可以换成 CustionStageBasic*

    *ent | next; //Connect two stages.
    // ent ----> next

    ent->Start();// Invoke Start after all stages are connected.

    ent->Forward(114);

    ent->Stop(); //Stop a Entrance will stop all stage in this pipeline.
}


BOOST_AUTO_TEST_CASE(pipeline_variant_function_override)
{
    bool isStartInvoked = false;
    bool isErrorInvoked = false;
    bool isPauseInvoked = false;
    bool isResumeInvoked = false;
    bool isStopInvoked = false;
    auto ent = Entrance<int>().Build();
    StageConfig cnf;
    cnf.is_same_context = true; //All Stages are executed in main thread.
    auto next = DefaultBuilder<int, int>(cnf)
            .OnStart([&](){isStartInvoked = true;})
            .OnError([&](const std::string&){isErrorInvoked = true;})
            .OnPause([&](){isPauseInvoked = true;})
            .OnResume([&](){isResumeInvoked = true;})
            .OnStop([&](){isStopInvoked = true;})
            .OnMessage([](){throw new std::exception();})//Throw a exception when Received a message.
            .Build();
    *ent | next;
    // ent ----> next

    ent->Start();
    BOOST_REQUIRE(isStartInvoked);

    ent->Pause();
    BOOST_REQUIRE(isPauseInvoked);
    ent->Forward(114);
    BOOST_REQUIRE(!isErrorInvoked);//A paused stage should not process any message.
    ent->Resume();
    BOOST_REQUIRE(isResumeInvoked);
    ent->Forward(114);
    BOOST_REQUIRE(isErrorInvoked);
    ent->Stop();
    BOOST_REQUIRE(isStopInvoked);
}

class TestStageOverride : public StageBase<int ,int>
{
public:
    bool isExecuted = false;
    bool Start() override
    {
        isExecuted = true;
        return StageType::Start();
    }

    void Pause() override
    {
        isExecuted = true;
        StageType::Pause();
    }

    void Resume() override
    {
        isExecuted = true;
        StageType::Resume();
    }

    void Error(const std::string &exception_info) override
    {
        isExecuted = true;
        StageType::Error(exception_info);
    }

    void Stop() override
    {
        isExecuted = true;
        StageType::Stop();
    }
};

/*
 * 类似的，所有在类中可以定义的方法，都可以在原地以类似的形式进行定义。
 * 默认在原地定义的代码，并不会覆盖类中定义的原代码。
 * 而是以原地定义->原类定义 的顺序执行。
 * 但如果在原地定义时输入额外的模板参数（如下所示），则可以覆盖原方法的代码。
 */
BOOST_AUTO_TEST_CASE(pipeline_variant_function_override_2)
{
    auto ent = Entrance<int>().Build();
    StageConfig cnf;
    cnf.is_same_context = true; //All Stages are executed in main thread.
    auto next = StageBuilder<TestStageOverride>(cnf)
            .OnStart<true>([&](){})
            .OnError<true>([&](const std::string&){})
            .OnPause<true>([&](){})
            .OnResume<true>([&](){})
            .OnStop<true>([&](){})
            .OnMessage([](){throw new std::exception();})//Throw a exception when Received a message.
            .Build();
    *ent | next;
    // ent ----> next

    ent->Start();

    ent->Pause();
    ent->Forward(114);
    ent->Resume();
    ent->Forward(114);
    ent->Stop();
    BOOST_REQUIRE(next->isExecuted == false); //All method are overwritten above.
}

/**
 * 对于对象的创建，可以在顶一个Builder时，加入Duplicate，
 * 从而同时创建多个副本。
 *
 * Stage默认的连接方式，会将消息传输到连接着的所有步骤
 */
BOOST_AUTO_TEST_CASE(pipeline_variant_duplicate)
{
    int invokeCount = 0;
    int value[2];
    auto ent = Entrance<int>().Build();
    StageConfig cnf;
    cnf.is_same_context = true;
    auto next = DefaultBuilder<int, int>(cnf)
            .OnMessage([&](const int& val){
                value[invokeCount] = val;
                invokeCount++;
            }).Duplicate(2).Build();//vector< StageBase<int, int> >
    *ent | next;
    /*
     *     next(duplicate_1)
     *    /
     * ent
     *    \
     *     next(duplicate_2)
     */

    ent->Start();

    ent->Forward(114);
    BOOST_REQUIRE(invokeCount == 2);
    BOOST_REQUIRE(value[0] == 114);
    BOOST_REQUIRE(value[1] == 114);

    ent->Stop();
}


/**
 * 但是我们可以通过附加LoadBalancer，来改变Stage的某一个输出端的连接方式。
 * LoadBalancer接受一个返回int的函数，将返回来的数值对 连接总数 求余，决定消息的流向。
 * 下面的的例子就是 数据 % 2 来选择接收数据的Stage的。
 */
BOOST_AUTO_TEST_CASE(pipeline_variant_duplicate_lb)
{
    int invokeCount = 0;
    int value[2];
    auto ent = Entrance<int>()
            .LoadBalancer<int>([](const int& val) -> int {return val;}) // Auto Mod
            .Build();
    StageConfig cnf;
    cnf.is_same_context = true;
    auto next = DefaultBuilder<int, int>(cnf)
            .OnMessage([&](const int& val){
                value[invokeCount] = val;
                invokeCount++;
            }).Duplicate(2).Build();
    *ent | next;
    /*
     *     next(duplicate_1)
     *    /
     * ent
     *    \
     *     next(duplicate_2)
     */

    ent->Start();

    ent->Forward(114);
    ent->Forward(115);
    BOOST_REQUIRE(invokeCount == 2);
    BOOST_REQUIRE(value[0] == 114);
    BOOST_REQUIRE(value[1] == 115);
}

/**
 * LoadBalance在没有指定函数时，默认采取的策略是Round-Robin
 */
BOOST_AUTO_TEST_CASE(pipeline_variant_default_lb)
{
    int invokeCount = 0;
    int value[2];
    auto ent = Entrance<int>()
            .LoadBalancer<int>() // Default LoadBalancer
            .Build();
    StageConfig cnf;
    cnf.is_same_context = true;
    auto next = StageBuilder< ValueChecker<int> >(cnf)
            .Duplicate(2)
            .Build();
    *ent | next;
    /*
     *     next(duplicate_1)
     *    /
     * ent
     *    \
     *     next(duplicate_2)
     */

    ent->Start();

    ent->Forward(114);
    ent->Forward(115);
    BOOST_REQUIRE(next[0]->values.size() == 1);
    BOOST_REQUIRE(next[1]->values.size() == 1);
    BOOST_REQUIRE(next[0]->values[0] == 115);
    BOOST_REQUIRE(next[1]->values[0] == 114);
}

class TestStageIndex : public StageBase<int, int>
{
};


/**
 * 对于被Duplicate的类，可以通过 GetIndex() 获取自己在这一组副本中的编号
 */
BOOST_AUTO_TEST_CASE(pipeline_variant_duplicate_getindex)
{
    auto next = StageBuilder<TestStageIndex>().Duplicate(2).Build();
    BOOST_REQUIRE(next[0]->GetIndex() == 0);
    BOOST_REQUIRE(next[1]->GetIndex() == 1);
}

/**
 * 对于一个连接操作，也可以通过 group 将一组 Stage/流程 合并起来一同连接
 *
 */

BOOST_AUTO_TEST_CASE(pipeline_variant_group_connect)
{
    uint64_t index = 0;
    double result[10];
    StageConfig cnf;
    cnf.is_same_context = true;
    auto ent = Entrance<int>().Build();
    auto next_1 = DefaultBuilder<int, double>(cnf)
            .OnMessage([&](const int& in) -> double{return in * 1.50f;})
            .Build();
    auto next_2 = DefaultBuilder<int, int>(cnf)
            .OnMessage([&](const int& in) -> int{return in * 2;})
            .Build();
    auto next_3 = DefaultBuilder<double, int>(cnf)
            .OnMessage([&](const double& in){
                result[index] = in;
                index++;
            })
            .Build();

    *ent | group(next_1, next_2 | transform([&](const int& in) -> double {return in * 1.50f;})) | next_3;
    /*     - next_1 ---------------
     *    /                        \
     * ent                           next_3
     *    \                        /
     *     - next_2 - transform() -
     */

    ent->Start();

    ent->Forward(1);
    constexpr double epsilon = 0.0000001f;
    BOOST_REQUIRE(index == 2);
    BOOST_REQUIRE(std::abs(result[0] - 1.5f) < epsilon);
    BOOST_REQUIRE(std::abs(result[1] - 3.0f) < epsilon);
}


/**
 * 对于原地的定义，你也可以通过 returnValues<> 类型来允许多种类型值的返回，
 * 也就是可以发送数据到不同的输出端。
 */
BOOST_AUTO_TEST_CASE(pipeline_variant_on_message)
{
    StageConfig cnf;
    cnf.is_same_context = true;
    auto ent = Entrance<int>().Build();
    auto next_1 = DefaultBuilder<int, int>(cnf)
            .OnMessage([](){}).Build(); //OnMessage Accept no arg, no ret functor.
    auto next_2 = DefaultBuilder<int, int>(cnf)
            .OnMessage([](){return 0;}).Build(); //OnMessage Accept no arg, RetType return functor.
    auto next_3 = DefaultBuilder<int, int>(cnf)
            .OnMessage([](const int& in){}).Build();//OnMessage Accept const ArgType& arg, no ret functor.
    auto next_4 = DefaultBuilder<int, int>(cnf)
            .OnMessage([](const int& in){return in;}).Build();//OnMessage Accept const ArgType& arg, RetType return functor.
    auto next_5 = DefaultBuilder<int, int, double>(cnf)
            .OnMessage([](const int& in) -> returnValues<int, double>{ // Use returnValues to return different types of data.
                if(in < 0)
                    return in;
                else if(in < 100)
                    return (double)(in * 1.5f);
                return returnValues<int, double>(); // returns Nothing.
            }).Build();
    bool invoked_int = false;
    bool invoked_double = false;
    auto next_6 = DefaultBuilder<int, int>(cnf)
            .OnMessage([&](){invoked_int = true;}).Build();
    auto next_7 = DefaultBuilder<int, int>(cnf)
            .OnMessage([&](){invoked_double = true;}).Build();
    ent | group(next_1, next_2, next_3, next_4, next_5);
    *next_5 | group(next_6, next_7);
    /*       - next_1
     *      /
     *     /-- next_2
     *    /
     * ent---- next_3
     *    \
     *     \-- next_4
     *      \
     *       - next_5 - next_6
     *               \
     *                - next_7
     */


    ent->Start();

    ent->Forward(200);
    BOOST_REQUIRE(!invoked_double && !invoked_int);
    ent->Forward(-1);
    BOOST_REQUIRE(invoked_int);
    ent->Forward(50);
    BOOST_REQUIRE(invoked_double);
    ent->Stop();
}


BOOST_AUTO_TEST_CASE(pipeline_variant_where)
{
    StageConfig cnf;
    cnf.is_same_context = true;
    auto ent = Entrance<int>().Build();
    auto next_1 = StageBuilder< ValueChecker<int> >(cnf)
            .Build();
    ent | where([](const int& in) -> bool {return in >= 0;}) | next_1;
    // ent ----> Any messages >=0 ----> next_1

    ent->Start();

    ent->Forward(-1);
    ent->Forward(1);
    BOOST_REQUIRE(next_1->isReceived);
    BOOST_REQUIRE(next_1->values.size() == 1);
    BOOST_REQUIRE(next_1->values[0] == 1);
    ent->Stop();
}


BOOST_AUTO_TEST_CASE(pipeline_variant_reorder)
{
    StageConfig cnf;
    cnf.is_same_context = true;
    auto ent = Entrance<int>()
            .LoadBalancer<int>([](const int& in) -> int{return in;})
            .Build();
    auto next_1 = StageBuilder< ValueChecker<int> >()
            .OnMessage([](const int& msg){
                sleep(1);
            })
            .Build();
    ValueChecker<int>* next_2 = StageBuilder< ValueChecker<int> >(cnf).Build();
    StageConfig cnf2;
    cnf2.is_ordered = true;
    auto next_3 = StageBuilder< ValueChecker<int> >(cnf2)
            .Build();
    *ent | next_1;
    *ent | next_2;
    group(next_1, next_2) | next_3;
    /*     -(SPSC) next_1 -
     *    /                \
     * ent                  -(MPSC) next_3
     *    \                /
     *     -(SPSC) next_2 -c
     */

    ent->Start();

    ent->Forward(2);//To next_1
    BOOST_REQUIRE(!next_1->isReceived);// in another thread.
    BOOST_REQUIRE(!next_3->isReceived);// next_3 should not receive any message because of multi-thread.
    ent->Forward(1);//To next_2
    BOOST_REQUIRE(next_2->isReceived);

    ent->Stop(); // Waiting For multi-thread works.
    BOOST_REQUIRE(next_1->isReceived);
    BOOST_REQUIRE(next_3->isReceived);
    printf("Size:%lu\n", next_3->values.size());
    BOOST_REQUIRE(next_3->values.size() == 2);
    BOOST_REQUIRE(next_3->values[0] == 2);
    BOOST_REQUIRE(next_3->values[1] == 1);
}

BOOST_AUTO_TEST_CASE(pipeline_variant_idle)
{
    auto ent = Entrance<int>()
            .Build();
    bool idle_invoked = false;
    auto next_1 = StageBuilder< ValueChecker<int> >()
            .OnIdle([&](){idle_invoked = true;})
            .Build();
    *ent | next_1;

    ent->Start();

    sleep(1);

    ent->Stop();

    BOOST_REQUIRE(idle_invoked);
}
