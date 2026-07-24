# ADK (Application Development Kit) 编译配置指南

## 1. 概述

本文档详细说明如何在 Ubuntu 22.04 或 Ubuntu 24.04 系统上编译 ADK 工程。ADK 是底层基础库，为上层应用框架提供核心支撑功能。

---

## 2. 系统要求

### 2.1 操作系统兼容性

| 操作系统 | 推荐版本 | 内核版本 | 状态 |
|---------|---------|---------|------|
| Ubuntu 22.04 LTS | 22.04.x | 5.15+ | ✅ 推荐 |
| Ubuntu 24.04 LTS | 24.04.x | 6.5+ | ✅ 支持 |
| Debian 11/12 | 11.x/12.x | 5.10+/6.1+ | ✅ 可选 |

### 2.2 硬件要求

- **CPU**: 2 核以上 (推荐 4 核)
- **内存**: 4GB 以上 (推荐 8GB)
- **磁盘空间**: 至少 5GB 可用空间
- **网络**: 需要互联网连接下载依赖

---

## 3. 环境准备

### 3.1 系统更新

```bash
# 更新软件包列表
sudo apt update

# 升级系统
sudo apt upgrade -y

# 安装基本开发工具
sudo apt install -y build-essential
```

### 3.2 安装编译器

#### Ubuntu 22.04

```bash
# 安装 GCC 11 (支持 C++17)
sudo apt install -y gcc-11 g++-11

# 设置为默认编译器
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 100
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-11 100

# 验证
gcc --version  # 应显示 11.x.x
g++ --version  # 应显示 11.x.x
```

#### Ubuntu 24.04

```bash
# Ubuntu 24.04 默认 GCC 13
sudo apt install -y gcc g++

# 验证
gcc --version  # 应显示 13.x.x
g++ --version  # 应显示 13.x.x
```

### 3.3 安装 CMake

```bash
# 安装 CMake
sudo apt install -y cmake

# 验证版本
cmake --version  # 应 >= 3.16

# 如需更新版本，从官网下载
wget https://github.com/Kitware/CMake/releases/download/v3.28.0/cmake-3.28.0-linux-x86_64.sh
chmod +x cmake-3.28.0-linux-x86_64.sh
sudo ./cmake-3.28.0-linux-x86_64.sh --prefix=/usr/local --skip-license
```

---

## 4. 依赖安装

### 4.1 Boost 库 (必需)

ADK 大量使用 Boost 库:

```bash
# 安装完整的 Boost 开发包
sudo apt install -y libboost-all-dev

# 或按需安装特定组件
sudo apt install -y \
    libboost-system-dev \
    libboost-thread-dev \
    libboost-filesystem-dev \
    libboost-program-options-dev \
    libboost-log-dev \
    libboost-timer-dev \
    libboost-chrono-dev \
    libboost-atomic-dev \
    libboost-date-time-dev \
    libboost-regex-dev

# 验证安装
dpkg -l | grep libboost | wc -l
# 应该显示多个已安装的 boost 包
```

### 4.2 网络相关库

```bash
# OpenSSL (加密通信)
sudo apt install -y libssl-dev

# libevent (事件循环)
sudo apt install -y libevent-dev

# libev (高性能事件循环，可选)
sudo apt install -y libev-dev

# libuv (异步 IO 库，可选)
sudo apt install -y libuv1-dev
```

### 4.3 日志库

```bash
# spdlog (快速日志库)
sudo apt install -y libspdlog-dev

# glog (Google 日志库，可选)
sudo apt install -y libgoogle-glog-dev
```

### 4.4 测试框架

```bash
# Google Test
sudo apt install -y libgtest-dev

# 编译 gtest
cd /usr/src/googletest
sudo mkdir -p build
cd build
sudo cmake ..
sudo make
sudo make install

# 验证
ls /usr/local/lib/libgtest*
```

### 4.5 其他工具库

```bash
# Protobuf (序列化)
sudo apt install -y libprotobuf-dev protobuf-compiler

# RapidJSON (JSON 解析)
sudo apt install -y rapidjson-dev

# zlib (压缩)
sudo apt install -y zlib1g-dev

# lz4 (快速压缩)
sudo apt install -y liblz4-dev
```

---

## 5. 获取源码

