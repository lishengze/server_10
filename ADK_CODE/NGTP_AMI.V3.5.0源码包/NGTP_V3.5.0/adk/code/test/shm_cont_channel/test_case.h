#include "rte_memcpy.h"

#include <boost/format.hpp>
#include <boost/date_time.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#define shm_memory rte_memcpy

struct Protocol
{
    uint32_t message_size;
    uint64_t message_sqn;
};