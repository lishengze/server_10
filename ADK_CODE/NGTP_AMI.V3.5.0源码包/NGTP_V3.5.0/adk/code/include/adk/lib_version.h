/**
 * @file       lib_version.h
 * @brief      an implementation of making .so to print version infomation
 *             of dynamic library to console
 * @author     lijie, lijie@archforce.com.cn
 * @date       2019/03/21
 * @note       make sure that this header file is inculuded once
 */
#ifndef __ADK_LIB_VERSION_H_
#define __ADK_LIB_VERSION_H_

#include <stdio.h>
#include <unistd.h>
#include <string>
#include <iostream>

extern "C"
{

void lib_main(void) __attribute__((noreturn));

extern const char my_interp[] __attribute__((section(".interp"))) = "/lib64/ld-linux-x86-64.so.2";

void lib_main(void)
{
    std::string header_str="Copyright (c) 2019 Archforce Financial Technology.  All rights reserved.\n";

    #ifndef __AMI_COMPILER_VERSION__
    std::string compiler_version = "unknown";
    #else
    std::string compiler_version = __AMI_COMPILER_VERSION__;
    #endif

    #ifndef __AMI_OS_VERSION__
    std::string os_version = "unknown";
    #else
    std::string os_version = __AMI_OS_VERSION__;
    #endif

    #ifndef __AMI_COMPILE_DATE__
    std::string compile_date = "unknown";
    #else
    std::string compile_date = __AMI_COMPILE_DATE__;
    #endif

    #ifndef __AMI_CLIENT_INFO__
    std::string version_detail = "unknown";
    #else
    std::string version_detail = __AMI_CLIENT_INFO__;
    #endif

    std::cout << header_str
              << "Compiler Version : <" << compiler_version << ">\n"
              << "OS Version : <" << os_version << ">\n"
              << "Compiled Date : <" << compile_date << ">\n"
              << "AMI Verion Detail :<" << version_detail << ">"
              << std::endl;


    _exit(0);
}

}

#endif
