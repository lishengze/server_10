#ifndef RDMA_TEST_CASE_H_
#define RDMA_TEST_CASE_H_

#include <boost/format.hpp>
#include <boost/date_time.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <adk/rdma/rdma_exp.h>

using RdmaContext = adk::rdma::Context;
using RdmaMcEndpoint = adk::rdma::McEndpoint;
using RdmaUcEndpoint = adk::rdma::UcEndpoint;

using RdmaDH = adk::rdma::DestHandler;

using adk::rdma::TxNodeEntry;

#define PING_PONG

#endif
