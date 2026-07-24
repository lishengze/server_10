# 源码包使用文档
## 源码包内容介绍
NGTP_V3.5.0.tar.gz 解压后得到如下目录：
``` c++
NGTP_V3.5.0
|-- aaf
|   |-- README.md
|   |-- code
|   |   |-- Jamroot
|   |   |-- example   // 示例程序
|   |   |-- include   // aaf头文件
|   |   |-- script
|   |   |-- sharding    // 多分片源码
|   |   |-- src    // aaf框架源码
|   |   |-- test    // 测试用 demo
|   |   `-- tools    // 工具类源码
|   |       |-- Jamfile.v2
|   |       |-- ami_bridge   // ami_bridge源码
|   |       |-- ami_record_tool    // ami_recorder 工具类源码，不可编译
|   |       `-- ami_recorder    // // ami_recorder 源码，不可编译
|   `-- doc
`-- adk
    `-- code
        |-- 3rd    // 三方库
        |-- CMakeLists.txt
        |-- Jamroot
        |-- benchmark_test
        |-- example    // 示例程序
        |-- include
        |   |-- adk    // adk 头文件，给内部开发使用
        |   `-- adk_pack    // adk 封装头文件，给外部开发使用
        |-- src    // include/adk 目录下头文件的实现
        |-- src_pack    // include/adk_pack 目录下头文件的实现
        |-- test    // include/adk 接口的测试用 demo
        |-- test_pack    // include/adk_pack 接口的测试用 demo
        |-- tools
        |-- unittest     // include/adk 接口的单元测试
        `-- unittest_pack    // include/adk_pack 接口的单元测试
