message("+++++++++++++ aaf CMake [START] +++++++++++++")
####################### 编译选项的通用性设置 ##########
cmake_minimum_required(VERSION 3.10)
project(aaf)

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
    message("######## aaf 独立编译 ######### ")

    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

    ## boost 库引用相关设置;
    set(Boost_USE_STATIC_LIBS OFF)
    set(Boost_USE_MULTITHREADED ON)
    set(Boost_USE_STATIC_RUNTIME OFF)

    ######## GCC > 5 时使用旧 ABI，与 libami.so/libaaf.so 保持一致 ########
    if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 5.0)
        add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)
    endif ()
        

    ######## 编译选项 ---保留足够的debug信息  ########
    set(Release_Debug_Symbol OFF CACHE  BOOL  "STORE DEBUG SYMBOLS" )
    if (Release_Debug_Symbol)
        message(" ********  Define RelWithDebInfo  ******** ${Release_Debug_Symbol} ")
        set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
    endif()

    ######## Release 模式下，链接命令不传递 -O3 和 -DNDEBUG ########
    string(REPLACE "-O3" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    string(REPLACE "-O2" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

    ###################### 三方库及头文件目录（统一使用 GTHT_WORK_DEV）######################
    if(NOT DEFINED ENV{GTHT_WORK_DEV})
        message(FATAL_ERROR "GTHT_WORK_DEV environment variable is not set")
    endif()
    set(GTHT_WORK_DEV $ENV{GTHT_WORK_DEV})
    message(STATUS "<aaf> GTHT_WORK_DEV is: ${GTHT_WORK_DEV}")

    # 所有编译依赖的三方库目录
    set(THIRD_LIB_DIR ${GTHT_WORK_DEV}/lib)
    set(AMI_LIB_DIR ${THIRD_LIB_DIR}/ami)
    set(BOOST_LIB_DIR ${THIRD_LIB_DIR}/boost_1_62_0)

    # 头文件目录
    set(THIRD_INCLUDE_DIR ${GTHT_WORK_DEV}/include)
    set(AMI_INCLUDE_DIR ${GTHT_WORK_DEV}/include/AMI)

    # Boost 头文件目录
    set(BOOST_INCLUDE_DIR ${THIRD_INCLUDE_DIR}/boost_1_62_0)
    message(STATUS "<aaf> 3rd lib dir is: ${THIRD_LIB_DIR}")
    message(STATUS "<aaf> 3rd include dir is: ${THIRD_INCLUDE_DIR}")
    message(STATUS "<aaf> Boost include dir is: ${BOOST_INCLUDE_DIR}")

    get_filename_component(AAF_DIR "${CMAKE_CURRENT_SOURCE_DIR}" PATH)
    get_filename_component(ADK_CODE_DIR "${AAF_DIR}" PATH)
    # get_filename_component(ADK_CODE_DIR "${AAF_DIR}" PATH)
    # message(STATUS "<aaf> AAF_CODE_DIR is: ${AAF_CODE_DIR}")
    message(STATUS "<aaf> AAF_DIR is: ${AAF_DIR}")
    message(STATUS "<aaf> ADK_CODE_DIR is: ${ADK_CODE_DIR}")

endif()

###################### 设置并添加编译依赖的文件目录 ######################
# aaf 项目自身头文件目录
set(AAF_INCLUDE_DIR_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ADK_CODE_DIR}/adk/include
    ${BOOST_INCLUDE_DIR}
    ${THIRD_INCLUDE_DIR}
    ${AMI_INCLUDE_DIR}

)

message(STATUS "  编译依赖的外部头文件或库文件所在目录: --------")
foreach(MY_DIR ${AAF_INCLUDE_DIR_SRC})
    if(NOT EXISTS ${MY_DIR})
         message(FATAL_ERROR "DIR: ${MY_DIR} DOES NOT EXIST")
    endif()
    message(STATUS "  ${MY_DIR}")
    include_directories(${MY_DIR})
endforeach()

set(AAF_LIB_DIR_SRC
    ${AMI_LIB_DIR}
    ${BOOST_LIB_DIR}
    ${THIRD_LIB_DIR}
)

message(STATUS "  编译依赖的外部库文件所在目录: --------")
foreach(MY_DIR ${AAF_LIB_DIR_SRC})
    if(NOT EXISTS ${MY_DIR})
         message(FATAL_ERROR "DIR: ${MY_DIR} DOES NOT EXIST")
    endif()
    message(STATUS "  ${MY_DIR}")
    link_directories(${MY_DIR})
endforeach()

###################### 定义构建变体（variant）对应的宏 ######################
# Jamroot 中 ADK_LOG_USE_BOOST_LOG 既在 requirements（编译自身时生效）
# 也在 usage-requirements（其他目标链接 aaf 时生效）
add_definitions(-DADK_LOG_USE_BOOST_LOG)

# Jamroot 中 aaf 通过 <library>/adk//adk 引用 adk 库，
# adk 的 usage-requirements 包含 <define>adk=adk_impl
# 独立编译时需显式定义此宏，使 adk::ErrorCode 等代码正确解析为 adk_impl::ErrorCode
add_definitions(-Dadk=adk_impl)

###################### 收集 aaf 库源文件 ######################
# Jamroot: [ path.glob-tree $(CWD)/src/util : *.cpp ]
#          [ path.glob-tree $(CWD)/src : *.cpp ]
#          [ path.glob-tree $(CWD)/sharding/ : *.cpp ]
# 注意：src/util 是 src 的子目录，glob-tree src/*.cpp 已经包含了 src/util/*.cpp
# 但 Jamroot 中分开写是为了明确意图，我们保持分开收集以对应
file(GLOB_RECURSE AAF_SRC_UTIL   "${CMAKE_CURRENT_SOURCE_DIR}/src/util/*.cpp")
file(GLOB_RECURSE AAF_SRC_MAIN   "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE AAF_SRC_SHARD  "${CMAKE_CURRENT_SOURCE_DIR}/sharding/*.cpp")

# 合并去重（src/util 下的文件会被 src/*.cpp 重复收集，去重即可）
list(APPEND AAF_SRC_ALL ${AAF_SRC_UTIL} ${AAF_SRC_MAIN} ${AAF_SRC_SHARD})
list(REMOVE_DUPLICATES AAF_SRC_ALL)

###################### 编译 aaf 库 ######################
# Jamroot 中 aaf 是 shared library（通过 <library>dependencies 和 <library>ami 暗示动态链接）
add_library(aaf SHARED ${AAF_SRC_ALL})

target_include_directories(aaf PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(aaf PRIVATE
    ${ADK_CODE_DIR}/adk/include
)

target_compile_options(aaf PRIVATE
    -std=c++11
)

# Jamroot 中 ADK_LOG_USE_BOOST_LOG 在 usage-requirements 中
# 使用 INTERFACE 让链接 aaf 的目标也继承此宏
target_compile_definitions(aaf INTERFACE ADK_LOG_USE_BOOST_LOG)

# 链接依赖：Boost 库 + adk + ami
target_link_libraries(aaf PRIVATE
    boost_log
    boost_log_setup
    boost_locale
    boost_thread
    boost_filesystem
    boost_program_options
    boost_regex
    adk
    ami
)

## Release模式  #########
if (DEFINED CMAKE_BUILD_TYPE AND CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(aaf PRIVATE
        -Wall
        -finline-functions
        -pthread
    )

else() # Debug模式  #########
    target_compile_options(aaf PRIVATE
        -fno-inline
        -g
        -O0
        -Wall
        -pthread
    )

endif()

###################### 设置安装目录并安装 ######################
# 生成的动态链接库安装到 $ENV{GTHT_WORK_DEV}/lib
set(AAF_LIB_INSTALL_DIR "${AMI_LIB_DIR}")

message("------- 安装的目录信息 ---------")
set(AAF_INSTALL_DIR_SRC ${AAF_LIB_INSTALL_DIR})

foreach(MY_DIR ${AAF_INSTALL_DIR_SRC})
    if(NOT EXISTS ${MY_DIR})
        file(MAKE_DIRECTORY ${MY_DIR})
        message(STATUS "Created directory: ${MY_DIR}")
        if(NOT EXISTS ${MY_DIR})
            message(FATAL_ERROR " INSTALL DIR CREATION FAILED: ${MY_DIR}")
        endif()
    endif()
    message(STATUS "  ${MY_DIR}")
endforeach()

install(TARGETS aaf LIBRARY DESTINATION ${AAF_LIB_INSTALL_DIR})

######################  统计编译文件数目  ######################
set(ALL_SRC_FILES ${AAF_SRC_ALL})
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

message("**********************  aaf CMake [END] ************************")
