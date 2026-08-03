message("+++++++++++++ adk CMake [START] +++++++++++++")
####################### 编译选项的通用性设置 ##########
cmake_minimum_required(VERSION 3.10)
project(adk)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)

# 默认构建类型为 Debug，命令行可覆盖为 Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Debug" FORCE)
endif()
message(STATUS "  Compile Type:  ${CMAKE_BUILD_TYPE}")
message(STATUS "  Platform: "    ${CMAKE_CXX_COMPILER_ID})
message(STATUS "  Gcc Version: " ${CMAKE_CXX_COMPILER_VERSION})

if ( NOT DEFINED BUILD_SUM_COUNT)
    set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS 1 CACHE INTERNAL "Suppress dev warnings")
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    set(CMAKE_C_COMPILER_WORKS TRUE CACHE INTERNAL "C compiler works")
    set(CMAKE_CXX_COMPILER_WORKS TRUE CACHE INTERNAL "CXX compiler works")
    set(BUILD_SUM_COUNT 0)      ## 用于统计整个编译的文件数目;
    message("######## adk 独立编译 ######### ")

    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

    ## boost 库引用相关设置;
    set(Boost_USE_STATIC_LIBS OFF)
    set(Boost_USE_MULTITHREADED ON)
    set(Boost_USE_STATIC_RUNTIME OFF)


    ######## Release 模式下，链接命令不传递 -O3 和 -DNDEBUG ########
    string(REPLACE "-O3" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    string(REPLACE "-O2" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

    ###################### 三方库及头文件目录（统一使用 GTHT_WORK_DEV）######################
    if(NOT DEFINED ENV{GTHT_WORK_DEV})
        message(FATAL_ERROR "GTHT_WORK_DEV environment variable is not set")
    endif()
    set(GTHT_WORK_DEV $ENV{GTHT_WORK_DEV})
    message(STATUS "<adk> GTHT_WORK_DEV is: ${GTHT_WORK_DEV}")

    # 所有编译依赖的三方库目录
    set(THIRD_LIB_DIR ${GTHT_WORK_DEV}/lib)
    # 头文件目录
    set(THIRD_INCLUDE_DIR ${GTHT_WORK_DEV}/include)
    # Boost 头文件目录
    set(BOOST_INCLUDE_DIR ${THIRD_INCLUDE_DIR}/boost_1_62_0)
    set(BOOST_LIBRARY_DIR ${THIRD_LIB_DIR}/boost_1_62_0)

    message(STATUS "<adk> 3rd lib dir is: ${THIRD_LIB_DIR}")
    message(STATUS "<adk> 3rd include dir is: ${THIRD_INCLUDE_DIR}")
    message(STATUS "<adk> Boost include dir is: ${BOOST_INCLUDE_DIR}")
    message(STATUS "<adk> Boost lib dir is: ${BOOST_LIBRARY_DIR}")

endif()

###################### 设置并添加编译依赖的文件目录 ######################
set(ADK_INCLUDE_DIR_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/adk/rdma
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io_engine/tcp_verbs
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd
    ${BOOST_INCLUDE_DIR}
    ${THIRD_INCLUDE_DIR}
)

message(STATUS "  编译依赖的外部头文件或库文件所在目录: --------")
foreach(MY_DIR ${ADK_INCLUDE_DIR_SRC})
    if(NOT EXISTS ${MY_DIR})
         message(FATAL_ERROR "DIR: ${MY_DIR} DOES NOT EXIST")
    endif()
    message(STATUS "  ${MY_DIR}")
    include_directories(${MY_DIR})
endforeach()

set(ADK_LIB_DIR_SRC ${THIRD_LIB_DIR}  ${BOOST_LIBRARY_DIR})

message(STATUS "  编译依赖的外部库文件所在目录: --------")
foreach(MY_DIR ${ADK_LIB_DIR_SRC})
    if(NOT EXISTS ${MY_DIR})
         message(FATAL_ERROR "DIR: ${MY_DIR} DOES NOT EXIST")
    endif()
    message(STATUS "  ${MY_DIR}")
    link_directories(${MY_DIR})
endforeach()

###################### 定义构建变体（variant）对应的宏 ######################
# 对应 Jamroot 中的 variant 定义:
#   adk_debug -> __ADK_DEBUG__
#   adk_test  -> __ADK_TEST__, __ADK_MQM_FAILURE_TEST__, __ADK_MP_FAILURE_TEST__, __ADK_SHMW_FAILURE_TEST__
#   adk_pack  -> __ADK_DEBUG__

set(ADK_VARIANT_DEFINES "" CACHE STRING "Additional compile definitions for adk variant")
if(ADK_VARIANT_DEFINES)
    add_definitions(${ADK_VARIANT_DEFINES})
    message(STATUS "  ADK Variant defines: ${ADK_VARIANT_DEFINES}")
endif()

###################### 编译 entry_wrapper 库 ######################
# Jamroot 中 entry_wrapper 始终 debug 模式: -O0 -ggdb
# 由 src/entry_wrapper.cpp 和 src_pack/entry_wrapper_pack.cpp 组成
set(ENTRY_WRAPPER_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/entry_wrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src_pack/entry_wrapper_pack.cpp
)

add_library(entry_wrapper SHARED ${ENTRY_WRAPPER_SRC})
target_compile_options(entry_wrapper PRIVATE -O0 -ggdb)
target_include_directories(entry_wrapper PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(entry_wrapper PUBLIC
    boost_log
    boost_log_setup
    boost_locale
    boost_filesystem
    boost_program_options
    boost_date_time
    boost_thread
    boost_regex
)

###################### 收集 adk 库源文件 ######################
# Jamroot: [ path.glob-tree $(CWD)/src : *.cpp : entry_wrapper.cpp exanic_cap.cpp ]
# 排除 entry_wrapper.cpp 和 exanic_cap.cpp（exanic_cap.cpp 不存在则忽略）
file(GLOB_RECURSE ADK_SRC_TMP "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM ADK_SRC_TMP
    ${CMAKE_CURRENT_SOURCE_DIR}/src/entry_wrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/exanic_cap.cpp
)

# Jamroot: [ path.glob-tree $(CWD)/src_pack : *.cpp : entry_wrapper_pack.cpp ]
file(GLOB_RECURSE ADK_SRC_PACK_TMP "${CMAKE_CURRENT_SOURCE_DIR}/src_pack/*.cpp")
list(REMOVE_ITEM ADK_SRC_PACK_TMP
    ${CMAKE_CURRENT_SOURCE_DIR}/src_pack/entry_wrapper_pack.cpp
)

###################### 编译 adk 库 ######################
# Jamroot 中 adk 是 shared library（通过 <link>shared:<linkflags> 暗示）
# 但同时也支持 static 链接。这里构建 shared 库以匹配 Jamroot 的默认行为
add_library(adk SHARED
    ${ADK_SRC_TMP}
    ${ADK_SRC_PACK_TMP}
)

target_include_directories(adk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/adk/rdma
)
target_include_directories(adk PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io_engine/tcp_verbs
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd
)

# Jamroot 中 -Dadk=adk_impl 定义在 usage-requirements（第5个冒号后）
# 即只在其他目标链接 adk 时才生效，编译 adk 本身时不定义此宏
# 这里使用 INTERFACE 来匹配 Jamroot 的行为
target_compile_definitions(adk INTERFACE adk=adk_impl)

target_compile_options(adk PRIVATE
    -std=c++11
    -ftemplate-depth=256
)

# 显式声明构建顺序依赖：adk 依赖 entry_wrapper 先编译完成
add_dependencies(adk entry_wrapper)

# 链接 entry_wrapper 和 Boost 依赖
target_link_libraries(adk PRIVATE
    entry_wrapper
    boost_log
    boost_log_setup
    boost_locale
    boost_filesystem
    boost_program_options
    boost_date_time
    boost_thread
    boost_regex
    dl
    z
)

# 如果设置了 AMI_LINK_FLAG 环境变量，则追加到链接选项
if(DEFINED ENV{AMI_LINK_FLAG} AND NOT "$ENV{AMI_LINK_FLAG}" STREQUAL "")
    target_link_options(adk PRIVATE $ENV{AMI_LINK_FLAG})
    message(STATUS "  AMI_LINK_FLAG: $ENV{AMI_LINK_FLAG}")
endif()

## Release模式  #########
if (DEFINED CMAKE_BUILD_TYPE AND CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(adk PRIVATE
        -Wall
        -finline-functions
        -pthread
    )

else() # Debug模式  #########
    target_compile_options(adk PRIVATE
        -fno-inline
        -g
        -O0
        -Wall
        -pthread
    )

endif()

###################### 设置安装目录并安装 ######################
# 生成的动态链接库安装到 $ENV{GTHT_WORK_DEV}/lib
set(ADK_LIB_INSTALL_DIR "${THIRD_LIB_DIR}/ami")

message("------- 安装的目录信息 ---------")
set(ADK_INSTALL_DIR_SRC ${ADK_LIB_INSTALL_DIR})

foreach(MY_DIR ${ADK_INSTALL_DIR_SRC})
    if(NOT EXISTS ${MY_DIR})
        file(MAKE_DIRECTORY ${MY_DIR})
        message(STATUS "Created directory: ${MY_DIR}")
        if(NOT EXISTS ${MY_DIR})
            message(FATAL_ERROR " INSTALL DIR CREATION FAILED: ${MY_DIR}")
        endif()
    endif()
    message(STATUS "  ${MY_DIR}")
endforeach()

install(TARGETS adk LIBRARY DESTINATION ${ADK_LIB_INSTALL_DIR})
install(TARGETS entry_wrapper LIBRARY DESTINATION ${ADK_LIB_INSTALL_DIR})

######################  统计编译文件数目  ######################
set(ALL_SRC_FILES ${ADK_SRC_TMP} ${ADK_SRC_PACK_TMP} ${ENTRY_WRAPPER_SRC})
foreach(FILE ${ALL_SRC_FILES})
    get_filename_component(PARENT_DIR ${FILE} DIRECTORY)
    set(DIR_STATS "${DIR_STATS};${PARENT_DIR}")
endforeach()

list(SORT DIR_STATS)
list(REMOVE_DUPLICATES DIR_STATS)

message(STATUS "按目录统计的编译的 CPP 文件数目:")
foreach(DIR ${DIR_STATS})
    file(GLOB DIR_FILES "${DIR}/*.cpp")
    list(LENGTH DIR_FILES DIR_COUNT)
    message(STATUS "  ${DIR}: ${DIR_COUNT} 个文件")
endforeach()
list(LENGTH ALL_SRC_FILES SOURCE_COUNT)
message(STATUS "${PROJECT_NAME} 合计编译的 CPP 文件数目: ${SOURCE_COUNT}")
if ( DEFINED BUILD_SUM_COUNT)
    math(EXPR BUILD_SUM_COUNT "${BUILD_SUM_COUNT} + ${SOURCE_COUNT}")
    set(BUILD_SUM_COUNT ${BUILD_SUM_COUNT} PARENT_SCOPE)
endif()

message("**********************  adk CMake [END] ************************")
