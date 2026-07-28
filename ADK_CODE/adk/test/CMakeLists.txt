PROJECT (test)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

ADD_EXECUTABLE(test_log test_log.cpp test_log.h)
target_link_libraries(test_log adk)

ADD_EXECUTABLE(test_thread_cache_msg test_thread_cache_msg.cpp)
target_link_libraries(test_thread_cache_msg adk)

add_subdirectory(pipeline_variant_benchmark)
add_subdirectory(pipeline_variant_benchmark_2)


ADD_EXECUTABLE(test_thread_sample test_thread_reference.cpp test_thread_impl.cpp test_thread_define.h)

ADD_EXECUTABLE(test_thread_noblock test_thread_noblock.cpp)
target_link_libraries(test_thread_noblock adk)

ADD_EXECUTABLE(test_spsc_queue_latency test_spsc_queue_latency.cpp)
target_link_libraries(test_spsc_queue_latency adk)

#find_package(Boost 1.62.0 REQUIRED)
#if(Boost_FOUND)
#    file(GLOB_RECURSE adk_SRC src/*.cpp)
#    file(GLOB_RECURSE adk_HDR include/*.h)
#    include_directories(${Boost_INCLUDE_DIRS} ../include)
#    ADD_EXECUTABLE(test_log test_log.cpp test_log.h)
#    target_link_libraries(test_log adk)
#endif()
