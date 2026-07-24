#include <adk/monitor/indicator_writer.h>

using namespace adk;

int main(int argc, char const *argv[])
{
    IndicatorWriter ind_wt;
    ind_wt.Init("./abc/", "test");
    boost::property_tree::ptree ptree;
    ptree.put("abc", "test");
    while (1)
    {
        ind_wt.Write("abc", "test", ptree);
        sleep(1);
        ind_wt.ClearIndicatorFiles(0);
    }
    
    return 0;
}

