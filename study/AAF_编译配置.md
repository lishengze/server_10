# AAF (AMI Application Framework) 编译配置指南

## 1. 概述

本文档详细说明如何在 Ubuntu 22.04 或 Ubuntu 24.04 系统上编译 AAF 工程。包括环境搭建、依赖安装、编译步骤和常见问题解决。

---

## 2. 系统要求

### 2.1 操作系统版本

| 操作系统 | 推荐版本 | 内核版本 | 状态 |
|---------|---------|---------|------|
| Ubuntu 22.04 LTS | 22.04.x | 5.15+ | ✅ 推荐 |
| Ubuntu 24.04 LTS | 24.04.x | 6.5+ | ✅ 支持 |

### 2.2 硬件要求

- **CPU**: 4 核以上 (推荐 8 核)
- **内存**: 8GB 以上 (推荐 16GB)
- **磁盘空间**: 至少 10GB 可用空间
- **网络**: 需要互联网连接下载依赖

---

## 3. 环境准备

### 3.1 更新系统

```bash
# 更新软件包列表
sudo apt update

# 升级已安装的软件包
sudo apt upgrade -y

# 安装基本开发工具
sudo apt install -y build-essential
```

### 3.2 安装编译器

AAF 需要 C++17 或更高版本的编译器支持。

#### Ubuntu 22.04

```bash
# 安装 GCC 11 (默认支持 C++17)
sudo apt install -y gcc-11 g++-11

# 设置为默认编译器
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 100
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-11 100

# 验证版本
gcc --version  # 应该显示 11.x.x
g++ --version  # 应该显示 11.x.x
```

#### Ubuntu 24.04

```bash
# Ubuntu 24.04 默认 GCC 13，已支持 C++20
sudo apt install -y gcc g++

# 验证版本
gcc --version  # 应该显示 13.x.x
g++ --version  # 应该显示 13.x.x
```

### 3.3 安装 CMake

AAF 使用 CMake 作为构建系统。

```bash
# 安装 CMake 3.20 或更高版本
sudo apt install -y cmake

# 验证版本
cmake --version  # 应该 >= 3.20

# 如果需要更新版本，可以从官网下载
# wget https://github.com/Kitware/CMake/releases/download/v3.28.0/cmake-3.28.0-linux-x86_64.sh
# chmod +x cmake-3.28.0-linux-x86_64.sh
# sudo ./cmake-3.28.0-linux-x86_64.sh --prefix=/usr/local --skip-license
```

---

## 4. 依赖安装

### 4.1 Boost 库

AAF 大量使用 Boost 库，需要安装以下组件:

```bash
# 安装 Boost 核心库
sudo apt install -y libboost-all-dev

# 或者按需安装特定组件
sudo apt install -y \
    libboost-system-dev \
    libboost-thread-dev \
    libboost-filesystem-dev \
    libboost-program-options-dev \
    libboost-log-dev \
    libboost-timer-dev \
    libboost-chrono-dev \
    libboost-atomic-dev

# 验证安装
dpkg -l | grep libboost
```

### 4.2 网络相关库

```bash
# 安装 OpenSSL (用于加密通信)
sudo apt install -y libssl-dev

# 安装 libevent (用于事件循环)
sudo apt install -y libevent-dev

# 安装 libev (可选，用于高性能事件循环)
sudo apt install -y libev-dev
```

### 4.3 日志和监控

```bash
# 安装 spdlog (快速日志库)
sudo apt install -y libspdlog-dev

# 安装 glog (Google 日志库，可选)
sudo apt install -y libgoogle-glog-dev
```

### 4.4 测试框架

```bash
# 安装 Google Test
sudo apt install -y libgtest-dev

# 编译 gtest 静态库
cd /usr/src/googletest
sudo mkdir build
cd build
sudo cmake ..
sudo make
sudo make install
```

### 4.5 其他工具库

