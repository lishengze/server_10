#ifndef ADK_TEST_ADK_LOG_H_
#define ADK_TEST_ADK_LOG_H_

#include <boost/utility/identity_type.hpp>
#include <adk/log.h>

namespace Test
{

class TestLog
{
private:
	ADK_LOG_DECLARE_AC(10000);

public:
	TestLog()
	{}

	~TestLog()
	{}
	
	void LogInfo();
};

template <typename T1, typename T2>
class TemplateTest
{
private:
    ADK_LOG_DECLARE_AC(20000);

    T1 t;

public:
    TemplateTest()
    {
        ADK_LOG_INFO_AC("Info Log", "test");
    }
};

template <typename T1, typename T2>
ADK_LOG_DEFINE_TMPL(TemplateTest, T1, T2);

} // TestLog

#endif // ADK_TEST_ADK_LOG_H_
