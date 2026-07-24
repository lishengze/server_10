#!/bin/bash

CWD=`pwd`

lib_path=${CWD}/../../lib64
header_path=${CWD}/../../include
g++ -O0 -fno-inline -Wall -g -pthread -fPIC -std=c++11 tcp_client.cpp -c -I${header_path}
"g++"  -Wl,-rpath-link -Wl,"${lib_path}/"          \
 -o "tcp_client" -Wl,--start-group "tcp_client.o"  \
  "${lib_path}/libadk.so"                          \
  "${lib_path}/libentry_wrapper.so"                \
  "${lib_path}/libz.so"                            \
  "${lib_path}/libboost_program_options.so.1.62.0" \
  "${lib_path}/libboost_locale.so.1.62.0"          \
  "${lib_path}/libboost_log.so.1.62.0"             \
  "${lib_path}/libboost_thread.so.1.62.0"          \
  "${lib_path}/libboost_regex.so.1.62.0"           \
  "${lib_path}/libboost_filesystem.so.1.62.0"      \
  "${lib_path}/libboost_system.so.1.62.0"          \
  "${lib_path}/libboost_date_time.so.1.62.0"       \
  "${lib_path}/libboost_atomic.so.1.62.0"          \
   -Wl,-Bstatic  -Wl,-Bdynamic -lrt -Wl,--end-group -g -pthread
rm tcp_client.o

g++ -O0 -fno-inline -Wall -g -pthread -fPIC -std=c++11 tcp_server.cpp -c -I${header_path}
"g++"  -Wl,-rpath-link -Wl,"${lib_path}/"          \
 -o "tcp_server" -Wl,--start-group "tcp_server.o"  \
  "${lib_path}/libadk.so"                          \
  "${lib_path}/libentry_wrapper.so"                \
  "${lib_path}/libz.so"                            \
  "${lib_path}/libboost_program_options.so.1.62.0" \
  "${lib_path}/libboost_locale.so.1.62.0"          \
  "${lib_path}/libboost_log.so.1.62.0"             \
  "${lib_path}/libboost_thread.so.1.62.0"          \
  "${lib_path}/libboost_regex.so.1.62.0"           \
  "${lib_path}/libboost_filesystem.so.1.62.0"      \
  "${lib_path}/libboost_system.so.1.62.0"          \
  "${lib_path}/libboost_date_time.so.1.62.0"       \
  "${lib_path}/libboost_atomic.so.1.62.0"          \
   -Wl,-Bstatic  -Wl,-Bdynamic -lrt -Wl,--end-group -g -pthread
rm tcp_server.o
