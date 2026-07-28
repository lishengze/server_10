cmake_minimum_required(VERSION 3.12...3.14)


set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost COMPONENTS unit_test_framework regex)

add_executable(adk_unittest)

set(myboost_regex -Wl,--whole-archive boost_regex.a -Wl,--no-whole-archive)

target_link_libraries(adk_unittest adk ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY} ${myboost_regex})

target_sources(adk_unittest PRIVATE pipeline_variant_test.cpp)

set(CMAKE_CXX_FLAGS "--coverage")


target_include_directories(adk_unittest PRIVATE include)

set(CMAKE_CXX_FLAGS -O3)