PROJECT (test)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

ADD_EXECUTABLE(pipeline_variant_benchmark_2 latency.cpp)
target_link_libraries(pipeline_variant_benchmark_2 adk)