```bash
# 安装 Protobuf (用于序列化)
sudo apt install -y libprotobuf-dev protobuf-compiler

# 安装 RapidJSON (JSON 解析)
sudo apt install -y rapidjson-dev

# 安装 etcd-cpp-api (用于配置中心)
# 注意：可能需要从源码编译
```

---

## 5. 获取源码

### 5.1 解压源码包

假设源码包位于 `/home/lsz/code/work/ami/ADK_CODE/` 目录:

```bash
# 进入工作目录
cd /home/lsz/code/work/ami/ADK_CODE

# 解压源码包 (如果是 tar.gz 格式)
tar -xzf NGTP_AMI.V3.5.0 源码包.tar.gz

# 或者如果是 zip 格式
unzip NGTP_AMI.V3.5.0 源码包.zip

# 进入源码目录
cd NGTP_V3.5.0
```

### 5.2 查看目录结构

```bash
ls -la
# 应该看到以下结构:
# ├── aaf/          # AAF 工程
# ├── adk/          # ADK 工程
# └── README.md
```

---

## 6. 编译步骤

### 6.1 编译 ADK (底层依赖库)

**必须先编译 ADK，因为 AAF 依赖 ADK。**

```bash
# 进入 ADK 目录
cd adk

# 创建构建目录
mkdir build
cd build

# 配置 CMake
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_TESTS=OFF

# 编译 (使用所有 CPU 核心)
make -j$(nproc)

# 安装到系统目录 (需要 sudo)
sudo make install

# 或者安装到用户目录
# cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/.local
# make install
```

### 6.2 编译 AAF (应用框架)

```bash
# 返回上级目录，进入 AAF 目录
cd ../../aaf

# 创建构建目录
mkdir build
cd build

# 配置 CMake
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_EXAMPLES=ON \
    -DBUILD_TOOLS=ON \
    -DBUILD_TESTS=OFF

# 编译
make -j$(nproc)

# 安装
sudo make install
```

### 6.3 编译示例程序

```bash
# 在 AAF 构建目录中
cd /path/to/aaf/build

# 编译所有示例
make examples

# 或者编译特定示例
make demo
make demo2
make follower_demo

# 运行示例
./example/demo --help
```

---

## 7. 编译选项说明

### 7.1 CMake 常用选项

| 选项 | 说明 | 默认值 | 示例 |
|------|------|--------|------|
| `CMAKE_BUILD_TYPE` | 构建类型 | Release | Debug/Release/RelWithDebInfo |
| `CMAKE_INSTALL_PREFIX` | 安装前缀 | /usr/local | /opt/myapp |
| `BUILD_SHARED_LIBS` | 构建动态库 | ON | ON/OFF |
| `BUILD_STATIC_LIBS` | 构建静态库 | OFF | ON/OFF |
| `BUILD_EXAMPLES` | 编译示例 | OFF | ON/OFF |
| `BUILD_TOOLS` | 编译工具 | OFF | ON/OFF |
| `BUILD_TESTS` | 编译测试 | OFF | ON/OFF |
| `ENABLE_LOGGING` | 启用日志 | ON | ON/OFF |
| `ENABLE_HA` | 启用 HA 功能 | ON | ON/OFF |

### 7.2 编译类型对比

```bash
# Debug 版本 (包含调试信息，无优化)
cmake .. -DCMAKE_BUILD_TYPE=Debug
# 特点：-g -O0，适合开发调试

# Release 版本 (完全优化，无调试信息)
cmake .. -DCMAKE_BUILD_TYPE=Release
# 特点：-O3 -DNDEBUG，适合生产环境

# RelWithDebInfo (优化 + 调试信息)
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
# 特点：-O2 -g，适合性能分析

# MinSizeRel (最小体积)
cmake .. -DCMAKE_BUILD_TYPE=MinSizeRel
# 特点：-Os，适合嵌入式
```

---

## 8. 环境变量配置

### 8.1 临时设置 (当前终端)