### 5.1 解压源码

```bash
# 进入工作目录
cd /home/lsz/code/work/ami/ADK_CODE

# 解压源码包
tar -xzf NGTP_AMI.V3.5.0 源码包.tar.gz

# 进入 ADK 目录
cd NGTP_V3.5.0/adk
```

### 5.2 目录结构

```bash
ls -la
# 应该看到:
# ├── 3rd/              # 第三方库
# ├── benchmark_test/   # 性能测试
# ├── code/             # 核心代码
# │   ├── example/      # 示例
# │   ├── include/      # 头文件
# │   ├── src/          # 源文件
# │   ├── test/         # 测试代码
# │   └── tools/        # 工具程序
# ├── CMakeLists.txt    # CMake 配置
# └── README.md
```

---

## 6. 编译步骤

### 6.1 基础编译

```bash
# 创建构建目录
mkdir -p build
cd build

# 配置 CMake
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DBUILD_SHARED_LIBS=ON

# 编译
make -j$(nproc)

# 安装
sudo make install
```

### 6.2 完整编译选项

```bash
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_STATIC_LIBS=OFF \
    -DBUILD_EXAMPLES=ON \
    -DBUILD_TOOLS=ON \
    -DBUILD_TESTS=OFF \
    -DBUILD_BENCHMARKS=OFF \
    -DENABLE_LOGGING=ON \
    -DENABLE_HA=ON \
    -DENABLE_SSL=ON
```

### 6.3 编译示例程序

```bash
# 编译所有示例
make examples

# 编译特定示例
make example_tcp_server
make example_http_server

# 运行示例
./example/tcp_server --help
```

### 6.4 编译测试

```bash
# 启用测试
cmake .. -DBUILD_TESTS=ON

# 编译测试
make tests

# 运行测试
ctest

# 或者详细输出
ctest --verbose
```

---

## 7. 编译配置详解

### 7.1 CMake 选项列表

| 选项 | 说明 | 默认值 | 可选值 |
|------|------|--------|--------|
| `CMAKE_BUILD_TYPE` | 构建类型 | Release | Debug/Release/RelWithDebInfo/MinSizeRel |
| `CMAKE_INSTALL_PREFIX` | 安装前缀 | /usr/local | 任意路径 |
| `BUILD_SHARED_LIBS` | 构建动态库 | ON | ON/OFF |
| `BUILD_STATIC_LIBS` | 构建静态库 | OFF | ON/OFF |
| `BUILD_EXAMPLES` | 编译示例 | OFF | ON/OFF |
| `BUILD_TOOLS` | 编译工具 | OFF | ON/OFF |
| `BUILD_TESTS` | 编译测试 | OFF | ON/OFF |
| `BUILD_BENCHMARKS` | 编译性能测试 | OFF | ON/OFF |
| `ENABLE_LOGGING` | 启用日志 | ON | ON/OFF |
| `ENABLE_HA` | 启用 HA 功能 | ON | ON/OFF |
| `ENABLE_SSL` | 启用 SSL 支持 | ON | ON/OFF |
| `ENABLE_COMPRESSION` | 启用压缩 | ON | ON/OFF |

### 7.2 构建类型对比

```bash
# Debug - 开发调试
cmake .. -DCMAKE_BUILD_TYPE=Debug
# 编译选项：-g -O0
# 特点：包含完整调试信息，无优化，运行慢但便于调试

# Release - 生产发布
cmake .. -DCMAKE_BUILD_TYPE=Release
# 编译选项：-O3 -DNDEBUG
# 特点：完全优化，无调试信息，性能最佳

# RelWithDebInfo - 性能分析
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
# 编译选项：-O2 -g
# 特点：优化 + 调试信息，适合性能分析

# MinSizeRel - 最小体积
cmake .. -DCMAKE_BUILD_TYPE=MinSizeRel
# 编译选项：-Os
# 特点：优化体积，适合嵌入式
```

---

## 8. 高级编译选项

### 8.1 自定义编译器标志

```bash
# 添加额外的编译选项
cmake .. \
    -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" \
    -DCMAKE_CXX_FLAGS_RELEASE="-O3 -march=native -flto"

# 或者在 CMakeLists.txt 中设置
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
```

### 8.2 链接时优化 (LTO)

