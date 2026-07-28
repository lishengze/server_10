#include "test_log.h"
#include <ostream>
#include <boost/locale/format.hpp>
#include <boost/thread.hpp>

namespace Test
{

template <typename T, int n>
class MyLogger : public adk::log::Logger
{

};

using boost::locale::format;

ADK_LOG_DEFINE(Test::TestLog);

void TestLog::LogInfo()
{
    uint32_t i = 0;
    uint32_t j = 0;
    ADK_LOG_INFO_AC("Info Log", (format("Information: this is a test, i = {1}, j = {2}") % i % j).str());

    [](int a) {
        ADK_LOG_INFO_AC_TF("Info Log", "Log in lambda: {1}", a);
    }(1);
}

} // TestLog

ADK_LOG_LOCAL_AC("Main", 10100);

void LogWarning()
{
    ADK_LOG_WARN_AC("Warn Log", "Warning log in new thread");
    ADK_LOG_INFO_AC("Info Log", "Info log in new thread");
}

struct S
{
    int a;
    int b;
};

std::ostream& operator<<(std::ostream& os, const S& t)
{
    os << "{" << t.a << "," << t.b << "}";
    return os;
}

int main(int argc, char const *argv[])
{
    ADK_LOG_INFO_AC("Info Log", "Before ADK_LOG_INIT");
    ADK_LOG_TRACE_AC("Trace Log", "Before ADK_LOG_INIT");

    ADK_LOG_SET_THRESHOLD(ADK_LOG_LEVEL_DEBUG);
    ADK_LOG_INFO_AC("Info Log", "Before ADK_LOG_INIT");
    ADK_LOG_TRACE_AC("Trace Log", "Before ADK_LOG_INIT");

    S s = {1, 2};
    ADK_LOG_INFO_AC_TF("Info Log", "Struct t: {1}", s);

    ADK_LOG_INIT("./", "TestApp", true, false, false, true, false, 1000);
    ADK_LOG_INFO_AC("Info Log", "After ADK_LOG_INIT");

    if (fork() == 0)
    {
        ADK_LOG_FORK();
    }
    ADK_LOG_INFO_AC("Info Log", "After fork");

    Test::TestLog test_log;
    test_log.LogInfo();

    boost::thread *thread = new boost::thread(LogWarning);

    for (int i = 0; i < 5; ++i)
    {
        ADK_LOG_FATAL_AC("Fatal Log", "Fatal log");
    }

    std::string str = ADK_TRANSLATE("Debug log with 1 parameter: {1}.");
    ADK_LOG_TRACE_AC_TF("Trace Log", "Trace log with 0 parameter.");
    ADK_LOG_DEBUG_AC("Debug Log", str);
    ADK_LOG_DEBUG_AC_TF("Debug Log", "Debug log with 2 parameter: {1}, {2}.", 1);
    ADK_LOG_INFO_TF(20000, "Info Log", "Info log with 2 parameter: {1}, {2}.", 1, "Para2");
    ADK_LOG_WARN_TF(30000, "Warn Log", "Warning log with 7 parameter: {1}, {2}, {3}, {4}, {5}, {6}, {7}.",
                1, 2, 3, 4, 5, 6, 7);
    ADK_LOG_ERROR_AC_TF("Error Log", "Error log with 10 parameter: {1}, {2}, {3}, {4}, {5}, {6}, {7}, {8},"
                    "{9}, {10}.",
                    1, 2.01, 3, 4, 5, 6, true, (void *)8, 9,"10");

    thread->join();

    Test::TemplateTest<int, int> t;

    adk::log::IntervalLogger logger;
    ADK_INV_LOG_WARN_AC_TF(logger, "Warn Log", "hello, {1}", "world");
    ADK_INV_LOG_INFO_TF(logger, 1, "Info Log", "hello, {1}", "world");
    ADK_INV_LOG_DEBUG_AC(logger, "Debug Log", "hello, world");
    ADK_INV_LOG_TRACE(logger, 1, "Trace Log", "hello, world");

    sleep(2);
    ADK_INV_LOG_WARN_AC_TF(logger, "Warn Log", "hello, {1}", "world");
    ADK_INV_LOG_INFO_TF(logger, 1, "Info Log", "hello, {1}", "world");
    ADK_INV_LOG_DEBUG_AC(logger, "Debug Log", "hello, world");
    ADK_INV_LOG_TRACE(logger, 1, "Trace Log", "hello, world");

    ADK_LOG_FINISH();

    return 0;
}
