#ifndef UNITTEST_ZF_VERBS_H_
#define UNITTEST_ZF_VERBS_H_

#include <string>

#include <time.h>
#include <string.h>

#include <boost/format.hpp>
#include <tcp_verbs/tcp_interface.h>

constexpr uint32_t kBufferSize = 1024;
char kBuffer[kBufferSize];

constexpr uint32_t kTestDataSize = sizeof(struct timespec);
constexpr uint16_t kServerPort = 50011;

const std::string kClientEnv("ZF_VERBS_UNITTEST_CLIENT");
const std::string kServerEnv("ZF_VERBS_UNITTEST_SERVER");
#endif