```bash
# 启用 LTO (需要 GCC 4.9+)
cmake .. -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON

# 或者手动设置
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
```

### 8.3 地址 sanitizer

```bash
# 启用 AddressSanitizer (检测内存错误)
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
```

### 8.4 ThreadSanitizer

```bash
# 启用 ThreadSanitizer (检测数据竞争)
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=thread" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
```

---

## 9. 环境变量配置

### 9.1 临时配置 (当前会话)

```bash
# 库搜索路径
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# pkg-config 路径
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH

# 头文件路径
export CPLUS_INCLUDE_PATH=/usr/local/include:$CPLUS_INCLUDE_PATH

# 可执行文件路径
export PATH=/usr/local/bin:$PATH
```

### 9.2 永久配置

```bash
# 编辑 shell 配置文件
vim ~/.bashrc  # 或 ~/.zshrc

# 添加以下内容
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
export CPLUS_INCLUDE_PATH=/usr/local/include:$CPLUS_INCLUDE_PATH
export PATH=/usr/local/bin:$PATH

# 使配置生效
source ~/.bashrc
```

### 9.3 系统级配置

```bash
# 创建 ldconfig 配置文件
sudo tee /etc/ld.so.conf.d/adk.conf << EOF
/usr/local/lib
EOF

# 更新动态链接库缓存
sudo ldconfig

# 验证
ldconfig -p | grep libadk
```

---

## 10. 一键编译脚本

创建 `build_adk.sh`:

```bash
#!/bin/bash

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info() { echo -e "${GREEN}[INFO]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; }
step() { echo -e "${BLUE}[STEP]${NC} $1"; }

# 检查依赖
check_dependencies() {
    step "Checking dependencies..."
    
    local deps=(cmake gcc g++ make)
    for dep in "${deps[@]}"; do
        if ! command -v $dep &> /dev/null; then
            error "$dep is not installed"
            exit 1
        fi
    done
    
    info "All dependencies found"
}

# 清理旧的构建
clean_build() {
    step "Cleaning old build..."
    rm -rf build
    mkdir -p build
    info "Build directory cleaned"
}

# 配置 CMake
configure_cmake() {
    step "Configuring CMake..."
    
    cd build
    
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DBUILD_SHARED_LIBS=ON \
        -DBUILD_EXAMPLES=ON \
        -DBUILD_TOOLS=ON \
        -DBUILD_TESTS=OFF
    
    info "CMake configuration completed"
}

# 编译
build_project() {
    step "Building project..."
    
    local cores=$(nproc)
    info "Using $cores CPU cores"
    
    make -j$cores
    
    info "Build completed successfully"
}

# 安装
install_project() {
    step "Installing..."
    
    sudo make install
    
    info "Installation completed"
}

# 验证安装
verify_installation() {
    step "Verifying installation..."
    
    if [ -f "/usr/local/lib/libadk.so" ]; then
        info "Library installed at /usr/local/lib/libadk.so"
    else
        error "Library not found!"
        exit 1
    fi
    
    if [ -f "/usr/local/include/adk/logger.h" ]; then
        info "Headers installed at /usr/local/include/adk/"
    else
        error "Headers not found!"
        exit 1
    fi
    
    info "Installation verified successfully"
}

# 主函数
main() {
    info "========================================="
    info "ADK Build Script"
    info "========================================="
    
    check_dependencies
    clean_build
    configure_cmake
    build_project
    install_project
    verify_installation
    
    info "========================================="
    info "Build completed successfully!"
    info "========================================="
    info ""
    info "Installed components:"
    info "  - Library: /usr/local/lib/libadk.so"
    info "  - Headers: /usr/local/include/adk/"
    info "  - Tools: /usr/local/bin/"
    info ""
    info "Environment setup:"
    info "  export LD_LIBRARY_PATH=/usr/local/lib:\$LD_LIBRARY_PATH"
}

main "$@"
```

使用方法:

```bash
chmod +x build_adk.sh
./build_adk.sh
```

---

## 11. Docker 编译环境

### 11.1 Dockerfile