```

## 环境准备
### AMI 安装包
1. 检查环境变量是否存在 AMI_HOME 环境变量；如果存在，则不需要执行步骤2
```shell
env | grep AMI_HOME  ## 检查是否设置AMI环境
```
2. 不存在 AMI_HOME 环境变量时，按下面步骤设置
```shell
cd ~/AMI/script/  ## 进入AMI安装包的script目录
. setup_env.sh  ## 设置AMI环境变量
```

### BOOST 编译环境
1. 检查是否设置 BOOST 编译环境；如果未设置，需用户自行设置。<b>推荐的BOOST版本：boost_1_62_0</b>
```shell
env | grep BOOST_ROOT    ## 检查是否设置boost环境
```

## 编译源码
### 解压源码包
```shell
tar -zxvf NGTP_V3.5.0.tar.gz
```
### 编译 adk 源码
```shell
cd NGTP_V3.5.0/adk/code/
b2 -j20    ## boost 编译命令
```

### 编译 aaf 源码
```shell
cd NGTP_V3.5.0/aaf/code/
b2 -j20    ## boost 编译命令
```

### 编译 ami_bridge
```shell
cd NGTP_V3.5.0/aaf/code/tools/ami_bridge/
b2 -j20    ## boost 编译命令
```

### 编译 aaf 文件夹下工具
1. 编译工具
    - ami_transmitter 为 发送消息 工具
    - ami_receiver 为 接收消息 工具
```shell
cd NGTP_V3.5.0/aaf/code/tools/
b2 -j20    ## boost 编译命令
```

## 使 aaf 和 adk 源码库生效

某些boost版本下，会出现源码编译的demo或者工具 <b>使用AMI安装包库</b> 的问题。可以通过调整 AMI 安装包结构 或者 直接替换 aaf 和 adk 库规避

### 查看 ami_bridge 的库依赖
```shell
ldd ./bin/gcc-4.8.5/release/threading-multi/ami_bridge
        linux-vdso.so.1 =>  (0x00007ffd86bf9000)
        libaaf.so => /root/AMI/lib64/libaaf.so (0x00007fa286b08000)    ## 应使用aaf源码库
        libadk.so => /root/AMI/lib64/libadk.so (0x00007fa2860f2000)    ## 应使用adk源码库
        libentry_wrapper.so => /root/AMI/lib64/libentry_wrapper.so (0x00007fa285ecc000)    ## 应使用adk源码库
        libboost_program_options.so.1.62.0 => /root/AMI/lib64/libboost_program_options.so.1.62.0 (0x00007fa285c5c000)    ## 可使用环境安装的 boost 库
        libboost_locale.so.1.62.0 => /root/AMI/lib64/libboost_locale.so.1.62.0 (0x00007fa2859c1000)    ## 可使用环境安装的 boost 库
        libboost_log_setup.so.1.62.0 => /root/AMI/lib64/libboost_log_setup.so.1.62.0 (0x00007fa285722000)    ## 可使用环境安装的 boost 库
        libboost_log.so.1.62.0 => /root/AMI/lib64/libboost_log.so.1.62.0 (0x00007fa285467000)    ## 可使用环境安装的 boost 库
        libboost_chrono.so.1.62.0 => /root/AMI/lib64/libboost_chrono.so.1.62.0 (0x00007fa285260000)    ## 可使用环境安装的 boost 库
        libboost_thread.so.1.62.0 => /root/AMI/lib64/libboost_thread.so.1.62.0 (0x00007fa28503f000)    ## 可使用环境安装的 boost 库
        libboost_regex.so.1.62.0 => /root/AMI/lib64/libboost_regex.so.1.62.0 (0x00007fa284d4d000)    ## 可使用环境安装的 boost 库
        libboost_filesystem.so.1.62.0 => /root/AMI/lib64/libboost_filesystem.so.1.62.0 (0x00007fa284b35000)    ## 可使用环境安装的 boost 库
        libboost_system.so.1.62.0 => /root/AMI/lib64/libboost_system.so.1.62.0 (0x00007fa284931000)    ## 可使用环境安装的 boost 库
        libboost_date_time.so.1.62.0 => /root/AMI/lib64/libboost_date_time.so.1.62.0 (0x00007fa284720000)    ## 可使用环境安装的 boost 库
        libami.so => /root/AMI/lib64/libami.so (0x00007fa283865000)    ## 应使用安装包库
        librt.so.1 => /lib64/librt.so.1 (0x00007fa28365d000)    ## 系统库
        libz.so.1 => /root/AMI/lib64/libz.so.1 (0x00007fa283447000)    ## 系统库
        libdl.so.2 => /lib64/libdl.so.2 (0x00007fa283243000)    ## 系统库
        libstdc++.so.6 => /lib64/libstdc++.so.6 (0x00007fa282f3b000)    ## 系统库
        libm.so.6 => /lib64/libm.so.6 (0x00007fa282c39000)    ## 系统库
        libgcc_s.so.1 => /lib64/libgcc_s.so.1 (0x00007fa282a23000)    ## 系统库
        libpthread.so.0 => /lib64/libpthread.so.0 (0x00007fa282807000)    ## 系统库
        libc.so.6 => /lib64/libc.so.6 (0x00007fa282439000)    ## 系统库
        /lib64/ld-linux-x86-64.so.2 (0x00007fa286e6a000)   ## 系统库
        libboost_atomic.so.1.62.0 => /root/AMI/lib64/libboost_atomic.so.1.62.0 (0x00007fa282237000)    ## 可使用环境安装的 boost 库
        libazeroth.so => /root/AMI/lib64/libazeroth.so (0x00007fa281e40000)    ## 应使用安装包库
        libetcd_client.so => /root/AMI/lib64/libetcd_client.so (0x00007fa281bf9000)    ## 应使用安装包库
        libetcd.so => /root/AMI/lib64/libetcd.so (0x00007fa2817d2000)    ## 应使用安装包库
        libprotobuf.so.12 => /root/AMI/lib64/libprotobuf.so.12 (0x00007fa28104f000)    ## 应使用安装包库
        libgrpc.so.3 => /root/AMI/lib64/libgrpc.so.3 (0x00007fa280c64000)    ## 应使用安装包库
        libgrpc++.so.3 => /root/AMI/lib64/libgrpc++.so.3 (0x00007fa280a10000)    ## 应使用安装包库