```bash
# 设置库搜索路径
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# 设置 pkg-config 路径
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH

# 设置头文件搜索路径
export CPLUS_INCLUDE_PATH=/usr/local/include:$CPLUS_INCLUDE_PATH
```

### 8.2 永久设置

```bash
# 编辑 ~/.bashrc 或 ~/.zshrc
vim ~/.bashrc

# 添加以下内容
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH

# 使配置生效
source ~/.bashrc
```

### 8.3 验证配置

```bash
# 检查库是否可被找到
ldconfig -p | grep libadk
ldconfig -p | grep libaaf

# 检查 pkg-config
pkg-config --libs adk
pkg-config --libs aaf
```

---

## 9. 完整编译脚本

### 9.1 一键编译脚本

创建文件 `build_all.sh`:

```bash
#!/bin/bash

set -e  # 遇到错误立即退出

echo "========================================="
echo "Starting AAF Build Process"
echo "========================================="

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 打印带颜色的消息
info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查依赖
check_dependencies() {
    info "Checking dependencies..."
    
    local deps=(cmake gcc g++ make)
    for dep in "${deps[@]}"; do
        if ! command -v $dep &> /dev/null; then
            error "$dep is not installed. Please install it first."
            exit 1
        fi
    done
    
    info "All dependencies are installed."
}

# 编译 ADK
build_adk() {
    info "Building ADK..."
    
    cd adk || exit 1
    
    mkdir -p build
    cd build
    
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DBUILD_SHARED_LIBS=ON
    
    make -j$(nproc)
    sudo make install
    
    cd ../..
    
    info "ADK build completed successfully."
}

# 编译 AAF
build_aaf() {
    info "Building AAF..."
    
    cd aaf || exit 1
    
    mkdir -p build
    cd build
    
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DBUILD_SHARED_LIBS=ON \
        -DBUILD_EXAMPLES=ON \
        -DBUILD_TOOLS=ON
    
    make -j$(nproc)
    sudo make install
    
    cd ../..
    
    info "AAF build completed successfully."
}

# 主函数
main() {
    check_dependencies
    
    info "Starting build process..."
    
    build_adk
    build_aaf
    
    info "========================================="
    info "Build completed successfully!"
    info "========================================="
    info ""
    info "Installed libraries:"
    info "  - libadk.so -> /usr/local/lib/libadk.so"
    info "  - libaaf.so -> /usr/local/lib/libaaf.so"
    info ""
    info "To use the libraries, add to your environment:"
    info "  export LD_LIBRARY_PATH=/usr/local/lib:\$LD_LIBRARY_PATH"
}

# 执行主函数
main "$@"
```

使用方法:

```bash
# 添加执行权限
chmod +x build_all.sh

# 执行编译
./build_all.sh
```

### 9.2 Docker 编译环境

创建 `Dockerfile`:

```dockerfile
FROM ubuntu:22.04

LABEL maintainer="your-email@example.com"

# 避免交互式提示
ENV DEBIAN_FRONTEND=noninteractive

# 安装基础工具
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    curl \
    && rm -rf /var/lib/apt/lists/*

# 安装 Boost
RUN apt-get update && apt-get install -y \
    libboost-all-dev \
    && rm -rf /var/lib/apt/lists/*

# 安装其他依赖
RUN apt-get update && apt-get install -y \
    libssl-dev \
    libevent-dev \
    libspdlog-dev \
    libprotobuf-dev \
    protobuf-compiler \
    && rm -rf /var/lib/apt/lists/*

# 设置工作目录
WORKDIR /workspace

# 复制源码
COPY . /workspace/

# 编译
RUN ./build_all.sh

# 设置环境变量
ENV LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# 默认命令
CMD ["/bin/bash"]
```

构建镜像:

```bash
docker build -t aaf-builder:latest .
docker run -it --rm aaf-builder
```

---

## 10. 常见问题解决

### Q1: 找不到 Boost 库