```dockerfile
FROM ubuntu:22.04

LABEL maintainer="your-email@example.com"
LABEL description="ADK Build Environment"

# 避免交互
ENV DEBIAN_FRONTEND=noninteractive

# 安装基础工具
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    curl \
    vim \
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
    rapidjson-dev \
    zlib1g-dev \
    liblz4-dev \
    && rm -rf /var/lib/apt/lists/*

# 设置工作目录
WORKDIR /workspace

# 复制源码
COPY . /workspace/

# 编译
RUN ./build_adk.sh

# 设置环境变量
ENV LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
ENV PATH=/usr/local/bin:$PATH

# 默认命令
CMD ["/bin/bash"]
```

### 11.2 构建和使用

```bash
# 构建镜像
docker build -t adk-builder:latest .

# 运行容器
docker run -it --rm adk-builder

# 或者挂载本地目录
docker run -it --rm \
    -v $(pwd):/workspace \
    adk-builder

# 导出编译产物
docker create --name adk-temp adk-builder:latest
docker cp adk-temp:/usr/local/lib/libadk.so ./
docker rm adk-temp
```

---

## 12. 常见问题解决

### Q1: Boost 库找不到

**错误**:
```
CMake Error: Could not find Boost
```

**解决**:
```bash
# 重新安装
sudo apt install --reinstall libboost-all-dev

# 或指定路径
cmake .. -DBOOST_ROOT=/usr/local -DBOOST_INCLUDEDIR=/usr/local/include
```

### Q2: 链接错误

**错误**:
```
undefined reference to `adk::xxx'
```

**解决**:
```bash
# 确保库已安装
ldconfig -p | grep libadk

# 添加库路径
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
sudo ldconfig
```

### Q3: 编译内存不足

**错误**:
```
virtual memory exhausted: Cannot allocate memory
```

**解决**:
```bash
# 减少并行任务
make -j2

# 增加 swap
sudo fallocate -l 4G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
```

### Q4: CMake 版本过低

**错误**:
```
CMake 3.16 or higher is required
```

**解决**:
```bash
# 从源码编译最新 CMake
wget https://github.com/Kitware/CMake/releases/download/v3.28.0/cmake-3.28.0.tar.gz
tar -xzf cmake-3.28.0.tar.gz
cd cmake-3.28.0
./bootstrap
make -j$(nproc)
sudo make install

# 验证
/usr/local/bin/cmake --version
```

---

## 13. 验证安装

### 13.1 检查文件

```bash
# 查看库文件
ls -la /usr/local/lib/libadk*

# 查看头文件
ls -la /usr/local/include/adk/

# 查看工具
ls -la /usr/local/bin/adk-*
```

### 13.2 编译测试程序

创建 `test_adk.cpp`:

```cpp
#include <adk/logger.h>
#include <adk/property.h>
#include <iostream>

int main() {
    ADK_LOG_INFO("ADK installation test");
    
    adk::Property config;
    config.SetValue("test_key", "test_value");
    
    std::cout << "Test passed!" << std::endl;
    return 0;
}
```

编译:

```bash
g++ -o test_adk test_adk.cpp \
    -I/usr/local/include \
    -L/usr/local/lib \
    -ladk \
    -lboost_system -lboost_thread -lpthread

./test_adk
```

### 13.3 运行示例

```bash
cd /path/to/adk/build/example
./tcp_server --help

# 应该看到帮助信息
```

---

## 14. 性能优化

### 14.1 编译器优化

```bash
# 启用最高优化级别
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-O3 -march=native -flto"

# 链接时优化
cmake .. -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
```

### 14.2 使用 ccache

```bash
# 安装 ccache
sudo apt install ccache

# 配置
export CC="ccache gcc"
export CXX="ccache g++"

# 重新编译
cmake ..
make
```

---

## 15. 总结

完成以上步骤后，您应该成功在 Ubuntu 上编译并安装了 ADK 工程。

**检查清单**:
- ✅ 编译器版本符合要求
- ✅ CMake 版本 >= 3.16
- ✅ Boost 库已安装
- ✅ ADK 库编译成功
- ✅ 库文件安装到正确位置
- ✅ 测试程序可以正常编译和运行

**下一步**:
- 阅读 `ADK_产品使用.md` 学习 API 使用
- 参考 `example/` 目录下的示例代码
- 查看 `ADK_技术实现.md` 深入了解架构

祝您开发愉快！🎉