```
### 方案一：调整 AMI 安装包结构 使用 源码编译库
1. AMI安装包下新建 lib 文件夹，将 adk 和 aaf 相关库 移动至 lib 下
```shell
cd AMI
mkdir lib
mv ./lib64/libadk.so ./lib64/libaaf.so ./lib64/libentry_wrapper.so lib/
```
2. 为保证安装包能正常使用，将 lib 目录设置到 LD_LIBRARY_PATH 环境变量下
```shell
cd AMI/lib
LD_LIBRARY_PATH=`pwd`:$LD_LIBRARY_PATH
```
3. 查看源码库中 ami_bridge 依赖
```shell
cd NGTP_V3.5.0/aaf/code/tools/ami_bridge/
ldd ./bin/gcc-4.8.5/release/threading-multi/ami_bridge
        linux-vdso.so.1 =>  (0x00007ffc16daf000)
        libaaf.so => /root/NGTP_V3.5.0/aaf/code/bin/gcc-4.8.5/release/threading-multi/libaaf.so (0x00007f9a319ce000)    ## 已为源码库
        libadk.so => /root/NGTP_V3.5.0/adk/code/bin/gcc-4.8.5/release/threading-multi/libadk.so (0x00007f9a31042000)    ## 已为源码库
        libentry_wrapper.so => /root/NGTP_V3.5.0/adk/code/bin/gcc-4.8.5/release/threading-multi/libentry_wrapper.so (0x00007f9a30e1c000)    ## 已为源码库
        libboost_program_options.so.1.62.0 => /root/AMI/lib64/libboost_program_options.so.1.62.0 (0x00007f9a30bac000)
        libboost_locale.so.1.62.0 => /root/AMI/lib64/libboost_locale.so.1.62.0 (0x00007f9a30911000)
        libboost_log_setup.so.1.62.0 => /root/AMI/lib64/libboost_log_setup.so.1.62.0 (0x00007f9a30672000)
        libboost_log.so.1.62.0 => /root/AMI/lib64/libboost_log.so.1.62.0 (0x00007f9a303b7000)
        libboost_chrono.so.1.62.0 => /root/AMI/lib64/libboost_chrono.so.1.62.0 (0x00007f9a301b0000)
        libboost_thread.so.1.62.0 => /root/AMI/lib64/libboost_thread.so.1.62.0 (0x00007f9a2ff8f000)
        libboost_regex.so.1.62.0 => /root/AMI/lib64/libboost_regex.so.1.62.0 (0x00007f9a2fc9d000)
        libboost_filesystem.so.1.62.0 => /root/AMI/lib64/libboost_filesystem.so.1.62.0 (0x00007f9a2fa85000)
        libboost_system.so.1.62.0 => /root/AMI/lib64/libboost_system.so.1.62.0 (0x00007f9a2f881000)
        libboost_date_time.so.1.62.0 => /root/AMI/lib64/libboost_date_time.so.1.62.0 (0x00007f9a2f670000)
        libami.so => /root/AMI/lib64/libami.so (0x00007f9a2e7b5000)
        librt.so.1 => /lib64/librt.so.1 (0x00007f9a2e5ad000)
        libz.so.1 => /root/AMI/lib64/libz.so.1 (0x00007f9a2e397000)
        libdl.so.2 => /lib64/libdl.so.2 (0x00007f9a2e193000)
        libstdc++.so.6 => /lib64/libstdc++.so.6 (0x00007f9a2de8b000)
        libm.so.6 => /lib64/libm.so.6 (0x00007f9a2db89000)
        libgcc_s.so.1 => /lib64/libgcc_s.so.1 (0x00007f9a2d973000)
        libpthread.so.0 => /lib64/libpthread.so.0 (0x00007f9a2d757000)
        libc.so.6 => /lib64/libc.so.6 (0x00007f9a2d389000)
        /lib64/ld-linux-x86-64.so.2 (0x00007f9a31d25000)
        libboost_atomic.so.1.62.0 => /root/AMI/lib64/libboost_atomic.so.1.62.0 (0x00007f9a2d187000)
        libazeroth.so => /root/AMI/lib64/libazeroth.so (0x00007f9a2cd90000)
        libetcd_client.so => /root/AMI/lib64/libetcd_client.so (0x00007f9a2cb49000)
        libetcd.so => /root/AMI/lib64/libetcd.so (0x00007f9a2c722000)
        libprotobuf.so.12 => /root/AMI/lib64/libprotobuf.so.12 (0x00007f9a2bf9f000)
        libgrpc.so.3 => /root/AMI/lib64/libgrpc.so.3 (0x00007f9a2bbb4000)
        libgrpc++.so.3 => /root/AMI/lib64/libgrpc++.so.3 (0x00007f9a2b960000)
```
### 方案二：源码编译库 直接 替换 AMI 安装包库
源码库：
1. NGTP_V3.5.0/adk 文件夹下 libentry_wrapper.so 
2. NGTP_V3.5.0/adk 文件夹下 libadk.so 
3. NGTP_V3.5.0/aaf 文件夹下 libaaf.so 

AMI安装包 库位置：AMI/lib64/