**错误信息**:
```
CMake Error: Could not find Boost
```

**解决方案**:
```bash
# 重新安装 Boost
sudo apt install --reinstall libboost-all-dev

# 或者手动指定 Boost 路径
cmake .. -DBOOST_ROOT=/usr/local -DBOOST_INCLUDEDIR=/usr/local/include
```

### Q2: 链接错误 undefined reference

**错误信息**:
```
undefined reference to `adk::Logger::Instance()'
```

**解决方案**:
```bash
# 确保库已正确安装
ldconfig -p | grep libadk

# 如果没有找到，添加库路径
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
sudo ldconfig

# 或者在 CMakeLists.txt 中显式链接
target_link_libraries(your_target PRIVATE adk aaf)
```

### Q3: 编译时内存不足

**错误信息**:
```
virtual memory exhausted: Cannot allocate memory
```

**解决方案**:
```bash
# 减少并行编译任务数
make -j2  # 使用 2 个核心而不是全部

# 或者增加 swap 空间
sudo fallocate -l 4G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
```

### Q4: CMake 版本过低

**错误信息**:
```
CMake 3.20 or higher is required. You are running version 3.16.3
```

**解决方案**:
```bash
# 从源码编译安装最新 CMake
wget https://github.com/Kitware/CMake/releases/download/v3.28.0/cmake-3.28.0.tar.gz
tar -xzf cmake-3.28.0.tar.gz
cd cmake-3.28.0
./bootstrap
make -j$(nproc)
sudo make install

# 验证版本
/usr/local/bin/cmake --version
```

---

## 11. 验证安装

### 11.1 检查库文件

```bash
# 查看已安装的库
ls -la /usr/local/lib/libadk*
ls -la /usr/local/lib/libaaf*

# 检查库的依赖关系
ldd /usr/local/lib/libaaf.so
```

### 11.2 编译测试程序

创建 `test_install.cpp`:

```cpp
#include <aaf/generic_ami_application.h>
#include <adk/logger.h>

int main() {
    ADK_LOG_INFO("AAF installation test");
    return 0;
}
```

编译:

```bash
g++ -o test_install test_install.cpp \
    -I/usr/local/include \
    -L/usr/local/lib \
    -laaf -ladk \
    -lboost_system -lboost_thread -lpthread

./test_install
```

### 11.3 运行示例

```bash
# 运行 AAF 示例
cd /path/to/aaf/build/example
./demo --help

# 应该看到帮助信息输出
```

---

## 12. 性能优化建议

### 12.1 编译器优化选项

```bash
# 在 CMakeLists.txt 中添加
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -flto")

# 或者手动设置
cmake .. -DCMAKE_CXX_FLAGS="-O3 -march=native -flto"
```

### 12.2 链接时优化 (LTO)

```bash
# 启用 LTO
cmake .. -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON

# 或者在 CMakeLists.txt 中
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
```

### 12.3 使用 ccache 加速重复编译

```bash
# 安装 ccache
sudo apt install ccache

# 配置 ccache
export CC="ccache gcc"
export CXX="ccache g++"

# 重新编译
cmake ..
make
```

---

## 13. 总结

完成以上步骤后，您应该成功在 Ubuntu 22.04 或 24.04 上编译并安装了 AAF 工程。

**检查清单**:
- ✅ 编译器版本 >= GCC 11 (Ubuntu 22.04) 或 GCC 13 (Ubuntu 24.04)
- ✅ CMake 版本 >= 3.20
- ✅ Boost 库已安装
- ✅ ADK 库编译并安装成功
- ✅ AAF 库编译并安装成功
- ✅ 示例程序可以正常运行

**下一步**:
- 阅读 `AAF_产品使用.md` 学习如何使用 AAF 开发应用
- 参考 `example/` 目录下的示例代码
- 查看 `AAF_技术实现.md` 深入了解框架架构

祝您开发愉快！🎉
