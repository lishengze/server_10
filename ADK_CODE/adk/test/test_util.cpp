#include <adk/util.h>

int main(int argc, char const *argv[])
{
    adk::SetCpuAffinity("0, 2, 3-4");
    sleep(1);

    cpu_set_t tmp_cpu_set;
    sched_getaffinity(0, sizeof(cpu_set_t), &tmp_cpu_set);

    assert(CPU_ISSET(0, &tmp_cpu_set));
    assert(!CPU_ISSET(1, &tmp_cpu_set));
    assert(CPU_ISSET(2, &tmp_cpu_set));
    assert(CPU_ISSET(3, &tmp_cpu_set));
    assert(CPU_ISSET(4, &tmp_cpu_set));
    assert(!CPU_ISSET(5, &tmp_cpu_set));
    return 0;
}