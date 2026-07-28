#include <assert.h>

#include <adk/util.h>

int main(int argc, char const *argv[])
{
    cpu_set_t var_cpuset;
    adk::ParseCpuSet("1-2, 10- 12, 8 -8, 9", var_cpuset);
    assert(CPU_ISSET(1, &var_cpuset));
    assert(CPU_ISSET(2, &var_cpuset));
    assert(CPU_ISSET(8, &var_cpuset));
    assert(CPU_ISSET(9, &var_cpuset));
    assert(CPU_ISSET(10, &var_cpuset));
    assert(CPU_ISSET(11, &var_cpuset));
    assert(CPU_ISSET(12, &var_cpuset));

    assert(!CPU_ISSET(0, &var_cpuset));
    assert(!CPU_ISSET(3, &var_cpuset));
    assert(!CPU_ISSET(7, &var_cpuset));
    assert(!CPU_ISSET(13, &var_cpuset));
    return 0;
}
